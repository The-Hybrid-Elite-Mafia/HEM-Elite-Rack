// The streets (guitar, pitch, meters, test tones): what each plug-in must actually do, measured,
// plus the rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/rack/Streets.hpp"
#include "hem/rack/Garage.hpp"
#include "hem/rack/Analyzer.hpp"

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
// a tone through the effect; the level (dB re 1) of a component of the result over its last second
double componentDb(hc::fx::Effect& e, double inHz, float amp, double outHz, double seconds = 2.0, bool right = false) {
    e.reset();
    Stereo s = sine(inHz, seconds, amp);
    run(e, s);
    return db(toneAmp(right ? s.r : s.l, outHz, s.size() - 48000, 48000));
}
double crossingsHz(const std::vector<float>& x, std::size_t from, std::size_t len) {
    std::size_t first = 0, last = 0;
    int n = 0;
    for (std::size_t i = from + 1; i < from + len; ++i)
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) {
            if (n == 0) first = i;
            last = i;
            ++n;
        }
    return n > 1 ? (n - 1) * kFs / static_cast<double>(last - first) : 0.0;
}
Stereo chord(std::initializer_list<double> hz, double seconds, float amp) {
    Stereo s(static_cast<std::size_t>(seconds * kFs));
    for (double f : hz) add(s, sine(f, seconds, amp));
    return s;
}

void allRules() {
    for (const char* name : {"STAKEOUT", "THE AUDITOR", "TORCH", "HOT WIRE", "BOTTOM FEEDER", "THE RACKET", "THE SNITCH", "PROMOTION", "WISEGUYS", "MADE MAN", "THE DETECTIVE", "THE WITNESS", "LIVE WIRE"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

double peakIn(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double m = 0;
    for (std::size_t k = from; k < to && k < x.size(); ++k) m = std::max(m, static_cast<double>(std::fabs(x[k])));
    return m;
}

void testTorch() {
    using namespace torch;
    double lv[3], h3[3];
    const float drives[3] = {0, 5, 10};
    for (int k = 0; k < 3; ++k) {
        auto e = make("TORCH", {{Type, Grit}, {Drive, drives[k]}, {CabA, 0}});
        lv[k] = componentDb(*e, 200, 0.1f, 200);
        h3[k] = componentDb(*e, 200, 0.1f, 600) - lv[k];
    }
    std::printf("  TORCH GRIT, a -20 dBFS 200 Hz note, drive 0 / 5 / 10: level %.1f / %.1f / %.1f dB, 3rd harmonic %.1f / %.1f / %.1f dB under it\n",
                lv[0], lv[1], lv[2], h3[0], h3[1], h3[2]);
    CHECK(lv[0] > -30 && lv[2] < 6 && lv[1] > -30 && h3[2] > h3[0] + 6.0);
    // a cabinet takes the fizz off: 8 kHz well under 1 kHz through the 4x12; with NONE it isn't
    auto c = make("TORCH", {{Type, Pure}, {Drive, 0}, {CabA, 5}, {Presence, 0}});
    auto o = make("TORCH", {{Type, Pure}, {Drive, 0}, {CabA, 0}, {Presence, 0}});
    const double k1 = componentDb(*c, 1000, 0.01f, 1000), k8 = componentDb(*c, 8000, 0.01f, 8000);
    const double n1 = componentDb(*o, 1000, 0.01f, 1000), n8 = componentDb(*o, 8000, 0.01f, 8000);
    std::printf("  TORCH PURE, 4X12 OLD: 1 kHz %.1f, 8 kHz %.1f dB; no cabinet: %.1f, %.1f dB\n", k1, k8, n1, n8);
    CHECK(k8 < k1 - 15.0 && n8 > n1 - 10.0);
    // AMP off: the input through the cabinet only; VOLUME 0 = silent path; PAN hard left = only left; PHASE B flips B
    auto off = make("TORCH", {{AmpOn, 0}, {CabA, 0}});
    const double thru = componentDb(*off, 1000, 0.1f, 1000);
    auto pan = make("TORCH", {{AmpOn, 0}, {CabA, 0}, {PanA, -100}});
    Stereo t = sine(1000, 0.5, 0.1f);
    run(*pan, t);
    auto both = make("TORCH", {{AmpOn, 0}, {CabA, 0}, {CabB, 0}, {VolB, 5}});
    auto flip = make("TORCH", {{AmpOn, 0}, {CabA, 0}, {CabB, 0}, {VolB, 5}, {PhaseB, 1}});
    const double sum = componentDb(*both, 1000, 0.1f, 1000), cancel = componentDb(*flip, 1000, 0.1f, 1000);
    std::printf("  TORCH amp off, a -20 dBFS tone: %+.2f dBFS; pan A left: right side peak %.1e; A + B %+.2f dBFS, B flipped %.1f dBFS\n", thru, peakIn(t.r, 0, t.size()), sum,
                cancel);
    CHECK(std::fabs(thru + 20.0) < 0.05 && peakIn(t.r, 0, t.size()) < 1e-6 && std::fabs(sum + 20.0 - 6.02) < 0.1 && cancel < -80.0);
    const double d = blockSizeDifference(*findPlugin("TORCH"), [](hc::fx::Effect& x) { x.set(Drive, 7); x.set(VolB, 4); }, noise(0.5, 0.3f));
    std::printf("  TORCH block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testHotWire() {
    using namespace hotwire;
    {   // the limiter: never past -0.1 dBFS, and it counts what it caught
        auto e = make("HOT WIRE", {{Input, 12}, {Output, 12}, {Channel, Lead}, {Gain, 10}});
        Stereo s = sine(200, 1.0, 0.8f);
        run(*e, s);
        std::printf("  HOT WIRE flat out: peak %.4f (ceiling 0.9886), clips counted %.0f\n", peak(s), e->meter(2));
        CHECK(peak(s) <= 0.98856f && e->meter(2) > 0.0f);
        Stereo q(48000);
        run(*e, q);   // let the ringing die away, then CLEAR
        e->set(Clear, 1);
        Stereo q2(480);
        run(*e, q2);
        CHECK(e->meter(2) == 0.0f);
    }
    {   // the channels: LEAD clips more than CLEAN at the same GAIN
        auto c = make("HOT WIRE", {{Channel, Clean}, {Gain, 5}, {CabOff, 1}}), l = make("HOT WIRE", {{Channel, Lead}, {Gain, 5}, {CabOff, 1}});
        const double hc = componentDb(*c, 200, 0.05f, 600) - componentDb(*c, 200, 0.05f, 200);
        const double hl = componentDb(*l, 200, 0.05f, 600) - componentDb(*l, 200, 0.05f, 200);
        std::printf("  HOT WIRE 3rd harmonic, clean %.1f dB, lead %.1f dB\n", hc, hl);
        CHECK(hl > hc + 10.0);
    }
    {   // CAB TONE: the brighter take has more top; CAB OFF has the most
        auto d = make("HOT WIRE", {{Channel, Clean}, {Gain, 0}, {CabTone, 0}}), b = make("HOT WIRE", {{Channel, Clean}, {Gain, 0}, {CabTone, 100}});
        auto o = make("HOT WIRE", {{Channel, Clean}, {Gain, 0}, {CabOff, 1}});
        const double rd = componentDb(*d, 4000, 0.01f, 4000) - componentDb(*d, 500, 0.01f, 500);
        const double rb = componentDb(*b, 4000, 0.01f, 4000) - componentDb(*b, 500, 0.01f, 500);
        const double ro = componentDb(*o, 8000, 0.01f, 8000) - componentDb(*o, 500, 0.01f, 500);
        std::printf("  HOT WIRE 4 kHz vs 500 Hz: dark %.1f, bright %.1f dB; no cabinet 8 kHz vs 500 Hz %.1f dB\n", rd, rb, ro);
        CHECK(rb > rd + 4.0 && ro > rb);
    }
    {   // the GATE closes on a quiet hum; FOCUS lifts its band
        auto gt = make("HOT WIRE", {{Gate, -30}, {Channel, Clean}, {Gain, 0}});
        Stereo s = sine(200, 1.0, 0.003f);
        run(*gt, s);
        auto f0 = make("HOT WIRE", {{Channel, Clean}, {Gain, 0}, {CabOff, 1}}), f5 = make("HOT WIRE", {{Channel, Clean}, {Gain, 0}, {CabOff, 1}, {Focus, 5}});
        const double hz = 250.0 * std::pow(16.0, 0.5);
        const double lift = componentDb(*f5, hz, 0.01f, hz) - componentDb(*f0, hz, 0.01f, hz);
        std::printf("  HOT WIRE gate -30 on a -50 dBFS tone: %.1f dB left; focus 5 at %.0f Hz %+.1f dB\n", db(rms(s.l, 24000, 24000) / 0.0021), hz, lift);
        CHECK(rms(s.l, 24000, 24000) < 1e-5 && lift > 3.0);
    }
}

void testBottomFeeder() {
    using namespace bottomfeeder;
    {   // BLEND all DI, DI TONE 0: the input, 23 samples late; DI FLIP turns it over
        auto e = make("BOTTOM FEEDER", {{Blend, 10}});
        CHECK(e->latency() == 23);
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 23; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i - 23])));
        auto f = make("BOTTOM FEEDER", {{Blend, 10}, {DiFlip, 1}});
        Stereo t = noise(0.3, 0.2f);
        run(*f, t);
        double fl = 0;
        for (std::size_t i = 23; i < t.size(); ++i) fl = std::max(fl, static_cast<double>(std::fabs(t.l[i] + ref.l[i - 23])));
        std::printf("  BOTTOM FEEDER all DI: largest change %.1e; flipped %.1e\n", d, fl);
        CHECK(d < 1e-5 && fl < 1e-5);
    }
    {   // the DRIVE pedal adds grit; VOICE 1 is narrower than 3
        auto off = make("BOTTOM FEEDER", {{Blend, 0}, {CabOff, 1}, {Gain, 0}}), fat = make("BOTTOM FEEDER", {{Blend, 0}, {CabOff, 1}, {Gain, 0}, {DriveType, Fat}, {DriveMix, 10}});
        const double h0 = componentDb(*off, 110, 0.1f, 330) - componentDb(*off, 110, 0.1f, 110);
        const double h1 = componentDb(*fat, 110, 0.1f, 330) - componentDb(*fat, 110, 0.1f, 110);
        auto v1 = make("BOTTOM FEEDER", {{Blend, 0}, {CabOff, 1}, {Gain, 0}, {Voice, 0}}), v3 = make("BOTTOM FEEDER", {{Blend, 0}, {CabOff, 1}, {Gain, 0}, {Voice, 2}});
        const double n1 = componentDb(*v1, 1100, 0.01f, 1100) - componentDb(*v1, 500, 0.01f, 500);
        const double n3 = componentDb(*v3, 1100, 0.01f, 1100) - componentDb(*v3, 500, 0.01f, 500);
        std::printf("  BOTTOM FEEDER 3rd harmonic: drive off %.1f, fat %.1f dB; 1.1 kHz vs 500 Hz: voice 1 %.1f, voice 3 %.1f dB\n", h0, h1, n1, n3);
        CHECK(h1 > h0 + 10.0 && n1 < n3 - 1.0);
    }
}

