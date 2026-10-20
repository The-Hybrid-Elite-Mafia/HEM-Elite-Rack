// The hideout (space, modulation, voice, monitoring): what each plug-in must actually do to the
// sound, measured, plus the rules every HEM effect follows (Rules.hpp).
#include "Harness.hpp"
#include "Rules.hpp"
#include "Signals.hpp"

#include "hem/rack/Hideout.hpp"
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
double toneGain(hc::fx::Effect& e, double hz, float amp, bool right = false, double seconds = 1.5) {
    e.reset();
    Stereo s = sine(hz, seconds, amp);
    run(e, s);
    return db(toneAmp(right ? s.r : s.l, hz, s.size() - 12000, 12000) / amp);
}
Stereo impulse(std::size_t n, std::size_t at = 100) {
    Stereo s(n);
    s.l[at] = s.r[at] = 1.0f;
    return s;
}
// where the biggest sample in [from, to) is
std::size_t peakAt(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double pk = -1;
    std::size_t where = from;
    for (std::size_t i = from; i < to && i < x.size(); ++i)
        if (std::fabs(x[i]) > pk) { pk = std::fabs(x[i]); where = i; }
    return where;
}
double peakIn(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double pk = 0;
    for (std::size_t i = from; i < to && i < x.size(); ++i) pk = std::max(pk, static_cast<double>(std::fabs(x[i])));
    return pk;
}

