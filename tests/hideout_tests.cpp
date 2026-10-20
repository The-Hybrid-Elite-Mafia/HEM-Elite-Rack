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
    for (const char* name : {"SPEAKEASY", "BIG HOUSE", "MOUTHPIECE", "SLINGER", "STREET TEAM", "COLD CASE", "SWERVE", "THE FIXER", "SIX SHOOTER", "DEAD DROP", "BACK ALLEY", "DRIVE-BY", "BODY DOUBLE", "HUSTLE", "THE INFORMANT", "STRAPPED", "SAFE HOUSE"}) {
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

void testSwerve() {
    using namespace swerve;
    {   // DEPTH 0, MIX 100: a click comes out DELAY later
        auto e = make("SWERVE", {{Depth, 0}, {Mix, 100}, {Feedback, 0}, {Delay, 2.2f}});
        Stereo s = impulse(4800);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 0, 4800);
        std::printf("  SWERVE delay 2.2 ms: the click at %.2f ms\n", (a - 100) / 48.0);
        CHECK(a >= 205 && a <= 206);
    }
    {   // MIX 50 at 1 ms, no sweep: a notch at 500 Hz, a peak at 1 kHz; WET FLIP swaps them
        auto e = make("SWERVE", {{Depth, 0}, {Mix, 50}, {Feedback, 0}, {Delay, 1.0f}});
        const double n500 = toneGain(*e, 500, 0.3f), p1k = toneGain(*e, 1000, 0.3f);
        auto f = make("SWERVE", {{Depth, 0}, {Mix, 50}, {Feedback, 0}, {Delay, 1.0f}, {WetFlip, 1}});
        const double f500 = toneGain(*f, 500, 0.3f), f1k = toneGain(*f, 1000, 0.3f);
        std::printf("  SWERVE 1 ms comb: 500 Hz %.1f dB, 1 kHz %+.1f dB; flipped: 500 Hz %+.1f dB, 1 kHz %.1f dB\n", n500, p1k, f500, f1k);
        CHECK(n500 < -30.0 && std::fabs(p1k) < 0.5 && std::fabs(f500) < 0.5 && f1k < -30.0);
    }
    {   // FEEDBACK deepens the peaks
        auto e = make("SWERVE", {{Depth, 0}, {Mix, 50}, {Feedback, 80}, {Delay, 1.0f}});
        const double p1k = toneGain(*e, 1000, 0.1f);
        std::printf("  SWERVE feedback 80: 1 kHz peak %+.1f dB\n", p1k);
        CHECK(p1k > 4.0);
    }
    {   // TAPE: the dry waits too, so with no sweep nothing cancels
        auto e = make("SWERVE", {{Depth, 0}, {Mix, 50}, {Feedback, 0}, {Delay, 1.0f}, {Tape, 1}});
        const double g = toneGain(*e, 500, 0.3f);
        std::printf("  SWERVE tape, no sweep: 500 Hz %+.2f dB\n", g);
        CHECK(std::fabs(g) < 0.2);
    }
    {   // HOLD stops the sweep where it is (at the start: the middle) = no sweep at all
        auto held = make("SWERVE", {{Rate, 5}, {Hold, 1}, {Feedback, 30}});
        auto still = make("SWERVE", {{Rate, 5}, {Depth, 0}, {Feedback, 30}});
        Stereo a = noise(0.5, 0.2f), b = a;
        run(*held, a);
        run(*still, b);
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        std::printf("  SWERVE hold vs no sweep: largest difference %.2e\n", d);
        CHECK(d < 1e-5);
    }
    {   // STEREO 0 keeps the sides the same on a mono input, 180 pulls them apart
        auto e0 = make("SWERVE", {{Rate, 2}, {StereoOffset, 0}}), e1 = make("SWERVE", {{Rate, 2}, {StereoOffset, 180}});
        Stereo a = noise(0.5, 0.2f);
        for (std::size_t i = 0; i < a.size(); ++i) a.r[i] = a.l[i];
        Stereo b = a;
        run(*e0, a);
        run(*e1, b);
        double d0 = 0, d1 = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            d0 = std::max(d0, static_cast<double>(std::fabs(a.l[i] - a.r[i])));
            d1 = std::max(d1, static_cast<double>(std::fabs(b.l[i] - b.r[i])));
        }
        std::printf("  SWERVE stereo 0: left/right difference %.1e; stereo 180: %.3f\n", d0, d1);
        CHECK(d0 < 1e-6 && d1 > 0.05);
    }
    {   // TIE: with RATE doubled after TIE went on, the depth heard halves - the same as DEPTH 50 at that rate
        auto tied = make("SWERVE", {{Depth, 100}, {Rate, 1}, {Tie, 1}, {Hold, 1}});
        auto plain = make("SWERVE", {{Depth, 50}, {Rate, 2}, {Hold, 1}});
        Stereo z(24000), z2(24000);   // settle the smoothing while held
        run(*tied, z);
        run(*plain, z2);
        tied->set(Rate, 2);
        tied->set(Hold, 0);
        plain->set(Hold, 0);
        Stereo a = noise(0.5, 0.2f), b = a;
        run(*tied, a);
        run(*plain, b);
        double d = 0;
        for (std::size_t i = 14400; i < a.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        std::printf("  SWERVE tie: rate x2 = depth 50 heard (once the move settles); largest difference %.2e\n", d);
        CHECK(d < 0.01);
        auto full = make("SWERVE", {{Depth, 100}, {Rate, 2}, {Hold, 1}});   // without TIE the same moves keep depth 100: clearly different
        Stereo z3(24000);
        run(*full, z3);
        full->set(Hold, 0);
        Stereo c = noise(0.5, 0.2f);
        run(*full, c);
        double dd = 0;
        for (std::size_t i = 14400; i < c.size(); ++i) dd = std::max(dd, static_cast<double>(std::fabs(c.l[i] - b.l[i])));
        std::printf("  SWERVE without tie (depth 100 at rate 2) vs depth 50: largest difference %.3f\n", dd);
        CHECK(dd > 0.05);
    }
    {   // SYNC HOST: RATE 1 = one sweep a beat; at 120 BPM that is 2 Hz, the same as RATE 2 free
        auto host = make("SWERVE", {{Sync, 1}, {Rate, 1}});
        auto free = make("SWERVE", {{Sync, 0}, {Rate, 2}});
        Stereo a = noise(0.5, 0.2f), b = a;
        run(*host, a);
        run(*free, b);
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        std::printf("  SWERVE sync host 1 per beat at 120 BPM = 2 Hz free: largest difference %.2e\n", d);
        CHECK(d < 1e-5);
    }
    {   // FILTER LOW-PASS 500 Hz takes the wet copy out of the highs: no comb up there
        auto e = make("SWERVE", {{Depth, 0}, {Mix, 50}, {Feedback, 0}, {Delay, 1.0f}, {FilterType, 1}, {FilterFreq, 500}});
        const double hi = toneGain(*e, 8500, 0.3f);   // a notch frequency without the filter
        std::printf("  SWERVE filter low-pass 500 Hz: 8.5 kHz %+.1f dB (only the dry half)\n", hi);
        CHECK(std::fabs(hi + 6.0) < 1.0);
    }
}

