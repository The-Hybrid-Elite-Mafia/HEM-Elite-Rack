// ELITE EQ, RESTORER and the spectrum display: what each must actually do to the sound,
// measured, plus the rules every HEM effect follows (silence stays silence, no allocation on
// the audio thread, same result for any block size, finite output, constant latency).
#include "Harness.hpp"
#include "Signals.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/ConsoleEq.hpp"
#include "hem/rack/Dynamics.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hybrid/media/Loudness.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hem/rack/Tools.hpp"
#include "hem/rack/ClassicEqs.hpp"
#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/ColorFx.hpp"
#include "hem/rack/Space.hpp"
#include "hem/rack/Movement.hpp"
#include "hem/rack/Specialty.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <set>
#include <string>

namespace hem::test {
namespace {

using namespace hem::rack;
constexpr double kFs = 48000.0;

// The rules every plug-in follows.
void commonRules(const PluginInfo& info) {
    auto e = info.create();
    const auto ps = e->params();
    std::set<std::string> ids;
    bool ranges = true, roundTrip = true;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i];
        ids.insert(p.id);
        if (!(p.min < p.max) || p.def < p.min || p.def > p.max || (p.taper == hc::fx::Taper::Log && p.min <= 0)) ranges = false;
        for (float nrm : {0.0f, 0.37f, 1.0f}) {
            const float v = hc::fx::toPlain(p, nrm);
            float back = 0;
            if (!hc::fx::parseValue(p, hc::fx::formatValue(p, v), back)) { roundTrip = false; continue; }
            const float tol = p.taper == hc::fx::Taper::Log ? v * 0.02f + 0.6f : (p.max - p.min) * 0.02f + 0.06f;
            if (std::fabs(back - v) > tol) roundTrip = false;
        }
    }
    CHECK(!ps.empty() && ps.size() <= static_cast<std::size_t>(hc::fx::Effect::kMaxParams));
    CHECK(ids.size() == ps.size() && ranges && roundTrip);

    e->prepare(kFs, 512);
    const std::uint32_t lat = e->latency();
    // silence stays silence, also with everything switched on
    Stereo z(48000 * 2);
    run(*e, z);
    CHECK(peak(z) < 1e-6f);
    // Every parameter swept at random while loud noise plays: finite, and not one allocation.
    if (auto* sp = dynamic_cast<SpectrumSource*>(e.get())) { sp->spectrumBefore().setActive(true); sp->spectrumAfter().setActive(true); }
    Stereo s = noise(3.0, 0.9f, 11);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::uint64_t allocs = 0;
    {
        AllocScope scope;
        for (std::size_t i = 0; i < s.size(); i += 256) {
            for (int k = 0; k < 6; ++k) {
                const int pi = static_cast<int>(u(rng) * static_cast<float>(ps.size() - 1));
                e->set(pi, hc::fx::toPlain(ps[static_cast<std::size_t>(pi)], u(rng)));
            }
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(256, s.size() - i));
            e->process(s.l.data() + i, s.r.data() + i, n);
        }
        allocs = scope.count();
    }
    std::printf("  %s: %zu parameters, latency %u samples, allocations while playing: %llu\n", info.name, ps.size(), lat,
                static_cast<unsigned long long>(allocs));
    CHECK(allocs == 0);
    CHECK(finite(s));
    CHECK(e->latency() == lat);
    // silence again after setDefaults + reset
    e->setDefaults();
    e->reset();
    Stereo z2(48000);
    run(*e, z2);
    CHECK(peak(z2) < 1e-6f);
}

// Same result whatever the host's block size.
template <class Setup>
double blockSizeDifference(const PluginInfo& info, Setup setup, const Stereo& in) {
    auto a = info.create(), b = info.create();
    setup(*a);
    setup(*b);
    a->prepare(kFs, 512);
    b->prepare(kFs, 512);
    Stereo x = in, y = in;
    run(*a, x, 512);
    run(*b, y, 1);
    double d = 0;
    for (std::size_t i = 0; i < x.size(); ++i) d = std::max({d, static_cast<double>(std::fabs(x.l[i] - y.l[i])), static_cast<double>(std::fabs(x.r[i] - y.r[i]))});
    return d;
}

void setBand(hc::fx::Effect& e, int b, eq::Type t, double hz, double gain, double q, int slopeIdx = 3, eq::Stereo st = eq::Stereo::Stereo) {
    using namespace eq;
    e.set(bandParam(b, On), 1);
    e.set(bandParam(b, TypeC), static_cast<float>(static_cast<int>(t)));
    e.set(bandParam(b, Freq), static_cast<float>(hz));
    e.set(bandParam(b, Gain), static_cast<float>(gain));
    e.set(bandParam(b, Q), static_cast<float>(q));
    e.set(bandParam(b, Slope), static_cast<float>(slopeIdx));
    e.set(bandParam(b, StereoC), static_cast<float>(static_cast<int>(st)));
}

// Gain (dB) the effect gives a sine at hz (steady state, left channel).
double gainAt(hc::fx::Effect& e, double hz, float amp = 0.25f) {
    e.reset();
    Stereo s = sine(hz, 1.0, amp);
    run(e, s);
    return db(toneAmp(s.l, hz, 24000, 24000) / amp);
}

} // namespace

void testEliteEq() {
    std::printf("[eq] ELITE EQ: filter shapes, mid / side, dynamic bands, character, auto gain\n");
    const PluginInfo* info = findPlugin("ELITE EQ");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    using namespace eq;

    // Every filter type does exactly what its curve (drawn on the faceplate) says.
    struct Case { Type t; double hz, gain, q; int slope; };
    const Case cases[] = {{Type::Bell, 1000, 6, 1.0, 3},     {Type::Bell, 250, -9, 4.0, 3},   {Type::LowShelf, 120, 5, 0.7, 3},
                          {Type::HighShelf, 8000, -6, 0.7, 3}, {Type::LowCut, 100, 0, 0.7071, 3}, {Type::LowCut, 60, 0, 1.4, 1},
                          {Type::HighCut, 12000, 0, 0.7071, 5}, {Type::HighCut, 5000, 0, 0.7071, 0}, {Type::Notch, 3000, 0, 8.0, 3},
                          {Type::Tilt, 1000, 6, 1.0, 3}};
    double worst = 0;
    for (const Case& c : cases) {
        auto e = info->create();
        e->prepare(kFs, 512);
        setBand(*e, 0, c.t, c.hz, c.gain, c.q, c.slope);
        Band b = bandOf(*e, 0);
        const Sections sec = design(b, kFs);
        for (double f : {40.0, 100.0, 300.0, 1000.0, 2500.0, 6000.0, 12000.0}) {
            const double want = magnitudeDb(sec, kFs, f);
            if (want < -60.0) continue;   // deep in a cut: below what a 1 s tone can show cleanly
            worst = std::max(worst, std::fabs(gainAt(*e, f) - want));
        }
    }
    std::printf("  10 filter shapes: measured vs drawn curve, worst difference %.3f dB\n", worst);
    CHECK(worst < 0.1);
    {   // the textbook values
        Band bell{true, Type::Bell, 1000, 6, 1.0, 24, eq::Stereo::Stereo};
        Band cut{true, Type::LowCut, 100, 0, 0.7071, 24, eq::Stereo::Stereo};
        const Sections sb = design(bell, kFs), sc = design(cut, kFs);
        std::printf("  BELL +6 dB @ 1 kHz: %.2f dB; LOW CUT 100 Hz 24 dB/oct: %.2f dB @ 100 Hz, %.1f dB @ 25 Hz\n", magnitudeDb(sb, kFs, 1000),
                    magnitudeDb(sc, kFs, 100), magnitudeDb(sc, kFs, 25));
        CHECK(std::fabs(magnitudeDb(sb, kFs, 1000) - 6.0) < 0.01);
        CHECK(std::fabs(magnitudeDb(sc, kFs, 100) + 3.01) < 0.05 && std::fabs(magnitudeDb(sc, kFs, 25) + 48.0) < 1.0);
    }

    // Mid / side / left: a SIDE cut leaves a mono signal alone and turns the stereo part down.
    {
        auto e = info->create();
        e->prepare(kFs, 512);
        setBand(*e, 0, Type::Bell, 1000, -12, 1.0, 3, eq::Stereo::Side);
        Stereo mono = sine(1000, 1.0, 0.25f), wide = sine(1000, 1.0, 0.25f, kFs, true);
        run(*e, mono);
        e->reset();
        run(*e, wide);
        const double m = db(toneAmp(mono.l, 1000, 24000, 24000) / 0.25), w = db(toneAmp(wide.l, 1000, 24000, 24000) / 0.25);
        std::printf("  SIDE bell -12 dB: mono signal %.2f dB, anti-phase signal %.2f dB\n", m, w);
        CHECK(std::fabs(m) < 0.05 && std::fabs(w + 12.0) < 0.1);
        auto l = info->create();
        l->prepare(kFs, 512);
        setBand(*l, 0, Type::Bell, 1000, 8, 1.0, 3, eq::Stereo::Left);
        Stereo s = sine(1000, 1.0, 0.25f);
        run(*l, s);
        const double gl = db(toneAmp(s.l, 1000, 24000, 24000) / 0.25), gr = db(toneAmp(s.r, 1000, 24000, 24000) / 0.25);
        std::printf("  LEFT bell +8 dB: left %.2f dB, right %.2f dB\n", gl, gr);
        CHECK(std::fabs(gl - 8.0) < 0.1 && std::fabs(gr) < 0.05);
    }

    // A dynamic band leaves a quiet note alone and only cuts it when it gets loud.
    {
        auto e = info->create();
        e->prepare(kFs, 512);
        setBand(*e, 0, Type::Bell, 3000, 0, 2.0);
        e->set(bandParam(0, Dyn), 1);
        e->set(bandParam(0, Threshold), -30);
        e->set(bandParam(0, Range), -9);
        e->set(bandParam(0, Attack), 5);
        e->set(bandParam(0, Release), 100);
        const double quiet = gainAt(*e, 3000, 0.01f);   // -40 dBFS: under the threshold
        const double loud = gainAt(*e, 3000, 0.5f);     // -6 dBFS: far over it
        auto* view = dynamic_cast<EliteEqView*>(e.get());
        const float shown = view ? view->dynamicGainDb(0) : 0.0f;
        const double other = gainAt(*e, 300, 0.5f);      // a loud note elsewhere barely triggers it
        std::printf("  DYNAMIC bell (thr -30, range -9) @ 3 kHz: quiet %.2f dB, loud %.2f dB (faceplate shows %.2f); loud 300 Hz %.2f dB\n",
                    quiet, loud, shown, other);
        CHECK(std::fabs(quiet) < 0.2);
        CHECK(loud < -8.0 && loud > -9.5);
        CHECK(shown < -8.5f && shown >= -9.0f);
        CHECK(std::fabs(other) < 0.5);
    }

    // CHARACTER: clean is the input (delayed by the constant latency); console adds odd harmonics, tube even.
    {
        auto e = info->create();
        e->prepare(kFs, 512);
        Stereo s = noise(0.5, 0.5f, 3), ref = s;
        run(*e, s);
        const std::uint32_t lat = e->latency();
        double d = 0;
        for (std::size_t i = 0; i + lat < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i + lat] - ref.l[i])));
        std::printf("  CLEAN, no bands: output = input delayed %u samples (max difference %.2e)\n", lat, d);
        CHECK(d < 1e-6 && lat == 23);
        auto harmonics = [&](Char c) {
            auto x = info->create();
            x->prepare(kFs, 512);
            x->set(Character, static_cast<float>(static_cast<int>(c)));
            x->set(Drive, 100);
            x->reset();
            Stereo t = sine(1000, 1.0, 0.9f);
            run(*x, t);
            const double f1 = toneAmp(t.l, 1000, 24000, 24000);
            return std::pair<double, double>{db(toneAmp(t.l, 2000, 24000, 24000) / f1), db(toneAmp(t.l, 3000, 24000, 24000) / f1)};
        };
        const auto con = harmonics(Char::Console), tube = harmonics(Char::Tube);
        std::printf("  CONSOLE drive 100%% @ -1 dBFS: 2nd %.1f dB, 3rd %.1f dB;  TUBE: 2nd %.1f dB, 3rd %.1f dB\n", con.first, con.second, tube.first,
                    tube.second);
        CHECK(con.second > -45.0 && con.first < con.second - 30.0);
        CHECK(tube.first > -45.0);
    }

    // AUTO GAIN brings the loudness back to the input's after a big boost.
    {
        auto e = info->create();
        e->prepare(kFs, 512);
        setBand(*e, 0, Type::HighShelf, 2000, 12, 0.7);
        e->set(AutoGain, 1);
        Stereo s = noise(12.0, 0.1f, 9), ref = s;
        run(*e, s);
        const std::size_t from = static_cast<std::size_t>(9 * kFs), len = static_cast<std::size_t>(3 * kFs);
        // compare K-weighted-ish loudness: the 2..8 kHz boost would make plain RMS meaningless
        auto weighted = [&](const std::vector<float>& x) {
            hc::fx::Biquad hs, hp;
            hs.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighShelf, kFs, 1500, 0.7071, 4);
            hp.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, kFs, 38, 0.5);
            double sum = 0;
            for (std::size_t i = 0; i < from + len && i < x.size(); ++i) {
                const float v = hp.tick(hs.tick(x[i]));
                if (i >= from) sum += static_cast<double>(v) * v;
            }
            return 10.0 * std::log10(sum / static_cast<double>(len));
        };
        auto* view = dynamic_cast<EliteEqView*>(e.get());
        const double diff = weighted(s.l) - weighted(ref.l);
        std::printf("  AUTO GAIN after a +12 dB high shelf: loudness change %.2f dB (auto gain %.1f dB)\n", diff, view ? view->autoGainDb() : 0.0f);
        CHECK(std::fabs(diff) < 1.0);
    }

    // Same output for any block size, with bands, a dynamic band and character on.
    {
        auto setup = [](hc::fx::Effect& e) {
            setBand(e, 0, Type::Bell, 2000, 4, 1.5);
            setBand(e, 1, Type::LowCut, 40, 0, 0.7071, 3);
            setBand(e, 2, Type::HighShelf, 9000, -3, 0.7, 3, eq::Stereo::Side);
            e.set(bandParam(2, Dyn), 1);
            e.set(Character, 2);
        };
        const double d = blockSizeDifference(*info, setup, noise(1.0, 0.5f, 21));
        std::printf("  block size 1 vs 512: max difference %.2e\n", d);
        CHECK(d < 1e-5);
    }
}

