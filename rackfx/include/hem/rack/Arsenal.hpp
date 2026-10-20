#pragma once
// =============================================================================
//  The HEM dynamics arsenal (our own designs, our own names). Each one publishes its gain
//  reduction (dB, <= 0) where it has one, and its own meters where its plate shows them.
//
//  SWITCHBLADE  channel compressor + gate + a filter that either steers what the detector hears
//               or limits the processing to one band. Compressor: THRESHOLD, RATIO, ATTACK,
//               RELEASE (or AUTO: quick after a short hit, slower in long loud parts), MAKEUP.
//               Gate: OPEN level, CLOSE (how far under OPEN it shuts: no chatter), RANGE (how far
//               it closes), HOLD, RELEASE. Filter: HIGH-PASS / LOW-PASS / BAND-PASS / BAND-CUT,
//               FREQ, Q, used as KEY (detector only) or BAND (only that band is processed).
//               LOOKAHEAD (2 ms), LISTEN (hear the detector), OUTPUT. Latency 96 samples, always.
//               Meters: 0 compressor gain change, 1 gate gain change, 2 input level.
//  TERRITORY    4- or 5-band dynamics: per band THRESHOLD, RANGE (minus = turns down, plus =
//               lifts up), GAIN; crossovers; ATTACK, RELEASE, AUTO release, KNEE, TAIL (QUICK or
//               LINGER near the end of a release), PHASE NATURAL (zero latency) or LINEAR (the bands
//               are split in the frequency domain: no phase shift, 2048 samples latency — hosts
//               get the larger latency always), LISTEN a band, OUTPUT. Meters 0..4 = band gain change.
//  CONSIGLIERE  a two-knob leveler: PEAK REDUCTION (how much), GAIN (makeup). MODEL OPTO (light
//               cell: soft, slow two-stage release) or SOLID (faster, firmer, a touch of grit).
//               COMPRESS or LIMIT, EMPHASIS (detector listens harder to the highs), MIX.
//  HEAVYWEIGHT  a punchy modern compressor with an analog stage: THRESHOLD, RATIO 1..50, ATTACK,
//               RELEASE (ms, or synced to a note of the song's tempo), PUNCH (lets the hits through
//               while the body is held), ANALOG (4x oversampled saturation), LIMIT (a soft ceiling at
//               -0.3 dBFS), MIX, OUTPUT. Auto makeup. Latency 23 samples, always.
//  THE DON      an old-school broadcast bus compressor: the detector listens to the OUTPUT
//               (feedback), so it breathes with the music. THRESHOLD, RATIO steps, ATTACK steps,
//               DECAY steps (or AUTO), tube-style output stage (WARMTH), MIX, OUTPUT.
//  SLOW BURN    a variable-mu tube limiter: INPUT, THRESHOLD, TIME 1..6 (5 and 6 let go by
//               themselves, slower after long passages), LINK (LINKED, DUAL, MID / SIDE), TUBE,
//               OUTPUT, MIX. Latency 23 samples.
//  IRON FIST    a console channel compressor driven by INPUT (no threshold: more input, more
//               compression), RATIO steps, ATTACK FAST / SLOW, RECOVERY steps or AUTO, a LIMITER
//               (LEVEL), DE-ESS (the compressor's ear on the highs), IRON (output transformer, 4x
//               oversampled), OUTPUT. Latency 23 samples.
//  KNOCKOUT     transient designer: ATTACK (boost / cut the hits), SENSITIVITY, LENGTH, SHAPE; the
//               same for SUSTAIN; WIDE or 4-BAND (each band's amount); GUARD OFF / CLIP / LIMIT; MIX,
//               OUTPUT. Zero latency.
//  FULL HOUSE   loudness without squash: GATE (quiet noise stays down), LOW LEVEL lifts the quiet
//               parts (THRESHOLD, MAX LIFT), LEVELER rides the average (AMOUNT, SPEED), HIGH LEVEL
//               holds the loud parts (with automatic gain), OUTPUT. Meters: 0 lift, 1 leveler,
//               2 hold (all dB).
//  WHEELMAN     an automatic level rider: keeps a VOCAL or BASS at the TARGET level, within MAX
//               BOOST / MAX CUT, ignoring anything under SENSITIVITY (it holds still in the gaps),
//               SPEED SLOW / FAST, OUTPUT. Meters: 0 ride (dB), 1 level (dB). Zero latency.
//  BIG SHOT     (and the other maximizers): see Maximizers.hpp.
//               Meters: 0 loudness (LUFS, 3 s), 1 output peak (dBFS).
//  SILENCER     de-esser: DETECT CLASSIC (a filter at FREQUENCY, HIGH-PASS or BAND-PASS) or SMART
//               (how much of the sound sits above FREQUENCY, so loud and quiet esses are caught the
//               same; WIDTH = how strict), THRESHOLD, RANGE (most it takes off), SPLIT (only the
//               highs go down) or WIDE, LOOKAHEAD, LISTEN. Latency 240 samples, always.
//  BREATHLESS   breath remover: SENSITIVITY (how breath-like a sound must be), LEVEL GATE (breaths
//               are quieter than this), REDUCTION, FADE OUT / FADE IN, LISTEN VOICE / BREATHS.
//               Latency 480 samples. Meters: 0 breath score (%), 1 level (dB), 2 reduction (dB).
//  TOMMY GUN    one-knob pump: AMOUNT (how deep and how hard), RATE (a note of the tempo),
//               OFFSET (where in the beat it dips), SYNC HOST or BPM (the BPM knob). Zero latency.
// =============================================================================
#include "hem/rack/Maximizers.hpp"
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace switchblade {
enum P : int { Threshold, Ratio, Attack, Release, AutoRelease, Makeup,
               GateOpen, GateClose, GateRange, GateHold, GateRelease,
               FilterType, FilterUse, FilterFreq, FilterQ, Lookahead, Listen, Output, kCount };
inline constexpr std::uint32_t kLookahead = 96;   // 2 ms at 48 kHz
} // namespace switchblade