void testColdCase() {
    using namespace coldcase;
    auto still = [](std::initializer_list<std::pair<int, float>> more) {   // no sweep, no loop, all wet, at 1 kHz
        auto e = findPlugin("COLD CASE")->create();
        for (const auto& [i, v] : std::initializer_list<std::pair<int, float>>{{Run, 0}, {LowHz, 1000}, {HighHz, 1000}, {LoopOn, 0}, {Mix, 100}, {Depth, 100}, {Notches, 0}})
            e->set(i, v);
        for (const auto& [i, v] : more) e->set(i, v);
        e->prepare(kFs, 512);
        return e;
    };
    {
        auto e = still({});
        auto f = still({{NotchFlip, 1}});
        const double a = toneGain(*e, 1000, 0.2f), b = toneGain(*f, 1000, 0.2f);
        std::printf("  COLD CASE notch at 1 kHz: %.1f dB; flipped: %+.1f dB\n", a, b);
        CHECK(a < -25.0 && std::fabs(b - 6.0) < 0.3);
    }
    {   // DEPTH 0, no loop: untouched
        auto e = still({{Depth, 0}});
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  COLD CASE depth 0: largest change %.2e\n", d);
        CHECK(d < 1e-5);
    }
    {   // the loop: an echo DELAY later; DENSITY 100 makes the right one twice as late
        auto e = still({{Depth, 0}, {LoopOn, 1}, {Decay, 80}, {Delay, 5}, {Density, 100}, {HiTame, 0}});
        Stereo s = impulse(4800);
        run(*e, s);
        const std::size_t l = peakAt(s.l, 110, 4800), r = peakAt(s.r, 110, 4800);
        std::printf("  COLD CASE loop 5 ms, density 100: left echo %.2f ms, right %.2f ms after\n", (l - 100) / 48.0, (r - 100) / 48.0);
        CHECK(l >= 339 && l <= 341 && r >= 579 && r <= 581);
    }
    {   // PRE-DELAY 10 ms
        auto e = still({{Depth, 0}, {PreDelay, 10}});
        Stereo s = impulse(4800);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 0, 4800);
        std::printf("  COLD CASE pre-delay 10 ms: %.2f ms\n", (a - 100) / 48.0);
        CHECK(a == 580);
    }
    {   // WET MODE MONO: both sides the same; MONO FLIP of a mono input: nothing left
        auto e = still({{Depth, 0}, {WetMode, 1}});
        auto f = still({{Depth, 0}, {WetMode, 2}});
        Stereo a = noise(0.3, 0.2f), b = a;
        for (std::size_t i = 0; i < b.size(); ++i) b.r[i] = b.l[i];
        run(*e, a);
        run(*f, b);
        double d = 0, z = 0;
        for (std::size_t i = 0; i < a.size(); ++i) { d = std::max(d, static_cast<double>(std::fabs(a.l[i] - a.r[i]))); z = std::max(z, static_cast<double>(std::fabs(b.l[i]))); }
        std::printf("  COLD CASE wet mono: L-R %.1e; mono flip of mono: %.1e\n", d, z);
        CHECK(d < 1e-6 && z < 1e-6);
    }
    {   // the sweep moves the notch: 1 kHz comes and goes; RUN off holds it
        auto moving = still({{Run, 1}, {LowHz, 300}, {HighHz, 3000}, {Rate, 0.5f}});   // one sweep a second
        Stereo s = sine(1000.0, 2.0, 0.2f);
        run(*moving, s);
        double lo = 1e9, hi = 0;
        for (std::size_t b = 4800; b + 960 <= s.size(); b += 960) { const double v = rms(s.l, b, 960); lo = std::min(lo, v); hi = std::max(hi, v); }
        std::printf("  COLD CASE sweeping: 1 kHz level moves over %.1f dB\n", db(hi / std::max(lo, 1e-12)));
        CHECK(db(hi / std::max(lo, 1e-12)) > 10.0);
    }
    {   // TEMPO OWN at 60 BPM with RATE x2 = HOST (120 BPM) at RATE x1
        auto own = still({{Run, 1}, {LowHz, 300}, {HighHz, 3000}, {Tempo, 1}, {Bpm, 60}, {Rate, 0.5f}});
        auto host = still({{Run, 1}, {LowHz, 300}, {HighHz, 3000}, {Tempo, 0}, {Rate, 0.25f}});
        Stereo a = noise(0.5, 0.2f), b = a;
        run(*own, a);
        run(*host, b);
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        std::printf("  COLD CASE own tempo 60 x2 = host 120: largest difference %.2e\n", d);
        CHECK(d < 1e-5);
    }
    {   // MIX 0: dry
        auto e = still({{Mix, 0}, {LoopOn, 1}});
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  COLD CASE mix 0: largest change %.2e\n", d);
        CHECK(d < 1e-6);
    }
}

