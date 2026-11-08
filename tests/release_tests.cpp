// Release checks: what Steven's control-by-control test of 0.51.3 on his PC found, turned into tests that run on every
// build. Every plug-in loads at a safe level with no noise of its own, every plug-in is told apart from the others,
// and each fixed control does what its label says.
#include "Harness.hpp"
#include "Signals.hpp"

#include "hem/rack/Arsenal.hpp"
#include "hem/rack/ColorFx.hpp"
#include "hem/rack/Hideout.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Garage.hpp"
#include "hem/rack/Streets.hpp"
#include "hem/rack/ToneShop.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>

namespace hem::test {
namespace {

constexpr double kFs = 48000.0;

// A song-like stereo signal: pink noise, a kick every half second and a 220 Hz note, nearly mono,
// about -18 dB RMS with peaks near -2 dBFS.
Stereo song(double seconds, unsigned seed = 7) {
    Stereo s(static_cast<std::size_t>(seconds * kFs));
    // the same numbers on every compiler (std::normal_distribution is not: MSVC and GCC give different songs)
    std::mt19937 g(seed);
    auto nd = [&g](int) {
        const double u1 = (static_cast<double>(g()) + 1.0) / 4294967297.0, u2 = static_cast<double>(g()) / 4294967296.0;
        return static_cast<float>(std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2));
    };
    float b0 = 0, b1 = 0, b2 = 0, side = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const float w = nd(0);
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        const float pink = (b0 + b1 + b2 + w * 0.1848f) * 0.035f;
        const double t = static_cast<double>(i) / kFs, tk = std::fmod(t, 0.5);
        const auto kick = static_cast<float>(0.25 * std::exp(-tk * 25.0) * std::sin(2.0 * std::numbers::pi * 55.0 * tk * (1.0 + 2.0 * std::exp(-tk * 40.0))));
        const auto tone = static_cast<float>(0.08 * std::sin(2.0 * std::numbers::pi * 220.0 * t));
        side = 0.999f * side + 0.02f * nd(0);
        const float m = pink + kick + tone;
        s.l[i] = m + side * 0.05f;
        s.r[i] = m - side * 0.05f;
    }
    return s;
}

struct Level { double rmsDb, peakDb; };
Level level(const Stereo& s, std::size_t from) {
    double e = 0.0, pk = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < s.size(); ++i, ++n) {
        e += 0.5 * (static_cast<double>(s.l[i]) * s.l[i] + static_cast<double>(s.r[i]) * s.r[i]);
        pk = std::max({pk, static_cast<double>(std::fabs(s.l[i])), static_cast<double>(std::fabs(s.r[i]))});
    }
    return {10.0 * std::log10(e / static_cast<double>(std::max<std::size_t>(n, 1)) + 1e-30), 20.0 * std::log10(pk + 1e-15)};
}

std::unique_ptr<hc::fx::Effect> load(const char* name, std::initializer_list<std::pair<int, float>> sets = {}) {
    const rack::PluginInfo* info = rack::findPlugin(name);
    if (!info) { std::printf("  no plug-in %s\n", name); return nullptr; }
    auto e = info->create();
    e->setDefaults();
    for (const auto& [i, v] : sets) e->set(i, v);
    e->prepare(kFs, 512);
    e->reset();
    return e;
}

// level change (dB) of a plug-in on the song, measured over the last two thirds
Level through(const char* name, std::initializer_list<std::pair<int, float>> sets = {}, double seconds = 6.0) {
    auto e = load(name, sets);
    if (!e) return {999.0, 999.0};
    Stereo s = song(seconds);
    const Level in = level(s, s.size() / 3);
    run(*e, s);
    const Level out = level(s, s.size() / 3);
    return {out.rmsDb - in.rmsDb, out.peakDb - in.peakDb};
}

void testEveryPluginIsItsOwn() {
    // the rack tells a running plug-in by its control table: no two plug-ins may share one (QUICK CASH opened as TOP DOG)
    std::map<const void*, std::string> seen;
    int shared = 0;
    for (const auto& p : rack::plugins()) {
        auto e = p.create();
        const auto [it, fresh] = seen.emplace(e->params().data(), p.name);
        if (!fresh) { std::printf("  %s shares its controls with %s\n", p.name, it->second.c_str()); ++shared; }
    }
    CHECK(shared == 0);
}