void testRestorer() {
    std::printf("[restorer] RESTORER: noise down, music kept, range, listen, noise curve\n");
    const PluginInfo* info = findPlugin("RESTORER");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    using namespace restorer;

    // NOISE REDUCTION 0: untouched (only delayed).
    {
        auto e = info->create();
        e->prepare(kFs, 512);
        e->set(Reduction, 0);
        Stereo s = noise(1.0, 0.3f, 2), ref = s;
        run(*e, s);
        const std::uint32_t lat = e->latency();
        double d = 0;
        for (std::size_t i = 4096; i + lat < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i + lat] - ref.l[i])));
        std::printf("  REDUCTION 0: output = input delayed %u samples (max difference %.2e)\n", lat, d);
        CHECK(d < 1e-4 && lat == 2048);
    }

    // Hiss under a tone: the hiss goes down a lot, the tone stays.
    auto hissAndTone = [&](float reduction, float low, float high, bool listen, double& toneChange, double& hissLowChange, double& hissHighChange) {
        auto e = info->create();
        e->prepare(kFs, 512);
        e->set(Reduction, reduction);
        e->set(LowHz, low);
        e->set(HighHz, high);
        e->set(Listen, listen ? 1.0f : 0.0f);
        Stereo s = noise(8.0, 0.006f, 4);   // about -50 dBFS of hiss
        add(s, sine(1000, 8.0, 0.3f));
        Stereo ref = s;
        run(*e, s);
        const std::uint32_t lat = e->latency();
        const std::size_t from = static_cast<std::size_t>(6 * kFs), len = static_cast<std::size_t>(kFs);
        std::vector<float> out(s.l.begin() + static_cast<long>(lat), s.l.end());
        toneChange = db(toneAmp(out, 1000, from, len) / toneAmp(ref.l, 1000, from, len));
        hissLowChange = bandLevelDb(out, 200, 600, from, len) - bandLevelDb(ref.l, 200, 600, from, len);
        hissHighChange = bandLevelDb(out, 6000, 15000, from, len) - bandLevelDb(ref.l, 6000, 15000, from, len);
    };
    double tone = 0, lo = 0, hi = 0;
    hissAndTone(7.0f, 20.0f, 20000.0f, false, tone, lo, hi);
    std::printf("  REDUCTION 7, full range: tone %.2f dB, hiss 200-600 Hz %.1f dB, hiss 6-15 kHz %.1f dB\n", tone, lo, hi);
    CHECK(std::fabs(tone) < 1.0);
    CHECK(lo < -10.0 && hi < -6.0);
    hissAndTone(7.0f, 4000.0f, 20000.0f, false, tone, lo, hi);
    std::printf("  RANGE 4k-20k: hiss 200-600 Hz %.1f dB (left alone), hiss 6-15 kHz %.1f dB\n", lo, hi);
    CHECK(std::fabs(lo) < 1.0 && hi < -6.0);
    hissAndTone(7.0f, 20.0f, 20000.0f, true, tone, lo, hi);
    std::printf("  LISTEN: the tone drops %.1f dB (only what is removed is heard)\n", tone);
    CHECK(tone < -20.0);

    // The learned noise curve sits where the noise really is on the display.
    {
        auto e = info->create();
        e->prepare(kFs, 512);
        auto* sp = dynamic_cast<SpectrumSource*>(e.get());
        auto* np = dynamic_cast<NoiseProfileSource*>(e.get());
        CHECK(sp && np);
        if (sp && np) {
            sp->spectrumBefore().setActive(true);
            Stereo s = noise(6.0, 0.01f, 8);
            run(*e, s);
            double diff = 0;
            int n = 0;
            for (int b = 60; b < 140; b += 10) { diff += np->noiseFloorDb(b) - sp->spectrumBefore().level(b); ++n; }
            diff /= n;
            std::printf("  noise curve vs measured noise on the display: %.1f dB apart on average\n", diff);
            CHECK(std::fabs(diff) < 6.0);
        }
    }
    {
        auto setup = [](hc::fx::Effect& e) { e.set(Reduction, 8); e.set(Focus, 40); e.set(LowHz, 300); };
        Stereo in = noise(1.5, 0.05f, 30);
        add(in, sine(440, 1.5, 0.2f));
        const double d = blockSizeDifference(*info, setup, in);
        std::printf("  block size 1 vs 512: max difference %.2e\n", d);
        CHECK(d < 1e-5);
    }
}

void testSpectrum() {
    std::printf("[spectrum] before / after display: calibration and bins\n");
    SpectrumMeter m;
    m.prepare(kFs);
    m.setActive(true);
    Stereo s = sine(1000, 1.0, 0.5f);
    m.push(s.l.data(), s.r.data(), static_cast<std::uint32_t>(s.size()));
    const int b = static_cast<int>(std::lround(spectrumBinOf(1000.0f)));
    float best = -200.0f;
    for (int k = b - 1; k <= b + 1; ++k) best = std::max(best, m.level(k));
    const float far = m.level(static_cast<int>(std::lround(spectrumBinOf(100.0f))));
    std::printf("  1 kHz sine at -6.0 dBFS reads %.2f dB; 100 Hz reads %.1f dB; %u analyses\n", best, far, m.frames());
    CHECK(std::fabs(best + 6.02f) < 1.0f);
    CHECK(far < -80.0f);
    CHECK(std::fabs(spectrumBinHz(0) - 20.0f) < 0.01f && std::fabs(spectrumBinHz(kSpectrumBins - 1) - 20000.0f) < 1.0f);
    // nothing is analysed (no CPU spent) while no faceplate is open
    SpectrumMeter off;
    off.prepare(kFs);
    off.push(s.l.data(), s.r.data(), static_cast<std::uint32_t>(s.size()));
    CHECK(off.frames() == 0);
}

} // namespace hem::test

// ---- CONSOLE EQ ----------------------------------------------------------------------------------
namespace hem::test {
namespace {
using namespace hem::rack;

void setConsole(hc::fx::Effect& e, std::initializer_list<std::pair<int, float>> v) {
    e.setDefaults();
    for (const auto& [i, x] : v) e.set(i, x);
}
console::Settings consoleSettings(const hc::fx::Effect& e) {
    float v[console::kCount];
    for (int i = 0; i < console::kCount; ++i) v[i] = e.get(i);
    return console::settingsFrom(v);
}
// Measured gain at hz, corrected for nothing: the console EQ's latency doesn't change a steady tone's level.
double consoleGainAt(hc::fx::Effect& e, double hz) {
    e.reset();
    Stereo s = sine(hz, 1.2, 0.1f);
    run(e, s);
    return db(toneAmp(s.l, hz, 28800, 28800) / 0.1);
}
} // namespace

void testConsoleEq() {
    using namespace console;
    std::printf("[console eq] CONSOLE EQ: filters, four bands, classic / modern curves, drive\n");
    const PluginInfo* info = findPlugin("CONSOLE EQ");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    auto e = info->create();
    e->prepare(kFs, 512);

    {   // flat: output = input delayed 23 samples, exactly
        setConsole(*e, {});
        e->reset();
        Stereo s = noise(1.0, 0.5f, 3), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 23; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i - 23])));
        std::printf("  flat (all at 0, filters OUT, drive 0): output = input delayed %u samples, max difference %.2e\n", e->latency(), d);
        CHECK(e->latency() == 23);
        CHECK(d < 1e-6);
    }
    {   // what you hear is what the faceplate draws, for every kind of setting
        const std::initializer_list<std::pair<int, float>> cases[] = {
            {{LfGain, 6}, {LfHz, 100}, {LmfGain, -4}, {LmfHz, 400}, {LmfQ, 1.5f}, {HmfGain, 3}, {HmfHz, 3000}, {HfGain, 5}, {HfHz, 10000}},
            {{LfGain, -8}, {LfHz, 200}, {LfBell, 1}, {HmfGain, 9}, {HmfHz, 2000}, {HmfQ, 0.7f}, {HfGain, -6}, {HfHz, 6000}, {HfBell, 1}, {Curve, 1}},
            {{HpfOn, 1}, {HpfHz, 120}, {LpfOn, 1}, {LpfHz, 9000}, {LmfGain, 12}, {LmfHz, 900}, {Curve, 1}},
        };
        double worst = 0;
        for (const auto& c : cases) {
            setConsole(*e, c);
            const Settings st = consoleSettings(*e);
            for (double hz : {40.0, 80.0, 150.0, 300.0, 600.0, 1000.0, 2000.0, 4000.0, 7000.0, 12000.0}) {
                const double want = curveDb(st, kFs, hz), got = consoleGainAt(*e, hz);
                if (want > -40) worst = std::max(worst, std::fabs(want - got));
            }
        }
        std::printf("  3 settings x 10 frequencies: measured vs drawn curve, worst difference %.3f dB\n", worst);
        CHECK(worst < 0.05);
    }
    {   // HPF 18 dB/oct
        setConsole(*e, {{HpfOn, 1}, {HpfHz, 80}});
        const double at80 = consoleGainAt(*e, 80), at20 = consoleGainAt(*e, 20), at1k = consoleGainAt(*e, 1000);
        std::printf("  HPF 80 Hz: %.2f dB at 80 Hz, %.1f dB at 20 Hz, %.2f dB at 1 kHz\n", at80, at20, at1k);
        CHECK(std::fabs(at80 + 3.0) < 0.3);
        CHECK(at20 < -33.0 && at20 > -39.0);
        CHECK(std::fabs(at1k) < 0.05);
    }
    {   // MODERN: a big boost is narrower than CLASSIC, a small one is wider
        setConsole(*e, {{LmfGain, 12}, {LmfHz, 1000}, {LmfQ, 1}});
        const double cBig = consoleGainAt(*e, 2000);
        setConsole(*e, {{LmfGain, 12}, {LmfHz, 1000}, {LmfQ, 1}, {Curve, 1}});
        const double mBig = consoleGainAt(*e, 2000);
        setConsole(*e, {{LmfGain, 3}, {LmfHz, 1000}, {LmfQ, 1}});
        const double cSmall = consoleGainAt(*e, 2000);
        setConsole(*e, {{LmfGain, 3}, {LmfHz, 1000}, {LmfQ, 1}, {Curve, 1}});
        const double mSmall = consoleGainAt(*e, 2000);
        std::printf("  LMF +12 dB one octave away: classic %.2f, modern %.2f dB;  +3 dB: classic %.2f, modern %.2f dB\n", cBig, mBig, cSmall, mSmall);
        CHECK(mBig < cBig - 0.5);
        CHECK(mSmall > cSmall + 0.2);
    }
    {   // DRIVE: harmonics only when asked for
        Stereo s0 = sine(200.0, 1.0, 0.89f), s1 = s0;
        setConsole(*e, {});
        e->reset();
        run(*e, s0);
        setConsole(*e, {{Drive, 100}});
        e->reset();
        run(*e, s1);
        const double h0 = db(toneAmp(s0.l, 600, 24000, 24000) / 0.89), h1 = db(toneAmp(s1.l, 600, 24000, 24000) / 0.89);
        std::printf("  -1 dBFS 200 Hz, 3rd harmonic: drive 0 %.1f dB, drive 100 %% %.1f dB\n", h0, h1);
        CHECK(h0 < -100.0);
        CHECK(h1 > -40.0);
    }
    {
        Stereo in = noise(1.0, 0.4f, 9);
        const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) {
            x.set(HpfOn, 1); x.set(LfGain, 5); x.set(HmfGain, -6); x.set(HfBell, 1); x.set(HfGain, 4); x.set(Curve, 1); x.set(Drive, 40);
        }, in);
        std::printf("  block size 1 vs 512: max difference %.2e\n", d);
        CHECK(d < 1e-6);
    }
}

// ---- BUS GLUE -------------------------------------------------------------------------------------
namespace {
// Output level (dB) of a steady tone after `seconds`, measured over the last 0.25 s.
double glueOut(hc::fx::Effect& e, double hz, float amp, double seconds = 1.5) {
    e.reset();
    Stereo s = sine(hz, seconds, amp);
    run(e, s);
    const std::size_t len = 12000;
    return db(toneAmp(s.l, hz, s.size() - len, len));
}
// Time (ms) for the gain reduction to come back above -1 dB after a loud passage of `loudS` seconds.
double recoveryMs(hc::fx::Effect& e, double loudS) {
    e.reset();
    Stereo loud = sine(1000.0, loudS, 0.7f);
    run(e, loud);
    Stereo quiet = sine(1000.0, 5.0, 0.01f);
    for (std::size_t i = 0; i < quiet.size(); i += 48) {
        e.process(quiet.l.data() + i, quiet.r.data() + i, 48);
        if (e.reductionDb() > -1.0f) return static_cast<double>(i) / 48.0;
    }
    return 5000.0;
}
} // namespace

void testBusGlue() {
    using namespace glue;
    std::printf("[bus glue] BUS GLUE: static curve, attack, release, auto release, sidechain HPF, mix\n");
    const PluginInfo* info = findPlugin("BUS GLUE");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    auto e = info->create();
    e->prepare(kFs, 512);
    auto set = [&](std::initializer_list<std::pair<int, float>> v) { e->setDefaults(); for (const auto& [i, x] : v) e->set(i, x); };

    {   // static curve: -6 dBFS peak, threshold -20, 4:1 -> 10.5 dB of reduction
        set({{Threshold, -20}, {Ratio, 1}, {Attack, 0}, {Release, 0}});
        const double out = glueOut(*e, 1000, 0.5f);
        const float gr = e->reductionDb();
        const double want = db(0.5) + gainComputerDb(static_cast<float>(db(0.5)), -20, 4);
        std::printf("  -6 dBFS, threshold -20 dB, 4:1: output %.2f dB (curve says %.2f), meter %.2f dB\n", out, want, gr);
        CHECK(std::fabs(out - want) < 0.8);
        CHECK(std::fabs(gr - gainComputerDb(static_cast<float>(db(0.5)), -20, 4)) < 0.8);
    }
    {   // under the threshold: untouched, exactly
        set({{Threshold, -3}});
        e->reset();
        Stereo s = sine(500, 0.5, 0.25f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  under the threshold: max difference %.2e, meter %.2f dB\n", d, e->reductionDb());
        CHECK(d < 1e-6);
        CHECK(e->reductionDb() == 0.0f);
    }
    {   // ratio
        set({{Threshold, -30}, {Ratio, 0}});
        const double r2 = glueOut(*e, 1000, 0.5f);
        set({{Threshold, -30}, {Ratio, 2}});
        const double r10 = glueOut(*e, 1000, 0.5f);
        std::printf("  threshold -30, -6 dBFS: 2:1 -> %.1f dB, 10:1 -> %.1f dB\n", r2, r10);
        CHECK(r10 < r2 - 6.0);
    }
    {   // MIX 0 = dry exactly; MAKEUP adds exactly what it says under the threshold
        set({{Threshold, -40}, {Ratio, 2}, {Mix, 0}});
        e->reset();
        Stereo s = sine(700, 0.5, 0.5f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        CHECK(d < 1e-6);
        set({{Threshold, 0}, {Makeup, 6}});
        const double mk = glueOut(*e, 1000, 0.1f) - db(0.1);
        std::printf("  MIX 0: difference from dry %.2e;  MAKEUP 6 under the threshold: %+.2f dB\n", d, mk);
        CHECK(std::fabs(mk - 6.0) < 0.05);
    }
    {   // attack: 0.1 ms grabs the first 5 ms of a hit, 30 ms lets it through
        auto firstMs = [&](int att) {
            set({{Threshold, -30}, {Ratio, 2}, {Attack, static_cast<float>(att)}});
            e->reset();
            Stereo s = sine(1000, 0.1, 0.5f);
            run(*e, s, 64);
            return db(rms(s.l, 0, 240) * std::numbers::sqrt2);
        };
        const double fast = firstMs(0), slow = firstMs(5);
        std::printf("  first 5 ms of a hit: attack 0.1 ms %.1f dB, attack 30 ms %.1f dB\n", fast, slow);
        CHECK(slow > fast + 6.0);
    }
    {   // release: 0.1 s comes back faster than 1.2 s; AUTO waits longer after sustained work
        set({{Threshold, -30}, {Release, 0}});
        const double r01 = recoveryMs(*e, 1.0);
        set({{Threshold, -30}, {Release, 3}});
        const double r12 = recoveryMs(*e, 1.0);
        set({{Threshold, -30}, {Release, 4}});
        const double aShort = recoveryMs(*e, 0.05), aLong = recoveryMs(*e, 3.0);
        std::printf("  back to -1 dB: release 0.1 s %.0f ms, 1.2 s %.0f ms;  AUTO after 50 ms %.0f ms, after 3 s %.0f ms\n", r01, r12, aShort, aLong);
        CHECK(r12 > 4.0 * r01);
        CHECK(aLong > 2.0 * aShort);
    }
    {   // sidechain high-pass: a 40 Hz bass line hardly moves it with SC HPF 150 Hz
        set({{Threshold, -30}, {ScHpf, 0}});
        (void)glueOut(*e, 40, 0.5f);
        const float off = e->reductionDb();
        set({{Threshold, -30}, {ScHpf, 3}});
        (void)glueOut(*e, 40, 0.5f);
        const float on = e->reductionDb();
        std::printf("  40 Hz at -6 dBFS: reduction %.1f dB with SC HPF off, %.1f dB at 150 Hz\n", off, on);
        CHECK(on > off + 6.0f);
    }
    {   // stereo link: a loud left side turns the right side down by the same amount
        set({{Threshold, -30}, {Ratio, 2}});
        e->reset();
        Stereo s(48000);
        for (std::size_t i = 0; i < s.size(); ++i) {
            const double t = static_cast<double>(i) / kFs;
            s.l[i] = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * 1000 * t));
            s.r[i] = 0.01f * static_cast<float>(std::sin(2 * std::numbers::pi * 300 * t));
        }
        run(*e, s);
        const double gl = db(toneAmp(s.l, 1000, 36000, 12000) / 0.5), gr = db(toneAmp(s.r, 300, 36000, 12000) / 0.01);
        std::printf("  stereo link: left turned down %.2f dB, right %.2f dB\n", gl, gr);
        CHECK(std::fabs(gl - gr) < 0.3);
    }
    {
        Stereo in = noise(1.0, 0.6f, 4);
        const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Threshold, -25); x.set(Release, 4); x.set(ScHpf, 2); x.set(Mix, 70); }, in);
        std::printf("  block size 1 vs 512: max difference %.2e\n", d);
        CHECK(d < 1e-5);
    }
}

} // namespace hem::test

