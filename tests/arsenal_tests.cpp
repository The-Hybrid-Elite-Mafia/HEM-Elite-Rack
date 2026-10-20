// The dynamics arsenal: what each plug-in must actually do to the sound, measured, plus the
// rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Arsenal.hpp"
#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/Plugins.hpp"
#include "hybrid/fx/Dsp.hpp"

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
// Level (dB re amp) of a steady tone after `seconds` (last 0.25 s; latency doesn't matter there).
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
double peakDb(const Stereo& s, std::size_t from) {
    double pk = 0;
    for (std::size_t i = from; i < s.size(); ++i) pk = std::max({pk, static_cast<double>(std::fabs(s.l[i])), static_cast<double>(std::fabs(s.r[i]))});
    return db(pk);
}
// Drum-like hits: a 120 Hz thump with a sharp attack every 0.5 s, on a steady body.
Stereo hits(double seconds, float amp) {
    Stereo s(static_cast<std::size_t>(seconds * kFs));
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / kFs, b = std::fmod(t, 0.5);
        const float v = amp * static_cast<float>(std::exp(-b * 18.0) * std::sin(2 * std::numbers::pi * 120 * b));
        s.l[i] = s.r[i] = v;
    }
    return s;
}
// Peak (dB) in the first 15 ms after each hit vs. the energy 150..400 ms after it.
void hitAndTail(const Stereo& s, std::uint32_t lat, double& hitDb, double& tailDb) {
    double h = 0, t = 0;
    int n = 0;
    for (double start = 1.0; start + 0.5 < static_cast<double>(s.size()) / kFs; start += 0.5, ++n) {
        const auto a = static_cast<std::size_t>(start * kFs) + lat;
        for (std::size_t i = a; i < a + 720 && i < s.size(); ++i) h = std::max(h, static_cast<double>(std::fabs(s.l[i])));
        t += rms(s.l, a + 7200, 12000);
    }
    hitDb = db(h);
    tailDb = db(t / std::max(n, 1));
}

void allRules() {
    for (const char* name : {"SWITCHBLADE", "TERRITORY", "CONSIGLIERE", "HEAVYWEIGHT", "THE DON", "SLOW BURN", "IRON FIST", "KNOCKOUT",
                             "FULL HOUSE", "WHEELMAN", "SILENCER", "BREATHLESS", "TOMMY GUN"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

void testSwitchblade() {
    using namespace switchblade;
    {   // 4:1 over -30 dBFS: a -6 dBFS tone comes out 18 dB lower (24 over -> 6 over)
        auto e = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 4}});
        const double out = toneOut(*e, 1000, 0.5f);
        std::printf("  SWITCHBLADE 4:1 at -30: a -6 dBFS tone comes out %.1f dB\n", out);
        CHECK(std::fabs(out + 18.0) < 1.5);
    }
    {   // the gate: open at -40, range 40: a -60 dBFS tone drops ~40 dB; a -20 one passes
        auto e = make("SWITCHBLADE", {{GateOpen, -40}, {GateRange, 40}});
        const double quiet = toneOut(*e, 1000, 0.001f, 2.0), loud = toneOut(*e, 1000, 0.1f, 2.0);
        std::printf("  SWITCHBLADE gate: -60 dBFS tone %.1f dB, -20 dBFS tone %.1f dB\n", quiet, loud);
        CHECK(quiet < -35.0 && std::fabs(loud) < 0.5);
    }
    {   // KEY high-pass at 1 kHz: a loud 60 Hz tone no longer triggers the compressor
        auto off = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 8}});
        auto key = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 8}, {FilterType, 1}, {FilterFreq, 1000}});
        const double a = toneOut(*off, 60, 0.5f), b = toneOut(*key, 60, 0.5f);
        std::printf("  SWITCHBLADE 60 Hz at -6 dBFS: plain %.1f dB, key high-passed %.1f dB\n", a, b);
        CHECK(a < -12.0 && b > -3.0);
    }
    {   // BAND mode, band-pass at 3 kHz: a 3 kHz tone is compressed, a 200 Hz tone beside it is not
        auto e = make("SWITCHBLADE", {{Threshold, -40}, {Ratio, 10}, {FilterType, 3}, {FilterUse, 1}, {FilterFreq, 3000}, {FilterQ, 2}});
        e->reset();
        Stereo s = sine(3000, 1.5, 0.3f);
        add(s, sine(200, 1.5, 0.3f));
        run(*e, s);
        const double hi = db(toneAmp(s.l, 3000, s.size() - 12000, 12000) / 0.3), lo = db(toneAmp(s.l, 200, s.size() - 12000, 12000) / 0.3);
        std::printf("  SWITCHBLADE band mode at 3 kHz: 3 kHz %.1f dB, 200 Hz %.1f dB\n", hi, lo);
        CHECK(hi < -8.0 && lo > -2.0);
    }
    const PluginInfo* info = findPlugin("SWITCHBLADE");
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Threshold, -30); x.set(GateOpen, -30); x.set(FilterType, 2); x.set(Lookahead, 1); },
                                         noise(1.0, 0.5f, 3));
    CHECK(d < 1e-5);
}