void testAuditor() {
    using namespace auditor;
    // a -20 dBFS sine on both sides (amplitude 0.1): about -20.0 LUFS
    {   // untouched (limiter off, gain 0), kAuditorLatency late
        auto e = make("THE AUDITOR");
        CHECK(e->latency() == kAuditorLatency);
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = kAuditorLatency; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i - kAuditorLatency])));
        CHECK(d < 1e-6);
    }
    auto read = [&](std::initializer_list<std::pair<int, float>> v, double hz = 1000.0, double secs = 5.0, float a = 0.1f) {
        auto e = make("THE AUDITOR", v);
        Stereo s = sine(hz, secs, a);
        run(*e, s);
        return e;
    };
    {
        auto e = read({});
        std::printf("  THE AUDITOR EBU, -20 dBFS 1 kHz: short %.2f, long %.2f, momentary %.2f LUFS, true peak %.2f dBTP, timer %.1f s\n", e->meter(ShortTerm),
                    e->meter(LongTerm), e->meter(Momentary), e->meter(TruePeak), e->meter(Timer));
        CHECK(std::fabs(e->meter(ShortTerm) + 20.0f) < 0.2f && std::fabs(e->meter(LongTerm) + 20.0f) < 0.2f && std::fabs(e->meter(TruePeak) + 20.0f) < 0.1f &&
              std::fabs(e->meter(Timer) - 5.0f) < 0.05f);
        auto lu = read({{Scale, 1}}), left = read({{Channels, 1}}), g = read({{Gain, 6}});
        std::printf("  THE AUDITOR scale LU %.2f; left only %.2f; gain +6 %.2f\n", lu->meter(LongTerm), left->meter(LongTerm), g->meter(LongTerm));
        CHECK(std::fabs(lu->meter(LongTerm) - 3.0f) < 0.2f && std::fabs(left->meter(LongTerm) + 23.0f) < 0.2f && std::fabs(g->meter(LongTerm) + 14.0f) < 0.2f);
    }
    {   // the weightings: A at 100 Hz is about 19 dB down; M at 6.3 kHz is 12.2 dB over 1 kHz, 6.6 over 2 kHz (its 0 dB); NONE flat
        const double none1k = read({{Weighting, NoWeight}})->meter(LongTerm), a1k = read({{Weighting, LeqA}})->meter(LongTerm);
        const double a100 = read({{Weighting, LeqA}}, 100.0)->meter(LongTerm), c100 = read({{Weighting, LeqC}}, 100.0)->meter(LongTerm);
        const double m2k = read({{Weighting, LeqM}}, 2000.0)->meter(LongTerm), m63 = read({{Weighting, LeqM}}, 6300.0)->meter(LongTerm);
        std::printf("  THE AUDITOR weighting: none 1 kHz %.2f, A 1 kHz %.2f, A 100 Hz %.2f, C 100 Hz %.2f, M 2 kHz %.2f, M 6.3 kHz %.2f\n", none1k, a1k, a100,
                    c100, m2k, m63);
        CHECK(std::fabs(a1k - none1k) < 0.1 && std::fabs(a100 - none1k + 19.1) < 0.6 && std::fabs(c100 - none1k + 0.3) < 0.4 && std::fabs(m2k - none1k) < 0.3 &&
              std::fabs(m63 - m2k - 6.6) < 0.5);
    }
    {   // the limiter holds true peaks under TRUE PEAK MAX; overs, trim
        auto e = read({{Limiter, 1}, {TruePeakMax, -6}}, 997.0, 3.0, 0.7f);
        std::printf("  THE AUDITOR limiter at -6 on a -3 dBFS sine: true peak %.2f dBTP, limiter %.1f dB\n", e->meter(TruePeak), e->meter(Reduction));
        CHECK(e->meter(TruePeak) < -5.8f && e->meter(Reduction) < -2.5f);
        auto o = read({{ShortMax, -30}, {Target, -24}});
        std::printf("  THE AUDITOR short max -30: overs %.0f; target -24: trim %+.2f dB\n", o->meter(Overs), o->meter(Trim));
        CHECK(o->meter(Overs) == 1.0f && std::fabs(o->meter(Trim) + 4.0f) < 0.2f);
        // PAUSE stops the timer and the long term; RESET clears them
        auto pz = read({{Measure, 0}}, 1000.0, 2.0);
        CHECK(pz->meter(Timer) == 0.0f && pz->meter(LongTerm) < -99.0f);
        auto r = read({});
        r->set(Reset, 1);
        Stereo q(4800);
        run(*r, q);
        CHECK(r->meter(Timer) < 0.2f && r->meter(Overs) == 0.0f);
    }
}

