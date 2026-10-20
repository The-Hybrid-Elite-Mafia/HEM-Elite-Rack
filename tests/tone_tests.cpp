// The tone shop (EQ, channel strips, tone, stereo): what each plug-in must actually do to the
// sound, measured, plus the rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/rack/ToneShop.hpp"
#include "hem/rack/Tools.hpp"

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
// Level change (dB) of a steady tone, measured on the left (and on the right when `right`).
double toneGain(hc::fx::Effect& e, double hz, float amp, bool right = false, double seconds = 1.5, bool antiPhase = false) {
    e.reset();
    Stereo s = sine(hz, seconds, amp, kFs, antiPhase);
    run(e, s);
    return db(toneAmp(right ? s.r : s.l, hz, s.size() - 12000, 12000) / amp);
}
double harmonicDb(hc::fx::Effect& e, double hz, int k, float amp) {
    e.reset();
    Stereo s = sine(hz, 1.0, amp);
    run(e, s);
    return db(toneAmp(s.l, hz * k, 24000, 24000) / amp);
}
Stereo impulse(std::size_t n, std::size_t at = 100) {
    Stereo s(n);
    s.l[at] = s.r[at] = 1.0f;
    return s;
}
double rmsDb(const std::vector<float>& x, std::size_t from, std::size_t len) { return db(rms(x, from, len)); }