void testTerritory() {
    using namespace territory;
    for (int phase = 0; phase < 2; ++phase) {   // neutral: flat (linear: the input, exactly, 2048 samples late)
        auto e = make("TERRITORY", {{Phase, static_cast<float>(phase)}});
        double worst = 0;
        for (double hz : {40.0, 120.0, 700.0, 3000.0, 9000.0, 15000.0}) worst = std::max(worst, std::fabs(toneOut(*e, hz, 0.25f, 1.0)));
        std::printf("  TERRITORY neutral, %s phase: flat within %.2f dB\n", phase ? "linear" : "natural", worst);
        CHECK(worst < 0.25);
        if (phase == 1) {
            e->reset();
            Stereo s = noise(1.0, 0.3f, 9), ref = s;
            run(*e, s);
            const double dd = maxDelayedDiff(s, ref, kLatency, kLatency + 4096);
            std::printf("  TERRITORY linear phase, neutral: output = input %u samples late, within %.1e\n", kLatency, dd);
            CHECK(dd < 2e-4);
        }
    }
    for (int phase = 0; phase < 2; ++phase) {   // band 3 pulled down 12 dB at most: a loud 1.2 kHz tone drops, 60 Hz doesn't
        auto e = make("TERRITORY", {{thr(2), -40}, {range(2), -12}, {Phase, static_cast<float>(phase)}});
        const double mid = toneOut(*e, 1200, 0.5f, 2.0), low = toneOut(*e, 60, 0.5f, 2.0);
        std::printf("  TERRITORY band 3 range -12, %s: 1.2 kHz %.1f dB, 60 Hz %.1f dB\n", phase ? "linear" : "natural", mid, low);
        CHECK(mid < -8.5 && mid > -12.5 && std::fabs(low) < 1.0);   // the bands beside it keep their share
    }
    {   // RANGE + lifts a band above its threshold; GAIN is a plain level
        auto up = make("TERRITORY", {{thr(1), -30}, {range(1), 6}});
        auto g = make("TERRITORY", {{gain(4), -6}});
        const double a = toneOut(*up, 400, 0.3f, 2.0), b = toneOut(*g, 14000, 0.1f);
        std::printf("  TERRITORY band 2 range +6: 400 Hz %+.1f dB; band 5 gain -6: 14 kHz %+.1f dB\n", a, b);
        CHECK(a > 4.0 && a < 6.5 && std::fabs(b + 6.0) < 0.6);
    }
    const PluginInfo* info = findPlugin("TERRITORY");
    for (int phase = 0; phase < 2; ++phase) {
        const double d = blockSizeDifference(*info, [phase](hc::fx::Effect& x) { x.set(Phase, static_cast<float>(phase)); x.set(range(0), -10); x.set(range(3), 6); },
                                             noise(1.0, 0.5f, 4));
        CHECK(d < 1e-4);
    }
}