double outDb(hc::fx::Effect& e, double hz, float amp) {   // the steady level (dBFS) of a sine through e
    e.reset();
    Stereo s = sine(hz, 1.5, amp);
    run(e, s);
    return db(toneAmp(s.l, hz, s.size() - 12000, 12000));
}

void testVoiceDynamics() {
    {
        using namespace mouthpiece;
        auto flat = make("MOUTHPIECE");
        std::printf("  MOUTHPIECE at rest, -12 dBFS in: %.2f dBFS out\n", outDb(*flat, 1000, 0.25f));
        CHECK(std::fabs(outDb(*flat, 1000, 0.25f) - db(0.25)) < 0.1);
        auto e = make("MOUTHPIECE", {{Press, -30}});
        const double lo = outDb(*e, 1000, 0.03f), hi = outDb(*e, 1000, 0.5f);
        std::printf("  MOUTHPIECE press -30: 24 dB apart in, %.1f dB apart out\n", hi - lo);
        CHECK(hi - lo < 14.0);
        auto c = make("MOUTHPIECE", {{Ceiling, -12}});
        Stereo s = sine(1000, 1.0, 0.9f);
        run(*c, s);
        std::printf("  MOUTHPIECE ceiling -12: peak %.2f dBFS\n", db(peakIn(s.l, 4800, s.size())));
        CHECK(db(peakIn(s.l, 4800, s.size())) < -11.9);
        auto g = make("MOUTHPIECE", {{Gate, -40}});
        std::printf("  MOUTHPIECE gate -40, a -60 dBFS tone: %.1f dB down\n", -(outDb(*g, 1000, 0.001f) + 60.0));
        CHECK(outDb(*g, 1000, 0.001f) < -75.0);
    }
    {
        using namespace slinger;
        auto flat = make("SLINGER");
        CHECK(std::fabs(outDb(*flat, 440, 0.25f) - db(0.25)) < 0.1);
        auto e = make("SLINGER", {{Threshold, -40}});
        const double lo = outDb(*e, 440, 0.02f), hi = outDb(*e, 440, 0.5f);
        std::printf("  SLINGER threshold -40: 28 dB apart in, %.1f dB apart out\n", hi - lo);
        CHECK(hi - lo < 14.0);
        auto g = make("SLINGER", {{Gain, -10}});
        std::printf("  SLINGER gain -10: %.2f dB\n", outDb(*g, 440, 0.25f) - db(0.25));
        CHECK(std::fabs(outDb(*g, 440, 0.25f) - db(0.25) + 10.0) < 0.1);
        // ATTACK: a slow attack lets the start of a note through louder
        auto fast = make("SLINGER", {{Threshold, -40}, {Attack, 0}}), slow = make("SLINGER", {{Threshold, -40}, {Attack, 50}});
        Stereo a(12000), b(12000);
        for (std::size_t i = 4800; i < 12000; ++i) a.l[i] = a.r[i] = b.l[i] = b.r[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * i / 48000.0));
        run(*fast, a);
        run(*slow, b);
        const double pf = peakIn(a.l, 4800, 5280), ps = peakIn(b.l, 4800, 5280);
        std::printf("  SLINGER the first 10 ms of a note: attack 0 peak %.3f, attack 50 peak %.3f\n", pf, ps);
        CHECK(ps > pf * 1.3);
    }
    {
        using namespace streetteam;
        auto flat = make("STREET TEAM");
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*flat, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  STREET TEAM at rest: largest change %.2e\n", d);
        CHECK(d < 1e-5);
        auto bell = make("STREET TEAM", {{band(2, On), 1}, {band(2, Type), 1}, {band(2, Freq), 1000}, {band(2, BandGain), 12}, {band(2, Q), 6}});
        const double at = toneGain(*bell, 1000, 0.05f), off = toneGain(*bell, 2000, 0.05f);
        std::printf("  STREET TEAM bell +12 at 1 kHz Q 6: 1 kHz %+.1f dB, 2 kHz %+.1f dB\n", at, off);
        CHECK(std::fabs(at - 12.0) < 0.3 && std::fabs(off) < 1.0);
        auto hp = make("STREET TEAM", {{band(0, On), 1}, {band(0, Type), 0}, {band(0, Freq), 200}});
        std::printf("  STREET TEAM high-pass 200 Hz: 50 Hz %.1f dB\n", toneGain(*hp, 50, 0.1f));
        CHECK(toneGain(*hp, 50, 0.1f) < -20.0);
        auto comp = make("STREET TEAM", {{CompThreshold, -30}, {Ratio, 4}});
        std::printf("  STREET TEAM comp -30, 4:1, -6 dBFS in: %.1f dB down\n", -(outDb(*comp, 1000, 0.5f) - db(0.5)));
        CHECK(std::fabs((outDb(*comp, 1000, 0.5f) - db(0.5)) + 18.0) < 2.5);
        auto ex = make("STREET TEAM", {{CompThreshold, -30}, {Ratio, 0.5f}, {Output, -12}});
        std::printf("  STREET TEAM ratio 0.5 (expands), -20 dBFS in: %+.1f dB (output -12)\n", outDb(*ex, 1000, 0.1f) - db(0.1));
        CHECK(outDb(*ex, 1000, 0.1f) - db(0.1) > -4.0);
        auto gate = make("STREET TEAM", {{GateThreshold, -40}});
        auto floorG = make("STREET TEAM", {{GateThreshold, -40}, {Floor, -20}});
        const double g1 = outDb(*gate, 1000, 0.001f) + 60.0, g2 = outDb(*floorG, 1000, 0.001f) + 60.0;
        std::printf("  STREET TEAM gate -40 on a -60 dBFS tone: floor -96 %.1f dB, floor -20 %.1f dB\n", g1, g2);
        CHECK(g1 < -80.0 && std::fabs(g2 + 20.0) < 0.5);
        auto mute = make("STREET TEAM", {{Input, -60}});
        Stereo m = noise(0.3, 0.2f);
        run(*mute, m);
        CHECK(peakIn(m.l, 2400, m.size()) < 1e-6);
        auto up = make("STREET TEAM", {{Output, 6}});
        CHECK(std::fabs(outDb(*up, 1000, 0.1f) - db(0.1) - 6.0) < 0.1);
    }
}