void allRules() {
    for (const char* name : {"THE FIXER", "SIX SHOOTER", "DEAD DROP", "BACK ALLEY", "DRIVE-BY", "BODY DOUBLE", "HUSTLE", "THE INFORMANT", "STRAPPED", "SAFE HOUSE"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

void testFixer() {
    using namespace fixer;
    // HARSH AMOUNT 60: threshold -40 dBFS, at most 10.8 dB off
    auto e = make("THE FIXER", {{band(1, Amount), 60}, {band(1, Freq), 100}});
    const double loud = toneGain(*e, 3000, 0.5f);
    const float m = e->meter(1);
    const double quiet = toneGain(*e, 3000, 0.003f), other = toneGain(*e, 300, 0.5f);
    std::printf("  THE FIXER harsh band (3 kHz, AMOUNT 60): loud %+.1f dB (meter %.1f), quiet %+.1f, a loud 300 Hz %+.1f dB\n", loud, m, quiet, other);
    CHECK(loud < -9.0 && loud > -11.5 && std::fabs(quiet) < 0.2 && std::fabs(other) < 1.0 && m < -8.0f);
    // AMOUNT 0 and BYPASS do nothing
    auto z = make("THE FIXER", {{band(1, Amount), 60}, {band(1, Freq), 100}, {band(1, Bypass), 1}});
    const double byp = toneGain(*z, 3000, 0.5f);
    auto l = make("THE FIXER", {{band(1, Listen), 1}, {band(1, Freq), 100}});
    const double in = toneGain(*l, 3000, 0.003f), out = toneGain(*l, 300, 0.003f);
    std::printf("  THE FIXER BYPASS: %+.2f dB; LISTEN HARSH: 3 kHz %+.1f dB, 300 Hz %+.1f dB\n", byp, in, out);
    CHECK(std::fabs(byp) < 0.05 && std::fabs(in) < 0.5 && out < -15.0);
    const double d = blockSizeDifference(*findPlugin("THE FIXER"), [](hc::fx::Effect& x) { x.set(band(0, Amount), 70); x.set(band(1, Amount), 70); },
                                         noise(0.6, 0.5f));
    std::printf("  THE FIXER block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testDeadDrop() {
    using namespace deaddrop;
    // MS 250, feedback 50 %, clean: repeats at 250 / 500 ms, 6 dB apart
    {
        auto e = make("DEAD DROP", {{Sync, 2}, {Time, 250}, {Feedback, 50}, {Analog, 0}, {Mix, 100}});
        Stereo s = impulse(30000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 1000, 18000) - 100, b = peakAt(s.l, 18000, 30000) - 100;
        const double fall = db(peakIn(s.l, 1000, 18000) / peakIn(s.l, 18000, 30000));
        std::printf("  DEAD DROP 250 ms, feedback 50 %%: repeats at %.1f / %.1f ms, %.1f dB apart\n", a / 48.0, b / 48.0, fall);
        CHECK(a == 12000 && b == 24000 && std::fabs(fall - 6.02) < 0.3);
    }
    // HOST sync, 1/8 D at 120 BPM = 375 ms; DUAL: right at 1/4 = 500 ms
    {
        auto e = make("DEAD DROP", {{Sync, 0}, {Note, 8}, {Dual, 1}, {NoteR, 10}, {Feedback, 0}, {Analog, 0}});
        e->setTempo(120);
        Stereo s = impulse(30000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 1000, 30000) - 100, b = peakAt(s.r, 1000, 30000) - 100;
        std::printf("  DEAD DROP HOST 120 BPM: 1/8 D left at %.1f ms, DUAL 1/4 right at %.1f ms\n", a / 48.0, b / 48.0);
        CHECK(a == 18000 && b == 24000);
    }
    // feedback 150 % builds up but the analog stage holds it in
    {
        auto e = make("DEAD DROP", {{Sync, 2}, {Time, 50}, {Feedback, 150}, {Analog, 2}});
        Stereo s = noise(3.0, 0.1f);
        run(*e, s);
        double pk = 0;
        for (float v : s.l) pk = std::max(pk, static_cast<double>(std::fabs(v)));
        std::printf("  DEAD DROP feedback 150 %%, analog 2: peak after 3 s %.2f (it builds, it doesn't run away)\n", pk);
        CHECK(pk > 0.3 && pk < 4.0 && std::isfinite(pk));
    }
    // PING PONG from the left: first repeat left, the next one right
    {
        auto e = make("DEAD DROP", {{Sync, 2}, {Time, 100}, {Feedback, 80}, {PingPong, 1}, {Analog, 0}});
        Stereo s = impulse(20000);
        run(*e, s);
        const double l1 = peakIn(s.l, 4000, 6000), r1 = peakIn(s.r, 4000, 6000), r2 = peakIn(s.r, 9000, 11000), l2 = peakIn(s.l, 9000, 11000);
        std::printf("  DEAD DROP PING PONG: first repeat L %.2f / R %.2f, second L %.2f / R %.2f\n", l1, r1, l2, r2);
        CHECK(l1 > 0.3 && r1 < 1e-4 && r2 > 0.2 && l2 < 1e-4);
    }
    const double d = blockSizeDifference(*findPlugin("DEAD DROP"), [](hc::fx::Effect& x) { x.set(ModDepth, 30); x.set(LowPass, 5000); x.set(LoFi, 1); },
                                         noise(1.0, 0.3f));
    std::printf("  DEAD DROP block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testSixShooter() {
    using namespace sixshooter;
    auto off = [](hc::fx::Effect& e) { for (int t = 0; t < kTaps; ++t) e.set(tap(t, On), 0); };
    // one tap: it lands at its time, at its gain, turned by ROTATE (+45 = right only)
    {
        auto e = make("SIX SHOOTER", {{DirectOn, 0}});
        off(*e);
        e->set(tap(2, On), 1); e->set(tap(2, Delay), 200); e->set(tap(2, Gain), -6); e->set(tap(2, Rotate), 45);
        e->reset();
        Stereo s = impulse(20000);
        s.r = s.l;
        run(*e, s);
        const std::size_t at = peakAt(s.r, 1000, 20000) - 100;
        const double lev = db(std::fabs(s.r[at + 100])), left = peakIn(s.l, 1000, 20000);
        std::printf("  SIX SHOOTER tap 3 at 200 ms, -6 dB, rotate +45: at %.1f ms, %+.1f dB on the right (+3 from both sides), left %.1e\n", at / 48.0, lev, left);
        CHECK(at == 9600 && std::fabs(lev - (-6.0 + 3.01)) < 0.1 && left < 1e-6);
    }
    // SNAP to 1/8 at 120 BPM (MANUAL): 260 ms lands on 250
    {
        auto e = make("SIX SHOOTER", {{DirectOn, 0}, {Sync, 1}, {Bpm, 120}, {Grid, 3}, {Snap, 1}});
        off(*e);
        e->set(tap(0, On), 1); e->set(tap(0, Delay), 260);
        e->reset();
        Stereo s = impulse(20000);
        run(*e, s);
        const std::size_t at = peakAt(s.l, 1000, 20000) - 100;
        std::printf("  SIX SHOOTER SNAP 1/8 at 120 BPM: 260 ms lands at %.1f ms\n", at / 48.0);
        CHECK(at == 12000);
    }
    // FEEDBACK NORMAL 50 %: each round 6 dB down (the shelf at 0 dB: no filtering)
    {
        auto e = make("SIX SHOOTER", {{DirectOn, 0}, {FbOn, 1}, {FbGain, 50}, {FbFilter, 1}, {FbFilterGain, 0}});
        off(*e);
        e->set(tap(0, On), 1); e->set(tap(0, Delay), 100); e->set(tap(0, Gain), 0);
        e->reset();
        Stereo s = impulse(24000);
        run(*e, s);
        const double first = peakIn(s.l, 4000, 6000), second = peakIn(s.l, 8800, 10800);
        std::printf("  SIX SHOOTER FEEDBACK 50 %%: second round %.1f dB under the first\n", db(first / second));
        CHECK(std::fabs(db(first / second) - 6.0) < 1.0);
    }
    // the tap EQ: a high-pass at 2 kHz on tap 1 takes the lows out of it
    {
        auto e = make("SIX SHOOTER", {{DirectOn, 0}});
        off(*e);
        e->set(tap(0, On), 1); e->set(tap(0, Delay), 10); e->set(tap(0, Gain), 0); e->set(tap(0, Rotate), 0);
        e->set(tap(0, EqOn), 1); e->set(tap(0, EqType), 4); e->set(tap(0, EqFreq), 2000);
        const double lo = toneGain(*e, 200, 0.2f), hi = toneGain(*e, 8000, 0.2f);
        std::printf("  SIX SHOOTER tap EQ high-pass 2 kHz: 200 Hz %.1f dB, 8 kHz %+.1f dB\n", lo, hi);
        CHECK(lo < -30.0 && std::fabs(hi) < 0.5);
    }
    const double d = blockSizeDifference(*findPlugin("SIX SHOOTER"), [](hc::fx::Effect& x) { x.set(ModOn, 1); x.set(FbOn, 1); x.set(tap(1, EqOn), 1); },
                                         noise(1.0, 0.3f));
    std::printf("  SIX SHOOTER block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testBackAlley() {
    using namespace backalley;
    {
        auto e = make("BACK ALLEY", {{ReverbOn, 0}, {Sync, 1}, {Time, 200}, {Feedback, 0}, {EchoLevel, 0}, {Mix, 100}});
        Stereo s = impulse(24000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 2000, 20000);
        std::printf("  BACK ALLEY echo at 200 ms (it wobbles a little): found at %.1f ms\n", (a - 100) / 48.0);
        CHECK(a > 9620 && a < 9780);
    }
    {
        auto e = make("BACK ALLEY", {{EchoOn, 0}, {Length, 1.0f}, {ReverbLevel, 0}, {Mix, 100}});
        Stereo s = noise(0.2, 0.5f);
        s.l.resize(96000, 0.0f);
        s.r.resize(96000, 0.0f);
        run(*e, s);
        const double early = rms(s.l, 9600, 9600), late = rms(s.l, 9600 + 48000, 9600);
        std::printf("  BACK ALLEY reverb (length 1 s): tail just after %.1f dB, one second later %.1f dB\n", db(early), db(late));
        CHECK(db(early) > -40.0 && db(late) < db(early) - 40.0);
    }
    {
        auto e = make("BACK ALLEY", {{Mono, 0}});
        Stereo s = noise(0.5, 0.3f);
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        auto m = make("BACK ALLEY", {{Mix, 0}});
        Stereo t = noise(0.5, 0.3f), ref = t;
        run(*m, t);
        double dry = 0;
        for (std::size_t i = 0; i < t.size(); ++i) dry = std::max(dry, static_cast<double>(std::fabs(t.l[i] - ref.l[i])));
        std::printf("  BACK ALLEY MONO 0: largest left/right difference %.2e; MIX 0: off the input by %.1e\n", diff, dry);
        CHECK(diff < 1e-5 && dry < 1e-6);
    }
    const double d = blockSizeDifference(*findPlugin("BACK ALLEY"), [](hc::fx::Effect& x) { x.set(PingPong, 1); x.set(Type, 1); }, noise(1.0, 0.3f));
    std::printf("  BACK ALLEY block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

// average frequency (Hz) from the zero crossings in [from, from + len)
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

void testDriveBy() {
    using namespace driveby;
    // a 2 s pass, CLOSE 70 % (about 3 m): coming in it is higher, going away lower; PITCH 0 = no bend
    {
        auto e = make("DRIVE-BY", {{TrackTime, 2}, {Trigger, 1}, {Air, 0}, {ReverbOn, 0}});
        Stereo s = sine(1000, 2.0, 0.3f);
        run(*e, s);
        Stereo m(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) m.l[i] = s.l[i] + s.r[i];
        const double in = crossingsHz(m.l, 14400, 9600), out = crossingsHz(m.l, 72000, 9600);
        auto f = make("DRIVE-BY", {{TrackTime, 2}, {Trigger, 1}, {Air, 0}, {ReverbOn, 0}, {PitchOn, 0}});
        Stereo t = sine(1000, 2.0, 0.3f);
        run(*f, t);
        Stereo mt(t.size());
        for (std::size_t i = 0; i < t.size(); ++i) mt.l[i] = t.l[i] + t.r[i];
        const double flat = crossingsHz(mt.l, 14400, 9600);
        std::printf("  DRIVE-BY 120 m in 2 s: approaching %.0f Hz, leaving %.0f Hz (a 1 kHz tone); PITCH off: %.0f Hz\n", in, out, flat);
        CHECK(in > 1100 && in < 1250 && out > 820 && out < 920 && std::fabs(flat - 1000) < 5);
    }
    // PAN: at the start it is on the left, at the end on the right
    {
        auto e = make("DRIVE-BY", {{TrackTime, 2}, {Trigger, 1}, {Air, 0}, {ReverbOn, 0}});
        Stereo s = sine(1000, 2.0, 0.3f);
        run(*e, s);
        const double lStart = rms(s.l, 2400, 4800), rStart = rms(s.r, 2400, 4800), lEnd = rms(s.l, 86400, 4800), rEnd = rms(s.r, 86400, 4800);
        std::printf("  DRIVE-BY pan: start L/R %.1f / %.1f dB, end %.1f / %.1f dB\n", db(lStart), db(rStart), db(lEnd), db(rEnd));
        CHECK(lStart > rStart * 3.0 && rEnd > lEnd * 3.0);
    }
    // ONE-SHOT, ENERGY: nothing until the sound starts; silence stays silent
    {
        auto e = make("DRIVE-BY", {{Mode, 1}});
        Stereo z(48000);
        run(*e, z);
        CHECK(peak(z) < 1e-9 && e->meter(0) < 0.01f);
    }
    const double d = blockSizeDifference(*findPlugin("DRIVE-BY"), [](hc::fx::Effect& x) { x.set(TrackTime, 1.5f); x.set(Trigger, 1); }, noise(1.0, 0.3f));
    std::printf("  DRIVE-BY block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testBodyDouble() {
    using namespace bodydouble;
    // voice 1 sharp on the left, voice 2 flat on the right (the others off, the direct off)
    {
        auto e = make("BODY DOUBLE", {{DirectOn, 0}, {voice(2, On), 0}, {voice(3, On), 0}, {voice(0, Detune), 30}, {voice(1, Detune), -30},
                                      {voice(0, Gain), 0}, {voice(1, Gain), 0}});
        Stereo s = sine(1000, 2.0, 0.3f);
        run(*e, s);
        const double hi = 1000.0 * std::exp2(30.0 / 1200.0), lo = 1000.0 * std::exp2(-30.0 / 1200.0);
        const double upL = db(toneAmp(s.l, hi, 48000, 48000)), downL = db(toneAmp(s.l, lo, 48000, 48000));
        const double upR = db(toneAmp(s.r, hi, 48000, 48000)), downR = db(toneAmp(s.r, lo, 48000, 48000));
        std::printf("  BODY DOUBLE voice 1 +30 c left, voice 2 -30 c right: left sharp %.1f / flat %.1f dB, right sharp %.1f / flat %.1f dB\n", upL, downL,
                    upR, downR);
        CHECK(upL > downL + 10.0 && downR > upR + 10.0);
    }
    // OCTAVE: a voice an octave down
    {
        auto e = make("BODY DOUBLE", {{DirectOn, 0}, {voice(1, On), 0}, {voice(2, On), 0}, {voice(3, On), 0}, {voice(0, Detune), 0},
                                      {voice(0, Octave), 1}, {voice(0, Pan), 0}, {voice(0, Gain), 0}});
        Stereo s = sine(800, 2.0, 0.3f);
        run(*e, s);
        const double down = db(toneAmp(s.l, 400, 48000, 48000)), orig = db(toneAmp(s.l, 800, 48000, 48000));
        std::printf("  BODY DOUBLE OCTAVE on an 800 Hz tone: 400 Hz %.1f dB, 800 Hz %.1f dB\n", down, orig);
        CHECK(down > orig + 15.0);
    }
    // the voices only: no delay, no detune -> the direct and the voice add up (ALIGN DIRECT lines them up)
    {
        auto e = make("BODY DOUBLE", {{voice(1, On), 0}, {voice(2, On), 0}, {voice(3, On), 0}, {voice(0, Detune), 0}, {voice(0, Delay), 0},
                                      {voice(0, Pan), 0}, {voice(0, Gain), 0}, {AlignDirect, 1}, {Range, 1}});
        const double g = toneGain(*e, 200, 0.2f, false, 2.0);
        std::printf("  BODY DOUBLE one voice at 0 ms / 0 cents with ALIGN DIRECT: %+.1f dB (two in step: +6)\n", g);
        CHECK(g > 4.5 && g < 6.5);
    }
    const double d = blockSizeDifference(*findPlugin("BODY DOUBLE"), [](hc::fx::Effect& x) { x.set(voice(0, ModDepth), 30); x.set(voice(1, Feedback), 20); },
                                         noise(1.0, 0.3f));
    std::printf("  BODY DOUBLE block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testHustle() {
    using namespace hustle;
    {
        auto e = make("HUSTLE", {{AmOn, 0}, {FmOn, 0}, {RotOn, 0}});
        CHECK(e->latency() > 0);
        Stereo s = noise(0.5, 0.3f), ref = s;
        run(*e, s);
        double diff = 0;
        const std::uint32_t lat = e->latency();
        for (std::size_t i = lat; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i - lat])));
        std::printf("  HUSTLE with nothing moving: the input %u samples late, largest difference %.2e\n", lat, diff);
        CHECK(diff < 1e-5);
    }
    {
        auto e = make("HUSTLE", {{AmDepth, 100}, {FmOn, 0}, {RotOn, 0}, {Rate, 4}});
        Stereo s = sine(1000, 1.0, 0.5f);
        run(*e, s);
        double lo = 1e9, hi = 0;
        for (std::size_t w = 12000; w + 480 < s.size(); w += 480) {
            const double r = rms(s.l, w, 480);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        std::printf("  HUSTLE AM 100 %% at 4 Hz: loudest %.1f dB, quietest %.1f dB\n", db(hi / (0.5 / std::sqrt(2.0))), db(lo / (0.5 / std::sqrt(2.0))));
        CHECK(db(hi / (0.5 / std::sqrt(2.0))) > -1.0 && db(lo / (0.5 / std::sqrt(2.0))) < -20.0);
    }
    {
        auto e = make("HUSTLE", {{AmOn, 0}, {FmOn, 0}, {RotRange, 180}, {Rate, 1}});
        Stereo s = sine(1000, 1.5, 0.5f);
        run(*e, s);
        double most = 0;
        for (std::size_t w = 6000; w + 960 < s.size(); w += 960) most = std::max(most, std::fabs(db(rms(s.l, w, 960) / std::max(rms(s.r, w, 960), 1e-9))));
        auto c = make("HUSTLE", {{AmOn, 0}, {FmOn, 0}, {RotRange, 1}, {RotCenter, 90}});
        const double l = toneGain(*c, 1000, 0.3f), r = toneGain(*c, 1000, 0.3f, true);
        std::printf("  HUSTLE rotation range 180: the picture swings as far as %.1f dB to one side; CENTER +90: left %+.1f, right %.1f dB\n", most, l, r);
        CHECK(most > 20.0 && l > r + 20.0);
    }
    {
        auto e = make("HUSTLE", {{Sync, 0}, {Factor, 3}, {AmDepth, 100}, {FmOn, 0}, {RotOn, 0}});
        e->setTempo(120);   // AUTO: 2 cycles a second, x2 = 4 Hz
        Stereo s = sine(1000, 2.0, 0.5f);
        run(*e, s);
        int dips = 0;
        bool low = false;
        for (std::size_t w = 0; w + 240 < s.size(); w += 240) {
            const bool now = rms(s.l, w, 240) < 0.1;
            if (now && !low) ++dips;
            low = now;
        }
        std::printf("  HUSTLE AUTO at 120 BPM, x2: %d dips in 2 s\n", dips);
        CHECK(dips >= 7 && dips <= 9);
    }
}

void testInformant() {
    using namespace informant;
    {
        auto e = make("THE INFORMANT", {{Note, 9}, {Octave, 1}, {Sibilance, 0}});   // A3 = 220 Hz
        Stereo s = noise(1.5, 0.2f);
        const double inRms = rms(s.l, 24000, 24000);
        run(*e, s);
        const double outRms = rms(s.l, 24000, 24000);
        const double h = toneAmp(s.l, 220, 24000, 48000), off = toneAmp(s.l, 300, 24000, 48000);
        std::printf("  THE INFORMANT noise in, A3 carrier: level %+.1f dB vs the input, 220 Hz is %.1f dB over 300 Hz\n", db(outRms / inRms), db(h / off));
        CHECK(std::fabs(db(outRms / inRms)) < 6.0 && db(h / off) > 15.0);
    }
    {
        auto e = make("THE INFORMANT", {{Mix, 0}});
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  THE INFORMANT mix 0: largest change %.2e\n", diff);
        CHECK(diff < 1e-6);
    }
}

void testStrapped() {
    using namespace strapped;
    {
        auto e = make("STRAPPED", {{Delay, 100}, {Note, 4}, {Tight, 20}});   // a quarter note at 120 BPM: 500 ms
        Stereo s = impulse(36000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 4000, 36000);
        std::printf("  STRAPPED delay, a quarter note at 120 BPM: echo at %.1f ms\n", (a - 100) / 48.0);
        CHECK(a > 24050 && a < 24200);
    }
    {
        auto e = make("STRAPPED", {{Width, 0}, {Chorus, 100}, {Room, 100}});
        Stereo s = noise(0.5, 0.3f);
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  STRAPPED width 0: largest left/right difference %.2e\n", diff);
        CHECK(diff < 1e-5);
    }
}

void testSafeHouse() {
    using namespace safehouse;
    auto e = make("SAFE HOUSE", {{Room, 0}, {Crossfeed, 100}, {Angle, 30}});
    Stereo s(4800);
    s.l[100] = 1.0f;
    run(*e, s);
    const std::size_t r = peakAt(s.r, 90, 200);
    std::printf("  SAFE HOUSE: a click on the left reaches the right ear %.2f ms later (30 degrees)\n", (r - 100) / 48.0);
    CHECK(r >= 110 && r <= 116);
    auto m = make("SAFE HOUSE", {{Room, 0}, {Crossfeed, 100}});
    const double g = toneGain(*m, 200, 0.3f);
    std::printf("  SAFE HOUSE mono 200 Hz level change: %+.1f dB\n", g);
    CHECK(std::fabs(g) < 3.0);
}

} // namespace

void testHideout() {
    std::printf("[the hideout]\n");
    allRules();
    testFixer();
    testDeadDrop();
    testSixShooter();
    testBackAlley();
    testDriveBy();
    testBodyDouble();
    testHustle();
    testInformant();
    testStrapped();
    testSafeHouse();
}

} // namespace hem::test