// ---- TAPE, TUBE EQ, VARI-MU, OPTO, FET ---------------------------------------------------------------
namespace hem::test {
namespace {
using namespace hem::rack;

std::unique_ptr<hc::fx::Effect> fresh(const char* name, std::initializer_list<std::pair<int, float>> v) {
    auto e = findPlugin(name)->create();
    for (const auto& [i, x] : v) e->set(i, x);
    e->prepare(kFs, 512);
    return e;
}
// Level (dB re amp) of a steady tone after it has run `seconds` (measured over the last 0.25 s).
double toneOut(hc::fx::Effect& e, double hz, float amp, double seconds = 1.5) {
    e.reset();
    Stereo s = sine(hz, seconds, amp);
    run(e, s);
    return db(toneAmp(s.l, hz, s.size() - 12000, 12000) / amp);
}
double harmonicDb(hc::fx::Effect& e, double hz, int k, float amp) {
    e.reset();
    Stereo s = sine(hz, 1.0, amp);
    run(e, s);
    return db(toneAmp(s.l, hz * k, 24000, 24000) / amp);
}
double maxDelayedDiff(const Stereo& out, const Stereo& in, std::uint32_t lat, std::size_t from) {
    double d = 0;
    for (std::size_t i = std::max<std::size_t>(from, lat); i < out.size(); ++i)
        d = std::max(d, static_cast<double>(std::fabs(out.l[i] - in.l[i - lat])));
    return d;
}
} // namespace

void testTape() {
    using namespace tape;
    std::printf("[tape] TAPE: saturation, head bump, speed, wow, hiss, auto trim, VU\n");
    const PluginInfo* info = findPlugin("TAPE");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    auto e = fresh("TAPE", {});
    CHECK(e->latency() == 23 + kTransportDelay);
    // INPUT drives it: more input, more harmonics; AUTO TRIM keeps the level where it was
    auto e0 = fresh("TAPE", {{Input, 0}}), e18 = fresh("TAPE", {{Input, 18}});
    const double h0 = harmonicDb(*e0, 200, 3, 0.25f), h18 = harmonicDb(*e18, 200, 3, 0.25f);
    const double lv0 = toneOut(*e0, 1000, 0.05f), lv18 = toneOut(*e18, 1000, 0.05f);
    std::printf("  -12 dBFS 200 Hz, 3rd harmonic: input 0 dB %.1f dB, input +18 dB %.1f dB;  quiet level with AUTO TRIM: %+.2f / %+.2f dB\n", h0, h18, lv0, lv18);
    CHECK(h18 > h0 + 15.0);
    CHECK(std::fabs(lv18 - lv0) < 1.0);
    // head bump: 15 IPS lifts around 55 Hz, 30 IPS around 95 Hz; 0 % is flat-ish
    auto b15 = fresh("TAPE", {{Bump, 100}, {Speed, 0}}), b30 = fresh("TAPE", {{Bump, 100}, {Speed, 1}}), b0 = fresh("TAPE", {{Bump, 0}});
    const double at55 = toneOut(*b15, 55, 0.05f), at95 = toneOut(*b30, 95, 0.05f), flat55 = toneOut(*b0, 55, 0.05f);
    std::printf("  head bump 100 %%: 15 IPS %+.2f dB at 55 Hz, 30 IPS %+.2f dB at 95 Hz;  bump 0: %+.2f dB at 55 Hz\n", at55, at95, flat55);
    CHECK(at55 > 2.5 && at95 > 2.5);
    CHECK(std::fabs(flat55) < 0.5);
    // top end: 15 IPS rolls off earlier than 30 IPS
    auto s15 = fresh("TAPE", {{Speed, 0}}), s30 = fresh("TAPE", {{Speed, 1}});
    const double t15 = toneOut(*s15, 18000, 0.05f), t30 = toneOut(*s30, 18000, 0.05f);
    std::printf("  18 kHz: 15 IPS %+.2f dB, 30 IPS %+.2f dB\n", t15, t30);
    CHECK(t15 < t30 - 1.5);
    // WOW 0: a steady tone stays a steady tone; WOW 100: the pitch wanders
    auto w0 = fresh("TAPE", {}), w100 = fresh("TAPE", {{Wow, 100}});
    auto pitchSpread = [](hc::fx::Effect& x) {
        x.reset();
        Stereo s = sine(1000, 3.0, 0.1f);
        run(x, s);
        double lo = 1e9, hi = -1e9;
        for (std::size_t from = 48000; from + 4800 < s.size(); from += 4800) {   // zero crossings per 100 ms
            int z = 0;
            for (std::size_t i = from + 1; i < from + 4800; ++i) z += (s.l[i - 1] < 0) != (s.l[i] < 0);
            lo = std::min(lo, z * 5.0); hi = std::max(hi, z * 5.0);
        }
        return hi - lo;
    };
    const double spread0 = pitchSpread(*w0), spread100 = pitchSpread(*w100);
    std::printf("  1 kHz pitch over 2 s: wow 0 varies %.0f Hz, wow 100 %% varies %.0f Hz\n", spread0, spread100);
    CHECK(spread0 <= 5.0);
    CHECK(spread100 >= 5.0);
    // HISS: none at 0 (silence stays silence, checked above); about -50 dBFS at 100 %
    auto hs = fresh("TAPE", {{Hiss, 100}});
    Stereo z(48000);
    run(*hs, z);
    const double hissDb = db(rms(z.l, 24000, 24000));
    std::printf("  HISS 100 %%: %.1f dBFS RMS\n", hissDb);
    CHECK(hissDb > -60.0 && hissDb < -40.0);
    // VU: a -18 dBFS sine reads about -18 on the meter
    auto vu = fresh("TAPE", {{Bump, 0}});
    (void)toneOut(*vu, 1000, static_cast<float>(std::pow(10.0, -18.0 / 20.0)));
    std::printf("  VU with a -18 dBFS sine: %.2f dB\n", vu->meter(0));
    CHECK(std::fabs(vu->meter(0) + 18.0f) < 0.5f);
    Stereo in = noise(1.0, 0.3f, 2);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Input, 9); x.set(Wow, 60); x.set(Hiss, 30); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testTubeEq() {
    using namespace tubeeq;
    std::printf("[tube eq] TUBE EQ: boosts and cuts as drawn, the low-end trick, tube\n");
    const PluginInfo* info = findPlugin("TUBE EQ");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // drive 0, all at 0: output = input delayed 23 samples
        auto e = fresh("TUBE EQ", {{Drive, 0}});
        Stereo s = noise(1.0, 0.4f, 6), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, e->latency(), 0);
        std::printf("  flat, drive 0: output = input delayed %u samples, max difference %.2e\n", e->latency(), d);
        CHECK(d < 1e-6);
    }
    {   // measured = drawn
        double worst = 0;
        const std::initializer_list<std::pair<int, float>> cases[] = {
            {{Drive, 0}, {LowFreq, 2}, {LowBoost, 8}, {LowAtten, 6}, {HighFreq, 4}, {HighBoost, 5}, {Bandwidth, 3}},
            {{Drive, 0}, {LowFreq, 0}, {LowBoost, 4}, {HighFreq, 1}, {HighBoost, 9}, {Bandwidth, 9}, {HighAtten, 5}, {AttenSel, 0}},
        };
        for (const auto& c : cases) {
            auto e = fresh("TUBE EQ", c);
            float v[kCount];
            for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
            const Settings st = settingsFrom(v);
            for (double hz : {25.0, 50.0, 100.0, 200.0, 400.0, 1000.0, 3000.0, 6000.0, 10000.0, 15000.0}) {
                const double want = curveDb(st, kFs, hz), got = toneOut(*e, hz, 0.05f, 1.2);
                worst = std::max(worst, std::fabs(want - got));
            }
        }
        std::printf("  2 settings x 10 frequencies: measured vs drawn curve, worst difference %.3f dB\n", worst);
        CHECK(worst < 0.05);
    }
    {   // boost and atten together at 60 Hz: more at the bottom, a dip just above
        auto e = fresh("TUBE EQ", {{Drive, 0}, {LowFreq, 2}, {LowBoost, 10}, {LowAtten, 10}});
        const double at30 = toneOut(*e, 30, 0.05f), at200 = toneOut(*e, 200, 0.05f);
        std::printf("  LOW 60 Hz, boost 10 + atten 10: %+.2f dB at 30 Hz, %+.2f dB at 200 Hz\n", at30, at200);
        CHECK(at30 > 3.0);
        CHECK(at200 < -1.0);
    }
    {   // the tube: 2nd harmonic grows with DRIVE
        auto d0 = fresh("TUBE EQ", {{Drive, 0}}), d100 = fresh("TUBE EQ", {{Drive, 100}});
        const double a = harmonicDb(*d0, 200, 2, 0.5f), b = harmonicDb(*d100, 200, 2, 0.5f);
        std::printf("  -6 dBFS 200 Hz, 2nd harmonic: drive 0 %.1f dB, drive 100 %% %.1f dB\n", a, b);
        CHECK(a < -100.0);
        CHECK(b > -40.0);
    }
    Stereo in = noise(1.0, 0.3f, 8);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(LowBoost, 6); x.set(HighBoost, 4); x.set(HighAtten, 3); x.set(Drive, 50); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-6);
}