// energy (dB) of x over [from, from + len)
double energyDb(const std::vector<float>& x, std::size_t from, std::size_t len) { return db(rms(x, from, len)); }

void testBigHouse() {
    using namespace bighouse;
    auto tailAt = [](std::initializer_list<std::pair<int, float>> v, double fromS, double lenS) {
        auto e = findPlugin("BIG HOUSE")->create();
        for (const auto& [i, x] : std::initializer_list<std::pair<int, float>>{{Mix, 100}, {EarlyLevel, -40}}) e->set(i, x);
        for (const auto& [i, x] : v) e->set(i, x);
        e->prepare(kFs, 512);
        Stereo s = impulse(static_cast<std::size_t>(5.0 * kFs));
        run(*e, s);
        return energyDb(s.l, static_cast<std::size_t>(fromS * kFs) + 100, static_cast<std::size_t>(lenS * kFs));
    };
    {   // TIME: the tail falls about 60 dB over TIME
        const double a = tailAt({{Time, 1.0f}}, 0.2, 0.1), b = tailAt({{Time, 1.0f}}, 1.2, 0.1);
        std::printf("  BIG HOUSE time 1 s: %.1f dB down from 0.2 s to 1.2 s\n", a - b);
        CHECK(a - b > 45.0 && a - b < 75.0);
        const double c = tailAt({{Time, 4.0f}}, 1.2, 0.1);
        CHECK(c > b + 20.0);
    }
    {   // HIGH RATIO 0.2: the highs die much sooner than the lows
        auto e = findPlugin("BIG HOUSE")->create();
        e->set(Mix, 100); e->set(EarlyLevel, -40); e->set(Time, 3.0f); e->set(HighRatio, 0.2f); e->set(HighDampHz, 3000);
        e->prepare(kFs, 512);
        Stereo s = noise(0.2, 0.3f);
        Stereo t(static_cast<std::size_t>(2.0 * kFs));
        for (std::size_t i = 0; i < s.size(); ++i) { t.l[i] = s.l[i]; t.r[i] = s.r[i]; }
        run(*e, t);
        const std::size_t at = static_cast<std::size_t>(1.0 * kFs);
        const double lo = bandLevelDb(t.l, 100, 400, at, 24000), hi = bandLevelDb(t.l, 6000, 12000, at, 24000);
        std::printf("  BIG HOUSE high ratio 0.2, 0.8 s after the noise: lows %.1f dB, highs %.1f dB\n", lo, hi);
        CHECK(lo - hi > 20.0);
    }
    {   // PRE-DELAY 100 ms: nothing wet for the first 100 ms; minus 100 ms: the dry is the one that waits
        auto e = make("BIG HOUSE", {{PreDelay, 100}, {Mix, 100}});
        Stereo s = impulse(12000);
        run(*e, s);
        std::printf("  BIG HOUSE pre-delay 100 ms: before %.2e, after %.2e\n", peakIn(s.l, 0, 4800 + 90), peakIn(s.l, 4800 + 100, 12000));
        CHECK(peakIn(s.l, 0, 4800 + 90) < 1e-6 && peakIn(s.l, 4800 + 100, 12000) > 1e-3);
        auto d = make("BIG HOUSE", {{PreDelay, -100}, {Mix, 0}});
        Stereo t = impulse(12000);
        run(*d, t);
        std::printf("  BIG HOUSE pre-delay -100 ms, dry only: the click at %.1f ms\n", (peakAt(t.l, 0, 12000) - 100) / 48.0);
        CHECK(peakAt(t.l, 0, 12000) == 4900);
    }
    {   // EARLY and REVERB levels at -40 = off: silence; MIX 0 = the dry untouched
        auto e = make("BIG HOUSE", {{EarlyLevel, -40}, {ReverbLevel, -40}, {Mix, 100}});
        Stereo s = noise(0.3, 0.3f);
        run(*e, s);
        CHECK(peakIn(s.l, 0, s.size()) < 1e-6);
        auto dry = make("BIG HOUSE", {{Mix, 0}});
        Stereo a = noise(0.3, 0.3f), ref = a;
        run(*dry, a);
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - ref.l[i])));
        std::printf("  BIG HOUSE mix 0: largest change %.2e\n", d);
        CHECK(d < 1e-6);
    }
    {   // GATED holds, then shuts: 0.6 s after a hit (TIME 1 s: hold 0.25 s) nothing is left; a natural hall still rings
        const double gated = tailAt({{Type, 8}, {Time, 1.0f}}, 0.6, 0.1), hall = tailAt({{Type, 0}, {Time, 1.0f}}, 0.6, 0.1);
        std::printf("  BIG HOUSE 0.6 s after a click: gated %.1f dB, hall %.1f dB\n", gated, hall);
        CHECK(gated < hall - 40.0);
        // BACKWARDS swells: later louder than earlier (TIME 4 s: it builds for 2 s)
        const double early = tailAt({{Type, 7}, {Time, 4.0f}}, 0.2, 0.1), late = tailAt({{Type, 7}, {Time, 4.0f}}, 1.6, 0.1);
        std::printf("  BIG HOUSE backwards: 0.2 s %.1f dB, 1.6 s %.1f dB\n", early, late);
        CHECK(late > early + 20.0);
        // DECAY 0.1 cuts a 4 s tail short
        const double natural = tailAt({{Time, 4.0f}}, 0.8, 0.1), cut = tailAt({{Time, 4.0f}, {Decay, 0.1f}}, 0.8, 0.1);
        std::printf("  BIG HOUSE decay 0.1 on a 4 s tail, at 0.8 s: %.1f dB vs %.1f dB\n", cut, natural);
        CHECK(cut < natural - 25.0);
    }
    {   // EQ LOW -24 = a cut: the tail loses its lows
        auto lows = [](float gain) {
            auto e = findPlugin("BIG HOUSE")->create();
            e->set(Mix, 100); e->set(EarlyLevel, -40); e->set(EqLowHz, 400); e->set(EqLowGain, gain);
            e->prepare(kFs, 512);
            Stereo t = noise(1.0, 0.3f);
            run(*e, t);
            return bandLevelDb(t.l, 50, 150, 24000, 24000);
        };
        const double flat = lows(0.0f), cut = lows(-24.0f);
        std::printf("  BIG HOUSE EQ low cut at 400 Hz: the tail's 50-150 Hz %.1f dB vs %.1f dB\n", cut, flat);
        CHECK(cut < flat - 12.0);
    }
}

