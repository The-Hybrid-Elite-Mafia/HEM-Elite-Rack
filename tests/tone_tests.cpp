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
    for (const char* name : {"STRAIGHT SHOOTER", "BOTTOM LINE", "TEN TOES", "FOUR CORNERS", "SMOOTH TALKER", "INSIDE MAN", "LOW RIDER", "UNDERTOW", "DEEP POCKETS", "LOW PROFILE", "THE BOSS", "KINGPIN", "GOLD CHAIN", "IVORIES", "HIGH ROLLER", "THE MIDDLEMAN", "DOUBLE CROSS",
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
    // flat at rest: a quiet tone goes through untouched (the preamp's colour only shows when it's driven)
    {
        auto e = make("THE BOSS", {{Analog, 0}});
        CHECK(e->latency() == kLatency);
        const double g = toneGain(*e, 1000, 0.01f), lo = toneGain(*e, 40, 0.01f), hi = toneGain(*e, 15000, 0.01f);
        std::printf("  THE BOSS at rest, -40 dBFS: 40 Hz %+.2f, 1 kHz %+.2f, 15 kHz %+.2f dB\n", lo, g, hi);
        CHECK(std::fabs(g) < 0.1 && std::fabs(lo) < 0.2 && std::fabs(hi) < 0.2);
    }
    // the EQ
    {
        auto e = make("THE BOSS", {{Analog, 0}, {High, 6}, {MidGain, 10}, {MidFreq, 3}, {LowFreq, 3}, {LowGain, 12}});
        const double hi = toneGain(*e, 18000, 0.01f), mid = toneGain(*e, 2000, 0.01f), lo = toneGain(*e, 40, 0.01f, false, 2.0);
        e->set(MidMode, 1);
        const double cut = toneGain(*e, 2000, 0.01f);
        e->set(EqIn, 0);
        const double off = toneGain(*e, 2000, 0.01f);
        std::printf("  THE BOSS EQ: HIGH +8 -> 18 kHz %+.1f; MID 10 at 2 kHz: boost %+.1f, cut %+.1f; LOW 12 at 250 -> 40 Hz %+.1f; EQ out %+.2f dB\n", hi, mid,
                    cut, lo, off);
        CHECK(hi > 6.5 && hi < 8.3 && std::fabs(mid - 10) < 0.3 && std::fabs(cut + 10) < 0.3 && lo > 11.0 && lo < 12.3 && std::fabs(off) < 0.1);
    }
    {
        auto e = make("THE BOSS", {{Analog, 0}, {LowCut, 5}});
        const double lo = toneGain(*e, 20, 0.01f, false, 2.0), keep = toneGain(*e, 1000, 0.01f);
        std::printf("  THE BOSS LOW CUT -15: 20 Hz %+.1f dB, 1 kHz %+.2f dB\n", lo, keep);
        CHECK(lo < -11.0 && std::fabs(keep) < 0.1);
    }
    // the preamp: harder = more harmonics on a loud tone, the level stays about the same
    {
        auto soft = make("THE BOSS", {{Analog, 0}, {Preamp, 0}}), hard = make("THE BOSS", {{Analog, 0}, {Preamp, 5}});
        const double h3s = harmonicDb(*soft, 200, 3, 0.7f), h3h = harmonicDb(*hard, 200, 3, 0.7f);
        const double gs = toneGain(*soft, 200, 0.05f), gh = toneGain(*hard, 200, 0.05f);
        std::printf("  THE BOSS PREAMP 20 vs 70, a -3 dBFS tone: 3rd harmonic %.1f vs %.1f dB; level at -26 dBFS %+.2f vs %+.2f dB\n", h3s, h3h, gs, gh);
        CHECK(h3h > h3s + 10.0 && std::fabs(gs) < 0.3 && std::fabs(gh) < 1.0);
    }
    // PHASE flips; the VU reads 0 for a -18 dBFS RMS tone with VU CAL 18; TRIM
    {
        auto e = make("THE BOSS", {{Analog, 0}, {Phase, 1}});
        Stereo s = sine(1000, 0.5, 0.25f, kFs);
        Stereo in = s;
        run(*e, s);
        double corr = 0;
        for (std::size_t i = 12000; i < s.size(); ++i) corr += s.l[i] * in.l[i - kLatency];
        auto v = make("THE BOSS", {{Analog, 0}});
        Stereo t = sine(1000, 1.0, static_cast<float>(std::sqrt(2.0) * std::pow(10.0, -18.0 / 20.0)), kFs);
        run(*v, t);
        const float vu = v->meter(0);
        std::printf("  THE BOSS PHASE: %s; VU for a -18 dBFS RMS tone: %+.2f\n", corr < 0 ? "flipped" : "not flipped", vu);
        CHECK(corr < 0 && std::fabs(vu) < 0.2f);
    }
    {
        auto e = make("THE BOSS", {{Analog, 0}});
        Stereo s = sine(1000, 0.5, 0.063f, kFs);
        run(*e, s);
        e->set(Trim, 1);
        Stereo blk(512);
        run(*e, blk);
        const double g = toneGain(*e, 1000, 0.01f);
        std::printf("  THE BOSS TRIM after a -24 dBFS tone: %+.2f dB (most it gives: +12)\n", g);
        CHECK(std::fabs(g - 12.0) < 0.2);
    }
    // ANALOG: the hum sits far under the music
    {
        auto e = make("THE BOSS", {{Analog, 2}});
        Stereo s = sine(1000, 1.5, 0.25f, kFs);
        run(*e, s);
        const double hum = db(toneAmp(s.l, 60, s.size() - 24000, 24000) / 0.25);
        std::printf("  THE BOSS ANALOG 60 Hz: hum %.1f dB under the music\n", -hum);
        CHECK(hum < -55.0 && hum > -80.0);
    }
    const double d = blockSizeDifference(*findPlugin("THE BOSS"), [](hc::fx::Effect& x) { x.set(Analog, 0); x.set(MidGain, 8); x.set(Preamp, 4); },
                                         noise(1.0, 0.8f));
    std::printf("  THE BOSS block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testInsideMan() {
    using namespace insideman;
    // at rest: untouched
    {
        auto e = make("INSIDE MAN");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  INSIDE MAN at rest: largest change %.2e\n", diff);
        CHECK(diff < 1e-5);
    }
    // the EQ follows its drawn curve
    {
        auto e = make("INSIDE MAN", {{B1Type, 2}, {B1Gain, 5}, {B1Q, 2}, {B2Gain, -6}, {B4Type, 0}, {B4Freq, 9000}});
        std::vector<float> v(kCount);
        for (int i = 0; i < kCount; ++i) v[static_cast<std::size_t>(i)] = e->get(i);
        double worst = 0;
        for (double hz : {40.0, 100.0, 250.0, 1000.0, 5000.0, 12000.0}) worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.05f) - curveDb(v.data(), kFs, hz)));
        std::printf("  INSIDE MAN EQ: audio vs drawn curve, worst %.3f dB\n", worst);
        CHECK(worst < 0.05);
    }
    // compressor: 4:1 at -30 on a -6 dBFS tone; VOCAL picks a ratio by itself; ratio under 1 lifts
    {
        auto c = make("INSIDE MAN", {{CompOn, 1}, {CompThreshold, -30}, {Ratio, 4}});
        const double g = toneGain(*c, 1000, 0.5f);
        auto up = make("INSIDE MAN", {{CompOn, 1}, {CompThreshold, -30}, {Ratio, 0.5f}});
        const double gu = toneGain(*up, 1000, 0.05f);
        std::printf("  INSIDE MAN comp: -6 dBFS tone, -30, 4:1: %+.1f dB (18 expected); ratio 0.5 on a -26 dBFS tone: %+.1f dB\n", g, gu);
        CHECK(g < -16.0 && g > -19.5 && gu > 2.0 && gu <= 12.01);
    }
    // gate: a quiet tone under the threshold goes down to FLOOR; the expander takes 1 dB per dB under
    {
        auto gt = make("INSIDE MAN", {{GateOn, 1}, {GateThreshold, -30}, {Floor, -40}});
        auto ex = make("INSIDE MAN", {{GateOn, 1}, {GateMode, 1}, {GateThreshold, -30}, {Floor, -80}});
        const double g = toneGain(*gt, 1000, 0.01f), x = toneGain(*ex, 1000, 0.01f);
        std::printf("  INSIDE MAN a -40 dBFS tone: gate at -30 (floor -40) %+.1f dB; expander %+.1f dB\n", g, x);
        CHECK(std::fabs(g + 40.0) < 0.5 && x < -8.5 && x > -11.5);
    }
    // the key filter: a low cut at 2 kHz keeps a 100 Hz tone from opening the gate
    {
        auto k = make("INSIDE MAN", {{GateOn, 1}, {GateThreshold, -30}, {Floor, -40}, {GKeyOn, 1}, {GKeyType, FLowCut}, {GKeyFreq, 2000}});
        const double g = toneGain(*k, 100, 0.25f);
        std::printf("  INSIDE MAN gate keyed through a 2 kHz low cut: a -12 dBFS 100 Hz tone %+.1f dB\n", g);
        CHECK(g < -30.0);
    }
    // ORDER: EQ after the compressor changes the result; ROTATE +45 turns a centred sound hard right; clip guard
    {
        auto e = make("INSIDE MAN", {{Rotate, 45}});
        Stereo s = sine(1000, 0.5, 0.25f, kFs);
        run(*e, s);
        const double l = rms(s.l, 12000, 12000), r = rms(s.r, 12000, 12000);
        auto hot = make("INSIDE MAN", {{Output, 12}});
        Stereo h = sine(1000, 0.5, 0.9f, kFs);
        run(*hot, h);
        std::printf("  INSIDE MAN ROTATE +45: left %.1e, right %+.1f dB; OUTPUT +12 on a loud tone: peak %.3f\n", l, db(r / (0.25 / std::sqrt(2.0))), peak(h));
        CHECK(l < 1e-4 && std::fabs(db(r / (0.25 / std::sqrt(2.0))) - 3.01) < 0.1 && peak(h) <= 1.0);
    }
    const double d = blockSizeDifference(*findPlugin("INSIDE MAN"),
                                         [](hc::fx::Effect& x) {
                                             x.set(GateOn, 1); x.set(GateThreshold, -30); x.set(CompOn, 1); x.set(CompThreshold, -20); x.set(Arc, 1);
                                             x.set(Order, 1); x.set(B1Gain, 4);
                                         },
                                         noise(1.0, 0.6f));
    std::printf("  INSIDE MAN block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testBassMakers() {
    // LOW RIDER: only DIRECT = the input, delayed; LOW AIR makes the octave below; LO above 0 dB never passes full scale
    {
        using namespace lowrider;
        auto e = make("LOW RIDER", {{LowAir, -60}, {Lo, -60}});
        const std::uint32_t lat = e->latency();
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double worst = 0;
        for (std::size_t i = lat; i < s.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(s.l[i] - ref.l[i - lat])));
        auto a = make("LOW RIDER", {{Direct, -60}, {Lo, -60}, {LowAir, 0}, {Range, 100}});
        Stereo t = sine(60, 2.0, 0.3f);
        run(*a, t);
        const double oct = db(toneAmp(t.l, 30, t.size() - 48000, 48000) / 0.3), orig = db(toneAmp(t.l, 60, t.size() - 48000, 48000) / 0.3);
        auto h = make("LOW RIDER", {{Direct, -60}, {Lo, 30}, {LowAir, -60}});
        Stereo u = sine(50, 1.0, 0.9f);
        run(*h, u);
        std::printf("  LOW RIDER latency %u; DIRECT only: off by at most %.1e; LOW AIR from a 60 Hz tone: 30 Hz %.1f dB, 60 Hz %.1f dB; LO +30 on a loud tone: peak %.3f\n",
                    lat, worst, oct, orig, peak(u));
        CHECK(worst < 1e-6 && oct > -15.0 && oct > orig + 10.0 && peak(u) <= 1.0);
        auto al = make("LOW RIDER", {{Align, 1}});
        CHECK(al->latency() == lat);
    }
    // UNDERTOW: MIX 0 is the input; SUB -1 / -2 sit an octave and two below; MONO SUB is the same both sides
    {
        using namespace undertow;
        auto e = make("UNDERTOW", {{Mix, 0}});
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double worst = 0;
        for (std::size_t i = 0; i < s.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        auto a = make("UNDERTOW", {{Mix, 100}, {Sub2On, 1}, {Sub1, -60}, {RangeMax, 200}});
        Stereo t = sine(96, 2.0, 0.3f);
        run(*a, t);
        const double s2 = db(toneAmp(t.l, 24, t.size() - 48000, 48000) / 0.3);
        auto b = make("UNDERTOW", {{Mix, 100}, {RangeMax, 200}});
        Stereo u = sine(96, 2.0, 0.3f);
        u.r = u.l;
        for (auto& x : u.r) x *= 0.5f;
        run(*b, u);
        const double s1 = db(toneAmp(u.l, 48, u.size() - 48000, 48000) / 0.3);
        double lr = 0;
        for (std::size_t i = 0; i < u.size(); ++i) lr = std::max(lr, static_cast<double>(std::fabs(u.l[i] - u.r[i])));
        std::printf("  UNDERTOW MIX 0: off by %.1e; a 96 Hz tone: SUB -1 at 48 Hz %.1f dB, SUB -2 at 24 Hz %.1f dB; MONO SUB left/right %.1e\n", worst, s1, s2, lr);
        CHECK(worst < 1e-7 && s1 > -16.0 && s2 > -20.0 && lr < 1e-6);
    }
    // DEEP POCKETS: at rest the level stays flat; the harmonics fall by DECAY each; MONITOR ORIGINAL keeps the bass only
    {
        using namespace deeppockets;
        auto e = make("DEEP POCKETS", {{Harmonics, -60}});
        const double lo = toneGain(*e, 50, 0.2f), hi = toneGain(*e, 1000, 0.2f);
        auto h = make("DEEP POCKETS", {{Original, -60}, {Frequency, 120}, {HighPass, 0}, {Decay, -12}});
        Stereo t = sine(50, 2.0, 0.3f);
        run(*h, t);
        const double f = db(toneAmp(t.l, 50, t.size() - 48000, 48000) / 0.3), h2 = db(toneAmp(t.l, 100, t.size() - 48000, 48000) / 0.3),
                     h3 = db(toneAmp(t.l, 150, t.size() - 48000, 48000) / 0.3);
        auto o = make("DEEP POCKETS", {{Listen, OriginalOnly}});
        const double ob = toneGain(*o, 30, 0.2f, false, 2.0), oh = toneGain(*o, 1000, 0.2f);
        std::printf("  DEEP POCKETS harmonics off: 50 Hz %+.2f, 1 kHz %+.2f dB; original off, 50 Hz tone: 50 Hz %.1f, 100 Hz %.1f, 150 Hz %.1f dB (DECAY -12); "
                    "MONITOR ORIGINAL: 30 Hz %+.1f, 1 kHz %.1f dB\n", lo, hi, f, h2, h3, ob, oh);
        CHECK(std::fabs(lo) < 0.2 && std::fabs(hi) < 0.1 && h2 > f + 10.0 && std::fabs((h3 - h2) + 12.0) < 2.0 && std::fabs(ob) < 0.3 && oh < -30.0);
        auto q1 = make("DEEP POCKETS", {{Original, -60}}), q4 = make("DEEP POCKETS", {{Original, -60}, {Ratio, 4}});
        Stereo a = sine(50, 2.0, 0.01f), b = sine(50, 2.0, 0.01f);
        run(*q1, a);
        run(*q4, b);
        const double r1 = db(toneAmp(a.l, 100, a.size() - 48000, 48000)), r4 = db(toneAmp(b.l, 100, b.size() - 48000, 48000));
        std::printf("  DEEP POCKETS a quiet bass (-40 dBFS): 100 Hz harmonic RATIO 1 %.1f, RATIO 4 %.1f dB\n", r1, r4);
        CHECK(r4 > r1 + 10.0);
    }
    // LOW PROFILE: ORIGINAL BASS out and harmonics in
    {
        using namespace lowprofile;
        auto e = make("LOW PROFILE", {{OriginalIn, 0}});
        Stereo t = sine(50, 2.0, 0.3f);
        run(*e, t);
        const double f = db(toneAmp(t.l, 50, t.size() - 48000, 48000) / 0.3), h2 = db(toneAmp(t.l, 100, t.size() - 48000, 48000) / 0.3);
        auto g = make("LOW PROFILE", {{Gain, -60}});
        const double off = toneGain(*g, 1000, 0.3f);
        std::printf("  LOW PROFILE original out, 50 Hz tone: 50 Hz %.1f dB, 100 Hz %.1f dB; GAIN bottom: %.0f dB\n", f, h2, off);
        CHECK(h2 > f + 10.0 && off < -100.0);
    }
    for (const char* name : {"LOW RIDER", "UNDERTOW", "DEEP POCKETS", "LOW PROFILE"}) {
        const double d = blockSizeDifference(*findPlugin(name), [](hc::fx::Effect&) {}, noise(0.8, 0.3f));
        std::printf("  %s block size 512 vs 1: %.2e\n", name, d);
        CHECK(d < 1e-5);
    }
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
        auto e = make("KINGPIN", {{Analog, 0}});
        const double g = toneGain(*e, 1000, 0.3f);
        std::printf("  KINGPIN at rest: %+.3f dB\n", g);
        CHECK(std::fabs(g) < 0.05);
    }
    // COMPRESS: the hits come down (SENS pushes it harder)
    {
        auto e = make("KINGPIN", {{Analog, 0}, {Compress, 100}, {Sens, 30}});
        Stereo s = hits(3.0, 0.8f);
        float most = 0.0f;
        for (std::size_t off = 0; off < s.size(); off += 512) {
            const auto m = static_cast<std::uint32_t>(std::min<std::size_t>(512, s.size() - off));
            e->process(s.l.data() + off, s.r.data() + off, m);
            most = std::min(most, e->reductionDb());
        }
        std::printf("  KINGPIN KICK COMPRESS 100, SENS +30: most reduction %.1f dB\n", most);
        CHECK(most < -10.0f);
    }
    // GATE (KICK only): a quiet tone under it is shut; on SNARE the gate does nothing
    {
        auto k = make("KINGPIN", {{Analog, 0}, {Gate, 0}});
        auto sn = make("KINGPIN", {{Analog, 0}, {Gate, 0}, {Type, Snare}});
        const double gk = toneGain(*k, 200, 0.003f), gs = toneGain(*sn, 200, 0.003f);
        std::printf("  KINGPIN GATE 0 (-35 dBFS), a -50 dBFS tone: KICK %+.1f dB, SNARE %+.1f dB\n", gk, gs);
        CHECK(gk < -30.0 && std::fabs(gs) < 0.1);
    }
    // FX: a tail after a snare hit
    {
        auto wet = make("KINGPIN", {{Analog, 0}, {Type, Snare}, {Fx, 100}}), dry = make("KINGPIN", {{Analog, 0}, {Type, Snare}});
        Stereo a = impulse(48000, 100), b = impulse(48000, 100);
        run(*wet, a);
        run(*dry, b);
        const double tw = rmsDb(a.l, 4900, 9600), td = rms(b.l, 4900, 9600);
        std::printf("  KINGPIN SNARE FX 100: tail 100-300 ms after a hit %.1f dB (dry: %.1e)\n", tw, td);
        CHECK(tw > -70.0 && tw < -20.0 && td < 1e-9);
    }
    // BASS / TREBLE follow TYPE; OUTPUT ±50 = ±10 dB
    {
        auto k = make("KINGPIN", {{Analog, 0}, {Bass, 5}}), oh = make("KINGPIN", {{Analog, 0}, {Bass, 5}, {Type, Overhead}});
        auto h = make("KINGPIN", {{Analog, 0}, {Treble, 5}, {Type, Hats}}), o = make("KINGPIN", {{Analog, 0}, {Output, 50}});
        const double kb = toneGain(*k, 30, 0.05f, false, 2.0), ob = toneGain(*oh, 30, 0.05f, false, 2.0), ht = toneGain(*h, 18000, 0.05f),
                     og = toneGain(*o, 1000, 0.05f);
        std::printf("  KINGPIN BASS 5: KICK 30 Hz %+.1f, OVERHEAD %+.2f; HATS TREBLE 5: 18 kHz %+.1f; OUTPUT +50: %+.2f dB\n", kb, ob, ht, og);
        CHECK(kb > 6.5 && kb < 7.6 && std::fabs(ob) < 0.05 && ht > 6.0 && ht < 7.6 && std::fabs(og - 10.0) < 0.05);
    }
    const double d = blockSizeDifference(*findPlugin("KINGPIN"), [](hc::fx::Effect& x) { x.set(Analog, 0); x.set(Compress, 70); x.set(Gate, -10); x.set(Fx, 50); x.set(Bass, 3); },
                                         noise(1.0, 0.8f));
    std::printf("  KINGPIN block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testGoldChain() {
    using namespace goldchain;
    {
        auto e = make("GOLD CHAIN", {{Analog, 0}});
        const double g = toneGain(*e, 1000, 0.3f);
        auto l = make("GOLD CHAIN", {{Input, 6}, {Analog, 0}});
        const double gl = toneGain(*l, 1000, 0.3f);
        std::printf("  GOLD CHAIN tone 0: 1 kHz %+.2f dB; INPUT +6 with LINK: %+.2f dB\n", g, gl);
        CHECK(std::fabs(g) < 0.1 && std::fabs(gl) < 0.1);
    }
    {
        auto a = make("GOLD CHAIN", {{Analog, 0}}), b = make("GOLD CHAIN", {{Tone, 100}, {Analog, 0}});
        const double h2a = harmonicDb(*a, 200, 2, 0.5f), h2b = harmonicDb(*b, 200, 2, 0.5f);
        const double lo = toneGain(*b, 60, 0.02f), hi = toneGain(*b, 14000, 0.02f), mid = toneGain(*b, 1000, 0.02f);
        std::printf("  GOLD CHAIN tone 100: 2nd harmonic %.1f -> %.1f dB; quiet 60 Hz %+.1f, 1 kHz %+.1f, 14 kHz %+.1f dB\n", h2a, h2b, lo, mid, hi);
        CHECK(h2b > h2a + 20.0 && lo > mid + 1.0 && hi > mid + 1.0);
    }
    const double d = blockSizeDifference(*findPlugin("GOLD CHAIN"), [](hc::fx::Effect& x) { x.set(Tone, 80); x.set(Analog, 0); x.set(Input, 4); },
                                         noise(1.0, 0.5f));
    std::printf("  GOLD CHAIN block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testIvories() {
    using namespace ivories;
    {
        auto e = make("IVORIES", {{Analog, 0}});
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  IVORIES at its defaults: largest change %.2e\n", diff);
        CHECK(diff < 1e-4);
    }
    {
        auto up = make("IVORIES", {{Analog, 0}, {Piano, 50}}), down = make("IVORIES", {{Analog, 0}, {Piano, -50}});
        const double u1 = toneGain(*up, 1000, 0.01f), d1 = toneGain(*down, 1000, 0.01f);
        const double uh = toneGain(*up, 8000, 0.01f) - u1, dh = toneGain(*down, 8000, 0.01f) - d1, db180 = toneGain(*down, 180, 0.01f) - d1;
        std::printf("  IVORIES PIANO, against 1 kHz: +50: 8 kHz %+.1f dB; -50: 8 kHz %+.1f, 180 Hz %+.1f dB\n", uh, dh, db180);
        CHECK(uh > 3.5 && dh < -3.5 && db180 > 2.5);
    }
    {
        auto e = make("IVORIES", {{Analog, 0}, {DelayOn, 1}, {DelayLevel, 0}});
        Stereo s = impulse(24000);
        run(*e, s);
        auto at = [&](const std::vector<float>& x, std::size_t from, std::size_t to) {
            double pk = 0;
            std::size_t where = 0;
            for (std::size_t i = from; i < to; ++i) if (std::fabs(x[i]) > pk) { pk = std::fabs(x[i]); where = i; }
            return where;
        };
        const std::size_t l = at(s.l, 2000, 20000) - 100, r = at(s.r, 2000, 20000) - 100;
        std::printf("  IVORIES DELAY: echo left after %.1f ms, right after %.1f ms\n", l / 48.0, r / 48.0);
        CHECK(l > 5700 && l < 5900 && r > 7400 && r < 7600);
    }
    {
        auto on = make("IVORIES", {{Analog, 0}, {DoublerOn, 1}}), off = make("IVORIES", {{Analog, 0}, {DoublerLevel, 0}});
        Stereo a = noise(0.5, 0.3f);
        a.r = a.l;
        Stereo b = a;
        run(*on, a);
        run(*off, b);
        double dOn = 0, dOff = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            dOn = std::max(dOn, static_cast<double>(std::fabs(a.l[i] - a.r[i])));
            dOff = std::max(dOff, static_cast<double>(std::fabs(b.l[i] - b.r[i])));
        }
        std::printf("  IVORIES DOUBLER on: left/right differ by up to %.3f; off (level up): %.1e\n", dOn, dOff);
        CHECK(dOn > 0.01 && dOff < 1e-6);
    }
}