void testVariMu() {
    using namespace varimu;
    std::printf("[vari-mu] VARI-MU: the ratio grows with level, time constants, mid / side, tube\n");
    const PluginInfo* info = findPlugin("VARI-MU");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    // ratio grows: the slope of the curve is steeper far over the threshold than just over it
    const float s1 = (gainComputerDb(-15, -20) - gainComputerDb(-17, -20)) / 2.0f;
    const float s2 = (gainComputerDb(0, -20) - gainComputerDb(-2, -20)) / 2.0f;
    std::printf("  dB of reduction per dB over: just over %.2f, 20 dB over %.2f\n", s1, s2);
    CHECK(s2 < s1 - 0.2f);
    {   // under the threshold, tube 0: output = input delayed 23
        auto e = fresh("VARI-MU", {{Threshold, 0}, {Tube, 0}});
        Stereo s = sine(500, 1.0, 0.1f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, 23, 0);
        std::printf("  under the threshold, tube 0: max difference from the input (23 samples later) %.2e\n", d);
        CHECK(d < 1e-6);
    }
    {   // it compresses as the curve says (time 1, tube 0)
        auto e = fresh("VARI-MU", {{Threshold, -24}, {Tube, 0}, {Time, 0}});
        const double out = toneOut(*e, 1000, 0.5f);
        const double want = gainComputerDb(static_cast<float>(db(0.5)), -24);
        std::printf("  -6 dBFS, threshold -24: %.2f dB (curve says %.2f), meter %.2f\n", out, want, e->reductionDb());
        CHECK(std::fabs(out - want) < 0.6);
    }
    {   // TIME 1 lets go faster than TIME 4
        auto recover = [](hc::fx::Effect& x) {
            x.reset();
            Stereo loud = sine(1000, 1.0, 0.7f);
            run(x, loud);
            Stereo quiet = sine(1000, 8.0, 0.005f);
            for (std::size_t i = 0; i < quiet.size(); i += 48) {
                x.process(quiet.l.data() + i, quiet.r.data() + i, 48);
                if (x.reductionDb() > -1.0f) return static_cast<double>(i) / 48.0;
            }
            return 8000.0;
        };
        auto t1 = fresh("VARI-MU", {{Threshold, -30}, {Time, 0}}), t4 = fresh("VARI-MU", {{Threshold, -30}, {Time, 3}});
        const double r1 = recover(*t1), r4 = recover(*t4);
        std::printf("  back to -1 dB: time 1 %.0f ms, time 4 %.0f ms\n", r1, r4);
        CHECK(r4 > 5.0 * r1);
    }
    {   // MID / SIDE: a side-only signal is compressed, the mid of a quiet mono signal is not
        auto e = fresh("VARI-MU", {{Threshold, -30}, {Mode, 1}, {Tube, 0}, {Time, 0}});
        Stereo s(72000);
        for (std::size_t i = 0; i < s.size(); ++i) {
            const double t = static_cast<double>(i) / kFs;
            const float side = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * 1000 * t));
            const float mid = 0.01f * static_cast<float>(std::sin(2 * std::numbers::pi * 300 * t));
            s.l[i] = mid + side;
            s.r[i] = mid - side;
        }
        run(*e, s);
        Stereo m(s.size()), sd(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) { m.l[i] = 0.5f * (s.l[i] + s.r[i]); sd.l[i] = 0.5f * (s.l[i] - s.r[i]); }
        const double midG = db(toneAmp(m.l, 300, 48000, 12000) / 0.01), sideG = db(toneAmp(sd.l, 1000, 48000, 12000) / 0.5);
        std::printf("  MID / SIDE: loud side turned down %.1f dB, quiet mid %.2f dB\n", sideG, midG);
        CHECK(sideG < -6.0);
        CHECK(std::fabs(midG) < 0.3);
    }
    {
        auto d0 = fresh("VARI-MU", {{Threshold, 0}, {Tube, 0}}), d100 = fresh("VARI-MU", {{Threshold, 0}, {Tube, 100}});
        const double a = harmonicDb(*d0, 200, 2, 0.5f), b = harmonicDb(*d100, 200, 2, 0.5f);
        std::printf("  TUBE 2nd harmonic at -6 dBFS: tube 0 %.1f dB, tube 100 %% %.1f dB\n", a, b);
        CHECK(a < -100.0 && b > -40.0);
    }
    Stereo in = noise(1.0, 0.5f, 12);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Threshold, -25); x.set(Time, 4); x.set(Mode, 1); x.set(Tube, 60); x.set(Mix, 80); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testOpto() {
    using namespace opto;
    std::printf("[opto] OPTO: peak reduction, compress / limit, two-stage release, emphasis\n");
    const PluginInfo* info = findPlugin("OPTO");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // PEAK REDUCTION 0: nothing happens (exactly); GAIN adds exactly what it says
        auto e = fresh("OPTO", {{PeakReduction, 0}, {Gain, 6}});
        const double g = toneOut(*e, 1000, 0.1f);
        std::printf("  peak reduction 0, gain +6: %+.3f dB, meter %.2f\n", g, e->reductionDb());
        CHECK(std::fabs(g - 6.0) < 0.02);
        CHECK(e->reductionDb() == 0.0f);
    }
    {
        auto lo = fresh("OPTO", {{PeakReduction, 30}}), hi = fresh("OPTO", {{PeakReduction, 70}});
        const double a = toneOut(*lo, 1000, 0.3f, 3.0), b = toneOut(*hi, 1000, 0.3f, 3.0);
        auto cm = fresh("OPTO", {{PeakReduction, 70}, {Mode, 0}}), lm = fresh("OPTO", {{PeakReduction, 70}, {Mode, 1}});
        const double c = toneOut(*cm, 1000, 0.5f, 3.0), l = toneOut(*lm, 1000, 0.5f, 3.0);
        std::printf("  -10 dBFS: peak reduction 30 -> %.1f dB, 70 -> %.1f dB;  -6 dBFS at 70: COMPRESS %.1f dB, LIMIT %.1f dB\n", a, b, c, l);
        CHECK(b < a - 5.0);
        CHECK(l < c - 3.0);
    }
    {   // two-stage release: after 4 s of hard work, half comes back fast, the rest slowly
        auto e = fresh("OPTO", {{PeakReduction, 80}});
        Stereo loud = sine(1000, 4.0, 0.5f);
        run(*e, loud);
        const float start = e->reductionDb();
        Stereo quiet = sine(1000, 4.0, 0.001f);
        double halfMs = -1, nearMs = 4000;   // 4000 = not back within the 4 s
        for (std::size_t i = 0; i < quiet.size(); i += 48) {
            e->process(quiet.l.data() + i, quiet.r.data() + i, 48);
            const float g = e->reductionDb();
            if (halfMs < 0 && g > 0.5f * start) halfMs = static_cast<double>(i) / 48.0;
            if (g > 0.1f * start) { nearMs = static_cast<double>(i) / 48.0; break; }
        }
        std::printf("  after 4 s at %.1f dB: half back in %.0f ms, 90 %% back in %.0f ms\n", start, halfMs, nearMs);
        CHECK(halfMs > 0 && halfMs < 250);
        CHECK(nearMs > 1000);
    }
    {   // EMPHASIS: a 6 kHz tone is pulled down harder with it
        auto e0 = fresh("OPTO", {{PeakReduction, 50}}), e100 = fresh("OPTO", {{PeakReduction, 50}, {Emphasis, 100}});
        const double a = toneOut(*e0, 6000, 0.2f, 3.0), b = toneOut(*e100, 6000, 0.2f, 3.0);
        std::printf("  6 kHz: emphasis 0 -> %.1f dB, emphasis 100 %% -> %.1f dB\n", a, b);
        CHECK(b < a - 2.0);
    }
    {
        auto e = fresh("OPTO", {{PeakReduction, 90}, {Mix, 0}});
        Stereo s = sine(700, 0.5, 0.5f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, 0, 0);
        std::printf("  MIX 0: difference from dry %.2e\n", d);
        CHECK(d < 1e-6);
    }
    Stereo in = noise(1.0, 0.5f, 13);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(PeakReduction, 60); x.set(Emphasis, 40); x.set(Mode, 1); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testFet() {
    using namespace fet;
    std::printf("[fet] FET: input / output, attack and release 1-7, ratios, ALL\n");
    const PluginInfo* info = findPlugin("FET");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    std::printf("  ATTACK 1 / 7: %.3f / %.3f ms;  RELEASE 1 / 7: %.0f / %.0f ms\n", attackMs(1), attackMs(7), releaseMs(1), releaseMs(7));
    CHECK(std::fabs(attackMs(1) - 0.8f) < 1e-4f && std::fabs(attackMs(7) - 0.02f) < 1e-5f);
    CHECK(std::fabs(releaseMs(1) - 1100.0f) < 0.1f && std::fabs(releaseMs(7) - 50.0f) < 0.01f);
    {   // more INPUT, more compression; 20:1 holds harder than 4:1; ALL harder still
        auto lv = [&](float input, int ratio) {
            auto e = fresh("FET", {{Input, input}, {Output, 0}, {Ratio, static_cast<float>(ratio)}});
            return toneOut(*e, 1000, 0.25f);
        };
        // 10 dB more INPUT once it is compressing: 4:1 lets 2.5 dB more out, 20:1 about 0.5 dB
        const double s4 = lv(30, 0) - lv(20, 0), s20 = lv(30, 3) - lv(20, 3), all = lv(30, 4), r20 = lv(30, 3);
        std::printf("  +10 dB of input -> output: 4:1 %+.2f dB, 20:1 %+.2f dB;  input 30: 20:1 %+.1f, ALL %+.1f dB\n", s4, s20, r20, all);
        CHECK(std::fabs(s4 - 2.5) < 0.6);
        CHECK(s20 < 1.0);
        CHECK(all < r20 - 1.0);
    }
    {   // ALL is grittier than 20:1
        auto r20 = fresh("FET", {{Input, 30}, {Ratio, 3}}), all = fresh("FET", {{Input, 30}, {Ratio, 4}});
        // 3rd harmonic relative to what comes out (ALL is quieter, so compare like with like)
        const double a = harmonicDb(*r20, 200, 3, 0.25f) - harmonicDb(*r20, 200, 1, 0.25f);
        const double b = harmonicDb(*all, 200, 3, 0.25f) - harmonicDb(*all, 200, 1, 0.25f);
        std::printf("  3rd harmonic below the tone, 20+ dB of reduction: 20:1 %.1f dB, ALL %.1f dB\n", a, b);
        CHECK(b > a + 3.0);
    }
    {   // attack 7 catches the first millisecond of a hit, attack 1 lets some through
        auto first = [&](float att) {
            auto e = fresh("FET", {{Input, 30}, {Output, 0}, {Attack, att}});
            Stereo s = sine(2000, 0.05, 0.25f);
            run(*e, s, 32);
            double pk = 0;
            for (std::size_t i = 23; i < 23 + 48; ++i) pk = std::max(pk, static_cast<double>(std::fabs(s.l[i])));
            return db(pk);
        };
        const double a1 = first(1), a7 = first(7);
        std::printf("  first ms of a hit: attack 1 peaks %.1f dBFS, attack 7 %.1f dBFS\n", a1, a7);
        CHECK(a7 < a1 - 3.0);
    }
    {
        auto e = fresh("FET", {{Input, 30}, {Mix, 0}});
        Stereo s = sine(700, 0.5, 0.5f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, 23, 0);
        std::printf("  MIX 0: difference from the dry signal 23 samples later %.2e\n", d);
        CHECK(d < 1e-6);
    }
    Stereo in = noise(1.0, 0.5f, 14);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Input, 25); x.set(Ratio, 4); x.set(Attack, 6); x.set(Mix, 70); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

} // namespace hem::test

// ---- LIMITER --------------------------------------------------------------------------------------
namespace hem::test {
namespace {
// Largest true peak (dBTP) of a signal: 4x oversampled with a flat-to-20-kHz filter.
double truePeakDb(const Stereo& s, std::size_t from) {
    hc::fx::Oversampler4 os[2] = {hc::fx::Oversampler4(1.0), hc::fx::Oversampler4(1.0)};
    std::vector<float> up(s.size() * 4);
    double pk = 0;
    const std::vector<float>* ch[2] = {&s.l, &s.r};
    for (int c = 0; c < 2; ++c) {
        os[c].up(ch[c]->data(), up.data(), static_cast<std::uint32_t>(s.size()));
        for (std::size_t i = (from + 12) * 4; i < up.size(); ++i) pk = std::max(pk, static_cast<double>(std::fabs(up[i])));
    }
    return db(pk);
}
double samplePeakDb(const Stereo& s, std::size_t from) {
    double pk = 0;
    for (std::size_t i = from; i < s.size(); ++i) pk = std::max({pk, static_cast<double>(std::fabs(s.l[i])), static_cast<double>(std::fabs(s.r[i]))});
    return db(pk);
}
// A loud, dense "master": noise + bass + bright transients, peaks near full scale.
Stereo hotMix(double seconds) {
    Stereo s = noise(seconds, 0.25f, 21);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / kFs, beat = std::fmod(t, 0.5);
        const float kick = 0.6f * static_cast<float>(std::exp(-beat * 25.0) * std::sin(2 * std::numbers::pi * 60 * beat));
        const float hat = (std::fmod(t, 0.125) < 0.004 ? 0.7f : 0.0f) * static_cast<float>(std::sin(2 * std::numbers::pi * 9000 * t));
        s.l[i] += kick + hat;
        s.r[i] += kick - hat;
    }
    return s;
}
} // namespace

void testLimiter() {
    using namespace limiter;
    std::printf("[limiter] LIMITER: ceiling, true peak, look-ahead, release, link, loudness\n");
    const PluginInfo* info = findPlugin("LIMITER");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // under the ceiling: untouched, just delayed
        auto e = fresh("LIMITER", {});
        CHECK(e->latency() == kLatency);
        Stereo s = sine(700, 1.0, 0.1f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, kLatency, 0);
        std::printf("  -20 dBFS through it: output = input delayed %u samples (%.2f ms), max difference %.2e\n", kLatency, kLatency / 48.0, d);
        CHECK(d < 1e-6);
    }
    {   // pushed hard: never a sample over the ceiling; with TRUE PEAK on, not between samples either
        for (float push : {6.0f, 12.0f, 18.0f}) {
            auto on = fresh("LIMITER", {{Input, push}, {Ceiling, -1.0f}}), off = fresh("LIMITER", {{Input, push}, {Ceiling, -1.0f}, {TruePeak, 0}});
            Stereo a = hotMix(4.0), b = a;
            run(*on, a);
            run(*off, b);
            const double sp = samplePeakDb(a, 4800), tpOn = truePeakDb(a, 4800), tpOff = truePeakDb(b, 4800);
            const double clipped = [&] {
                std::size_t k = 0;
                for (std::size_t i = 4800; i < a.size(); ++i) k += std::fabs(a.l[i]) >= 0.8912f || std::fabs(a.r[i]) >= 0.8912f;
                return 100.0 * static_cast<double>(k) / static_cast<double>(2 * (a.size() - 4800));
            }();
            std::printf("  +%2.0f dB into a -1 dB ceiling: sample peak %.2f dBFS, true peak %.2f dBTP (TRUE PEAK off: %.2f), at the safety clip %.3f %% of samples\n",
                        push, sp, tpOn, tpOff, clipped);
            CHECK(sp <= -1.0 + 1e-4);
            CHECK(tpOn <= -1.0 + 0.3);
            CHECK(clipped < 0.5);
        }
    }
    {   // INPUT makes it louder (that's the point)
        auto e0 = fresh("LIMITER", {{Input, 0}}), e8 = fresh("LIMITER", {{Input, 8}});
        Stereo a = hotMix(4.0);
        for (std::size_t i = 0; i < a.size(); ++i) { a.l[i] *= 0.25f; a.r[i] *= 0.25f; }   // a mix with headroom left
        Stereo b = a;
        run(*e0, a);
        run(*e8, b);
        const double l0 = hc::media::integratedLufs(a.l.data(), a.r.data(), a.size(), kFs), l8 = hc::media::integratedLufs(b.l.data(), b.r.data(), b.size(), kFs);
        std::printf("  loudness: input 0 -> %.1f LUFS (meter %.1f), input +8 -> %.1f LUFS (meter %.1f)\n", l0, e0->meter(0), l8, e8->meter(0));
        CHECK(l8 > l0 + 4.0);
        CHECK(std::fabs(e8->meter(0) - l8) < 0.5);
    }
    {   // a steady tone 3 dB over the ceiling comes out clean (the gain sits still)
        auto e = fresh("LIMITER", {{Ceiling, -6.0f}});
        Stereo s = sine(1000, 1.5, static_cast<float>(std::pow(10.0, -3.0 / 20.0)));
        run(*e, s);
        const double f = toneAmp(s.l, 1000, 36000, 24000), h3 = toneAmp(s.l, 3000, 36000, 24000);
        std::printf("  steady 1 kHz 3 dB over: out %.2f dBFS, 3rd harmonic %.1f dB below it\n", db(f), db(h3) - db(f));
        CHECK(std::fabs(db(f) + 6.0) < 0.3);
        CHECK(db(h3) - db(f) < -50.0);
    }
    {   // release: 30 ms comes back faster than 600 ms
        auto back = [&](float ms) {
            auto e = fresh("LIMITER", {{AutoRelease, 0}, {Release, ms}, {Ceiling, -6}});
            Stereo loud = sine(1000, 0.5, 1.0f);
            run(*e, loud);
            Stereo quiet = sine(1000, 3.0, 0.2f);
            for (std::size_t i = 0; i < quiet.size(); i += 48) {
                e->process(quiet.l.data() + i, quiet.r.data() + i, 48);
                if (e->reductionDb() > -0.5f) return static_cast<double>(i) / 48.0;
            }
            return 3000.0;
        };
        const double fast = back(30), slow = back(600);
        std::printf("  back within 0.5 dB: release 30 ms -> %.0f ms, 600 ms -> %.0f ms\n", fast, slow);
        CHECK(slow > 5.0 * fast);
    }
    {   // LINK: 100 % turns the quiet side down with the loud one; 0 % leaves it alone
        auto side = [&](float link) {
            auto e = fresh("LIMITER", {{Link, link}, {Ceiling, -6}});
            Stereo s(72000);
            for (std::size_t i = 0; i < s.size(); ++i) {
                const double t = static_cast<double>(i) / kFs;
                s.l[i] = 0.9f * static_cast<float>(std::sin(2 * std::numbers::pi * 1000 * t));
                s.r[i] = 0.05f * static_cast<float>(std::sin(2 * std::numbers::pi * 300 * t));
            }
            run(*e, s);
            return db(toneAmp(s.r, 300, 48000, 12000) / 0.05);
        };
        const double l100 = side(100), l0 = side(0);
        std::printf("  quiet right side while the left is limited: link 100 %% %.2f dB, link 0 %% %.2f dB\n", l100, l0);
        CHECK(l100 < -4.0);
        CHECK(std::fabs(l0) < 0.1);
    }
    Stereo in = hotMix(1.0);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Input, 10); x.set(Link, 60); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

} // namespace hem::test

