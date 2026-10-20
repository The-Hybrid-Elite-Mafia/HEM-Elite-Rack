// THE RACKET — the pedalboard, one pedal at a time (see Streets.hpp).
//
// Only the chosen pedal runs. A pedal change fades the sound out (5 ms), clears the new pedal's state and
// fades back in (5 ms). The drive pedals clip at 4x the sample rate; every other pedal is held back by the
// same 23 samples, so the latency never changes with the pedal.
//
// Our readings where the original's guide leaves the sound open (ours, not measured on the original):
//  * the drives' gain ranges, pre-filters and clipping curves; LEVEL 50 = unity after the clipper, 100 = +12 dB
//  * RASP: the guide places a resonant peak at a high-pass and calls TONE a resonant low-pass; it has both
//  * the modulation depths (flange sweep, vibrato swing up to 2.5 ms, chorus swing up to 5 ms, phase sweep up to
//    ±2.5 octaves around 900 Hz), the wah's range (350 Hz up by 1..4 octaves) and its follower's times
//  * ECHO's HIGH CUT 0.1..7 is read as kHz, its STEREO as how far the echoes bounce side to side; FLIP ECHO's
//    STEREO offsets the right side's pieces; THICKEN's STEREO spreads its two detuned copies
//  * ROOM's TONE is how fast the highs die against the rest (1 = the same); SPRING is a three-spring tank
//  * SQUEEZE: 4:1 from 0 to -40 dB as the amount goes up, with half the squeeze of a -18 dBFS signal made
//    back; SLAM: 20:1, release 120 ms
//  * GRAPHIC: the guide gives each band a Q of 10 (narrow); the rumble filter is 12 dB / oct at 40 Hz
#include "hem/rack/Streets.hpp"

#include "../hideout/RoomKit.hpp"
#include "../tone/ToneCommon.hpp"