void testHighRoller() {
    using namespace highroller;
    {
        auto e = make("HIGH ROLLER");
        double worst = 0;
        for (double hz : {50.0, 200.0, 1000.0, 4000.0, 12000.0}) worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.2f)));
        std::printf("  HIGH ROLLER at its defaults: tones 50 Hz..12 kHz within %.2f dB\n", worst);
        CHECK(worst < 0.6);
    }
    // GAIN lifts the quiet detail far more than the loud parts
    {
        auto e = make("HIGH ROLLER", {{band(2, Gain), 30}});
        const double quiet = toneGain(*e, 1000, 0.01f), loud = toneGain(*e, 1000, 0.5f);
        std::printf("  HIGH ROLLER MID GAIN 30: a -40 dBFS tone %+.1f dB, a -6 dBFS tone %+.1f dB\n", quiet, loud);
        CHECK(quiet > 4.0 && loud < quiet - 3.0);
        e->set(band(2, Bypass), 1);
        const double byp = toneGain(*e, 1000, 0.01f);
        std::printf("  HIGH ROLLER the same with BYPASS: %+.2f dB\n", byp);
        CHECK(std::fabs(byp) < 0.1);
    }
    // DIRECT off and no GAIN: almost silence; SOLO keeps only its band
    {
        auto e = make("HIGH ROLLER", {{Direct, -45}});
        const double g = toneGain(*e, 1000, 0.3f);
        auto s = make("HIGH ROLLER", {{band(0, Solo), 1}});
        const double lo = toneGain(*s, 50, 0.3f), hi = toneGain(*s, 4000, 0.3f);
        std::printf("  HIGH ROLLER DIRECT off: %.1f dB; LOW soloed: 50 Hz %+.1f, 4 kHz %.1f dB\n", g, lo, hi);
        CHECK(g < -20.0 && std::fabs(lo) < 0.8 && hi < -40.0);
    }
    // WIDTH per band; a crossover moved
    {
        auto e = make("HIGH ROLLER", {{band(2, Width), 0}});
        const double side = toneGain(*e, 1000, 0.3f, false, 1.5, true);
        auto w = make("HIGH ROLLER", {{band(2, Width), 3}});
        const double wide = toneGain(*w, 1000, 0.3f, false, 1.5, true);
        auto x = make("HIGH ROLLER", {{band(0, Solo), 1}, {Cross1, 1000}});
        const double moved = toneGain(*x, 300, 0.3f);
        std::printf("  HIGH ROLLER pure sides at 1 kHz: MID WIDTH 0 %.1f dB, 3 %+.1f dB; LOW soloed with its crossover at 1 kHz: 300 Hz %+.1f dB\n",
                    side, wide, moved);
        CHECK(side < -15.0 && wide > 8.0 && std::fabs(moved) < 1.0);
    }
    const double d = blockSizeDifference(*findPlugin("HIGH ROLLER"), [](hc::fx::Effect& x) { x.set(band(1, Gain), 20); x.set(band(3, Width), 2); x.set(Punch, 1); },
                                         noise(1.0, 0.5f));
    std::printf("  HIGH ROLLER block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testMiddleman() {
    using namespace middleman;
    {
        auto e = make("THE MIDDLEMAN");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i]) + std::fabs(s.r[i] - ref.r[i])));
        std::printf("  THE MIDDLEMAN at rest: largest change %.2e (the band split adds back to an all-pass, not the input)\n", diff);
        auto m = make("THE MIDDLEMAN", {{Center, -6}, {Low, 100}, {High, 50}, {Punch, 80}});
        Stereo t = noise(0.5, 0.3f);
        t.r = t.l;
        run(*m, t);
        double lr = 0;
        for (std::size_t i = 0; i < t.size(); ++i) lr = std::max(lr, static_cast<double>(std::fabs(t.l[i] - t.r[i])));
        std::printf("  THE MIDDLEMAN mono in: largest left/right difference %.2e; centre meter %.0f %%\n", lr, m->meter(0));
        CHECK(lr < 1e-6 && m->meter(0) > 99.0f);
    }
    {
        auto c = make("THE MIDDLEMAN", {{Center, -6}});
        auto s = make("THE MIDDLEMAN", {{Sides, 6}});
        auto off = make("THE MIDDLEMAN", {{Center, -60}});
        auto lo = make("THE MIDDLEMAN", {{Center, -60}, {Low, 100}});
        const double mono = toneGain(*c, 1000, 0.3f), side = toneGain(*s, 1000, 0.3f, false, 1.5, true), gone = toneGain(*off, 1000, 0.3f),
                     back = toneGain(*lo, 60, 0.3f);
        std::printf("  THE MIDDLEMAN: CENTER -6 on mono %+.1f dB, SIDES +6 on sides %+.1f dB, CENTER off %.0f dB, and LOW 100 brings 60 Hz back: %+.1f dB\n",
                    mono, side, gone, back);
        CHECK(std::fabs(mono + 6.0) < 0.2 && std::fabs(side - 6.0) < 0.2 && gone < -100.0 && std::fabs(back) < 0.5);
    }
    {
        auto e = make("THE MIDDLEMAN", {{Master, -12}});
        const double g = toneGain(*e, 1000, 0.3f);
        std::printf("  THE MIDDLEMAN MASTER -12: %+.2f dB\n", g);
        CHECK(std::fabs(g + 12.0) < 0.1);
    }
}