void allRules() {
    for (const char* name : {"STRAIGHT SHOOTER", "BOTTOM LINE", "TEN TOES", "FOUR CORNERS", "SMOOTH TALKER", "THE BOSS", "KINGPIN", "GOLD CHAIN", "IVORIES", "HIGH ROLLER", "THE MIDDLEMAN", "DOUBLE CROSS",
                             "CUTTHROAT"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

// Both linear-phase EQs: flat is a pure delay; the impulse is symmetrical; the audio follows the drawn curve.
template <class CurveFn>
void linEqBasics(const char* name, std::uint32_t lat, int outParam, std::initializer_list<std::pair<int, float>> shape, CurveFn curve,
                 std::initializer_list<double> freqs, double tol) {
    {
        auto e = make(name);
        CHECK(e->latency() == lat);
        Stereo s = impulse(lat + 6000);
        run(*e, s);
        double off = 0;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (i != 100 + lat) off = std::max(off, static_cast<double>(std::fabs(s.l[i])));
        std::printf("  %s flat: impulse %.5f at %u samples, largest other sample %.2e\n", name, s.l[100 + lat], lat, off);
        CHECK(std::fabs(s.l[100 + lat] - 1.0f) < 1e-3f && off < 1e-4);
    }
    auto e = make(name, shape);
    Stereo s = impulse(2 * lat + 4000, 2000);
    run(*e, s);
    const std::size_t c = 2000 + lat;
    double asym = 0, pk = std::fabs(s.l[c]);
    for (std::size_t k = 1; k < lat - 600; ++k) {
        asym = std::max(asym, static_cast<double>(std::fabs(s.l[c + k] - s.l[c - k])));
        pk = std::max(pk, static_cast<double>(std::fabs(s.l[c + k])));
    }
    std::printf("  %s impulse response: peak %.3f, largest left/right mismatch %.2e\n", name, pk, asym);
    CHECK(asym < pk * 1e-3);
    std::vector<float> v(static_cast<std::size_t>(outParam + 5));
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = e->get(static_cast<int>(i));
    double worst = 0, worstHz = 0;
    for (double hz : freqs) {
        const double got = toneGain(*e, hz, 0.1f, false, 2.5), want = curve(v.data(), kFs, hz);
        if (std::fabs(got - want) > worst) { worst = std::fabs(got - want); worstHz = hz; }
    }
    std::printf("  %s: audio vs drawn curve, worst %.2f dB (at %.0f Hz)\n", name, worst, worstHz);
    CHECK(worst < tol);
}

void testStraightShooter() {
    using namespace lineq;
    using namespace straight;
    linEqBasics("STRAIGHT SHOOTER", kLatency, Output,
                {{band(0, Type), FLowCut}, {band(0, Freq), 80}, {band(2, Freq), 1000}, {band(2, Gain), 6}, {band(3, Type), RHighShelf},
                 {band(3, Freq), 3000}, {band(3, Gain), -4}, {band(3, Q), 2.0f}, {band(5, Type), VHighCut}, {band(5, Freq), 14000}, {band(5, Q), 2}},
                curveDb, {60.0, 150.0, 400.0, 1000.0, 2500.0, 4000.0, 6000.0, 12000.0, 16000.0}, 0.5);
    // the precision cut gets steeper with Q and never bumps; the resonant one bumps
    {
        float v[kCount];
        auto e = make("STRAIGHT SHOOTER");
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        v[band(1, Type)] = VLowCut;
        v[band(1, Freq)] = 1000;
        auto octave = [&](float q) { v[band(1, Q)] = q; return curveDb(v, kFs, 500) - curveDb(v, kFs, 250); };
        const double soft = octave(0.26f), steep = octave(6.5f);
        double bump = -99;
        v[band(1, Q)] = 6.5f;
        for (double hz = 1000; hz < 4000; hz *= 1.05) bump = std::max(bump, curveDb(v, kFs, hz));
        v[band(1, Type)] = RLowCut;
        v[band(1, Q)] = 3.0f;
        const double res = curveDb(v, kFs, 1000);
        std::printf("  STRAIGHT SHOOTER precision low cut, dB per octave: Q 0.26 %.1f, Q 6.5 %.1f; most above the corner %+.2f; resonant Q 3 at the corner %+.1f dB\n",
                    soft, steep, bump, res);
        CHECK(soft > 3.5 && soft < 5.0 && steep > 38.0 && bump <= 0.0 && res > 8.0);
    }
    // the 30 Hz end of band 1: a 12 dB low cut at 80 Hz
    {
        auto e = make("STRAIGHT SHOOTER", {{band(0, Type), FLowCut}, {band(0, Freq), 80}, {band(0, Q), 0.71f}});
        const double lo = toneGain(*e, 30, 0.25f, false, 2.0);
        std::printf("  STRAIGHT SHOOTER: low cut at 80 Hz, 30 Hz -> %.1f dB\n", lo);
        CHECK(lo < -15.0 && lo > -19.5);
    }
    // a band switched off is flat; GAIN is a plain gain
    {
        auto e = make("STRAIGHT SHOOTER", {{band(2, Gain), 12}, {band(2, On), 0}, {Output, -6}});
        const double g = toneGain(*e, 689, 0.25f);
        std::printf("  STRAIGHT SHOOTER band off, GAIN -6: %+.2f dB\n", g);
        CHECK(std::fabs(g + 6.0) < 0.1);
    }
    // TRIM: the loudest peak lands on 0 dBFS after a push
    {
        auto e = make("STRAIGHT SHOOTER", {{band(3, Gain), 9}, {band(3, Q), 0.5f}});
        Stereo s = sine(1809, 1.0, 0.5f, kFs);
        run(*e, s);
        e->set(Trim, 1);
        Stereo t = sine(1809, 1.0, 0.5f, kFs);
        run(*e, t);
        float pk = 0;
        for (std::size_t i = t.size() / 2; i < t.size(); ++i) pk = std::max(pk, std::fabs(t.l[i]));
        std::printf("  STRAIGHT SHOOTER TRIM push: peak after %.2f dBFS\n", db(pk));
        CHECK(std::fabs(db(pk)) < 0.1);
    }
    // DITHER: 24-bit steps, and digital silence stays silent
    {
        auto e = make("STRAIGHT SHOOTER", {{Dither, 1}});
        Stereo s = sine(1000, 0.5, 0.25f, kFs);
        run(*e, s);
        const double lsb = std::ldexp(1.0, -23);
        double worst = 0;
        for (std::size_t i = 6000; i < s.size(); ++i) worst = std::max(worst, std::fabs(s.l[i] / lsb - std::round(s.l[i] / lsb)));
        Stereo z(30000);
        run(*e, z);
        float tail = 0;
        for (std::size_t i = 20000; i < z.size(); ++i) tail = std::max(tail, std::fabs(z.l[i]));
        std::printf("  STRAIGHT SHOOTER DITHER: off the 24-bit grid by at most %.3f steps; silence after %.1e\n", worst, tail);
        CHECK(worst < 0.01 && tail == 0.0f);
    }
    // FILTER changes the window, not the delay
    {
        auto e = make("STRAIGHT SHOOTER", {{Filter, 2}, {band(2, Gain), -8}});
        CHECK(e->latency() == kLatency);
        const double g = toneGain(*e, 689, 0.25f);
        std::printf("  STRAIGHT SHOOTER SMOOTH filter, -8 dB bell: %+.2f dB\n", g);
        CHECK(std::fabs(g + 8.0) < 0.3);
    }
    const double d = blockSizeDifference(*findPlugin("STRAIGHT SHOOTER"),
                                         [](hc::fx::Effect& x) { x.set(band(1, Gain), -6); x.set(band(5, Type), VHighCut); x.set(band(5, Freq), 6000); },
                                         noise(0.6, 0.3f));
    std::printf("  STRAIGHT SHOOTER block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testBottomLine() {
    using namespace lineq;
    using namespace bottomline;
    linEqBasics("BOTTOM LINE", kLatency, Output,
                {{band(0, On), 1}, {band(0, Freq), 30}, {band(0, Q), 2}, {band(1, Freq), 80}, {band(1, Gain), 5}, {band(1, Q), 1.5f},
                 {band(2, Type), RLowShelf}, {band(2, Freq), 300}, {band(2, Gain), -3}},
                curveDb, {25.0, 40.0, 60.0, 80.0, 120.0, 250.0, 500.0, 1000.0, 5000.0}, 0.6);
    // the resonant low shelf stops at +10 dB
    {
        auto e = make("BOTTOM LINE", {{band(1, Type), RLowShelf}, {band(1, Gain), 25}, {band(1, Freq), 200}, {band(1, Q), 0.71f}});
        const double g = toneGain(*e, 30, 0.02f, false, 2.5);
        std::printf("  BOTTOM LINE resonant low shelf asked +25: 30 Hz %+.1f dB\n", g);
        CHECK(g > 9.0 && g < 10.6);
    }
    // a steep cut down low: 40 Hz precision cut, Q 4, at 20 Hz
    {
        auto e = make("BOTTOM LINE", {{band(0, On), 1}, {band(0, Freq), 40}, {band(0, Q), 4}});
        const double g = toneGain(*e, 20, 0.25f, false, 3.0), keep = toneGain(*e, 100, 0.25f);
        std::printf("  BOTTOM LINE precision cut 40 Hz Q 4: 20 Hz %.1f dB, 100 Hz %+.2f dB\n", g, keep);
        CHECK(g < -18.0 && std::fabs(keep) < 0.2);
    }
    const double d = blockSizeDifference(*findPlugin("BOTTOM LINE"), [](hc::fx::Effect& x) { x.set(band(1, Gain), 6); }, noise(0.6, 0.3f));
    std::printf("  BOTTOM LINE block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

// The minimum-phase EQs: the audio follows the drawn curves (left and right), LINK, the side controls.
void iirEqBasics(const char* name, int side, int link, std::initializer_list<std::pair<int, float>> shape,
                 double (*left)(const float*, double, double), double (*right)(const float*, double, double)) {
    auto e = make(name, shape);
    e->set(link, 0);
    std::vector<float> v(e->params().size());
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = e->get(static_cast<int>(i));
    double worst = 0;
    for (double hz : {40.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0, 15000.0}) {
        worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.1f) - left(v.data(), kFs, hz)));
        worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.1f, true) - right(v.data(), kFs, hz)));
    }
    // linked: the right side runs the left set
    e->set(link, 1);
    v[static_cast<std::size_t>(link)] = 1;
    double linkDiff = 0;
    for (double hz : {100.0, 1000.0, 8000.0}) linkDiff = std::max(linkDiff, std::fabs(toneGain(*e, hz, 0.1f, true) - left(v.data(), kFs, hz)));
    std::printf("  %s: audio vs drawn curves (left and right), worst %.3f dB; linked right vs left curve %.3f dB\n", name, worst, linkDiff);
    CHECK(worst < 0.1 && linkDiff < 0.1);
    // a big jump of every gain while playing: no click (the largest step between samples stays small)
    auto f = make(name);
    Stereo a = sine(200, 0.3, 0.5f, kFs);
    run(*f, a);
    for (int b = 0; b < 4; ++b) f->set(b * lineq::kStride + lineq::Gain, 12);
    Stereo c = sine(200, 0.3, 0.5f, kFs);
    for (std::size_t i = 0; i < c.size(); ++i) c.l[i] = c.r[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 200.0 * (i + a.size()) / kFs));
    run(*f, c);
    float step = 0;
    for (std::size_t i = 1; i < c.size(); ++i) step = std::max(step, std::fabs(c.l[i] - c.l[i - 1]));
    std::printf("  %s: all gains jump to +12 while playing, largest sample step %.4f\n", name, step);
    CHECK(step < 0.2f);
    (void)side;
    const double d = blockSizeDifference(*findPlugin(name), [](hc::fx::Effect& x) { x.set(lineq::Gain, 6); x.set(lineq::kStride + lineq::Gain, -5); },
                                         noise(0.5, 0.3f));
    std::printf("  %s block size 512 vs 1: %.2e\n", name, d);
    CHECK(d < 1e-5);
}