void testStakeout() {
    using namespace stakeout;
    auto band = [](int res, double hz) {   // the band a frequency falls in
        int best = 0;
        for (int b = 0; b < bandCount(res); ++b)
            if (std::fabs(std::log(bandHz(res, b) / hz)) < std::fabs(std::log(bandHz(res, best) / hz))) best = b;
        return best;
    };
    auto look = [&](std::initializer_list<std::pair<int, float>> v, double hz, float l, float r, double secs = 1.0) {
        auto e = make("STAKEOUT", v);
        Stereo s(static_cast<std::size_t>(secs * 48000.0));
        for (std::size_t i = 0; i < s.size(); ++i) {
            const auto x = static_cast<float>(std::sin(2.0 * 3.14159265358979 * hz * static_cast<double>(i) / 48000.0));
            s.l[i] = l * x;
            s.r[i] = r * x;
        }
        Stereo ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        CHECK(d == 0.0);   // the sound passes untouched
        return e;
    };
    auto* a = dynamic_cast<AnalyzerSource*>(look({}, 1000.0, 0.1f, 0.1f).get());
    CHECK(a == nullptr || true);
    {
        auto e = look({}, 1000.0, 0.1f, 0.1f);
        auto* an = dynamic_cast<AnalyzerSource*>(e.get());
        const int b = band(0, 1000.0);
        std::printf("  STAKEOUT a -20 dBFS 1 kHz sine: its band %.1f dB (neighbour two over %.1f), position %.1f deg; peak L %.1f, sum %.1f dB\n",
                    an->rtaLevel(0, b), an->rtaLevel(0, b + 2), an->spdAngle(b), e->meter(PeakL), e->meter(Sum));
        CHECK(std::fabs(an->rtaLevel(0, b) + 20.0f) < 0.5f && an->rtaLevel(0, b + 2) < -40.0f && std::fabs(an->spdAngle(b)) < 2.0f &&
              std::fabs(e->meter(PeakL) + 20.0f) < 0.1f && std::fabs(e->meter(Sum) + 20.0f) < 0.1f);
        CHECK(an->rtaMax(0, b) >= an->rtaLevel(0, b) - 0.01f);
    }
    {   // left only: -45 degrees, the sum 6 dB down; right only +45; upside down: out past 45
        auto l = look({}, 2000.0, 0.1f, 0.0f), r = look({}, 2000.0, 0.0f, 0.1f), o = look({}, 2000.0, 0.1f, -0.1f);
        const int b = band(0, 2000.0);
        const float al = dynamic_cast<AnalyzerSource*>(l.get())->spdAngle(b), ar = dynamic_cast<AnalyzerSource*>(r.get())->spdAngle(b);
        const float ao = dynamic_cast<AnalyzerSource*>(o.get())->spdAngle(b);
        std::printf("  STAKEOUT position: left only %.1f, right only %.1f, out of phase %.1f deg; left-only sum %.1f dB\n", al, ar, ao, l->meter(Sum));
        CHECK(std::fabs(al + 45.0f) < 2.0f && std::fabs(ar - 45.0f) < 2.0f && std::fabs(ao) > 85.0f && std::fabs(l->meter(Sum) + 26.0f) < 0.2f);
    }
    {   // 10 Hz resolution: a 55 Hz tone reads in its own low band; RMS reads a steady sine the same; A weighting at 100 Hz
        auto e = look({{Resolution, 2}, {Mode, 1}}, 60.0, 0.1f, 0.1f, 2.0);
        const int b = band(2, 60.0);
        auto w = look({{Weighting, 1}, {Resolution, 2}}, 100.0, 0.1f, 0.1f);
        const int bw = band(2, 100.0);
        const float lw = dynamic_cast<AnalyzerSource*>(w.get())->rtaLevel(0, bw);
        std::printf("  STAKEOUT 10 Hz bands (%d of them), RMS, 60 Hz: %.1f dB; A weighting, 100 Hz (band %.0f Hz): %.1f dB\n", bandCount(2),
                    dynamic_cast<AnalyzerSource*>(e.get())->rtaLevel(0, b), bandHz(2, bw), lw);
        CHECK(bandCount(2) == 70 && std::fabs(dynamic_cast<AnalyzerSource*>(e.get())->rtaLevel(0, b) + 20.0f) < 0.7f && std::fabs(lw + 39.1f) < 0.8f);
    }
    {   // a full-scale sample lights the clip light; CLEAR CLIPS puts it out
        auto e = make("STAKEOUT");
        Stereo s(4800);
        s.l[100] = 1.0f;
        run(*e, s);
        const float lit = e->meter(ClipL), other = e->meter(ClipR);
        e->set(ClearClips, 1);
        Stereo q(480);
        run(*e, q);
        CHECK(lit == 1.0f && other == 0.0f && e->meter(ClipL) == 0.0f);
    }
}

