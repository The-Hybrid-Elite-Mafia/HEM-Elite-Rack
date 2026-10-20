// The tone shop (EQ, channel strips, tone, stereo): what each plug-in must actually do to the
// sound, measured, plus the rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/rack/ToneShop.hpp"
#include "hem/rack/Comeback.hpp"
#include "hem/rack/Garage.hpp"
#include "hem/rack/Tools.hpp"
#include "hem/rack/Clone.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <chrono>
#include <thread>
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
double peakIn(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double m = 0;
    for (std::size_t i = from; i < to && i < x.size(); ++i) m = std::max(m, static_cast<double>(std::fabs(x[i])));
    return m;
}

void allRules() {
    for (const char* name : {"STRAIGHT SHOOTER", "BOTTOM LINE", "TEN TOES", "FOUR CORNERS", "SMOOTH TALKER", "INSIDE MAN", "LOW RIDER", "UNDERTOW", "DEEP POCKETS", "LOW PROFILE", "THE BOSS", "KINGPIN", "GOLD CHAIN", "IVORIES", "HIGH ROLLER", "THE MIDDLEMAN", "DOUBLE CROSS",
                             "CUTTHROAT", "TUNNEL VISION", "THE SETUP", "FOUNDRY", "BOARDROOM", "MIDTOWN", "HEAVY HITTER", "REEL MONEY", "HOTHEAD", "THE MOLD", "THE FORGER"}) {
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
        const double top = toneGain(*e, 20000, 0.01f);
        std::printf("  THE BOSS at rest, -40 dBFS: 40 Hz %+.2f, 1 kHz %+.2f, 15 kHz %+.2f, 20 kHz %+.2f dB\n", lo, g, hi, top);
        CHECK(std::fabs(top) < 0.2);   // the colour stage never dulls the top octave
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

void testTheSetup() {
    using namespace thesetup;
    auto peakAt = [](const std::vector<float>& x, std::size_t from, std::size_t to) {
        double pk = 0;
        std::size_t where = from;
        for (std::size_t i = from; i < to && i < x.size(); ++i)
            if (std::fabs(x[i]) > pk) { pk = std::fabs(x[i]); where = i; }
        return where;
    };
    // at rest the audio follows the drawn curve
    {
        auto e = make("THE SETUP", {{Freq, 1500}, {Res, 40}, {Slope, 1}});
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        double worst = 0;
        for (double hz : {200.0, 1000.0, 1500.0, 3000.0}) worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.05f) - curveDb(v, kFs, hz)));
        std::printf("  THE SETUP at rest (24 dB low-pass, 1.5 kHz, res 40): audio vs drawn curve, worst %.3f dB\n", worst);
        CHECK(worst < 0.1);
    }
    // the LFO sweeps the frequency: a 3 kHz tone through a 1 kHz low-pass comes and goes
    auto swing = [](hc::fx::Effect& e, double hz, double seconds) {
        e.reset();
        Stereo s = sine(hz, seconds, 0.2f);
        run(e, s);
        double lo = 1e9, hi = 0;
        for (std::size_t at = 24000; at + 960 < s.size(); at += 960) {
            const double r = rms(s.l, at, 960);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        return db(hi / std::max(lo, 1e-12));
    };
    {
        auto e = make("THE SETUP", {{Freq, 1000}, {LfoSync, 0}, {LfoRate, 2}, {depth(Lfo, ToFreq), 60}});
        auto still = make("THE SETUP", {{Freq, 1000}});
        const double moving = swing(*e, 3000, 2.0), rest = swing(*still, 3000, 2.0);
        std::printf("  THE SETUP LFO > FREQ 60 %%: a 3 kHz tone swings %.1f dB (without: %.2f dB)\n", moving, rest);
        CHECK(moving > 15.0 && rest < 0.5);
    }
    // the sequencer: steps +100 / -100 at 10 Hz
    {
        auto e = make("THE SETUP", {{Freq, 1000}, {SeqSync, 0}, {SeqRate, 10}, {Last, 2}, {Step1, 100}, {Step1 + 1, -100}, {depth(Seq, ToFreq), 40}});
        const double moving = swing(*e, 2000, 2.0);
        std::printf("  THE SETUP SEQUENCER two steps > FREQ 40 %%: a 2 kHz tone swings %.1f dB\n", moving);
        CHECK(moving > 15.0);
    }
    // the follower opens AMP: loud passes louder than quiet
    {
        auto e = make("THE SETUP", {{Kind, Amp}, {Gain, -40}, {depth(Follow, ToFreq), 100}});
        const double loud = toneGain(*e, 1000, 0.5f), quiet = toneGain(*e, 1000, 0.005f);
        std::printf("  THE SETUP AMP -40 with FOLLOW > 100 %%: a -6 dBFS tone %+.1f dB, a -46 dBFS tone %+.1f dB\n", loud, quiet);
        CHECK(loud > quiet + 20.0);
    }
    // the comb: peaks at 1 / DELAY
    {
        auto e = make("THE SETUP", {{Kind, Comb}, {CombDelay, 15}, {CombFeedback, 90}});
        const double on = toneGain(*e, 1000.0 / 15.0 * 3.0, 0.02f), off = toneGain(*e, 1000.0 / 15.0 * 3.5, 0.02f);
        std::printf("  THE SETUP COMB 15 ms, feedback 90: on a peak %+.1f dB, between %+.1f dB\n", on, off);
        CHECK(on > off + 12.0);
    }
    // the delay: left at TIME, right at 150 %
    {
        auto e = make("THE SETUP", {{Kind, Amp}, {DelaySync, 0}, {DelayTime, 200}, {DelayRight, 1}, {DelayMix, 100}});
        Stereo s = impulse(30000);
        run(*e, s);
        const std::size_t l = peakAt(s.l, 2000, 30000) - 100, r = peakAt(s.r, 2000, 30000) - 100;
        std::printf("  THE SETUP DELAY 200 ms, RIGHT 150 %%: left after %.1f ms, right after %.1f ms\n", l / 48.0, r / 48.0);
        CHECK(l > 9550 && l < 9650 && r > 14350 && r < 14450);
    }
    const double d = blockSizeDifference(*findPlugin("THE SETUP"),
                                         [](hc::fx::Effect& x) {
                                             x.set(Freq, 800); x.set(depth(Lfo, ToFreq), 50); x.set(depth(Seq, ToRes), 30); x.set(Crush, 30);
                                             x.set(DelayMix, 40); x.set(DelayFeedback, 40); x.set(DelayAnalog, 1);
                                         },
                                         noise(1.0, 0.3f));
    std::printf("  THE SETUP block size 512 vs 1: %.2e\n", d);
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
    // both filters off: untouched; the curves (left and right) are what the audio does
    {
        auto e = make("CUTTHROAT", {{HpOn, 0}, {LpOn, 0}, {RHpOn, 0}, {RLpOn, 0}});
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  CUTTHROAT both off: largest change %.2e\n", diff);
        CHECK(diff < 1e-6);
    }
    {
        auto e = make("CUTTHROAT", {{HpFreq, 200}, {LpFreq, 5000}, {Link, 0}, {RHpFreq, 60}, {RLpFreq, 12000}});
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = e->get(i);
        double worst = 0;
        for (double hz : {50.0, 100.0, 200.0, 1000.0, 5000.0, 9000.0}) {
            worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.1f) - curveDb(v, kFs, hz)));
            worst = std::max(worst, std::fabs(toneGain(*e, hz, 0.1f, true) - curveRightDb(v, kFs, hz)));
        }
        const double slope = curveDb(v, kFs, 50) - curveDb(v, kFs, 100);
        std::printf("  CUTTHROAT audio vs drawn curves (left, right): worst %.3f dB; the high-pass an octave down: %.1f dB per octave\n", worst, slope);
        CHECK(worst < 0.05 && slope < -15.0 && slope > -19.0);
        e->set(Link, 1);
        v[Link] = 1;
        const double linked = toneGain(*e, 100, 0.1f, true);
        std::printf("  CUTTHROAT linked: right side at 100 Hz %+.2f dB (left curve %+.2f)\n", linked, curveDb(v, kFs, 100));
        CHECK(std::fabs(linked - curveDb(v, kFs, 100)) < 0.05);
    }
    // a frequency jump glides: no sample steps out of line
    {
        auto e = make("CUTTHROAT", {{LpFreq, 20000}, {HpOn, 0}});
        Stereo a = sine(300, 0.3, 0.5f);
        run(*e, a);
        e->set(LpFreq, 400);
        Stereo c(14400);
        for (std::size_t i = 0; i < c.size(); ++i) c.l[i] = c.r[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 300.0 * (i + a.size()) / kFs));
        run(*e, c);
        float step = 0;
        for (std::size_t i = 1; i < c.size(); ++i) step = std::max(step, std::fabs(c.l[i] - c.l[i - 1]));
        std::printf("  CUTTHROAT low-pass 20 kHz -> 400 Hz while playing: largest sample step %.4f\n", step);
        CHECK(step < 0.05f);
    }
    const double d = blockSizeDifference(*findPlugin("CUTTHROAT"), [](hc::fx::Effect& x) { x.set(HpFreq, 300); x.set(LpFreq, 3000); x.set(Link, 0); },
                                         noise(0.6, 0.3f));
    std::printf("  CUTTHROAT block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testTunnelVision() {
    using namespace tunnelvision;
    {
        auto e = make("TUNNEL VISION");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        auto c = make("TUNNEL VISION", {{Filter, 5}});
        float v[kCount] = {5.0f, 0.0f};
        const double at1k = toneGain(*c, 1000, 0.1f), at5k = toneGain(*c, 5000, 0.1f);
        auto r = make("TUNNEL VISION", {{Filter, 5}, {Resonance, 3}});
        float vr[kCount] = {5.0f, 3.0f};
        const double edge = toneGain(*r, 1549.2, 0.05f);
        std::printf("  TUNNEL VISION open: largest change %.1e; FILTER 5 (1.55 kHz): 1 kHz %+.1f (drawn %+.1f), 5 kHz %.1f dB; RESONANCE HIGH at the edge %+.1f dB (drawn %+.1f)\n",
                    diff, at1k, curveDb(v, kFs, 1000), at5k, edge, curveDb(vr, kFs, 1549.2));
        CHECK(diff < 1e-6 && at5k < -30.0 && std::fabs(at1k - curveDb(v, kFs, 1000)) < 0.05 && edge > 10.0 && std::fabs(edge - curveDb(vr, kFs, 1549.2)) < 0.1);
    }
    const double d = blockSizeDifference(*findPlugin("TUNNEL VISION"), [](hc::fx::Effect& x) { x.set(Filter, 4); x.set(Resonance, 1); }, noise(0.6, 0.3f));
    std::printf("  TUNNEL VISION block size 512 vs 1: %.2e\n", d);
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

void testConsoleEqs() {
    {
        using namespace foundry;
        auto e = make("FOUNDRY");
        CHECK(e->latency() == kComebackEqLatency);
        const double g = toneGain(*e, 1000, 0.01f), lo = toneGain(*e, 40, 0.01f, false, 2.0), hi = toneGain(*e, 15000, 0.01f);
        std::printf("  FOUNDRY at rest, -40 dBFS: 40 Hz %+.2f, 1 kHz %+.2f, 15 kHz %+.2f dB\n", lo, g, hi);
        CHECK(std::fabs(g) < 0.1 && std::fabs(lo) < 0.1 && std::fabs(hi) < 0.1);
        auto q = make("FOUNDRY", {{LowFreq, 2}, {LowGain, 12}, {MidFreq, 4}, {MidGain, 12}, {HighFreq, 2}, {HighGain, -12}});
        const double l = toneGain(*q, 20, 0.01f, false, 2.0), m = toneGain(*q, 1600, 0.01f), h = toneGain(*q, 20000, 0.01f);
        q->set(EqIn, 0);
        const double off = toneGain(*q, 1600, 0.01f);
        std::printf("  FOUNDRY LOWS +12 at 60 -> 20 Hz %+.1f; MIDDLE +12 at 1.6K %+.1f; HIGHS -12 at 12K -> 20 kHz %+.1f; EQ out %+.2f dB\n", l, m, h, off);
        CHECK(l > 10.5 && l < 12.3 && std::fabs(m - 12.0) < 0.3 && h < -10.0 && h > -12.5 && std::fabs(off) < 0.1);
        auto c = make("FOUNDRY", {{LowCut, 4}});
        const double at = toneGain(*c, 80, 0.01f, false, 2.0), oct = toneGain(*c, 40, 0.01f, false, 2.0), keep = toneGain(*c, 1000, 0.01f);
        std::printf("  FOUNDRY LOW CUT 80: 80 Hz %+.1f, 40 Hz %+.1f, 1 kHz %+.2f dB\n", at, oct, keep);
        CHECK(std::fabs(at + 3.0) < 0.5 && oct < -14.0 && oct > -20.0 && std::fabs(keep) < 0.1);
        // FLIP turns the sound upside down
        auto a = make("FOUNDRY", {{Vintage, 0}}), b = make("FOUNDRY", {{Vintage, 0}, {Phase, 1}});
        Stereo x = noise(0.3, 0.2f), y = x;
        run(*a, x);
        run(*b, y);
        double worst = 0;
        for (std::size_t i = 0; i < x.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(x.l[i] + y.l[i])));
        std::printf("  FOUNDRY FLIP: largest |a + flipped| %.1e\n", worst);
        CHECK(worst < 1e-5);
        // VINTAGE: a hot tone gets harmonics, off = none
        auto v1 = make("FOUNDRY"), v0 = make("FOUNDRY", {{Vintage, 0}});
        const double h1 = harmonicDb(*v1, 200, 2, 0.8f), h0 = harmonicDb(*v0, 200, 2, 0.8f);
        std::printf("  FOUNDRY 2nd harmonic of a hot 200 Hz tone: vintage %.1f dB, off %.1f dB\n", h1, h0);
        CHECK(h1 > -50.0 && h0 < -90.0);
        // readouts: a -6 dBFS peak reads PEAK about -6, TO TOP about +5.9
        auto r = make("FOUNDRY", {{Vintage, 0}});
        Stereo t = sine(1000, 0.5, 0.5f);
        run(*r, t);
        std::printf("  FOUNDRY readouts on a 0.5 sine: PEAK %.2f dBFS, TO TOP %+.2f dB, CLIPS %.0f\n", r->meter(0), r->meter(1), r->meter(2));
        CHECK(std::fabs(r->meter(0) + 6.02) < 0.1 && std::fabs(r->meter(1) - 5.92) < 0.1 && r->meter(2) == 0.0f);
    }
    {
        using namespace boardroom;
        auto e = make("BOARDROOM");
        const double g = toneGain(*e, 1000, 0.01f), hi = toneGain(*e, 15000, 0.01f);
        CHECK(std::fabs(g) < 0.1 && std::fabs(hi) < 0.1);
        auto f = make("BOARDROOM", {{HighPass, 3}, {LowPass, 3}});
        const double hp = toneGain(*f, 82, 0.01f, false, 2.0), lp = toneGain(*f, 8700, 0.01f), mid = toneGain(*f, 1000, 0.01f);
        std::printf("  BOARDROOM HIGH-PASS 82 at 82 Hz %+.1f, LOW-PASS 8.7K at 8.7 kHz %+.1f, 1 kHz %+.2f dB\n", hp, lp, mid);
        CHECK(std::fabs(hp + 3.0) < 0.5 && std::fabs(lp + 3.0) < 0.6 && std::fabs(mid) < 0.15);
        auto w = make("BOARDROOM", {{LoMidFreq, 4}, {LoMidGain, 18}}), t = make("BOARDROOM", {{LoMidFreq, 4}, {LoMidGain, 18}, {LoMidTight, 1}});
        const double wide = toneGain(*w, 390, 0.01f), tight = toneGain(*t, 390, 0.01f), wideOct = toneGain(*w, 780, 0.01f), tightOct = toneGain(*t, 780, 0.01f);
        std::printf("  BOARDROOM LOW MIDS +18 at 390: wide %+.1f (an octave up %+.1f), tight %+.1f (an octave up %+.1f) dB\n", wide, wideOct, tight, tightOct);
        CHECK(std::fabs(wide - 12.0) < 0.3 && std::fabs(tight - 18.0) < 0.3 && tightOct < wideOct);
        auto sh = make("BOARDROOM", {{HighFreq, 3}, {HighGain, 10}}), bl = make("BOARDROOM", {{HighFreq, 3}, {HighGain, 10}, {HighBell, 1}});
        const double sh20 = toneGain(*sh, 20000, 0.01f), bl20 = toneGain(*bl, 20000, 0.01f), bl10 = toneGain(*bl, 10000, 0.01f);
        std::printf("  BOARDROOM HIGHS +10 at 10K: shelf at 20 kHz %+.1f; bell at 10 kHz %+.1f, at 20 kHz %+.1f dB\n", sh20, bl10, bl20);
        CHECK(sh20 > 9.0 && std::fabs(bl10 - 10.0) < 0.3 && bl20 < sh20 - 3.0);
        auto lo = make("BOARDROOM", {{LowFreq, 3}, {LowGain, -12}});
        const double l = toneGain(*lo, 25, 0.01f, false, 2.0);
        lo->set(LowFreq, 0);
        const double lOff = toneGain(*lo, 25, 0.01f, false, 2.0);
        std::printf("  BOARDROOM LOWS -12 at 100: 25 Hz %+.1f dB; LOWS AT OFF %+.2f dB\n", l, lOff);
        CHECK(l < -10.0 && std::fabs(lOff) < 0.1);
    }
    {
        using namespace midtown;
        auto e = make("MIDTOWN");
        CHECK(std::fabs(toneGain(*e, 1000, 0.01f)) < 0.1 && std::fabs(toneGain(*e, 100, 0.01f)) < 0.1);
        auto q = make("MIDTOWN", {{LowFreq, 2}, {LowPeak, 10}});
        const double lp = toneGain(*q, 500, 0.01f);
        auto d = make("MIDTOWN", {{DipFreq, 7}, {Dip, 10}});
        const double dp = toneGain(*d, 3000, 0.01f);
        auto h = make("MIDTOWN", {{HighFreq, 2}, {HighPeak, 10}});
        const double hp = toneGain(*h, 3000, 0.01f);
        h->set(EqIn, 0);
        const double off = toneGain(*h, 3000, 0.01f);
        std::printf("  MIDTOWN LOW PEAK 10 at 500 %+.1f, DIP 10 at 3K %+.1f, HIGH PEAK 10 at 3K %+.1f, EQ out %+.2f dB\n", lp, dp, hp, off);
        CHECK(std::fabs(lp - 10.0) < 0.3 && std::fabs(dp + 10.0) < 0.3 && std::fabs(hp - 8.0) < 0.3 && std::fabs(off) < 0.1);
        // the plate's curve is what the audio does
        auto all = make("MIDTOWN", {{LowPeak, 6}, {Dip, 4}, {DipFreq, 5}, {HighPeak, 7}, {HighFreq, 4}});
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = all->get(i);
        double worst = 0;
        for (double hz : {150.0, 500.0, 1500.0, 5000.0, 12000.0})
            worst = std::max(worst, std::fabs(toneGain(*all, hz, 0.01f) - kCurve.db(v, kFs, hz)));
        std::printf("  MIDTOWN curve vs the sound: largest difference %.2f dB\n", worst);
        CHECK(worst < 0.15);
    }
}