void testTenToes() {
    using namespace lineq;
    using namespace tentoes;
    iirEqBasics("TEN TOES", kSide, Link,
                {{band(0, Type), LowCut}, {band(0, Freq), 60}, {band(4, Gain), 6}, {band(6, Gain), -9}, {band(6, Q), 8},
                 {kSide + band(2, Gain), 4}, {kSide + band(8, Type), HighShelf}, {kSide + band(8, Gain), -6}},
                curveDb, curveRightDb);
    // the tight bell narrows as it boosts: half a octave off, a tight +15 sits lower than a plain +15
    {
        auto plain = make("TEN TOES", {{band(5, Gain), 15}, {band(5, Q), 1}});
        auto tight = make("TEN TOES", {{band(5, Gain), 15}, {band(5, Q), 1}, {band(5, Type), TightBell}});
        const double a = toneGain(*plain, 1414, 0.02f), b = toneGain(*tight, 1414, 0.02f), peak = toneGain(*tight, 1000, 0.02f);
        std::printf("  TEN TOES +15 at 1 kHz, Q 1; at 1.4 kHz: bell %+.1f, tight bell %+.1f dB (peak %+.1f)\n", a, b, peak);
        CHECK(b < a - 1.0 && std::fabs(peak - 15.0) < 0.2);
    }
    // the side controls: PHASE L flips the left only; INPUT R at the bottom is off; OUTPUT L / R
    {
        auto e = make("TEN TOES", {{PhaseL, 1}, {OutputR, -6}});
        Stereo s = sine(1000, 0.5, 0.25f, kFs);
        Stereo in = s;
        run(*e, s);
        double corrL = 0, corrR = 0;
        for (std::size_t i = 12000; i < s.size(); ++i) { corrL += s.l[i] * in.l[i]; corrR += s.r[i] * in.r[i]; }
        const double gR = toneGain(*e, 1000, 0.25f, true);
        e->set(InputR, -60);
        const double off = toneGain(*e, 1000, 0.25f, true);
        std::printf("  TEN TOES PHASE L: left %s, right %s; OUTPUT R -6: %+.2f dB; INPUT R off: %.0f dB\n", corrL < 0 ? "flipped" : "not flipped",
                    corrR > 0 ? "untouched" : "flipped", gR, off);
        CHECK(corrL < 0 && corrR > 0 && std::fabs(gR + 6) < 0.05 && off < -100);
    }
    // TRIM: at most +12, and the loudest peak onto 0 dBFS
    {
        auto e = make("TEN TOES");
        Stereo s = sine(1000, 0.5, 0.5f, kFs);
        run(*e, s);
        e->set(Trim, 1);
        Stereo blk(512);
        run(*e, blk);   // the push lands on the next block; a reset afterwards keeps it
        const double g = toneGain(*e, 1000, 0.5f);
        std::printf("  TEN TOES TRIM after a -6 dBFS tone: %+.2f dB\n", g);
        CHECK(std::fabs(g - 6.02) < 0.05);
    }
}