void testDoubleCross() {
    using namespace doublecross;
    // SPREAD 0: the input comes out exactly, kLatency later
    {
        auto e = make("DOUBLE CROSS", {{Spread, 0}});
        CHECK(e->latency() == kLatency);
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double worst = 0;
        for (std::size_t i = kLatency; i < s.size(); ++i)
            worst = std::max({worst, static_cast<double>(std::fabs(s.l[i] - ref.l[i - kLatency])), static_cast<double>(std::fabs(s.r[i] - ref.r[i - kLatency]))});
        std::printf("  DOUBLE CROSS spread 0: largest difference from the input %u samples earlier %.2e\n", kLatency, worst);
        CHECK(worst < 1e-5);
    }
    // spread: mono noise becomes wide, and folds back to the original mono exactly
    {
        auto e = make("DOUBLE CROSS", {{Spread, 1.0f}, {Sweeps, 8}});
        Stereo n = noise(1.5, 0.3f);
        n.r = n.l;
        const Stereo ref = n;
        run(*e, n);
        double lr = 0, ll = 0, rr = 0, fold = 0;
        for (std::size_t i = kLatency + 4800; i < n.size(); ++i) {
            lr += n.l[i] * n.r[i];
            ll += n.l[i] * n.l[i];
            rr += n.r[i] * n.r[i];
            fold = std::max(fold, static_cast<double>(std::fabs(0.5f * (n.l[i] + n.r[i]) - ref.l[i - kLatency])));
        }
        const double corr = lr / std::sqrt(ll * rr);
        std::printf("  DOUBLE CROSS spread 1, 8 sweeps: left/right correlation of mono noise %.2f; mono fold-down off by at most %.1e\n", corr, fold);
        CHECK(corr < 0.5 && fold < 1e-5);
    }
    // a tone sits where panAt says; LF SPREAD 0 keeps the lows in the middle
    {
        auto e = make("DOUBLE CROSS", {{Spread, 1.0f}, {Sweeps, 6}, {Freq, 300}});
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        double worst = 0;
        for (double hz : {1500.0, 3100.0, 7000.0}) {
            e->reset();
            Stereo s = sine(hz, 1.0, 0.2f);
            run(*e, s);
            const double l = toneAmp(s.l, hz, 24000, 12000), r = toneAmp(s.r, hz, 24000, 12000);
            const double want = panAt(v, hz), got = (l - r) / 0.4;   // l = 0.2 (1 + p), r = 0.2 (1 - p) when |p| <= 1
            worst = std::max(worst, std::fabs(std::fabs(got) - std::fabs(want)));
        }
        e->reset();
        Stereo b = sine(60, 1.5, 0.2f);
        run(*e, b);
        const double side = db(std::fabs(toneAmp(b.l, 60, 36000, 24000) - toneAmp(b.r, 60, 36000, 24000)) / 0.4 + 1e-9);
        std::printf("  DOUBLE CROSS tones lean as drawn (worst %.3f); 60 Hz under FREQ with LF SPREAD 0: sides %.0f dB\n", worst, side);
        CHECK(worst < 0.03 && side < -30.0);
    }
    // SPLIT square: hard left or right; the input side: polarity, swap, width 0, rotation
    {
        float v[kCount];
        auto e = make("DOUBLE CROSS", {{Kind, SplitMode}, {Shape, 1.0f}, {Sweeps, 4}});
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        double most = 0;
        for (double hz = 400; hz < 16000; hz *= 1.01) most = std::max(most, std::fabs(panAt(v, hz)));
        int hard = 0, all = 0;
        for (double hz = 400; hz < 16000; hz *= 1.01, ++all) hard += std::fabs(panAt(v, hz)) > 0.9 ? 1 : 0;
        std::printf("  DOUBLE CROSS SPLIT, SHAPE 1: %d of %d frequencies past 90 %% of the way to a side\n", hard, all);
        CHECK(hard > all * 0.7);
    }
    {
        auto e = make("DOUBLE CROSS", {{Spread, 0}, {Width, 0}});
        Stereo s = noise(0.5, 0.3f);
        run(*e, s);
        double lr = 0;
        for (std::size_t i = 0; i < s.size(); ++i) lr = std::max(lr, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  DOUBLE CROSS WIDTH 0: largest left/right difference %.1e\n", lr);
        CHECK(lr < 1e-6);
    }
    const double d = blockSizeDifference(*findPlugin("DOUBLE CROSS"), [](hc::fx::Effect& x) { x.set(Sweeps, 12); x.set(FDensity, 0.4f); x.set(Rotation, 10); },
                                         noise(0.8, 0.3f));
    std::printf("  DOUBLE CROSS block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
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
    testInsideMan();
    testBassMakers();
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