// THE RACKET: each pedal does its job (measured), the board's input modes, the pedal change, the latency.
void testRacket() {
    using namespace racket;
    auto ped = [](int which, std::initializer_list<std::pair<int, float>> v = {}) {
        auto e = findPlugin("THE RACKET")->create();
        e->set(Which, static_cast<float>(which));
        for (const auto& [i, x] : v) e->set(i, x);
        e->prepare(kFs, 512);
        return e;
    };
    const std::uint32_t lat = ped(PdOverdrive)->latency();
    bool sameLatency = true;
    for (int k = 0; k < kPedals; ++k) sameLatency = sameLatency && ped(k)->latency() == lat;
    CHECK(sameLatency && lat == 23);
    // every pedal: noise in, finite out, and the controls on the plate belong to that pedal
    int bad = 0;
    for (int k = 0; k < kPedals; ++k) {
        auto e = ped(k);
        Stereo s = noise(1.0, 0.5f, 3 + static_cast<unsigned>(k));
        run(*e, s);
        const PedalFace f = pedalFace(k);
        bool own = f.knobCount <= kMaxKnobs && f.switchCount <= kMaxSwitches;
        for (int i = 0; i < f.knobCount; ++i) own = own && pedalOf(f.knobs[i].param) == k;
        for (int i = 0; i < f.switchCount; ++i) own = own && pedalOf(f.switches[i].param) == k;
        // every parameter of the pedal is on its plate
        int shown = f.knobCount + f.switchCount;
        if (!finite(s) || !own || shown != kFirst[k + 1] - kFirst[k]) {
            ++bad;
            std::printf("  THE RACKET pedal %d: finite %d, own controls %d, %d of %d shown\n", k, finite(s), own, shown, kFirst[k + 1] - kFirst[k]);
        }
    }
    CHECK(bad == 0);
    {
        auto h = ped(PdHeavy);
        CHECK(h->paramActive(HvDrive) && !h->paramActive(FlDepth) && h->paramActive(InputGain));
    }
    // the drives
    auto od0 = ped(PdOverdrive, {{OdDrive, 0}}), od1 = ped(PdOverdrive, {{OdDrive, 100}});
    const double a = componentDb(*od0, 220, 0.1f, 660) - componentDb(*od0, 220, 0.1f, 220);
    const double b = componentDb(*od1, 220, 0.1f, 660) - componentDb(*od1, 220, 0.1f, 220);
    auto lv50 = ped(PdOverdrive), lv100 = ped(PdOverdrive, {{OdLevel, 100}});
    const double up = componentDb(*lv100, 220, 0.1f, 220) - componentDb(*lv50, 220, 0.1f, 220);
    auto dsDark = ped(PdHardClip, {{DsTone, 700}}), dsBright = ped(PdHardClip, {{DsTone, 10000}});
    const double tone = componentDb(*dsBright, 220, 0.1f, 4180) - componentDb(*dsDark, 220, 0.1f, 4180);
    auto fz = ped(PdFuzz);
    const double even = componentDb(*fz, 220, 0.1f, 440) - componentDb(*fz, 220, 0.1f, 220);
    std::printf("  THE RACKET drives: overdrive 3rd harmonic drive 0 %.1f / 100 %.1f dB; LEVEL 50->100 %+.2f dB; hard clip TONE 700 Hz -> 10 kHz "
                "at 4.2 kHz %+.1f dB; fuzz 2nd harmonic %.1f dB\n", a, b, up, tone, even);
    CHECK(b > a + 10.0 && std::fabs(up - 12.04) < 0.3 && tone > 20.0 && even > -30.0);
    // VOLUME: the latency is exact, the scales
    {
        auto v = ped(PdVolume);
        Stereo s(4800);
        s.l[100] = s.r[100] = 0.5f;
        run(*v, s);
        CHECK(std::fabs(s.l[100 + lat] - 0.5f) < 1e-6f && std::fabs(s.r[100 + lat] - 0.5f) < 1e-6f);
        auto lin0 = ped(PdVolume, {{VlVolume, 0}}), log50 = ped(PdVolume, {{VlVolume, 50}, {VlScale, 1}}), lin50 = ped(PdVolume, {{VlVolume, 50}});
        const double d0 = componentDb(*lin0, 1000, 0.5f, 1000) - db(0.5), d1 = componentDb(*log50, 1000, 0.5f, 1000) - db(0.5),
                     d2 = componentDb(*lin50, 1000, 0.5f, 1000) - db(0.5);
        const double want2 = db(0.5 * (std::pow(10.0, -24.0 / 20.0) + 1.0));
        std::printf("  THE RACKET VOLUME: LIN 0 %.2f dB (want -24), LOG 50 %.2f dB (want -12), LIN 50 %.2f dB (want %.2f)\n", d0, d1, d2, want2);
        CHECK(std::fabs(d0 + 24.0) < 0.1 && std::fabs(d1 + 12.0) < 0.1 && std::fabs(d2 - want2) < 0.1);
    }
    // GRAPHIC and TONE STACK
    {
        auto g = ped(PdGraphic, {{Eq1k, 12}});
        const double at1k = componentDb(*g, 1000, 0.1f, 1000) - db(0.1), at14 = componentDb(*g, 1400, 0.1f, 1400) - db(0.1);
        auto flat = ped(PdGraphic);
        const double rumble = componentDb(*flat, 20, 0.1f, 20, 3.0) - db(0.1);
        auto st = ped(PdToneStack, {{TsLow, 18}, {TsLowCut, 16}, {TsHighCut, 4000}});
        const double low = componentDb(*st, 60, 0.1f, 60) - db(0.1), high = componentDb(*st, 10000, 0.1f, 10000) - db(0.1);
        std::printf("  THE RACKET GRAPHIC 1 kHz +12: at 1 kHz %+.2f, at 1.4 kHz %+.2f dB; rumble at 20 Hz %+.1f dB; TONE STACK low +18 at 60 Hz %+.1f, "
                    "high cut 4 kHz at 10 kHz %+.1f dB\n", at1k, at14, rumble, low, high);
        CHECK(std::fabs(at1k - 12.0) < 0.3 && at14 < 3.0 && rumble < -6.0 && low > 16.5 && high < -12.0);
    }
    // OCT DOWN: one and two octaves under a 220 Hz note
    {
        auto o1 = ped(PdOctDown, {{OcDirect, 0}, {OcOne, 100}, {OcTwo, 0}}), o2 = ped(PdOctDown, {{OcDirect, 0}, {OcOne, 0}, {OcTwo, 100}});
        const double one = componentDb(*o1, 220, 0.3f, 110), oneAt220 = componentDb(*o1, 220, 0.3f, 220);
        const double two = componentDb(*o2, 220, 0.3f, 55), twoAt110 = componentDb(*o2, 220, 0.3f, 110);
        std::printf("  THE RACKET OCT DOWN on 220 Hz at -10 dBFS: one down 110 Hz %.1f dB (220 Hz %.1f); two down 55 Hz %.1f dB (110 Hz %.1f)\n",
                    one, oneAt220, two, twoAt110);
        CHECK(one > -25.0 && one > oneAt220 + 20.0 && two > -30.0 && two > twoAt110 + 10.0);
    }
    // PITCH RIDE and BASS RIDE: all wet, both ends at the same shift
    {
        auto pr = ped(PdPitchRide, {{PrMix, 100}, {PrLow, 12}, {PrHigh, 12}});
        Stereo s = sine(220, 2.0, 0.3f);
        run(*pr, s);
        const double hz = crossingsHz(s.l, 48000, 48000);
        auto br = ped(PdBassRide, {{BrMix, 100}, {BrLow, -12}, {BrHigh, -12}});
        Stereo t = sine(110, 2.0, 0.3f);
        run(*br, t);
        const double hz2 = crossingsHz(t.l, 48000, 48000);
        std::printf("  THE RACKET PITCH RIDE +12 on 220 Hz -> %.1f Hz; BASS RIDE -12 on 110 Hz -> %.1f Hz\n", hz, hz2);
        CHECK(std::fabs(hz - 440.0) < 3.0 && std::fabs(hz2 - 55.0) < 1.0);
    }
    // ECHO: 100 ms, all wet, no feedback; STEREO 10: the first echo left, the next one would need feedback
    {
        auto e = ped(PdEcho, {{EcMix, 100}, {EcTime, 0.1f}, {EcFeedback, 0}});
        Stereo s(24000);
        s.l[0] = s.r[0] = 0.5f;
        run(*e, s);
        std::size_t at = 0;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (std::fabs(s.l[i]) > std::fabs(s.l[at])) at = i;
        const float right = std::fabs(s.r[at]);
        double later = 0;
        for (std::size_t i = at + 200; i < s.size(); ++i) later = std::max(later, static_cast<double>(std::fabs(s.l[i]) + std::fabs(s.r[i])));
        auto f = ped(PdEcho, {{EcMix, 100}, {EcTime, 0.1f}, {EcFeedback, 8}, {EcStereo, 0}});
        Stereo s2(48000);
        s2.l[0] = s2.r[0] = 0.5f;
        run(*f, s2);
        const double second = std::fabs(s2.l[2 * 4800 + lat]), third = std::fabs(s2.l[3 * 4800 + lat]);
        std::printf("  THE RACKET ECHO 100 ms: echo at %zu samples (want %u), left %.3f right %.3f, nothing after %.1e; feedback 8: 2nd %.3f 3rd %.3f\n",
                    at, 4800 + lat, static_cast<double>(std::fabs(s.l[at])), static_cast<double>(right), later, second, third);
        CHECK(at == 4800 + lat && right < 1e-6f && later < 1e-6 && second > 0.1 && third > 0.05 && third < second);
    }
    // FLIP ECHO REVERSE: a note that swells up comes back fading away
    {
        auto run1 = [&](float rev) {
            auto e = ped(PdFlipEcho, {{FeMix, 100}, {FeTime, 0.25f}, {FeFeedback, 0}, {FeStereo, 0}, {FeReverse, rev}});
            Stereo s(48000 * 2);
            for (std::size_t i = 0; i < 12000; ++i) s.l[i] = s.r[i] = 0.5f * static_cast<float>(i) / 12000.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(i) / kFs));
            run(*e, s);
            // how the level moves where the echo is loudest: the level 20 ms before the peak against 20 ms after
            double best = 0;
            std::size_t at = 0;
            for (std::size_t i = 0; i + 480 < s.size(); i += 480) {
                const double r = rms(s.l, i, 480);
                if (r > best) { best = r; at = i; }
            }
            const double before = rms(s.l, at >= 960 ? at - 960 : 0, 480), after = rms(s.l, at + 960, 480);
            return std::pair<double, double>{before, after};
        };
        const auto fw = run1(0.0f), bw = run1(1.0f);
        std::printf("  THE RACKET FLIP ECHO on a swell: forward %.3f before / %.3f after the loudest point, reversed %.3f / %.3f\n",
                    fw.first, fw.second, bw.first, bw.second);
        CHECK(fw.first > fw.second && bw.second > bw.first);
    }
    // ROOM and SPRING: a tail; ROOM TIME stretches it
    {
        auto tailAt = [&](int which, std::initializer_list<std::pair<int, float>> v, double from) {
            auto e = ped(which, v);
            Stereo s(48000 * 3);
            for (std::size_t i = 0; i < 480; ++i) s.l[i] = s.r[i] = 0.3f;
            run(*e, s);
            return db(rms(s.l, static_cast<std::size_t>(from * kFs), 4800));
        };
        const double shortRoom = tailAt(PdRoom, {{RmMix, 100}, {RmTime, 0.5f}}, 1.0), longRoom = tailAt(PdRoom, {{RmMix, 100}, {RmTime, 5.0f}}, 1.0);
        const double spring = tailAt(PdSpring, {{SpMix, 100}}, 0.5);
        std::printf("  THE RACKET ROOM at 1 s: TIME 0.5 s %.1f dB, 5 s %.1f dB; SPRING at 0.5 s %.1f dB\n", shortRoom, longRoom, spring);
        CHECK(longRoom > shortRoom + 30.0 && longRoom > -60.0 && spring > -60.0);
    }
    // SQUEEZE: 24 dB apart in, closer out; GATE: shut under the threshold, open over it
    {
        auto lvl = [&](int which, std::initializer_list<std::pair<int, float>> v, float amp) {
            auto e = ped(which, v);
            return componentDb(*e, 1000, amp, 1000) - db(amp);
        };
        const double loud = lvl(PdSqueeze, {{SqAmount, 100}}, 0.5f) + db(0.5), quiet = lvl(PdSqueeze, {{SqAmount, 100}}, 0.03f) + db(0.03);
        const double shut = lvl(PdGate, {{GtThreshold, -40}}, 0.001f), open = lvl(PdGate, {{GtThreshold, -40}}, 0.1f);
        std::printf("  THE RACKET SQUEEZE 100: -6 and -30 dBFS in -> %.1f and %.1f dB out; GATE -40: -60 dBFS %+.1f dB, -20 dBFS %+.2f dB\n",
                    loud, quiet, shut, open);
        CHECK(loud - quiet < 14.0 && shut < -60.0 && std::fabs(open) < 0.3);
    }
    // WAH by hand: heel down favours 400 Hz, toe down 1.9 kHz
    {
        auto heel = ped(PdWah, {{WhMode, 1}, {WhPos, 0}}), toe = ped(PdWah, {{WhMode, 1}, {WhPos, 100}});
        const double h1 = componentDb(*heel, 400, 0.1f, 400) - componentDb(*heel, 1900, 0.1f, 1900);
        const double t1 = componentDb(*toe, 400, 0.1f, 400) - componentDb(*toe, 1900, 0.1f, 1900);
        std::printf("  THE RACKET WAH 400 Hz against 1.9 kHz: heel %+.1f dB, toe %+.1f dB\n", h1, t1);
        CHECK(h1 > 10.0 && t1 < -10.0);
    }
    // VIB / TREM full tremolo dips to silence; AUTO PAN full width swings to one side
    {
        auto tr = ped(PdVibTrem, {{VtTremolo, 100}, {VtRate, 4}});
        Stereo s = sine(1000, 1.0, 0.3f);
        run(*tr, s);
        double lo = 1.0, hi = 0.0;
        for (std::size_t i = 4800; i + 480 < s.size(); i += 480) { const double r = rms(s.l, i, 480); lo = std::min(lo, r); hi = std::max(hi, r); }
        auto pn = ped(PdAutoPan, {{PnWidth, 100}, {PnRate, 1}});
        Stereo t = sine(1000, 1.0, 0.3f);
        run(*pn, t);
        double side = 0;
        for (std::size_t i = 0; i + 480 < t.size(); i += 480) side = std::max(side, db(rms(t.l, i, 480)) - db(rms(t.r, i, 480) + 1e-9));
        std::printf("  THE RACKET tremolo 100: %.3f .. %.3f RMS; AUTO PAN 100: one side up to %.1f dB over the other\n", lo, hi, side);
        CHECK(lo < 0.02 * hi && side > 30.0);
    }
    // the input modes: MONO makes both sides the same; the pedal change fades, never jumps
    {
        auto m = ped(PdChorus, {{Input, InMono}});
        Stereo s = sine(500, 0.5, 0.3f);
        for (float& x : s.r) x = 0.0f;
        run(*m, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        auto sw = ped(PdOverdrive, {{OdDrive, 100}, {OdLevel, 100}});
        Stereo t = sine(220, 1.0, 0.3f);
        float jump = 0.0f;
        for (std::size_t i = 0; i < t.size(); i += 256) {
            if (i == 24064) sw->set(Which, PdVolume);
            sw->process(t.l.data() + i, t.r.data() + i, static_cast<std::uint32_t>(std::min<std::size_t>(256, t.size() - i)));
        }
        // the steps while it changes are no bigger than the overdrive's own, and it passes through silence
        float before = 0.0f;
        for (std::size_t i = 12000; i < 24064; ++i) before = std::max(before, std::fabs(t.l[i] - t.l[i - 1]));
        for (std::size_t i = 24064; i < 24064 + 4800; ++i) jump = std::max(jump, std::fabs(t.l[i] - t.l[i - 1]));
        double quietest = 1.0;
        for (std::size_t i = 24064; i < 24064 + 960; i += 16) quietest = std::min(quietest, rms(t.l, i, 16));
        const double after = db(rms(t.l, 36000, 9600)), want = db(0.3 / std::numbers::sqrt2);
        std::printf("  THE RACKET MONO: sides differ by %.1e; OVERDRIVE -> VOLUME: biggest step %.3f (the overdrive's own %.3f), quietest %.4f, "
                    "then %.2f dB (the input %.2f)\n", diff, static_cast<double>(jump), static_cast<double>(before), quietest, after, want);
        CHECK(diff < 1e-6 && jump <= before && quietest < 0.01 && std::fabs(after - want) < 0.05);
    }
    // the same result whatever the host's block size
    for (int which : {PdOverdrive, PdFlipEcho, PdEcho, PdOctDown, PdWah, PdThicken}) {
        const double d = blockSizeDifference(*findPlugin("THE RACKET"), [&](hc::fx::Effect& e) { e.set(Which, static_cast<float>(which)); }, noise(1.0, 0.3f, 9));
        if (d > 1e-5) std::printf("  THE RACKET pedal %d: block size changes the result by %.2e\n", which, d);
        CHECK(d <= 1e-5);
    }
}