void testFourCorners() {
    using namespace lineq;
    using namespace fourcorners;
    iirEqBasics("FOUR CORNERS", kSide, Link,
                {{band(0, Type), LowShelfRes}, {band(0, Gain), 5}, {band(0, Q), 2}, {band(2, Gain), -6}, {band(3, Type), HighCut}, {band(3, Freq), 9000},
                 {kSide + band(1, Gain), 7}, {kSide + band(3, Gain), 3}},
                curveDb, curveRightDb);
    // a plain shelf never bumps, a resonant one does
    {
        float v[kCount];
        auto e = make("FOUR CORNERS");
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        v[band(0, Gain)] = 6;
        v[band(0, Q)] = 4;
        double plainMin = 99, resMin = 99;
        for (double hz = 100; hz < 1000; hz *= 1.03) plainMin = std::min(plainMin, curveDb(v, kFs, hz));
        v[band(0, Type)] = LowShelfRes;
        for (double hz = 100; hz < 1000; hz *= 1.03) resMin = std::min(resMin, curveDb(v, kFs, hz));
        std::printf("  FOUR CORNERS low shelf +6, Q 4, lowest point above the corner: plain %+.2f, resonant %+.2f dB\n", plainMin, resMin);
        CHECK(plainMin > -0.05 && resMin < -2.0);
    }
    {
        auto e = make("FOUR CORNERS", {{GainL, -4}, {GainR, 5}});
        const double l = toneGain(*e, 1000, 0.1f), r = toneGain(*e, 1000, 0.1f, true);
        std::printf("  FOUR CORNERS GAIN L -4 / R +5: %+.2f / %+.2f dB\n", l, r);
        CHECK(std::fabs(l + 4) < 0.05 && std::fabs(r - 5) < 0.05);
    }
}

