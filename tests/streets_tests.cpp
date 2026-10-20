// The streets (guitar, pitch, meters, test tones): what each plug-in must actually do, measured,
// plus the rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/rack/Streets.hpp"
#include "hem/rack/Garage.hpp"

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
    for (const char* name : {"TORCH", "HOT WIRE", "BOTTOM FEEDER", "THE RACKET", "THE SNITCH", "PROMOTION", "WISEGUYS", "MADE MAN", "THE DETECTIVE", "THE WITNESS", "LIVE WIRE"}) {
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
        auto e = make("TORCH", {{Type, Crunch}, {Drive, drives[k]}, {CabA, 0}});
        lv[k] = componentDb(*e, 200, 0.1f, 200);
        h3[k] = componentDb(*e, 200, 0.1f, 600) - lv[k];
    }
    std::printf("  TORCH crunch, a -20 dBFS 200 Hz note, drive 0 / 5 / 10: level %.1f / %.1f / %.1f dB, 3rd harmonic %.1f / %.1f / %.1f dB under it\n",
                lv[0], lv[1], lv[2], h3[0], h3[1], h3[2]);
    CHECK(lv[0] > -30 && lv[2] < 6 && lv[1] > -30 && h3[2] > h3[0] + 6.0);
    // a cabinet takes the fizz off: 8 kHz well under 1 kHz through the 4x12; with NONE it isn't
    auto c = make("TORCH", {{Type, Clean}, {Drive, 0}, {CabA, 5}, {Presence, 0}});
    auto o = make("TORCH", {{Type, Clean}, {Drive, 0}, {CabA, 0}, {Presence, 0}});
    const double k1 = componentDb(*c, 1000, 0.01f, 1000), k8 = componentDb(*c, 8000, 0.01f, 8000);
    const double n1 = componentDb(*o, 1000, 0.01f, 1000), n8 = componentDb(*o, 8000, 0.01f, 8000);
    std::printf("  TORCH clean, 4x12 vintage: 1 kHz %.1f, 8 kHz %.1f dB; no cabinet: %.1f, %.1f dB\n", k1, k8, n1, n8);
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

void testRacket() {
    using namespace racket;
    auto fz = make("THE RACKET", {{Type, 2}, {Drive, 60}, {Tone, 100}});
    auto oc = make("THE RACKET", {{Type, 3}, {Drive, 60}, {Tone, 100}});
    const double f1 = componentDb(*fz, 220, 0.2f, 220), f2 = componentDb(*fz, 220, 0.2f, 440);
    const double o1 = componentDb(*oc, 220, 0.2f, 220), o2 = componentDb(*oc, 220, 0.2f, 440);
    std::printf("  THE RACKET 220 Hz: FUZZ octave above %.1f dB re the note, OCTAVE %.1f dB\n", f2 - f1, o2 - o1);
    CHECK(o2 - o1 > f2 - f1 + 6.0);
    auto od = make("THE RACKET", {{Type, 0}, {Drive, 0}});
    auto od2 = make("THE RACKET", {{Type, 0}, {Drive, 100}});
    const double a = componentDb(*od, 220, 0.1f, 660) - componentDb(*od, 220, 0.1f, 220);
    const double b = componentDb(*od2, 220, 0.1f, 660) - componentDb(*od2, 220, 0.1f, 220);
    std::printf("  THE RACKET overdrive 3rd harmonic: drive 0 %.1f dB, drive 100 %.1f dB\n", a, b);
    CHECK(b > a + 10.0);
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
        std::printf("  PROMOTION at 0: the input %u samples late, largest difference %.2e\n", lat, diff);
        CHECK(diff < 1e-4);
    }
    for (int ch = 0; ch < 2; ++ch) {
        auto e = make("PROMOTION", {{Shift, 12}, {Character, static_cast<float>(ch)}});
        const double up = componentDb(*e, 220, 0.3f, 440), orig = componentDb(*e, 220, 0.3f, 220);
        auto f = make("PROMOTION", {{Shift, -12}, {Character, static_cast<float>(ch)}});
        const double down = componentDb(*f, 440, 0.3f, 220), orig2 = componentDb(*f, 440, 0.3f, 440);
        std::printf("  PROMOTION %s: +12 st on 220 Hz -> 440 Hz %.1f dB, 220 Hz left %.1f dB; -12 st on 440 Hz -> 220 Hz %.1f dB, 440 left %.1f dB\n",
                    ch ? "TIGHT" : "SMOOTH", up, orig, down, orig2);
        CHECK(up > -16.0 && orig < up - 20.0 && down > -16.0 && orig2 < down - 20.0);
    }
}

