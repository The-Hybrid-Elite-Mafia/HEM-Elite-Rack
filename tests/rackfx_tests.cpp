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
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
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