void testSmoothTalker() {
    using namespace lineq;
    using namespace smoothtalker;
    iirEqBasics("SMOOTH TALKER", kSide, Link,
                {{band(0, On), 1}, {band(0, Freq), 50}, {band(1, Gain), 4}, {band(3, Gain), -5}, {band(5, On), 1}, {band(5, Type), 1}, {band(5, Gain), 3},
                 {band(5, Freq), 9000}, {kSide + band(2, Gain), 6}, {kSide + band(4, Gain), -4}},
                curveDb, curveRightDb);
    // shelves and cuts keep Q between 0.71 and 1.41 whatever the knob says; OUTPUT at the bottom is off
    {
        float v[kCount];
        auto e = make("SMOOTH TALKER");
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        v[band(0, On)] = 1;
        v[band(0, Freq)] = 100;
        v[band(0, Q)] = 6.5f;
        const double atQ65 = curveDb(v, kFs, 100);
        v[band(0, Q)] = 1.41f;
        const double atQ141 = curveDb(v, kFs, 100);
        e->set(Output, -60);
        const double off = toneGain(*e, 1000, 0.25f);
        std::printf("  SMOOTH TALKER low cut at its corner, Q knob 6.5: %+.2f dB (same as 1.41: %+.2f); OUTPUT bottom: %.0f dB\n", atQ65, atQ141, off);
        CHECK(std::fabs(atQ65 - atQ141) < 1e-9 && off < -100);
    }
}

