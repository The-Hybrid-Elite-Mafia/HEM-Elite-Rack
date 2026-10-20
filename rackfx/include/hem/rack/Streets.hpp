#pragma once
// =============================================================================
//  The HEM streets: guitar, pitch, meters and test tones (our own designs, our own names).
//
//  TORCH         guitar amp with two cabinets: AMP on / off, TYPE (ten amps of our own, from DIRECT and CLEAN up
//                to HOT, LEAD and MODERN), DRIVE, BASS, MID, TREBLE, PRESENCE and MASTER (all 0..10, 5 = middle),
//                INPUT and OUTPUT ±12 dB; two cabinet paths A and B, each its CABINET (NONE or one of eight), MIC
//                (dynamic, condenser or ribbon, on or off axis), VOLUME 0..10 (0 = off) and PAN; B has a PHASE
//                flip; ROUTING DUAL (both paths hear the amp) or STEREO (left through A, right through B).
//                4x oversampled; latency 23.
//  THE RACKET    the pedalboard, one pedal at a time: PEDAL picks one of 26 (stack several RACKETs for a chain),
//                and the plate shows that pedal's own controls. INPUT STEREO / MONO / MONO > WIDE (summed to mono
//                in, stereo pedals spread it out) and INPUT GAIN ±18 dB are the board's. The pedals:
//                DRIVE    OVERDRIVE (drive, level, tone), HARD CLIP (drive, level, body = the mid scoop, tone
//                         700 Hz..10 kHz), FUZZ (sustain, level, tone), RASP (drive, tone = a resonant low-pass,
//                         level; a resonant high-pass sits under it), HEAVY (drive, level, low ±: a bell at 120 Hz,
//                         high: a shelf -30..0, mid freq 250..1500 Hz and mid gain -36..+12)
//                MOVEMENT FLANGE (depth, rate or sync note, delay 1.5..10 ms, feedback, stereo), VIB / TREM (wave,
//                         rate or sync note, vibrato, tremolo), AUTO PAN (wave, rate or note, width), PHASE (depth,
//                         rate or note, stereo), OCT DOWN (direct, one and two octaves down, each with its own pan),
//                         WAH (sensitivity, speed, range, AUTO or HAND with its position), CHORUS (depth, rate),
//                         THICKEN (mix, detune 1..25 cents, delay 12..100 ms, feedback, stereo), PITCH RIDE and
//                         BASS RIDE (mix, low and high shift ±12 semitones, and where between them it sits)
//                SPACE    ECHO (mix, time 0.01..2 s or sync note, feedback 0..10 (past 8.7 it builds), high cut,
//                         stereo = how far the echoes bounce side to side), FLIP ECHO (mix, time 0.01..1 s or note,
//                         pitch ±1200 cents, feedback, stereo, REVERSE), ROOM (mix, pre-delay 11..160 ms, time
//                         0.2..10 s, tone = how fast the highs die), SPRING (mix, time, pre-delay 20..170 ms)
//                BASICS   SQUEEZE (amount, attack, release), SLAM (squeeze, attack, output), GATE (threshold,
//                         attack, release, hold), CLEAN UP (gate threshold, squeeze), TONE STACK (low / mid / high
//                         ±18 at 310 Hz / 1 kHz / 4 kHz, low cut 16..310 Hz, high cut 4..18 kHz), GRAPHIC (125 Hz ..
//                         4 kHz ±12, narrow bands, a fixed rumble filter at 40 Hz), VOLUME (the pedal, LIN or LOG,
//                         its lowest and highest level).
//                Switching pedals fades out and in (10 ms). 4x oversampled drives; latency 23 for every pedal.
//  THE SNITCH    tuner: MODE CHROMATIC (any note) / GUITAR / DROP D / OPEN G / OPEN D / OPEN E / DADGAD /
//                HALF DOWN / MY NOTES (only the notes switched on below); REFERENCE A ±6 cents around 440 Hz;
//                the twelve note switches C .. B (used by MY NOTES; pick one for a single note). It tunes to the
//                switched-on note nearest the sound and shows that NOTE, how many CENTS off (±50) and the
//                pitch. The sound passes untouched. Meters: 0 note (MIDI number, -1 = none), 1 cents, 2 Hz,
//                3 how it stands: -1 flat (3 cents or more under), 0 in tune (within 6), +1 sharp.
//  PROMOTION     pitch shifter (the length stays): SET BY TRANSPOSE (SEMITONES -12..+12 and CENTS 0..99 up from
//                there; the semitone steps are also named as intervals) or FREQUENCY (FROM Hz -> TO Hz: the ratio
//                between them), up to an octave either way; MODE SYNC / SMOOTH / TRANSIENT / PUNCHY. The original's
//                time stretching works on a whole file, not on a live stream, so it isn't here. Latency 1681 samples.
//  WISEGUYS      voice harmonizer: six added voices, each ON, PITCH (-12..+12 semitones, cents allowed), FORMANT
//                (-12..+12 semitones), GAIN (0..-24 dB), PAN (±45 degrees) and DELAY (0..100 ms); the DIRECT voice
//                (the original) ON, GAIN and DELAY; ANIMATE 0..100 ms (each voice drifts that far in time, so they
//                don't land together), LINK (FREE / VOICE / SONG / BRASS / WOODS / STRINGS / LINEAR / INVERTED: how
//                far each voice's formant follows its pitch, our own amounts), MODE (how the pitch is read: DEFAULT,
//                VOICE STEADY, VOICE JUMPY, OPERA, MUSIC STEADY, CLEAR TONES, BASS), MIN PITCH and MAX PITCH.
//                PSOLA voices (formants kept unless moved); latency 4096 samples.
//  MADE MAN      pitch correction for one voice: ROOT and SCALE (twenty of ours, or MY NOTES: the twelve switches
//                say which notes are allowed), SPEED (how fast the voice is pulled onto the note: 0 = hard), NOTE
//                TRANSITION (how fast the target moves from one note to the next), RATIO (how much of the way: 100 =
//                all), FORMANT CORRECTED (the voice's character stays) / OFF, SHIFT -12..+12 semitones (moves it all,
//                for octave drops), REFERENCE A 400..480 Hz, MIN / MAX PITCH (the range it listens in). PSOLA; latency
//                4096 samples. Meters: 0 the sung note, 1 how far it was pulled (cents).
//  THE DETECTIVE listens and names the song's KEY, two ALTERNATIVES and the song's TUNING (where its A sits).
//                It keeps listening until the same key has held for 30 seconds (or the music stops for 3 s),
//                then stops. DETECT stops it, or starts it over. EDIT shows a key of your own choosing (ROOT,
//                SCALE MAJOR / MINOR) instead. SPELL SHARPS / FLATS. The sound passes untouched. Meters: 0 key
//                (0..11 major, 12..23 minor, +24 = spelled with flats, -1 = none yet), 1 / 2 the alternatives,
//                3 tuning (Hz of A), 4 state (0 waiting for music, 1 listening, 2 found, 3 nothing found, 4 edited).
//  THE WITNESS   VU meters for left and right: HEADROOM 4..26 dB (0 VU = a sine that far under full scale),
//                DISPLAY DELAY 0..1000 ms (holds the needles back to line up with buses that come out later), and
//                a clip light per side (a sample at -0.009 dBFS or more) that CLEAR puts out. The sound passes
//                untouched. Meters: 0 / 1 VU left / right, 2 / 3 clip left / right (1 = lit).
//  LIVE WIRE     signal generator: ON (off by default: then the sound passes untouched; on: only the signal comes
//                out), TYPE PINK / WHITE (20 Hz .. 21 kHz) / SINE, FREQ 20 Hz .. 21 kHz (SINE) with QUICK FREQ (the
//                knob, or 100 Hz / 1 kHz / 10 kHz), GAIN 0..-120 dB with QUICK GAIN (the knob, or -6 / -12 / -20),
//                SEND L / R / L+R, FLIP (the right side upside down). A sine at GAIN peaks there; the noises have
//                the same RMS as that sine.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace torch {
enum P : int { AmpOn, Type, Drive, Bass, Mid, Treble, Presence, Master, Input, Output,
               CabA, MicA, VolA, PanA, CabB, MicB, VolB, PanB, PhaseB, Routing, kCount };