// ---- WAVE 5: DE-ESSER, STEREO WIDTH, CLIPPER, EXCITER, TRANSIENT, LOUDNESS METER ----------------
namespace hem::test {
namespace {
// A drum-like hit every 0.5 s: a 150 Hz tone that dies away over ~150 ms.
Stereo hits(double seconds, float amp) {
    Stereo s(static_cast<std::size_t>(seconds * kFs));
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = std::fmod(static_cast<double>(i) / kFs, 0.5);
        const float v = amp * static_cast<float>(std::exp(-t * 20.0) * std::sin(2 * std::numbers::pi * 150 * t));
        s.l[i] = s.r[i] = v;
    }
    return s;
}
// Largest |sample| between t0 and t1 (seconds) after each hit, averaged over the hits from 1 s on.
double hitPart(const Stereo& s, double t0, double t1) {
    double sum = 0;
    int k = 0;
    for (double h = 1.0; h + 0.5 <= static_cast<double>(s.size()) / kFs; h += 0.5, ++k) {
        double pk = 0;
        for (auto i = static_cast<std::size_t>((h + t0) * kFs); i < static_cast<std::size_t>((h + t1) * kFs); ++i) pk = std::max(pk, static_cast<double>(std::fabs(s.l[i])));
        sum += pk;
    }
    return k ? db(sum / k) : -200.0;
}
} // namespace

void testDeEsser() {
    using namespace deesser;
    std::printf("[deesser] DE-ESSER: esses down, the rest alone\n");
    const PluginInfo* info = findPlugin("DE-ESSER");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    auto mix = [] { Stereo s = sine(300, 1.5, 0.3f); add(s, sine(7000, 1.5, 0.3f)); return s; };
    {   // quiet: nothing happens at all
        auto e = fresh("DE-ESSER", {{Threshold, 0}});
        Stereo s = mix(), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, 0, 0);
        std::printf("  under the threshold: max difference from the input %.2e\n", d);
        CHECK(d < 1e-6);
    }
    for (int mode : {0, 1}) {
        auto e = fresh("DE-ESSER", {{Threshold, -40}, {Range, 8}, {Mode, static_cast<float>(mode)}});
        Stereo s = mix();
        run(*e, s);
        const double ess = db(toneAmp(s.l, 7000, 48000, 12000) / 0.3), body = db(toneAmp(s.l, 300, 48000, 12000) / 0.3);
        std::printf("  %s, threshold -40, range 8: 7 kHz ess %.2f dB, 300 Hz body %.2f dB, needle %.1f dB\n", mode ? "WIDE" : "SPLIT", ess, body,
                    e->reductionDb());
        CHECK(ess < -4.0 && ess > -8.6);   // turned down, never more than RANGE
        if (mode == 0) CHECK(std::fabs(body) < 0.2);   // SPLIT: the body is untouched
        else CHECK(body < -3.0);                       // WIDE: everything ducks
        CHECK(e->reductionDb() < -3.0f);
    }
    {   // LISTEN: only the ess band
        auto e = fresh("DE-ESSER", {{Listen, 1}, {Freq, 6000}});
        Stereo s = mix();
        run(*e, s);
        const double ess = db(toneAmp(s.l, 7000, 48000, 12000) / 0.3), body = db(toneAmp(s.l, 300, 48000, 12000) / 0.3);
        std::printf("  LISTEN: 7 kHz %.1f dB, 300 Hz %.1f dB\n", ess, body);
        CHECK(ess > -6.0 && body < -60.0);
    }
    Stereo in = mix();
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Threshold, -40); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testWidth() {
    using namespace width;
    std::printf("[width] STEREO WIDTH: mono, wider, mono bass, balance, meters\n");
    const PluginInfo* info = findPlugin("STEREO WIDTH");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // 100 %: untouched
        auto e = fresh("STEREO WIDTH", {});
        Stereo s = noise(1.0, 0.4f), ref = s;
        run(*e, s);
        CHECK(maxDelayedDiff(s, ref, 0, 0) < 1e-6);
    }
    {   // 0 %: mono (L = R), correlation +1
        auto e = fresh("STEREO WIDTH", {{Width, 0}});
        Stereo s = noise(1.0, 0.4f);
        run(*e, s);
        double d = 0;
        for (std::size_t i = 4800; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  width 0: |L - R| max %.2e, correlation %.2f\n", d, e->meter(0));
        CHECK(d < 1e-5 && e->meter(0) > 0.99f);
    }
    {   // 200 %: the sides +6 dB, the middle the same
        auto e = fresh("STEREO WIDTH", {{Width, 200}});
        Stereo s = sine(1000, 1.0, 0.2f);
        add(s, sine(3000, 1.0, 0.1f, kFs, true));   // a side-only tone
        run(*e, s);
        const double mid = db(toneAmp(s.l, 1000, 24000, 12000) / 0.2), side = db(toneAmp(s.l, 3000, 24000, 12000) / 0.1);
        std::printf("  width 200: middle %.2f dB, sides %+.2f dB\n", mid, side);
        CHECK(std::fabs(mid) < 0.05 && std::fabs(side - 6.02) < 0.1);
    }
    {   // MONO BASS 120 Hz: low side content gone, high side content kept
        auto e = fresh("STEREO WIDTH", {{MonoBass, 2}});
        Stereo s = sine(50, 2.0, 0.2f, kFs, true);
        add(s, sine(2000, 2.0, 0.1f, kFs, true));
        run(*e, s);
        const double lo = db(toneAmp(s.l, 50, 48000, 24000) / 0.2), hi = db(toneAmp(s.l, 2000, 48000, 24000) / 0.1);
        std::printf("  mono bass 120 Hz: 50 Hz side %.1f dB, 2 kHz side %.2f dB\n", lo, hi);
        CHECK(lo < -15.0 && std::fabs(hi) < 0.2);
    }
    {   // antiphase reads -1; BALANCE right turns the left down
        auto e = fresh("STEREO WIDTH", {{Balance, 50}});
        Stereo s = sine(500, 1.0, 0.2f, kFs, true);
        run(*e, s);
        const double l = db(toneAmp(s.l, 500, 24000, 12000) / 0.2), r = db(toneAmp(s.r, 500, 24000, 12000) / 0.2);
        std::printf("  antiphase: correlation %.2f; balance +50: left %.2f dB, right %.2f dB\n", e->meter(0), l, r);
        CHECK(e->meter(0) < -0.99f);
        CHECK(std::fabs(l + 6.02) < 0.1 && std::fabs(r) < 0.05);
    }
    Stereo in = noise(1.0, 0.4f);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Width, 150); x.set(MonoBass, 3); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testClipper() {
    using namespace clipper;
    std::printf("[clipper] CLIPPER: under the ceiling clean, over it shaved\n");
    const PluginInfo* info = findPlugin("CLIPPER");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // well under the ceiling: the same sound, delayed
        auto e = fresh("CLIPPER", {});
        Stereo s = sine(1000, 1.0, 0.3f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, e->latency(), 4800);
        std::printf("  -10 dBFS: latency %u samples, max difference %.2e\n", e->latency(), d);
        CHECK(d < 2e-3);
        CHECK(e->reductionDb() > -0.01f);
    }
    for (float shape : {0.0f, 100.0f}) {   // pushed 12 dB into a -1 dB ceiling
        auto e = fresh("CLIPPER", {{Input, 12}, {Ceiling, -1}, {Shape, shape}});
        Stereo s = hotMix(3.0);
        float most = 0.0f;
        for (std::size_t i = 0; i < s.size(); i += 512) {
            e->process(s.l.data() + i, s.r.data() + i, static_cast<std::uint32_t>(std::min<std::size_t>(512, s.size() - i)));
            most = std::min(most, e->reductionDb());
        }
        const double sp = samplePeakDb(s, 4800);
        std::printf("  +12 dB, shape %.0f: sample peak %.2f dBFS, needle down to %.1f dB\n", shape, sp, most);
        CHECK(sp <= -1.0 + 1e-3);
        CHECK(most < -10.0f);
    }
    {   // the clipping is oversampled: a hard-clipped 5 kHz tone leaves little aliasing (energy off the harmonics)
        auto e = fresh("CLIPPER", {{Input, 12}, {Shape, 0}});
        Stereo s = sine(5000, 1.0, 0.5f);
        run(*e, s);
        const double f = toneAmp(s.l, 5000, 24000, 12000), alias = toneAmp(s.l, 48000 - 7 * 5000 - 0, 24000, 12000);   // 7th harmonic folded to 13 kHz
        std::printf("  hard clip 5 kHz: 7th harmonic folded back to 13 kHz %.1f dB under the tone\n", db(alias) - db(f));
        CHECK(db(alias) - db(f) < -45.0);
    }
    Stereo in = hotMix(1.0);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Input, 9); x.set(Shape, 40); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testExciter() {
    using namespace exciter;
    std::printf("[exciter] EXCITER: harmonics up top only\n");
    const PluginInfo* info = findPlugin("EXCITER");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // MIX 0: the dry sound, delayed
        auto e = fresh("EXCITER", {{Mix, 0}});
        Stereo s = noise(1.0, 0.3f), ref = s;
        run(*e, s);
        CHECK(maxDelayedDiff(s, ref, e->latency(), 4800) < 1e-6);
    }
    {   // a 400 Hz tone (under FREQ 3 kHz): left alone
        auto e = fresh("EXCITER", {{Drive, 100}, {Mix, 100}});
        Stereo s = sine(400, 1.0, 0.5f);
        run(*e, s);
        const double f = db(toneAmp(s.l, 400, 24000, 12000) / 0.5), h2 = db(toneAmp(s.l, 800, 24000, 12000) / 0.5);
        std::printf("  400 Hz under FREQ: fundamental %.2f dB, 2nd harmonic %.1f dB\n", f, h2);
        CHECK(std::fabs(f) < 0.2 && h2 < -50.0);
    }
    {   // a 4 kHz tone: new harmonics; TUBE even (8 kHz) stronger than TAPE's, TAPE odd (12 kHz)
        auto h = [&](float mode, double hz) {
            auto e = fresh("EXCITER", {{Freq, 3000}, {Drive, 80}, {Mix, 50}, {Mode, mode}});
            Stereo s = sine(4000, 1.0, 0.4f);
            run(*e, s);
            return db(toneAmp(s.l, hz, 24000, 12000) / 0.4);
        };
        const double t2 = h(0, 8000), p2 = h(1, 8000), p3 = h(1, 12000);
        std::printf("  4 kHz: TUBE 2nd harmonic %.1f dB, TAPE 2nd %.1f dB, TAPE 3rd %.1f dB\n", t2, p2, p3);
        CHECK(t2 > -40.0 && t2 > p2 + 10.0 && p3 > -45.0);
    }
    Stereo in = noise(1.0, 0.3f);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Drive, 70); x.set(Mix, 40); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testTransient() {
    using namespace transient;
    std::printf("[transient] TRANSIENT: more or less hit, more or less tail, any level\n");
    const PluginInfo* info = findPlugin("TRANSIENT");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // at 0 / 0: untouched
        auto e = fresh("TRANSIENT", {});
        Stereo s = hits(2.0, 0.5f), ref = s;
        run(*e, s);
        CHECK(maxDelayedDiff(s, ref, 0, 0) < 1e-6);
    }
    const Stereo dry = hits(4.0, 0.5f);
    const double hit0 = hitPart(dry, 0.0, 0.01), tail0 = hitPart(dry, 0.08, 0.2);
    for (float amp : {0.5f, 0.05f}) {   // the same at -6 and -26 dBFS
        auto part = [&](std::initializer_list<std::pair<int, float>> v, double t0, double t1) {
            auto e = fresh("TRANSIENT", v);
            Stereo s = hits(4.0, amp);
            run(*e, s);
            return hitPart(s, t0, t1) - db(amp / 0.5);
        };
        const double hitUp = part({{Attack, 100}}, 0.0, 0.01) - hit0, hitDown = part({{Attack, -100}}, 0.0, 0.01) - hit0;
        const double tailUp = part({{Sustain, 100}}, 0.08, 0.2) - tail0, tailDown = part({{Sustain, -100}}, 0.08, 0.2) - tail0;
        std::printf("  at %.0f dBFS: attack +100 hit %+.1f dB, -100 %+.1f dB; sustain +100 tail %+.1f dB, -100 %+.1f dB\n", db(amp), hitUp, hitDown, tailUp,
                    tailDown);
        CHECK(hitUp > 2.0 && hitDown < -2.0 && tailUp > 2.0 && tailDown < -2.0);
    }
    Stereo in = hits(1.5, 0.5f);
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Attack, 60); x.set(Sustain, -40); }, in);
    std::printf("  block size 1 vs 512: max difference %.2e\n", d);
    CHECK(d < 1e-5);
}

void testLoudnessMeter() {
    using namespace meter;
    std::printf("[meter] LOUDNESS METER: BS.1770 / EBU R128 numbers, sound untouched\n");
    const PluginInfo* info = findPlugin("LOUDNESS METER");
    CHECK(info != nullptr);
    if (!info) return;
    commonRules(*info);
    {   // untouched; a -20 dBFS 1 kHz tone in both channels reads -20 LUFS everywhere
        auto e = fresh("LOUDNESS METER", {});
        Stereo s = sine(1000, 5.0, 0.1f), ref = s;
        run(*e, s);
        std::printf("  -20 dBFS 1 kHz: momentary %.2f, short-term %.2f, integrated %.2f LUFS, range %.2f LU, true peak %.2f dBTP, to -14 target %+.2f\n",
                    e->meter(Momentary), e->meter(ShortTerm), e->meter(Integrated), e->meter(Range), e->meter(TruePeak), e->meter(ToTarget));
        CHECK(maxDelayedDiff(s, ref, 0, 0) == 0.0);
        CHECK(std::fabs(e->meter(Momentary) + 20.0f) < 0.1f && std::fabs(e->meter(ShortTerm) + 20.0f) < 0.1f && std::fabs(e->meter(Integrated) + 20.0f) < 0.1f);
        CHECK(e->meter(Range) < 0.5f);
        CHECK(std::fabs(e->meter(TruePeak) + 20.0f) < 0.2f);
        CHECK(std::fabs(e->meter(ToTarget) + 6.0f) < 0.1f);
        CHECK(e->meter(Correlation) > 0.99f);
    }
    {   // integrated agrees with The Hybrid Core's export meter on a real-ish mix
        auto e = fresh("LOUDNESS METER", {});
        Stereo s = hotMix(10.0);
        for (std::size_t i = 0; i < s.size(); ++i) { s.l[i] *= 0.4f; s.r[i] *= 0.4f; }
        const double ref = hc::media::integratedLufs(s.l.data(), s.r.data(), s.size(), kFs);
        run(*e, s);
        std::printf("  10 s mix: meter %.2f LUFS, Hybrid Core export meter %.2f LUFS\n", e->meter(Integrated), ref);
        CHECK(std::fabs(e->meter(Integrated) - ref) < 0.1);
    }
    {   // gating: silence after the music doesn't drag the integrated value down
        auto e = fresh("LOUDNESS METER", {});
        Stereo s = sine(1000, 10.0, 0.1f);
        for (std::size_t i = s.size() / 2; i < s.size(); ++i) s.l[i] = s.r[i] = 0.0f;
        const double ref = hc::media::integratedLufs(s.l.data(), s.r.data(), s.size(), kFs);
        run(*e, s);
        std::printf("  5 s at -20 then 5 s silence: integrated %.2f LUFS (Hybrid Core export meter %.2f)\n", e->meter(Integrated), ref);
        CHECK(std::fabs(e->meter(Integrated) + 20.0f) < 0.2f && std::fabs(e->meter(Integrated) - ref) < 0.05);
    }
    {   // loudness range: 20 s at -20, 20 s at -30 (EBU Tech 3342 style) reads about 10 LU
        auto e = fresh("LOUDNESS METER", {});
        Stereo s = sine(1000, 40.0, 0.1f);
        for (std::size_t i = s.size() / 2; i < s.size(); ++i) { s.l[i] *= 0.31623f; s.r[i] *= 0.31623f; }
        run(*e, s);
        std::printf("  20 s at -20 then 20 s at -30 LUFS: range %.2f LU\n", e->meter(Range));
        CHECK(std::fabs(e->meter(Range) - 10.0f) < 0.6f);
    }
    {   // true peak between the samples: fs / 4 tone at 45 degrees, samples at 0.707 of the real peak
        auto e = fresh("LOUDNESS METER", {});
        Stereo s(48000);
        for (std::size_t i = 0; i < s.size(); ++i) s.l[i] = s.r[i] = 0.5f * static_cast<float>(std::sin(std::numbers::pi * 0.5 * static_cast<double>(i) + std::numbers::pi * 0.25));
        run(*e, s);
        std::printf("  12 kHz with samples at -9.0 dBFS, real peak -6.0: true peak %.2f dBTP\n", e->meter(TruePeak));
        CHECK(std::fabs(e->meter(TruePeak) + 6.02f) < 0.6f);
    }
    {   // correlation: antiphase -1, independent noise about 0
        auto a = fresh("LOUDNESS METER", {}), b = fresh("LOUDNESS METER", {});
        Stereo s = sine(500, 2.0, 0.3f, kFs, true), n = noise(2.0, 0.3f);
        run(*a, s);
        run(*b, n);
        std::printf("  correlation: antiphase %.2f, independent noise %.2f\n", a->meter(Correlation), b->meter(Correlation));
        CHECK(a->meter(Correlation) < -0.99f && std::fabs(b->meter(Correlation)) < 0.15f);
    }
    {   // RESET starts the integrated value over; TARGET changes the difference
        auto e = fresh("LOUDNESS METER", {{Target, 1}});
        Stereo loud = sine(1000, 3.0, 0.1f), quiet = sine(1000, 3.0, 0.01f);
        run(*e, loud);
        const float before = e->meter(Integrated);
        e->set(Reset, 1);
        run(*e, quiet);
        std::printf("  -20 LUFS, RESET, then -40: integrated %.2f -> %.2f LUFS, to -16 target %+.2f\n", before, e->meter(Integrated), e->meter(ToTarget));
        CHECK(std::fabs(before + 20.0f) < 0.1f && std::fabs(e->meter(Integrated) + 40.0f) < 0.1f);
        CHECK(std::fabs(e->meter(ToTarget) + 24.0f) < 0.1f);
    }
}

} // namespace hem::test