void testTheBoss() {
    using namespace boss;
    // compressor: a loud tone comes down; the reduction is reported
    {
        auto e = make("THE BOSS", {{Threshold, -30}, {Ratio, 4}});
        const double g = toneGain(*e, 1000, 0.5f);
        std::printf("  THE BOSS -6 dBFS tone, threshold -30, 4:1: %+.1f dB, reduction %.1f dB\n", g, e->reductionDb());
        CHECK(g < -12.0 && g > -22.0 && e->reductionDb() < -10.0f);
    }
    // EQ (dynamics out of the way): LOW +6 at 100 Hz lifts 40 Hz by about 6 dB, LOW MID cuts its frequency
    {
        auto e = make("THE BOSS", {{Ratio, 1}, {LowGain, 6}, {LowFreq, 1}, {LoMidGain, -8}, {LoMidFreq, 700}});
        const double lo = toneGain(*e, 40, 0.1f), mid = toneGain(*e, 700, 0.1f), hi = toneGain(*e, 10000, 0.1f);
        std::printf("  THE BOSS EQ: 40 Hz %+.1f, 700 Hz %+.1f, 10 kHz %+.1f dB\n", lo, mid, hi);
        CHECK(lo > 5.0 && lo < 6.5 && mid < -7.0 && std::fabs(hi) < 0.5);
    }
    // gate: a quiet tone under GATE is shut down
    {
        auto e = make("THE BOSS", {{Ratio, 1}, {Gate, -30}});
        const double g = toneGain(*e, 1000, 0.003f);
        std::printf("  THE BOSS gate -30, a -50 dBFS tone: %+.1f dB\n", g);
        CHECK(g < -35.0);
    }
    // limiter: nothing over -0.3 dBFS, latency the same with it on or off
    {
        auto e = make("THE BOSS", {{Ratio, 1}, {Limit, 1}, {LowGain, 12}});
        Stereo s = sine(100, 1.0, 1.5f);
        run(*e, s);
        const double pk = peak(s);
        std::printf("  THE BOSS limit on, a +3.5 dBFS tone +12 dB: peak %.3f, latency %u\n", pk, e->latency());
        CHECK(pk <= 0.9661 && e->latency() == kLatency);
    }
    const double d = blockSizeDifference(*findPlugin("THE BOSS"), [](hc::fx::Effect& x) { x.set(Threshold, -30); x.set(Gate, -20); x.set(Limit, 1); x.set(Order, 1); },
                                         noise(1.0, 0.8f));
    std::printf("  THE BOSS block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

// Drum-like hits: a 120 Hz thump with a sharp attack every 0.5 s.
Stereo hits(double seconds, float amp) {
    Stereo s(static_cast<std::size_t>(seconds * kFs));
    for (std::size_t i = 0; i < s.size(); ++i) {
        const double t = static_cast<double>(i) / kFs, b = std::fmod(t, 0.5);
        const float v = amp * static_cast<float>(std::exp(-b * 18.0) * std::sin(2 * std::numbers::pi * 120 * b));
        s.l[i] = s.r[i] = v;
    }
    return s;
}
// peak in the first 15 ms after each hit, and the level 150..400 ms after it
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

void testKingpin() {
    using namespace kingpin;
    {
        auto e = make("KINGPIN");
        const double g = toneGain(*e, 1000, 0.3f);
        std::printf("  KINGPIN at its defaults: 1 kHz %+.2f dB (latency %u)\n", g, e->latency());
        CHECK(std::fabs(g) < 0.1);
    }
    double h0, t0, hp, tp, hc, tc;
    {
        auto e = make("KINGPIN");
        Stereo s = hits(3.0, 0.3f);
        run(*e, s);
        hitAndTail(s, e->latency(), h0, t0);
        auto p = make("KINGPIN", {{Punch, 100}});
        Stereo sp = hits(3.0, 0.3f);
        run(*p, sp);
        hitAndTail(sp, p->latency(), hp, tp);
        auto c = make("KINGPIN", {{Crush, 100}});
        Stereo sc = hits(3.0, 0.3f);
        run(*c, sc);
        hitAndTail(sc, c->latency(), hc, tc);
    }
    std::printf("  KINGPIN hits: plain %.1f / tail %.1f dB, PUNCH 100 %.1f / %.1f, CRUSH 100 %.1f / %.1f\n", h0, t0, hp, tp, hc, tc);
    CHECK(hp > h0 + 2.0 && tp < t0 + 1.0);
    CHECK(tc > t0 + 4.0);
    {
        auto e = make("KINGPIN", {{Thump, 6}, {ThumpFreq, 70}});
        const double g = toneGain(*e, 70, 0.1f);
        auto d0 = make("KINGPIN");
        auto d1 = make("KINGPIN", {{Drive, 100}});
        const double h3a = harmonicDb(*d0, 200, 3, 0.5f), h3b = harmonicDb(*d1, 200, 3, 0.5f);
        std::printf("  KINGPIN THUMP 6 dB at 70 Hz: %+.1f dB; DRIVE 100: 3rd harmonic %.1f -> %.1f dB\n", g, h3a, h3b);
        CHECK(g > 5.5 && g < 6.5 && h3b > h3a + 20.0);
    }
    const double d = blockSizeDifference(*findPlugin("KINGPIN"), [](hc::fx::Effect& x) { x.set(Gate, -30); x.set(Punch, 50); x.set(Crush, 50); x.set(Drive, 50); },
                                         hits(1.5, 0.5f));
    std::printf("  KINGPIN block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testGoldChain() {
    using namespace goldchain;
    {
        auto e = make("GOLD CHAIN");
        const double g = toneGain(*e, 1000, 0.3f);
        auto l = make("GOLD CHAIN", {{Input, 6}});
        const double gl = toneGain(*l, 1000, 0.3f);
        std::printf("  GOLD CHAIN tone 0: 1 kHz %+.2f dB; INPUT +6 with LINK: %+.2f dB\n", g, gl);
        CHECK(std::fabs(g) < 0.1 && std::fabs(gl) < 0.1);
    }
    {
        auto a = make("GOLD CHAIN"), b = make("GOLD CHAIN", {{Tone, 100}});
        const double h2a = harmonicDb(*a, 200, 2, 0.5f), h2b = harmonicDb(*b, 200, 2, 0.5f);
        const double lo = toneGain(*b, 60, 0.02f), hi = toneGain(*b, 14000, 0.02f), mid = toneGain(*b, 1000, 0.02f);
        std::printf("  GOLD CHAIN tone 100: 2nd harmonic %.1f -> %.1f dB; quiet 60 Hz %+.1f, 1 kHz %+.1f, 14 kHz %+.1f dB\n", h2a, h2b, lo, mid, hi);
        CHECK(h2b > h2a + 20.0 && lo > mid + 1.0 && hi > mid + 1.0);
    }
    const double d = blockSizeDifference(*findPlugin("GOLD CHAIN"), [](hc::fx::Effect& x) { x.set(Tone, 80); x.set(Mix, 70); x.set(Input, 4); },
                                         noise(1.0, 0.5f));
    std::printf("  GOLD CHAIN block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testIvories() {
    using namespace ivories;
    {
        auto e = make("IVORIES");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  IVORIES at its defaults: largest change %.2e\n", diff);
        CHECK(diff < 1e-4);
    }
    {
        auto e = make("IVORIES", {{Tone, 100}});
        const double lo = toneGain(*e, 100, 0.1f), hi = toneGain(*e, 8000, 0.1f);
        std::printf("  IVORIES TONE +100: 100 Hz %+.1f, 8 kHz %+.1f dB\n", lo, hi);
        CHECK(lo < -2.5 && hi > 3.5);
    }
    {
        auto e = make("IVORIES", {{Delay, 100}, {Time, 100}});
        Stereo s = impulse(24000);
        run(*e, s);
        // the echo: left at 100 ms, right at 130 ms
        auto at = [&](const std::vector<float>& x, std::size_t from, std::size_t to) {
            double pk = 0;
            std::size_t where = 0;
            for (std::size_t i = from; i < to; ++i) if (std::fabs(x[i]) > pk) { pk = std::fabs(x[i]); where = i; }
            return where;
        };
        const std::size_t l = at(s.l, 2000, 20000) - 100, r = at(s.r, 2000, 20000) - 100;
        std::printf("  IVORIES DELAY: echo left after %.1f ms, right after %.1f ms\n", l / 48.0, r / 48.0);
        CHECK(l > 4700 && l < 4950 && r > 6150 && r < 6400);
    }
    {
        auto e = make("IVORIES", {{Width, 0}, {Doubler, 100}});
        Stereo s = noise(0.5, 0.3f);
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  IVORIES WIDTH 0 (doubler on): largest left/right difference %.2e\n", diff);
        CHECK(diff < 1e-5);
    }
}

void testHighRoller() {
    using namespace highroller;
    {
        auto e = make("HIGH ROLLER");
        double worst = 0;
        for (double hz : {50.0, 200.0, 1000.0, 4000.0, 12000.0}) worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.2f)));
        std::printf("  HIGH ROLLER at its defaults: tones 50 Hz..12 kHz within %.2f dB\n", worst);
        CHECK(worst < 0.1);
    }
    {
        auto e = make("HIGH ROLLER", {{Lift3, 100}});
        const double quiet = toneGain(*e, 1000, 0.01f), loud = toneGain(*e, 1000, 0.5f);
        std::printf("  HIGH ROLLER MID LIFT 100: a -40 dBFS tone %+.1f dB, a -6 dBFS tone %+.1f dB\n", quiet, loud);
        CHECK(quiet > 4.0 && loud < 1.0);
    }
    {
        auto e = make("HIGH ROLLER", {{Width3, 0}});
        const double side = toneGain(*e, 1000, 0.3f, false, 1.5, true);
        auto w = make("HIGH ROLLER", {{Width3, 200}});
        const double wide = toneGain(*w, 1000, 0.3f, false, 1.5, true);
        std::printf("  HIGH ROLLER pure sides at 1 kHz: MID WIDTH 0 %.1f dB, 200 %+.1f dB\n", side, wide);
        CHECK(side < -18.0 && wide > 5.5);   // the neighbouring bands still leak in a little (24 dB / octave splits)
    }
}

void testMiddleman() {
    using namespace middleman;
    {
        auto e = make("THE MIDDLEMAN", {{Center, -6}, {LowBalance, 100}, {HighBalance, -50}, {Punch, 80}});
        Stereo s = noise(0.5, 0.3f);
        s.r = s.l;   // mono
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  THE MIDDLEMAN mono in: largest left/right difference %.2e\n", diff);
        CHECK(diff < 1e-6);
    }
    {
        auto c = make("THE MIDDLEMAN", {{Center, -6}});
        auto s = make("THE MIDDLEMAN", {{Sides, 6}});
        auto lb = make("THE MIDDLEMAN", {{LowBalance, 100}});
        const double mono = toneGain(*c, 1000, 0.3f), side = toneGain(*s, 1000, 0.3f, false, 1.5, true), lowSide = toneGain(*lb, 60, 0.3f, false, 1.5, true);
        std::printf("  THE MIDDLEMAN: CENTER -6 on mono %+.1f dB, SIDES +6 on sides %+.1f dB, LOW BALANCE +100 on low sides %+.1f dB\n", mono, side, lowSide);
        CHECK(std::fabs(mono + 6.0) < 0.2 && std::fabs(side - 6.0) < 0.2 && lowSide < -5.0);
    }
}

void testDoubleCross() {
    using namespace doublecross;
    {
        auto e = make("DOUBLE CROSS", {{Spread, 100}});
        double worst = 0;
        for (double hz : {80.0, 250.0, 700.0, 2000.0, 6000.0}) {
            e->reset();
            Stereo s = sine(hz, 1.5, 0.3f);
            run(*e, s);
            Stereo sum(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) sum.l[i] = 0.5f * (s.l[i] + s.r[i]);
            worst = std::max(worst, std::fabs(db(toneAmp(sum.l, hz, s.size() - 12000, 12000) / 0.3)));
        }
        Stereo n = noise(1.0, 0.3f);
        n.r = n.l;
        e->reset();
        run(*e, n);
        double lr = 0, ll = 0, rr = 0;
        for (std::size_t i = 4800; i < n.size(); ++i) { lr += n.l[i] * n.r[i]; ll += n.l[i] * n.l[i]; rr += n.r[i] * n.r[i]; }
        const double corr = lr / std::sqrt(ll * rr);
        std::printf("  DOUBLE CROSS spread 100: mono fold-down within %.2f dB; left/right correlation of mono noise %.2f\n", worst, corr);
        CHECK(worst < 0.1 && corr < 0.6);
    }
    {
        auto e = make("DOUBLE CROSS", {{Spread, 100}, {LowMono, 2}});
        e->reset();
        Stereo s = sine(50, 1.5, 0.3f);
        run(*e, s);
        Stereo d(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) d.l[i] = s.l[i] - s.r[i];
        const double sideDb = db(toneAmp(d.l, 50, s.size() - 12000, 12000) / 0.3);
        std::printf("  DOUBLE CROSS low stays mono (200 Hz): sides at 50 Hz %.1f dB\n", sideDb);
        CHECK(sideDb < -20.0);
    }
}

void testCutthroat() {
    using namespace cutthroat;
    {
        auto e = make("CUTTHROAT");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  CUTTHROAT at its defaults: largest change %.2e\n", diff);
        CHECK(diff < 1e-6);
    }
    {
        auto dark = make("CUTTHROAT", {{Sweep, -100}}), thin = make("CUTTHROAT", {{Sweep, 100}});
        const double a = toneGain(*dark, 1000, 0.2f), b = toneGain(*thin, 1000, 0.2f);
        std::printf("  CUTTHROAT 1 kHz: SWEEP all left %.1f dB, all right %.1f dB\n", a, b);
        CHECK(a < -30.0 && b < -30.0);
    }
    {
        auto e = make("CUTTHROAT", {{LowCut, 200}, {HighCut, 5000}, {Resonance, 30}, {Slope, 1}});
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        double worst = 0;
        for (double hz : {100.0, 200.0, 1000.0, 5000.0, 9000.0}) worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.1f) - curveDb(v, kFs, hz)));
        std::printf("  CUTTHROAT audio vs drawn curve: worst %.2f dB\n", worst);
        CHECK(worst < 0.3);
    }
    const double d = blockSizeDifference(*findPlugin("CUTTHROAT"), [](hc::fx::Effect& x) { x.set(Sweep, -40); x.set(Resonance, 60); x.set(Drive, 50); },
                                         noise(0.6, 0.3f));
    std::printf("  CUTTHROAT block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

// WIDE LOAD's later additions: SHUFFLE, ROTATION, ASYMMETRY (defaults leave the sound as it was)
void testWideLoadAdditions() {
    using namespace width;
    {
        auto e = make("WIDE LOAD");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max({diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])), static_cast<double>(std::fabs(s.r[i] - ref.r[i]))});
        std::printf("  WIDE LOAD at its defaults: largest change %.2e\n", diff);
        CHECK(diff < 1e-5);
    }
    {
        auto e = make("WIDE LOAD", {{Rotation, 45}});
        const double l = toneGain(*e, 1000, 0.3f), r = toneGain(*e, 1000, 0.3f, true);
        std::printf("  WIDE LOAD rotation +45 on a centred tone: left %.1f dB, right %+.1f dB\n", l, r);
        CHECK(l < -40.0 && std::fabs(r - 3.0) < 0.2);
    }
    {
        auto e = make("WIDE LOAD", {{Shuffle, 100}});
        const double lo = toneGain(*e, 100, 0.3f, false, 1.5, true), hi = toneGain(*e, 8000, 0.3f, false, 1.5, true);
        std::printf("  WIDE LOAD shuffle 100 on the sides: 100 Hz %+.1f dB, 8 kHz %+.1f dB\n", lo, hi);
        CHECK(lo > 5.0 && std::fabs(hi) < 0.3);
    }
    {
        auto e = make("WIDE LOAD", {{Asymmetry, 100}});
        const double l = toneGain(*e, 1000, 0.3f), r = toneGain(*e, 1000, 0.3f, true);
        std::printf("  WIDE LOAD asymmetry +100 on a centred tone: left %+.1f dB, right %+.1f dB\n", l, r);
        CHECK(l > r + 6.0);
    }
}

} // namespace

void testToneShop() {
    std::printf("[the tone shop]\n");
    allRules();
    testStraightShooter();
    testBottomLine();
    testTenToes();
    testFourCorners();
    testSmoothTalker();
    testTheBoss();
    testKingpin();
    testGoldChain();
    testIvories();
    testHighRoller();
    testMiddleman();
    testDoubleCross();
    testCutthroat();
    testWideLoadAdditions();
}

} // namespace hem::test