void testLevelers() {
    {   using namespace consigliere;
        auto light = make("CONSIGLIERE", {{PeakReduction, 30}}), hard = make("CONSIGLIERE", {{PeakReduction, 70}});
        auto lim = make("CONSIGLIERE", {{PeakReduction, 70}, {Mode, 1}});
        auto solid = make("CONSIGLIERE", {{PeakReduction, 70}, {Model, 1}});
        const double a = toneOut(*light, 1000, 0.3f, 3.0), b = toneOut(*hard, 1000, 0.3f, 3.0), c = toneOut(*lim, 1000, 0.3f, 3.0);
        const double d = toneOut(*solid, 1000, 0.3f, 3.0);
        std::printf("  CONSIGLIERE -10 dBFS tone: PR 30 %.1f dB, PR 70 %.1f dB, PR 70 LIMIT %.1f dB, PR 70 SOLID %.1f dB\n", a, b, c, d);
        CHECK(b < a - 3.0 && c < b - 2.0 && d < -3.0);
        auto dry = make("CONSIGLIERE", {{PeakReduction, 90}, {Mix, 0}});
        Stereo s = noise(0.5, 0.5f), ref = s;
        run(*dry, s);
        CHECK(maxDelayedDiff(s, ref, 0, 0) < 1e-6);
    }
    {   using namespace thedon;
        auto soft = make("THE DON", {{Threshold, -24}, {Ratio, 0}}), hard = make("THE DON", {{Threshold, -24}, {Ratio, 5}});
        const double a = toneOut(*soft, 500, 0.5f, 3.0), b = toneOut(*hard, 500, 0.5f, 3.0);
        std::printf("  THE DON -6 dBFS tone over -24: ratio 1.5 %.1f dB, LIMIT %.1f dB\n", a, b);
        CHECK(a < -2.0 && b < a - 6.0);
    }
    {   using namespace slowburn;
        // DUAL: a tone only on the left leaves the right alone; LINKED pulls both
        auto run1 = [&](float link) {
            auto e = make("SLOW BURN", {{Threshold, -30}, {Link, link}, {Tube, 0}});
            Stereo s = sine(500, 3.0, 0.5f);
            Stereo r = sine(300, 3.0, 0.02f);
            for (std::size_t i = 0; i < s.size(); ++i) s.r[i] = r.l[i];
            run(*e, s);
            return db(toneAmp(s.r, 300, s.size() - 12000, 12000) / 0.02);
        };
        const double linked = run1(0), dual = run1(1);
        std::printf("  SLOW BURN quiet right while left is loud: LINKED %.1f dB, DUAL %.1f dB\n", linked, dual);
        CHECK(linked < -6.0 && std::fabs(dual) < 0.5);
        auto e = make("SLOW BURN", {{Threshold, -30}});
        CHECK(e->latency() == hc::fx::Oversampler4::latency());
    }
}