namespace hem::rack {
namespace racket {
namespace {
using namespace tone;
using hideout::Wave;

const char* const kPedalN[kPedals] = {"OVERDRIVE", "HARD CLIP", "FUZZ", "RASP", "HEAVY",
                                      "FLANGE", "VIB / TREM", "AUTO PAN", "PHASE", "OCT DOWN", "WAH", "CHORUS", "THICKEN", "PITCH RIDE", "BASS RIDE",
                                      "ECHO", "FLIP ECHO", "ROOM", "SPRING",
                                      "SQUEEZE", "SLAM", "GATE", "CLEAN UP", "TONE STACK", "GRAPHIC", "VOLUME"};
const char* const kInputN[] = {"STEREO", "MONO", "MONO > WIDE"};
const char* const kWaveN[] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE"};
constexpr Wave kWaveOf[] = {Wave::Sine, Wave::Triangle, Wave::SawUp, Wave::SawDown, Wave::Square};
const char* const kNoteN[] = {"2 BARS", "1 BAR", "1/2 D", "1/2", "1/2 T", "1/4 D", "1/4", "1/4 T",
                              "1/8 D", "1/8", "1/8 T", "1/16 D", "1/16", "1/16 T", "1/32", "1/32 T"};
constexpr int kNotes = 16;
constexpr double kNoteBeats[kNotes] = {8.0, 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                       0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0};
const char* const kWahModeN[] = {"AUTO", "HAND"};
const char* const kScaleN[] = {"LIN", "LOG"};

#define HEM_PCT(id, name, def) {id, name, "%", 0.0f, 100.0f, def}
#define HEM_SYNC(id) {id, "Sync", "", 0, 1, 0, Taper::Toggle, kOnOff}
#define HEM_NOTE(id, def) {id, "Sync Note", "", 0, kNotes - 1, def, Taper::Choice, kNoteN}
#define HEM_WAVE(id) {id, "Wave", "", 0, 4, 0, Taper::Choice, kWaveN}
const ParamInfo kParams[kCount] = {
    {"pedal", "Pedal", "", 0, kPedals - 1, 0, Taper::Choice, kPedalN},
    {"input", "Input", "", 0, 2, 0, Taper::Choice, kInputN},
    {"ingain", "Input Gain", "dB", -18.0f, 18.0f, 0.0f},
    // OVERDRIVE
    HEM_PCT("oddrive", "Overdrive Drive", 50.0f), HEM_PCT("odlevel", "Overdrive Level", 50.0f), HEM_PCT("odtone", "Overdrive Tone", 50.0f),
    // HARD CLIP
    HEM_PCT("dsdrive", "Hard Clip Drive", 50.0f), HEM_PCT("dslevel", "Hard Clip Level", 50.0f), HEM_PCT("dsbody", "Hard Clip Body", 50.0f),
    {"dstone", "Hard Clip Tone", "Hz", 700.0f, 10000.0f, 3900.0f, Taper::Log},
    // FUZZ
    HEM_PCT("fzsustain", "Fuzz Sustain", 50.0f), HEM_PCT("fzlevel", "Fuzz Level", 50.0f), HEM_PCT("fztone", "Fuzz Tone", 50.0f),
    // RASP
    HEM_PCT("rpdrive", "Rasp Drive", 50.0f), HEM_PCT("rptone", "Rasp Tone", 34.0f), HEM_PCT("rplevel", "Rasp Level", 50.0f),
    // HEAVY
    HEM_PCT("hvdrive", "Heavy Drive", 50.0f), HEM_PCT("hvlevel", "Heavy Level", 50.0f),
    {"hvlow", "Heavy Low", "dB", -12.0f, 18.0f, 2.4f},
    {"hvhigh", "Heavy High", "dB", -30.0f, 0.0f, -12.0f},
    {"hvmidfreq", "Heavy Mid Freq", "Hz", 250.0f, 1500.0f, 400.0f, Taper::Log},
    {"hvmidgain", "Heavy Mid Gain", "dB", -36.0f, 12.0f, -6.0f},
    // FLANGE
    HEM_PCT("fldepth", "Flange Depth", 50.0f), HEM_SYNC("flsync"), HEM_NOTE("flnote", 7),
    {"flrate", "Flange Rate", "Hz", 0.0f, 20.0f, 0.2f},
    {"fldelay", "Flange Delay", "ms", 1.5f, 10.0f, 3.0f},
    HEM_PCT("flfeedback", "Flange Feedback", 50.0f), HEM_PCT("flstereo", "Flange Stereo", 50.0f),
    // VIB / TREM
    HEM_WAVE("vtwave"), HEM_SYNC("vtsync"), HEM_NOTE("vtnote", 7),
    {"vtrate", "Vib Trem Rate", "Hz", 1.0f, 20.0f, 8.0f},
    HEM_PCT("vtvibrato", "Vibrato", 0.0f), HEM_PCT("vttremolo", "Tremolo", 50.0f),
    // AUTO PAN
    HEM_WAVE("pnwave"), HEM_SYNC("pnsync"), HEM_NOTE("pnnote", 7),
    {"pnrate", "Auto Pan Rate", "Hz", 0.1f, 20.0f, 8.0f, Taper::Log},
    HEM_PCT("pnwidth", "Auto Pan Width", 50.0f),
    // PHASE
    {"phdepth", "Phase Depth", "", 0.0f, 0.8f, 0.0f}, HEM_SYNC("phsync"), HEM_NOTE("phnote", 7),
    {"phrate", "Phase Rate", "Hz", 0.0f, 20.0f, 1.0f},
    HEM_PCT("phstereo", "Phase Stereo", 0.0f),
    // OCT DOWN
    HEM_PCT("ocdirect", "Direct", 75.0f), HEM_PCT("ocone", "One Down", 75.0f),
    {"oconepan", "One Down Pan", "deg", -45.0f, 45.0f, 22.5f},
    HEM_PCT("octwo", "Two Down", 75.0f),
    {"octwopan", "Two Down Pan", "deg", -45.0f, 45.0f, -22.5f},
    // WAH
    HEM_PCT("whsens", "Wah Sensitivity", 50.0f), HEM_PCT("whspeed", "Wah Speed", 50.0f), HEM_PCT("whrange", "Wah Range", 50.0f),
    {"whmode", "Wah Mode", "", 0, 1, 0, Taper::Choice, kWahModeN},
    HEM_PCT("whpos", "Wah Position", 50.0f),
    // CHORUS
    {"chdepth", "Chorus Depth", "%", 1.0f, 100.0f, 10.0f},
    {"chrate", "Chorus Rate", "Hz", 0.1f, 10.0f, 0.77f},
    // THICKEN
    HEM_PCT("thmix", "Thicken Mix", 50.0f),
    {"thdetune", "Thicken Detune", "cents", 1.0f, 25.0f, 8.0f},
    {"thdelay", "Thicken Delay", "ms", 12.0f, 100.0f, 12.0f},
    HEM_PCT("thfeedback", "Thicken Feedback", 0.0f), HEM_PCT("thstereo", "Thicken Stereo", 50.0f),
    // PITCH RIDE
    HEM_PCT("prmix", "Pitch Ride Mix", 50.0f),
    {"prlow", "Pitch Ride Low", "st", -12.0f, 12.0f, -12.0f},
    {"prhigh", "Pitch Ride High", "st", -12.0f, 12.0f, 12.0f},
    HEM_PCT("prpos", "Pitch Ride Position", 50.0f),
    // BASS RIDE
    HEM_PCT("brmix", "Bass Ride Mix", 50.0f),
    {"brlow", "Bass Ride Low", "st", -12.0f, 12.0f, -12.0f},
    {"brhigh", "Bass Ride High", "st", -12.0f, 12.0f, 12.0f},
    HEM_PCT("brpos", "Bass Ride Position", 50.0f),
    // ECHO
    HEM_PCT("ecmix", "Echo Mix", 50.0f), HEM_SYNC("ecsync"), HEM_NOTE("ecnote", 12),
    {"ectime", "Echo Time", "s", 0.01f, 2.0f, 0.5f, Taper::Log},
    {"ecfeedback", "Echo Feedback", "", 0.0f, 10.0f, 5.0f},
    {"echighcut", "Echo High Cut", "kHz", 0.1f, 7.0f, 7.0f, Taper::Log},
    {"ecstereo", "Echo Stereo", "", 0.0f, 10.0f, 10.0f},
    // FLIP ECHO
    HEM_PCT("femix", "Flip Echo Mix", 50.0f), HEM_SYNC("fesync"), HEM_NOTE("fenote", 12),
    {"fetime", "Flip Echo Time", "s", 0.01f, 1.0f, 0.5f, Taper::Log},
    {"fepitch", "Flip Echo Pitch", "cents", -1200.0f, 1200.0f, 0.0f},
    HEM_PCT("fefeedback", "Flip Echo Feedback", 50.0f), HEM_PCT("festereo", "Flip Echo Stereo", 100.0f),
    {"fereverse", "Reverse", "", 0, 1, 1, Taper::Toggle, kOnOff},
    // ROOM
    HEM_PCT("rmmix", "Room Mix", 50.0f),
    {"rmpre", "Room Pre-Delay", "ms", 11.0f, 160.0f, 80.0f},
    {"rmtime", "Room Time", "s", 0.2f, 10.0f, 1.4f, Taper::Log},
    {"rmtone", "Room Tone", "", 0.1f, 2.0f, 0.6f},
    // SPRING
    HEM_PCT("spmix", "Spring Mix", 50.0f),
    {"sptime", "Spring Time", "s", 0.2f, 10.0f, 3.6f, Taper::Log},
    {"sppre", "Spring Pre-Delay", "ms", 20.0f, 170.0f, 20.0f},
    // SQUEEZE
    HEM_PCT("sqamount", "Squeeze Amount", 0.0f),
    {"sqattack", "Squeeze Attack", "ms", 1.0f, 50.0f, 4.99f},
    {"sqrelease", "Squeeze Release", "ms", 50.0f, 2000.0f, 752.0f, Taper::Log},
    // SLAM
    HEM_PCT("slsqueeze", "Slam Squeeze", 0.0f),
    {"slattack", "Slam Attack", "ms", 0.0f, 50.0f, 5.0f},
    {"sloutput", "Slam Output", "dB", -30.0f, 0.0f, 0.0f},
    // GATE
    {"gtthreshold", "Gate Threshold", "dB", -96.0f, 0.0f, -50.0f},
    {"gtattack", "Gate Attack", "ms", 0.01f, 1000.0f, 0.05f, Taper::Log},
    {"gtrelease", "Gate Release", "ms", 1.0f, 10000.0f, 100.0f, Taper::Log},
    {"gthold", "Gate Hold", "ms", 0.01f, 5000.0f, 0.2f, Taper::Log},
    // CLEAN UP
    {"cugate", "Clean Up Gate", "dB", -96.0f, 0.0f, -60.0f},
    HEM_PCT("cusqueeze", "Clean Up Squeeze", 50.0f),
    // TONE STACK
    {"tslow", "Stack Low", "dB", -18.0f, 18.0f, 0.0f},
    {"tsmid", "Stack Mid", "dB", -18.0f, 18.0f, 0.0f},
    {"tshigh", "Stack High", "dB", -18.0f, 18.0f, 0.0f},
    {"tslowcut", "Stack Low Cut", "Hz", 16.0f, 310.0f, 40.0f, Taper::Log},
    {"tshighcut", "Stack High Cut", "Hz", 4000.0f, 18000.0f, 14000.0f, Taper::Log},
    // GRAPHIC
    {"eq125", "Graphic 125 Hz", "dB", -12.0f, 12.0f, 0.0f}, {"eq250", "Graphic 250 Hz", "dB", -12.0f, 12.0f, 0.0f},
    {"eq500", "Graphic 500 Hz", "dB", -12.0f, 12.0f, 0.0f}, {"eq1k", "Graphic 1 kHz", "dB", -12.0f, 12.0f, 0.0f},
    {"eq2k", "Graphic 2 kHz", "dB", -12.0f, 12.0f, 0.0f}, {"eq4k", "Graphic 4 kHz", "dB", -12.0f, 12.0f, 0.0f},
    // VOLUME
    HEM_PCT("vlvolume", "Volume", 100.0f),
    {"vlscale", "Volume Scale", "", 0, 1, 0, Taper::Choice, kScaleN},
    {"vlmin", "Volume Lowest", "dB", -144.0f, 0.0f, -24.0f},
    {"vlmax", "Volume Highest", "dB", -144.0f, 12.0f, 0.0f},
};
#undef HEM_PCT
#undef HEM_SYNC
#undef HEM_NOTE
#undef HEM_WAVE

// ---- what the plate shows per pedal -----------------------------------------------------------------
constexpr PedalCtl kOdK[] = {{OdDrive, "DRIVE"}, {OdLevel, "LEVEL"}, {OdTone, "TONE"}};
constexpr PedalCtl kDsK[] = {{DsDrive, "DRIVE"}, {DsLevel, "LEVEL"}, {DsBody, "BODY"}, {DsTone, "TONE"}};
constexpr PedalCtl kFzK[] = {{FzSustain, "SUSTAIN"}, {FzLevel, "LEVEL"}, {FzTone, "TONE"}};
constexpr PedalCtl kRpK[] = {{RpDrive, "DRIVE"}, {RpTone, "TONE"}, {RpLevel, "LEVEL"}};
constexpr PedalCtl kHvK[] = {{HvDrive, "DRIVE"}, {HvLevel, "LEVEL"}, {HvLow, "LOW"}, {HvHigh, "HIGH"}, {HvMidFreq, "MID FREQ"}, {HvMidGain, "MID GAIN"}};
constexpr PedalCtl kFlK[] = {{FlDepth, "DEPTH"}, {FlRate, "RATE"}, {FlDelay, "DELAY"}, {FlFeedback, "FEEDBACK"}, {FlStereo, "STEREO"}};
constexpr PedalCtl kFlS[] = {{FlSync, "SYNC"}, {FlNote, "NOTE"}};
constexpr PedalCtl kVtK[] = {{VtRate, "RATE"}, {VtVibrato, "VIBRATO"}, {VtTremolo, "TREMOLO"}};
constexpr PedalCtl kVtS[] = {{VtWave, "WAVE"}, {VtSync, "SYNC"}, {VtNote, "NOTE"}};
constexpr PedalCtl kPnK[] = {{PnRate, "RATE"}, {PnWidth, "WIDTH"}};
constexpr PedalCtl kPnS[] = {{PnWave, "WAVE"}, {PnSync, "SYNC"}, {PnNote, "NOTE"}};
constexpr PedalCtl kPhK[] = {{PhDepth, "DEPTH"}, {PhRate, "RATE"}, {PhStereo, "STEREO"}};
constexpr PedalCtl kPhS[] = {{PhSync, "SYNC"}, {PhNote, "NOTE"}};
constexpr PedalCtl kOcK[] = {{OcDirect, "DIRECT"}, {OcOne, "ONE DOWN"}, {OcOnePan, "ONE PAN"}, {OcTwo, "TWO DOWN"}, {OcTwoPan, "TWO PAN"}};
constexpr PedalCtl kWhK[] = {{WhSens, "SENSITIVITY"}, {WhSpeed, "SPEED"}, {WhRange, "RANGE"}, {WhPos, "POSITION"}};
constexpr PedalCtl kWhS[] = {{WhMode, "MODE"}};
constexpr PedalCtl kChK[] = {{ChDepth, "DEPTH"}, {ChRate, "RATE"}};
constexpr PedalCtl kThK[] = {{ThMix, "MIX"}, {ThDetune, "DETUNE"}, {ThDelay, "DELAY"}, {ThFeedback, "FEEDBACK"}, {ThStereo, "STEREO"}};
constexpr PedalCtl kPrK[] = {{PrMix, "MIX"}, {PrLow, "LOW SHIFT"}, {PrHigh, "HIGH SHIFT"}, {PrPos, "POSITION"}};
constexpr PedalCtl kBrK[] = {{BrMix, "MIX"}, {BrLow, "LOW SHIFT"}, {BrHigh, "HIGH SHIFT"}, {BrPos, "POSITION"}};
constexpr PedalCtl kEcK[] = {{EcMix, "MIX"}, {EcTime, "TIME"}, {EcFeedback, "FEEDBACK"}, {EcHighCut, "HIGH CUT"}, {EcStereo, "STEREO"}};
constexpr PedalCtl kEcS[] = {{EcSync, "SYNC"}, {EcNote, "NOTE"}};
constexpr PedalCtl kFeK[] = {{FeMix, "MIX"}, {FeTime, "TIME"}, {FePitch, "PITCH"}, {FeFeedback, "FEEDBACK"}, {FeStereo, "STEREO"}};
constexpr PedalCtl kFeS[] = {{FeSync, "SYNC"}, {FeNote, "NOTE"}, {FeReverse, "REVERSE"}};
constexpr PedalCtl kRmK[] = {{RmMix, "MIX"}, {RmPre, "PRE-DELAY"}, {RmTime, "TIME"}, {RmTone, "TONE"}};
constexpr PedalCtl kSpK[] = {{SpMix, "MIX"}, {SpTime, "TIME"}, {SpPre, "PRE-DELAY"}};
constexpr PedalCtl kSqK[] = {{SqAmount, "AMOUNT"}, {SqAttack, "ATTACK"}, {SqRelease, "RELEASE"}};
constexpr PedalCtl kSlK[] = {{SlSqueeze, "SQUEEZE"}, {SlAttack, "ATTACK"}, {SlOutput, "OUTPUT"}};
constexpr PedalCtl kGtK[] = {{GtThreshold, "THRESHOLD"}, {GtAttack, "ATTACK"}, {GtRelease, "RELEASE"}, {GtHold, "HOLD"}};
constexpr PedalCtl kCuK[] = {{CuGate, "GATE"}, {CuSqueeze, "SQUEEZE"}};
constexpr PedalCtl kTsK[] = {{TsLow, "LOW"}, {TsMid, "MID"}, {TsHigh, "HIGH"}, {TsLowCut, "LOW CUT"}, {TsHighCut, "HIGH CUT"}};
constexpr PedalCtl kEqK[] = {{Eq125, "125"}, {Eq250, "250"}, {Eq500, "500"}, {Eq1k, "1K"}, {Eq2k, "2K"}, {Eq4k, "4K"}};
constexpr PedalCtl kVlK[] = {{VlVolume, "VOLUME"}, {VlMin, "LOWEST"}, {VlMax, "HIGHEST"}};
constexpr PedalCtl kVlS[] = {{VlScale, "SCALE"}};
template <std::size_t N>
constexpr PedalFace pf(const PedalCtl (&k)[N]) { return {k, static_cast<int>(N), nullptr, 0}; }
template <std::size_t N, std::size_t M>
constexpr PedalFace pf(const PedalCtl (&k)[N], const PedalCtl (&s)[M]) { return {k, static_cast<int>(N), s, static_cast<int>(M)}; }
const PedalFace kFaces[kPedals] = {pf(kOdK), pf(kDsK), pf(kFzK), pf(kRpK), pf(kHvK),
                                   pf(kFlK, kFlS), pf(kVtK, kVtS), pf(kPnK, kPnS), pf(kPhK, kPhS), pf(kOcK), pf(kWhK, kWhS), pf(kChK),
                                   pf(kThK), pf(kPrK), pf(kBrK),
                                   pf(kEcK, kEcS), pf(kFeK, kFeS), pf(kRmK), pf(kSpK),
                                   pf(kSqK), pf(kSlK), pf(kGtK), pf(kCuK), pf(kTsK), pf(kEqK), pf(kVlK, kVlS)};

// ---- small pieces -------------------------------------------------------------------------------------
// a LEVEL knob 0..100: 50 = unity, 100 = +12 dB, 0 = silent (a pot's square law)
inline float levelGain(float pct) noexcept {
    const float v = std::max(pct, 0.0f) / 50.0f;
    return v * v;
}
// MIX: both full at 50, the dry one going at 100, the wet one at 0
inline float dryOf(float m) noexcept { return std::min(1.0f, 2.0f * (1.0f - m)); }
inline float wetOf(float m) noexcept { return std::min(1.0f, 2.0f * m); }
// a pan of ±45 degrees: constant power, both sides 1 in the middle
inline void panGains(float deg, float& l, float& r) noexcept {
    const float th = (std::clamp(deg, -45.0f, 45.0f) + 45.0f) / 90.0f * static_cast<float>(std::numbers::pi) * 0.5f;
    l = std::cos(th) * std::numbers::sqrt2_v<float>;
    r = std::sin(th) * std::numbers::sqrt2_v<float>;
}
inline float fuzzCurve(float x) noexcept { return x > 0.0f ? 1.0f - std::exp(-x) : -0.6f * (1.0f - std::exp(x / 0.6f)); }
// first-order all-pass for the phaser
struct Ap1 {
    float z = 0.0f;
    float tick(float x, float a) noexcept {
        const float y = a * x + z;
        z = x - a * y;
        return y;
    }
};
// half the pitch (and a quarter): a flip-flop on every other rising crossing of the low-passed input
struct Divider {
    float env = 0.0f, q1 = 1.0f, q2 = 1.0f;
    bool high = false;
    void clear() noexcept { env = 0.0f; q1 = q2 = 1.0f; high = false; }
    void tick(float x, float envRel) noexcept {
        const float a = std::fabs(x);
        env = a > env ? a : a + envRel * (env - a);
        const float h = 0.1f * env;
        if (!high && x > h) {
            high = true;
            const float before = q1;
            q1 = -q1;
            if (before < 0.0f) q2 = -q2;
        } else if (high && x < -h) {
            high = false;
        }
    }
};
// a state-variable filter (topology-preserving), for the wah
struct Svf {
    float s1 = 0.0f, s2 = 0.0f;
    void clear() noexcept { s1 = s2 = 0.0f; }
    // returns the band-pass; lp gets the low-pass
    float tick(float x, float g, float k, float& lp) noexcept {
        const float hp = (x - (g + k) * s1 - s2) / (1.0f + g * (g + k));
        const float bp = g * hp + s1;
        s1 = g * hp + bp;
        lp = g * bp + s2;
        s2 = g * bp + lp;
        return bp;
    }
};
// one spring of the SPRING pedal: a loop with the dispersion that makes the drip (as in COIL)
constexpr int kStages = 16;
struct SpringLine {
    hc::fx::DelayLine loop;
    std::array<float, kStages> ap{};
    float lp = 0.0f, len = 1000.0f, g = 0.0f;
    void clear() noexcept {
        loop.clear();
        ap.fill(0.0f);
        lp = 0.0f;
    }
    float tick(float x, float drip, float lpA) noexcept {
        const float back = loop.read(len - 1.0f);
        lp = back + lpA * (lp - back);
        float v = x + g * lp;
        for (int k = 0; k < kStages; ++k) {
            const float y = -drip * v + ap[static_cast<std::size_t>(k)];
            ap[static_cast<std::size_t>(k)] = v + drip * y;
            v = y;
        }
        loop.write(v);
        return lp;
    }
};
constexpr float kSpringMs[3] = {31.0f, 37.0f, 43.0f};
constexpr float kSpringDrip = 0.6f, kSpringLpHz = 4200.0f, kSpringLowCut = 110.0f;

constexpr std::uint32_t kChunk = 32;
constexpr double kEchoMaxS = 2.0, kFlipMaxS = 1.0, kRoomPreMs = 160.0, kSpringPreMs = 170.0;
constexpr int kKey = 8;   // the most parameters a pedal has

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        const int ped = pedalOf(i);
        if (ped < 0) return true;
        if (ped != pick(Which, kPedals - 1)) return false;
        // the free rate or time only matters off sync, the sync note only on it
        auto sync = [&](int s) { return p(s) >= 0.5f; };
        switch (i) {
        case FlRate: return !sync(FlSync);
        case FlNote: return sync(FlSync);
        case VtRate: return !sync(VtSync);
        case VtNote: return sync(VtSync);
        case PnRate: return !sync(PnSync);
        case PnNote: return sync(PnSync);
        case PhRate: return !sync(PhSync);
        case PhNote: return sync(PhSync);
        case EcTime: return !sync(EcSync);
        case EcNote: return sync(EcSync);
        case FeTime: return !sync(FeSync);
        case FeNote: return sync(FeSync);
        case WhSens: case WhSpeed: return p(WhMode) < 0.5f;
        case WhPos: return p(WhMode) >= 0.5f;
        default: return true;
        }
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lat_.prepare(OS::latency());
        for (auto& d : md_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);   // flange / vibrato / chorus
        for (auto& d : th_) d.prepare(static_cast<std::uint32_t>(0.105 * fs) + 8);
        for (auto& s : ps_) s.prepare(fs, 40.0);
        for (auto& s : psBass_) s.prepare(fs, 80.0);
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(kEchoMaxS * fs) + 8);
        for (auto& d : flip_) d.prepare(static_cast<std::uint32_t>(2.0 * kFlipMaxS * fs) + 16);
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(std::max(kRoomPreMs, kSpringPreMs) * 0.001 * fs) + 8);
        tail_.prepare(fs, 2.0);
        early_.prepare(fs, 130.0);
        for (auto& side : spring_)
            for (auto& s : side) s.loop.prepare(static_cast<std::uint32_t>(0.05 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        cur_ = pick(Which, kPedals - 1);
        fade_ = 1.0f;
        inGain_.prepare(fs_, 20, hc::fx::dbToGain(p(InputGain)));
        clearPedal();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    int pick(int param, int max) const noexcept { return std::clamp(static_cast<int>(std::lround(p(param))), 0, max); }
    static bool isDrive(int ped) noexcept { return ped <= PdHeavy; }

    // ---- the board ----------------------------------------------------------------------------------
    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        const int want = pick(Which, kPedals - 1);
        const float step = static_cast<float>(1.0 / (0.005 * fs_));
        const int mode = pick(Input, 2);
        const float wIn = hc::fx::dbToGain(p(InputGain));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = inGain_.next(wIn);
            float l = L[i] * g, r = R[i] * g;
            if (mode != InStereo) l = r = 0.5f * (l + r);
            L[i] = l;
            R[i] = r;
        }
        if (want != cur_ && fade_ <= 0.0f) {   // faded out: switch
            cur_ = want;
            clearPedal();
        }
        design();
        runPedal(L, R, n);
        if (!isDrive(cur_))
            for (std::uint32_t i = 0; i < n; ++i) {
                L[i] = lat_.l.process(L[i], OS::latency());
                R[i] = lat_.r.process(R[i], OS::latency());
            }
        for (std::uint32_t i = 0; i < n; ++i) {
            fade_ = want != cur_ ? std::max(0.0f, fade_ - step) : std::min(1.0f, fade_ + step);
            float l = L[i], r = R[i];
            if (mode == InMono) l = r = 0.5f * (l + r);
            L[i] = l * fade_;
            R[i] = r * fade_;
        }
    }