void testHeavyHitter() {
    using namespace heavyhitter;
    {   // at rest: untouched
        auto e = make("HEAVY HITTER");
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  HEAVY HITTER at rest: largest change %.1e\n", d);
        CHECK(d < 1e-6);
    }
    {   // THUMP +12 on KICK lifts 60 Hz 12 dB; on HATS it's a low shelf; TREBLE +12 on SNARE TOP lifts the top
        auto k = make("HEAVY HITTER", {{Drum, Kick}, {Thump, 12}});
        const double k60 = toneGain(*k, 60, 0.05f, false, 2.0), k1k = toneGain(*k, 1000, 0.05f);
        auto h = make("HEAVY HITTER", {{Drum, Hats}, {Thump, -12}});
        const double h50 = toneGain(*h, 50, 0.05f, false, 2.0);
        auto t = make("HEAVY HITTER", {{Drum, SnareTop}, {Treble, 12}});
        const double t15 = toneGain(*t, 15000, 0.05f);
        std::printf("  HEAVY HITTER kick thump +12: 60 Hz %+.1f, 1 kHz %+.2f; hats thump -12: 50 Hz %+.1f; snare treble +12: 15 kHz %+.1f dB\n",
                    k60, k1k, h50, t15);
        CHECK(std::fabs(k60 - 12.0) < 0.3 && std::fabs(k1k) < 0.3 && h50 < -10.5 && t15 > 10.5);
    }
    {   // SNAP: the front of a hit gets louder, the steady part does not
        auto hits = [&](float snap) {
            auto e = make("HEAVY HITTER", {{Drum, SnareTop}, {Snap, snap}});
            Stereo s(48000);
            for (std::size_t i = 4800; i < s.size(); ++i) s.l[i] = s.r[i] = 0.2f * static_cast<float>(std::sin(2.0 * 3.14159265 * 200.0 * static_cast<double>(i) / kFs));
            run(*e, s);
            return std::pair<double, double>{db(rms(s.l, 4800, 240)), db(rms(s.l, 24000, 4800))};
        };
        const auto a = hits(0), b = hits(100);
        std::printf("  HEAVY HITTER snap 100: the first 5 ms %+.1f dB, the steady part %+.2f dB\n", b.first - a.first, b.second - a.second);
        CHECK(b.first - a.first > 4.0 && std::fabs(b.second - a.second) < 0.3);
    }
    {   // SENSITIVITY / OUTPUT, and the meters
        auto e = make("HEAVY HITTER", {{Sensitivity, 6}, {Output, -6}, {Meter, 0}});
        const double g = toneGain(*e, 1000, 0.1f);
        Stereo s = sine(1000, 0.5, 0.1f);
        e->reset();
        run(*e, s);
        std::printf("  HEAVY HITTER sensitivity +6, output -6: %+.2f dB; meter IN %.1f dBFS, INPUT %.1f dBFS\n", g, e->meter(0), e->meter(1));
        CHECK(std::fabs(g) < 0.05 && std::fabs(e->meter(0) - (-20.0 + 6.0)) < 0.3 && std::fabs(e->meter(1) - e->meter(0)) < 0.01);
    }
}