enum Amp : int { Direct, Clean, Sparkle, Edge, DriveAmp, Crunch, Brit, Hot, Lead, Modern, kAmps };
inline constexpr int kCabs = 9;   // NONE + eight
} // namespace torch

namespace racket {
enum Pedal : int { PdOverdrive, PdHardClip, PdFuzz, PdRasp, PdHeavy,
                   PdFlange, PdVibTrem, PdAutoPan, PdPhase, PdOctDown, PdWah, PdChorus, PdThicken, PdPitchRide, PdBassRide,
                   PdEcho, PdFlipEcho, PdRoom, PdSpring,
                   PdSqueeze, PdSlam, PdGate, PdCleanUp, PdToneStack, PdGraphic, PdVolume, kPedals };
enum P : int {
    Which, Input, InputGain,
    OdDrive, OdLevel, OdTone,
    DsDrive, DsLevel, DsBody, DsTone,
    FzSustain, FzLevel, FzTone,
    RpDrive, RpTone, RpLevel,
    HvDrive, HvLevel, HvLow, HvHigh, HvMidFreq, HvMidGain,
    FlDepth, FlSync, FlNote, FlRate, FlDelay, FlFeedback, FlStereo,
    VtWave, VtSync, VtNote, VtRate, VtVibrato, VtTremolo,
    PnWave, PnSync, PnNote, PnRate, PnWidth,
    PhDepth, PhSync, PhNote, PhRate, PhStereo,
    OcDirect, OcOne, OcOnePan, OcTwo, OcTwoPan,
    WhSens, WhSpeed, WhRange, WhMode, WhPos,
    ChDepth, ChRate,
    ThMix, ThDetune, ThDelay, ThFeedback, ThStereo,
    PrMix, PrLow, PrHigh, PrPos,
    BrMix, BrLow, BrHigh, BrPos,
    EcMix, EcSync, EcNote, EcTime, EcFeedback, EcHighCut, EcStereo,
    FeMix, FeSync, FeNote, FeTime, FePitch, FeFeedback, FeStereo, FeReverse,
    RmMix, RmPre, RmTime, RmTone,
    SpMix, SpTime, SpPre,
    SqAmount, SqAttack, SqRelease,
    SlSqueeze, SlAttack, SlOutput,
    GtThreshold, GtAttack, GtRelease, GtHold,
    CuGate, CuSqueeze,
    TsLow, TsMid, TsHigh, TsLowCut, TsHighCut,
    Eq125, Eq250, Eq500, Eq1k, Eq2k, Eq4k,
    VlVolume, VlScale, VlMin, VlMax,
    kCount };
enum InputMode : int { InStereo, InMono, InMonoWide };
// each pedal's first parameter (the next pedal's first is where it ends)
inline constexpr int kFirst[kPedals + 1] = {OdDrive, DsDrive, FzSustain, RpDrive, HvDrive, FlDepth, VtWave, PnWave, PhDepth, OcDirect,
                                            WhSens, ChDepth, ThMix, PrMix, BrMix, EcMix, FeMix, RmMix, SpMix, SqAmount, SlSqueeze,
                                            GtThreshold, CuGate, TsLow, Eq125, VlVolume, kCount};
// the pedal a parameter belongs to (-1: the board's own)
[[nodiscard]] constexpr int pedalOf(int param) noexcept {
    if (param < kFirst[0]) return -1;
    for (int k = 0; k < kPedals; ++k)
        if (param < kFirst[k + 1]) return k;
    return -1;
}
// What the plate shows for a pedal: its knobs (up to six) and its switches (up to three: an ON / OFF
// parameter is a switch, a choice a stepper), each with its caption.
struct PedalCtl { int param; const char* caption; };
struct PedalFace { const PedalCtl* knobs; int knobCount; const PedalCtl* switches; int switchCount; };
inline constexpr int kMaxKnobs = 6, kMaxSwitches = 3;
[[nodiscard]] PedalFace pedalFace(int pedal) noexcept;
} // namespace racket