namespace territory {
inline constexpr int kBands = 5;
enum P : int { X1, X2, X3, X4,
               Thr1, Range1, Gain1, Thr2, Range2, Gain2, Thr3, Range3, Gain3, Thr4, Range4, Gain4, Thr5, Range5, Gain5,
               Attack, Release, AutoRelease, Knee, Tail, Phase, Bands, Listen, Output, kCount };
inline constexpr int thr(int b) noexcept { return Thr1 + 3 * b; }
inline constexpr int range(int b) noexcept { return Range1 + 3 * b; }
inline constexpr int gain(int b) noexcept { return Gain1 + 3 * b; }
inline constexpr std::uint32_t kFftSize = 2048, kHop = 512;
inline constexpr std::uint32_t kLatency = kFftSize;   // reported always (LINEAR needs it; NATURAL is delayed to match)
} // namespace territory

namespace consigliere {
enum P : int { PeakReduction, Gain, Model, Mode, Emphasis, Mix, kCount };
} // namespace consigliere

namespace heavyweight {
enum P : int { Threshold, Ratio, Attack, Release, Sync, Punch, Analog, Limit, Mix, Output, Bpm, SyncSource, kCount };
inline constexpr float kNoteBeats[] = {0, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f};   // OFF, 1/32, 1/16, 1/8, 1/4, 1/2
} // namespace heavyweight

namespace thedon {
enum P : int { Threshold, Ratio, Attack, Decay, Warmth, Mix, Output, kCount };
inline constexpr float kRatios[] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 20.0f};
inline constexpr float kAttackMs[] = {1.0f, 5.0f, 20.0f};
inline constexpr float kDecayMs[] = {100, 250, 500, 1000, 2000};   // 5 = AUTO
} // namespace thedon

namespace slowburn {
enum P : int { Input, Threshold, Time, Link, Tube, Output, Mix, kCount };
inline constexpr float kAttackMs[6] = {0.2f, 0.2f, 4.0f, 0.4f, 0.4f, 0.2f};
inline constexpr float kReleaseS[6] = {0.3f, 0.8f, 2.0f, 5.0f, 2.0f, 0.8f};
inline constexpr float kReleaseLongS[6] = {0.3f, 0.8f, 2.0f, 5.0f, 10.0f, 25.0f};
[[nodiscard]] float gainComputerDb(float levelDb, float thresholdDb) noexcept;   // ratio grows with level
} // namespace slowburn

namespace ironfist {
enum P : int { Input, Ratio, Attack, Recovery, Limit, LimitLevel, DeEss, Iron, Output, kCount };
inline constexpr float kRatios[] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
inline constexpr float kRecoveryMs[] = {100, 400, 800, 1500};   // 4 = AUTO
inline constexpr float kThresholdDb = -20.0f;   // fixed: INPUT pushes the signal into it
} // namespace ironfist

namespace knockout {
enum P : int { Mode, Attack, AttackSens, AttackTime, AttackShape, Sustain, SustainSens, SustainTime, SustainShape,
               Band1, Band2, Band3, Band4, Guard, Mix, Output, kCount };
inline constexpr float kCrossHz[3] = {150.0f, 1200.0f, 5000.0f};
} // namespace knockout

namespace fullhouse {
enum P : int { Gate, LowThreshold, LowLift, Leveler, LevelSpeed, HighLevel, Output, kCount };
} // namespace fullhouse

namespace wheelman {
enum P : int { Source, Target, Sensitivity, MaxBoost, MaxCut, Speed, Output, kCount };
} // namespace wheelman

namespace silencer {
enum P : int { Detect, Freq, Filter, Width, Threshold, Range, Split, Lookahead, Listen, kCount };
inline constexpr std::uint32_t kLookahead = 240;   // 5 ms at 48 kHz
} // namespace silencer

namespace breathless {
enum P : int { Sensitivity, LevelGate, Reduction, FadeOut, FadeIn, Listen, kCount };
inline constexpr std::uint32_t kLookahead = 480;   // 10 ms at 48 kHz
} // namespace breathless

namespace tommygun {
enum P : int { Amount, Rate, Offset, SyncSource, Bpm, kCount };
inline constexpr float kRateBeats[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};   // 1/16, 1/8, 1/4, 1/2, 1 BAR
} // namespace tommygun

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSwitchblade();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTerritory();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeConsigliere();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHeavyweight();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTheDon();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSlowBurn();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeIronFist();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeKnockout();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFullHouse();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeWheelman();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSilencer();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBreathless();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTommyGun();

} // namespace hem::rack