void testReelMoney() {
    using namespace reelmoney;
    {   // at rest (no wobble): flat in the middle, the latency exact in both monitor modes
        auto e = make("REEL MONEY", {{Wobble, 0}});
        CHECK(e->latency() == kReelLatency);
        const double mid = toneGain(*e, 1000, 0.01f), top = toneGain(*e, 15000, 0.01f), bump = toneGain(*e, 50, 0.01f, false, 2.0);
        std::printf("  REEL MONEY at rest (15 IPS, hot bias): 1 kHz %+.2f, 15 kHz %+.2f, 50 Hz %+.2f dB\n", mid, top, bump);
        CHECK(std::fabs(mid) < 0.3 && top < 0.0 && top > -4.0 && bump > 0.5);
        for (float mon : {0.0f, 1.0f}) {
            auto x = make("REEL MONEY", {{Wobble, 0}, {Monitor, mon}});
            Stereo s = impulse(4800);
            run(*x, s);
            std::size_t at = 0;
            for (std::size_t i = 0; i < s.size(); ++i)
                if (std::fabs(s.l[i]) > std::fabs(s.l[at])) at = i;
            std::printf("  REEL MONEY monitor %s: the click comes out at %zu (expected %u)\n", mon < 0.5f ? "TAPE" : "INPUT", at, 100 + kReelLatency);
            CHECK(at >= 100 + kReelLatency - 2 && at <= 100 + kReelLatency + 2);
        }
    }
    {   // SPEED 7.5 loses more top than 15; HOT bias softer than NORMAL
        auto s75 = make("REEL MONEY", {{Wobble, 0}, {Speed, 0}}), s15 = make("REEL MONEY", {{Wobble, 0}, {Speed, 1}});
        auto nb = make("REEL MONEY", {{Wobble, 0}, {Bias, 0}});
        const double a = toneGain(*s75, 15000, 0.01f), b = toneGain(*s15, 15000, 0.01f), c = toneGain(*nb, 12000, 0.01f), d = toneGain(*s15, 12000, 0.01f);
        std::printf("  REEL MONEY 15 kHz: 7.5 IPS %+.1f, 15 IPS %+.1f dB; 12 kHz: normal bias %+.1f, hot %+.1f dB\n", a, b, c, d);
        CHECK(a < b - 3.0 && c > d + 0.8);
    }
    {   // TAPE LEVEL: more = more saturation, the quiet level the same; INTO TAPE with LINK keeps the level
        auto t0 = make("REEL MONEY", {{Wobble, 0}}), t1 = make("REEL MONEY", {{Wobble, 0}, {TapeLevel, 1020}});
        const double q0 = toneGain(*t0, 1000, 0.003f), q1 = toneGain(*t1, 1000, 0.003f);
        const double h0 = harmonicDb(*t0, 200, 3, 0.25f), h1 = harmonicDb(*t1, 200, 3, 0.25f);
        auto li = make("REEL MONEY", {{Wobble, 0}, {IntoTape, 12}});
        const double ql = toneGain(*li, 1000, 0.003f);
        std::printf("  REEL MONEY tape level 1020: quiet %+.2f (185: %+.2f) dB, 3rd harmonic %.1f -> %.1f dB; into tape +12 linked %+.2f dB\n", q1, q0, h0,
                    h1, ql);
        CHECK(std::fabs(q1 - q0) < 0.2 && h1 > h0 + 10.0 && std::fabs(ql - q0) < 0.3);
    }
    {   // HISS 0 dB on silence: about -62 dBFS RMS; OFF: silence
        auto h = make("REEL MONEY", {{Hiss, 0}}), off = make("REEL MONEY");
        Stereo a(96000), b(96000);
        run(*h, a);
        run(*off, b);
        const double lv = db(rms(a.l, 48000, 48000));
        std::printf("  REEL MONEY hiss 0 dB: %.1f dBFS RMS; off: peak %.1e\n", lv, peakIn(b.l, 0, b.size()));
        CHECK(lv > -68.0 && lv < -56.0 && peakIn(b.l, 0, b.size()) < 1e-7);
    }
    {   // the echo: SLAP = one repeat at ECHO TIME, REPEATS = more; MONITOR INPUT = no echo
        auto echoAt = [&](float style, float mon, double ms) {
            auto e = make("REEL MONEY", {{Wobble, 0}, {EchoLevel, 100}, {EchoTime, 100}, {EchoStyle, style}, {EchoTone, 16000}, {Monitor, mon}});
            Stereo s = impulse(48000);
            run(*e, s);
            const std::size_t at = 100 + kReelLatency + static_cast<std::size_t>(ms * 48.0);
            return peakIn(s.l, at - 48, at + 96);
        };
        const double s1 = echoAt(0, 0, 100), s2 = echoAt(0, 0, 200), r2 = echoAt(1, 0, 200), in1 = echoAt(0, 1, 100);
        std::printf("  REEL MONEY echo 100 ms: slap 1st %.3f, 2nd %.1e; repeats 2nd %.3f; monitor input 1st %.1e\n", s1, s2, r2, in1);
        CHECK(s1 > 0.2 && s2 < 1e-3 && r2 > 0.1 && in1 < 1e-3);
    }
    {   // WOBBLE moves the pitch
        auto spread = [&](float w) {
            auto e = make("REEL MONEY", {{Wobble, w}});
            Stereo s = sine(1000, 3.0, 0.1f);
            run(*e, s);
            std::vector<double> cross;
            for (std::size_t i = 4801; i < s.size(); ++i)
                if (s.l[i - 1] < 0.0f && s.l[i] >= 0.0f) cross.push_back(static_cast<double>(i - 1) + s.l[i - 1] / (s.l[i - 1] - s.l[i]));
            double m = 0, v = 0;
            for (std::size_t i = 1; i < cross.size(); ++i) m += cross[i] - cross[i - 1];
            m /= static_cast<double>(cross.size() - 1);
            for (std::size_t i = 1; i < cross.size(); ++i) v += (cross[i] - cross[i - 1] - m) * (cross[i] - cross[i - 1] - m);
            return std::sqrt(v / static_cast<double>(cross.size() - 1));
        };
        const double w0 = spread(0), w100 = spread(100);
        std::printf("  REEL MONEY wobble: period spread 0 -> %.4f, 100 -> %.4f samples\n", w0, w100);
        CHECK(w100 > 10.0 * std::max(w0, 1e-4));
    }
    {   // VU: a -18 dBFS RMS tone reads 0 VU with VU ZERO at -18
        auto e = make("REEL MONEY", {{Wobble, 0}, {Meter, 0}});
        Stereo s = sine(1000, 1.0, 0.1778f);
        run(*e, s);
        std::printf("  REEL MONEY VU in on a -18 dBFS RMS tone: %+.2f VU\n", e->meter(0));
        CHECK(std::fabs(e->meter(0)) < 0.3);
    }
}

