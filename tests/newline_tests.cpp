// =============================================================================
//  The staging list (the 47 new HEM engines, Batches 1–10) inside hem_rack_tests.
//  Every engine in hem::staging::plugins() gets, automatically:
//    1. parameter rules: unique ids, sane ranges, default inside its range, choices named
//    2. dead flat on load: output == input (after its reported latency) within 0.1 dB and a
//       residual below -60 dB, at 44.1, 48 and 96 kHz
//    3. stability: every control at its minimum and maximum, then 24 random settings, at block
//       sizes 7 / 64 / 512 / 4096, with MIDI notes and a sidechain where the engine takes them:
//       no NaN, no Inf, nothing above +40 dBFS
//    4. no heap allocation on the audio path (defaults and a busy random setting)
//    5. latency: the reported latency is the real delay of an impulse, and it never changes
//       with any control (the VST3 wrapper reports latency() to the DAW once)
//    6. clicks / zipper noise: every smooth control (not switches) jumped mid-stream on a clean
//       two-tone signal; the jump may not make a spike in the signal's curvature more than
//       12 dB above the steady signal before or after it
//  Plus engine-specific cases (NoteForge pitch accuracy, +/-5 cents, approved 2026-11-27).
// =============================================================================
#include "Harness.hpp"
#include "Signals.hpp"

#include "hybrid/fx/Dsp.hpp"

#include "hem/rack/Notes.hpp"
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <random>
#include <set>
#include <string>

namespace hem::test {
namespace {
using hc::fx::Effect;
using hc::fx::ParamInfo;
using hc::fx::Taper;
namespace st = hem::staging;

constexpr std::uint32_t kBlock = 512;

double rmsOf(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double s = 0; std::size_t n = 0;
    for (std::size_t i = from; i < to && i < v.size(); ++i) { s += static_cast<double>(v[i]) * v[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}
double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

Stereo testSignal(double fs, double seconds) {
    Stereo s = noise(seconds, 0.2f, 11, fs);
    add(s, sine(997.0, seconds, 0.2f, fs));
    add(s, sine(73.0, seconds, 0.2f, fs));
    return s;
}

// ---- 1. parameter rules --------------------------------------------------------------------
void checkParams(const st::StagingInfo& info, Effect& e) {
    const auto ps = e.params();
    CHECK(ps.size() > 0);
    CHECK(ps.size() <= static_cast<std::size_t>(Effect::kMaxParams));
    std::set<std::string> ids;
    int bad = 0;
    for (const ParamInfo& p : ps) {
        const bool ok = p.id && *p.id && p.name && *p.name && p.min < p.max && p.def >= p.min && p.def <= p.max &&
                        (p.taper != Taper::Log || p.min > 0.0f) &&
                        (p.taper != Taper::Toggle || (p.min == 0.0f && p.max == 1.0f)) &&
                        (p.taper != Taper::Choice || (p.choices && p.min == 0.0f && std::floor(p.max) == p.max));
        if (!ok) { std::printf("  %s: bad parameter %s\n", info.name, p.id ? p.id : "(null)"); ++bad; }
        if (!ids.insert(p.id ? p.id : "").second) { std::printf("  %s: duplicate parameter id %s\n", info.name, p.id); ++bad; }
        if (p.taper == Taper::Choice && p.choices)
            for (int c = 0; c <= static_cast<int>(p.max); ++c) if (!p.choices[c] || !*p.choices[c]) { std::printf("  %s: %s choice %d unnamed\n", info.name, p.id, c); ++bad; }
    }
    CHECK(bad == 0);
    // set() clamps: a value far outside the range lands on the edge
    for (std::size_t i = 0; i < ps.size(); ++i) {
        e.set(static_cast<int>(i), ps[i].max + 1e6f);
        CHECK(e.get(static_cast<int>(i)) <= ps[i].max);
        e.set(static_cast<int>(i), ps[i].min - 1e6f);
        CHECK(e.get(static_cast<int>(i)) >= ps[i].min);
    }
    e.setDefaults();
}

// ---- 2. dead flat on load ------------------------------------------------------------------
void checkNeutral(const st::StagingInfo& info) {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        auto e = info.create();
        e->prepare(fs, kBlock);
        e->setDefaults();
        e->reset();
        const std::uint32_t lat = e->latency();
        Stereo in = testSignal(fs, 1.5);
        Stereo out = in;
        run(*e, out, kBlock);
        std::vector<float> resL(in.size(), 0.0f), resR(in.size(), 0.0f), refL(in.size(), 0.0f), outL(in.size(), 0.0f);
        const std::size_t from = lat + static_cast<std::size_t>(0.1 * fs);
        for (std::size_t i = from; i < in.size(); ++i) {
            resL[i] = out.l[i] - in.l[i - lat];
            resR[i] = out.r[i] - in.r[i - lat];
            refL[i] = in.l[i - lat];
            outL[i] = out.l[i];
        }
        const double ref = rmsOf(refL, from, in.size());
        const double levelDiff = db(rmsOf(outL, from, in.size())) - db(ref);
        const double residual = db(std::max(rmsOf(resL, from, in.size()), rmsOf(resR, from, in.size()))) - db(ref);
        const bool ok = std::fabs(levelDiff) <= 0.1 && residual <= -60.0;
        if (!ok || fs == 48000.0)
            std::printf("  %-26s %5.1f kHz  latency %4u  level %+.3f dB  residual %7.1f dB  %s\n", info.name, fs / 1000.0, lat, levelDiff, residual, ok ? "flat" : "NOT FLAT");
        CHECK(ok);
    }
}

// ---- 3. stability ----------------------------------------------------------------------------
struct Feeds {   // MIDI notes and a sidechain for engines that take them
    void before(Effect& e, std::uint32_t n, const float* scl, const float* scr, std::mt19937& rng) {
        if (auto* ns = dynamic_cast<hem::rack::NoteSink*>(&e)) {
            if (rng() % 3 == 0) ns->noteOn(36 + static_cast<int>(rng() % 36), 0.8f, 0);
            if (rng() % 5 == 0) ns->noteOff(36 + static_cast<int>(rng() % 36), 0);
        }
        if (auto* sc = dynamic_cast<hem::rack::SidechainSink*>(&e)) sc->setSidechain(scl, scr);
        (void)n;
    }
};

bool runChecked(Effect& e, Stereo& s, const Stereo& side, std::uint32_t block, std::mt19937& rng, float& worst) {
    Feeds f;
    bool ok = true;
    for (std::size_t i = 0; i < s.size(); i += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, s.size() - i));
        f.before(e, n, side.l.data() + i, side.r.data() + i, rng);
        e.process(s.l.data() + i, s.r.data() + i, n);
        for (std::uint32_t k = 0; k < n; ++k) {
            const float a = s.l[i + k], b = s.r[i + k];
            if (!std::isfinite(a) || !std::isfinite(b)) ok = false;
            worst = std::max({worst, std::isfinite(a) ? std::fabs(a) : 1e9f, std::isfinite(b) ? std::fabs(b) : 1e9f});
        }
    }
    return ok && worst <= 100.0f;   // +40 dBFS
}

void checkStability(const st::StagingInfo& info) {
    const double fs = 48000.0;
    auto e = info.create();
    e->prepare(fs, Effect::kMaxBlock);
    const auto ps = e->params();
    std::mt19937 rng(1234);
    const std::array<std::uint32_t, 4> blocks{7, 64, 512, 4096};
    int fails = 0;
    float worst = 0.0f;
    const Stereo side = testSignal(fs, 0.3);   // as long as the longest run below (the host's sidechain covers every block)
    // every control alone at its edges
    for (std::size_t i = 0; i < ps.size(); ++i) {
        for (float v : {ps[i].min, ps[i].max}) {
            e->setDefaults(); e->reset();
            e->set(static_cast<int>(i), v);
            Stereo s = testSignal(fs, 0.15);
            for (auto& x : s.l) x *= 2.5f;   // loud: peaks near 0 dBFS
            for (auto& x : s.r) x *= 2.5f;
            float w = 0;
            if (!runChecked(*e, s, side, blocks[i % blocks.size()], rng, w)) { std::printf("  %s: %s = %g unstable (peak %g)\n", info.name, ps[i].id, static_cast<double>(v), static_cast<double>(w)); ++fails; }
            worst = std::max(worst, w);
        }
    }
    // random whole settings, changed mid-stream
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int t = 0; t < 24; ++t) {
        e->reset();
        for (std::size_t i = 0; i < ps.size(); ++i) e->set(static_cast<int>(i), ps[i].min + u(rng) * (ps[i].max - ps[i].min));
        Stereo s = testSignal(fs, 0.3);
        float w = 0;
        if (!runChecked(*e, s, side, blocks[static_cast<std::size_t>(t) % blocks.size()], rng, w)) { std::printf("  %s: random setting %d unstable (peak %g)\n", info.name, t, static_cast<double>(w)); ++fails; }
        worst = std::max(worst, w);
    }
    std::printf("  %-26s stability: %zu controls x 2 edges + 24 random settings, loudest %+.1f dBFS%s\n", info.name, ps.size(),
                20.0 * std::log10(std::max(static_cast<double>(worst), 1e-9)), fails ? "  FAILED" : "");
    CHECK(fails == 0);
}

// ---- 4. no allocation on the audio path ------------------------------------------------------
void checkNoAlloc(const st::StagingInfo& info) {
    auto e = info.create();
    e->prepare(48000.0, kBlock);
    Stereo s = testSignal(48000.0, 0.5);
    std::uint64_t count = 0;
    {
        AllocScope scope;
        run(*e, s, kBlock);
        count += scope.count();
    }
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const auto ps = e->params();
    for (std::size_t i = 0; i < ps.size(); ++i) e->set(static_cast<int>(i), ps[i].min + u(rng) * (ps[i].max - ps[i].min));
    Stereo s2 = testSignal(48000.0, 0.5);
    const Stereo side = testSignal(48000.0, 0.5);
    {
        AllocScope scope;
        Feeds f;
        for (std::size_t i = 0; i < s2.size(); i += kBlock) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(kBlock, s2.size() - i));
            f.before(*e, n, side.l.data() + i, side.r.data() + i, rng);
            e->process(s2.l.data() + i, s2.r.data() + i, n);
        }
        count += scope.count();
    }
    if (count) std::printf("  %s: %llu heap allocations on the audio path\n", info.name, static_cast<unsigned long long>(count));
    CHECK(count == 0);
}

// ---- 5. latency is real and constant ----------------------------------------------------------
void checkLatency(const st::StagingInfo& info) {
    const double fs = 48000.0;
    auto e = info.create();
    e->prepare(fs, kBlock);
    const std::uint32_t lat = e->latency();
    Stereo s(static_cast<std::size_t>(fs * 0.5));
    s.l[1000] = 0.5f; s.r[1000] = 0.5f;
    run(*e, s, kBlock);
    std::size_t at = 0;
    for (std::size_t i = 0; i < s.size(); ++i) if (std::fabs(s.l[i]) > std::fabs(s.l[at])) at = i;
    const bool delayOk = at == 1000 + lat;
    const auto ps = e->params();
    int changes = 0;
    for (std::size_t i = 0; i < ps.size(); ++i)
        for (float v : {ps[i].min, ps[i].max, 0.5f * (ps[i].min + ps[i].max)}) {
            e->setDefaults(); e->set(static_cast<int>(i), v);
            if (e->latency() != lat) { if (changes++ < 3) std::printf("  %s: latency %u -> %u when %s = %g\n", info.name, lat, e->latency(), ps[i].id, static_cast<double>(v)); }
        }
    if (!delayOk) std::printf("  %s: reports %u samples, impulse came out %zd late\n", info.name, lat, static_cast<std::ptrdiff_t>(at) - 1000);
    CHECK(delayOk);
    CHECK(changes == 0);
}

