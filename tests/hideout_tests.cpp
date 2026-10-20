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
    for (const char* name : {"THE FIXER", "SIX SHOOTER", "BACK ALLEY", "DRIVE-BY", "BODY DOUBLE", "HUSTLE", "THE INFORMANT", "STRAPPED", "SAFE HOUSE"}) {
        const PluginInfo* info = findPlugin(name);
        CHECK(info != nullptr);
        if (info) commonRules(*info);
    }
}

void testFixer() {
    using namespace fixer;
    auto e = make("THE FIXER", {{Thr1, 0}, {Thr2, -30}, {Thr3, 0}, {Range2, 8}, {Freq2, 3000}});
    const double loud = toneGain(*e, 3000, 0.5f);
    const float m = e->meter(1);
    const double quiet = toneGain(*e, 3000, 0.003f), other = toneGain(*e, 300, 0.5f);
    std::printf("  THE FIXER harsh band (3 kHz, threshold -30, range 8): loud %+.1f dB (meter %.1f), quiet %+.1f, a loud 300 Hz %+.1f dB\n",
                loud, m, quiet, other);
    CHECK(loud < -6.5 && loud > -8.5 && std::fabs(quiet) < 0.2 && std::fabs(other) < 1.0 && m < -6.0f);
    auto l = make("THE FIXER", {{Listen, 2}, {Freq2, 3000}});
    const double in = toneGain(*l, 3000, 0.003f), out = toneGain(*l, 300, 0.003f);
    std::printf("  THE FIXER LISTEN HARSH: 3 kHz %+.1f dB, 300 Hz %+.1f dB\n", in, out);
    CHECK(std::fabs(in) < 0.5 && out < -15.0);
    const double d = blockSizeDifference(*findPlugin("THE FIXER"), [](hc::fx::Effect& x) { x.set(Thr2, -40); x.set(Thr1, -40); }, noise(0.6, 0.5f));
    std::printf("  THE FIXER block size 512 vs 1: %.2e\n", d);
    CHECK(d < 1e-5);
}

void testSixShooter() {
    using namespace sixshooter;
    {
        auto e = make("SIX SHOOTER", {{Shots, 2}, {Note, 0}, {Time, 100}, {Decay, 0}, {Spread, 0}, {Mix, 100}, {Tone, 20000}});
        Stereo s = impulse(30000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 2000, 7000), b = peakAt(s.l, 7000, 12000), c = peakAt(s.l, 12000, 17000);
        const double pa = std::fabs(s.l[a]), pb = std::fabs(s.l[b]), pc = std::fabs(s.l[c]), after = peakIn(s.l, 17000, 30000);
        std::printf("  SIX SHOOTER 3 shots, 100 ms: at %.1f / %.1f / %.1f ms, levels %.2f %.2f %.2f, after the third %.3f\n",
                    (a - 100) / 48.0, (b - 100) / 48.0, (c - 100) / 48.0, pa, pb, pc, after);
        CHECK(a == 4900 && b == 9700 && c == 14500 && std::fabs(pa - pb) < 0.05 && std::fabs(pb - pc) < 0.05 && after < 0.01);
    }
    {
        auto e = make("SIX SHOOTER", {{Shots, 1}, {Note, 0}, {Time, 100}, {Swing, 50}, {Spread, 100}, {Mix, 100}, {Tone, 20000}});
        Stereo s = impulse(20000);
        run(*e, s);
        const std::size_t b = peakAt(s.r, 7000, 16000);
        const double firstR = peakIn(s.r, 2000, 7000), firstL = peakIn(s.l, 2000, 7000);
        std::printf("  SIX SHOOTER swing 50 %%: second shot at %.1f ms; spread 100: first shot left %.2f, right %.3f\n", (b - 100) / 48.0, firstL, firstR);
        CHECK(b > 12050 && b < 12150 && firstR < 0.01 && firstL > 0.9);
    }
}