void testHothead() {
    using namespace hothead;
    {   // DRIVE 0: untouched, kLatency late
        auto e = make("HOTHEAD");
        CHECK(e->latency() == kLatency);
        Stereo s = noise(0.5, 0.5f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = kLatency; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i - kLatency])));
        std::printf("  HOTHEAD drive 0: largest change %.1e\n", d);
        CHECK(d < 1e-5);
    }
    {   // more DRIVE = more harmonics; a -12 dBFS tone comes out within a few dB; the top darkens
        double prev = -300;
        bool rising = true;
        for (float d : {2.0f, 5.0f, 10.0f}) {
            auto e = make("HOTHEAD", {{Drive, d}});
            const double h = harmonicDb(*e, 200, 3, 0.25f);
            rising = rising && h > prev + 3.0;
            prev = h;
        }
        auto e = make("HOTHEAD", {{Drive, 10}});
        const double lv = toneGain(*e, 400, 0.25f), top = toneGain(*e, 10000, 0.002f) - toneGain(*e, 1000, 0.002f);
        std::printf("  HOTHEAD drive 10: 3rd harmonic %.1f dB; a -12 dBFS tone %+.1f dB; quiet 10 kHz vs 1 kHz %+.1f dB\n", prev, lv, top);
        CHECK(rising && prev > -20.0 && std::fabs(lv) < 4.0 && top < -15.0);
    }
}

} // namespace