// ---- 6. clicks / zipper noise on a sudden control move -------------------------------------
void checkClicks(const st::StagingInfo& info) {
    const double fs = 48000.0;
    auto probe = info.create();
    const auto ps = probe->params();
    int fails = 0, tested = 0;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (ps[i].taper == Taper::Choice || ps[i].taper == Taper::Toggle) continue;   // switches may click, like hardware
        auto e = info.create();
        e->prepare(fs, 64);
        const std::uint32_t lat = e->latency();
        const std::size_t jump = static_cast<std::size_t>(0.3 * fs);
        const std::size_t total = jump + lat + static_cast<std::size_t>(0.4 * fs);
        Stereo s = sine(1000.0, static_cast<double>(total) / fs, 0.25f, fs);
        add(s, sine(150.0, static_cast<double>(total) / fs, 0.25f, fs));
        const float span = ps[i].max - ps[i].min;
        const float to = std::fabs(ps[i].def - (ps[i].min + 0.8f * span)) > 0.1f * span ? ps[i].min + 0.8f * span : ps[i].min + 0.2f * span;
        for (std::size_t k = 0; k < s.size(); k += 64) {
            if (k == jump) e->set(static_cast<int>(i), to);
            e->process(s.l.data() + k, s.r.data() + k, static_cast<std::uint32_t>(std::min<std::size_t>(64, s.size() - k)));
        }
        auto curv = [&](std::size_t a, std::size_t b) {
            double m = 0;
            for (std::size_t k = std::max<std::size_t>(a, 2); k < b && k < s.size(); ++k)
                m = std::max({m, static_cast<double>(std::fabs(s.l[k] - 2 * s.l[k - 1] + s.l[k - 2])), static_cast<double>(std::fabs(s.r[k] - 2 * s.r[k - 1] + s.r[k - 2]))});
            return m;
        };
        const std::size_t ms = static_cast<std::size_t>(fs / 1000);
        const double spike = curv(jump - 64, jump + lat + 60 * ms);
        const double before = curv(jump + lat - 150 * ms, jump + lat - 30 * ms);
        const double after = curv(jump + lat + 150 * ms, jump + lat + 350 * ms);
        const double ref = std::max(before, after);
        ++tested;
        if (spike > 4.0 * ref + 1e-4) {
            std::printf("  %s: %s jump to %g clicks: spike %+.1f dB over the steady signal\n", info.name, ps[i].id, static_cast<double>(to), 20.0 * std::log10(spike / std::max(ref, 1e-9)));
            ++fails;
        }
    }
    std::printf("  %-26s clicks: %d smooth controls jumped, %d clicked\n", info.name, tested, fails);
    CHECK(fails == 0);
}

// ---- NoteForge: pitch accuracy (+/-5 cents) ----------------------------------------------------
// A sung-like tone (six harmonics), measured with a fine single-frequency scan of the output's
// fundamental over its last three seconds — an independent measurement, not NoteForge's own detector.
Stereo voiceTone(double hz, double seconds, double fs, double vibCents = 0.0, double vibHz = 5.5) {
    Stereo s(static_cast<std::size_t>(seconds * fs));
    double ph = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const double f = hz * std::exp2(vibCents / 1200.0 * std::sin(2 * std::numbers::pi * vibHz * t));
        ph += f / fs;
        double v = 0;
        for (int h = 1; h <= 6; ++h) v += std::sin(2 * std::numbers::pi * ph * h) / h;
        s.l[i] = s.r[i] = static_cast<float>(0.25 * v);
    }
    return s;
}
double measuredPitch(const std::vector<float>& x, double expect, std::size_t from, std::size_t len, double fs) {
    double best = expect, bestA = -1;
    const double lo = expect * std::exp2(-80.0 / 1200.0), hi = expect * std::exp2(80.0 / 1200.0);
    for (double f = lo; f <= hi; f += 0.05) {
        const double a = toneAmp(x, f, from, len, fs);
        if (a > bestA) { bestA = a; best = f; }
    }
    return best;
}

void testNoteForgePitch() {
    std::printf("[NOTEFORGE pitch accuracy: within +/-5 cents]\n");
    const st::StagingInfo* info = st::find("NOTEFORGE");
    CHECK(info != nullptr);
    if (!info) return;
    struct Case { const char* what; double inHz, vib, expectHz; int key, scale, formant; };
    const double fs = 48000.0;
    const Case cases[] = {
        {"452 Hz -> A4, chromatic", 452.0, 0.0, 440.0, 0, 0, 1},
        {"270 Hz -> C#4, chromatic", 270.0, 0.0, 277.1826, 0, 0, 1},
        {"285 Hz -> D4, C major", 285.0, 0.0, 293.6648, 0, 1, 1},
        {"223 Hz +/-25c vibrato -> A3, vibrato 0 %", 223.0, 25.0, 220.0, 0, 0, 1},
        {"452 Hz -> A4, formant non-corrected", 452.0, 0.0, 440.0, 0, 0, 0},
        {"190 Hz -> G3 (196), G minor", 190.0, 0.0, 195.9977, 7, 2, 1},
    };
    for (const Case& c : cases) {
        auto e = info->create();
        e->prepare(fs, kBlock);
        const auto ps = e->params();
        auto idx = [&](const char* id) { for (std::size_t i = 0; i < ps.size(); ++i) if (std::strcmp(ps[i].id, id) == 0) return static_cast<int>(i); return -1; };
        e->set(idx("correction"), 100.0f);
        e->set(idx("speed"), 0.1f);
        e->set(idx("transition"), 0.1f);
        e->set(idx("vibrato"), 0.0f);
        e->set(idx("key"), static_cast<float>(c.key));
        e->set(idx("scale"), static_cast<float>(c.scale));
        e->set(idx("formant"), static_cast<float>(c.formant));
        Stereo s = voiceTone(c.inHz, 4.0, fs, c.vib);
        run(*e, s, kBlock);
        const std::size_t len = static_cast<std::size_t>(3.0 * fs);   // 3 s: 0.33 Hz resolution
        const double got = measuredPitch(s.l, c.expectHz, s.size() - len, len, fs);
        const double cents = 1200.0 * std::log2(got / c.expectHz);
        const double det = e->meter(st::noteforge::kMeterDetectedHz);
        const double detCents = c.vib == 0.0 ? 1200.0 * std::log2(std::max(det, 1.0) / c.inHz) : 0.0;
        const bool ok = std::fabs(cents) <= 5.0 && std::fabs(detCents) <= 5.0;
        std::printf("  %-44s out %8.3f Hz (%+5.2f cents)  detector %+5.2f cents  %s\n", c.what, got, cents, detCents, ok ? "pass" : "FAIL");
        CHECK(ok);
    }
}
// ---- does each engine do its job? (one measured fingerprint per engine) -----------------------
int idxOf(const Effect& e, const char* id) {
    const auto ps = e.params();
    for (std::size_t i = 0; i < ps.size(); ++i) if (std::strcmp(ps[i].id, id) == 0) return static_cast<int>(i);
    std::printf("  missing parameter %s\n", id);
    return -1;
}
std::unique_ptr<Effect> made(const char* name) {
    auto e = st::find(name)->create();
    e->prepare(48000.0, kBlock);
    return e;
}
double toneDb(const std::vector<float>& x, double hz, std::size_t from) { return db(toneAmp(x, hz, from, 24000, 48000.0)); }