void testSafeOnLoad() {
    // Loading a plug-in never jumps the song up, never drops it a lot and never clips it. Measured on the song above.
    // Left out, each for its own reason: THE CLEANER takes noise out (this song is mostly noise), DRIVE-BY's sound passes by
    // (it is meant to come and go), WHEELMAN and CHAUFFEUR ride to their target, THE MOLD is a hardware loop (silent with
    // nothing plugged in), THE INFORMANT plays only when it gets notes.
    const char* const skip[] = {"THE CLEANER", "DRIVE-BY", "WHEELMAN", "CHAUFFEUR", "THE MOLD", "THE INFORMANT"};
    for (const auto& p : rack::plugins()) {
        bool skipped = false;
        for (const char* s : skip) skipped = skipped || std::strcmp(s, p.name) == 0;
        if (skipped) continue;
        const Level d = through(p.name);
        const bool ok = d.rmsDb > -4.0 && d.rmsDb < 2.0 && d.peakDb < 2.0;
        if (!ok) std::printf("  %s on load: level %+.1f dB, peak %+.1f dB\n", p.name, d.rmsDb, d.peakDb);
        CHECK(ok);
    }
}

void testQuietOnLoad() {
    // No hum, hiss or noise of their own at load: one second of silence in, nothing out (GRAND LARCENY's pedal is only
    // heard while it plays; LIVE WIRE's tones are off at load).
    for (const auto& p : rack::plugins()) {
        auto e = load(p.name);
        Stereo s(static_cast<std::size_t>(kFs));
        run(*e, s);
        const Level out = level(s, 0);
        const bool ok = out.peakDb < -90.0;
        if (!ok) std::printf("  %s makes noise on silence: peak %.1f dBFS\n", p.name, out.peakDb);
        CHECK(ok);
    }
}