void testPunchers() {
    {   using namespace heavyweight;
        auto e = make("HEAVYWEIGHT", {{Threshold, -30}, {Ratio, 4}});
        const double out = toneOut(*e, 1000, 0.5f);
        // auto makeup gives back half of what a full-scale signal loses: 4:1 over -30 at 0 dBFS = -22.5, +11.25
        std::printf("  HEAVYWEIGHT 4:1 at -30: a -6 dBFS tone comes out %.1f dB (auto makeup)\n", out);
        CHECK(std::fabs(out - (-18.0 + 11.25)) < 2.0);
        // PUNCH lets the hits through: the attack peak stays higher with PUNCH 100
        double h0, t0, h1, t1;
        auto p0 = make("HEAVYWEIGHT", {{Threshold, -36}, {Ratio, 8}, {Attack, 0.5f}, {Punch, 0}});
        auto p1 = make("HEAVYWEIGHT", {{Threshold, -36}, {Ratio, 8}, {Attack, 0.5f}, {Punch, 100}});
        Stereo a = hits(3.0, 0.8f), b = a;
        run(*p0, a);
        run(*p1, b);
        hitAndTail(a, p0->latency(), h0, t0);
        hitAndTail(b, p1->latency(), h1, t1);
        std::printf("  HEAVYWEIGHT hits: peak PUNCH 0 %.1f dB, PUNCH 100 %.1f dB\n", h0, h1);
        CHECK(h1 > h0 + 3.0);
        // LIMIT: nothing over -0.3 dBFS
        auto lim = make("HEAVYWEIGHT", {{Limit, 1}, {Output, 18}});
        Stereo c = noise(1.0, 0.9f);
        run(*lim, c);
        CHECK(peakDb(c, 0) < -0.29);
        // release on the beat follows the tempo
        auto sy = make("HEAVYWEIGHT", {{Sync, 4}, {SyncSource, 1}, {Bpm, 60}});
        CHECK(sy->paramActive(Bpm) && !sy->paramActive(Release));
        CHECK(e->latency() == hc::fx::Oversampler4::latency());
    }
    {   using namespace ironfist;
        auto a = make("IRON FIST", {{Input, 0}, {Iron, 0}}), b = make("IRON FIST", {{Input, 20}, {Iron, 0}, {Output, -20}});
        const double qa = toneOut(*a, 1000, 0.1f), qb = toneOut(*b, 1000, 0.1f);
        std::printf("  IRON FIST -20 dBFS tone: INPUT 0 %.1f dB, INPUT +20 (output -20) %.1f dB\n", qa, qb);
        CHECK(std::fabs(qa) < 1.0 && qb < -6.0);
        auto lim = make("IRON FIST", {{Input, 25}, {Limit, 1}, {LimitLevel, -10}, {Iron, 0}});
        Stereo c = noise(1.0, 0.5f);
        run(*lim, c);
        std::printf("  IRON FIST limiter at -10: peaks %.1f dBFS\n", peakDb(c, 9600));
        CHECK(peakDb(c, 9600) < -7.5);
    }
    {   using namespace knockout;
        Stereo dry = hits(3.0, 0.5f);
        double hd, td, ha, ta, hs, ts;
        hitAndTail(dry, 0, hd, td);
        auto atk = make("KNOCKOUT", {{Attack, 100}});
        Stereo a = dry;
        run(*atk, a);
        hitAndTail(a, 0, ha, ta);
        auto sus = make("KNOCKOUT", {{Sustain, -100}});
        Stereo s = dry;
        run(*sus, s);
        hitAndTail(s, 0, hs, ts);
        std::printf("  KNOCKOUT hits: dry hit %.1f / tail %.1f dB; ATTACK +100 hit %.1f; SUSTAIN -100 tail %.1f dB\n", hd, td, ha, ts);
        CHECK(ha > hd + 3.0 && ts < td - 3.0);
        auto g = make("KNOCKOUT", {{Attack, 100}, {Guard, 2}, {Output, 18}});
        Stereo c = hits(2.0, 0.9f);
        run(*g, c);
        CHECK(peakDb(c, 0) < -0.09);
        auto neutral = make("KNOCKOUT");
        Stereo n = noise(0.5, 0.5f), ref = n;
        run(*neutral, n);
        CHECK(maxDelayedDiff(n, ref, 0, 0) < 1e-5);
        const double d = blockSizeDifference(*findPlugin("KNOCKOUT"), [](hc::fx::Effect& x) { x.set(Attack, 80); x.set(Mode, 1); x.set(Guard, 2); }, hits(1.0, 0.8f));
        CHECK(d < 1e-5);
    }
}