void testSpeakeasy() {
    using namespace speakeasy;
    auto mk = [](std::initializer_list<std::pair<int, float>> v) {
        auto e = findPlugin("SPEAKEASY")->create();
        for (const auto& [i, x] : v) e->set(i, x);
        e->prepare(kFs, 512);
        return e;
    };
    {   // only the direct sound, at 1 m: untouched; at 10 m: 20 dB down
        auto e = mk({{Distance, 1}, {EarlyOn, 0}, {ReverbOn, 0}});
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double d = 0;
        for (std::size_t i = 0; i < s.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        CHECK(d < 1e-6);
        auto far = mk({{Distance, 10}, {EarlyOn, 0}, {ReverbOn, 0}});
        std::printf("  SPEAKEASY direct at 1 m: change %.1e; at 10 m: %.1f dB\n", d, toneGain(*far, 1000, 0.2f));
        CHECK(std::fabs(toneGain(*far, 1000, 0.2f) + 20.0) < 0.2);
    }
    auto firstAt = [&](std::initializer_list<std::pair<int, float>> v) {
        auto e = mk(v);
        Stereo s = impulse(48000);
        run(*e, s);
        for (std::size_t i = 101; i < s.size(); ++i)
            if (std::fabs(s.l[i]) > 1e-4f) return (static_cast<double>(i) - 100.0) / 48.0;
        return -1.0;
    };
    {   // closer = a longer gap to the first reflection
        const double nearMs = firstAt({{Distance, 1}, {DirectOn, 0}, {ReverbOn, 0}, {RoomSize, 8000}});
        const double farMs = firstAt({{Distance, 15}, {DirectOn, 0}, {ReverbOn, 0}, {RoomSize, 8000}});
        std::printf("  SPEAKEASY first reflection, 8000 m3: at 1 m %.1f ms, at 15 m %.1f ms\n", nearMs, farMs);
        CHECK(nearMs > farMs + 5.0);
        // LINK off: the reverb waits PRE-DELAY
        const double pre = firstAt({{Link, 0}, {PreDelay, 200}, {DirectOn, 0}, {EarlyOn, 0}});
        std::printf("  SPEAKEASY link off, pre-delay 200 ms: the reverb starts at %.1f ms\n", pre);
        CHECK(pre >= 200.0 && pre < 240.0);
    }
    {   // BALANCE +12: reverb up 6 dB, direct down 6 dB
        auto lvl = [&](float bal, bool reverbOnly) {
            auto e = mk({{Balance, bal}, {EarlyOn, 0}, {DirectOn, reverbOnly ? 0.0f : 1.0f}, {ReverbOn, reverbOnly ? 1.0f : 0.0f}});
            Stereo s = noise(1.0, 0.2f);
            run(*e, s);
            return db(rms(s.l, 24000, 24000));
        };
        const double r0 = lvl(0, true), r12 = lvl(12, true), d0 = lvl(0, false), d12 = lvl(12, false);
        std::printf("  SPEAKEASY balance +12: reverb %+.2f dB, direct %+.2f dB\n", r12 - r0, d12 - d0);
        CHECK(std::fabs(r12 - r0 - 6.0) < 0.3 && std::fabs(d12 - d0 + 6.0) < 0.1);
    }
    {   // DECAY: the reverb falls about 60 dB over DECAY
        auto tailDb = [&](float t, double at) {
            auto e = mk({{DecayTime, t}, {DirectOn, 0}, {EarlyOn, 0}});
            Stereo s = impulse(static_cast<std::size_t>(4.0 * kFs));
            run(*e, s);
            return db(rms(s.l, static_cast<std::size_t>(at * kFs), 4800));
        };
        const double a = tailDb(1.0f, 0.3), b = tailDb(1.0f, 1.3);
        std::printf("  SPEAKEASY decay 1 s: %.1f dB down from 0.3 s to 1.3 s\n", a - b);
        CHECK(a - b > 45.0 && a - b < 75.0);
    }
    {   // INPUT -60 = off; REVERB GAIN +6 = 6 dB more reverb
        auto e = mk({{Input, -60}});
        Stereo s = noise(0.3, 0.2f);
        run(*e, s);
        CHECK(peakIn(s.l, 0, s.size()) < 1e-6);
        auto lv = [&](float g) {
            auto x = mk({{ReverbGain, g}, {DirectOn, 0}, {EarlyOn, 0}});
            Stereo t = noise(1.0, 0.2f);
            run(*x, t);
            return db(rms(t.l, 24000, 24000));
        };
        std::printf("  SPEAKEASY reverb gain +6: %+.2f dB\n", lv(6) - lv(0));
        // REV SHELF -24 at 2 kHz: the reverb's highs drop, its lows stay; ER ABSORB the same on the early reflections
        auto band = [&](int which, float cut, float hz) {
            auto x = mk({{RevShelf, which == 0 ? cut : 0.0f}, {ErAbsorb, which == 1 ? cut : 0.0f}, {ShelfFreq, 2000},
                         {DirectOn, 0}, {EarlyOn, which == 1 ? 1.0f : 0.0f}, {ReverbOn, which == 0 ? 1.0f : 0.0f}});
            return outDb(*x, hz, 0.2f);
        };
        const double rh = band(0, -24, 8000) - band(0, 0, 8000), rl = band(0, -24, 200) - band(0, 0, 200);
        const double eh = band(1, -24, 8000) - band(1, 0, 8000), el = band(1, -24, 200) - band(1, 0, 200);
        std::printf("  SPEAKEASY shelf -24 at 2 kHz: reverb 8 kHz %+.1f / 200 Hz %+.1f dB, early 8 kHz %+.1f / 200 Hz %+.1f dB\n", rh, rl, eh, el);
        CHECK(rh < -15.0 && std::fabs(rl) < 2.0 && eh < -15.0 && std::fabs(el) < 2.0);
        CHECK(std::fabs(lv(6) - lv(0) - 6.0) < 0.1);
    }
}

void testInformant() {
    using namespace informant;
    // a talking-like modulator: noise in short bursts; the morph only (no noise path)
    auto keysOn = [](hc::fx::Effect& e, std::initializer_list<int> keys) { for (int k : keys) e.set(Key0 + k, 1.0f); };
    {   // A3 (key 21) held, noise as the words: the output is the synth's 220 Hz shaped by the noise
        auto e = make("THE INFORMANT", {{Level0 + NoisePath, -60.0f}});
        keysOn(*e, {21});
        Stereo s = noise(1.5, 0.2f);
        run(*e, s);
        const double h = toneAmp(s.l, 220, 24000, 48000), off = toneAmp(s.l, 300, 24000, 48000);
        std::printf("  THE INFORMANT key A3, noise as the words: 220 Hz is %.1f dB over 300 Hz, level %.1f dBFS\n", db(h / off), db(rms(s.l, 24000, 48000)));
        CHECK(db(h / off) > 15.0);
        CHECK(db(rms(s.l, 24000, 48000)) > -40.0);
    }
    {   // TUNE 220: the same key sounds an octave lower
        auto e = make("THE INFORMANT", {{Level0 + NoisePath, -60.0f}, {Tune, 220.0f}});
        keysOn(*e, {21});
        Stereo s = noise(1.5, 0.2f);
        run(*e, s);
        const double h = toneAmp(s.l, 110, 24000, 48000), off = toneAmp(s.l, 150, 24000, 48000);
        std::printf("  THE INFORMANT tune 220: A3 key at 110 Hz is %.1f dB over 150 Hz\n", db(h / off));
        CHECK(db(h / off) > 12.0);
    }
    {   // eight voices: a ninth key is ignored
        auto e = make("THE INFORMANT", {{Level0 + MorphPath, -60.0f}, {Level0 + NoisePath, -60.0f}, {Level0 + CarrierPath, 0.0f}, {Sound, 2}});
        auto ref = make("THE INFORMANT", {{Level0 + MorphPath, -60.0f}, {Level0 + NoisePath, -60.0f}, {Level0 + CarrierPath, 0.0f}, {Sound, 2}});
        keysOn(*e, {0, 2, 4, 5, 7, 9, 11, 12});
        keysOn(*ref, {0, 2, 4, 5, 7, 9, 11, 12});
        Stereo a(24000), b(24000);
        run(*e, a);
        run(*ref, b);
        keysOn(*e, {45});   // A5 = 880 Hz on one of them: no voice left, so both stay the same
        Stereo s(24000), t(24000);
        run(*e, s);
        run(*ref, t);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - t.l[i])));
        std::printf("  THE INFORMANT ninth key pressed with eight held: largest change %.2e\n", diff);
        CHECK(diff < 1e-6);
    }
    {   // SPREAD 1 with two keys: one voice to each side
        auto e = make("THE INFORMANT", {{Level0 + MorphPath, -60.0f}, {Level0 + NoisePath, -60.0f}, {Level0 + CarrierPath, 0.0f}, {Spread, 1.0f}, {Sound, 2}});
        keysOn(*e, {21, 33});   // 220 and 440 Hz
        Stereo s(24000);
        run(*e, s);
        const double lowL = toneAmp(s.l, 220, 9600, 14400), lowR = toneAmp(s.r, 220, 9600, 14400);
        std::printf("  THE INFORMANT spread 1: the low voice is %.1f dB louder on the left\n", db(lowL / std::max(lowR, 1e-12)));
        CHECK(db(lowL / std::max(lowR, 1e-12)) > 30.0);
    }
    {   // the mixer: MODULATOR at 0 dB with SOLO gives the input back untouched
        auto e = make("THE INFORMANT", {{Level0 + ModPath, 0.0f}, {Solo0 + ModPath, 1.0f}});
        keysOn(*e, {21});
        Stereo s = noise(0.3, 0.2f), ref = s;
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 4800; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - ref.l[i])));
        std::printf("  THE INFORMANT modulator solo: largest change %.2e\n", diff);
        CHECK(diff < 1e-5);
    }
    {   // MUTE on the morph and the noise: silence even with a key and words
        auto e = make("THE INFORMANT", {{Mute0 + MorphPath, 1.0f}, {Mute0 + NoisePath, 1.0f}});
        keysOn(*e, {21});
        Stereo s = noise(0.5, 0.2f);
        run(*e, s);
        std::printf("  THE INFORMANT morph and noise muted: peak %.2e\n", peakIn(s.l, 4800, s.size()));
        CHECK(peakIn(s.l, 4800, s.size()) < 1e-5);
    }
    {   // RELEASE: after the words stop the shape holds (5000 = forever) - versus 50 ms, where it dies away
        auto held = make("THE INFORMANT", {{Level0 + NoisePath, -60.0f}, {Release, 5000.0f}});
        auto fast = make("THE INFORMANT", {{Level0 + NoisePath, -60.0f}, {Release, 50.0f}});
        keysOn(*held, {21});
        keysOn(*fast, {21});
        Stereo a = noise(1.5, 0.2f), b = a;
        for (std::size_t i = 36000; i < a.size(); ++i) a.l[i] = a.r[i] = b.l[i] = b.r[i] = 0.0f;   // the words stop at 0.75 s
        run(*held, a);
        run(*fast, b);
        const double ha = db(rms(a.l, 60000, 12000)), fb = db(rms(b.l, 60000, 12000));
        std::printf("  THE INFORMANT 0.5 s after the words stop: release 5000 %.1f dBFS, release 50 %.1f dBFS\n", ha, fb);
        CHECK(ha > -40.0 && ha > fb + 30.0);
    }
    {   // FORMANT x2 moves the voice's shape up: a band of words at 500 Hz shows up near 1 kHz
        auto up = make("THE INFORMANT", {{Level0 + NoisePath, -60.0f}, {Formant, 2.0f}, {Sound, 8}});   // AIR: noisy, a full spectrum
        keysOn(*up, {21});
        Stereo s = sine(500.0, 1.5, 0.3f);
        run(*up, s);
        const double near1k = toneAmp(s.l, 1000, 24000, 48000) + toneAmp(s.l, 880, 24000, 48000) + toneAmp(s.l, 1100, 24000, 48000);
        const double near500 = toneAmp(s.l, 440, 24000, 48000) + toneAmp(s.l, 500, 24000, 48000) + toneAmp(s.l, 550, 24000, 48000);
        std::printf("  THE INFORMANT formant x2, words at 500 Hz: around 1 kHz is %.1f dB over around 500 Hz\n", db(near1k / std::max(near500, 1e-12)));
        CHECK(db(near1k / std::max(near500, 1e-12)) > 6.0);
    }
}

