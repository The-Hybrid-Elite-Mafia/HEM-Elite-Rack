// The dynamics arsenal: what each plug-in must actually do to the sound, measured, plus the
// rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Arsenal.hpp"

#include <string>
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
    for (const char* name : {"SWITCHBLADE", "TERRITORY", "STRAIGHT EDGE", "CONSIGLIERE", "STONE COLD", "HEAVYWEIGHT", "THE DON", "SLOW BURN", "LONE WOLF",
                             "IRON FIST", "QUICK DRAW", "OLD MONEY", "KNOCKOUT", "HAYMAKER", "FOUR KINGS", "BACKBONE", "FULL HOUSE", "WHEELMAN",
                             "CHAUFFEUR", "SILENCER", "TIGHT LIPS", "OMERTA", "BREATHLESS", "TOMMY GUN"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

void testSwitchblade() {
    using namespace switchblade;
    const PluginInfo* info = findPlugin("SWITCHBLADE");
    CHECK(make("SWITCHBLADE")->latency() == kLookahead);
    {   // 4:1 over -30 dBFS: a -6 dBFS tone comes out 18 dB lower (24 over -> 6 over)
        auto e = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 4}});
        const double out = toneOut(*e, 1000, 0.5f);
        std::printf("  SWITCHBLADE 4:1 at -30: a -6 dBFS tone comes out %.1f dB\n", out);
        CHECK(std::fabs(out + 18.0) < 1.5);
        // LEVEL REF PEAK: a full-scale tone stays at full scale
        auto pk = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 4}, {LevelRef, 1}});
        const double full = toneOut(*pk, 1000, 0.99f);
        std::printf("  SWITCHBLADE PEAK reference: a 0 dBFS tone comes out %.1f dB\n", full);
        CHECK(std::fabs(full) < 1.5);
        // CANCEL: past the threshold, louder in = quieter out
        auto cn = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 5}, {Cancel, 1}});
        const double a = toneOut(*cn, 1000, 0.3f) + db(0.3), b = toneOut(*cn, 1000, 0.6f) + db(0.6);
        std::printf("  SWITCHBLADE CANCEL 5: -10 dBFS in -> %.1f dBFS out, -4 dBFS in -> %.1f dBFS out\n", a, b);
        CHECK(b < a - 0.5);
        // upward expansion under 1:1
        auto up = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 0.5f}});
        CHECK(toneOut(*up, 1000, 0.1f) > 6.0);
    }
    {   // the gate: open at -40, floor -40: a -60 dBFS tone drops ~40 dB; a -20 one passes
        auto e = make("SWITCHBLADE", {{CompOn, 0}, {GateOn, 1}, {GateOpen, -40}, {GateClose, -44}, {Floor, -40}});
        const double quiet = toneOut(*e, 1000, 0.001f, 2.0), loud = toneOut(*e, 1000, 0.1f, 2.0);
        std::printf("  SWITCHBLADE gate: -60 dBFS tone %.1f dB, -20 dBFS tone %.1f dB\n", quiet, loud);
        CHECK(quiet < -35.0 && std::fabs(loud) < 0.5);
        // EXPANDER 2:1 under -40: a -50 dBFS tone comes out 10 dB lower
        auto x = make("SWITCHBLADE", {{CompOn, 0}, {GateOn, 1}, {GateMode, 1}, {GateOpen, -40}, {ExpSlope, 2}});
        const double ex = toneOut(*x, 1000, 0.00316f, 2.0);
        std::printf("  SWITCHBLADE expander 2:1 under -40: a -50 dBFS tone %.1f dB\n", ex);
        CHECK(std::fabs(ex + 10.0) < 2.0);
    }
    {   // SIDECHAIN: with a LO CUT at 1 kHz the compressor no longer hears a loud 60 Hz tone; WIDE does
        auto wide = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 8}, {FilterType, 2}, {FilterFreq, 1000}, {CompEq, 0}});
        auto keyed = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 8}, {FilterType, 2}, {FilterFreq, 1000}, {CompEq, 1}});
        const double w = toneOut(*wide, 60, 0.5f, 2.0), k = toneOut(*keyed, 60, 0.5f, 2.0);
        std::printf("  SWITCHBLADE loud 60 Hz: WIDE %.1f dB, SIDECHAIN through a 1 kHz lo cut %.1f dB\n", w, k);
        CHECK(w < -10.0 && std::fabs(k) < 1.0);
        // SPLIT: only the band moves (a dynamic EQ): BAND at 5 kHz
        auto split = make("SWITCHBLADE", {{Threshold, -40}, {Ratio, 8}, {FilterType, 0}, {FilterFreq, 5000}, {FilterQ, 0.6f}, {CompEq, 2}});
        const double lo = toneOut(*split, 300, 0.3f, 2.0), hi = toneOut(*split, 5000, 0.3f, 2.0);
        std::printf("  SWITCHBLADE SPLIT on a 5 kHz band: 300 Hz %.1f dB, 5 kHz %.1f dB\n", lo, hi);
        CHECK(std::fabs(lo) < 1.0 && hi < -8.0);
    }
    {   // MONITOR SIDECHAIN + PASSIVE add up to the sound
        Stereo in = noise(0.5, 0.3f, 4);
        Stereo a = in, b = in;
        auto sc = make("SWITCHBLADE", {{Monitor, 1}, {FilterType, 0}}), pa = make("SWITCHBLADE", {{Monitor, 2}, {FilterType, 0}});
        run(*sc, a);
        run(*pa, b);
        double worst = 0;
        for (std::size_t i = kLookahead; i < in.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(a.l[i] + b.l[i] - in.l[i - kLookahead])));
        CHECK(worst < 1e-5);
        // KEY L > R: the left side's level works the right side; the left passes
        auto key = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 8}, {Key, 1}});
        Stereo s = sine(500, 2.0, 0.5f);
        Stereo q = sine(300, 2.0, 0.05f);
        for (std::size_t i = 0; i < s.size(); ++i) s.r[i] = q.l[i];
        run(*key, s);
        const double left = db(toneAmp(s.l, 500, s.size() - 12000, 12000) / 0.5), right = db(toneAmp(s.r, 300, s.size() - 12000, 12000) / 0.05);
        std::printf("  SWITCHBLADE KEY L > R: left %.1f dB, quiet right %.1f dB\n", left, right);
        CHECK(std::fabs(left) < 0.5 && right < -10.0);
        auto in2 = make("SWITCHBLADE", {{CompOn, 0}, {InputL, -12}});
        CHECK(std::fabs(toneOut(*in2, 1000, 0.1f) + 12.0) < 0.3);
    }
    {   // PDR: after a short burst the release is PDR, not RELEASE
        auto rec = [](float pdr) {
            auto e = make("SWITCHBLADE", {{Threshold, -30}, {Ratio, 8}, {Release, 2000}, {Pdr, pdr}, {Attack, 0.1f}});
            Stereo s = sine(1000, 0.05, 0.8f);
            Stereo q = sine(1000, 1.0, 0.01f);
            s.l.insert(s.l.end(), q.l.begin(), q.l.end());
            s.r.insert(s.r.end(), q.r.begin(), q.r.end());
            run(*e, s);
            return db(toneAmp(s.l, 1000, 2400 + kLookahead + 14400, 9600) / 0.01);   // 300..500 ms after the burst
        };
        const double slow = rec(0), quick = rec(200);
        std::printf("  SWITCHBLADE 300-500 ms after a 50 ms burst: release 2 s %.1f dB, PDR 200 ms %.1f dB\n", slow, quick);
        CHECK(quick > slow + 6.0);
    }
    const double d = blockSizeDifference(*info, [](hc::fx::Effect& x) { x.set(Threshold, -30); x.set(GateOn, 1); x.set(GateOpen, -30); x.set(CompEq, 2); },
                                         noise(1.0, 0.5f, 3));
    CHECK(d < 1e-5);
}