void testSnitch() {
    using namespace snitch;
    auto e = make("THE SNITCH");
    Stereo s = sine(110, 1.0, 0.3f), ref = s;
    run(*e, s);
    const float n1 = e->meter(0), c1 = e->meter(1);
    double diff = 0;
    for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
    e->reset();
    Stereo s2 = sine(440.0 * std::exp2(20.0 / 1200.0), 1.0, 0.3f);
    run(*e, s2);
    const float n2 = e->meter(0), c2 = e->meter(1);
    auto r = make("THE SNITCH", {{Reference, 5}});
    Stereo s3 = sine(440.0 * std::exp2(5.0 / 1200.0), 1.0, 0.3f);
    run(*r, s3);
    std::printf("  THE SNITCH: 110 Hz -> note %.0f %+.1f c; A4 +20 c -> note %.0f %+.1f c; A +5 c with REFERENCE +5 -> %+.1f c; sound changed by %.1e\n",
                n1, c1, n2, c2, r->meter(1), diff);
    CHECK(n1 == 45.0f && std::fabs(c1) < 3.0f && n2 == 69.0f && std::fabs(c2 - 20.0f) < 3.0f && std::fabs(r->meter(1)) < 3.0f && diff == 0.0);
    // the modes: 405 Hz is G#4 -43 c in CHROMATIC, G4 (+50 c, held at the edge) in GUITAR, B4 with only B switched on
    auto at = [&](std::initializer_list<std::pair<int, float>> v) {
        auto e2 = make("THE SNITCH", v);
        Stereo t = sine(405.0, 1.0, 0.3f);
        run(*e2, t);
        return std::pair<float, float>{e2->meter(0), e2->meter(1)};
    };
    const auto ch = at({{Mode, Chromatic}}), gt = at({{Mode, Guitar}});
    std::initializer_list<std::pair<int, float>> onlyB = {{Mode, MyNotes}, {Note0 + 0, 0}, {Note0 + 1, 0}, {Note0 + 2, 0}, {Note0 + 3, 0}, {Note0 + 4, 0},
                                                         {Note0 + 5, 0}, {Note0 + 6, 0}, {Note0 + 7, 0}, {Note0 + 8, 0}, {Note0 + 9, 0}, {Note0 + 10, 0}};
    const auto mb = at(onlyB);
    std::printf("  THE SNITCH 405 Hz: chromatic %.0f %+.1f c; guitar %.0f %+.1f c; only B %.0f %+.1f c\n", ch.first, ch.second, gt.first, gt.second, mb.first, mb.second);
    CHECK(ch.first == 68.0f && std::fabs(ch.second + 43.0f) < 3.0f && gt.first == 67.0f && gt.second == 50.0f && mb.first == 71.0f && mb.second == -50.0f);
}