    void clearPedal() noexcept {
        lat_.clear();
        for (auto& o : os_) o.clear();
        for (auto& ch : f_)
            for (auto& b : ch) b.clear();
        for (auto& b : rumbleF_) b.clear();
        for (auto& d : md_) d.clear();
        for (auto& d : th_) d.clear();
        for (auto& s : ps_) s.clear();
        for (auto& s : psBass_) s.clear();
        for (auto& d : echo_) d.clear();
        for (auto& d : flip_) d.clear();
        for (auto& d : pre_) d.clear();
        tail_.clear();
        early_.clear();
        for (auto& side : spring_)
            for (auto& s : side) s.clear();
        for (auto& ch : ap_)
            for (auto& a : ch) a.z = 0.0f;
        for (auto& d : div_) d.clear();
        for (auto& s : svf_) s.clear();
        phase_ = 0.0;
        env_ = wahEnv_ = gr_ = 0.0f;
        gate_.clear();
        dc_[0] = dc_[1] = 0.0f;
        fbL_ = fbR_ = 0.0f;
        lpE_[0] = lpE_[1] = 0.0f;
        lastWet_[0] = lastWet_[1] = 0.0f;
        flipK_ = flipN_ = 0;
        key_.fill(-1e9f);
        design();
        for (int k = 0; k < kSm; ++k) sm_[k].prepare(fs_, 20, target(k));
    }