void testTerritory() {
    for (const char* name : {"TERRITORY", "STRAIGHT EDGE"}) {
        const bool lin = std::string(name) == "STRAIGHT EDGE";
        using namespace territory;   // the band layout and the master block are the same in both
        const int mRange = lin ? straightedge::MRange : MRange, mGain = lin ? straightedge::MGain : MGain;
        const std::uint32_t lat = lin ? straightedge::kLatency : 0;
        // MASTER RANGE +6 cancels the bands' -6: nothing moves, the bands add back up to the sound
        auto e = make(name, {{mRange, 6}});
        if (lin) { e->set(straightedge::Dither, 0); e->reset(); }
        CHECK(e->latency() == lat);
        Stereo s = noise(1.0, 0.3f, 9), ref = s;
        run(*e, s);
        const double dd = maxDelayedDiff(s, ref, lat, lat);
        std::printf("  %s neutral: output = input %u samples late, within %.1e\n", name, lat, dd);
        CHECK(dd < 2e-5);
        // band 2 RANGE -12 under a loud 300 Hz tone; the 3 kHz tone (band 3, RANGE 0) stays
        auto two = make(name, {{mRange, 6}, {band(1, Range), -18}, {band(1, Threshold), -40}});
        const double b2 = toneOut(*two, 300, 0.3f, 2.0), b3 = toneOut(*two, 3000, 0.3f, 2.0);
        std::printf("  %s band 2 range -12: 300 Hz %.1f dB, 3 kHz %.1f dB\n", name, b2, b3);
        CHECK(std::fabs(b2 + 12.0) < (lin ? 1.5 : 3.0) && std::fabs(b3) < 0.6);   // TERRITORY's shelves reach -12 by the band's middle more gently
        // a plus RANGE lifts
        auto up = make(name, {{mRange, 6}, {band(2, Range), 0}, {band(1, Range), 6}, {band(1, Threshold), -40}});
        const double u = toneOut(*up, 300, 0.3f, 2.0);
        std::printf("  %s band 2 range +12: 300 Hz %+.1f dB\n", name, u);
        CHECK(std::fabs(u - 12.0) < (lin ? 1.5 : 3.0));
        // SOLO band 2; BYPASS band 2
        auto solo = make(name, {{mRange, 6}, {band(1, Solo), 1}});
        const double s300 = toneOut(*solo, 300, 0.1f), s5k = toneOut(*solo, 5000, 0.1f);
        std::printf("  %s solo band 2: 300 Hz %.1f dB, 5 kHz %.1f dB\n", name, s300, s5k);
        CHECK(std::fabs(s300) < 1.0 && s5k < -20.0);
        auto byp = make(name, {{band(1, Range), -24}, {band(1, Threshold), -60}, {band(1, Bypass), 1}});
        CHECK(std::fabs(toneOut(*byp, 300, 0.3f, 2.0)) < 0.6);
        // MASTER GAIN moves every band
        auto mg = make(name, {{mRange, 6}, {mGain, 6}});
        CHECK(std::fabs(toneOut(*mg, 300, 0.05f) - 6.0) < 0.5 && std::fabs(toneOut(*mg, 5000, 0.05f) - 6.0) < 0.5);
        const double bd = blockSizeDifference(*findPlugin(name), [](hc::fx::Effect& x) { x.set(band(1, Threshold), -40); x.set(band(2, Range), 6); },
                                              noise(1.0, 0.5f, 3));
        CHECK(bd < 1e-5);
    }
    {   using namespace straightedge;
        // AUTO MAKEUP: half the RANGE back (-6 range: +3 under the threshold)
        auto mk = make("STRAIGHT EDGE", {{Makeup, 1}, {Dither, 0}});
        const double m = toneOut(*mk, 1000, 0.001f, 1.5);
        std::printf("  STRAIGHT EDGE auto makeup, quiet tone: %+.1f dB\n", m);
        CHECK(std::fabs(m - 3.0) < 0.5);
        // ADAPTIVE: a loud band under it keeps band 3 from working
        auto adaptive = [](float a) {
            auto e = make("STRAIGHT EDGE", {{band(2, Range), -12}, {band(2, Threshold), -40}, {Adaptive, a}, {Dither, 0}});
            Stereo s = sine(300, 2.0, 0.5f);
            add(s, sine(1500, 2.0, 0.1f));
            run(*e, s);
            return db(toneAmp(s.l, 1500, s.size() - 24000, 24000) / 0.1);
        };
        const double off = adaptive(-40), on = adaptive(0);
        std::printf("  STRAIGHT EDGE 1.5 kHz under a loud 300 Hz: ADAPTIVE off %.1f dB, 0 dB %.1f dB\n", off, on);
        CHECK(on > off + 3.0);
        // TRIM: one push brings the loudest peak since the last push to 0 dBFS
        auto t = make("STRAIGHT EDGE", {{MRange, 6}, {Dither, 0}});
        Stereo a = noise(1.0, 0.25f, 2);
        run(*t, a);
        t->set(Trim, 1);
        Stereo b = noise(1.0, 0.25f, 2);
        run(*t, b);
        std::printf("  STRAIGHT EDGE after TRIM: peak %.2f dBFS (was %.2f)\n", peakDb(b, 24000), peakDb(a, 0));
        CHECK(std::fabs(peakDb(b, 24000)) < 1.0);
    }
    {   using namespace territory;
        // Q: a steeper split (0.75) keeps a 300 Hz tone out of band 1's move more than a soft one (0.1)
        auto q = [](float qq) {
            auto e = make("TERRITORY", {{MRange, 6}, {band(0, Range), -18}, {band(0, Threshold), -60}, {Q, qq}});
            return toneOut(*e, 300, 0.3f, 2.0);
        };
        const double soft = q(0.1f), steep = q(0.75f);
        std::printf("  TERRITORY band 1 down 12 dB, a 300 Hz tone: Q 0.1 %.1f dB, Q 0.75 %.1f dB\n", soft, steep);
        CHECK(steep > soft + 1.0);
    }
}