void testJobs() {
    std::printf("[STAGING: each engine does its job]\n");
    const double fs = 48000.0;
    auto report = [](const char* what, bool ok, const char* detail) { std::printf("  %-58s %s  %s\n", what, detail, ok ? "pass" : "FAIL"); CHECK(ok); };
    char buf[160];
    {   // DYNAMIC EQ 8: band 4 (800 Hz) at +6 dB lifts 800 Hz by 6 dB
        auto e = made("DYNAMIC EQ 8");
        e->set(idxOf(*e, "b4_gain"), 6.0f);
        Stereo s = sine(800.0, 1.0, 0.1f, fs); const double in = toneDb(s.l, 800.0, 24000);
        run(*e, s); const double out = toneDb(s.l, 800.0, 24000);
        std::snprintf(buf, sizeof buf, "%+.2f dB", out - in);
        report("DYNAMIC EQ 8: band 4 +6 dB at 800 Hz", std::fabs(out - in - 6.0) < 0.2, buf);
    }
    {   // DYNAMIC EQ 8: a dynamic band cuts only the loud part
        auto e = made("DYNAMIC EQ 8");
        e->set(idxOf(*e, "b4_dyn"), 1.0f); e->set(idxOf(*e, "b4_thr"), -30.0f); e->set(idxOf(*e, "b4_range"), -12.0f);
        Stereo quiet = sine(800.0, 1.0, 0.01f, fs), loud = sine(800.0, 1.0, 0.5f, fs);
        const double q0 = toneDb(quiet.l, 800, 24000), l0 = toneDb(loud.l, 800, 24000);
        run(*e, quiet); e->reset(); run(*e, loud);
        const double dq = toneDb(quiet.l, 800, 24000) - q0, dl = toneDb(loud.l, 800, 24000) - l0;
        std::snprintf(buf, sizeof buf, "quiet %+.1f dB, loud %+.1f dB", dq, dl);
        report("DYNAMIC EQ 8: dynamic band leaves quiet alone, cuts loud", std::fabs(dq) < 0.5 && dl < -6.0 && dl > -12.5, buf);
    }
    {   // THROATSHIFT: PITCH +12 at 100 % mix turns 220 Hz into 440 Hz
        auto e = made("THROATSHIFT");
        e->set(idxOf(*e, "pitch"), 12.0f); e->set(idxOf(*e, "mix"), 100.0f);
        Stereo s = voiceTone(220.0, 3.0, fs);
        run(*e, s);
        const double got = measuredPitch(s.l, 440.0, s.size() - 48000, 48000, fs);
        std::snprintf(buf, sizeof buf, "%.2f Hz (%+.1f cents)", got, 1200 * std::log2(got / 440.0));
        report("THROATSHIFT: +12 st moves 220 Hz to 440 Hz", std::fabs(1200 * std::log2(got / 440.0)) < 5.0, buf);
    }
    {   // VOICEVEIL: steady noise under a tone: noise down at least 10 dB, the tone within 3 dB
        auto e = made("VOICEVEIL");
        e->set(idxOf(*e, "reduction"), 100.0f);
        // a voice in phrases (0.6 s sung, 0.3 s gap) over steady noise: noise reducers learn in the gaps
        Stereo s = noise(4.0, 0.03f, 5, fs);
        Stereo v = sine(440.0, 4.0, 0.2f, fs);
        for (std::size_t i = 0; i < v.size(); ++i) if ((i % 43200) >= 28800 && i < 3 * 48000 - 4800) v.l[i] = v.r[i] = 0.0f;
        add(s, v);
        Stereo ref = s;
        run(*e, s);
        const std::size_t lat = e->latency(), from = 3 * 48000;
        // noise measured above 2 kHz (two 12 dB/oct high-passes: the 440 Hz tone is ~50 dB down there)
        auto hfDb = [&](const std::vector<float>& x, std::size_t off) {
            hc::fx::Biquad h1, h2;
            h1.c = h2.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, fs, 2000.0, 0.7071);
            double acc = 0;
            for (std::size_t i = from - 4800; i < from + 24000; ++i) {
                const float y = h2.tick(h1.tick(x[i - off]));
                if (i >= from) acc += static_cast<double>(y) * y;
            }
            return 10 * std::log10(acc + 1e-20);
        };
        const double noiseDrop = hfDb(s.l, 0) - hfDb(ref.l, lat);
        const double toneChange = toneDb(s.l, 440, from) - toneDb(ref.l, 440, from - lat);
        std::snprintf(buf, sizeof buf, "noise %+.1f dB, tone %+.1f dB", noiseDrop, toneChange);
        report("VOICEVEIL: removes steady noise, keeps the voice", noiseDrop < -10.0 && std::fabs(toneChange) < 3.0, buf);
    }
    {   // LEVELPILOT: a vocal at -33 dBFS is ridden up toward the -21 dBFS target (limited by MAX GAIN +6)
        auto e = made("LEVELPILOT");
        e->set(idxOf(*e, "span"), 24.0f); e->set(idxOf(*e, "motor"), 0.0f); e->set(idxOf(*e, "max_gain"), 12.0f);
        Stereo s = sine(300.0, 3.0, 0.0316f, fs);   // -30 dBFS peak = -33 dBFS RMS... sine-calibrated detector reads -30
        const double in = toneDb(s.l, 300, 96000);
        run(*e, s);
        const double gain = toneDb(s.l, 300, 96000) - in;
        std::snprintf(buf, sizeof buf, "rode %+.1f dB (target +9 dB)", gain);
        report("LEVELPILOT: rides a quiet vocal up to the target", std::fabs(gain - 9.0) < 1.0, buf);
    }
    {   // S.I.P.A.: width 0 = mono (no side), width 200 = sides doubled
        auto e = made("S.I.P.A.");
        Stereo s = noise(1.0, 0.2f, 3, fs);
        e->set(idxOf(*e, "width"), 0.0f);
        Stereo a = s; run(*e, a);
        double side = 0, sideIn = 0;
        for (std::size_t i = 24000; i < a.size(); ++i) { side += std::pow(a.l[i] - a.r[i], 2); sideIn += std::pow(s.l[i] - s.r[i], 2); }
        auto e2 = made("S.I.P.A.");
        e2->set(idxOf(*e2, "width"), 200.0f);
        Stereo b = s; run(*e2, b);
        double side2 = 0;
        for (std::size_t i = 24000; i < b.size(); ++i) side2 += std::pow(b.l[i] - b.r[i], 2);
        const double d0 = 10 * std::log10(side / sideIn + 1e-30), d2 = 10 * std::log10(side2 / sideIn);
        std::snprintf(buf, sizeof buf, "width 0: sides %+.0f dB, width 200: %+.2f dB", d0, d2);
        report("S.I.P.A.: width narrows to mono and widens x2", d0 < -60.0 && std::fabs(d2 - 6.02) < 0.2, buf);
    }
    {   // LEADSUITE: BASS +10 DEEP lifts 40 Hz by about 12 dB; COMPRESS +10 brings a loud tone down
        auto e = made("LEADSUITE");
        e->set(idxOf(*e, "bass"), 10.0f);
        Stereo s = sine(30.0, 1.0, 0.05f, fs); const double in = toneDb(s.l, 30, 24000);
        run(*e, s);
        const double up = toneDb(s.l, 30, 24000) - in;
        auto c = made("LEADSUITE");
        c->set(idxOf(*c, "compress"), 10.0f);
        Stereo t = sine(1000.0, 1.0, 0.9f, fs); const double tin = toneDb(t.l, 1000, 24000);
        run(*c, t);
        const double down = toneDb(t.l, 1000, 24000) - tin;
        std::snprintf(buf, sizeof buf, "30 Hz %+.1f dB, loud tone %+.1f dB", up, down);
        report("LEADSUITE: bass fader boosts lows, compress fader squeezes", up > 10.0 && up < 12.5 && down < -6.0, buf);
    }
    {   // CHOIRFORGE: voice 1 a fifth up (+7) on a 220 Hz voice puts 329.6 Hz in the output, in tune
        auto e = made("CHOIRFORGE");
        e->set(idxOf(*e, "v1_on"), 1.0f); e->set(idxOf(*e, "v1_pitch"), 7.0f); e->set(idxOf(*e, "scale"), 0.0f);
        Stereo s = voiceTone(220.0, 3.0, fs);
        run(*e, s);
        const double got = measuredPitch(s.l, 329.6276, s.size() - 48000, 48000, fs);
        const double lvl = db(toneAmp(s.l, got, s.size() - 48000, 48000, fs)) - db(toneAmp(s.l, 220.0, s.size() - 48000, 48000, fs));
        std::snprintf(buf, sizeof buf, "%.2f Hz (%+.1f cents), %+.1f dB vs the lead", got, 1200 * std::log2(got / 329.6276), lvl);
        report("CHOIRFORGE: voice 1 +7 st sings the fifth", std::fabs(1200 * std::log2(got / 329.6276)) < 5.0 && lvl > -9.0, buf);
    }
    {   // VOICEPRESS: compression 20 dB narrows the gap between a loud and a soft line; the gate at -40 dB drops a -55 dBFS hum
        auto gap = [&](float comp) {
            auto e = made("VOICEPRESS");
            e->set(idxOf(*e, "compression"), comp);
            Stereo loud = sine(500.0, 1.0, 0.7f, fs), soft = sine(500.0, 1.0, 0.05f, fs);
            run(*e, loud); e->reset(); run(*e, soft);
            return toneDb(loud.l, 500, 24000) - toneDb(soft.l, 500, 24000);
        };
        const double before = gap(0.0f), after = gap(20.0f);
        auto g = made("VOICEPRESS");
        g->set(idxOf(*g, "gate"), -40.0f);
        Stereo q = sine(100.0, 1.0, 0.00178f, fs); const double qin = toneDb(q.l, 100, 24000);
        run(*g, q);
        const double gated = toneDb(q.l, 100, 24000) - qin;
        std::snprintf(buf, sizeof buf, "loud-soft gap %.1f -> %.1f dB, quiet hum %+.1f dB", before, after, gated);
        report("VOICEPRESS: compresses loud lines, gates quiet noise", before - after > 6.0 && gated < -20.0, buf);
    }
    {   // RESONANCEGUARD: a ringing 2.5 kHz peak over pink-ish noise comes down; the noise around it barely moves
        auto e = made("RESONANCEGUARD");
        e->set(idxOf(*e, "threshold"), 0.0f);
        Stereo s = noise(4.0, 0.1f, 9, fs);
        add(s, sine(2500.0, 4.0, 0.3f, fs));
        Stereo ref = s;
        run(*e, s);
        const std::size_t lat = e->latency(), from = 3 * 48000;
        const double peak = toneDb(s.l, 2500, from) - toneDb(ref.l, 2500, from - lat);
        auto band = [&](const std::vector<float>& x, std::size_t off) {   // noise energy 250..600 Hz, 24 dB/oct each side (the 2.5 kHz tone is ~48 dB down)
            hc::fx::Biquad h1, h2, l1, l2;
            h1.c = h2.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, fs, 250.0, 0.7071);
            l1.c = l2.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::LowPass, fs, 600.0, 0.7071);
            double acc = 0;
            for (std::size_t i = from - 4800; i < from + 24000; ++i) { const float y = l2.tick(l1.tick(h2.tick(h1.tick(x[i - off])))); if (i >= from) acc += static_cast<double>(y) * y; }
            return 10 * std::log10(acc + 1e-20);
        };
        const double rest = band(s.l, 0) - band(ref.l, lat);
        std::snprintf(buf, sizeof buf, "peak %+.1f dB, the rest %+.1f dB", peak, rest);
        report("RESONANCEGUARD: pulls a ringing peak down, leaves the rest", peak < -6.0 && std::fabs(rest) < 1.5, buf);
    }
    {   // BANDMOTION: band 3 (800 Hz) +6 dB; the same band on MID leaves a pure-side signal untouched; LP 24 dB at 1 kHz
        auto e = made("BANDMOTION");
        e->set(idxOf(*e, "b3_gain"), 6.0f);
        Stereo s = sine(800.0, 1.0, 0.1f, fs); const double in = toneDb(s.l, 800, 24000);
        run(*e, s); const double up = toneDb(s.l, 800, 24000) - in;
        auto m = made("BANDMOTION");
        m->set(idxOf(*m, "b3_gain"), 6.0f); m->set(idxOf(*m, "b3_channel"), 1.0f);
        Stereo side = sine(800.0, 1.0, 0.1f, fs, true); const double sin0 = toneDb(side.l, 800, 24000);
        run(*m, side); const double sideMove = toneDb(side.l, 800, 24000) - sin0;
        auto f = made("BANDMOTION");
        f->set(idxOf(*f, "lp_on"), 1.0f); f->set(idxOf(*f, "lp_freq"), 1000.0f); f->set(idxOf(*f, "lp_slope"), 3.0f);
        Stereo hi = sine(4000.0, 1.0, 0.1f, fs); const double hin = toneDb(hi.l, 4000, 24000);
        run(*f, hi); const double cut = toneDb(hi.l, 4000, 24000) - hin;
        std::snprintf(buf, sizeof buf, "band %+.2f dB, mid band on side %+.2f dB, LP24 at 2 oct %+.1f dB", up, sideMove, cut);
        report("BANDMOTION: band gain, mid-only band, 24 dB low-pass", std::fabs(up - 6.0) < 0.2 && std::fabs(sideMove) < 0.1 && cut < -44.0, buf);
    }
    {   // GRAPHICFORGE: 1 kHz +6 dB lands at +6; at +12 the band is narrower than at +3 (proportional Q)
        auto e = made("GRAPHICFORGE");
        e->set(idxOf(*e, "g1k"), 6.0f);
        Stereo s = sine(1000.0, 1.0, 0.1f, fs); const double in = toneDb(s.l, 1000, 24000);
        run(*e, s); const double at = toneDb(s.l, 1000, 24000) - in;
        auto widthAt = [&](float g) {   // fraction (in dB) of the boost still there one octave off centre
            auto x = made("GRAPHICFORGE"); x->set(idxOf(*x, "g1k"), g);
            Stereo t = sine(2000.0, 1.0, 0.1f, fs); const double t0 = toneDb(t.l, 2000, 24000);
            run(*x, t); return (toneDb(t.l, 2000, 24000) - t0) / g;
        };
        const double w3 = widthAt(3.0f), w12 = widthAt(12.0f);
        std::snprintf(buf, sizeof buf, "+6 -> %+.2f dB; an octave off keeps %.0f%% at +3, %.0f%% at +12", at, 100 * w3, 100 * w12);
        report("GRAPHICFORGE: fader gain and proportional Q", std::fabs(at - 6.0) < 0.3 && w12 < w3, buf);
    }
    {   // SILKCONTOUR: LOW BOOST 10 at 60 Hz lifts 40 Hz; boost + atten together lift 30 Hz yet dip 300 Hz
        auto resp = [&](float boost, float atten, double hz) {
            auto e = made("SILKCONTOUR");
            e->set(idxOf(*e, "low_boost"), boost); e->set(idxOf(*e, "low_atten"), atten);
            Stereo s = sine(hz, 1.0, 0.05f, fs); const double in = toneDb(s.l, hz, 24000);
            run(*e, s); return toneDb(s.l, hz, 24000) - in;
        };
        const double b40 = resp(10, 0, 40), t30 = resp(10, 10, 30), t300 = resp(10, 10, 300);
        std::snprintf(buf, sizeof buf, "boost: 40 Hz %+.1f dB; boost+atten: 30 Hz %+.1f, 300 Hz %+.1f dB", b40, t30, t300);
        report("SILKCONTOUR: low boost, and the boost + atten lift-and-dip", b40 > 8.0 && t30 > 0.5 && t300 < -0.5, buf);
    }
    {   // GLOWLEVEL: mix 100 %, compression 70 takes a loud tone down; HF emphasis 0 makes a 6 kHz tone compress harder
        auto gr = [&](float hfEmph, double hz) {
            auto e = made("GLOWLEVEL");
            e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "compression"), 70.0f); e->set(idxOf(*e, "hi_freq"), hfEmph);
            Stereo s = sine(hz, 2.0, 0.3f, fs); const double in = toneDb(s.l, hz, 48000);
            run(*e, s); return toneDb(s.l, hz, 48000) - in;
        };
        const double flat = gr(100, 6000), emph = gr(0, 6000), low = gr(100, 300);
        std::snprintf(buf, sizeof buf, "300 Hz %+.1f dB; 6 kHz flat %+.1f, emphasised %+.1f dB", low, flat, emph);
        report("GLOWLEVEL: levels a loud tone; HF emphasis pushes highs harder", low < -6.0 && emph < flat - 3.0, buf);
    }
    {   // STRIKECLAMP: compression on, mix 100 %, input +20 dB on a -20 dBFS tone at 4:1 -> about 15 dB of reduction;
        // MODE B adds more 2nd harmonic than MODE A
        auto run1 = [&](float model, double& h2) {
            auto e = made("STRIKECLAMP");
            e->set(idxOf(*e, "compression"), 1.0f); e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "input"), 20.0f); e->set(idxOf(*e, "model"), model);
            Stereo s = sine(200.0, 1.0, 0.1f, fs);
            run(*e, s);
            h2 = toneDb(s.l, 400.0, 24000) - toneDb(s.l, 200.0, 24000);
            return e->meter(1);
        };
        double h2a = 0, h2b = 0;
        const double grA = run1(0, h2a); run1(1, h2b);
        std::snprintf(buf, sizeof buf, "GR %.1f dB; 2nd harmonic A %.0f dBc, B %.0f dBc", grA, h2a, h2b);
        report("STRIKECLAMP: input drives the FET, model B grittier", grA < -12.0 && grA > -18.0 && h2b > h2a + 6.0, buf);
    }
    {   // BUSFOUNDRY: threshold -20, ratio 4, hard knee: a -6 dBFS peak tone comes out at -20 + 14/4 = -16.5 dBFS peak;
        // STEREO LINK IND leaves a quiet right side alone while the left is squeezed, 100 % pulls it down too
        auto e = made("BUSFOUNDRY");
        e->set(idxOf(*e, "comp_in"), 1.0f); e->set(idxOf(*e, "threshold"), -20.0f); e->set(idxOf(*e, "ratio"), 3.0f);
        e->set(idxOf(*e, "attack"), 3.0f); e->set(idxOf(*e, "release"), 3.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs);
        run(*e, s);
        const double outPk = db(toneAmp(s.l, 1000.0, 48000, 48000, fs));
        auto side = [&](float link) {
            auto x = made("BUSFOUNDRY");
            x->set(idxOf(*x, "comp_in"), 1.0f); x->set(idxOf(*x, "threshold"), -20.0f); x->set(idxOf(*x, "ratio"), 6.0f); x->set(idxOf(*x, "link"), link);
            Stereo t = sine(1000.0, 1.0, 0.5f, fs);
            for (auto& v : t.r) v *= 0.02f;   // right side far below threshold
            const double in = toneDb(t.r, 1000, 24000);
            run(*x, t); return toneDb(t.r, 1000, 24000) - in;
        };
        const double ind = side(0), full = side(6);
        std::snprintf(buf, sizeof buf, "-6 dBFS in -> %.2f dBFS out (expect -16.5); quiet side IND %+.1f, 100%% %+.1f dB", outPk, ind, full);
        report("BUSFOUNDRY: static curve, stereo link", std::fabs(outPk + 16.5) < 1.0 && std::fabs(ind) < 0.2 && full < -6.0, buf);
    }
    {   // BANDCOUNCIL: band 3 (150 Hz..1.5 kHz) threshold -30, range -12 cuts a loud 600 Hz tone by up to 12 dB;
        // a 60 Hz tone in band 2 is untouched; band 4 static gain +6 lifts 3 kHz by 6
        auto e = made("BANDCOUNCIL");
        e->set(idxOf(*e, "band3_thr"), -30.0f); e->set(idxOf(*e, "band3_range"), -12.0f); e->set(idxOf(*e, "band4_gain"), 6.0f);
        Stereo s = sine(600.0, 1.0, 0.5f, fs); add(s, sine(60.0, 1.0, 0.1f, fs)); add(s, sine(3000.0, 1.0, 0.05f, fs));
        Stereo ref = s;
        run(*e, s);
        const double c600 = toneDb(s.l, 600, 24000) - toneDb(ref.l, 600, 24000);
        const double c60 = toneDb(s.l, 60, 24000) - toneDb(ref.l, 60, 24000);
        const double c3k = toneDb(s.l, 3000, 24000) - toneDb(ref.l, 3000, 24000);
        std::snprintf(buf, sizeof buf, "600 Hz %+.1f dB, 60 Hz %+.2f dB, 3 kHz %+.2f dB", c600, c60, c3k);
        report("BANDCOUNCIL: one band compresses, others untouched / static gain", c600 < -9.0 && c600 > -12.5 && std::fabs(c60) < 0.3 && std::fabs(c3k - 6.0) < 1.0, buf);   // a crossover band shares its edges: +-1 dB at its centre
    }
    // ---- Batch 5 ----
    auto impulse = [&](double seconds, std::size_t at = 1000) { Stereo s(static_cast<std::size_t>(seconds * fs)); s.l[at] = s.r[at] = 1.0f; return s; };
    auto peakAt = [](const std::vector<float>& x, std::size_t from, std::size_t to) {
        std::size_t best = from; float bv = -1.0f;
        for (std::size_t i = from; i < to && i < x.size(); ++i) if (std::fabs(x[i]) > bv) { bv = std::fabs(x[i]); best = i; }
        return std::pair<std::size_t, float>{best, bv};
    };
    auto winDb = [&](const std::vector<float>& x, double fromS, double toS, std::size_t at = 1000) {
        return db(rmsOf(x, at + static_cast<std::size_t>(fromS * fs), at + static_cast<std::size_t>(toS * fs)));
    };
    {   // IMPACTGUIDE: threshold -20, mix 100 %: a loud 1 kHz tone is turned down by 6 dB or more, quiet noise at
        // 200..400 Hz moves less than 2 dB; FOCUS GAIN -12 at 1 kHz protects the tone (at least 3 dB less cut)
        auto runIg = [&](float thr, float focus) {
            auto e = made("IMPACTGUIDE");
            e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "threshold"), thr); e->set(idxOf(*e, "focus_gain"), focus);
            Stereo s = sine(1000.0, 2.0, 0.5f, fs); add(s, noise(2.0, 0.003f, 5, fs));
            run(*e, s);
            return s;
        };
        const Stereo ref = runIg(0.0f, 0.0f), cut = runIg(-20.0f, 0.0f), prot = runIg(-20.0f, -12.0f);
        const double tone = toneDb(cut.l, 1000, 48000) - toneDb(ref.l, 1000, 48000);
        const double nz = bandLevelDb(cut.l, 200, 400, 48000, 48000, fs) - bandLevelDb(ref.l, 200, 400, 48000, 48000, fs);
        const double tp = toneDb(prot.l, 1000, 48000) - toneDb(ref.l, 1000, 48000);
        std::snprintf(buf, sizeof buf, "tone %+.1f dB, 200-400 Hz noise %+.2f dB, focus -12: tone %+.1f dB", tone, nz, tp);
        report("IMPACTGUIDE: cuts the loud slice only; focus protects", tone <= -6.0 && std::fabs(nz) < 2.0 && tp > tone + 3.0, buf);
    }
    auto edcRt = [&](const std::vector<float>& x, std::size_t at) {   // RT60 from the backward-integrated decay, -5 to -25 dB, x3
        std::vector<double> e(x.size() - at);
        double acc = 0;
        for (std::size_t i = x.size(); i-- > at;) { acc += static_cast<double>(x[i]) * x[i]; e[i - at] = acc; }
        if (acc <= 0) return 0.0;
        double t5 = -1, t25 = -1;
        for (std::size_t i = 0; i < e.size(); ++i) {
            const double d = 10.0 * std::log10(std::max(e[i] / e[0], 1e-30));
            if (t5 < 0 && d <= -5.0) t5 = static_cast<double>(i) / fs;
            if (t25 < 0 && d <= -25.0) { t25 = static_cast<double>(i) / fs; break; }
        }
        return t5 >= 0 && t25 > t5 ? 3.0 * (t25 - t5) : 0.0;
    };
    {   // CHAMBERPLATE: plate CHARACTER A decays about 1.3 s (0.9..1.8 s measured), DAMPER 8 rings shorter; the chamber's tape path echoes at
        // exactly TAPE LEFT (120 ms); ROOM B alone decays about 1.4 s and carries steady program at a usable level
        auto rt = [&](float damper) {
            auto e = made("CHAMBERPLATE");
            e->set(idxOf(*e, "plate_mix"), 100.0f); e->set(idxOf(*e, "damper"), damper);
            Stereo s = impulse(6.0); run(*e, s);
            return edcRt(s.l, 1000);
        };
        const double rt0 = rt(0), rt8 = rt(8);
        auto e = made("CHAMBERPLATE");
        e->set(idxOf(*e, "chamber_mix"), 100.0f); e->set(idxOf(*e, "chamber_on"), 0.0f);
        Stereo s = impulse(1.0); run(*e, s);
        const auto [at, v] = peakAt(s.l, 1001, s.size());
        auto c = made("CHAMBERPLATE");
        c->set(idxOf(*c, "chamber_mix"), 100.0f); c->set(idxOf(*c, "tape_on"), 0.0f);
        Stereo t = impulse(6.0); run(*c, t);
        const double roomRt = edcRt(t.l, 1000);
        auto g = made("CHAMBERPLATE");
        g->set(idxOf(*g, "chamber_mix"), 100.0f); g->set(idxOf(*g, "tape_on"), 0.0f);
        Stereo nz = noise(2.0, 0.1f, 9, fs); const double inDb = db(rmsOf(nz.l, 48000, 96000));
        run(*g, nz);
        const double gain = db(rmsOf(nz.l, 48000, 96000)) - inDb;
        const long echoOff = static_cast<long>(at) - 1000 - 5760;
        std::snprintf(buf, sizeof buf, "plate RT %.2f s, damper 8 %.2f s; tape echo %+ld smp; room RT %.2f s, level %+.1f dB", rt0, rt8, echoOff, roomRt, gain);
        report("CHAMBERPLATE: plate decay, damper, tape echo, room", rt0 > 0.9 && rt0 < 1.8 && rt8 < rt0 * 0.6 && echoOff == 0 && v > 0.3f
                                                                      && roomRt > 0.7 && roomRt < 2.8 && gain > -20.0 && gain < 6.0, buf);
    }
    {   // ECHOFORGE: 250 ms, feedback 50 %, ping pong off: echoes land on 250 / 500 ms, the second 6 dB down;
        // feedback 150 % grows but the loop saturator keeps it bounded
        auto e = made("ECHOFORGE");
        e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "time_l"), 250.0f); e->set(idxOf(*e, "dual_link"), 1.0f);
        e->set(idxOf(*e, "feedback"), 50.0f); e->set(idxOf(*e, "ping_pong"), 0.0f);
        Stereo s = impulse(1.0); run(*e, s);
        const auto [a1, v1] = peakAt(s.l, 1001, 1000 + 18000);
        const auto [a2, v2] = peakAt(s.l, 1000 + 18000, 1000 + 30000);
        const double ratio = db(v2) - db(v1);
        auto h = made("ECHOFORGE");
        h->set(idxOf(*h, "mix"), 100.0f); h->set(idxOf(*h, "feedback"), 150.0f); h->set(idxOf(*h, "time_l"), 50.0f); h->set(idxOf(*h, "time_r"), 70.0f);
        Stereo n = noise(1.0, 0.3f, 3, fs); Stereo tl(static_cast<std::size_t>(5.0 * fs));
        for (std::size_t i = 0; i < n.size(); ++i) { tl.l[i] = n.l[i]; tl.r[i] = n.r[i]; }
        run(*h, tl);
        const float pk = peak(tl);
        const double endDb = db(rmsOf(tl.l, tl.size() - 48000, tl.size()));
        std::snprintf(buf, sizeof buf, "echoes at %+ld / %+ld samples, 2nd %+.2f dB; fb 150%%: peak %.2f, tail %.0f dB",
                      static_cast<long>(a1) - 13000, static_cast<long>(a2) - 25000, ratio, pk, endDb);
        report("ECHOFORGE: exact echo times, feedback law, 150% bounded", a1 == 13000 && a2 == 25000 && std::fabs(ratio + 6.02) < 0.3 && finite(tl) && pk < 4.0f && endDb > -40.0, buf);
    }
    {   // MOTIONFIELD: TO DELAY 100 sends into a 200 ms echo; TO REVERB 100 adds a tail; MORPH with FEEDBACK assigned
        // (range 0..50 %) at A-B 100 % brings the second echo in, at 0 % it is gone
        auto mk = [&](float morph, bool assign, float toRev) {
            auto e = made("MOTIONFIELD");
            e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "to_delay"), 100.0f); e->set(idxOf(*e, "delay_time"), 200.0f);
            e->set(idxOf(*e, "to_reverb"), toRev);
            if (assign) { e->set(idxOf(*e, "m_feedback_assign"), 1.0f); e->set(idxOf(*e, "m_feedback_max"), 50.0f); }
            e->set(idxOf(*e, "morph"), morph);
            Stereo s = impulse(2.0); run(*e, s);
            return s;
        };
        const Stereo plain = mk(0, false, 0), m0 = mk(0, true, 0), m1 = mk(100, true, 0), rv = mk(0, false, 100);
        const auto [ea, ev] = peakAt(plain.l, 1001, 1000 + 14400);
        const double second0 = db(peakAt(m0.l, 1000 + 14400, 1000 + 24000).second), second1 = db(peakAt(m1.l, 1000 + 14400, 1000 + 24000).second);
        const double tailNo = winDb(plain.l, 0.5, 0.7), tailRv = winDb(rv.l, 0.5, 0.7);
        std::snprintf(buf, sizeof buf, "echo %+ld smp; 2nd echo morph 0 %.0f / 100 %.1f dB; tail %.0f -> %.0f dB",
                      static_cast<long>(ea) - 10600, second0, second1, tailNo, tailRv);
        report("MOTIONFIELD: send, echo, reverb tail, morph moves feedback", ea == 10600 && ev > 0.9f && second0 < -60.0 && second1 > -10.0 && tailRv > tailNo + 20.0, buf);
    }
    {   // DEPTHMATRIX: DUAL ECHO soloed echoes at 300 ms; routing DUAL ECHO -> HALL adds energy to the soloed HALL
        // tail after 300 ms; muting the HALL silences it
        auto mk = [&](const char* solo, float toHall, bool muteHall) {
            auto e = made("DEPTHMATRIX");
            e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, solo), 1.0f);
            e->set(idxOf(*e, "dual_to_hall"), toHall);
            if (muteHall) e->set(idxOf(*e, "mix_hall_mute"), 1.0f);
            Stereo s = impulse(2.0); run(*e, s);
            return s;
        };
        const Stereo d = mk("mix_dual_solo", -60.0f, false), h0 = mk("mix_hall_solo", -60.0f, false), h1 = mk("mix_hall_solo", 0.0f, false), hm = mk("mix_hall_solo", 0.0f, true);
        const auto [ea, ev] = peakAt(d.l, 1001, d.size());
        const auto [er, evr] = peakAt(d.r, 1001, d.size());
        const double w0 = winDb(h0.l, 0.32, 0.7), w1 = winDb(h1.l, 0.32, 0.7), wm = db(rmsOf(hm.l, 0, hm.size()));
        std::snprintf(buf, sizeof buf, "echo L %+ld R %+ld smp; hall after 300 ms %.1f -> %.1f dB; muted %.0f dB",
                      static_cast<long>(ea) - 15400, static_cast<long>(er) - 22600, w0, w1, wm);
        report("DEPTHMATRIX: echo times, grid send into hall, mute", ea == 15400 && er == 22600 && ev > 0.9f && evr > 0.9f && w1 > w0 + 3.0 && wm < -100.0, buf);
    }
    // ---- Batch 6 ----
    {   // CONSOLECORE: compressor -20 dB / 4:1 / fast puts a -6 dBFS tone at about -16.5 dBFS; EQ CHARACTER B narrows a
        // +12 dB LMF bell (an octave away it lifts less than A); the gate with 40 dB range cuts a quiet tone
        auto e = made("CONSOLECORE");
        e->set(idxOf(*e, "comp_threshold"), -20.0f); e->set(idxOf(*e, "comp_ratio"), 4.0f); e->set(idxOf(*e, "comp_fast"), 1.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs); run(*e, s);
        const double out = db(toneAmp(s.l, 1000.0, 48000, 48000, fs));
        auto octave = [&](float character) {
            auto x = made("CONSOLECORE");
            x->set(idxOf(*x, "lmf_freq"), 1000.0f); x->set(idxOf(*x, "lmf_gain"), 12.0f); x->set(idxOf(*x, "eq_character"), character);
            Stereo t = sine(2000.0, 1.0, 0.05f, fs); const double in = toneDb(t.l, 2000, 24000);
            run(*x, t); return toneDb(t.l, 2000, 24000) - in;
        };
        const double a = octave(0), b = octave(1);
        auto g = made("CONSOLECORE");
        g->set(idxOf(*g, "gate_threshold"), -30.0f); g->set(idxOf(*g, "gate_range"), 40.0f);
        Stereo q = sine(1000.0, 1.0, 0.003f, fs); const double qin = toneDb(q.l, 1000, 24000);
        run(*g, q); const double cut = toneDb(q.l, 1000, 24000) - qin;
        std::snprintf(buf, sizeof buf, "-6 dBFS -> %.2f dBFS (expect -16.5); 1 oct off A %+.1f / B %+.1f dB; gate %+.1f dB", out, a, b, cut);
        report("CONSOLECORE: comp curve, EQ character, gate range", std::fabs(out + 16.5) < 1.5 && a > b + 2.0 && cut < -30.0, buf);
    }
    {   // CHANNELMATRIX: module order changes the sound (EQ boost before EVEN saturation makes more 2nd harmonic than
        // after it); compressor MIX 0 % leaves it alone, 100 % squeezes; the safety limiter holds a +6 dBFS peak at 0 dBFS
        auto mk = [&](bool eqFirst) {
            auto e = made("CHANNELMATRIX");
            e->set(idxOf(*e, "slot1"), eqFirst ? 1.0f : 0.0f); e->set(idxOf(*e, "slot2"), eqFirst ? 0.0f : 1.0f);
            e->set(idxOf(*e, "sat_amount"), 60.0f); e->set(idxOf(*e, "sat_type"), 1.0f);
            e->set(idxOf(*e, "highmid_freq"), 1000.0f); e->set(idxOf(*e, "highmid_gain"), 12.0f);
            Stereo s = sine(1000.0, 1.0, 0.1f, fs); run(*e, s);
            return toneDb(s.l, 2000, 24000) - toneDb(s.l, 1000, 24000);
        };
        const double h2pre = mk(false), h2eq = mk(true);
        auto cmp = [&](float mix) {
            auto e = made("CHANNELMATRIX");
            e->set(idxOf(*e, "comp_threshold"), -30.0f); e->set(idxOf(*e, "comp_ratio"), 10.0f); e->set(idxOf(*e, "comp_mix"), mix);
            Stereo s = sine(1000.0, 1.0, 0.3f, fs); const double in = toneDb(s.l, 1000, 24000);
            run(*e, s); return toneDb(s.l, 1000, 24000) - in;
        };
        const double m0 = cmp(0), m100 = cmp(100);
        auto l = made("CHANNELMATRIX");
        l->set(idxOf(*l, "input"), 6.0f);
        Stereo s = sine(1000.0, 1.0, 1.0f, fs); run(*l, s);
        const float pk = peak(s);
        std::snprintf(buf, sizeof buf, "2nd harm PRE>EQ %.0f / EQ>PRE %.0f dBc; comp mix 0 %+.2f, 100 %+.1f dB; limiter peak %.3f", h2pre, h2eq, m0, m100, pk);
        report("CHANNELMATRIX: order matters, comp mix, safety limiter", h2eq > h2pre + 6.0 && std::fabs(m0) < 0.05 && m100 < -10.0 && pk <= 1.0f, buf);
    }
    {   // EMBERSTRIP: DRIVE 80 adds both even and odd harmonics (none at 0); COMPRESS 80 narrows a loud / soft gap;
        // EXPAND 60 pushes a -60 dBFS hum down while a -10 dBFS tone passes
        auto h = [&](float drive, double& h2, double& h3) {
            auto e = made("EMBERSTRIP"); e->set(idxOf(*e, "drive"), drive);
            Stereo s = sine(500.0, 1.0, 0.3f, fs); run(*e, s);
            const double f = toneDb(s.l, 500, 24000); h2 = toneDb(s.l, 1000, 24000) - f; h3 = toneDb(s.l, 1500, 24000) - f;
        };
        double a2, a3, b2, b3; h(0, a2, a3); h(80, b2, b3);
        auto lvl = [&](float comp, float amp) {
            auto e = made("EMBERSTRIP"); e->set(idxOf(*e, "compress"), comp);
            Stereo s = sine(1000.0, 2.0, amp, fs); run(*e, s); return db(toneAmp(s.l, 1000, 48000, 48000, fs));
        };
        const double gap0 = lvl(0, 0.5f) - lvl(0, 0.05f), gap1 = lvl(80, 0.5f) - lvl(80, 0.05f);
        auto ex = [&](float amp) {
            auto e = made("EMBERSTRIP"); e->set(idxOf(*e, "expand"), 60.0f);
            Stereo s = sine(100.0, 2.0, amp, fs); const double in = db(toneAmp(s.l, 100, 48000, 48000, fs));
            run(*e, s); return db(toneAmp(s.l, 100, 48000, 48000, fs)) - in;
        };
        const double quiet = ex(0.001f), loud = ex(0.3f);
        std::snprintf(buf, sizeof buf, "drive 80: 2nd %.0f 3rd %.0f dBc (0: %.0f/%.0f); gap %.1f -> %.1f dB; expand -60 %+.1f, -10 %+.2f dB", b2, b3, a2, a3, gap0, gap1, quiet, loud);
        report("EMBERSTRIP: tube harmonics, one-knob comp, expander", b2 > -50.0 && b3 > -50.0 && a2 < -100.0 && a3 < -100.0 && gap1 < gap0 - 5.0 && quiet < -10.0 && std::fabs(loud) < 1.0, buf);
    }
    {   // MIXBOND: mix 100 %, threshold 0 (-10 dBFS), 4:1: a -2 dBFS tone comes out near -8 dBFS; MIX 0 % leaves it;
        // FADE on with a 1 s timer is silent after the timer
        auto mk = [&](float mix) {
            auto e = made("MIXBOND");
            e->set(idxOf(*e, "mix"), mix); e->set(idxOf(*e, "attack"), 2.0f); e->set(idxOf(*e, "release"), 1.0f);
            Stereo s = sine(1000.0, 2.0, 0.794f, fs); run(*e, s); return db(toneAmp(s.l, 1000, 48000, 48000, fs));
        };
        const double wet = mk(100), dry = mk(0);
        auto f = made("MIXBOND");
        f->set(idxOf(*f, "fade_time"), 1.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs);
        for (std::size_t k = 0; k < s.size(); k += 512) { if (k == 4096) f->set(idxOf(*f, "fade"), 1.0f); f->process(s.l.data() + k, s.r.data() + k, static_cast<std::uint32_t>(std::min<std::size_t>(512, s.size() - k))); }
        const double tail = db(rmsOf(s.l, static_cast<std::size_t>(1.3 * fs), s.size()));
        std::snprintf(buf, sizeof buf, "-2 dBFS -> %.2f dBFS (expect about -8); mix 0 %.2f dBFS; faded %.0f dB", wet, dry, tail);
        report("MIXBOND: bus curve, parallel mix, autofade", std::fabs(wet + 8.0) < 1.5 && std::fabs(dry + 2.0) < 0.05 && tail < -80.0, buf);
    }
    {   // REELAURA: SATURATION 80 adds harmonics; at 7.5 IPS the head bump lifts 60 Hz over 1 kHz; the tape echo (SLAP,
        // 120 ms) lands 120 ms after the transport delay; SEND - RETURN drops the dry signal
        auto e = made("REELAURA");
        e->set(idxOf(*e, "saturation"), 80.0f);
        Stereo s = sine(500.0, 1.0, 0.5f, fs); run(*e, s);
        const double h3 = toneDb(s.l, 1500, 24000) - toneDb(s.l, 500, 24000);
        auto bump = [&](double hz) {
            auto x = made("REELAURA"); x->set(idxOf(*x, "saturation"), 1.0f); x->set(idxOf(*x, "speed"), 0.0f);
            Stereo t = sine(hz, 1.0, 0.03f, fs); const double in = toneDb(t.l, hz, 24000);
            run(*x, t); return toneDb(t.l, hz, 24000) - in;
        };
        const double lift = bump(60) - bump(1000);
        auto echo = [&](float routing) {
            auto x = made("REELAURA"); x->set(idxOf(*x, "delay_on"), 1.0f); x->set(idxOf(*x, "delay_routing"), routing);
            Stereo t = impulse(1.0); run(*x, t); return t;
        };
        const Stereo ins = echo(0), snd = echo(1);
        const std::size_t lat = static_cast<std::size_t>(std::lround(0.004 * fs));
        const auto [ea, ev] = peakAt(ins.l, 1000 + lat + 100, ins.size());
        const long off = static_cast<long>(ea) - static_cast<long>(1000 + lat + 5760);
        const double dryIns = db(std::fabs(ins.l[1000 + lat])), drySnd = db(std::fabs(snd.l[1000 + lat]));
        std::snprintf(buf, sizeof buf, "3rd harm %.0f dBc; 60 Hz bump %+.1f dB; echo %+ld smp; dry insert %.0f / send %.0f dB", h3, lift, off, dryIns, drySnd);
        report("REELAURA: saturation, head bump, tape echo, send-return", h3 > -50.0 && lift > 1.5 && std::labs(off) <= 3 && ev > 0.3f && dryIns > -1.0 && drySnd < -60.0, buf);
    }
    // ---- Batch 7 ----
    auto truePeakDb = [](const std::vector<float>& x, std::size_t from) {   // 16x windowed-sinc read of the peaks between samples
        double m = 0;
        for (std::size_t i = from + 32; i + 32 < x.size(); ++i)
            for (int ph = 0; ph < 16; ++ph) {
                const double t = ph / 16.0;
                double acc = 0;
                for (int k = -31; k <= 32; ++k) {
                    const double u = k - t;
                    const double s = std::fabs(u) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * u) / (std::numbers::pi * u);
                    const double w = 0.5 + 0.5 * std::cos(std::numbers::pi * u / 33.0);
                    acc += x[static_cast<std::size_t>(static_cast<long>(i) + k)] * s * w;
                }
                m = std::max(m, std::fabs(acc));
            }
        return db(m);
    };
    {   // PEAKCITADEL: threshold -12 / ceiling -1 makes a tone louder but never over -1 dBFS; TRUE PEAK holds the peaks
        // between the samples too (DIGITAL lets them through); ISOLATION 100 % stops a loud low band from ducking a quiet high one
        auto e = made("PEAKCITADEL");
        e->set(idxOf(*e, "threshold_l"), -12.0f); e->set(idxOf(*e, "ceiling_l"), -1.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs); const double inRms = db(rmsOf(s.l, 48000, 96000));
        run(*e, s);
        const double pk = db(peak(s)), lift = db(rmsOf(s.l, 48000, 96000)) - inRms;
        auto tp = [&](float domain) {
            auto x = made("PEAKCITADEL");
            x->set(idxOf(*x, "threshold_l"), -6.0f); x->set(idxOf(*x, "ceiling_l"), -1.0f); x->set(idxOf(*x, "domain"), domain);
            Stereo t(static_cast<std::size_t>(fs));
            for (std::size_t i = 0; i < t.size(); ++i) t.l[i] = t.r[i] = 0.5f * static_cast<float>(std::sin(std::numbers::pi * 0.5 * static_cast<double>(i) + std::numbers::pi * 0.25));   // fs/4, peaks between samples
            run(*x, t); return truePeakDb(t.l, 24000);
        };
        const double tpDig = tp(0), tpTp = tp(2);
        auto iso = [&](float isolation) {
            auto x = made("PEAKCITADEL");
            x->set(idxOf(*x, "mode"), 2.0f); x->set(idxOf(*x, "threshold_l"), -12.0f); x->set(idxOf(*x, "isolation"), isolation);
            Stereo t = sine(60.0, 2.0, 0.6f, fs); add(t, sine(5000.0, 2.0, 0.02f, fs));
            const double in = toneDb(t.l, 5000, 48000);
            run(*x, t); return toneDb(t.l, 5000, 48000) - in;
        };
        const double i0 = iso(0), i100 = iso(100);
        std::snprintf(buf, sizeof buf, "peak %.2f dBFS, louder %+.1f dB; true peak DIGITAL %.2f / TRUE PEAK %.2f dBFS; 5 kHz iso 0 %+.1f / 100 %+.1f dB", pk, lift, tpDig, tpTp, i0, i100);
        report("PEAKCITADEL: ceiling, true peak, band isolation", pk <= -0.99 && lift > 3.0 && tpDig > -0.5 && tpTp < -0.8 && i100 > i0 + 3.0, buf);   // ceiling -1: DIGITAL lets the between-sample peak over it, TRUE PEAK does not
    }
    {   // IMPACTCROWN: LOW 100 at G1 lifts 49 Hz, leaves 1 kHz; DRIVE 12 dB into CLIP never passes 0 dBFS; STEREO IMAGE 100
        // lifts the side above 300 Hz
        auto e = made("IMPACTCROWN"); e->set(idxOf(*e, "low"), 100.0f);
        Stereo s = sine(49.0, 1.0, 0.05f, fs); add(s, sine(1000.0, 1.0, 0.05f, fs));
        const double l0 = toneDb(s.l, 49, 24000), k0 = toneDb(s.l, 1000, 24000);
        run(*e, s);
        const double low = toneDb(s.l, 49, 24000) - l0, mid = toneDb(s.l, 1000, 24000) - k0;
        auto d = made("IMPACTCROWN"); d->set(idxOf(*d, "drive_gain"), 12.0f);
        Stereo c = sine(200.0, 1.0, 0.8f, fs); run(*d, c);
        auto w = made("IMPACTCROWN"); w->set(idxOf(*w, "stereo_image"), 100.0f);
        Stereo t = sine(2000.0, 1.0, 0.1f, fs, true);   // all side
        const double s0 = toneDb(t.l, 2000, 24000);
        run(*w, t);
        const double side = toneDb(t.l, 2000, 24000) - s0;
        std::snprintf(buf, sizeof buf, "49 Hz %+.1f, 1 kHz %+.2f dB; clip peak %.3f; side %+.1f dB", low, mid, static_cast<double>(peak(c)), side);
        report("IMPACTCROWN: low at the note, clip ceiling, stereo image", low > 8.0 && std::fabs(mid) < 0.5 && peak(c) <= 1.0f && side > 4.0, buf);
    }
    {   // MASTERCOUNCIL: DUAL with LINK off runs set A on the left and set B on the right; dynamics AMOUNT 50 (-15 dBFS),
        // 4:1, MIX 100 cuts a -3 dBFS tone by about 9 dB; PHASE L+90 turns a mono tone into an uncorrelated pair
        auto e = made("MASTERCOUNCIL");
        e->set(idxOf(*e, "tone_mode"), 1.0f); e->set(idxOf(*e, "tone_link"), 0.0f); e->set(idxOf(*e, "tone_a_lowmid_gain"), 6.0f);
        Stereo s = sine(700.0, 1.0, 0.05f, fs); const double in = toneDb(s.l, 700, 24000);
        run(*e, s);
        const double gl = toneDb(s.l, 700, 24000) - in, gr = toneDb(s.r, 700, 24000) - in;
        auto d = made("MASTERCOUNCIL");
        d->set(idxOf(*d, "dyn_a_character"), 1.0f); d->set(idxOf(*d, "dyn_a_amount"), 50.0f); d->set(idxOf(*d, "dyn_a_ratio"), 4.0f); d->set(idxOf(*d, "dyn_a_mix"), 100.0f);
        Stereo c = sine(1000.0, 2.0, 0.708f, fs); const double cin = db(toneAmp(c.l, 1000, 48000, 48000, fs));
        run(*d, c);
        const double cut = db(toneAmp(c.l, 1000, 48000, 48000, fs)) - cin;
        auto ph = made("MASTERCOUNCIL"); ph->set(idxOf(*ph, "phase"), 1.0f);
        Stereo m = sine(1000.0, 1.0, 0.3f, fs); const double m0 = toneDb(m.l, 1000, 24000);
        run(*ph, m);
        double lr = 0, ll = 0, rr = 0;
        for (std::size_t i = 24000; i < m.size(); ++i) { lr += static_cast<double>(m.l[i]) * m.r[i]; ll += static_cast<double>(m.l[i]) * m.l[i]; rr += static_cast<double>(m.r[i]) * m.r[i]; }
        const double corr = lr / std::sqrt(ll * rr), lvl = toneDb(m.l, 1000, 24000) - m0;
        std::snprintf(buf, sizeof buf, "DUAL: L %+.2f / R %+.2f dB; dynamics %+.1f dB; L+90 correlation %+.2f, level %+.2f dB", gl, gr, cut, corr, lvl);
        report("MASTERCOUNCIL: dual knob sets, dynamics, 90-degree phase", std::fabs(gl - 6.0) < 0.3 && std::fabs(gr) < 0.05 && cut < -7.0 && cut > -11.0 && std::fabs(corr) < 0.1 && std::fabs(lvl) < 0.3, buf);
    }
    auto hits = [&](double seconds) {   // a drum-like hit every 250 ms: 200 Hz, instant attack, 60 ms decay
        Stereo s(static_cast<std::size_t>(seconds * fs));
        for (std::size_t i = 0; i < s.size(); ++i) {
            const double t = std::fmod(static_cast<double>(i) / fs, 0.25);
            s.l[i] = s.r[i] = static_cast<float>(0.5 * std::exp(-t / 0.06) * std::sin(2.0 * std::numbers::pi * 200.0 * t));
        }
        return s;
    };
    {   // HITCONTOUR: ATTACK +100 (NEEDLE, 20 ms; +12 dB at its tip, falling fast) lifts the first 8 ms by over 4 dB and leaves the tail; SUSTAIN -100 (200 ms)
        // cuts the tail and leaves the front; MIX 0 % changes nothing
        auto mk = [&](float a, float su, float mix, double& front, double& tail) {
            auto e = made("HITCONTOUR");
            e->set(idxOf(*e, "attack_amount"), a); e->set(idxOf(*e, "attack_sensitivity"), 50.0f);
            e->set(idxOf(*e, "sustain_amount"), su); e->set(idxOf(*e, "sustain_sensitivity"), 50.0f); e->set(idxOf(*e, "mix"), mix);
            Stereo s = hits(2.0); run(*e, s);
            const std::size_t h0 = static_cast<std::size_t>(1.0 * fs);   // a hit well after the start
            front = db(rmsOf(s.l, h0, h0 + static_cast<std::size_t>(0.008 * fs)));
            tail = db(rmsOf(s.l, h0 + static_cast<std::size_t>(0.06 * fs), h0 + static_cast<std::size_t>(0.15 * fs)));
        };
        double f0, t0, fa, ta, fs2, ts2, fm, tm;
        mk(0, 0, 0, f0, t0); mk(100, 0, 100, fa, ta); mk(0, -100, 100, fs2, ts2); mk(100, -100, 0, fm, tm);
        std::snprintf(buf, sizeof buf, "attack: front %+.1f tail %+.1f dB; sustain: front %+.1f tail %+.1f dB; mix 0 %+.2f dB", fa - f0, ta - t0, fs2 - f0, ts2 - t0, fm - f0);
        report("HITCONTOUR: attack and sustain shaping, mix", fa - f0 > 4.0 && std::fabs(ta - t0) < 2.0 && ts2 - t0 < -6.0 && std::fabs(fs2 - f0) < 2.0 && std::fabs(fm - f0) < 0.05, buf);
    }
    {   // DRUMKEY: a 200 Hz ring in the focus band, threshold -40, shift +700 cents: the ring moves up a fifth (~300 Hz);
        // a 2 kHz part outside the band is untouched; THRESHOLD 0 dBFS changes nothing
        auto mk = [&](float thr) {
            auto e = made("DRUMKEY");
            e->set(idxOf(*e, "threshold"), thr); e->set(idxOf(*e, "formant_shift"), 700.0f);
            Stereo s = sine(200.0, 1.5, 0.3f, fs); add(s, sine(2000.0, 1.5, 0.05f, fs));
            run(*e, s); return s;
        };
        const Stereo on = mk(-40.0f), off = mk(0.0f);
        const double up = toneDb(on.l, 299.66, 48000) - toneDb(on.l, 200, 48000);
        const double hi = toneDb(on.l, 2000, 48000) - db(0.05), offRing = toneDb(off.l, 200, 48000) - db(0.3);
        std::snprintf(buf, sizeof buf, "300 Hz over 200 Hz %+.1f dB; 2 kHz %+.2f dB; threshold 0: ring %+.2f dB", up, hi, offRing);
        report("DRUMKEY: retunes the focus band only", up > 6.0 && std::fabs(hi) < 0.5 && std::fabs(offRing) < 0.05, buf);
    }
    // ---- Batches 9 + 10 ----
    {   // MASTER LIMITER: threshold -10, ceiling -1 holds a loud tone at -1 dBFS; ISP holds the between-sample peaks
        // (off: they get through); LINK MID leaves a quiet side at the +6 dB the threshold gives it, L/R ducks it with the mid
        auto e = made("MASTER LIMITER");
        e->set(idxOf(*e, "threshold"), -10.0f); e->set(idxOf(*e, "ceiling"), -1.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs); run(*e, s);
        const double pk = db(peak(s));
        auto tp = [&](float isp) {
            auto x = made("MASTER LIMITER");
            x->set(idxOf(*x, "threshold"), -6.0f); x->set(idxOf(*x, "ceiling"), -1.0f); x->set(idxOf(*x, "isp"), isp);
            Stereo t(static_cast<std::size_t>(fs));
            for (std::size_t i = 0; i < t.size(); ++i) t.l[i] = t.r[i] = 0.5f * static_cast<float>(std::sin(std::numbers::pi * 0.5 * static_cast<double>(i) + std::numbers::pi * 0.25));
            run(*x, t); return truePeakDb(t.l, 24000);
        };
        const double tpOff = tp(0), tpOn = tp(1);
        auto side = [&](float link) {
            auto x = made("MASTER LIMITER");
            x->set(idxOf(*x, "threshold"), -6.0f); x->set(idxOf(*x, "ceiling"), -1.0f); x->set(idxOf(*x, "link"), link);
            Stereo t = sine(1000.0, 2.0, 0.7f, fs); add(t, sine(3000.0, 2.0, 0.02f, fs, true));
            Stereo ref = t;
            run(*x, t);
            std::vector<float> sd(t.size()), sr(t.size());
            for (std::size_t i = 0; i < t.size(); ++i) { sd[i] = 0.5f * (t.l[i] - t.r[i]); sr[i] = 0.5f * (ref.l[i] - ref.r[i]); }
            return toneDb(sd, 3000, 48000) - toneDb(sr, 3000, 48000);
        };
        const double sLR = side(0), sMid = side(1);
        std::snprintf(buf, sizeof buf, "peak %.2f dBFS; true peak ISP off %.2f / on %.2f dBFS; quiet side L/R %+.1f, MID %+.2f dB", pk, tpOff, tpOn, sLR, sMid);
        report("MASTER LIMITER: ceiling, ISP, mid link", pk <= -0.99 && tpOff > -0.5 && tpOn < -0.8 && sLR < 6.0 - 3.0 && std::fabs(sMid - 6.0) < 0.5, buf);   // the -6 dB threshold lifts everything 6 dB first
    }
    {   // OPTICAL COMPRESSOR: threshold -30, 4:1, mix 100 squeezes a -6 dBFS tone hard; after a loud burst the light
        // cell lets go slowly (VINTAGE): a quiet tone 300 ms later is still held down
        auto e = made("OPTICAL COMPRESSOR");
        e->set(idxOf(*e, "threshold"), -30.0f); e->set(idxOf(*e, "ratio"), 4.0f); e->set(idxOf(*e, "mix"), 100.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs); const double in = db(toneAmp(s.l, 1000, 48000, 48000, fs));
        run(*e, s);
        const double cut = db(toneAmp(s.l, 1000, 48000, 48000, fs)) - in;
        auto held = [&](bool burst) {
            auto x = made("OPTICAL COMPRESSOR");
            x->set(idxOf(*x, "threshold"), -30.0f); x->set(idxOf(*x, "ratio"), 4.0f); x->set(idxOf(*x, "mix"), 100.0f);
            x->set(idxOf(*x, "release"), 100.0f); x->set(idxOf(*x, "mode"), 2.0f);
            Stereo t = sine(1000.0, 3.0, 0.02f, fs);
            if (burst) for (std::size_t i = 0; i < static_cast<std::size_t>(2.0 * fs); ++i) { t.l[i] *= 25.0f; t.r[i] *= 25.0f; }
            run(*x, t);
            return db(toneAmp(t.l, 1000, static_cast<std::size_t>(2.3 * fs), 4800, fs));
        };
        const double after = held(true) - held(false);
        std::snprintf(buf, sizeof buf, "-6 dBFS tone %+.1f dB; 300 ms after a burst %+.1f dB vs steady", cut, after);
        report("OPTICAL COMPRESSOR: squeeze and slow light-cell release", cut < -12.0 && cut > -22.0 && after < -2.0, buf);
    }
    {   // PARAMETRIC EQ: MID +12 at 1 kHz; NOTCH at 1 kHz cuts it deep; HPF 24 dB at 1 kHz is ~48 dB down two octaves below;
        // L/R mode runs set A on the left only
        auto mk = [&](const std::vector<std::pair<const char*, float>>& set, double hz, double& l, double& r) {
            auto e = made("PARAMETRIC EQ");
            for (const auto& [id, v] : set) e->set(idxOf(*e, id), v);
            Stereo s = sine(hz, 1.0, 0.05f, fs); const double in = toneDb(s.l, hz, 24000);
            run(*e, s); l = toneDb(s.l, hz, 24000) - in; r = toneDb(s.r, hz, 24000) - in;
        };
        double bell, d1, notch, d2, hp, d3, lrL, lrR;
        mk({{"a_mid_gain", 12.0f}, {"a_mid_q", 1.0f}}, 1000, bell, d1);
        mk({{"a_mid_type", 3.0f}, {"a_mid_q", 2.0f}}, 1000, notch, d2);
        mk({{"a_hpf_power", 1.0f}, {"a_hpf_freq", 1000.0f}, {"a_hpf_slope", 1.0f}}, 250, hp, d3);
        mk({{"eq_mode", 1.0f}, {"a_mid_gain", 6.0f}}, 1000, lrL, lrR);
        std::snprintf(buf, sizeof buf, "bell %+.2f dB; notch %.1f dB; HPF24 2 oct below %.1f dB; L/R mode L %+.2f / R %+.2f dB", bell, notch, hp, lrL, lrR);
        report("PARAMETRIC EQ: bell, notch, HPF slope, L/R sets", std::fabs(bell - 12.0) < 0.1 && notch < -30.0 && hp < -45.0 && hp > -51.0 && std::fabs(lrL - 6.0) < 0.1 && std::fabs(lrR) < 0.05, buf);
    }
    {   // REVERB: every algorithm, DECAY 2 s, measures 2 s +-5 % (backward-integrated RT60); FREEZE holds the tail
        double rts[5];
        for (int a = 0; a < 5; ++a) {
            auto e = made("REVERB");
            e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "algorithm"), static_cast<float>(a)); e->set(idxOf(*e, "diffusion"), 70.0f);
            Stereo s = impulse(8.0); run(*e, s);
            rts[a] = edcRt(s.l, 1000);
        }
        auto f = made("REVERB");
        f->set(idxOf(*f, "mix"), 100.0f);
        Stereo s = noise(4.0, 0.1f, 4, fs);
        for (std::size_t i = static_cast<std::size_t>(0.5 * fs); i < s.size(); ++i) s.l[i] = s.r[i] = 0.0f;   // half a second of noise, then silence
        for (std::size_t k = 0; k < s.size(); k += 512) { if (k == static_cast<std::size_t>(0.5 * fs) / 512 * 512) f->set(idxOf(*f, "freeze"), 1.0f); f->process(s.l.data() + k, s.r.data() + k, static_cast<std::uint32_t>(std::min<std::size_t>(512, s.size() - k))); }
        const double early = db(rmsOf(s.l, static_cast<std::size_t>(0.7 * fs), static_cast<std::size_t>(1.2 * fs))), late = db(rmsOf(s.l, static_cast<std::size_t>(3.0 * fs), static_cast<std::size_t>(3.5 * fs)));
        bool ok = true;
        for (double r : rts) ok = ok && std::fabs(r / 2.0 - 1.0) < 0.05;   // measured RT60 within +-5 % of DECAY
        std::snprintf(buf, sizeof buf, "RT hall %.2f plate %.2f room %.2f chamber %.2f spring %.2f s; freeze %.1f dB over 2.5 s", rts[0], rts[1], rts[2], rts[3], rts[4], late - early);
        report("REVERB: five algorithms decay as set, freeze holds", ok && late - early > -3.0, buf);
    }
    {   // STEREO DELAY: SYNC 1/4 at 120 BPM lands exactly on 500 ms; FEEDBACK 100 % holds the repeats level (no build-up after the input stops); DUAL keeps its own times
        auto e = made("STEREO DELAY");
        e->set(idxOf(*e, "mix"), 100.0f); e->set(idxOf(*e, "tempo_source"), 1.0f); e->set(idxOf(*e, "time_l_div"), 10.0f);
        Stereo s = impulse(1.0); run(*e, s);
        const auto [at, v] = peakAt(s.l, 1001, s.size());
        auto fb = made("STEREO DELAY");
        fb->set(idxOf(*fb, "mix"), 100.0f); fb->set(idxOf(*fb, "sync"), 0.0f); fb->set(idxOf(*fb, "time_l_ms"), 50.0f); fb->set(idxOf(*fb, "feedback_l"), 100.0f);
        Stereo t = noise(10.0, 0.3f, 6, fs);
        for (std::size_t i = static_cast<std::size_t>(0.5 * fs); i < t.size(); ++i) t.l[i] = t.r[i] = 0.0f;
        run(*fb, t);
        const double inLvl = db(rmsOf(t.l, static_cast<std::size_t>(1.0 * fs), static_cast<std::size_t>(2.0 * fs))), endLvl = db(rmsOf(t.l, t.size() - 48000, t.size()));   // after the input stops: held, not growing
        auto d = made("STEREO DELAY");
        d->set(idxOf(*d, "mix"), 100.0f); d->set(idxOf(*d, "sync"), 0.0f); d->set(idxOf(*d, "delay_mode"), 2.0f);
        d->set(idxOf(*d, "time_l_ms"), 100.0f); d->set(idxOf(*d, "time_r_ms"), 300.0f);
        Stereo u = impulse(1.0); run(*d, u);
        const auto [al, vl] = peakAt(u.l, 1001, u.size());
        const auto [ar, vr] = peakAt(u.r, 1001, u.size());
        std::snprintf(buf, sizeof buf, "1/4 @120 %+ld smp; fb 100%%: %+.2f dB from 1-2 s to 9-10 s; DUAL L %+ld R %+ld smp", static_cast<long>(at) - 25000, endLvl - inLvl,
                      static_cast<long>(al) - 5800, static_cast<long>(ar) - 15400);
        report("STEREO DELAY: sync time, capped feedback, dual times", at == 25000 && v > 0.9f && std::fabs(endLvl - inLvl) < 0.5 && al == 5800 && ar == 15400, buf);
    }
    {   // TUBE PREAMP: DRIVE 6 on a -12 dBFS tone: each tube its own recipe (12AX7 strong 2nd, 6L6 odd over even,
        // 12AU7 the cleanest); DRIVE 0 adds nothing
        auto h = [&](float tube, float drive, double& h2, double& h3) {
            auto e = made("TUBE PREAMP"); e->set(idxOf(*e, "tube_stage"), tube); e->set(idxOf(*e, "drive"), drive);
            Stereo s = sine(500.0, 1.0, 0.25f, fs); run(*e, s);
            const double f = toneDb(s.l, 500, 24000); h2 = toneDb(s.l, 1000, 24000) - f; h3 = toneDb(s.l, 1500, 24000) - f;
        };
        double ax2, ax3, au2, au3, l2, l3, c2, c3;
        h(0, 6, ax2, ax3); h(1, 6, au2, au3); h(2, 6, l2, l3); h(0, 0, c2, c3);
        const auto tot = [](double a, double b) { return 10.0 * std::log10(std::pow(10.0, a / 10) + std::pow(10.0, b / 10)); };
        std::snprintf(buf, sizeof buf, "12AX7 2nd %.0f 3rd %.0f; 12AU7 %.0f / %.0f; 6L6 %.0f / %.0f dBc; drive 0: %.0f / %.0f", ax2, ax3, au2, au3, l2, l3, c2, c3);
        report("TUBE PREAMP: three tube recipes, clean at drive 0", ax2 > -40.0 && l3 > l2 + 6.0 && tot(au2, au3) < tot(ax2, ax3) - 3.0 && c2 < -100.0 && c3 < -100.0, buf);
    }
    {   // VCA COMPRESSOR: -20 dB / 4:1 / hard knee / 0.1 ms: a -6 dBFS tone comes out at -16.5 dBFS; SIDECHAIN HPF stops a
        // loud 60 Hz from ducking a quiet 2 kHz part
        auto e = made("VCA COMPRESSOR");
        e->set(idxOf(*e, "threshold"), -20.0f); e->set(idxOf(*e, "ratio"), 4.0f); e->set(idxOf(*e, "knee"), 0.0f); e->set(idxOf(*e, "attack"), 0.1f); e->set(idxOf(*e, "mix"), 100.0f);
        Stereo s = sine(1000.0, 2.0, 0.5f, fs); run(*e, s);
        const double out = db(toneAmp(s.l, 1000, 48000, 48000, fs));
        auto sc = [&](float mode) {
            auto x = made("VCA COMPRESSOR");
            x->set(idxOf(*x, "threshold"), -30.0f); x->set(idxOf(*x, "ratio"), 8.0f); x->set(idxOf(*x, "mix"), 100.0f); x->set(idxOf(*x, "sidechain"), mode);
            Stereo t = sine(60.0, 2.0, 0.5f, fs); add(t, sine(2000.0, 2.0, 0.02f, fs));
            const double in = toneDb(t.l, 2000, 48000);
            run(*x, t); return toneDb(t.l, 2000, 48000) - in;
        };
        const double intC = sc(0), hpfC = sc(1);
        std::snprintf(buf, sizeof buf, "-6 dBFS -> %.2f dBFS (expect -16.5); 2 kHz under a loud 60 Hz: INT %+.1f / HPF %+.1f dB", out, intC, hpfC);
        report("VCA COMPRESSOR: exact curve, sidechain HPF", std::fabs(out + 16.5) < 1.0 && hpfC > intC + 3.0, buf);
    }
    // ---- Batch 8, and the 90-degree phase fix (S.I.P.A. / SUBARCHITECT) ----
    auto corrDb = [&](const Stereo& s, double hz, double& corr, double& lvlL, double& lvlR, double inDb) {
        double lr = 0, ll = 0, rr = 0;
        for (std::size_t i = 24000; i < s.size(); ++i) { lr += static_cast<double>(s.l[i]) * s.r[i]; ll += static_cast<double>(s.l[i]) * s.l[i]; rr += static_cast<double>(s.r[i]) * s.r[i]; }
        corr = lr / std::sqrt(ll * rr); lvlL = toneDb(s.l, hz, 24000) - inDb; lvlR = toneDb(s.r, hz, 24000) - inDb;
    };
    {   // S.I.P.A. PHASE ROTATE 90 and SUBARCHITECT PHASE ANGLE 45 (unlinked: +45 / -45) put a mono tone 90 degrees
        // apart between the sides (correlation ~0) at low and high frequencies, with the level unchanged
        double worstC = 0, worstL = 0;
        for (double hz : {100.0, 1000.0, 5000.0}) {
            auto e = made("S.I.P.A."); e->set(idxOf(*e, "phase_rotate"), 90.0f);
            Stereo s = sine(hz, 1.0, 0.3f, fs); const double in = toneDb(s.l, hz, 24000);
            run(*e, s);
            double c, l, r; corrDb(s, hz, c, l, r, in);
            worstC = std::max(worstC, std::fabs(c)); worstL = std::max({worstL, std::fabs(l), std::fabs(r)});
            auto x = made("SUB-BASS & 808 ARCHITECT"); x->set(idxOf(*x, "mix"), 100.0f); x->set(idxOf(*x, "phase_on"), 1.0f); x->set(idxOf(*x, "link_lr"), 0.0f); x->set(idxOf(*x, "phase_angle"), 45.0f);
            Stereo t = sine(hz, 1.0, 0.3f, fs); run(*x, t);
            corrDb(t, hz, c, l, r, in);
            worstC = std::max(worstC, std::fabs(c)); worstL = std::max({worstL, std::fabs(l), std::fabs(r)});
        }
        std::snprintf(buf, sizeof buf, "100 Hz / 1 kHz / 5 kHz: worst correlation %.3f, worst level %+.2f dB", worstC, worstL);
        report("PHASE FIX: S.I.P.A. rotate and SUBARCHITECT angle are true 90 deg", worstC < 0.05 && worstL < 0.3, buf);
    }
    {   // AIREMBER: AIR 12 dB at 12 kHz lifts 16 kHz by about 10 dB and leaves 1 kHz; EMBER 100 grows an even (2nd)
        // harmonic from a 5 kHz tone, not an odd one
        auto e = made("AIREMBER"); e->set(idxOf(*e, "air_gain"), 12.0f);
        Stereo s = sine(16000.0, 1.0, 0.05f, fs); add(s, sine(1000.0, 1.0, 0.05f, fs));
        const double a0 = toneDb(s.l, 16000, 24000), k0 = toneDb(s.l, 1000, 24000);
        run(*e, s);
        const double air = toneDb(s.l, 16000, 24000) - a0, mid = toneDb(s.l, 1000, 24000) - k0;
        auto x = made("AIREMBER"); x->set(idxOf(*x, "ember_drive"), 100.0f); x->set(idxOf(*x, "softness"), 0.0f);
        Stereo t = sine(5000.0, 1.0, 0.3f, fs); run(*x, t);
        const double f = toneDb(t.l, 5000, 24000), h2 = toneDb(t.l, 10000, 24000) - f, h3 = toneDb(t.l, 15000, 24000) - f;
        std::snprintf(buf, sizeof buf, "16 kHz %+.1f, 1 kHz %+.2f dB; ember 2nd %.0f / 3rd %.0f dBc", air, mid, h2, h3);
        report("AIREMBER: air shelf, even-harmonic ember", air > 8.0 && std::fabs(mid) < 0.1 && h2 > -30.0 && h3 < h2 - 20.0, buf);
    }
    {   // BASSBEACON: SUB 12 dB lifts 60 Hz by 12 and leaves 1 kHz; HARMONICS 100 adds 100 / 150 Hz to a 50 Hz tone;
        // MONO BELOW 120 Hz removes a low side signal and keeps a high one
        auto e = made("BASSBEACON"); e->set(idxOf(*e, "sub_gain"), 12.0f);
        Stereo s = sine(60.0, 1.0, 0.05f, fs); add(s, sine(1000.0, 1.0, 0.05f, fs));
        const double l0 = toneDb(s.l, 60, 24000), k0 = toneDb(s.l, 1000, 24000);
        run(*e, s);
        const double lift = toneDb(s.l, 60, 24000) - l0, mid = toneDb(s.l, 1000, 24000) - k0;
        auto h = made("BASSBEACON"); h->set(idxOf(*h, "harmonics"), 100.0f);
        Stereo t = sine(50.0, 1.0, 0.3f, fs); run(*h, t);
        const double f = toneDb(t.l, 50, 24000), h2 = toneDb(t.l, 100, 24000) - f, h3 = toneDb(t.l, 150, 24000) - f;
        auto m = made("BASSBEACON"); m->set(idxOf(*m, "mono_below_on"), 1.0f); m->set(idxOf(*m, "mono_below_freq"), 120.0f);
        Stereo u = sine(40.0, 1.0, 0.2f, fs, true); add(u, sine(2000.0, 1.0, 0.05f, fs, true));
        const double s40 = toneDb(u.l, 40, 24000), s2k = toneDb(u.l, 2000, 24000);
        run(*m, u);
        const double lowSide = toneDb(u.l, 40, 24000) - s40, hiSide = toneDb(u.l, 2000, 24000) - s2k;
        std::snprintf(buf, sizeof buf, "60 Hz %+.2f, 1 kHz %+.2f dB; harmonics 2nd %.0f 3rd %.0f dBc; mono below: 40 Hz side %.1f, 2 kHz side %+.2f dB", lift, mid, h2, h3, lowSide, hiSide);
        report("BASSBEACON: sub lift, harmonics, mono below", std::fabs(lift - 12.0) < 0.5 && std::fabs(mid) < 0.1 && h2 > -40.0 && h3 > -40.0 && lowSide < -25.0 && std::fabs(hiSide) < 0.5, buf);
    }
    {   // ROOMERASER: AMOUNT 100 keeps the front of each hit and cuts its dying tail; AMOUNT 0 changes nothing
        auto mk = [&](float amount, double& front, double& tail) {
            auto e = made("ROOMERASER"); e->set(idxOf(*e, "amount"), amount);
            Stereo s = hits(2.0); run(*e, s);
            const std::size_t h0 = static_cast<std::size_t>(1.0 * fs);
            front = db(rmsOf(s.l, h0, h0 + static_cast<std::size_t>(0.008 * fs)));
            tail = db(rmsOf(s.l, h0 + static_cast<std::size_t>(0.06 * fs), h0 + static_cast<std::size_t>(0.15 * fs)));
        };
        double f0, t0, f1, t1;
        mk(0, f0, t0); mk(100, f1, t1);
        Stereo ref = hits(2.0);
        const std::size_t h0 = static_cast<std::size_t>(1.0 * fs);
        const double rf = db(rmsOf(ref.l, h0, h0 + static_cast<std::size_t>(0.008 * fs)));
        std::snprintf(buf, sizeof buf, "amount 100: front %+.1f, tail %+.1f dB; amount 0 front %+.2f dB", f1 - f0, t1 - t0, f0 - rf);
        report("ROOMERASER: keeps the hit, cuts the tail", f1 - f0 > -1.5 && t1 - t0 < -6.0 && std::fabs(f0 - rf) < 0.01, buf);
    }
    {   // MONITORSPHERE: PHONE drops 100 Hz hard and lifts 2 kHz; MONO cancels a side-only signal; CROSSFEED 100 puts a
        // hard-left low tone into the right ear; DIM is exactly -20 dB
        auto mk = [&](float mode, float cross, float dim, Stereo s) { auto e = made("MONITORSPHERE"); e->set(idxOf(*e, "mode"), mode); e->set(idxOf(*e, "crossfeed"), cross); e->set(idxOf(*e, "dim"), dim); run(*e, s); return s; };
        Stereo ph = sine(100.0, 1.0, 0.05f, fs); add(ph, sine(2000.0, 1.0, 0.05f, fs));
        const double b0 = toneDb(ph.l, 100, 24000), m0 = toneDb(ph.l, 2000, 24000);
        const Stereo p1 = mk(4, 0, 0, ph);
        const double low = toneDb(p1.l, 100, 24000) - b0, pres = toneDb(p1.l, 2000, 24000) - m0;
        const Stereo mono = mk(5, 0, 0, sine(500.0, 1.0, 0.2f, fs, true));
        Stereo left = sine(300.0, 1.0, 0.2f, fs); for (auto& v : left.r) v = 0.0f;
        const Stereo xf = mk(0, 100, 0, left);
        const double bleed = toneDb(xf.r, 300, 24000) - toneDb(xf.l, 300, 24000);
        const Stereo dm = mk(0, 0, 1, sine(1000.0, 1.0, 0.2f, fs));
        const double dimDb = toneDb(dm.l, 1000, 24000) - db(0.2);
        std::snprintf(buf, sizeof buf, "phone 100 Hz %.1f, 2 kHz %+.1f dB; mono side %.0f dB; crossfeed R vs L %.1f dB; dim %.2f dB", low, pres, db(peak(mono)), bleed, dimDb);
        report("MONITORSPHERE: profiles, mono, crossfeed, dim", low < -20.0 && pres > 4.0 && db(peak(mono)) < -100.0 && bleed > -12.0 && bleed < -3.0 && std::fabs(dimDb + 20.0) < 0.05, buf);
    }
    {   // DE-ESSER: THRESH -30, RANGE 12: SPLIT-BAND cuts a loud 7 kHz part and leaves 500 Hz; WIDEBAND ducks both;
        // RANGE 0 changes nothing
        auto mk = [&](float range, float mode, double& s7, double& s5) {
            auto e = made("DE-ESSER"); e->set(idxOf(*e, "thresh"), -30.0f); e->set(idxOf(*e, "range"), range); e->set(idxOf(*e, "mode"), mode);
            Stereo s = sine(7000.0, 1.0, 0.3f, fs); add(s, sine(500.0, 1.0, 0.1f, fs));
            const double a = toneDb(s.l, 7000, 24000), b = toneDb(s.l, 500, 24000);
            run(*e, s); s7 = toneDb(s.l, 7000, 24000) - a; s5 = toneDb(s.l, 500, 24000) - b;
        };
        double sp7, sp5, wd7, wd5, z7, z5;
        mk(12, 0, sp7, sp5); mk(12, 1, wd7, wd5); mk(0, 0, z7, z5);
        std::snprintf(buf, sizeof buf, "split: 7 kHz %+.1f, 500 Hz %+.2f dB; wide: %+.1f / %+.1f dB; range 0: %+.2f / %+.2f dB", sp7, sp5, wd7, wd5, z7, z5);
        report("DE-ESSER: split-band, wideband, range 0", sp7 < -8.0 && std::fabs(sp5) < 0.5 && wd7 < -8.0 && wd5 < -6.0 && std::fabs(z7) < 0.01 && std::fabs(z5) < 0.01, buf);
    }
    {   // IDLE AT DEAD-FLAT: NOTEFORGE sits idle (correction 0, detector analysis paused) for 1.5 s on a 452 Hz voice,
        // then correction goes to 100 % mid-stream: the detector picks up at once and the output lands on A4 (440 Hz)
        auto e = made("NOTEFORGE");
        e->set(idxOf(*e, "speed"), 0.1f); e->set(idxOf(*e, "transition"), 0.1f); e->set(idxOf(*e, "vibrato"), 0.0f); e->set(idxOf(*e, "correction"), 0.0f);
        Stereo s = voiceTone(452.0, 3.5, fs);
        const std::size_t sw = static_cast<std::size_t>(1.5 * fs) / 512 * 512;
        for (std::size_t k = 0; k < s.size(); k += 512) {
            if (k == sw) e->set(idxOf(*e, "correction"), 100.0f);
            e->process(s.l.data() + k, s.r.data() + k, static_cast<std::uint32_t>(std::min<std::size_t>(512, s.size() - k)));
        }
        const std::size_t len = static_cast<std::size_t>(0.8 * fs);
        const double got = measuredPitch(s.l, 440.0, s.size() - len, len, fs);
        const double cents = 1200.0 * std::log2(got / 440.0);
        std::snprintf(buf, sizeof buf, "after idle: %.2f Hz (%+.1f cents from A4)", got, cents);
        report("IDLE: NOTEFORGE resumes in tune after sitting idle", std::fabs(cents) < 5.0, buf);
    }
}
} // namespace

void testNewline() {
    std::printf("[STAGING: %zu new HEM engines — parameters, dead flat, stability, no allocation, latency, clicks]\n", hem::staging::plugins().size());
    for (const auto& info : hem::staging::plugins()) {
        auto e = info.create();
        checkParams(info, *e);
        checkNeutral(info);
        checkStability(info);
        checkNoAlloc(info);
        checkLatency(info);
        checkClicks(info);
    }
    testNoteForgePitch();
    testJobs();
}

} // namespace hem::test