    // ---- smoothed values per pedal (gains, mixes, times): slot k of the current pedal -------------------
    static constexpr int kSm = 6;
    float target(int k) const noexcept {
        switch (cur_) {
        case PdOverdrive: return k == 0 ? levelGain(p(OdLevel)) : k == 1 ? driveGain(cur_) : 0.0f;
        case PdHardClip: return k == 0 ? levelGain(p(DsLevel)) : k == 1 ? driveGain(cur_) : 0.0f;
        case PdFuzz: return k == 0 ? levelGain(p(FzLevel)) : k == 1 ? driveGain(cur_) : k == 2 ? p(FzTone) * 0.01f : 0.0f;
        case PdRasp: return k == 0 ? levelGain(p(RpLevel)) : k == 1 ? driveGain(cur_) : 0.0f;
        case PdHeavy: return k == 0 ? levelGain(p(HvLevel)) : k == 1 ? driveGain(cur_) : 0.0f;
        case PdFlange: return k == 0 ? p(FlDepth) * 0.01f : k == 1 ? ms(p(FlDelay)) : k == 2 ? 0.9f * p(FlFeedback) * 0.01f : 0.0f;
        case PdVibTrem: return k == 0 ? p(VtVibrato) * 0.01f * ms(2.5f) : k == 1 ? p(VtTremolo) * 0.01f : 0.0f;
        case PdAutoPan: return k == 0 ? p(PnWidth) * 0.01f : 0.0f;
        case PdPhase: return k == 0 ? p(PhDepth) / 0.8f : 0.0f;
        case PdOctDown: return k == 0 ? p(OcDirect) * 0.01f : k == 1 ? p(OcOne) * 0.01f : k == 2 ? p(OcTwo) * 0.01f : 0.0f;
        case PdWah: return k == 0 ? p(WhPos) * 0.01f : 0.0f;
        case PdChorus: return k == 0 ? p(ChDepth) * 0.01f * ms(5.0f) : 0.0f;
        case PdThicken: return k == 0 ? p(ThMix) * 0.01f : k == 1 ? ms(p(ThDelay)) : k == 2 ? 0.85f * p(ThFeedback) * 0.01f : k == 3 ? p(ThStereo) * 0.01f : 0.0f;
        case PdPitchRide: return k == 0 ? p(PrMix) * 0.01f : k == 1 ? rideShift(PrLow, PrHigh, PrPos) : 0.0f;
        case PdBassRide: return k == 0 ? p(BrMix) * 0.01f : k == 1 ? rideShift(BrLow, BrHigh, BrPos) : 0.0f;
        case PdEcho: return k == 0 ? p(EcMix) * 0.01f : k == 1 ? echoSamples() : k == 2 ? p(EcFeedback) / 8.7f : k == 3 ? p(EcStereo) * 0.1f : 0.0f;
        case PdFlipEcho: return k == 0 ? p(FeMix) * 0.01f : k == 1 ? 0.9f * p(FeFeedback) * 0.01f : k == 2 ? std::exp2(p(FePitch) / 1200.0f) : 0.0f;
        case PdRoom: return k == 0 ? p(RmMix) * 0.01f : k == 1 ? ms(p(RmPre)) : 0.0f;
        case PdSpring: return k == 0 ? p(SpMix) * 0.01f : k == 1 ? ms(p(SpPre)) : 0.0f;
        case PdSlam: return k == 0 ? hc::fx::dbToGain(p(SlOutput)) : 0.0f;
        case PdVolume: return k == 0 ? volumeGain() : 0.0f;
        default: return 0.0f;
        }
    }
    float ms(float v) const noexcept { return static_cast<float>(v * 0.001 * fs_); }
    float rideShift(int lo, int hi, int pos) const noexcept { return p(lo) + (p(hi) - p(lo)) * p(pos) * 0.01f; }
    double beats(int note) const noexcept { return kNoteBeats[pick(note, kNotes - 1)]; }
    double rateHz(int sync, int note, int rate) const noexcept { return p(sync) >= 0.5f ? tempo() / 60.0 / beats(note) : p(rate); }
    float echoSamples() const noexcept {
        const double s = p(EcSync) >= 0.5f ? beats(EcNote) * 60.0 / tempo() : p(EcTime);
        return static_cast<float>(std::clamp(s, 0.001, kEchoMaxS) * fs_);
    }
    int flipSamples() const noexcept {
        const double s = p(FeSync) >= 0.5f ? beats(FeNote) * 60.0 / tempo() : p(FeTime);
        return std::max(16, static_cast<int>(std::clamp(s, 0.005, kFlipMaxS) * fs_));
    }
    float volumeGain() const noexcept {
        const float lo = std::min(p(VlMin), p(VlMax)), hi = p(VlMax), v = p(VlVolume) * 0.01f;
        if (p(VlScale) >= 0.5f) return lo <= -143.9f && v <= 0.0f ? 0.0f : hc::fx::dbToGain(lo + (hi - lo) * v);   // LOG: even steps in dB
        const float gl = lo <= -143.9f ? 0.0f : hc::fx::dbToGain(lo), gh = hc::fx::dbToGain(hi);
        return gl + (gh - gl) * v;                                                                                 // LIN: even steps in level
    }
    // the drives' gain into the clipper
    float driveGain(int ped) const noexcept {
        switch (ped) {
        case PdOverdrive: return hc::fx::dbToGain(6.0f + 0.34f * p(OdDrive));
        case PdHardClip: return hc::fx::dbToGain(15.0f + 0.35f * p(DsDrive));
        case PdFuzz: return hc::fx::dbToGain(20.0f + 0.4f * p(FzSustain));
        case PdRasp: return hc::fx::dbToGain(10.0f + 0.4f * p(RpDrive));
        default: return hc::fx::dbToGain(20.0f + 0.4f * p(HvDrive));
        }
    }