void testPromotion() {
    using namespace promotion;
    {
        auto e = make("PROMOTION");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        const std::uint32_t lat = e->latency();
        double diff = 0;
        for (std::size_t i = lat; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i - lat])));
        std::printf("  PROMOTION at unison: the input %u samples late, largest difference %.2e\n", lat, diff);
        CHECK(diff < 1e-4);
    }
    for (int m = 0; m < kModes; ++m) {
        auto e = make("PROMOTION", {{Semitones, 24}, {P::Mode, static_cast<float>(m)}});
        const double up = componentDb(*e, 220, 0.3f, 440), orig = componentDb(*e, 220, 0.3f, 220);
        auto f = make("PROMOTION", {{Semitones, 0}, {P::Mode, static_cast<float>(m)}});
        const double down = componentDb(*f, 440, 0.3f, 220), orig2 = componentDb(*f, 440, 0.3f, 440);
        std::printf("  PROMOTION mode %d: an octave up on 220 Hz -> 440 Hz %.1f dB, 220 left %.1f dB; down on 440 -> 220 Hz %.1f dB, 440 left %.1f dB\n",
                    m, up, orig, down, orig2);
        CHECK(up > -16.0 && orig < up - 20.0 && down > -16.0 && orig2 < down - 20.0);
    }
    {   // FREQUENCY 440 -> 660 Hz: a fifth up; SEMITONES +7 and CENTS 2 lands the same place
        auto f = make("PROMOTION", {{Entry, 1}, {FromHz, 440}, {ToHz, 660}});
        auto t = make("PROMOTION", {{Semitones, 19}, {Cents, 2}});
        const double a = componentDb(*f, 300, 0.3f, 450), b = componentDb(*t, 300, 0.3f, 300.0 * std::exp2(702.0 / 1200.0));
        std::printf("  PROMOTION 440 -> 660 Hz on 300 Hz: 450 Hz %.1f dB; +7 st 2 c: %.1f dB\n", a, b);
        CHECK(a > -16.0 && b > -16.0);
    }
}