void testBackAlley() {
    using namespace backalley;
    {
        auto e = make("BACK ALLEY", {{Reverb, 0}, {Echo, 100}, {Time, 200}, {Wobble, 0}, {Feedback, 0}, {Mix, 100}, {Narrow, 0}, {Grit, 0}});
        Stereo s = impulse(24000);
        run(*e, s);
        const std::size_t a = peakAt(s.l, 2000, 20000);
        std::printf("  BACK ALLEY echo at 200 ms: found at %.1f ms\n", (a - 100) / 48.0);
        CHECK(a > 9690 && a < 9720);
    }
    {
        auto e = make("BACK ALLEY", {{Reverb, 100}, {Echo, 0}, {Decay, 1.0f}, {Mix, 100}});
        Stereo s = noise(0.2, 0.5f);
        s.l.resize(96000, 0.0f);
        s.r.resize(96000, 0.0f);
        run(*e, s);
        const double early = rms(s.l, 9600, 9600), late = rms(s.l, 9600 + 48000, 9600);
        std::printf("  BACK ALLEY reverb (decay 1 s): tail just after %.1f dB, one second later %.1f dB\n", db(early), db(late));
        CHECK(db(early) > -40.0 && db(late) < db(early) - 40.0);
    }
    {
        auto e = make("BACK ALLEY", {{Narrow, 100}});
        Stereo s = noise(0.5, 0.3f);
        run(*e, s);
        double diff = 0;
        for (std::size_t i = 0; i < s.size(); ++i) diff = std::max(diff, static_cast<double>(std::fabs(s.l[i] - s.r[i])));
        std::printf("  BACK ALLEY narrow 100: largest left/right difference %.2e\n", diff);
        CHECK(diff < 1e-5);
    }
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
    {
        auto e = make("DRIVE-BY", {{Mode, 0}, {Position, 100}, {Air, 0}});
        const double l = toneGain(*e, 1000, 0.3f), r = toneGain(*e, 1000, 0.3f, true);
        std::printf("  DRIVE-BY parked on the right: left %.1f dB, right %.1f dB\n", l, r);
        CHECK(r > l + 10.0);
    }
    {
        // one pass every bar at 120 BPM (2 s), 50 m/s: coming in it is higher, going away lower
        auto e = make("DRIVE-BY", {{Mode, 1}, {Rate, 7}, {SyncSource, 1}, {Bpm, 120}, {Speed, 50}, {Distance, 5}, {Air, 0}});
        Stereo s = sine(1000, 2.0, 0.3f);
        run(*e, s);
        Stereo m(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) m.l[i] = s.l[i] + s.r[i];
        const double in = crossingsHz(m.l, 14400, 9600), out = crossingsHz(m.l, 72000, 9600);
        std::printf("  DRIVE-BY at 50 m/s: approaching %.0f Hz, leaving %.0f Hz (a 1 kHz tone)\n", in, out);
        CHECK(in > 1100 && in < 1200 && out > 830 && out < 920);
    }
}

void testBodyDouble() {
    using namespace bodydouble;
    auto e = make("BODY DOUBLE", {{Detune, 30}, {Spread, 100}, {VoiceLevel, 0}, {LowCut, 20}, {Dry, -24}});
    Stereo s = sine(1000, 2.0, 0.3f);
    run(*e, s);
    const double upL = bandLevelDb(s.l, 1008, 1030, 48000, 48000), downL = bandLevelDb(s.l, 970, 992, 48000, 48000);
    const double upR = bandLevelDb(s.r, 1008, 1030, 48000, 48000), downR = bandLevelDb(s.r, 970, 992, 48000, 48000);
    std::printf("  BODY DOUBLE detune 30 c, spread 100: left sharp %.1f / flat %.1f dB, right sharp %.1f / flat %.1f dB\n", upL, downL, upR, downR);
    CHECK(upL > downL + 10.0 && downR > upR + 10.0);
}

void testHustle() {
    using namespace hustle;
    {
        auto e = make("HUSTLE", {{Tremolo, 0}});
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
        auto e = make("HUSTLE", {{Tremolo, 100}, {Rate, 4}});
        Stereo s = sine(1000, 1.0, 0.5f);
        run(*e, s);
        double lo = 1e9, hi = 0;
        for (std::size_t w = 12000; w + 480 < s.size(); w += 480) {
            const double r = rms(s.l, w, 480);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        std::printf("  HUSTLE tremolo 100 %% at 4 Hz: loudest %.1f dB, quietest %.1f dB\n", db(hi / (0.5 / std::sqrt(2.0))), db(lo / (0.5 / std::sqrt(2.0))));
        CHECK(db(hi / (0.5 / std::sqrt(2.0))) > -1.0 && db(lo / (0.5 / std::sqrt(2.0))) < -20.0);
    }
    {
        auto e = make("HUSTLE", {{Tremolo, 0}, {Pan, 100}, {Rate, 1}});
        Stereo s = sine(1000, 1.5, 0.5f);
        run(*e, s);
        double most = 0;
        for (std::size_t w = 6000; w + 960 < s.size(); w += 960) most = std::max(most, std::fabs(db(rms(s.l, w, 960) / std::max(rms(s.r, w, 960), 1e-9))));
        std::printf("  HUSTLE pan 100 %%: the image swings as far as %.1f dB to one side\n", most);
        CHECK(most > 20.0);
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