void testLevelers() {
    {   using namespace opto;
        for (const char* name : {"CONSIGLIERE", "STONE COLD"}) {
            const float k = std::string(name) == "STONE COLD" ? 0.1f : 1.0f;   // its dials go 0..10
            auto light = make(name, {{PeakReduction, 30 * k}}), hard = make(name, {{PeakReduction, 70 * k}});
            auto lim = make(name, {{PeakReduction, 70 * k}, {Mode, 1}});
            const double a = toneOut(*light, 1000, 0.3f, 3.0), b = toneOut(*hard, 1000, 0.3f, 3.0), c = toneOut(*lim, 1000, 0.3f, 3.0);
            std::printf("  %s -10 dBFS tone: PR 30 %%  %.1f dB, PR 70 %% %.1f dB, PR 70 %% LIMIT %.1f dB\n", name, a, b, c);
            CHECK(b < a - 3.0 && c < b - 2.0);
            // AUTO MAKEUP gives back what it takes, on average
            auto mk = make(name, {{PeakReduction, 70 * k}, {AutoMakeup, 1}});
            const double m = toneOut(*mk, 1000, 0.3f, 6.0);
            std::printf("  %s PR 70 %% with AUTO MAKEUP: %.1f dB\n", name, m);
            CHECK(std::fabs(m) < 1.5);
            // GAIN: the dial's full turn is +40 dB
            auto g = make(name, {{Gain, 25 * k}, {PeakReduction, 0}});
            CHECK(std::fabs(toneOut(*g, 1000, 0.01f) - 10.0) < 0.3);
            auto dry = make(name, {{PeakReduction, 90 * k}, {Mix, 0}});
            Stereo s = noise(0.5, 0.5f), ref = s;
            run(*dry, s);
            CHECK(maxDelayedDiff(s, ref, 0, 0) < 1e-6);
        }
        // the meters: a -18 dBFS RMS tone reads 0 VU
        auto e = make("CONSIGLIERE", {{PeakReduction, 0}});
        Stereo s = sine(1000, 1.0, 0.1259f * 1.4142f);
        run(*e, s);
        const float vu = e->meter(0);
        std::printf("  CONSIGLIERE: a -18 dBFS RMS tone reads %+.2f VU\n", vu);
        CHECK(std::fabs(vu) < 0.3f);
        // ANALOG: hum under the music, nothing on silence
        auto hum = make("CONSIGLIERE", {{PeakReduction, 0}, {Analog, 2}});
        Stereo t = sine(1000, 2.0, 0.3f);
        run(*hum, t);
        const double h60 = db(toneAmp(t.l, 60, 48000, 48000) / 0.3);
        std::printf("  CONSIGLIERE ANALOG 60 HZ: hum %.1f dB under the music\n", h60);
        CHECK(h60 < -55.0 && h60 > -80.0);
    }
    {   using namespace thedon;
        auto gentle = make("THE DON", {{Threshold, 6}, {Ratio, 1}}), hard = make("THE DON", {{Threshold, 6}, {Ratio, 4}});
        auto none = make("THE DON", {{Threshold, 6}, {Ratio, 0}});
        const double a = toneOut(*gentle, 500, 0.5f, 3.0), b = toneOut(*hard, 500, 0.5f, 3.0), c = toneOut(*none, 500, 0.5f, 3.0);
        // threshold -12 on the dial = -30 dBFS (VU CALIBRATE 18)
        std::printf("  THE DON -6 dBFS tone, threshold -12: 1:1 %.1f dB, 2:1 %.1f dB, LIMIT %.1f dB\n", c, a, b);
        CHECK(std::fabs(c) < 0.6 && a < -4.0 && b < a - 6.0);
        auto cal = make("THE DON", {{Threshold, 6}, {Ratio, 2}, {VuCal, 8}});
        CHECK(toneOut(*cal, 500, 0.5f, 3.0) > toneOut(*make("THE DON", {{Threshold, 6}, {Ratio, 2}}), 500, 0.5f, 3.0) + 3.0);
    }
    {   using namespace slowburn;
        // SEPARATE: a tone only on the left leaves the right alone; LINKED pulls both
        auto run1 = [&](float link) {
            auto e = make("SLOW BURN", {{Threshold, 7.5f}, {Link, link}});
            Stereo s = sine(500, 3.0, 0.5f);
            Stereo r = sine(300, 3.0, 0.02f);
            for (std::size_t i = 0; i < s.size(); ++i) s.r[i] = r.l[i];
            run(*e, s);
            return db(toneAmp(s.r, 300, s.size() - 12000, 12000) / 0.02);
        };
        const double linked = run1(1), separate = run1(0);
        std::printf("  SLOW BURN quiet right while left is loud: LINKED %.1f dB, SEPARATE %.1f dB\n", linked, separate);
        CHECK(linked < -6.0 && std::fabs(separate) < 0.5);
        auto unity = make("SLOW BURN");   // INPUT -14 = unity, THRESHOLD 0 = no compression
        CHECK(std::fabs(toneOut(*unity, 500, 0.2f)) < 0.5);
        auto off = make("SLOW BURN", {{Threshold, 9}, {Compress, 0}});
        CHECK(std::fabs(toneOut(*off, 500, 0.2f)) < 0.5);
        CHECK(unity->latency() == hc::fx::Oversampler4::latency());
        using namespace lonewolf;
        auto wolf = make("LONE WOLF", {{lonewolf::Threshold, 7.5f}});
        const double w = toneOut(*wolf, 500, 0.5f, 3.0);
        std::printf("  LONE WOLF threshold 7.5, -6 dBFS tone: %.1f dB\n", w);
        CHECK(w < -6.0);
    }
    {   using namespace oldmoney;
        auto c = make("OLD MONEY", {{Threshold, -30}, {Ratio, 4}});
        const double out = toneOut(*c, 1000, 0.5f, 2.0);
        std::printf("  OLD MONEY 4:1 at -30: a -6 dBFS tone comes out %.1f dB\n", out);
        CHECK(std::fabs(out + 18.0) < 2.0);
        auto up = make("OLD MONEY", {{Threshold, -30}, {Ratio, 0.5f}, {Output, -12}});
        const double u = toneOut(*up, 1000, 0.1f, 2.0);   // -20 dBFS, 10 over: lifted 10 dB, then -12
        std::printf("  OLD MONEY ratio 0.5 (lift) at -30, -20 dBFS tone, output -12: %.1f dB\n", u);
        CHECK(std::fabs(u - (10.0 - 12.0)) < 2.0);
        auto lim = make("OLD MONEY", {{Output, 30}});
        Stereo n = noise(1.0, 0.5f);
        run(*lim, n);
        CHECK(peakDb(n, 0) <= 0.0);
        // ELECTRO lets go faster than OPTO near the end of the release
        auto rel = [](float behavior) {
            auto e = make("OLD MONEY", {{Threshold, -30}, {Ratio, 4}, {ReleaseMode, 1}, {Release, 300}, {Behavior, behavior}});
            Stereo s = sine(1000, 0.5, 0.5f);
            Stereo q = sine(1000, 1.0, 0.02f);
            s.l.insert(s.l.end(), q.l.begin(), q.l.end());
            s.r.insert(s.r.end(), q.r.begin(), q.r.end());
            run(*e, s);
            return db(toneAmp(s.l, 1000, 24000 + 19200, 9600) / 0.02);
        };
        const double el = rel(0), op = rel(1);
        std::printf("  OLD MONEY 400-600 ms after a loud part: ELECTRO %.1f dB, OPTO %.1f dB\n", el, op);
        CHECK(el > op + 1.0);
    }
}