void testWiseguys() {
    using namespace wiseguys;
    auto e = make("WISEGUYS", {{Interval1, 7}, {Level1, 100}, {Pan1, 0}, {Level2, 0}, {Level3, 0}, {Dry, 0}});
    const double fifth = componentDb(*e, 400, 0.3f, 400 * std::exp2(7.0 / 12.0)), orig = componentDb(*e, 400, 0.3f, 400);
    std::printf("  WISEGUYS one voice a fifth up, no dry: 599 Hz %.1f dB, 400 Hz %.1f dB\n", fifth, orig);
    CHECK(fifth > -16.0 && orig < fifth - 20.0);
}

void testMadeMan() {
    using namespace mademan;
    auto e = make("MADE MAN", {{Key, 9}, {Scale, 0}, {Speed, 0}});
    Stereo s = sine(452, 2.0, 0.3f);   // A4 + 46 cents: pulled down to 440
    run(*e, s);
    const double hz = crossingsHz(s.l, 48000, 48000);
    std::printf("  MADE MAN: 452 Hz sung, chromatic, speed 0 -> %.1f Hz, pull meter %.0f c\n", hz, e->meter(1));
    CHECK(std::fabs(hz - 440.0) < 2.0);
    auto m = make("MADE MAN", {{Key, 0}, {Scale, 1}, {Speed, 0}});
    Stereo s2 = sine(277.18, 2.0, 0.3f);   // C#4 is not in C major: goes to C4 or D4
    run(*m, s2);
    const double hz2 = crossingsHz(s2.l, 48000, 48000);
    std::printf("  MADE MAN: C#4 sung in C major -> %.1f Hz\n", hz2);
    CHECK(std::fabs(hz2 - 261.63) < 2.0 || std::fabs(hz2 - 293.66) < 2.0);
}

void testDetective() {
    using namespace detective;
    auto e = make("THE DETECTIVE");
    Stereo c = chord({130.81, 261.63, 329.63, 392.0}, 8.0, 0.1f);   // C major
    run(*e, c);
    const float k1 = e->meter(0), sure1 = e->meter(1);
    e->set(Reset, 1);
    Stereo a = chord({220.0, 261.63, 329.63, 110.0}, 8.0, 0.1f);    // A minor
    run(*e, a);
    const float k2 = e->meter(0);
    std::printf("  THE DETECTIVE: C major chord -> key %.0f (sure %.0f %%), A minor chord -> key %.0f\n", k1, sure1, k2);
    CHECK(k1 == 0.0f && k2 == 21.0f);
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
    {
        auto e = make("LIVE WIRE", {{On, 1}, {Wave, 0}, {Freq, 1000}, {Level, -18}, {Channels, 1}});
        Stereo s = sine(300, 1.0, 0.3f);
        run(*e, s);
        const double tone = db(toneAmp(s.l, 1000, 24000, 24000)), input = db(toneAmp(s.l, 300, 24000, 24000));
        std::printf("  LIVE WIRE sine 1 kHz at -18 dBFS, left only, replace: left %.1f dB, input left %.1f dB, right peak %.2e\n", tone, input,
                    static_cast<double>(*std::max_element(s.r.begin() + 24000, s.r.end(), [](float x, float y) { return std::fabs(x) < std::fabs(y); })));
        CHECK(std::fabs(tone + 18.0) < 0.2 && input < -80.0);
        double rpk = 0;
        for (std::size_t i = 24000; i < s.size(); ++i) rpk = std::max(rpk, static_cast<double>(std::fabs(s.r[i])));
        CHECK(rpk < 1e-6);
    }
    {
        auto e = make("LIVE WIRE");
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  LIVE WIRE off: sound changed by %.1e\n", diff);
        CHECK(diff == 0.0);
    }
}

} // namespace

void testStreets() {
    std::printf("[the streets]\n");
    allRules();
    testTorch();
    testHotWire();
    testBottomFeeder();
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
