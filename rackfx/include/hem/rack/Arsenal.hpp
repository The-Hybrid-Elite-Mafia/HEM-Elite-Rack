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
//  CONSIGLIERE  light-cell (opto) leveler: GAIN 0..100, PEAK REDUCTION 0..100, AUTO MAKEUP, COMPRESS /
//               LIMIT, HI FREQ (how much harder the detector listens to the highs; 50 = +6 dB above
//               1.5 kHz), ANALOG OFF / 50 / 60 HZ (the hardware's hum, riding under the music), MIX, TRIM,
//               METER IN / GR / OUT (the needle; IN and OUT read VU, 0 VU = -18 dBFS RMS).
//               Release in two stages: half in ~60 ms, half in 0.5..5 s (longer after long hard work).
//               Meters: 0 / 1 input / output VU.
//  STONE COLD   the solid-state light-cell leveler: GAIN 0..10, PEAK REDUCTION 0..10, the rest as
//               CONSIGLIERE. Quicker (1.5 ms attack, 40 ms then 0.3..1.5 s) with a touch of grit.
//  HEAVYWEIGHT  punchy modern compressor: THRESHOLD, RATIO 1..50, ATTACK 0.5..100 ms, PUNCH 0..30 (lets the
//               hits through while the body is held), RELEASE 3 ms..3 s, or RELEASE IN HOST / BPM tempo
//               with a NOTE (1/64 .. 2 BARS) and BPM 30..300, ANALOG OFF / 1 (tube and transformer) /
//               2 (transistor) / 3, 4 (driven; 4 without hiss), LIMITER on (rounds into -0.3 dBFS) or off
//               (a clipper at 0 dBFS), MIX, OUTPUT, METER IN / GR / OUT. Auto makeup. 4x oversampled,
//               latency 23 samples. Meters: 0 / 1 input / output VU.
//  THE DON      old-school broadcast bus compressor; the detector listens to the OUTPUT (feedback), so
//               it breathes with the music. THRESHOLD -24..+16 dB in 2 dB steps (around 0 VU), RATIO 1:1 / 2 /
//               3 / 5 / LIMIT, DECAY 1..32 (x 100 ms), OUTPUT, METER IN / GR / OUT, ANALOG OFF / 50 / 60 HZ,
//               VU CALIBRATE 8..24 dB (0 VU = that far under full scale). Meters: 0 / 1 input / output VU.
//  SLOW BURN    stereo variable-mu tube limiter: INPUT -20..0 (-14 = unity), THRESHOLD 0..10 (0 = none),
//               TIME CONSTANT 1..6 (5 and 6 let go by themselves, slower after long passages), LINK
//               SEPARATE / LINKED / MID / SIDE, MAINS OFF / 50 / 60 HZ, COMPRESS ON / OFF (off: the tubes
//               only), OUTPUT. 4x oversampled tubes, latency 23 samples.
//  LONE WOLF    one channel of the same circuit (the same on both sides; no LINK).
//  IRON FIST    console channel compressor and limiter, driven by INPUT -24..+18 (its threshold is fixed
//               at 0 dBu = -22 dBFS: more input, more compression), OUTPUT, METER IN / GR / OUT, ANALOG
//               OFF / 25 / 50 / 100 % (output iron and hum), COMPRESS, RATIO 1.5..6:1, RELEASE 400 MS /
//               800 MS / 1.5 S / AUTO, DE-ESS (the compressor hears only the highs), LIMITER, LIMIT LEVEL
//               +4..+12 dBu, LIMIT ATTACK 1..1000 us, LIMIT RELEASE 100 / 200 / 800 MS / AUTO, AUTO MAKEUP
//               (OUTPUT follows INPUT the other way), MIX, TRIM. 4x oversampled iron, latency 23 samples.
//  QUICK DRAW   fast transistor limiting amp: INPUT (-30 = unity into it), OUTPUT (-18 = unity),
//               ATTACK 1..7 and RELEASE 1..7 (7 = fastest), RATIO 4 / 8 / 12 / 20 / ALL, COMPRESS, REVISION
//               EARLY / LATE (EARLY colours more), AUTO MAKEUP, ANALOG OFF / 50 / 60 HZ, METER, MIX, TRIM.
//               Latency 23 samples. Meters: 0 / 1 input / output VU.
//  OLD MONEY    smooth all-purpose compressor: THRESHOLD, RATIO 0.5..50 (under 1 lifts the loud parts),
//               ATTACK, RELEASE, RELEASE AUTO / MANUAL, BEHAVIOR ELECTRO / OPTO, CHARACTER WARM / SMOOTH,
//               OUTPUT (a soft limiter keeps it under 0 dBFS), MIX, TRIM. Meters: 0 / 1 input / output
//               peak (dBFS), 2 the output limiter (dB).
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