void testPunchers() {
    {   using namespace heavyweight;
        auto e = make("HEAVYWEIGHT", {{Threshold, -30}, {Ratio, 4}, {Analog, 0}});
        const double out = toneOut(*e, 1000, 0.5f);
        // auto makeup gives back half of what a full-scale signal loses: 4:1 over -30 at 0 dBFS = -22.5, +11.25
        std::printf("  HEAVYWEIGHT 4:1 at -30: a -6 dBFS tone comes out %.1f dB (auto makeup)\n", out);
        CHECK(std::fabs(out - (-18.0 + 11.25)) < 2.0);
        // PUNCH lets the hits through: the attack peak stays higher with PUNCH 30
        double h0, t0, h1, t1;
        auto p0 = make("HEAVYWEIGHT", {{Threshold, -36}, {Ratio, 8}, {Attack, 0.5f}, {Punch, 0}, {Analog, 0}});
        auto p1 = make("HEAVYWEIGHT", {{Threshold, -36}, {Ratio, 8}, {Attack, 0.5f}, {Punch, 30}, {Analog, 0}});
        Stereo a = hits(3.0, 0.8f), b = a;
        run(*p0, a);
        run(*p1, b);
        hitAndTail(a, p0->latency(), h0, t0);
        hitAndTail(b, p1->latency(), h1, t1);
        std::printf("  HEAVYWEIGHT hits: peak PUNCH 0 %.1f dB, PUNCH 30 %.1f dB\n", h0, h1);
        CHECK(h1 > h0 + 3.0);
        // LIMITER on: nothing over -0.3 dBFS; off: the clipper holds 0 dBFS
        auto lim = make("HEAVYWEIGHT", {{Limiter, 1}, {Output, 18}});
        Stereo c = noise(1.0, 0.9f);
        run(*lim, c);
        CHECK(peakDb(c, 0) < -0.29);
        auto clip = make("HEAVYWEIGHT", {{Limiter, 0}, {Output, 18}});
        Stereo d = noise(1.0, 0.9f);
        run(*clip, d);
        CHECK(peakDb(d, 0) <= 0.0);
        // the ANALOG flavours colour the sound differently (3rd harmonic of a hot tone)
        double h3[5];
        for (int f = 0; f < 5; ++f) {
            auto x = make("HEAVYWEIGHT", {{Threshold, 0}, {Analog, static_cast<float>(f)}});
            h3[f] = harmonicDb(*x, 200, 3, 0.7f);
        }
        std::printf("  HEAVYWEIGHT 3rd harmonic at -3 dBFS: OFF %.0f, 1 %.0f, 2 %.0f, 3 %.0f, 4 %.0f dB\n", h3[0], h3[1], h3[2], h3[3], h3[4]);
        CHECK(h3[0] < -80.0 && h3[2] > h3[1] && h3[4] > h3[2]);
        // release on the beat: BPM active only in BPM mode, the NOTE when synced
        auto sy = make("HEAVYWEIGHT", {{Sync, 2}, {Bpm, 60}});
        CHECK(sy->paramActive(Bpm) && sy->paramActive(Note) && !sy->paramActive(Release));
        CHECK(e->latency() == hc::fx::Oversampler4::latency());
    }
    {   using namespace ironfist;
        auto a = make("IRON FIST", {{Input, 0}, {Analog, 0}}), b = make("IRON FIST", {{Input, 18}, {Analog, 0}, {Output, -18}});
        const double qa = toneOut(*a, 1000, 0.1f), qb = toneOut(*b, 1000, 0.1f);
        std::printf("  IRON FIST -20 dBFS tone: INPUT 0 %.1f dB, INPUT +18 (output -18) %.1f dB\n", qa, qb);
        CHECK(std::fabs(qa) < 1.5 && qb < -6.0);
        auto am = make("IRON FIST", {{Input, 18}, {Analog, 0}, {AutoMakeup, 1}});
        const double qm = toneOut(*am, 1000, 0.1f);
        std::printf("  IRON FIST INPUT +18 with AUTO MAKEUP: %.1f dB\n", qm);
        CHECK(qm < qa && qm > qb + 3.0);
        auto lim = make("IRON FIST", {{Input, 18}, {Compress, 0}, {Limiter, 1}, {LimitLevel, 8}, {LimitAttack, 1}, {Analog, 0}});
        Stereo c = noise(1.0, 0.5f);
        run(*lim, c);
        std::printf("  IRON FIST limiter at +8 dBu (-14 dBFS): peaks %.1f dBFS\n", peakDb(c, 9600));
        CHECK(peakDb(c, 9600) < -11.5);
        // DE-ESS: the compressor only hears the highs
        auto de = make("IRON FIST", {{Input, 18}, {DeEss, 1}, {Analog, 0}, {Output, -18}});
        const double lo = toneOut(*de, 300, 0.1f), hi = toneOut(*de, 8000, 0.1f);
        std::printf("  IRON FIST DE-ESS (input +18, output -18): 300 Hz %.1f dB, 8 kHz %.1f dB\n", lo, hi);
        CHECK(lo > hi + 6.0);
    }
    {   using namespace quickdraw;
        auto unity = make("QUICK DRAW", {{Compress, 0}});
        const double u = toneOut(*unity, 1000, 0.05f);
        auto hard = make("QUICK DRAW", {{Input, 0}, {Output, -48}});
        const double h = toneOut(*hard, 1000, 0.05f, 2.0);
        std::printf("  QUICK DRAW -26 dBFS tone: COMPRESS OFF %.1f dB; INPUT 0 / OUTPUT -48 %.1f dB (30 dB in, 30 out)\n", u, h);
        CHECK(std::fabs(u) < 0.5 && h < -6.0);
        double r4, r20;
        auto a4 = make("QUICK DRAW", {{Input, -10}, {Ratio, 0}}), a20 = make("QUICK DRAW", {{Input, -10}, {Ratio, 3}});
        r4 = toneOut(*a4, 1000, 0.2f, 2.0);
        r20 = toneOut(*a20, 1000, 0.2f, 2.0);
        std::printf("  QUICK DRAW input -10, -14 dBFS tone: 4:1 %.1f dB, 20:1 %.1f dB\n", r4, r20);
        CHECK(r20 < r4 - 1.0);
        // faster ATTACK (7) catches the start of a hit more than slow (1)
        double hs, ts, hf, tf;
        auto slow = make("QUICK DRAW", {{Input, 0}, {Attack, 1}, {Mix, 100}}), fast = make("QUICK DRAW", {{Input, 0}, {Attack, 7}});
        Stereo x = hits(2.0, 0.5f), y = x;
        run(*slow, x);
        run(*fast, y);
        hitAndTail(x, slow->latency(), hs, ts);
        hitAndTail(y, fast->latency(), hf, tf);
        std::printf("  QUICK DRAW hit peaks: attack 1 %.1f dB, attack 7 %.1f dB\n", hs, hf);
        CHECK(hf < hs);
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
        const double d = blockSizeDifference(*findPlugin("KNOCKOUT"), [](hc::fx::Effect& x) { x.set(Attack, 80); x.set(Guard, 2); }, hits(1.0, 0.8f));
        CHECK(d < 1e-5);
    }
    {   // HAYMAKER: plus RANGE makes the hits stand out, minus softens them; the body stays
        using namespace haymaker;
        Stereo dry = hits(3.0, 0.5f);
        double hd, td, hu, tu, hdn, tdn;
        hitAndTail(dry, 0, hd, td);
        auto up = make("HAYMAKER", {{Range, 12}}), down = make("HAYMAKER", {{Range, -12}});
        Stereo a = dry, b = dry;
        run(*up, a);
        run(*down, b);
        hitAndTail(a, 0, hu, tu);
        hitAndTail(b, 0, hdn, tdn);
        std::printf("  HAYMAKER hits: dry %.1f dB; range +12 %.1f; range -12 %.1f (tails %.1f / %.1f / %.1f)\n", hd, hu, hdn, td, tu, tdn);
        CHECK(hu > hd + 3.0 && hdn < hd - 3.0 && std::fabs(tu - td) < 1.5);
        auto flat = make("HAYMAKER", {{Range, 6}});
        CHECK(std::fabs(toneOut(*flat, 1000, 0.3f)) < 0.2);   // a steady tone has no transients
        // TRIM: one push brings the loudest peak to 0 dBFS
        auto t = make("HAYMAKER", {{Range, 0}});
        Stereo x = noise(1.0, 0.25f, 2);
        run(*t, x);
        t->set(Trim, 1);
        Stereo y = noise(1.0, 0.25f, 2);
        run(*t, y);
        CHECK(std::fabs(peakDb(y, 0)) < 0.5);
    }
    {   // FOUR KINGS: only the band that's switched on and has RANGE moves
        using namespace fourkings;
        auto low = make("FOUR KINGS", {{band(0, Range), 12}, {band(1, On), 0}, {band(2, On), 0}, {band(3, On), 0}});
        auto none = make("FOUR KINGS", {{band(0, On), 0}, {band(1, On), 0}, {band(2, On), 0}, {band(3, On), 0}});
        Stereo dry = hits(3.0, 0.5f), a = dry, b = dry;
        run(*low, a);
        run(*none, b);
        double ha, ta, hb, tb;
        hitAndTail(a, 0, ha, ta);
        hitAndTail(b, 0, hb, tb);
        std::printf("  FOUR KINGS 120 Hz hits: band 1 range +12 %.1f dB, all off %.1f dB\n", ha, hb);
        CHECK(ha > hb + 3.0);
        // all off: the bands add back up to an all-pass (same level)
        Stereo n = noise(1.0, 0.3f, 6), r = n;
        run(*none, n);
        CHECK(std::fabs(db(rms(n.l, 0, 48000)) - db(rms(r.l, 0, 48000))) < 0.1);
    }
}

