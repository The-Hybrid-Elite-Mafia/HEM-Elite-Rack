// The maximizers (BIG SHOT, LAST WORD, HIGH STAKES, TOP DOG, SWEET SIXTEEN, FAST MONEY, QUICK CASH):
// what each must actually do, measured, plus the rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Maximizers.hpp"
#include "hem/rack/Plugins.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <utility>

namespace hem::test {
namespace {

using namespace hem::rack;
constexpr double kFs = 48000.0;

std::unique_ptr<hc::fx::Effect> make(const char* name, std::initializer_list<std::pair<int, float>> v = {}) {
    auto e = findPlugin(name)->create();
    for (const auto& [i, x] : v) e->set(i, x);
    e->prepare(kFs, 512);
    return e;
}
double maxDelayedDiff(const Stereo& out, const Stereo& in, std::uint32_t lat, std::size_t from) {
    double d = 0;
    for (std::size_t i = std::max<std::size_t>(from, lat); i < out.size(); ++i)
        d = std::max({d, static_cast<double>(std::fabs(out.l[i] - in.l[i - lat])), static_cast<double>(std::fabs(out.r[i] - in.r[i - lat]))});
    return d;
}
double peakDb(const Stereo& s, std::size_t from) {
    double pk = 0;
    for (std::size_t i = from; i < s.size(); ++i) pk = std::max({pk, static_cast<double>(std::fabs(s.l[i])), static_cast<double>(std::fabs(s.r[i]))});
    return db(pk);
}
// the peak between the samples too: 8x, 64-tap windowed sinc (a finer look than the plug-ins use)
double truePeakDb(const std::vector<float>& x, std::size_t from) {
    double pk = 0;
    for (std::size_t i = std::max<std::size_t>(from, 32); i + 33 < x.size(); ++i)
        for (int j = 0; j < 8; ++j) {
            const double t = j / 8.0;
            double v = 0;
            for (int k = -31; k <= 32; ++k) {
                const double u = t - k;
                const double s = std::fabs(u) < 1e-12 ? 1.0 : std::sin(std::numbers::pi * u) / (std::numbers::pi * u);
                const double w = 0.5 + 0.5 * std::cos(std::numbers::pi * u / 33.0);
                v += x[static_cast<std::size_t>(static_cast<long>(i) + k)] * s * w;
            }
            pk = std::max(pk, std::fabs(v));
        }
    return db(pk);
}
// drum-like hits on a body
Stereo loudMix(double seconds, float amp) {
    Stereo s = noise(seconds, amp * 0.5f, 5);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / kFs, b = std::fmod(t, 0.5);
        const float v = amp * static_cast<float>(std::exp(-b * 18.0) * std::sin(2 * std::numbers::pi * 120 * b));
        s.l[i] += v;
        s.r[i] += v;
    }
    return s;
}
// two steady tones; each one's level change (dB) over the last half second
void twoTones(hc::fx::Effect& e, double f1, double f2, float amp, double& d1, double& d2) {
    e.reset();
    Stereo s = sine(f1, 3.0, amp);
    add(s, sine(f2, 3.0, amp));
    run(e, s);
    d1 = db(toneAmp(s.l, f1, s.size() - 24000, 24000) / amp);
    d2 = db(toneAmp(s.l, f2, s.size() - 24000, 24000) / amp);
}

void allRules() {
    for (const char* name : {"BIG SHOT", "LAST WORD", "HIGH STAKES", "TOP DOG", "SWEET SIXTEEN", "FAST MONEY", "QUICK CASH"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

// every maximizer: never past the ceiling, and louder
void testCeilings() {
    struct Case { const char* name; int thr, ceil, extra; float v; std::uint32_t lat; };
    const Case cases[] = {
        {"BIG SHOT", bigshot::Threshold, bigshot::Ceiling, -1, 0, bigshot::kLatency},
        {"LAST WORD", lastword::Threshold, lastword::Ceiling, -1, 0, lastword::kLatency},
        {"HIGH STAKES", multimax::Threshold, multimax::Ceiling, -1, 0, multimax::kLatencyLinear},
        {"FAST MONEY", multimax::Threshold, multimax::Ceiling, -1, 0, multimax::kLatencyFast},
        {"SWEET SIXTEEN", sixteen::Threshold, sixteen::Ceiling, -1, 0, sixteen::kLatency},
        {"SWEET SIXTEEN", sixteen::Threshold, sixteen::Ceiling, sixteen::Separation, 0, sixteen::kLatency},
        {"SWEET SIXTEEN", sixteen::Threshold, sixteen::Ceiling, sixteen::Separation, 2, sixteen::kLatency},
    };
    for (const Case& c : cases) {
        auto e = make(c.name, {{c.thr, -12}, {c.ceil, -1}});
        if (c.extra >= 0) { e->set(c.extra, c.v); e->reset(); }
        CHECK(e->latency() == c.lat);
        Stereo s = loudMix(3.0, 0.6f);
        const double inRms = db(rms(s.l, 48000, 96000));
        run(*e, s);
        const double pk = peakDb(s, 0), outRms = db(rms(s.l, 48000 + c.lat, 96000));
        std::printf("  %s%s threshold -12, ceiling -1: peak %.2f dBFS, %+.1f dB louder\n", c.name, c.extra >= 0 ? (c.v == 0 ? " (separation low)" : " (separation high)") : "",
                    pk, outRms - inRms);
        CHECK(pk <= -0.999 && outRms - inRms > 1.5);
    }
    for (int prof = 0; prof < ultramax::kProfiles; ++prof)
        for (const char* name : {"TOP DOG", "QUICK CASH"}) {
            auto e = make(name, {{ultramax::Threshold, -12}, {ultramax::Ceiling, -1}, {ultramax::Profile, static_cast<float>(prof)}});
            Stereo s = loudMix(2.0, 0.6f);
            const double inRms = db(rms(s.l, 48000, 48000));
            run(*e, s);
            const double pk = peakDb(s, 0), up = db(rms(s.l, 48000 + e->latency(), 48000)) - inRms;
            if (prof == 0 || pk > -0.999 || up < 0.5)
                std::printf("  %s profile %s: peak %.2f dBFS, %+.1f dB louder\n", name, e->params()[ultramax::Profile].choices[prof], pk, up);
            CHECK(pk <= -0.999 && up > 0.5);
        }
}

// IDR: word length, dither, shaping — measured on the error left after a quiet tone goes through
void testIdr() {
    using namespace bigshot;
    auto errorOf = [](int dither, int shaping, double& total, double& mid) {
        auto e = make("BIG SHOT", {{Ceiling, 0}, {Quantize, 4}, {Dither, static_cast<float>(dither)}, {Shaping, static_cast<float>(shaping)}});
        Stereo in = sine(1000, 1.5, 0.01f), out = in;
        run(*e, out);
        Stereo err(in.size());
        for (std::size_t i = kLatency; i < in.size(); ++i) { err.l[i] = out.l[i] - in.l[i - kLatency]; err.r[i] = out.r[i] - in.r[i - kLatency]; }
        total = db(rms(err.l, 24000, 48000));
        mid = bandLevelDb(err.l, 2500, 5000, 24000, 48000);
    };
    double t1, m1, t2, m2, tn, mn;
    errorOf(0, 3, t1, m1);
    errorOf(1, 3, t2, m2);
    errorOf(2, 3, tn, mn);
    std::printf("  16 bit, no shaping: error %.1f dBFS with TYPE 1, %.1f with TYPE 2, %.1f with no dither (TYPE 1 should be -96.3)\n", t1, t2, tn);
    CHECK(std::fabs(t1 + 96.3) < 1.5 && t2 < t1 - 2.0);
    double prev = m1;
    for (int sh = 0; sh < 3; ++sh) {
        double t, m;
        errorOf(0, sh, t, m);
        std::printf("  TYPE 1 + %s: noise at 2.5-5 kHz %+.1f dB against no shaping (overall %+.1f dB)\n", sh == 0 ? "MODERATE" : sh == 1 ? "NORMAL" : "ULTRA", m - m1, t - t1);
        CHECK(m < prev - 2.0);
        prev = m;
    }
    // digital silence stays silence, even at 16 bits with dither and shaping
    auto e = make("BIG SHOT", {{Quantize, 4}, {Dither, 0}, {Shaping, 2}});
    Stereo z = sine(1000, 0.5, 0.3f);
    Stereo tail(48000);
    z.l.insert(z.l.end(), tail.l.begin(), tail.l.end());
    z.r.insert(z.r.end(), tail.r.begin(), tail.r.end());
    run(*e, z);
    CHECK(peakDb(z, 24000 + 2000) < -200.0);
    // under the ceiling with THRESHOLD 0 and no dither: the sound, unchanged (late)
    auto clean = make("BIG SHOT", {{Ceiling, 0}, {Dither, 2}, {Shaping, 3}});
    Stereo n = noise(0.5, 0.2f), ref = n;
    run(*clean, n);
    CHECK(maxDelayedDiff(n, ref, kLatency, 0) < 1e-6);
}

void testBigShot() {
    using namespace bigshot;
    // AUTO RELEASE: after a loud burst, a quiet tone comes back up quicker than with the steady 100 ms
    auto rec = [](float autoRel) {
        auto e = make("BIG SHOT", {{Threshold, -12}, {AutoRelease, autoRel}});
        Stereo s = sine(1000, 0.05, 0.9f);
        Stereo q = sine(1000, 1.0, 0.05f);
        s.l.insert(s.l.end(), q.l.begin(), q.l.end());
        s.r.insert(s.r.end(), q.r.begin(), q.r.end());
        run(*e, s);
        return db(toneAmp(s.l, 1000, 2400 + kLatency + 1200, 2400) / 0.05f);   // 25..75 ms after the burst
    };
    const double a = rec(1), m = rec(0);
    std::printf("  BIG SHOT 25-75 ms after a short burst: %+.1f dB with AUTO RELEASE, %+.1f dB with it off (+12 = fully back)\n", a, m);
    CHECK(a > m + 1.0);
}

void testLastWord() {
    using namespace lastword;
    // a 12 kHz tone sampled at 45 degrees: the samples read 3 dB under the real peak
    auto tp = [](int domain, double& samplePk, double& truePk) {
        auto e = make("LAST WORD", {{Threshold, -6}, {Ceiling, -1}, {Domain, static_cast<float>(domain)}});
        Stereo s(static_cast<std::size_t>(kFs));
        for (std::size_t i = 0; i < s.size(); ++i) s.l[i] = s.r[i] = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * 0.25 * i + std::numbers::pi / 4));
        run(*e, s);
        samplePk = peakDb(s, 4800);
        truePk = truePeakDb(s.l, 4800);
    };
    double ds, dt, as, at, ts, tt;
    tp(0, ds, dt);
    tp(1, as, at);
    tp(2, ts, tt);
    std::printf("  LAST WORD ceiling -1, peaks between samples: DIGITAL samples %.2f / true %.2f dBTP; ANALOG %.2f / %.2f; TRUE PEAK %.2f / %.2f\n", ds, dt, as, at, ts, tt);
    CHECK(ds <= -0.999 && dt > -0.5 && tt <= -0.9 && at <= -0.9);
    // INPUT LINK: the right side follows INPUT LEFT
    auto linked = make("LAST WORD", {{InputL, -12}, {InputR, 0}, {InputLink, 1}});
    auto apart = make("LAST WORD", {{InputL, -12}, {InputR, 0}, {InputLink, 0}});
    Stereo a = sine(500, 1.0, 0.1f), b = a;
    run(*linked, a);
    run(*apart, b);
    const double lr = db(toneAmp(a.r, 500, 24000, 24000) / 0.1), ar = db(toneAmp(b.r, 500, 24000, 24000) / 0.1);
    std::printf("  LAST WORD input left -12: right side %+.1f dB linked, %+.1f dB unlinked (input right 0)\n", lr, ar);
    CHECK(std::fabs(lr + 12.0) < 0.2 && std::fabs(ar) < 0.2);
    // RELEASE: 1 ms lets a quiet tone back up at once; 1000 ms holds it down
    auto rel = [](float ms) {
        auto e = make("LAST WORD", {{Threshold, -12}, {Release, ms}});
        Stereo s = sine(1000, 0.3, 0.9f);
        Stereo q = sine(1000, 1.0, 0.05f);
        s.l.insert(s.l.end(), q.l.begin(), q.l.end());
        s.r.insert(s.r.end(), q.r.begin(), q.r.end());
        run(*e, s);
        return db(toneAmp(s.l, 1000, 14400 + kLatency + 2400, 4800) / 0.05f);
    };
    const double fast = rel(1.0f), slow = rel(1000.0f);
    std::printf("  LAST WORD 50-150 ms after a loud part: %+.1f dB at release 1 ms, %+.1f dB at 1000 ms\n", fast, slow);
    CHECK(fast > 11.0 && slow < fast - 4.0);
}

void testFiveBands() {
    using namespace multimax;
    // the five bands add back up to the sound (late), at THRESHOLD 0
    {
        auto e = make("HIGH STAKES", {{Dither, 2}, {Shaping, 3}});
        Stereo s = noise(1.0, 0.2f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, kLatencyLinear, 0);
        std::printf("  HIGH STAKES at threshold 0: the bands add back up to the sound within %.2g (latency %u)\n", d, kLatencyLinear);
        CHECK(d < 1e-4);
        auto f = make("FAST MONEY", {{Dither, 2}, {Shaping, 3}});
        Stereo t = noise(1.0, 0.2f), tr = t;
        run(*f, t);
        const double change = db(rms(t.l, 12000, 36000)) - db(rms(tr.l, 12000 - kLatencyFast, 36000));
        std::printf("  FAST MONEY at threshold 0: level %+.3f dB (an all-pass: same level, phase only)\n", change);
        CHECK(std::fabs(change) < 0.1);
    }
    for (const char* name : {"HIGH STAKES", "FAST MONEY"}) {
        // 40 Hz (band 1) and 2.5 kHz (band 4), equally loud, pushed 12 dB
        double l0, h0, lp, hp;
        auto even = make(name, {{Threshold, -12}});
        twoTones(*even, 40, 2500, 0.35f, l0, h0);
        auto prio = make(name, {{Threshold, -12}, {band(0, Priority), 12}});
        twoTones(*prio, 40, 2500, 0.35f, lp, hp);
        std::printf("  %s pushed 12 dB, 40 Hz / 2.5 kHz: even %+.1f / %+.1f; band 1 priority +12 %+.1f / %+.1f dB\n", name, l0, h0, lp, hp);
        CHECK(std::fabs(l0 - h0) < 1.5 && lp > hp + 3.0);
        // SEPARATION: a loud 40 Hz and a quiet 2.5 kHz. 100: the loud band gives the most; 0: both the same
        auto sepTones = [&](float sep, double& dl, double& dh) {
            auto e = make(name, {{Threshold, -12}, {Separation, sep}});
            Stereo s = sine(40, 3.0, 0.5f);
            add(s, sine(2500, 3.0, 0.08f));
            run(*e, s);
            dl = db(toneAmp(s.l, 40, s.size() - 24000, 24000) / 0.5);
            dh = db(toneAmp(s.l, 2500, s.size() - 24000, 24000) / 0.08);
        };
        double sl0, sh0, sl1, sh1;
        sepTones(0, sl0, sh0);
        sepTones(100, sl1, sh1);
        std::printf("  %s loud 40 Hz + quiet 2.5 kHz, pushed 12 dB: separation 0 %+.1f / %+.1f, separation 100 %+.1f / %+.1f dB\n", name, sl0, sh0, sl1, sh1);
        CHECK(std::fabs(sl0 - sh0) < 0.5 && sh1 > sl1 + 3.0);
        // SOLO band 4: only the 2.5 kHz tone
        double sl, sh;
        auto solo = make(name, {{band(3, Solo), 1}});
        twoTones(*solo, 40, 2500, 0.2f, sl, sh);
        std::printf("  %s solo band 4: 40 Hz %+.1f dB, 2.5 kHz %+.1f dB\n", name, sl, sh);
        CHECK(sl < -30.0 && std::fabs(sh) < 1.5);
        // GAIN: +6 on band 4 with nothing to limit; pushed hard, the band takes more of the limiting
        double ql, qh;
        auto gain = make(name, {{band(3, Gain), 6}});
        twoTones(*gain, 40, 2500, 0.05f, ql, qh);
        auto gainHard = make(name, {{Threshold, -12}, {band(3, Gain), 6}});
        double gl2, gh2;
        twoTones(*gainHard, 40, 2500, 0.35f, gl2, gh2);
        std::printf("  %s band 4 gain +6: quiet %+.1f / %+.1f dB; pushed 12 dB %+.1f / %+.1f dB\n", name, ql, qh, gl2, gh2);
        CHECK(std::fabs(ql) < 0.3 && std::fabs(qh - 6.0) < 1.5 && gl2 > l0);
        const double gh = gh2;
        // LINK: GAIN also raises PRIORITY -> the boosted band keeps more of its boost
        double kl, kh;
        auto link = make(name, {{Threshold, -12}, {band(3, Gain), 6}, {band(3, BandLink), 1}});
        twoTones(*link, 40, 2500, 0.35f, kl, kh);
        std::printf("  %s band 4 gain +6 and LINK, pushed 12 dB: 2.5 kHz %+.1f dB (%+.1f without LINK)\n", name, kh, gh);
        CHECK(kh > gh + 1.0);
        // LINK (threshold / ceiling): the limiting without the loudness
        auto lk = make(name, {{Threshold, -6}, {Link, 1}});
        Stereo s = loudMix(2.0, 0.6f);
        run(*lk, s);
        CHECK(peakDb(s, 0) <= -5.99);
    }
    // the crossovers can move while it plays, without clicks or allocation (covered by the rules); here: they do move
    {
        auto e = make("HIGH STAKES", {{X1, 40}});
        double a, b;
        twoTones(*e, 60, 2500, 0.2f, a, b);
        auto s = make("HIGH STAKES", {{X1, 40}, {band(1, Solo), 1}});
        double sl, sh;
        twoTones(*s, 60, 2500, 0.2f, sl, sh);   // with split 1 at 40 Hz, 60 Hz is in band 2
        CHECK(sl > -1.0);
    }
    const double d = blockSizeDifference(*findPlugin("HIGH STAKES"), [](hc::fx::Effect& x) { x.set(Threshold, -10); x.set(band(2, Priority), 4); }, loudMix(1.0, 0.6f));
    const double f = blockSizeDifference(*findPlugin("FAST MONEY"), [](hc::fx::Effect& x) { x.set(Threshold, -10); x.set(ReleaseType, 3); }, loudMix(1.0, 0.6f));
    CHECK(d < 1e-5 && f < 1e-5);
}

void testProfiles() {
    using namespace ultramax;
    double l, h;
    auto one = make("TOP DOG", {{Threshold, -12}, {Profile, 4}});   // SINGLE FIXED: all bands give the same
    twoTones(*one, 40, 2500, 0.35f, l, h);
    double vl, vh;
    auto velvet = make("TOP DOG", {{Threshold, -12}, {Profile, 5}});   // VELVET: the lows keep more
    twoTones(*velvet, 40, 2500, 0.35f, vl, vh);
    std::printf("  TOP DOG pushed 12 dB, 40 Hz / 2.5 kHz: SINGLE FIXED %+.1f / %+.1f dB, VELVET %+.1f / %+.1f dB\n", l, h, vl, vh);
    CHECK(std::fabs(l - h) < 0.5 && vl > vh + 1.0);
    // RELEASE x0.1 recovers sooner than x10
    auto rel = [](float x) {
        auto e = make("QUICK CASH", {{Threshold, -12}, {Release, x}});
        Stereo s = sine(1000, 0.3, 0.9f);
        Stereo q = sine(1000, 1.0, 0.05f);
        s.l.insert(s.l.end(), q.l.begin(), q.l.end());
        s.r.insert(s.r.end(), q.r.begin(), q.r.end());
        run(*e, s);
        return db(toneAmp(s.l, 1000, 14400 + e->latency() + 2400, 4800) / 0.05f);
    };
    const double fast = rel(0.1f), slow = rel(10.0f);
    std::printf("  QUICK CASH 50-150 ms after a loud part: %+.1f dB at release x0.1, %+.1f dB at x10\n", fast, slow);
    CHECK(fast > slow + 3.0);
}

void testSixteen() {
    using namespace sixteen;
    // the sixteen bands add back up, at each SEPARATION
    for (int sep = 0; sep < 3; ++sep) {
        auto e = make("SWEET SIXTEEN", {{Separation, static_cast<float>(sep)}, {Dither, 2}, {Shaping, 3}});
        Stereo s = noise(1.0, 0.2f), ref = s;
        run(*e, s);
        const double d = maxDelayedDiff(s, ref, kLatency, 0);
        std::printf("  SWEET SIXTEEN separation %s: the bands add back up within %.2g\n", e->params()[Separation].choices[sep], d);
        CHECK(d < 1e-4);
    }
    // GAIN is an EQ before the limiter: band 3 (400 Hz bell) +6 on a quiet tone
    auto eq = make("SWEET SIXTEEN", {{band(2, Gain), 6}});
    Stereo t = sine(400, 1.0, 0.05f);
    run(*eq, t);
    const double g = db(toneAmp(t.l, 400, 24000, 24000) / 0.05);
    auto off = make("SWEET SIXTEEN", {{band(2, Gain), 6}, {band(2, In), 0}});
    Stereo u = sine(400, 1.0, 0.05f);
    run(*off, u);
    const double go = db(toneAmp(u.l, 400, 24000, 24000) / 0.05);
    std::printf("  SWEET SIXTEEN band 3 gain +6 at 400 Hz: %+.2f dB (band out: %+.2f dB)\n", g, go);
    CHECK(std::fabs(g - 6.0) < 0.3 && std::fabs(go) < 0.1);
    // PRIORITY: the low shelf (band 1) +12 -> the lows give less
    double l0, h0, lp, hp;
    auto even = make("SWEET SIXTEEN", {{Threshold, -12}});
    twoTones(*even, 50, 2500, 0.35f, l0, h0);
    auto prio = make("SWEET SIXTEEN", {{Threshold, -12}, {band(0, Priority), 12}});
    twoTones(*prio, 50, 2500, 0.35f, lp, hp);
    std::printf("  SWEET SIXTEEN pushed 12 dB, 50 Hz / 2.5 kHz: even %+.1f / %+.1f; low-shelf priority +12 %+.1f / %+.1f dB\n", l0, h0, lp, hp);
    CHECK(lp > hp + 3.0 && lp > l0);
    const double d = blockSizeDifference(*findPlugin("SWEET SIXTEEN"), [](hc::fx::Effect& x) { x.set(Threshold, -10); x.set(Character, 9); }, loudMix(1.0, 0.6f));
    CHECK(d < 1e-5);
}

} // namespace

void testMaximizers() {
    std::printf("[the maximizers]\n");
    allRules();
    testCeilings();
    testIdr();
    testBigShot();
    testLastWord();
    testFiveBands();
    testProfiles();
    testSixteen();
}

} // namespace hem::test