namespace opto {   // CONSIGLIERE and STONE COLD
enum P : int { Gain, PeakReduction, AutoMakeup, Mode, HiFreq, Analog, Mix, Trim, Meter, kCount };
} // namespace opto
namespace consigliere = opto;
namespace stonecold = opto;

namespace heavyweight {
enum P : int { Threshold, Ratio, Attack, Punch, Release, Sync, Note, Bpm, Analog, Limiter, Mix, Output, Meter, kCount };
// NOTE: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, BAR, 2 BARS (in beats)
inline constexpr float kNoteBeats[] = {0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
} // namespace heavyweight

namespace thedon {
enum P : int { Threshold, Ratio, Decay, Output, Meter, Analog, VuCal, kCount };
inline constexpr float kRatios[] = {1.0f, 2.0f, 3.0f, 5.0f, 30.0f};    // 1:1, 2, 3, 5, LIMIT
inline constexpr float kDecayMs[] = {100, 200, 400, 800, 1600, 3200};  // the switch's 1 .. 32, x 100 ms
} // namespace thedon

namespace varimu {   // SLOW BURN (stereo) and LONE WOLF (one channel)
// TIME 1..6: attack (ms); release (s) for a single peak, and for long loud passages (5 and 6 let go by themselves)
inline constexpr float kAttackMs[6] = {0.2f, 0.2f, 0.4f, 0.4f, 0.4f, 0.2f};
inline constexpr float kReleaseS[6] = {0.3f, 0.8f, 2.0f, 5.0f, 2.0f, 0.8f};
inline constexpr float kReleaseLongS[6] = {0.3f, 0.8f, 2.0f, 5.0f, 10.0f, 25.0f};
float gainComputerDb(float levelDb, float thresholdDb) noexcept;
} // namespace varimu
namespace slowburn {
enum P : int { Input, Threshold, Time, Link, Mains, Compress, Output, kCount };
} // namespace slowburn
namespace lonewolf {
enum P : int { Input, Threshold, Time, Mains, Compress, Output, kCount };
} // namespace lonewolf

namespace ironfist {
enum P : int { Input, Output, Meter, Analog, Compress, Ratio, Release, DeEss, Limiter, LimitLevel, LimitAttack, LimitRelease,
               AutoMakeup, Mix, Trim, kCount };
inline constexpr float kRatios[] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
inline constexpr float kReleaseMs[] = {400, 800, 1500};       // 3 = AUTO
inline constexpr float kLimitReleaseMs[] = {100, 200, 800};   // 3 = AUTO
inline constexpr float kThresholdDb = -22.0f;   // fixed (0 dBu): INPUT pushes the signal into it
} // namespace ironfist

namespace quickdraw {
enum P : int { Input, Output, Attack, Release, Ratio, Compress, Revision, AutoMakeup, Analog, Meter, Mix, Trim, kCount };
} // namespace quickdraw

namespace oldmoney {
enum P : int { Threshold, Ratio, Attack, Release, ReleaseMode, Behavior, Character, Output, Mix, Trim, kCount };
} // namespace oldmoney

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
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStoneCold();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHeavyweight();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTheDon();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSlowBurn();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLoneWolf();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeQuickDraw();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeOldMoney();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeIronFist();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeKnockout();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFullHouse();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeWheelman();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSilencer();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBreathless();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTommyGun();

} // namespace hem::rack