void testStrapped() {
    using namespace strapped;
    const auto off = [](std::initializer_list<std::pair<int, float>> v) {   // every colour OFF, mode BYPASS, plus `v`
        auto e = findPlugin("STRAPPED")->create();
        e->set(Mode, 3);
        for (int f = 0; f < kFaders; ++f) e->set(Color0 + f, 0);
        for (const auto& [i, x] : v) e->set(i, x);
        e->prepare(kFs, 512);
        return e;
    };
    {   // everything off: the guitar comes through untouched, only late by the latency
        auto e = off({});
        Stereo s = impulse(4000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 0, 4000);
        std::printf("  STRAPPED all off: impulse at %zu (latency %u), height %.4f\n", a, e->latency(), s.l[a]);
        CHECK(a == 100 + e->latency());
        CHECK(std::fabs(s.l[a] - 1.0f) < 0.02f);
    }
    {   // DELAY 1/4 at 120 BPM: an echo 500 ms later
        auto e = off({{Color0 + DelayF, 3}, {Fader0 + DelayF, 10}});
        Stereo s = impulse(36000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 4000, 36000);
        std::printf("  STRAPPED delay, a quarter note at 120 BPM: echo at %.1f ms\n", (a - 100.0 - e->latency()) / 48.0);
        CHECK(a > 24050 && a < 24200);
    }
    {   // BASS +10 on 80 Hz lifts the lows by about 12 dB, leaves 5 kHz alone
        auto e = off({{Color0 + BassF, 1}, {Fader0 + BassF, 10}});
        const double lo = toneGain(*e, 30.0, 0.05f), hi = toneGain(*e, 5000.0, 0.05f);
        std::printf("  STRAPPED bass +10 at 80 Hz: 30 Hz %+.1f dB, 5 kHz %+.1f dB\n", lo, hi);
        CHECK(lo > 10.5 && lo < 12.5);
        CHECK(std::fabs(hi) < 0.5);
    }
    {   // COMPRESS HEAVY at +10 pulls a loud tone down
        auto e = off({{Color0 + CompressF, 3}, {Fader0 + CompressF, 10}});
        Stereo s = sine(1000.0, 1.0, 0.7f);
        run(*e, s);
        std::printf("  STRAPPED compress heavy +10: reduction %.1f dB\n", e->reductionDb());
        CHECK(e->reductionDb() < -10.0f);
    }
    {   // BALANCE +6 turns the left side down 6 dB
        auto e = off({{Balance, 6}});
        Stereo s = sine(1000.0, 0.5, 0.25f);
        run(*e, s);
        const double l = db(rms(s.l, 12000, 12000)), r = db(rms(s.r, 12000, 12000));
        std::printf("  STRAPPED balance +6: left %.2f dB below right\n", r - l);
        CHECK(std::fabs((r - l) - 6.0) < 0.1);
    }
    {   // PITCH WIDEST spreads a mono guitar into stereo
        auto e = off({{Color0 + PitchF, 3}, {Fader0 + PitchF, 10}});
        Stereo s = noise(0.5, 0.3f);
        for (std::size_t i = 0; i < s.size(); ++i) s.r[i] = s.l[i];
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 12000; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  STRAPPED pitch widest: largest left/right difference %.3f\n", diff);
        CHECK(diff > 0.05);
    }
    {   // RE-AMP HEAVY drives it: a loud sine gets harmonics
        auto e = make("STRAPPED", {{Mode, 2}, {ReAmp, 1}});
        auto c = make("STRAPPED", {{Mode, 2}, {ReAmp, 0}});
        Stereo a = sine(220.0, 0.5, 0.5f), b = a;
        run(*e, a);
        run(*c, b);
        double d = 0;
        for (std::size_t i = 12000; i < a.size(); ++i) d = std::max(d, static_cast<double>(std::fabs(a.l[i] - b.l[i])));
        std::printf("  STRAPPED re-amp heavy: largest change %.3f\n", d);
        CHECK(d > 0.05);
    }
}