// ---- WAVE 6: BRIT PRE, AMERICAN EQ, MID EQ, AIR EQ, GRAPHIC EQ, TILT ------------------------------
namespace hem::test {

void testClassicEqs() {
    std::printf("[classic eqs] the curve on the faceplate is what you hear\n");
    const char* names[] = {"BRIT PRE", "AMERICAN EQ", "MID EQ", "AIR EQ", "GRAPHIC EQ", "TILT"};
    const double probe[] = {60, 150, 400, 1000, 2500, 6000, 12000};
    for (const char* name : names) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr && info->curve != nullptr);
        if (!info || !info->curve) continue;
        commonRules(*info);
        auto proto = info->create();
        const auto ps = proto->params();
        auto isColor = [&](std::size_t i) { return std::string(ps[i].id) == "drive" || std::string(ps[i].id) == "output"; };
        {   // everything at 0 (and no drive): the input, exactly (delayed by the latency)
            auto e = info->create();
            for (std::size_t i = 0; i < ps.size(); ++i)
                if (isColor(i) || ps[i].unit == std::string("dB") || ps[i].unit == std::string("/10")) e->set(static_cast<int>(i), 0.0f);
            e->prepare(kFs, 512);
            Stereo s = noise(1.0, 0.3f), ref = s;
            run(*e, s);
            const double d = maxDelayedDiff(s, ref, e->latency(), 0);
            std::printf("  %s flat: latency %u, max difference from the input %.2e\n", name, e->latency(), d);
            CHECK(d < 1e-6);
        }
        double worst = 0;
        std::mt19937 rng(static_cast<unsigned>(std::strlen(name)) * 31u);
        std::uniform_real_distribution<float> u(0.0f, 1.0f);
        for (int trial = 0; trial < 4; ++trial) {   // random settings: measured response == drawn curve
            float v[hc::fx::Effect::kMaxParams] = {};
            auto e = info->create();
            for (std::size_t i = 0; i < ps.size(); ++i) {
                float x = hc::fx::toPlain(ps[i], u(rng));
                if (isColor(i)) x = 0.0f;
                e->set(static_cast<int>(i), x);
                v[i] = e->get(static_cast<int>(i));
            }
            e->prepare(kFs, 512);
            for (double hz : probe) {
                const double want = info->curve->db(v, kFs, hz);
                e->reset();
                Stereo s = sine(hz, 0.6, 0.05f);
                run(*e, s);
                const double got = db(toneAmp(s.l, hz, 14400, 14400) / 0.05);
                worst = std::max(worst, std::fabs(got - want));
            }
        }
        std::printf("  %s: 4 random settings x 7 frequencies, largest difference between curve and sound %.3f dB\n", name, worst);
        CHECK(worst < 0.1);
    }
    {   // AMERICAN EQ: proportional Q - a small boost is broader than a big one (relative to its peak)
        using namespace american;
        auto widthAtHalf = [](float gain) {
            float v[kCount] = {};
            v[LowFreq] = 1; v[MidFreq] = 2; v[HighFreq] = 2; v[MidGain] = gain;
            const double peak = kAmericanCurve.db(v, kFs, 1500);
            double lo = 1500;
            while (lo > 20 && kAmericanCurve.db(v, kFs, lo) > peak * 0.5) lo /= 1.01;
            return std::log2(1500 / lo) * 2.0;   // octaves between the half-gain points
        };
        const double w3 = widthAtHalf(3), w12 = widthAtHalf(12);
        std::printf("  AMERICAN EQ mid: +3 dB is %.2f octaves wide, +12 dB is %.2f octaves wide\n", w3, w12);
        CHECK(w3 > 1.4 * w12);
    }
    {   // TILT +6: lows -3 dB, highs +3 dB, pivot ~0
        using namespace tilt;
        float v[kCount] = {};
        v[Tilt] = 6; v[Pivot] = 1;
        const double lo = kTiltCurve.db(v, kFs, 30), mid = kTiltCurve.db(v, kFs, 650), hi = kTiltCurve.db(v, kFs, 18000);
        std::printf("  TILT +6 around 650 Hz: 30 Hz %.2f dB, 650 Hz %.2f dB, 18 kHz %.2f dB\n", lo, mid, hi);
        CHECK(std::fabs(lo + 3.0) < 0.4 && std::fabs(mid) < 0.5 && std::fabs(hi - 3.0) < 0.4);
    }
    {   // BRIT PRE high-pass: 18 dB / octave
        using namespace brit;
        float v[kCount] = {};
        v[Hpf] = 3; v[LowFreq] = 1; v[MidFreq] = 2;   // 160 Hz
        const double a = kBritCurve.db(v, kFs, 40), b = kBritCurve.db(v, kFs, 20);
        std::printf("  BRIT PRE high-pass 160 Hz: 40 Hz %.1f dB, 20 Hz %.1f dB (%.1f dB / octave)\n", a, b, a - b);
        CHECK(std::fabs((a - b) - 18.0) < 1.0);
    }
    {   // DRIVE at a normal level (-14 dBFS, drive 50): BRIT PRE (iron) odd harmonics, MID EQ (tube) even first
        auto h = [](const char* n, int drive, int k) {
            auto e = findPlugin(n)->create();
            e->set(drive, 50);
            e->prepare(kFs, 512);
            Stereo s = sine(200, 1.0, 0.2f);
            run(*e, s);
            return db(toneAmp(s.l, 200.0 * k, 24000, 24000) / 0.2);
        };
        const double b2 = h("BRIT PRE", brit::Drive, 2), b3 = h("BRIT PRE", brit::Drive, 3), m2 = h("MID EQ", mideq::Drive, 2), m3 = h("MID EQ", mideq::Drive, 3);
        std::printf("  drive 50 at -14 dBFS: BRIT PRE 2nd %.1f / 3rd %.1f dB, MID EQ 2nd %.1f / 3rd %.1f dB\n", b2, b3, m2, m3);
        CHECK(b3 > b2 + 10.0 && m2 > m3 && m2 > -50.0 && b3 > -50.0);
    }
}

} // namespace hem::test