void testLoudness() {
    {   using namespace fullhouse;
        auto lift = make("FULL HOUSE", {{LowOn, 1}, {LowGain, 12}, {LowThreshold, -30}, {HighOn, 0}});
        const double q = toneOut(*lift, 500, 0.01f, 3.0);   // -40 dBFS: 10 dB under -30 -> +5 dB (sine peaks: a little less)
        auto hold = make("FULL HOUSE", {{LowOn, 0}, {HighOn, 1}, {HighThreshold, -24}, {HighGain, -6}});
        Stereo n = noise(2.0, 0.9f);
        run(*hold, n);
        auto gate = make("FULL HOUSE", {{GateOn, 1}, {Gate, -50}, {LowOn, 0}, {HighOn, 0}});
        const double g = toneOut(*gate, 500, 0.001f, 2.0);
        auto lev = make("FULL HOUSE", {{LevelerOn, 1}, {Leveler, -20}, {LowOn, 0}, {HighOn, 0}});
        const double l = toneOut(*lev, 500, 0.5f, 8.0);
        std::printf("  FULL HOUSE: -40 dBFS lifted %+.1f dB; high gain -6 peaks %.2f dBFS; -60 dBFS under a -50 gate %+.1f dB; -9 dBFS RMS levelled to -20: %+.1f dB\n",
                    q, peakDb(n, 0), g, l);
        CHECK(q > 3.0 && q < 6.5 && peakDb(n, 0) <= -5.99 && g < -8.0 && l < -8.0);
    }
    {   using namespace backbone;
        auto lift = make("BACKBONE", {{LowLevel, 24}});
        const double q = toneOut(*lift, 500, 0.01f, 3.0);   // -40 dBFS, threshold -36: +2 dB; quieter: more
        const double qq = toneOut(*lift, 500, 0.001f, 3.0); // -60 dBFS: 24 under -> +12
        auto hold = make("BACKBONE", {{HighLevel, -24}});
        const double h = toneOut(*hold, 500, 0.1f, 2.0) + db(0.1);   // -20 dBFS over -24, made up so 0 dBFS stays
        std::printf("  BACKBONE: low level 24 lifts -40 dBFS %+.1f dB, -60 dBFS %+.1f dB; high level -24: -20 dBFS comes out %.1f dBFS\n", q, qq, h);
        CHECK(q > 0.5 && qq > 9.0 && qq < 13.0 && h > -8.0);
    }
    {   using namespace wheelman;
        auto e = make("WHEELMAN", {{Target, -20}, {RangeMin, -12}, {RangeMax, 12}});
        const double loud = toneOut(*e, 1000, 0.5f, 4.0), quiet = toneOut(*e, 1000, 0.03f, 4.0);
        // -9 dBFS RMS -> wants -20: cut 11; -33 dBFS RMS -> boost 12 (the most)
        std::printf("  WHEELMAN target -20: a -6 dBFS tone %+.1f dB, a -30 dBFS tone %+.1f dB\n", loud, quiet);
        CHECK(loud < -9.0 && quiet > 9.0);
        auto idle = make("WHEELMAN", {{Idle, -6}, {Sensitivity, -12}});
        const double under = toneOut(*idle, 1000, 0.003f, 3.0);   // too quiet to count as a voice: the wheel sits at IDLE
        std::printf("  WHEELMAN idle -6, no voice: %+.1f dB\n", under);
        CHECK(std::fabs(under + 6.0) < 0.5);
        auto live = make("WHEELMAN", {{Live, 1}, {Spill, -20}, {Idle, 0}});
        CHECK(std::fabs(toneOut(*live, 1000, 0.03f, 3.0)) < 0.5);   // -33 dBFS RMS is under SPILL: left alone
    }
    {   using namespace chauffeur;
        auto e = make("CHAUFFEUR", {{Target, -20}, {RangeMin, -12}, {RangeMax, 12}});
        const double loud = toneOut(*e, 80, 0.5f, 4.0), quiet = toneOut(*e, 80, 0.05f, 4.0);
        std::printf("  CHAUFFEUR target -20: an 80 Hz note at -6 dBFS %+.1f dB, at -26 dBFS %+.1f dB\n", loud, quiet);
        CHECK(loud < -8.0 && quiet > 4.0);
        // a cymbal-like high sound isn't a bass note: the wheel stays at IDLE
        auto hi = make("CHAUFFEUR", {{Target, -20}, {Idle, 0}});
        const double h = toneOut(*hi, 6000, 0.05f, 3.0);
        std::printf("  CHAUFFEUR a 6 kHz tone: %+.1f dB (not a note)\n", h);
        CHECK(std::fabs(h) < 0.5);
    }
}