    // ---- filters (redesigned when the pedal's settings move) --------------------------------------------
    void design() noexcept {
        std::array<float, kKey> k{};
        const int a = kFirst[cur_], b = kFirst[cur_ + 1];
        for (int i = a; i < b && i - a < kKey; ++i) k[static_cast<std::size_t>(i - a)] = p(i);
        if (k == key_) return;
        key_ = k;
        const C id{};
        std::array<C, 6> c{id, id, id, id, id, id};
        const double nyq = fs_ * 0.45;
        auto lp = [&](double hz, double q = 0.7071) { return C::make(T::LowPass, fs_, std::min(hz, nyq), q); };
        auto hp = [&](double hz, double q = 0.7071) { return C::make(T::HighPass, fs_, std::min(hz, nyq), q); };
        switch (cur_) {
        case PdOverdrive:   // pre: low cut and a mid lean; post: TONE dark 800 Hz .. bright 12.8 kHz
            c[0] = hp(160.0);
            c[1] = C::make(T::Bell, fs_, 720.0, 0.7, 6.0);
            c[2] = lp(800.0 * std::pow(16.0, p(OdTone) * 0.01));
            c[3] = lp(9000.0);
            break;
        case PdHardClip:    // post: BODY = the mid scoop (+6 .. -12 dB at 750 Hz), TONE = a low-pass
            c[0] = hp(120.0);
            c[2] = C::make(T::Bell, fs_, 750.0, 0.8, 6.0 - 0.18 * p(DsBody));
            c[3] = lp(p(DsTone));
            c[4] = lp(p(DsTone));
            break;
        case PdFuzz:        // post: TONE blends a 700 Hz low-pass into a 1.2 kHz high-pass
            c[0] = hp(80.0);
            c[2] = lp(700.0);
            c[3] = hp(1200.0);
            c[4] = lp(7000.0);
            break;
        case PdRasp:        // post: the resonant high-pass at 300 Hz, TONE a resonant low-pass 400 Hz .. 8 kHz
            c[0] = hp(60.0);
            c[2] = hp(300.0, 2.5);
            c[3] = lp(400.0 * std::pow(20.0, p(RpTone) * 0.01), 2.5);
            break;
        case PdHeavy:       // post: LOW at 120 Hz, the MID bell, HIGH a shelf from 3 kHz
            c[0] = hp(100.0);
            c[2] = C::make(T::Bell, fs_, 120.0, 0.7, p(HvLow));
            c[3] = C::make(T::Bell, fs_, p(HvMidFreq), 1.0, p(HvMidGain));
            c[4] = C::make(T::HighShelf, fs_, 3000.0, 0.7071, p(HvHigh));
            c[5] = lp(9000.0);
            break;
        case PdOctDown:     // the octave finder's input below 800 Hz; the squares rounded at 500 / 250 Hz
            c[0] = lp(800.0);
            c[1] = lp(800.0);
            c[2] = lp(500.0);
            c[3] = lp(500.0);
            c[4] = lp(250.0);
            c[5] = lp(250.0);
            break;
        case PdSpring: {
            c[0] = hp(kSpringLowCut);
            const double t60 = p(SpTime);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 3; ++s) {
                    auto& sp = spring_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(s)];
                    const double msl = kSpringMs[s] * (ch == 0 ? 1.0 : 1.07);
                    sp.len = static_cast<float>(msl * 0.001 * fs_);
                    sp.g = static_cast<float>(std::pow(10.0, -3.0 * msl * 0.001 / t60));
                }
            break;
        }
        case PdToneStack:
            c[0] = C::make(T::LowShelf, fs_, 310.0, 0.7071, p(TsLow));
            c[1] = C::make(T::Bell, fs_, 1000.0, 0.7, p(TsMid));
            c[2] = C::make(T::HighShelf, fs_, 4000.0, 0.7071, p(TsHigh));
            c[3] = hp(p(TsLowCut));
            c[4] = lp(p(TsHighCut));
            break;
        case PdGraphic: {
            constexpr double kHz[6] = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};
            for (int i = 0; i < 6; ++i) c[static_cast<std::size_t>(i)] = C::make(T::Bell, fs_, kHz[i], 10.0, p(Eq125 + i));
            for (auto& r : rumbleF_) r.c = hp(40.0);
            break;
        }
        case PdRoom: {
            const double t = p(RmTime), size = std::clamp(0.6 + 0.14 * t, 0.6, 2.0);
            tail_.set(size, t, 1.1, p(RmTone), 250.0, 3500.0, 0.85);
            early_.set(std::min(120.0, 40.0 * size), 5, 1.3, 16, 7000.0);
            break;
        }
        default: break;
        }
        for (auto& ch : f_)
            for (std::size_t i = 0; i < c.size(); ++i) ch[i].c = c[i];
    }

    // ---- the pedals -------------------------------------------------------------------------------------
    void runPedal(float* L, float* R, std::uint32_t n) noexcept {
        switch (cur_) {
        case PdOverdrive: case PdHardClip: case PdFuzz: case PdRasp: case PdHeavy: drive(L, R, n); break;
        case PdFlange: flange(L, R, n); break;
        case PdVibTrem: vibTrem(L, R, n); break;
        case PdAutoPan: autoPan(L, R, n); break;
        case PdPhase: phaser(L, R, n); break;
        case PdOctDown: octDown(L, R, n); break;
        case PdWah: wah(L, R, n); break;
        case PdChorus: chorus(L, R, n); break;
        case PdThicken: thicken(L, R, n); break;
        case PdPitchRide: case PdBassRide: ride(L, R, n); break;
        case PdEcho: echo(L, R, n); break;
        case PdFlipEcho: flipEcho(L, R, n); break;
        case PdRoom: room(L, R, n); break;
        case PdSpring: springs(L, R, n); break;
        case PdSqueeze: case PdSlam: case PdCleanUp: squeeze(L, R, n); break;
        case PdGate: gate(L, R, n); break;
        case PdToneStack: case PdGraphic: eq(L, R, n); break;
        case PdVolume: volume(L, R, n); break;
        default: break;
        }
    }

    // pre-filters (slots 0, 1) -> gain -> clip at 4x -> post filters (2..5) -> DC block -> LEVEL
    void drive(float* L, float* R, std::uint32_t n) noexcept {
        const int ped = cur_;
        const float wG = driveGain(ped), wLvl = target(0), wTone = target(2);
        float g[kChunk], lvl[kChunk], tone[kChunk];
        for (std::uint32_t i = 0; i < n; ++i) {
            lvl[i] = sm_[0].next(wLvl);
            g[i] = sm_[1].next(wG);
            tone[i] = sm_[2].next(wTone);
        }
        float* io[2] = {L, R};
        float x4[kChunk * OS::kFactor];
        for (int c = 0; c < 2; ++c) {
            auto& f = f_[static_cast<std::size_t>(c)];
            for (std::uint32_t i = 0; i < n; ++i) io[c][i] = f[1].tick(f[0].tick(io[c][i])) * g[i];
            os_[c].up(io[c], x4, n);
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) {
                const float v = x4[j];
                float y = 0.0f;
                switch (ped) {
                case PdOverdrive: y = softClip(v); break;                                              // smooth, even both ways
                case PdHardClip: y = 0.6f * (std::tanh(2.0f * (v + 0.1f)) - std::tanh(0.2f)); break;   // harder, a little lopsided
                case PdFuzz: y = 0.6f * fuzzCurve(v); break;                                          // squared off, very lopsided
                case PdRasp: y = 0.5f * std::clamp(v, -1.2f, 1.2f); break;                           // flat-topped
                default: y = 0.55f * std::tanh(v); break;                                             // HEAVY
                }
                x4[j] = y;
            }
            os_[c].down(x4, io[c], n);
            float& dc = dc_[c];
            for (std::uint32_t i = 0; i < n; ++i) {
                float x = io[c][i];
                if (ped == PdFuzz) {
                    const float lo = f[2].tick(x), hi = f[3].tick(x);
                    x = f[4].tick(lo + tone[i] * (hi - lo));
                } else {
                    x = f[5].tick(f[4].tick(f[3].tick(f[2].tick(x))));
                }
                dc += (x - dc) * 0.0005f;
                io[c][i] = (x - dc) * lvl[i];
            }
        }
    }

    void advance(double hz) noexcept {
        phase_ += hz / fs_;
        if (phase_ >= 1.0) phase_ -= std::floor(phase_);
    }

    void flange(float* L, float* R, std::uint32_t n) noexcept {
        const double hz = rateHz(FlSync, FlNote, FlRate);
        const double off = 0.5 * p(FlStereo) * 0.01;
        const float minD = ms(0.1f);
        const float wD = target(0), wDl = target(1), wFb = target(2);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float depth = sm_[0].next(wD), dl = sm_[1].next(wDl), fb = sm_[2].next(wFb);
            for (int c = 0; c < 2; ++c) {
                const float uni = 0.5f * (1.0f + hideout::lfo(phase_ + (c ? off : 0.0), Wave::Sine));
                const float d = minD + (dl - minD) * (1.0f - depth * uni);
                auto& line = md_[c];
                const float w = line.read(d - 1.0f);
                const float x = io[c][i];
                line.write(x + fb * w);
                io[c][i] = 0.5f * (x + (1.0f - fb) * w);
            }
            advance(hz);
        }
    }

    void vibTrem(float* L, float* R, std::uint32_t n) noexcept {
        const double hz = rateHz(VtSync, VtNote, VtRate);
        const Wave wv = kWaveOf[pick(VtWave, 4)];
        const float wSw = target(0), wTr = target(1);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float swing = sm_[0].next(wSw), trem = sm_[1].next(wTr);
            const float v = hideout::lfo(phase_, wv);
            const float d = swing * (1.0f + v), g = 1.0f - trem * 0.5f * (1.0f - v);
            for (int c = 0; c < 2; ++c) {
                md_[c].write(io[c][i]);
                io[c][i] = md_[c].read(d) * g;
            }
            advance(hz);
        }
    }

    void autoPan(float* L, float* R, std::uint32_t n) noexcept {
        const double hz = rateHz(PnSync, PnNote, PnRate);
        const Wave wv = kWaveOf[pick(PnWave, 4)];
        const float wW = target(0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float width = sm_[0].next(wW);
            float gl = 1.0f, gr = 1.0f;
            panGains(45.0f * width * hideout::lfo(phase_, wv), gl, gr);
            const float m = 0.5f * (L[i] + R[i]);
            L[i] = m * gl;
            R[i] = m * gr;
            advance(hz);
        }
    }

    void phaser(float* L, float* R, std::uint32_t n) noexcept {
        const double hz = rateHz(PhSync, PhNote, PhRate);
        const double off = 0.5 * p(PhStereo) * 0.01;
        const float wD = target(0);
        float* io[2] = {L, R};
        const float piOverFs = static_cast<float>(std::numbers::pi / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float depth = sm_[0].next(wD);
            for (int c = 0; c < 2; ++c) {
                const float fc = 900.0f * std::exp2(2.5f * depth * hideout::lfo(phase_ + (c ? off : 0.0), Wave::Sine));
                const float t = std::tan(std::min(fc, static_cast<float>(fs_) * 0.45f) * piOverFs);
                const float a = (t - 1.0f) / (t + 1.0f);
                float& fb = c ? fbR_ : fbL_;
                float v = io[c][i] + 0.4f * fb;
                for (auto& s : ap_[static_cast<std::size_t>(c)]) v = s.tick(v, a);
                fb = v;
                io[c][i] = 0.5f * (io[c][i] + v);
            }
            advance(hz);
        }
    }

    void octDown(float* L, float* R, std::uint32_t n) noexcept {
        float l1 = 1.0f, r1 = 1.0f, l2 = 1.0f, r2 = 1.0f;
        panGains(p(OcOnePan), l1, r1);
        panGains(p(OcTwoPan), l2, r2);
        const float wDir = target(0), wOne = target(1), wTwo = target(2), envRel = coefMs(fs_, 30.0);
        auto& f = f_[0];
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dir = sm_[0].next(wDir), one = sm_[1].next(wOne), two = sm_[2].next(wTwo);
            const float x = f[1].tick(f[0].tick(0.5f * (L[i] + R[i])));
            div_[0].tick(x, envRel);
            const float lev = div_[0].env;
            const float o1 = f[3].tick(f[2].tick(div_[0].q1 * lev)), o2 = f[5].tick(f[4].tick(div_[0].q2 * lev));
            L[i] = dir * L[i] + one * l1 * o1 + two * l2 * o2;
            R[i] = dir * R[i] + one * r1 * o1 + two * r2 * o2;
        }
    }

    void wah(float* L, float* R, std::uint32_t n) noexcept {
        const bool hand = p(WhMode) >= 0.5f;
        const float sens = hc::fx::dbToGain(p(WhSens) - 30.0f);
        const float spd = p(WhSpeed) * 0.01f;
        const float att = coefMs(fs_, 30.0 - 28.0 * spd), rel = coefMs(fs_, 500.0 - 470.0 * spd);
        const float octs = 1.0f + 3.0f * p(WhRange) * 0.01f, wPos = target(0);
        constexpr float kK = 1.0f / 4.0f;   // Q 4
        const float piOverFs = static_cast<float>(std::numbers::pi / fs_);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = std::max(std::fabs(L[i]), std::fabs(R[i]));
            wahEnv_ = envStep(wahEnv_, a, att, rel);
            const float handPos = sm_[0].next(wPos);
            const float pos = hand ? handPos : std::min(1.0f, wahEnv_ * sens);
            const float fc = std::min(350.0f * std::exp2(octs * pos), static_cast<float>(fs_) * 0.45f);
            const float g = std::tan(fc * piOverFs);
            for (int c = 0; c < 2; ++c) {
                float lp = 0.0f;
                const float bp = svf_[c].tick(io[c][i], g, kK, lp);
                io[c][i] = 1.6f * kK * bp + 0.15f * lp;
            }
        }
    }

    void chorus(float* L, float* R, std::uint32_t n) noexcept {
        const double hz = p(ChRate);
        const float centre = ms(8.0f), wSw = target(0);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float swing = sm_[0].next(wSw);
            for (int c = 0; c < 2; ++c) {
                const float d = centre + swing * hideout::lfo(phase_ + (c ? 0.25 : 0.0), Wave::Sine);
                md_[c].write(io[c][i]);
                io[c][i] = 0.7071f * (io[c][i] + md_[c].read(d));
            }
            advance(hz);
        }
    }

    void thicken(float* L, float* R, std::uint32_t n) noexcept {
        const float up = std::exp2(p(ThDetune) / 1200.0f), down = 1.0f / up;
        const float wM = target(0), wDl = target(1), wFb = target(2), wSt = target(3);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = sm_[0].next(wM), dl = sm_[1].next(wDl), fb = sm_[2].next(wFb), st = sm_[3].next(wSt);
            const float in[2] = {L[i], R[i]};
            float w[2];
            for (int c = 0; c < 2; ++c) {
                w[c] = th_[c].read(dl - 1.0f);
                const float shifted = ps_[c].tick(in[c], c ? down : up);
                th_[c].write(shifted + fb * w[c]);
            }
            // STEREO: 0 = both copies in the middle, 100 = the sharp one left, the flat one right
            const float a = 0.5f + 0.5f * st, b = 1.0f - a;
            const float wl = a * w[0] + b * w[1], wr = b * w[0] + a * w[1];
            L[i] = dryOf(m) * in[0] + wetOf(m) * wl;
            R[i] = dryOf(m) * in[1] + wetOf(m) * wr;
        }
    }

    void ride(float* L, float* R, std::uint32_t n) noexcept {
        const bool bass = cur_ == PdBassRide;
        const float wM = target(0), wSh = target(1);
        auto& shifters = bass ? psBass_ : ps_;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = sm_[0].next(wM), st = sm_[1].next(wSh);
            const float ratio = std::exp2(st / 12.0f);
            const float wl = shifters[0].tick(L[i], ratio), wr = shifters[1].tick(R[i], ratio);
            L[i] = dryOf(m) * L[i] + wetOf(m) * wl;
            R[i] = dryOf(m) * R[i] + wetOf(m) * wr;
        }
    }

    void echo(float* L, float* R, std::uint32_t n) noexcept {
        const float hcHz = std::min(p(EcHighCut) * 1000.0f, static_cast<float>(fs_) * 0.45f);
        const float a = std::exp(-2.0f * static_cast<float>(std::numbers::pi) * hcHz / static_cast<float>(fs_));
        const float wM = target(0), wT = target(1), wFb = target(2), wSt = target(3);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = sm_[0].next(wM), t = sm_[1].next(wT), fb = sm_[2].next(wFb), s = sm_[3].next(wSt);
            const float wl = echo_[0].read(t - 1.0f), wr = echo_[1].read(t - 1.0f);
            // the loop's high cut
            lpE_[0] = wl + a * (lpE_[0] - wl);
            lpE_[1] = wr + a * (lpE_[1] - wr);
            // STEREO: how much each side's echo crosses to the other (1 = they bounce side to side)
            const float back[2] = {(1.0f - s) * lpE_[0] + s * lpE_[1], (1.0f - s) * lpE_[1] + s * lpE_[0]};
            const float mono = 0.5f * (L[i] + R[i]);
            const float inL = (1.0f - s) * L[i] + s * mono, inR = (1.0f - s) * R[i];
            echo_[0].write(inL + std::tanh(fb * back[0]));
            echo_[1].write(inR + std::tanh(fb * back[1]));
            L[i] = dryOf(m) * L[i] + wetOf(m) * wl;
            R[i] = dryOf(m) * R[i] + wetOf(m) * wr;
        }
    }

    // REVERSE: each piece of TIME is played backwards by two heads half a piece apart, each faded in and
    // out (sin^2), so together they hold a steady level. A new TIME is taken up at the start of a piece.
    void flipEcho(float* L, float* R, std::uint32_t n) noexcept {
        const bool rev = p(FeReverse) >= 0.5f;
        const float st = p(FeStereo) * 0.01f;
        const float wM = target(0), wFb = target(1), wPitch = target(2);
        const float pi = static_cast<float>(std::numbers::pi);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = sm_[0].next(wM), fb = sm_[1].next(wFb), ratio = sm_[2].next(wPitch);
            if (flipK_ <= 0 || flipN_ <= 0) {
                flipN_ = flipSamples();
                flipK_ = flipN_;
            }
            const int N = flipN_;
            const int k = N - flipK_;   // 0 .. N-1 into the piece
            --flipK_;
            const float in[2] = {L[i], R[i]};
            float out[2];
            for (int c = 0; c < 2; ++c) {
                auto& line = flip_[c];
                line.write(in[c] + fb * lastWet_[c]);
                float y = 0.0f;
                if (rev) {
                    // the right side's pieces start STEREO x a quarter piece later
                    const int kc = (k + (c ? static_cast<int>(st * 0.25f * static_cast<float>(N)) : 0)) % N;
                    const int k2 = (kc + N / 2) % N;
                    const float w1 = std::sin(pi * static_cast<float>(kc) / static_cast<float>(N));
                    const float w2 = std::sin(pi * static_cast<float>(k2) / static_cast<float>(N));
                    y = w1 * w1 * line.read(2.0f * static_cast<float>(kc)) + w2 * w2 * line.read(2.0f * static_cast<float>(k2));
                } else {
                    y = line.read(static_cast<float>(N) * (c ? 1.0f + 0.15f * st : 1.0f) - 1.0f);
                }
                y = ps_[c].tick(y, ratio);
                lastWet_[c] = y;
                out[c] = dryOf(m) * in[c] + wetOf(m) * y;
            }
            L[i] = out[0];
            R[i] = out[1];
        }
    }

    void room(float* L, float* R, std::uint32_t n) noexcept {
        const float wM = target(0), wP = target(1);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = sm_[0].next(wM), pd = sm_[1].next(wP);
            pre_[0].write(L[i]);
            pre_[1].write(R[i]);
            const float xl = pre_[0].read(pd), xr = pre_[1].read(pd);
            float el = 0.0f, er = 0.0f, tl = 0.0f, tr = 0.0f;
            early_.tick(xl, xr, el, er);
            tail_.tick(xl, xr, tl, tr);
            L[i] = dryOf(m) * L[i] + wetOf(m) * (0.6f * el + tl);
            R[i] = dryOf(m) * R[i] + wetOf(m) * (0.6f * er + tr);
        }
    }

    void springs(float* L, float* R, std::uint32_t n) noexcept {
        const float lpA = std::exp(-2.0f * static_cast<float>(std::numbers::pi) * kSpringLpHz / static_cast<float>(fs_));
        const float norm = 0.5f / std::sqrt(3.0f);
        const float wM = target(0), wP = target(1);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = sm_[0].next(wM), pd = sm_[1].next(wP);
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                pre_[c].write(x);
                const float in = pre_[c].read(pd) * norm;
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) sum += spring_[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)].tick(in, kSpringDrip, lpA) * (k & 1 ? -1.0f : 1.0f);
                const float wet = f_[static_cast<std::size_t>(c)][0].tick(sum);
                io[c][i] = dryOf(m) * x + wetOf(m) * wet;
            }
        }
    }

    // SQUEEZE / SLAM / CLEAN UP: one linked detector for both sides
    void squeeze(float* L, float* R, std::uint32_t n) noexcept {
        const int ped = cur_;
        float thr = 0.0f, ratio = 4.0f, attMs = 5.0f, relMs = 250.0f;
        if (ped == PdSqueeze) {
            thr = -0.4f * p(SqAmount);
            attMs = p(SqAttack);
            relMs = p(SqRelease);
        } else if (ped == PdSlam) {
            thr = -0.4f * p(SlSqueeze);
            ratio = 20.0f;
            attMs = std::max(0.01f, p(SlAttack));
            relMs = 120.0f;
        } else {   // CLEAN UP
            thr = -0.4f * p(CuSqueeze);
        }
        const float makeup = 0.5f * (-downDb(-18.0f, thr, ratio, 6.0f));
        const float att = coefMs(fs_, attMs), rel = coefMs(fs_, relMs), pkRel = coefMs(fs_, 10.0);
        const float wOut = target(0);
        const bool gated = ped == PdCleanUp, slam = ped == PdSlam;
        const float gAtt = coefMs(fs_, 0.5), gRel = coefMs(fs_, 80.0), gEnv = coefMs(fs_, 10.0);
        const auto hold = static_cast<std::uint32_t>(0.02 * fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = std::max(std::fabs(L[i]), std::fabs(R[i]));
            const float gateDb = gated ? gate_.tick(a, true, p(CuGate), -80.0f, hold, gEnv, gAtt, gRel) : 0.0f;
            env_ = a > env_ ? a : a + pkRel * (env_ - a);
            const float want = downDb(dbOf(env_), thr, ratio, 6.0f);
            gr_ = follow(gr_, want, att, rel);
            const float out = sm_[0].next(wOut);
            const float g = hc::fx::dbToGain(gr_ + makeup + gateDb) * (slam ? out : 1.0f);
            L[i] *= g;
            R[i] *= g;
        }
    }

    void gate(float* L, float* R, std::uint32_t n) noexcept {
        const float att = coefMs(fs_, p(GtAttack)), rel = coefMs(fs_, p(GtRelease)), gEnv = coefMs(fs_, 10.0);
        const auto hold = static_cast<std::uint32_t>(p(GtHold) * 0.001 * fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = std::max(std::fabs(L[i]), std::fabs(R[i]));
            const float g = hc::fx::dbToGain(gate_.tick(a, true, p(GtThreshold), -80.0f, hold, gEnv, att, rel));
            L[i] *= g;
            R[i] *= g;
        }
    }

    void eq(float* L, float* R, std::uint32_t n) noexcept {
        float* io[2] = {L, R};
        const bool graphic = cur_ == PdGraphic;
        const int bands = graphic ? 6 : 5;
        for (int c = 0; c < 2; ++c) {
            auto& f = f_[static_cast<std::size_t>(c)];
            for (std::uint32_t i = 0; i < n; ++i) {
                float x = io[c][i];
                if (graphic) x = rumbleF_[c].tick(x);
                for (int b = 0; b < bands; ++b) x = f[static_cast<std::size_t>(b)].tick(x);
                io[c][i] = x;
            }
        }
    }

    void volume(float* L, float* R, std::uint32_t n) noexcept {
        const float w = target(0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = sm_[0].next(w);
            L[i] *= g;
            R[i] *= g;
        }
    }

    double fs_ = 48000.0, phase_ = 0.0;
    int cur_ = 0;
    float fade_ = 1.0f;
    Delay2 lat_;
    OS os_[2];
    std::array<std::array<hc::fx::Biquad, 6>, 2> f_{};
    hc::fx::Biquad rumbleF_[2];
    std::array<float, kKey> key_{};
    hc::fx::Smoothed inGain_, sm_[kSm];
    float dc_[2]{};
    // modulation
    hc::fx::DelayLine md_[2], th_[2];
    hideout::PitchShift ps_[2], psBass_[2];
    std::array<std::array<Ap1, 4>, 2> ap_{};
    float fbL_ = 0.0f, fbR_ = 0.0f;
    Divider div_[1];
    Svf svf_[2];
    float wahEnv_ = 0.0f;
    // space
    hc::fx::DelayLine echo_[2], flip_[2], pre_[2];
    float lpE_[2]{}, lastWet_[2]{};
    int flipK_ = 0, flipN_ = 0;
    hideout::Tail tail_;
    hideout::Early early_;
    std::array<std::array<SpringLine, 3>, 2> spring_;
    // dynamics
    float env_ = 0.0f, gr_ = 0.0f;
    GateStage gate_;
};

} // namespace

PedalFace pedalFace(int pedal) noexcept { return kFaces[std::clamp(pedal, 0, kPedals - 1)]; }

} // namespace racket

std::unique_ptr<hc::fx::Effect> makeRacket() { return std::make_unique<racket::Impl>(); }

} // namespace hem::rack