void testLoudness() {
    {   using namespace fullhouse;
        auto lift = make("FULL HOUSE", {{LowLift, 12}, {LowThreshold, -30}});
        const double q = toneOut(*lift, 500, 0.01f, 3.0);   // -40 dBFS: 10 dB under -30 -> +5 dB
        auto hold = make("FULL HOUSE", {{HighLevel, 100}});
        const double l = toneOut(*hold, 500, 0.9f, 2.0);
        auto gate = make("FULL HOUSE", {{Gate, -50}});
        const double g = toneOut(*gate, 500, 0.001f, 2.0);
        std::printf("  FULL HOUSE: -40 dBFS lifted %+.1f dB; loud held %+.1f dB; -60 dBFS under a -50 gate %+.1f dB\n", q, l, g);
        CHECK(q > 3.5 && q < 6.5 && l < -3.0 && g < -25.0);
    }
    {   using namespace wheelman;
        auto e = make("WHEELMAN", {{Target, -20}, {MaxBoost, 12}, {MaxCut, 12}});
        const double loud = toneOut(*e, 1000, 0.5f, 4.0), quiet = toneOut(*e, 1000, 0.03f, 4.0);
        // -6 dBFS -> wants -20: cut 12 (the most); -30 dBFS -> boost 10 (sine peaks read +3 over RMS)
        std::printf("  WHEELMAN target -20: a -6 dBFS tone %+.1f dB, a -30 dBFS tone %+.1f dB\n", loud, quiet);
        CHECK(loud < -10.0 && quiet > 6.0);
        auto still = make("WHEELMAN", {{Sensitivity, -40}});
        const double under = toneOut(*still, 1000, 0.003f, 2.0);   // -50 dBFS, under SENSITIVITY: left alone
        CHECK(std::fabs(under) < 0.2);
    }
}

void testDeEssAndBreath() {
    {   using namespace silencer;
        for (int det = 0; det < 2; ++det) {
            auto e = make("SILENCER", {{Detect, static_cast<float>(det)}, {Threshold, -40}, {Range, 12}});
            const double ess = toneOut(*e, 8000, 0.3f), body = toneOut(*e, 400, 0.3f);
            std::printf("  SILENCER %s: 8 kHz %.1f dB, 400 Hz %.1f dB\n", det ? "smart" : "classic", ess, body);
            CHECK(ess < -8.0 && std::fabs(body) < 0.5);
        }
        // SMART: a loud voice with only a little top isn't an ess (CLASSIC at the same threshold reacts to it)
        auto mixSig = [] {
            Stereo s = sine(300, 1.5, 0.6f);
            add(s, sine(7000, 1.5, 0.05f));
            return s;
        };
        auto cl = make("SILENCER", {{Detect, 0}, {Threshold, -40}, {Range, 12}}), sm = make("SILENCER", {{Detect, 1}, {Threshold, -40}, {Range, 12}});
        Stereo a = mixSig(), b = mixSig();
        run(*cl, a);
        run(*sm, b);
        const double ca = db(toneAmp(a.l, 7000, a.size() - 12000, 12000) / 0.05), sa = db(toneAmp(b.l, 7000, b.size() - 12000, 12000) / 0.05);
        std::printf("  SILENCER voice with a little top: classic takes %.1f dB, smart %.1f dB\n", ca, sa);
        CHECK(ca < -6.0 && sa > -2.0);
    }
    {   using namespace breathless;
        // a breath: soft noise in the middle highs; a word: a voiced tone with harmonics, at the same level
        Stereo breath = noise(1.5, 0.02f, 8);
        {
            hc::fx::Biquad hp, lp;
            hp.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, kFs, 1200, 0.7);
            lp.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::LowPass, kFs, 5000, 0.7);
            for (std::size_t i = 0; i < breath.size(); ++i) breath.l[i] = breath.r[i] = lp.tick(hp.tick(breath.l[i])) * 3.0f;
        }
        Stereo word(breath.size());
        for (std::size_t i = 0; i < word.size(); ++i) {
            const double t = static_cast<double>(i) / kFs;
            float v = 0;
            for (int k = 1; k <= 8; ++k) v += static_cast<float>(std::sin(2 * std::numbers::pi * 160 * k * t) / k);
            word.l[i] = word.r[i] = 0.012f * v;
        }
        auto e = make("BREATHLESS", {{Reduction, 20}});
        Stereo b = breath, w = word;
        run(*e, b);
        e->reset();
        run(*e, w);
        const double bd = db(rms(b.l, 24000, 36000) / rms(breath.l, 24000, 36000)), wd = db(rms(w.l, 24000, 36000) / rms(word.l, 24000, 36000));
        std::printf("  BREATHLESS: breath-like noise %.1f dB, voiced tone %.1f dB\n", bd, wd);
        CHECK(bd < -12.0 && std::fabs(wd) < 1.0);
    }
}