// THE MOLD measures a "hardware" EQ in a loop (a bell +6 dB at 1 kHz and a low shelf -4 dB at 120 Hz, 300 samples
// of extra delay), THE FORGER plays what it measured; HOLD keeps it, ADD stacks the box's moves on top, CLEAR flattens.
void testClone() {
    using namespace hem::rack;
    auto mold = make("THE MOLD", {{mold::Transmit, 1}});
    hc::fx::Biquad bell, shelf;
    bell.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::Bell, kFs, 1000.0, 1.0, 6.0);
    shelf.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::LowShelf, kFs, 120.0, 0.7071, -4.0);
    hc::fx::DelayLine wire;
    wire.prepare(400);
    constexpr std::uint32_t kBlk = 256;
    std::vector<float> back(kBlk, 0.0f), l(kBlk), r(kBlk);
    // the loop: what the MOLD sends comes back through the box (in a DAW the return arrives on its left input)
    auto loopFor = [&](double seconds) {
        for (int b = 0; b < static_cast<int>(seconds * kFs / kBlk); ++b) {
            for (std::uint32_t i = 0; i < kBlk; ++i) { l[i] = back[i]; r[i] = 0.0f; }
            mold->process(l.data(), r.data(), kBlk);
            for (std::uint32_t i = 0; i < kBlk; ++i) back[i] = shelf.tick(bell.tick(wire.process(l[i], 300)));
        }
    };
    loopFor(2.0);
    float curve[clone::kPoints];
    const bool fresh = clone::latestCurve(curve, nullptr);
    auto at = [&](double hz) {
        int best = 0;
        for (int i = 0; i < clone::kPoints; ++i)
            if (std::fabs(std::log(clone::pointHz(i) / hz)) < std::fabs(std::log(clone::pointHz(best) / hz))) best = i;
        return static_cast<double>(curve[best]);
    };
    const double want1k = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::Bell, kFs, 1000.0, 1.0, 6.0).magnitudeDb(kFs, clone::pointHz(0) * 0 + 1000.0);
    std::printf("  THE MOLD: receiving %.0f, loop %.2f ms (300 + one 256 block = %.2f ms); curve at 60 Hz %+.2f, 1 kHz %+.2f (box %+.2f), 5 kHz %+.2f dB\n",
                static_cast<double>(mold->meter(mold::Receiving)), static_cast<double>(mold->meter(mold::LoopMs)), 556.0 / 48.0, at(60.0), at(1000.0),
                want1k, at(5000.0));
    CHECK(fresh && mold->meter(mold::Receiving) > 0.5f && std::fabs(mold->meter(mold::LoopMs) - 556.0f / 48.0f) < 0.05f);
    CHECK(std::fabs(at(1000.0) - 6.0) < 0.6 && std::fabs(at(60.0) + 4.0) < 0.8 && std::fabs(at(5000.0)) < 0.6);
    // THE FORGER in CAPTURE plays that curve (its background thread makes the FIR: give it time while the loop keeps running)
    auto forger = make("THE FORGER", {{forger::Mode, forger::Capture}});
    auto gainAt = [&](double hz) {
        Stereo s = sine(hz, 0.6, 0.1f);
        run(*forger, s, 256);
        return db(toneAmp(s.l, hz, s.size() - 9600, 9600) / 0.1);
    };
    for (int k = 0; k < 20; ++k) {
        loopFor(0.05);
        Stereo s(2560);
        run(*forger, s, 256);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    loopFor(0.2);
    const double g1k = gainAt(1000.0), g60 = gainAt(60.0), g5k = gainAt(5000.0);
    std::printf("  THE FORGER CAPTURE: 60 Hz %+.2f, 1 kHz %+.2f, 5 kHz %+.2f dB; receiving %.0f, latency %u\n", g60, g1k, g5k,
                static_cast<double>(forger->meter(forger::Receiving)), forger->latency());
    CHECK(std::fabs(g1k - 6.0) < 0.8 && std::fabs(g60 + 4.0) < 1.2 && std::fabs(g5k) < 0.8 && forger->latency() == 256);
    // HOLD: the curve goes into its own parameters (saved with the session)
    forger->set(forger::Mode, forger::Hold);
    {
        Stereo s(256);
        run(*forger, s, 256);
    }
    int i1k = 0;
    for (int i = 0; i < clone::kPoints; ++i)
        if (std::fabs(std::log(clone::pointHz(i) / 1000.0)) < std::fabs(std::log(clone::pointHz(i1k) / 1000.0))) i1k = i;
    const float held = forger->get(forger::Curve0 + i1k);
    // ADD: the box's moves (still +6 at 1 kHz) on top of the held curve
    forger->set(forger::Mode, forger::Add);
    for (int k = 0; k < 15; ++k) {
        loopFor(0.05);
        Stereo s(2560);
        run(*forger, s, 256);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double added = gainAt(1000.0);
    // HOLD again (the sum is kept), then CLEAR: flat
    forger->set(forger::Mode, forger::Hold);
    {
        Stereo s(256);
        run(*forger, s, 256);
    }
    const float heldSum = forger->get(forger::Curve0 + i1k);
    forger->set(forger::Clear, 1);
    for (int k = 0; k < 10; ++k) {
        Stereo s(2560);
        run(*forger, s, 256);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const double flat = gainAt(1000.0);
    std::printf("  THE FORGER HOLD keeps %+.2f dB at 1 kHz; ADD: %+.2f dB; HOLD keeps %+.2f; CLEAR: %+.2f dB\n", static_cast<double>(held), added,
                static_cast<double>(heldSum), flat);
    CHECK(std::fabs(held - 6.0f) < 0.6f && std::fabs(added - 12.0) < 1.0 && std::fabs(heldSum - 12.0f) < 1.0f && std::fabs(flat) < 0.3);
}

void testToneShop() {
    std::printf("[the tone shop]\n");
    allRules();
    testClone();
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
    testTunnelVision();
    testTheSetup();
    testWideLoadAdditions();
    testConsoleEqs();
    testHeavyHitter();
    testReelMoney();
    testHothead();
}

} // namespace hem::test