void testFixedControls() {
    // SPEAKEASY: it loads 1 m from the sound, so DIRECT alone (early and reverb off) is the dry song and the whole room
    // is about as loud as the song (at 5 m, the old start, the direct sound alone was 14 dB down: the real room's 1 / distance)
    {
        using namespace rack::speakeasy;
        CHECK(std::fabs(through("SPEAKEASY", {{EarlyOn, 0}, {ReverbOn, 0}}).rmsDb) < 0.5);
        CHECK(std::fabs(through("SPEAKEASY").rmsDb) < 1.0);
    }
    // BACK ALLEY: echo and reverb both off = the dry song
    {
        using namespace rack::backalley;
        CHECK(std::fabs(through("BACK ALLEY", {{EchoOn, 0}, {ReverbOn, 0}}).rmsDb) < 0.5);
    }
    // DRIVE-BY: every part switched off = the dry song (summed to one source, as it is a single moving sound)
    {
        using namespace rack::driveby;
        CHECK(std::fabs(through("DRIVE-BY", {{GainOn, 0}, {PanOn, 0}, {PitchOn, 0}, {AirOn, 0}, {ReverbOn, 0}}).rmsDb) < 0.5);
    }
    // LOW PROFILE: AMOUNT tops out at +12 dB (one drag took it to +22.7 dB and the song to +16.5 dB)
    {
        const rack::PluginInfo* info = rack::findPlugin("LOW PROFILE");
        auto e = info->create();
        CHECK(e->params()[rack::lowprofile::Intensity].max <= 12.0f);
        CHECK(through("LOW PROFILE", {{rack::lowprofile::Intensity, 12}}).rmsDb < 6.0);
    }
    // LOW RIDER: its added paths top out at +12 dB too
    {
        auto e = rack::findPlugin("LOW RIDER")->create();
        CHECK(e->params()[rack::lowrider::LowAir].max <= 12.0f && e->params()[rack::lowrider::Lo].max <= 12.0f);
    }
    // KNOCKOUT: SUSTAIN at -100 % takes the tails down on a song (it did nothing at the default pickup)
    {
        using namespace rack::knockout;
        CHECK(through("KNOCKOUT", {{Sustain, -100}}).rmsDb < -0.3);
        CHECK(through("KNOCKOUT", {{Sustain, 100}}).rmsDb > 0.3);
        {   // SAFETY clips at -0.1 dBFS at load: ATTACK +100 % on a hot song never goes past it
            auto e = load("KNOCKOUT", {{Attack, 100}});
            Stereo s = song(3.0);
            for (std::size_t i = 0; i < s.size(); ++i) { s.l[i] *= 1.25f; s.r[i] *= 1.25f; }
            run(*e, s);
            CHECK(level(s, 0).peakDb <= -0.09);
        }
    }
    // SURGEON: a high Q no longer bends a cutting shelf into a boost (LOW SHELF -16.8 dB at Q 5.8 boosted +14 dB)
    {
        for (const float type : {1.0f, 2.0f}) {   // LOW SHELF, HIGH SHELF
            auto e = load("SURGEON", {{0, 1}, {1, type}, {2, 1000}, {3, -16.8f}, {4, 40}});
            Stereo imp(16384);
            for (int k = 0; k < 200; ++k) { Stereo z(512); run(*e, z); }   // settle
            imp.l[0] = imp.r[0] = 1.0f;
            run(*e, imp);
            const auto lat = static_cast<double>(e->latency());
            double most = -999.0;
            for (double hz = 30.0; hz < 20000.0; hz *= 1.05) {
                double re = 0.0, im = 0.0;
                for (std::size_t i = 0; i < imp.size(); ++i) {
                    const double ph = 2.0 * std::numbers::pi * hz * (static_cast<double>(i) - lat) / kFs;
                    re += imp.l[i] * std::cos(ph);
                    im -= imp.l[i] * std::sin(ph);
                }
                most = std::max(most, 10.0 * std::log10(re * re + im * im + 1e-30));
            }
            std::printf("  SURGEON %s -16.8 dB at Q 40: highest point %+.1f dB\n", type < 1.5f ? "LOW SHELF" : "HIGH SHELF", most);
            CHECK(most < 4.0);
        }
    }
    // BACKBONE: LOUD DOWN's make-up never pushes a hit past -0.1 dBFS (+4 dBFS on a loud song before)
    {
        using namespace rack::backbone;
        auto e = load("BACKBONE", {{LowLevel, 13.6f}, {HighLevel, -13.5f}});
        Stereo s = song(3.0);
        for (std::size_t i = 0; i < s.size(); ++i) { s.l[i] *= 2.0f; s.r[i] *= 2.0f; }
        run(*e, s);
        CHECK(level(s, 0).peakDb <= -0.09);
    }
    // KINGPIN: OUTPUT reads in dB and does what it reads
    {
        using namespace rack::kingpin;
        CHECK(std::fabs(through("KINGPIN", {{Output, -6}}).rmsDb + 6.0) < 0.3);
        CHECK(std::fabs(through("KINGPIN", {{Output, 6}}).rmsDb - 6.0) < 0.3);
    }
    // THE RACKET: a drive pedal at LEVEL 50 is as loud as the song (it came out 11-14 dB louder); LEVEL 100 = +12 dB
    {
        using namespace rack::racket;
        for (int ped = 0; ped < 5; ++ped) {
            const Level d = through("THE RACKET", {{Which, static_cast<float>(ped)}});
            if (std::fabs(d.rmsDb) >= 1.0) std::printf("  THE RACKET pedal %d: %+.1f dB\n", ped, d.rmsDb);
            CHECK(std::fabs(d.rmsDb) < 1.0);
        }
        CHECK(std::fabs(through("THE RACKET", {{OdLevel, 100}}).rmsDb - 12.0) < 1.0);
    }
    // PAWN SHOP: each old device keeps the song's loudness
    {
        using namespace rack::pawnshop;
        for (int dev = 0; dev < 4; ++dev) CHECK(std::fabs(through("PAWN SHOP", {{Device, static_cast<float>(dev)}}).rmsDb) < 1.0);
    }
}