void testDeEssAndBreath() {
    {   using namespace silencer;
        auto e = make("SILENCER", {{Threshold, -40}});
        const double ess = toneOut(*e, 8000, 0.3f), body = toneOut(*e, 400, 0.3f);
        std::printf("  SILENCER: 8 kHz %.1f dB, 400 Hz %.1f dB\n", ess, body);
        CHECK(ess < -8.0 && std::fabs(body) < 0.5);
        auto wide = make("SILENCER", {{Threshold, -40}, {Audio, 1}});
        Stereo s = sine(400, 1.5, 0.3f);
        add(s, sine(8000, 1.5, 0.3f));
        run(*wide, s);
        CHECK(db(toneAmp(s.l, 400, s.size() - 12000, 12000) / 0.3) < -3.0);   // WIDE: the body comes down with the ess
    }
    {   using namespace tightlips;
        auto e = make("TIGHT LIPS", {{Threshold, -50}, {Range, -8}});
        const double ess = toneOut(*e, 8000, 0.3f), body = toneOut(*e, 400, 0.3f);
        std::printf("  TIGHT LIPS range -8: 8 kHz %.1f dB, 400 Hz %.1f dB\n", ess, body);
        CHECK(std::fabs(ess + 8.0) < 2.5 && std::fabs(body) < 0.5);   // SPLIT: the highs band runs a little over the cut
    }
    {   using namespace omerta;
        // a loud voice with only a little top isn't an ess; a pure ess is
        auto e = make("OMERTA", {{Threshold, -50}, {Range, -12}});
        Stereo a = sine(300, 1.5, 0.6f);
        add(a, sine(7000, 1.5, 0.05f));
        run(*e, a);
        const double voice = db(toneAmp(a.l, 7000, a.size() - 12000, 12000) / 0.05);
        auto f = make("OMERTA", {{Threshold, -50}, {Range, -12}, {Mode, 100}});
        const double ess = toneOut(*f, 7000, 0.1f), body = toneOut(*f, 300, 0.1f);
        std::printf("  OMERTA: a voice's little top %.1f dB; a pure ess %.1f dB; a low tone %.1f dB\n", voice, ess, body);
        CHECK(voice > -2.0 && ess < -8.0 && std::fabs(body) < 0.5);
        CHECK(e->latency() == kLookahead);
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
        auto e = make("BREATHLESS", {{Reduction, -20}});
        Stereo b = breath, w = word;
        run(*e, b);
        e->reset();
        run(*e, w);
        const double bd = db(rms(b.l, 24000, 36000) / rms(breath.l, 24000, 36000)), wd = db(rms(w.l, 24000, 36000) / rms(word.l, 24000, 36000));
        std::printf("  BREATHLESS: breath-like noise %.1f dB, voiced tone %.1f dB\n", bd, wd);
        CHECK(bd < -12.0 && std::fabs(wd) < 1.0);
        // ROOM TONE: the breath only comes down as far as the room (here: the breath itself is the quietest there is)
        auto room = make("BREATHLESS", {{Reduction, -80}, {RoomTone, 1}});
        Stereo c = breath, longer = breath;
        for (int k = 0; k < 2; ++k) { c.l.insert(c.l.end(), breath.l.begin(), breath.l.end()); c.r.insert(c.r.end(), breath.r.begin(), breath.r.end()); }
        longer = c;
        run(*room, c);
        const double rd = db(rms(c.l, 144000, 48000) / rms(longer.l, 144000, 48000));
        std::printf("  BREATHLESS reduction all, ROOM TONE on: breath %.1f dB\n", rd);
        CHECK(rd > -12.0);
    }
}

void testPump() {
    using namespace tommygun;
    auto off = make("TOMMY GUN");
    Stereo s = noise(0.5, 0.5f), ref = s;
    run(*off, s);
    CHECK(maxDelayedDiff(s, ref, 0, 0) < 1e-6);
    auto e = make("TOMMY GUN", {{Amount, 10}, {SyncSource, 1}, {Bpm, 120}, {Rate, 2}});   // a dip every 0.5 s
    Stereo c = sine(1000, 2.0, 0.5f);
    run(*e, c);
    const double dip = db(rms(c.l, 48000 + 240, 240) / (0.5 / std::sqrt(2.0))), up = db(rms(c.l, 48000 + 19200, 960) / (0.5 / std::sqrt(2.0)));
    std::printf("  TOMMY GUN amount 10, quarter notes at 120: on the beat %.1f dB, 0.4 s later %.1f dB\n", dip, up);
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