// ---- WAVE 7: VCA COMP, PUNCH 160, THRUST BUS, BRIT BUS, GATE, MULTIBAND ---------------------------
namespace hem::test {
namespace {
// Steady 1 kHz tone level change (dB) after `seconds`, measured over the last 0.25 s.
double steadyChange(const char* name, std::initializer_list<std::pair<int, float>> v, float amp, double seconds = 1.5) {
    auto e = fresh(name, v);
    return toneOut(*e, 1000, amp, seconds);
}
} // namespace

void testMoreDynamics() {
    std::printf("[dynamics 2] VCA COMP, PUNCH 160, THRUST BUS, BRIT BUS, GATE, MULTIBAND\n");
    for (const char* n : {"VCA COMP", "PUNCH 160", "THRUST BUS", "BRIT BUS", "GATE", "MULTIBAND"}) {
        const PluginInfo* info = findPlugin(n);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
    {   // VCA COMP: the static curve. -6 dBFS tone, threshold -20, 4:1, hard knee: 14 over -> 3.5 over = -10.5 dB
        using namespace vca;
        const double under = steadyChange("VCA COMP", {{Threshold, -20}, {Knee, 0}}, 0.05f);   // -26 dBFS: under
        const double over = steadyChange("VCA COMP", {{Threshold, -20}, {Knee, 0}, {Ratio, 4}}, 0.5f);
        const double autoMk = steadyChange("VCA COMP", {{Threshold, -20}, {Knee, 0}, {Ratio, 4}, {AutoMakeup, 1}}, 0.5f);
        const double half = steadyChange("VCA COMP", {{Threshold, -20}, {Knee, 0}, {Ratio, 4}, {Mix, 50}}, 0.5f);
        std::printf("  VCA COMP -20 dB, 4:1: under %.3f dB, -6 dBFS tone %.2f dB (curve -10.50), auto makeup %.2f dB, mix 50 %% %.2f dB\n", under, over, autoMk, half);
        CHECK(std::fabs(under) < 1e-4);
        CHECK(std::fabs(over + 10.5) < 0.6);
        CHECK(std::fabs(autoMk - (over + 7.5)) < 0.3);
        CHECK(half > over + 3.0 && half < -1.0);
        // attack: fast grabs a burst sooner than slow
        auto grabbed = [](float attackMs) {
            auto e = fresh("VCA COMP", {{Threshold, -30}, {Attack, attackMs}});
            Stereo s = sine(1000, 0.5, 0.5f);
            for (std::size_t i = 0; i < s.size(); i += 48) {
                e->process(s.l.data() + i, s.r.data() + i, 48);
                if (e->reductionDb() < -6.0f) return static_cast<double>(i) / 48.0;
            }
            return 500.0;
        };
        const double fast = grabbed(0.5f), slow = grabbed(30.0f);
        std::printf("  VCA COMP to -6 dB: attack 0.5 ms in %.0f ms, 30 ms in %.0f ms\n", fast, slow);
        CHECK(fast < 3.0 && slow > 10.0 * std::max(fast, 1.0));
    }
    {   // PUNCH 160: RMS on a sine scale, 4:1 at -20: a -6 dBFS tone -> -10.5 dB; it lets go at ~120 dB/s
        using namespace punch;
        const double over = steadyChange("PUNCH 160", {{Threshold, -20}, {Compression, 4}}, 0.5f);
        std::printf("  PUNCH 160 -20 dB, 4:1: -6 dBFS tone %.2f dB (curve -10.50)\n", over);
        CHECK(std::fabs(over + 10.5) < 0.6);
        auto e = fresh("PUNCH 160", {{Threshold, -30}, {Compression, 60}});
        Stereo loud = sine(1000, 0.5, 0.5f);
        run(*e, loud);
        const float start = e->reductionDb();
        Stereo quiet(4800);   // 100 ms of silence
        run(*e, quiet);
        const float after = e->reductionDb();
        std::printf("  PUNCH 160 release: %.1f dB -> %.1f dB after 100 ms of silence (%.0f dB/s)\n", start, after, (after - start) * 10.0f);
        CHECK(std::fabs((after - start) * 10.0f - 120.0f) < 10.0f);
    }
    {   // THRUST BUS: NEW and OLD give the same ratio; LOUD thrust compresses bass far less than NORM
        using namespace thrust;
        const double neu = steadyChange("THRUST BUS", {{Threshold, -20}, {Knee, 0}, {Ratio, 3}, {Style, 0}}, 0.5f);
        const double old = steadyChange("THRUST BUS", {{Threshold, -20}, {Knee, 0}, {Ratio, 3}, {Style, 1}}, 0.5f);
        std::printf("  THRUST BUS 4:1 at -20, -6 dBFS tone: NEW %.2f dB, OLD %.2f dB (curve -10.50)\n", neu, old);
        CHECK(std::fabs(neu + 10.5) < 0.6 && std::fabs(old + 10.5) < 1.0);
        auto bass = [](float thrustSel) {
            auto e = fresh("THRUST BUS", {{Threshold, -20}, {Thrust, thrustSel}});
            return toneOut(*e, 60, 0.5f, 1.5);
        };
        const double norm = bass(0), loud = bass(2);
        std::printf("  60 Hz at -6 dBFS: THRUST NORM %.2f dB, LOUD %.2f dB\n", norm, loud);
        CHECK(loud > norm + 4.0);
    }
    {   // BRIT BUS: compresses; the LIMIT section holds the level; AUTO recovers faster after a short hit
        using namespace britbus;
        const double c = steadyChange("BRIT BUS", {{Threshold, -20}, {Ratio, 3}, {Drive, 0}}, 0.5f);
        const double lim = steadyChange("BRIT BUS", {{Threshold, 0}, {Limit, 1}, {LimitLevel, -10}, {Drive, 0}}, 0.5f);
        std::printf("  BRIT BUS 4:1 at -20: -6 dBFS tone %.2f dB; LIMIT at -10: %.2f dB (wants -4.0)\n", c, lim);
        CHECK(c < -7.0 && c > -12.0);
        CHECK(std::fabs(lim + 4.0) < 0.6);
        auto e = fresh("BRIT BUS", {{Drive, 0}, {Mix, 100}});
        Stereo s = noise(1.0, 0.05f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, e->latency(), 4800);
        std::printf("  BRIT BUS under the threshold, no iron: latency %u, max difference %.2e\n", e->latency(), d);
        CHECK(d < 1e-3);
    }
    {   // GATE: closes by RANGE on quiet, opens on loud, holds; EXPAND drops 2 dB per dB under
        using namespace gate;
        const double closed = steadyChange("GATE", {{Threshold, -30}, {Range, 40}}, 0.01f);   // -40 dBFS
        const double open = steadyChange("GATE", {{Threshold, -30}, {Range, 40}}, 0.1f);      // -20 dBFS
        const double exp = steadyChange("GATE", {{Threshold, -30}, {Range, 60}, {Mode, 1}}, 0.01f);   // 10 under: -20 dB
        std::printf("  GATE -30: -40 dBFS tone %.2f dB (closed, range 40), -20 dBFS %.3f dB (open); EXPAND 10 dB under %.2f dB\n", closed, open, exp);
        CHECK(std::fabs(closed + 40.0) < 0.5 && std::fabs(open) < 0.01 && std::fabs(exp + 20.0) < 1.5);
    }
    {   // MULTIBAND: no compression -> flat (the bands add back up); a loud low tone is compressed, a mid tone isn't
        using namespace multiband;
        double worst = 0;
        for (double hz : {40.0, 100.0, 150.0, 300.0, 1000.0, 3000.0, 6000.0, 12000.0}) {
            auto e = fresh("MULTIBAND", {{LowThr, 0}, {MidThr, 0}, {HighThr, 0}});
            worst = std::max(worst, std::fabs(toneOut(*e, hz, 0.1f, 1.0)));
        }
        std::printf("  MULTIBAND at rest, 40 Hz .. 12 kHz: largest deviation from flat %.3f dB\n", worst);
        CHECK(worst < 0.05);
        auto e = fresh("MULTIBAND", {{LowThr, -30}, {LowRatio, 4}, {MidThr, 0}, {HighThr, 0}});
        Stereo s = sine(60, 1.5, 0.3f);
        add(s, sine(1000, 1.5, 0.05f));
        run(*e, s);
        const double lo = db(toneAmp(s.l, 60, 48000, 12000) / 0.3), mid = db(toneAmp(s.l, 1000, 48000, 12000) / 0.05);
        std::printf("  MULTIBAND low band at -30, 4:1: 60 Hz %.2f dB, 1 kHz %.2f dB, meters %.1f / %.1f / %.1f\n", lo, mid, e->meter(0), e->meter(1), e->meter(2));
        CHECK(lo < -10.0 && std::fabs(mid) < 0.2 && e->meter(0) < -10.0f && e->meter(1) > -0.1f);
    }
    for (const char* n : {"VCA COMP", "PUNCH 160", "THRUST BUS", "BRIT BUS", "GATE", "MULTIBAND"}) {
        Stereo in = hotMix(1.0);
        const double d = blockSizeDifference(*findPlugin(n), [](hc::fx::Effect&) {}, in);
        std::printf("  %s block size 1 vs 512: max difference %.2e\n", n, d);
        CHECK(d < 1e-5);
    }
}

} // namespace hem::test

// ---- WAVE 8: TUBE SAT, PREAMP, CASSETTE, VINYL, BIT CRUSHER, TRANSFORMER -------------------------
namespace hem::test {
namespace {
// Spread (standard deviation, in samples) of the time between upward zero crossings of a tone:
// steady pitch = tiny spread, wobbling pitch = bigger.
double periodSpread(const std::vector<float>& x, std::size_t from) {
    std::vector<double> cross;
    for (std::size_t i = from + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) cross.push_back(static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
    double m = 0, v = 0;
    const std::size_t k = cross.size() > 1 ? cross.size() - 1 : 0;
    for (std::size_t i = 0; i < k; ++i) m += cross[i + 1] - cross[i];
    m /= std::max<std::size_t>(k, 1);
    for (std::size_t i = 0; i < k; ++i) v += (cross[i + 1] - cross[i] - m) * (cross[i + 1] - cross[i] - m);
    return std::sqrt(v / std::max<std::size_t>(k, 1));
}
} // namespace

void testColorFx() {
    std::printf("[colour] TUBE SAT, PREAMP, CASSETTE, VINYL, BIT CRUSHER, TRANSFORMER\n");
    for (const char* n : {"TUBE SAT", "PREAMP", "CASSETTE", "VINYL", "BIT CRUSHER", "TRANSFORMER"}) {
        const PluginInfo* info = findPlugin(n);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
    {   // TUBE SAT: quiet passes at the same level; driven, BIAS brings the 2nd harmonic; MIX 0 = dry
        using namespace tubesat;
        auto quiet = fresh("TUBE SAT", {{Drive, 100}});
        const double q = toneOut(*quiet, 1000, 0.003f);
        auto h2 = [](float bias) { auto e = fresh("TUBE SAT", {{Drive, 70}, {Bias, bias}}); return harmonicDb(*e, 200, 2, 0.3f); };
        auto h3 = [] { auto e = fresh("TUBE SAT", {{Drive, 70}, {Bias, 0}}); return harmonicDb(*e, 200, 3, 0.3f); }();
        const double b0 = h2(0), b100 = h2(100);
        std::printf("  TUBE SAT: -50 dBFS at drive 100 %.2f dB; drive 70 at -10 dBFS: 2nd harmonic bias 0 %.1f dB, bias 100 %.1f dB, 3rd %.1f dB\n", q, b0, b100, h3);
        CHECK(std::fabs(q) < 0.3 && b100 > b0 + 20.0 && b100 > -40.0 && h3 > -40.0);
        auto dry = fresh("TUBE SAT", {{Mix, 0}, {Drive, 100}});
        Stereo s = noise(1.0, 0.5f), ref = s;
        run(*dry, s);
        CHECK(maxDelayedDiff(s, ref, dry->latency(), 0) < 1e-6);
    }
    {   // PREAMP: +20 dB is +20 dB (clean, quiet signal); PHASE flips; LOW CUT takes out 30 Hz
        using namespace preamp;
        auto e = fresh("PREAMP", {{Gain, 20}, {Character, 0}});
        const double g = toneOut(*e, 1000, 0.003f);
        auto ph = fresh("PREAMP", {{Phase, 1}, {Character, 0}});
        Stereo s = sine(1000, 0.5, 0.01f), ref = s;
        run(*ph, s);
        double sum = 0;
        for (std::size_t i = 12000; i < s.size(); ++i) sum += s.l[i] * ref.l[i - ph->latency()];
        auto lc = fresh("PREAMP", {{LowCut, 1}, {Character, 0}});
        const double low = toneOut(*lc, 30, 0.01f, 2.0);
        std::printf("  PREAMP: gain +20 -> %.2f dB, phase invert correlation %s, low cut 30 Hz %.1f dB\n", g, sum < 0 ? "negative" : "positive", low);
        CHECK(std::fabs(g - 20.0) < 0.3 && sum < 0 && low < -10.0);
    }
    {   // CASSETTE: TYPE IV keeps more top than TYPE I; WOW moves the pitch; HISS lands near -40 dBFS at 100 %
        using namespace cassette;
        auto top = [](float type) { auto e = fresh("CASSETTE", {{Type, type}, {Drive, 0}, {Wow, 0}, {Flutter, 0}}); return toneOut(*e, 12000, 0.05f); };
        const double t1 = top(0), t4 = top(2);
        auto spread = [](float wow) {
            auto e = fresh("CASSETTE", {{Wow, wow}, {Flutter, 0}, {Drive, 0}});
            Stereo s = sine(1000, 3.0, 0.1f);
            run(*e, s);
            return periodSpread(s.l, 4800);
        };
        const double still = spread(0), wobble = spread(100);
        auto hs = fresh("CASSETTE", {{Hiss, 100}});
        Stereo z(96000);
        run(*hs, z);
        const double hissDb = db(rms(z.l, 48000, 48000));
        std::printf("  CASSETTE: 12 kHz TYPE I %.1f dB, TYPE IV %.1f dB; period spread still %.4f, wow 100 %% %.4f samples; hiss %.1f dBFS RMS\n", t1, t4, still, wobble, hissDb);
        CHECK(t4 > t1 + 3.0 && wobble > 20.0 * std::max(still, 1e-4) && hissDb > -50.0 && hissDb < -30.0);
    }
    {   // VINYL: clicks appear with CRACKLE; with everything off it's the input, delayed
        using namespace vinyl;
        auto e = fresh("VINYL", {{Crackle, 100}, {Wear, 0}, {Warp, 0}});
        Stereo z(48000 * 4);
        run(*e, z);
        int clicks = 0;
        for (std::size_t i = 1; i < z.size(); ++i) clicks += std::fabs(z.l[i]) > 0.01f && std::fabs(z.l[i - 1]) <= 0.01f;
        auto clean = fresh("VINYL", {{Wear, 0}, {Warp, 0}, {MonoLows, 0}});
        Stereo s = noise(1.0, 0.3f), ref = s;
        run(*clean, s);
        const double d = maxDelayedDiff(s, ref, clean->latency(), 0);
        std::printf("  VINYL: crackle 100 %% -> %d click onsets in 4 s; all off: max difference from the input %.2e\n", clicks, d);
        CHECK(clicks > 40 && d < 1e-6);
    }
    {   // BIT CRUSHER: 4 bits = at most 17 levels; downsample 4 holds each value 4 samples; 24 bits / 1x is clean
        using namespace crusher;
        auto e = fresh("BIT CRUSHER", {{Bits, 4}, {Downsample, 4}});
        Stereo s = sine(97, 0.5, 0.9f);
        run(*e, s);
        std::set<float> levels(s.l.begin() + 4800, s.l.end());
        bool held = true;
        for (std::size_t i = 4800; i + 4 < s.size(); i += 4)
            for (int k = 1; k < 4; ++k) held = held && s.l[i + k] == s.l[i];
        auto hi = fresh("BIT CRUSHER", {{Bits, 24}, {Downsample, 1}});
        Stereo t = noise(0.5, 0.5f), ref = t;
        run(*hi, t);
        const double d = maxDelayedDiff(t, ref, 0, 0);
        std::printf("  BIT CRUSHER: 4 bits -> %zu levels, held for 4 samples %s; 24 bits 1x max difference %.2e\n", levels.size(), held ? "yes" : "no", d);
        CHECK(levels.size() <= 17 && held && d < 1e-6);
    }
    {   // TRANSFORMER: the lows saturate more than the highs; WEIGHT lifts 60 Hz
        using namespace transformer;
        auto h = [](double hz) {   // 3rd harmonic relative to what comes out at the fundamental
            auto e = fresh("TRANSFORMER", {{Drive, 100}, {Weight, 0}});
            return harmonicDb(*e, hz, 3, 0.4f) - harmonicDb(*e, hz, 1, 0.4f);
        };
        const double lo = h(50), hi = h(2000);
        auto w = fresh("TRANSFORMER", {{Drive, 0}, {Weight, 6}});
        const double bump = toneOut(*w, 60, 0.01f, 2.0);
        std::printf("  TRANSFORMER drive 100 at -8 dBFS: 3rd harmonic of 50 Hz %.1f dB under it, of 2 kHz %.1f dB; weight +6 at 60 Hz %.2f dB\n", lo, hi, bump);
        CHECK(lo > hi + 6.0 && std::fabs(bump - 6.0) < 0.5);
    }
    for (const char* n : {"TUBE SAT", "PREAMP", "CASSETTE", "VINYL", "BIT CRUSHER", "TRANSFORMER"}) {
        Stereo in = hotMix(1.0);
        const double d = blockSizeDifference(*findPlugin(n), [](hc::fx::Effect&) {}, in);
        std::printf("  %s block size 1 vs 512: max difference %.2e\n", n, d);
        CHECK(d < 1e-5);
    }
}

} // namespace hem::test

// ---- WAVE 9: HALL, ROOM, PLATE, SPRING, TAPE ECHO, DIGITAL DELAY ---------------------------------
namespace hem::test {
namespace {
Stereo impulse(double seconds, float amp = 1.0f) {
    Stereo s(static_cast<std::size_t>(seconds * kFs));
    s.l[0] = s.r[0] = amp;
    return s;
}
// Reverb time from an impulse response: Schroeder backward integration, the -5 .. -25 dB slope x 3.
double rt60(const Stereo& s) {
    std::vector<double> edc(s.size());
    double acc = 0;
    for (std::size_t i = s.size(); i-- > 0;) {
        acc += static_cast<double>(s.l[i]) * s.l[i] + static_cast<double>(s.r[i]) * s.r[i];
        edc[i] = acc;
    }
    const double top = edc[0];
    double t5 = -1, t25 = -1;
    for (std::size_t i = 0; i < edc.size(); ++i) {
        const double d = 10.0 * std::log10(edc[i] / top);
        if (t5 < 0 && d <= -5.0) t5 = static_cast<double>(i) / kFs;
        if (t25 < 0 && d <= -25.0) { t25 = static_cast<double>(i) / kFs; break; }
    }
    return t25 > 0 && t5 >= 0 ? 3.0 * (t25 - t5) : -1.0;
}
double correlation(const Stereo& s, std::size_t from) {
    double lr = 0, ll = 0, rr = 0;
    for (std::size_t i = from; i < s.size(); ++i) { lr += s.l[i] * s.r[i]; ll += s.l[i] * s.l[i]; rr += s.r[i] * s.r[i]; }
    return lr / std::sqrt(std::max(ll * rr, 1e-30));
}
std::size_t peakIndex(const std::vector<float>& x, std::size_t from, std::size_t to) {
    std::size_t k = from;
    for (std::size_t i = from; i < to && i < x.size(); ++i) if (std::fabs(x[i]) > std::fabs(x[k])) k = i;
    return k;
}
double energyAround(const std::vector<float>& x, std::size_t at, std::size_t half) {
    double e = 0;
    for (std::size_t i = at > half ? at - half : 0; i < at + half && i < x.size(); ++i) e += static_cast<double>(x[i]) * x[i];
    return e;
}
} // namespace

void testSpace() {
    std::printf("[space] HALL, ROOM, PLATE, SPRING, TAPE ECHO, DIGITAL DELAY\n");
    for (const char* n : {"HALL", "ROOM", "PLATE", "SPRING", "TAPE ECHO", "DIGITAL DELAY"}) {
        const PluginInfo* info = findPlugin(n);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
    {   // the reverbs: DECAY is the real reverb time; MIX 0 is the dry signal; the tail is wide
        using namespace verb;
        const struct { const char* n; float decay; } cases[] = {{"HALL", 3.0f}, {"HALL", 1.2f}, {"ROOM", 0.8f}, {"PLATE", 2.0f}};
        for (const auto& c : cases) {
            auto e = fresh(c.n, {{Decay, c.decay}, {Damping, 0}, {Mod, 0}, {LowCut, 20}, {HighCut, 20000}, {Mix, 100}, {PreDelay, 0}});
            Stereo s = impulse(c.decay * 2.0 + 0.5);
            run(*e, s);
            const double t = rt60(s), corr = correlation(s, 4800);
            std::printf("  %s decay %.1f s: measured reverb time %.2f s, left / right correlation %.2f\n", c.n, c.decay, t, corr);
            CHECK(t > c.decay * 0.75 && t < c.decay * 1.25);
            CHECK(std::fabs(corr) < 0.5);
        }
        auto dry = fresh("HALL", {{Mix, 0}});
        Stereo s = noise(1.0, 0.3f), ref = s;
        run(*dry, s);
        CHECK(maxDelayedDiff(s, ref, 0, 0) == 0.0);
        auto dark = fresh("HALL", {{Damping, 100}, {Mix, 100}}), bright = fresh("HALL", {{Damping, 0}, {Mix, 100}});
        Stereo a = noise(1.0, 0.3f), b = a;
        run(*dark, a);
        run(*bright, b);
        const double hd = bandLevelDb(a.l, 6000, 12000, 24000, 24000), hb = bandLevelDb(b.l, 6000, 12000, 24000, 24000);
        std::printf("  HALL 6-12 kHz in the tail: damping 100 %% %.1f dB, damping 0 %% %.1f dB\n", hd, hb);
        CHECK(hd < hb - 6.0);
    }
    {   // SPRING: it rings and dies away within DECAY
        using namespace spring;
        auto e = fresh("SPRING", {{Decay, 2.0f}, {Mix, 100}});
        Stereo s = impulse(3.0);
        run(*e, s);
        const double early = rms(s.l, 4800, 24000), late = rms(s.l, 96000, 24000);
        std::printf("  SPRING decay 2 s: level 0.1-0.6 s %.1f dB, 2.0-2.5 s %.1f dB\n", db(early), db(late));
        CHECK(db(early) > -60.0 && db(late) < db(early) - 30.0);
    }
    {   // TAPE ECHO: the first repeat lands exactly at TIME; the heads at 2x and 3x; 110 % feedback stays bounded
        using namespace tapeecho;
        auto e = fresh("TAPE ECHO", {{Time, 250}, {Wobble, 0}, {Feedback, 0}, {Mix, 100}});
        Stereo s = impulse(1.0, 0.5f);
        run(*e, s);
        const std::size_t k = peakIndex(s.l, 1, s.size());
        auto e3 = fresh("TAPE ECHO", {{Time, 100}, {Heads, 2}, {Wobble, 0}, {Feedback, 0}, {Mix, 100}});
        Stereo t = impulse(1.0, 0.5f);
        run(*e3, t);
        const std::size_t h2 = peakIndex(t.l, 6000, 12000), h3 = peakIndex(t.l, 12000, 18000);
        auto run110 = fresh("TAPE ECHO", {{Time, 120}, {Feedback, 110}, {Mix, 100}});
        Stereo u = impulse(10.0, 0.5f);
        run(*run110, u);
        std::printf("  TAPE ECHO: 250 ms repeat at sample %zu (12000); heads 2 and 3 at %zu and %zu (9600, 14400); 110 %% feedback for 10 s peaks at %.2f\n", k, h2, h3, peak(u));
        CHECK(k == 12000 && h2 == 9600 && h3 == 14400);
        CHECK(finite(u) && peak(u) < 1.5f && peak(u) > 0.1f);
    }
    {   // DIGITAL DELAY: exact time, feedback level, note values at BPM, ping-pong alternates sides
        using namespace ddelay;
        auto e = fresh("DIGITAL DELAY", {{Division, 0}, {Time, 250}, {Feedback, 50}, {Mix, 100}, {LowCut, 20}, {HighCut, 20000}});
        Stereo s = impulse(1.0);
        run(*e, s);
        const std::size_t k = peakIndex(s.l, 1, 18000);
        const double second = 10.0 * std::log10(energyAround(s.l, 24000, 400) / energyAround(s.l, 12000, 400));
        auto q = fresh("DIGITAL DELAY", {{Division, 1}, {Bpm, 120}, {Feedback, 0}, {Mix, 100}});
        Stereo t = impulse(1.0);
        run(*q, t);
        const std::size_t kq = peakIndex(t.l, 1, t.size());
        auto pp = fresh("DIGITAL DELAY", {{Division, 0}, {Time, 100}, {Feedback, 60}, {PingPong, 1}, {Mix, 100}});
        Stereo u = impulse(1.0);
        run(*pp, u);
        const double l1 = energyAround(u.l, 4800, 200), r1 = energyAround(u.r, 4800, 200), l2 = energyAround(u.l, 9600, 200), r2 = energyAround(u.r, 9600, 200);
        std::printf("  DIGITAL DELAY: 250 ms echo at %zu (12000), value %.3f, second repeat %.1f dB (feedback 50 %% = -6.0); 1/4 at 120 BPM at %zu (24000); ping-pong 1st L/R %.2f / %.2f, 2nd %.2f / %.2f\n",
                    k, s.l[k], second, kq, l1, r1, l2, r2);
        CHECK(k == 12000 && std::fabs(s.l[k] - 1.0f) < 1e-6f && std::fabs(second + 6.0) < 1.5 && kq == 24000);
        CHECK(l1 > 0.1 && r1 < 1e-6 && r2 > 0.05 && l2 < 1e-6);
    }
    for (const char* n : {"HALL", "ROOM", "PLATE", "SPRING", "TAPE ECHO", "DIGITAL DELAY"}) {
        Stereo in = hotMix(1.0);
        const double d = blockSizeDifference(*findPlugin(n), [](hc::fx::Effect&) {}, in);
        std::printf("  %s block size 1 vs 512: max difference %.2e\n", n, d);
        CHECK(d < 1e-5);
    }
}

} // namespace hem::test

// ---- WAVE 10: CHORUS, FLANGER, PHASER, TREMOLO, ROTARY, AUTO FILTER ------------------------------
namespace hem::test {

void testMovement() {
    std::printf("[movement] CHORUS, FLANGER, PHASER, TREMOLO, ROTARY, AUTO FILTER\n");
    for (const char* n : {"CHORUS", "FLANGER", "PHASER", "TREMOLO", "ROTARY", "AUTO FILTER"}) {
        const PluginInfo* info = findPlugin(n);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
    {   // CHORUS: MIX 0 = dry; it wobbles the pitch and spreads a mono source
        using namespace chorus;
        auto dry = fresh("CHORUS", {{Mix, 0}});
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*dry, s);
        CHECK(maxDelayedDiff(s, ref, 0, 0) == 0.0);
        auto e = fresh("CHORUS", {{Mix, 100}, {Depth, 100}, {Rate, 1}});
        Stereo t = noise(2.0, 0.3f);
        for (std::size_t i = 0; i < t.size(); ++i) t.r[i] = t.l[i];
        run(*e, t);
        const double corr = correlation(t, 9600);
        auto w = fresh("CHORUS", {{Mix, 100}, {Depth, 100}, {Rate, 1}, {Voices, 0}});
        Stereo u = sine(1000, 3.0, 0.3f);
        run(*w, u);
        std::printf("  CHORUS: mono noise in, left / right correlation %.2f; 1 kHz period spread %.3f samples\n", corr, periodSpread(u.l, 4800));
        CHECK(corr < 0.8 && periodSpread(u.l, 4800) > 0.05);
    }
    {   // FLANGER: still (depth 0) at 1 ms, the comb: 500 Hz cancels, 1 kHz passes
        using namespace flanger;
        auto f = [](double hz) { auto e = fresh("FLANGER", {{Depth, 0}, {Manual, 1.0f}, {Feedback, 0}, {Mix, 50}}); return toneOut(*e, hz, 0.2f); };
        const double n500 = f(500), p1k = f(1000);
        std::printf("  FLANGER at 1 ms, mix 50 %%: 500 Hz %.1f dB (notch), 1 kHz %.2f dB\n", n500, p1k);
        CHECK(n500 < -30.0 && std::fabs(p1k) < 0.2);
    }
    {   // PHASER: still, the stages make a deep notch somewhere; MIX 0 = dry
        using namespace phaser;
        double lowest = 0;
        for (double hz = 100; hz < 10000; hz *= 1.03) {
            auto e = fresh("PHASER", {{Depth, 0}, {Center, 1000}, {Stages, 0}, {Feedback, 0}, {Mix, 50}});
            lowest = std::min(lowest, toneOut(*e, hz, 0.2f, 0.5));
        }
        std::printf("  PHASER 4 stages, still at 1 kHz: deepest notch %.1f dB\n", lowest);
        CHECK(lowest < -20.0);
    }
    {   // TREMOLO: depth 100 swings the level all the way; AUTO PAN moves left against right
        using namespace tremolo;
        auto e = fresh("TREMOLO", {{Depth, 100}, {Rate, 4}, {Pan, 1}});
        Stereo s = sine(1000, 2.0, 0.3f);
        run(*e, s);
        double lo = 1e9, hi = 0, envCorr = 0;
        for (std::size_t w = 4800; w + 480 < s.size(); w += 480) {
            const double l = rms(s.l, w, 480), r = rms(s.r, w, 480);
            lo = std::min(lo, l);
            hi = std::max(hi, l);
            envCorr += (l - 0.106) * (r - 0.106);
        }
        std::printf("  TREMOLO depth 100: level swings %.1f dB; auto pan left / right envelopes move %s\n", db(hi / std::max(lo, 1e-9)), envCorr < 0 ? "opposite" : "together");
        CHECK(db(hi / std::max(lo, 1e-9)) > 20.0 && envCorr < 0);
    }
    {   // ROTARY: FAST spins the level and pitch faster than SLOW, and the horn gets there first
        using namespace rotary;
        auto swing = [](float speed) {
            auto e = fresh("ROTARY", {{Speed, speed}, {Drive, 0}});
            Stereo s = sine(3000, 3.0, 0.2f);
            run(*e, s);
            int crossings = 0;
            double prev = 0;
            const double mean = rms(s.l, 48000, 96000);
            for (std::size_t w = 48000; w + 240 < s.size(); w += 240) {
                const double v = rms(s.l, w, 240) - mean;
                crossings += (prev < 0 && v >= 0);
                prev = v;
            }
            return crossings / 2.0;   // level cycles per second (over 2 s)
        };
        const double slow = swing(0), fast = swing(1);
        std::printf("  ROTARY horn level cycles per second: SLOW %.1f, FAST %.1f\n", slow, fast);
        CHECK(fast > 3.0 * std::max(slow, 0.5) && fast < 9.0);
    }
    {   // AUTO FILTER: still, low-pass at 500 Hz: 100 Hz through, 5 kHz far down; ENVELOPE opens it on loud input
        using namespace autofilter;
        auto lp = [](double hz, float env, float amp) { auto e = fresh("AUTO FILTER", {{Type, 0}, {Cutoff, 500}, {Resonance, 0.7071f}, {Envelope, env}, {Depth, 0}}); return toneOut(*e, hz, amp); };
        const double pass = lp(100, 0, 0.3f), stop = lp(5000, 0, 0.3f), open = lp(5000, 100, 0.3f);
        std::printf("  AUTO FILTER low-pass 500 Hz: 100 Hz %.2f dB, 5 kHz %.1f dB; with ENVELOPE 100 on a -10 dBFS tone 5 kHz %.1f dB\n", pass, stop, open);
        CHECK(std::fabs(pass) < 0.3 && stop < -30.0 && open > stop + 20.0);
    }
    for (const char* n : {"CHORUS", "FLANGER", "PHASER", "TREMOLO", "ROTARY", "AUTO FILTER"}) {
        Stereo in = hotMix(1.0);
        const double d = blockSizeDifference(*findPlugin(n), [](hc::fx::Effect&) {}, in);
        std::printf("  %s block size 1 vs 512: max difference %.2e\n", n, d);
        CHECK(d < 1e-5);
    }
}

} // namespace hem::test

// ---- WAVE 11: SUB, BASS TIGHTENER, VOCAL STRIP ---------------------------------------------------
namespace hem::test {

void testSpecialty() {
    std::printf("[specialty] SUB, BASS TIGHTENER, VOCAL STRIP\n");
    for (const char* n : {"SUB", "BASS TIGHTENER", "VOCAL STRIP"}) {
        const PluginInfo* info = findPlugin(n);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
    {   // SUB: AMOUNT 0 = the input exactly; OCTAVE puts 40 Hz under an 80 Hz bass; HARMONICS adds 100 / 150 Hz to 50 Hz
        using namespace sub;
        auto off = fresh("SUB", {{Amount, 0}});
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*off, s);
        CHECK(maxDelayedDiff(s, ref, 0, 0) == 0.0);
        auto oct = fresh("SUB", {{Mode, 0}, {Amount, 80}, {Freq, 100}});
        Stereo b = sine(80, 2.0, 0.3f);
        run(*oct, b);
        const double sub40 = db(toneAmp(b.l, 40, 48000, 48000) / 0.3);
        auto har = fresh("SUB", {{Mode, 1}, {Amount, 80}, {Freq, 80}});
        Stereo c = sine(50, 2.0, 0.3f);
        run(*har, c);
        const double h2 = db(toneAmp(c.l, 100, 48000, 48000) / 0.3), h3 = db(toneAmp(c.l, 150, 48000, 48000) / 0.3);
        std::printf("  SUB: OCTAVE on 80 Hz adds 40 Hz at %.1f dB; HARMONICS on 50 Hz adds 100 Hz %.1f dB, 150 Hz %.1f dB\n", sub40, h2, h3);
        CHECK(sub40 > -20.0 && h2 > -25.0 && h3 > -25.0);
    }
    {   // BASS TIGHTENER: at rest flat; TIGHTEN holds a loud 50 Hz down and leaves 1 kHz; MONO takes the low sides out
        using namespace tightener;
        double worst = 0;
        for (double hz : {40.0, 120.0, 400.0, 3000.0}) { auto e = fresh("BASS TIGHTENER", {}); worst = std::max(worst, std::fabs(toneOut(*e, hz, 0.2f))); }
        auto t = fresh("BASS TIGHTENER", {{Tighten, 100}});
        Stereo s = sine(50, 1.5, 0.5f);
        add(s, sine(1000, 1.5, 0.05f));
        run(*t, s);
        const double lo = db(toneAmp(s.l, 50, 48000, 12000) / 0.5), mid = db(toneAmp(s.l, 1000, 48000, 12000) / 0.05);
        auto m = fresh("BASS TIGHTENER", {{Mono, 1}});
        Stereo u = sine(50, 1.5, 0.3f, kFs, true);
        run(*m, u);
        const double side = db(toneAmp(u.l, 50, 48000, 12000) / 0.3);
        std::printf("  BASS TIGHTENER: at rest within %.3f dB; TIGHTEN 100 on -6 dBFS 50 Hz %.1f dB, 1 kHz %.2f dB; MONO, 50 Hz sides %.1f dB\n", worst, lo, mid, side);
        CHECK(worst < 0.05 && lo < -8.0 && std::fabs(mid) < 0.3 && side < -20.0);
    }
    {   // VOCAL STRIP: all at 0 = exactly the input; HIGH-PASS, PRESENCE and COMPRESS do what they say
        using namespace vocal;
        auto e = fresh("VOCAL STRIP", {});
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, 0, 0);
        auto hp = fresh("VOCAL STRIP", {{Hpf, 4}});
        const double cut = toneOut(*hp, 40, 0.1f, 1.5);
        auto pr = fresh("VOCAL STRIP", {{Presence, 6}});
        const double pres = toneOut(*pr, 4000, 0.05f);
        auto cp = fresh("VOCAL STRIP", {{Compress, 100}});
        const double loud = toneOut(*cp, 1000, 0.5f), quiet = toneOut(*cp, 1000, 0.005f);
        std::printf("  VOCAL STRIP: neutral max difference %.2e; 120 Hz high-pass at 40 Hz %.1f dB; presence +6 at 4 kHz %.2f dB; compress 100: -6 dBFS %+.1f dB, -46 dBFS %+.1f dB\n",
                    d, cut, pres, loud, quiet);
        CHECK(d == 0.0 && cut < -15.0 && std::fabs(pres - 6.0) < 0.2 && quiet - loud > 10.0);
    }
    for (const char* n : {"SUB", "BASS TIGHTENER", "VOCAL STRIP"}) {
        Stereo in = hotMix(1.0);
        const double d = blockSizeDifference(*findPlugin(n), [](hc::fx::Effect& x) { if (x.params().size() > 2) x.set(2, 50); }, in);
        std::printf("  %s block size 1 vs 512: max difference %.2e\n", n, d);
        CHECK(d < 1e-5);
    }
}

} // namespace hem::test