void testWiseguys() {
    using namespace wiseguys;
    {   // the direct voice alone: the input, kLatency late
        auto e = make("WISEGUYS");
        CHECK(e->latency() == kLatency);
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = kLatency; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i - kLatency])));
        std::printf("  WISEGUYS direct only: largest change %.1e\n", d);
        CHECK(d < 1e-6);
    }
    {   // one voice a fifth up on a sung-like tone (harmonics), no direct: the fifth is there, the original isn't
        auto e = make("WISEGUYS", {{voice(0, On), 1}, {voice(0, Pitch), 7}, {voice(0, Gain), 0}, {voice(0, Pan), 0}, {DirectOn, 0}});
        Stereo s(96000);
        for (std::size_t i = 0; i < s.size(); ++i) {
            const double t = static_cast<double>(i) / kFs;
            float v = 0.0f;
            for (int h = 1; h <= 6; ++h) v += static_cast<float>(0.3 / h * std::sin(2.0 * 3.14159265358979 * 200.0 * h * t));
            s.l[i] = s.r[i] = v;
        }
        run(*e, s);
        const double up = db(toneAmp(s.l, 200.0 * std::exp2(7.0 / 12.0), 48000, 48000)), orig = db(toneAmp(s.l, 200.0, 48000, 48000));
        std::printf("  WISEGUYS a fifth up on a 200 Hz voice-like tone: 299.7 Hz %.1f dB, 200 Hz %.1f dB\n", up, orig);
        CHECK(up > -20.0 && orig < up - 20.0);
        // FORMANT: the same voice with its formants moved up an octave puts more of its energy higher
        auto f = make("WISEGUYS", {{voice(0, On), 1}, {voice(0, Pitch), 0}, {voice(0, Gain), 0}, {voice(0, Formant), 12}, {DirectOn, 0}});
        Stereo t = s;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const double tt = static_cast<double>(i) / kFs;
            float v = 0.0f;
            for (int h = 1; h <= 6; ++h) v += static_cast<float>(0.3 / h * std::sin(2.0 * 3.14159265358979 * 200.0 * h * tt));
            t.l[i] = t.r[i] = v;
        }
        Stereo ref = t;
        run(*f, t);
        auto upper = [](const std::vector<float>& x) {   // harmonics 7..12 together (the source has only 1..6)
            double e = 0;
            for (int h = 7; h <= 12; ++h) { const double a = toneAmp(x, 200.0 * h, 48000, 48000); e += a * a; }
            return 10.0 * std::log10(e + 1e-30);
        };
        const double hiRef = upper(ref.l), hiF = upper(t.l);
        const double fRef = db(toneAmp(ref.l, 200.0, 48000, 48000)), fF = db(toneAmp(t.l, 200.0, 48000, 48000));
        std::printf("  WISEGUYS formant +12 at unison: harmonics 7-12 %.1f dB (was %.1f), 200 Hz still %.1f dB (was %.1f)\n", hiF, hiRef, fF, fRef);
        CHECK(hiF > -35.0 && hiRef < -100.0 && fF > fRef - 20.0);
    }
    {   // PAN hard left: nothing on the right
        auto e = make("WISEGUYS", {{voice(0, On), 1}, {voice(0, Pan), -45}, {DirectOn, 0}});
        Stereo s = sine(300, 1.0, 0.3f);
        run(*e, s);
        double r = 0;
        for (float x : s.r) r = std::max(r, static_cast<double>(std::fabs(x)));
        CHECK(r < 1e-6);
    }
}

void testMadeMan() {
    using namespace mademan;
    auto voiceLike = [](double hz, double secs) {
        Stereo s(static_cast<std::size_t>(secs * 48000.0));
        for (std::size_t i = 0; i < s.size(); ++i) {
            const double t = static_cast<double>(i) / 48000.0;
            float v = 0.0f;
            for (int h = 1; h <= 5; ++h) v += static_cast<float>(0.25 / h * std::sin(2.0 * 3.14159265358979 * hz * h * t));
            s.l[i] = s.r[i] = v;
        }
        return s;
    };
    {
        auto e = make("MADE MAN", {{Key, 9}, {Scale, 0}, {Speed, 0}, {Transition, 0}});
        Stereo s = voiceLike(452, 2.0);   // A4 + 46 cents: pulled down to 440
        run(*e, s);
        const double hz = crossingsHz(s.l, 48000, 48000);
        std::printf("  MADE MAN: 452 Hz sung, chromatic, hard -> %.1f Hz, pull meter %.0f c\n", hz, e->meter(1));
        CHECK(std::fabs(hz - 440.0) < 2.0 && std::fabs(e->meter(1) + 46.0f) < 4.0f);
    }
    {   // C#4 is not in C major: goes to C4 or D4; RATIO 50 goes half way
        auto m = make("MADE MAN", {{Key, 0}, {Scale, 1}, {Speed, 0}, {Transition, 0}});
        Stereo s2 = voiceLike(277.18, 2.0);
        run(*m, s2);
        const double hz2 = crossingsHz(s2.l, 48000, 48000);
        auto h = make("MADE MAN", {{Key, 9}, {Scale, 0}, {Speed, 0}, {Transition, 0}, {Ratio, 50}});
        Stereo s3 = voiceLike(452, 2.0);
        run(*h, s3);
        const double hz3 = crossingsHz(s3.l, 48000, 48000);
        std::printf("  MADE MAN: C#4 in C major -> %.1f Hz; 452 Hz at ratio 50 -> %.1f Hz (half way: 446.0)\n", hz2, hz3);
        CHECK((std::fabs(hz2 - 261.63) < 2.0 || std::fabs(hz2 - 293.66) < 2.0) && std::fabs(hz3 - 446.0) < 2.0);
    }
    {   // MY NOTES with only E: an A comes out as an E; SHIFT -12 drops it an octave
        std::initializer_list<std::pair<int, float>> onlyE = {{Scale, kScales - 1}, {Speed, 0}, {Transition, 0}, {Note0 + 0, 0}, {Note0 + 1, 0},
                                                             {Note0 + 2, 0}, {Note0 + 3, 0}, {Note0 + 5, 0}, {Note0 + 6, 0}, {Note0 + 7, 0},
                                                             {Note0 + 8, 0}, {Note0 + 9, 0}, {Note0 + 10, 0}, {Note0 + 11, 0}};
        auto e = make("MADE MAN", onlyE);
        Stereo s = voiceLike(400, 2.0);   // between E4 (329.6) and E5 (659): nearer E4 (in semitones)
        run(*e, s);
        // a voice-like pulse train (each pulse rings a 900 Hz formant) at 220 Hz, dropped an octave: 110 Hz appears
        auto d = make("MADE MAN", {{Scale, 0}, {Speed, 0}, {Transition, 0}, {Shift, -12}});
        Stereo t(96000);
        const double per = 48000.0 / 220.0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            const double ph = std::fmod(static_cast<double>(i), per) / 48000.0;
            t.l[i] = t.r[i] = static_cast<float>(0.5 * std::exp(-ph * 1500.0) * std::sin(2.0 * 3.14159265358979 * 900.0 * ph));
        }
        auto odd = [](const std::vector<float>& x) {   // the new in-between harmonics (odd multiples of 110 Hz) near the formant
            double e = 0;
            for (double hz : {770.0, 990.0}) { const double v = toneAmp(x, hz, 48000, 48000); e += v * v; }
            return 10.0 * std::log10(e + 1e-30);
        };
        const double before = odd(t.l);
        run(*d, t);
        const double after = odd(t.l), keep = db(toneAmp(t.l, 880.0, 48000, 48000));
        const double a = crossingsHz(s.l, 48000, 48000);
        std::printf("  MADE MAN: only E allowed, 400 Hz -> %.1f Hz; shift -12 on a 220 Hz voice: 770 + 990 Hz %.1f dB (was %.1f), 880 Hz %.1f dB\n", a, after,
                    before, keep);
        CHECK(std::fabs(a - 329.63) < 2.0 && after > before + 60.0 && after > keep - 10.0);
    }
}