namespace snitch {
enum P : int { Mode, Reference, Note0, kCount = Note0 + 12 };
enum Tuning : int { Chromatic, Guitar, DropD, OpenG, OpenD, OpenE, Dadgad, HalfDown, MyNotes, kTunings };
} // namespace snitch

namespace promotion {
enum P : int { Entry, Semitones, Cents, FromHz, ToHz, Mode, kCount };
enum Modes : int { Sync, Smooth, Transient, Punchy, kModes };
} // namespace promotion

namespace wiseguys {
inline constexpr int kVoices = 6, kStride = 6;
enum V : int { On, Pitch, Formant, Gain, Pan, Delay };   // per voice: voice(v, ...) = v * kStride + ...
enum P : int { DirectOn = kVoices * kStride, DirectGain, DirectDelay, Animate, Link, Mode, MinPitch, MaxPitch, kCount };
constexpr int voice(int v, V k) noexcept { return v * kStride + k; }
inline constexpr std::uint32_t kLatency = 4096;
} // namespace wiseguys

namespace mademan {
enum P : int { Key, Scale, Speed, Transition, Ratio, Formant, Shift, Reference, MinPitch, MaxPitch, Note0, kCount = Note0 + 12 };
inline constexpr int kScales = 21;   // twenty scales and MY NOTES (the last)
inline constexpr std::uint32_t kLatency = 4096;
} // namespace mademan

namespace detective {
enum P : int { Detect, Spell, Edit, Root, Scale, kCount };
enum State : int { Waiting, Listening, Found, Nothing, Edited };
} // namespace detective

namespace witness {
enum P : int { Headroom, DisplayDelay, Clear, kCount };
} // namespace witness

namespace livewire {
enum P : int { On, Type, Freq, QuickFreq, Gain, QuickGain, Send, Flip, kCount };
enum Signal : int { Pink, White, Sine };
} // namespace livewire

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTorch();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeRacket();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSnitch();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePromotion();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeWiseguys();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMadeMan();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDetective();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeWitness();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLiveWire();

} // namespace hem::rack
