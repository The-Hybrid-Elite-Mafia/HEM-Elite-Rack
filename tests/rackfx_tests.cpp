// ELITE EQ, RESTORER and the spectrum display: what each must actually do to the sound,
// measured, plus the rules every HEM effect follows (silence stays silence, no allocation on
// the audio thread, same result for any block size, finite output, constant latency).
#include "Harness.hpp"
#include "Signals.hpp"

#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
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