void testSafeHouse() {
    using namespace safehouse;
    {   // the usual triangle (speakers at +-30 degrees), a 55 cm head: the left speaker reaches the right ear ~0.26 ms later
        auto e = make("SAFE HOUSE", {{Amount, 0}});
        Stereo s(4800);
        s.l[100] = 1.0f;
        run(*e, s);
        const std::size_t r = peakAt(s.r, 90, 200), l = peakAt(s.l, 90, 200);
        std::printf("  SAFE HOUSE: a click from the left speaker - left ear at %zu, right ear %.2f ms later\n", l, (r - 100) / 48.0);
        CHECK(l == 100);
        CHECK(r >= 110 && r <= 116);
    }
    {   // a bigger head: later still
        auto e = make("SAFE HOUSE", {{Amount, 0}, {Head, 75}, {Arc, 40}});
        Stereo s(4800);
        s.l[100] = 1.0f;
        run(*e, s);
        const std::size_t r = peakAt(s.r, 90, 300);
        std::printf("  SAFE HOUSE head 75 cm, arc 40 cm: %.2f ms\n", (r - 100) / 48.0);
        CHECK(r > 125);
    }
    {   // head turned 30 degrees right: the right speaker is now straight ahead, both ears hear it together
        auto e = make("SAFE HOUSE", {{Amount, 0}, {Rotate, 30}});
        Stereo s(4800);
        s.r[100] = 1.0f;
        run(*e, s);
        const std::size_t l = peakAt(s.l, 90, 200), r = peakAt(s.r, 90, 200);
        std::printf("  SAFE HOUSE rotate +30: right speaker reaches left ear at %zu, right ear at %zu\n", l, r);
        CHECK(l == r);
    }
    {   // MUTE L: the left speaker is gone from both ears
        auto e = make("SAFE HOUSE", {{Amount, 50}, {MuteL, 1}});
        Stereo s = sine(500.0, 0.5, 0.3f);
        for (auto& x : s.r) x = 0.0f;
        run(*e, s);
        const double pk = std::max(peakIn(s.l, 4800, s.size()), peakIn(s.r, 4800, s.size()));
        std::printf("  SAFE HOUSE mute L, left only in: peak %.2e\n", pk);
        CHECK(pk < 1e-5);
    }
    {   // a centred (mono) sound keeps about its level
        auto m = make("SAFE HOUSE", {{Amount, 0}});
        const double g = toneGain(*m, 200, 0.3f);
        std::printf("  SAFE HOUSE mono 200 Hz level change: %+.1f dB\n", g);
        CHECK(std::fabs(g) < 3.0);
    }
    {   // AMOUNT brings the room: sound after the click has ended
        auto dry = make("SAFE HOUSE", {{Amount, 0}}), wet = make("SAFE HOUSE", {{Amount, 100}});
        Stereo a = impulse(24000), b = impulse(24000);
        run(*dry, a);
        run(*wet, b);
        const double ta = db(rms(a.l, 2000, 9600)), tb = db(rms(b.l, 2000, 9600));
        std::printf("  SAFE HOUSE room 40-240 ms after a click: amount 0 %.0f dB, amount 100 %.0f dB\n", ta, tb);
        CHECK(tb > ta + 40.0);
    }
}

} // namespace

void testHideout() {
    std::printf("[the hideout]\n");
    allRules();
    testFixer();
    testDeadDrop();
    testSwerve();
    testColdCase();
    testVoiceDynamics();
    testBigHouse();
    testSpeakeasy();
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