void testPump() {
    using namespace tommygun;
    auto off = make("TOMMY GUN");
    Stereo s = noise(0.5, 0.5f), ref = s;
    run(*off, s);
    CHECK(maxDelayedDiff(s, ref, 0, 0) < 1e-6);
    auto e = make("TOMMY GUN", {{Amount, 100}, {SyncSource, 1}, {Bpm, 120}, {Rate, 2}});   // a dip every 0.5 s
    Stereo c = sine(1000, 2.0, 0.5f);
    run(*e, c);
    const double dip = db(rms(c.l, 48000 + 240, 240) / (0.5 / std::sqrt(2.0))), up = db(rms(c.l, 48000 + 19200, 960) / (0.5 / std::sqrt(2.0)));
    std::printf("  TOMMY GUN amount 100, quarter notes at 120: on the beat %.1f dB, 0.4 s later %.1f dB\n", dip, up);
    CHECK(dip < -15.0 && up > -3.0);
}

void testEnforcerFolds() {
    using namespace vca;
    // WARM adds low-end 2nd harmonic while it works; CLEAN doesn't
    auto clean = make("ENFORCER", {{Threshold, -30}, {Ratio, 6}});
    auto warm = make("ENFORCER", {{Threshold, -30}, {Ratio, 6}, {Character, 1}});
    const double hc = harmonicDb(*clean, 100, 2, 0.5f), hw = harmonicDb(*warm, 100, 2, 0.5f);
    std::printf("  ENFORCER 2nd harmonic of 100 Hz: CLEAN %.1f dB, WARM %.1f dB\n", hc, hw);
    CHECK(hw > hc + 15.0);
    // LINGER: after a loud burst stops, the gain comes back more slowly near the end
    auto rec = [&](float tail) {
        auto e = make("ENFORCER", {{Threshold, -30}, {Ratio, 8}, {Release, 100}, {Tail, tail}});
        Stereo s = sine(1000, 1.0, 0.5f);
        Stereo q = sine(1000, 0.5, 0.01f);
        s.l.insert(s.l.end(), q.l.begin(), q.l.end());
        s.r.insert(s.r.end(), q.r.begin(), q.r.end());
        run(*e, s);
        return db(rms(s.l, 48000 + 19200, 2400) / (0.01 / std::sqrt(2.0)));   // 400..450 ms after the burst
    };
    const double steady = rec(0), linger = rec(1);
    std::printf("  ENFORCER 400 ms after a burst: STEADY %.1f dB, LINGER %.1f dB\n", steady, linger);
    CHECK(linger < steady - 0.5);
}

} // namespace

void testArsenal() {
    std::printf("[the dynamics arsenal]\n");
    allRules();
    testSwitchblade();
    testTerritory();
    testLevelers();
    testPunchers();
    testLoudness();
    testDeEssAndBreath();
    testPump();
    testEnforcerFolds();
}

} // namespace hem::test