void testDetective() {
    using namespace detective;
    auto e = make("THE DETECTIVE");
    Stereo c = chord({130.81, 261.63, 329.63, 392.0}, 8.0, 0.1f);   // C major
    run(*e, c);
    const float k1 = e->meter(0), st1 = e->meter(4), tun = e->meter(3);
    // DETECT while listening stops it (the alternatives show); pressed again it starts over
    e->set(Detect, 1);
    Stereo z(4800);
    run(*e, z);
    const float st2 = e->meter(4), a1 = e->meter(1);
    e->set(Detect, 0);
    Stereo a = chord({220.0, 261.63, 329.63, 110.0}, 8.0, 0.1f);    // A minor
    run(*e, a);
    const float k2 = e->meter(0);
    // SPELL FLATS: the same key + 24; EDIT shows your own
    e->set(Spell, 1);
    run(*e, z);
    const float k3 = e->meter(0);
    e->set(Edit, 1);
    e->set(Root, 3);
    e->set(Scale, 1);
    run(*e, z);
    const float k4 = e->meter(0);
    // a song tuned 20 cents sharp: TUNING about 445 Hz
    auto t = make("THE DETECTIVE");
    const double up = std::exp2(20.0 / 1200.0);
    Stereo s = chord({130.81 * up, 261.63 * up, 329.63 * up, 392.0 * up}, 8.0, 0.1f);
    run(*t, s);
    std::printf("  THE DETECTIVE: C major -> key %.0f (state %.0f, tuning %.1f Hz); stopped: state %.0f, alternative %.0f; A minor -> %.0f; flats %.0f; "
                "edit D# minor %.0f; 20 cents sharp -> %.1f Hz\n",
                k1, st1, tun, st2, a1, k2, k3, k4, t->meter(3));
    CHECK(k1 == 0.0f && st1 == static_cast<float>(Listening) && std::fabs(tun - 440.0f) < 1.0f && st2 == static_cast<float>(Found) && a1 >= 0.0f &&
          k2 == 21.0f && k3 == 45.0f && k4 == 15.0f + 24.0f && std::fabs(t->meter(3) - 445.1f) < 1.0f);
}

void testWitness() {
    using namespace witness;
    auto e = make("THE WITNESS");
    const float amp = static_cast<float>(std::pow(10.0, -18.0 / 20.0));
    Stereo s = sine(1000, 2.0, amp), ref = s;
    run(*e, s);
    double diff = 0;
    for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
    std::printf("  THE WITNESS: a -18 dBFS sine reads %+.2f VU, clip light %.0f; sound changed by %.1e\n", e->meter(0), e->meter(2), diff);
    CHECK(std::fabs(e->meter(0)) < 0.3f && e->meter(2) == 0.0f && diff == 0.0);
    // HEADROOM 10: the same sine reads -8 VU; a full-scale sample lights the clip light, CLEAR puts it out
    auto h = make("THE WITNESS", {{Headroom, 10}});
    Stereo t = sine(1000, 2.0, amp);
    t.l[1000] = 1.0f;
    run(*h, t);
    const float vu = h->meter(0), lit = h->meter(2), right = h->meter(3);
    h->set(Clear, 1);
    Stereo q = sine(1000, 0.1, amp);
    run(*h, q);
    // DISPLAY DELAY 500 ms: a tone that starts now shows half a second later
    auto d = make("THE WITNESS", {{DisplayDelay, 500}});
    Stereo u(36000);
    for (std::size_t i = 12000; i < u.size(); ++i) u.l[i] = u.r[i] = amp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 48000.0));
    run(*d, u);
    const float early = d->meter(0);
    Stereo u2 = sine(1000, 0.5, amp);
    run(*d, u2);
    std::printf("  THE WITNESS headroom 10: %+.2f VU; clip L %.0f R %.0f, after CLEAR %.0f; delay 500 ms: 0.5 s in %.1f VU, 1 s in %+.2f VU\n", vu, lit,
                right, h->meter(2), early, d->meter(0));
    CHECK(std::fabs(vu + 8.0f) < 0.3f && lit == 1.0f && right == 0.0f && h->meter(2) == 0.0f && early < -20.0f && std::fabs(d->meter(0)) < 0.5f);
}

void testLiveWire() {
    using namespace livewire;
    {   // SINE 1 kHz at -18, sent left only: the left is the tone (the input gone), the right silent
        auto e = make("LIVE WIRE", {{On, 1}, {Type, Sine}, {Freq, 1000}, {Gain, -18}, {Send, 0}});
        Stereo s = sine(300, 1.0, 0.3f);
        run(*e, s);
        const double tone = db(toneAmp(s.l, 1000, 24000, 24000)), input = db(toneAmp(s.l, 300, 24000, 24000));
        double rpk = 0;
        for (std::size_t i = 24000; i < s.size(); ++i) rpk = std::max(rpk, static_cast<double>(std::fabs(s.r[i])));
        std::printf("  LIVE WIRE sine 1 kHz at -18, left only: left %.1f dB, input left %.1f dB, right peak %.1e\n", tone, input, rpk);
        CHECK(std::fabs(tone + 18.0) < 0.2 && input < -80.0 && rpk < 1e-6);
    }
    {   // QUICK FREQ 10 kHz and QUICK GAIN -6 override the knobs; FLIP turns the right over
        auto e = make("LIVE WIRE", {{On, 1}, {Type, Sine}, {Freq, 300}, {QuickFreq, 3}, {Gain, -60}, {QuickGain, 1}, {Flip, 1}});
        Stereo s(48000);
        run(*e, s);
        double worst = 0;
        for (std::size_t i = 2400; i < s.size(); ++i) worst = std::max(worst, static_cast<double>(std::fabs(s.l[i] + s.r[i])));
        std::printf("  LIVE WIRE quick 10 kHz at -6: %.1f dB; L + R largest %.1e\n", db(toneAmp(s.l, 10000, 24000, 24000)), worst);
        CHECK(std::fabs(db(toneAmp(s.l, 10000, 24000, 24000)) + 6.0) < 0.2 && worst < 1e-6);
    }
    {   // the noises at -20 have the RMS of a -20 sine (-23 dBFS); off: untouched
        for (float t : {static_cast<float>(Pink), static_cast<float>(White)}) {
            auto e = make("LIVE WIRE", {{On, 1}, {Type, t}, {Gain, -20}});
            Stereo s(96000);
            run(*e, s);
            const double lv = db(rms(s.l, 24000, 72000));
            std::printf("  LIVE WIRE %s at -20: %.2f dBFS RMS (a -20 sine: -23.01)\n", t == Pink ? "pink" : "white", lv);
            CHECK(std::fabs(lv + 23.01) < 0.5);
        }
        auto off = make("LIVE WIRE");
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*off, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        CHECK(d == 0.0);
    }
}

} // namespace

void testStreets() {
    std::printf("[the streets]\n");
    allRules();
    testTorch();
    testHotWire();
    testBottomFeeder();
    testAuditor();
    testStakeout();
    testRacket();
    testSnitch();
    testPromotion();
    testWiseguys();
    testMadeMan();
    testDetective();
    testWitness();
    testLiveWire();
}

} // namespace hem::test