void testFixedMeters() {
    // THE INFORMANT: soloing the modulator (your song) plays it, even with its fader at the bottom (-60 = off)
    {
        using namespace rack::informant;
        CHECK(std::fabs(through("THE INFORMANT", {{Solo0 + 1, 1}}).rmsDb) < 0.5);
    }
    // THE AUDITOR: +6.8 dB of GAIN into a -2 dBFS song with the limiter off puts true peaks past TRUE PEAK MAX: OVERS counts
    {
        using namespace rack::auditor;
        auto e = load("THE AUDITOR", {{Gain, 6.8f}, {Limiter, 0}});
        Stereo s = song(3.0);
        run(*e, s);
        std::printf("  THE AUDITOR +6.8 dB, limiter off: true peak %.1f dBTP, overs %.0f\n", e->meter(TruePeak), e->meter(Overs));
        CHECK(e->meter(TruePeak) > -1.0f && e->meter(Overs) >= 1.0f);
    }
    // THE WITNESS: peak meters that hold the highest sample (dBFS) until CLEAR
    {
        auto e = load("THE WITNESS");
        Stereo s = sine(1000, 0.5, 0.5f);
        run(*e, s);
        std::printf("  THE WITNESS a -6.02 dBFS sine: PEAK L %.2f, PEAK R %.2f dBFS\n", e->meter(4), e->meter(5));
        CHECK(std::fabs(e->meter(4) + 6.02f) < 0.1f && std::fabs(e->meter(5) + 6.02f) < 0.1f);
    }
    // DOUBLE CROSS: in M / S the side meter reads the side (it kept the L / R peak: S read the same as R)
    {
        using namespace rack::doublecross;
        auto e = load("DOUBLE CROSS");
        Stereo s = sine(500, 1.0, 0.5f);   // the same on both sides: no side at all going in
        run(*e, s);
        e->set(MeterMode, 1);
        Stereo t = sine(500, 2.0, 0.5f);
        run(*e, t);
        std::printf("  DOUBLE CROSS M / S on a mono tone: M %.1f, S %.1f dBFS\n", e->meter(0), e->meter(1));
        CHECK(e->meter(1) < e->meter(0) - 3.0f);
    }
    // THE FIXER: FREQ reads in Hz
    {
        auto e = rack::findPlugin("THE FIXER")->create();
        CHECK(std::string(e->params()[rack::fixer::band(0, rack::fixer::Freq)].unit) == "Hz");
    }
    // KINGPIN: its meter can show the gain reduction (COMPRESS looked dead: the needle only had IN and OUT)
    {
        auto e = rack::findPlugin("KINGPIN")->create();
        const auto& m = e->params()[rack::kingpin::Meter];
        CHECK(m.max >= 2.0f && std::string(m.choices[1]) == "GR");
    }
    // FURNACE and MIXTAPE: more DRIVE is more tone at about the same level on a song
    {
        CHECK(std::fabs(through("FURNACE", {{rack::tubesat::Drive, 100}}).rmsDb - through("FURNACE", {{rack::tubesat::Drive, 0}}).rmsDb) < 1.5);
        CHECK(std::fabs(through("MIXTAPE", {{0, 100}}).rmsDb - through("MIXTAPE", {{0, 0}}).rmsDb) < 1.5);
    }
    // IRON FIST: AUTO MAKEUP gives back what the compressor takes (it gave +0.5 dB back of 11 with INPUT at 0)
    {
        using namespace rack::ironfist;
        const double off = through("IRON FIST", {{Input, 0}, {Output, 0}}).rmsDb, on = through("IRON FIST", {{Input, 0}, {AutoMakeup, 1}}).rmsDb;
        std::printf("  IRON FIST INPUT 0: %+.1f dB, with AUTO MAKEUP %+.1f dB\n", off, on);
        CHECK(on > off + 5.0 && std::fabs(on) < 3.0);
    }
}

} // namespace

void testRelease() {
    testEveryPluginIsItsOwn();
    testSafeOnLoad();
    testQuietOnLoad();
    testFixedControls();
    testFixedMeters();
}

} // namespace hem::test
