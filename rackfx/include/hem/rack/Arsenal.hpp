#pragma once
// =============================================================================
//  The HEM dynamics arsenal (our own designs, our own names). Each one publishes its gain
//  reduction (dB, <= 0) where it has one, and its own meters where its plate shows them.
//
//  SWITCHBLADE  the channel compander: a compressor / expander, a gate / expander and a filter, each
//               module able to work on the whole sound (WIDE), hear only the filter (SIDECHAIN) or work
//               only on the filter's band (SPLIT; the rest passes untouched: a dynamic EQ).
//               Compressor: ON, LEVEL REF LOW (the classic: lower threshold = quieter, use MAKEUP) or
//               PEAK (full-scale peaks stay where they are), MAKEUP -40..+40, THRESHOLD -100..0,
//               RATIO 0.5..50 (under 1: upward expansion), CANCEL (the ratio turns negative, -5..-50:
//               the louder it gets, the quieter it comes out), ATTACK 0.01..1000 ms, RELEASE 1..10000 ms,
//               SMART REL 0..1000 ms (peaks shorter than this let go at SMART REL instead: program-dependent),
//               EQ WIDE / SIDECHAIN / SPLIT. Gate: ON, MODE GATE (opens at OPEN, shuts under CLOSE,
//               HOLD, down to FLOOR -80 (= off) .. +12 dB) or EXPANDER (below OPEN at SLOPE 2..10:1,
//               no deeper than FLOOR; a FLOOR over 0 lifts quiet parts instead), ATTACK, RELEASE, EQ.
//               LINK: the gate uses the compressor's attack, release and EQ mode. Filter: BAND / NOTCH /
//               LO CUT / HI CUT, FREQ 16..21000 Hz, Q 0.1..0.6. LOOKAHEAD (7 ms), KEY STEREO / L>R (the
//               left side's level works the right side; the left passes) / R>L, MONITOR AUDIO /
//               SIDECHAIN (what the detector hears) / PASSIVE (the band left alone), INPUT LEFT / RIGHT
//               0..-24, OUTPUT +18..-40. Latency 336 samples, always.
//               Meters: 0 compressor gain change, 1 gate gain change, 2 input level.
//  TERRITORY    4-band dynamics: per band THRESHOLD, GAIN, RANGE (the most it moves: minus = down, plus =
//               up; centred on the threshold), ATTACK, RELEASE, SOLO, BYPASS; MASTER THRESHOLD / GAIN /
//               RANGE (added to every band) and ATTACK / RELEASE (x every band); three crossovers and their
//               Q (0.10..0.75: how steep), RELEASE AUTO / MANUAL, BEHAVIOR EASE OUT / SNAP BACK, KNEE 0 (soft)
//               .. 100 (hard), OUTPUT. Zero latency. Meters 0..3 band moves (dB), 4 output peak (dBFS).
//  STRAIGHT EDGE 5-band linear-phase dynamics: the same band and MASTER controls, four crossovers,
//               MAKEUP MANUAL / AUTO (each band gets half its RANGE back), ADAPTIVE (-40 = off .. +12: a
//               band only works when it stands out over the band under it), RELEASE, BEHAVIOR, KNEE,
//               OUTPUT, TRIM (a push: the output comes down by the loudest peak since the last push, so
//               that peak would land on 0 dBFS), DITHER (24-bit). Latency 2559 samples.
//               Meters 0..4 band moves (dB), 5 output peak (dBFS).
//  CONSIGLIERE  light-cell (opto) leveler: GAIN 0..100, SQUEEZE 0..100, AUTO MAKEUP, COMPRESS /
//               LIMIT, BRIGHT DETECT (how much harder the detector listens to the highs; 50 = +6 dB above
//               1.5 kHz), ANALOG OFF / 50 / 60 HZ (the hardware's hum, riding under the music), MIX, TRIM,
//               METER IN / GR / OUT (the needle; IN and OUT read VU, 0 VU = -18 dBFS RMS).
//               Release in two stages: half in ~60 ms, half in 0.5..5 s (longer after long hard work).
//               Meters: 0 / 1 input / output VU.
//  STONE COLD   the solid-state light-cell leveler: GAIN 0..10, SQUEEZE 0..10, the rest as
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
//               VU ZERO 8..24 dB (0 VU = that far under full scale). Meters: 0 / 1 input / output VU.
//  SLOW BURN    stereo variable-mu tube limiter: INPUT -20..0 (-14 = unity), THRESHOLD 0..10 (0 = none),
//               TIMING 1..6 (5 and 6 let go by themselves, slower after long passages), LINK
//               SEPARATE / LINKED / MID / SIDE, HUM OFF / 50 / 60 HZ, COMPRESS ON / OFF (off: the tubes
//               only), OUTPUT. 4x oversampled tubes, latency 23 samples.
//  LONE WOLF    one channel of the same circuit (the same on both sides; no LINK).
//  IRON FIST    console channel compressor and limiter, driven by INPUT -24..+18 (its threshold is fixed
//               at 0 dBu = -22 dBFS: more input, more compression), OUTPUT, METER IN / GR / OUT, ANALOG
//               OFF / 25 / 50 / 100 % (output iron and hum), COMPRESS, RATIO 1.5..6:1, RELEASE 400 MS /
//               800 MS / 1.5 S / AUTO, DE-ESS (the compressor hears only the highs), LIMITER, LIMIT AT
//               +4..+12 dBu, LIMIT ATTACK 1..1000 us, LIMIT RELEASE 100 / 200 / 800 MS / AUTO, AUTO MAKEUP
//               (OUTPUT follows INPUT the other way), MIX, TRIM. 4x oversampled iron, latency 23 samples.
//  QUICK DRAW   fast transistor limiting amp: INPUT (-30 = unity into it), OUTPUT (-18 = unity),
//               ATTACK 1..7 and RELEASE 1..7 (7 = fastest), RATIO 4 / 8 / 12 / 20 / ALL, COMPRESS, REVISION
//               EARLY / LATE (EARLY colours more), AUTO MAKEUP, ANALOG OFF / 50 / 60 HZ, METER, MIX, TRIM.
//               Latency 23 samples. Meters: 0 / 1 input / output VU.
//  OLD MONEY    smooth all-purpose compressor: THRESHOLD, RATIO 0.5..50 (under 1 lifts the loud parts),
//               ATTACK, RELEASE, RELEASE AUTO / MANUAL, BEHAVIOR SNAP BACK / EASE OUT, CHARACTER WARM / SMOOTH,
//               OUTPUT (a soft limiter keeps it under 0 dBFS), MIX, TRIM. Meters: 0 / 1 input / output
//               peak (dBFS), 2 the output limiter (dB).
//  KNOCKOUT     transient designer: ATTACK (boost / cut the hits), SENSITIVITY, LENGTH, SHAPE SHARP / PUNCHY /
//               ROUND; the same for SUSTAIN (shape EVEN / CURVED / SOFT); SAFETY OFF / CLIP / LIMIT; MIX,
//               OUTPUT. Zero latency.
//  HAYMAKER     transient shaper: RANGE -24..+18 dB (how far the hits move), SENS -10..+10, DURATION (how
//               fast a rise counts as a hit), RELEASE, OUTPUT, TRIM (a push). Meters: 0 gain, 1 output peak.
//  FOUR KINGS   the same in four bands (150 Hz / 1.2 kHz / 5 kHz): RANGE, SENS, ON per band, MASTER RANGE /
//               SENS, DURATION, RELEASE, OUTPUT, TRIM. Meters 0..3 band gains, 4 output peak.
//  BACKBONE     two-fader loudness: LOW LEVEL 0..48 (lifts the quiet parts), HIGH LEVEL 0..-48 (holds the
//               loud parts, made up so full scale stays full scale), OUTPUT. Meters: 0 cut, 1 boost, 2 output.
//  FULL HOUSE   loudness without squash, four sections each with ON: EVENER (slowly brings anything louder
//               than its fader toward it; release LOUD / SOFT), GATE (gentle downward expansion under its
//               threshold), LOW LEVEL (THRESHOLD, GAIN: lifts the quiet parts), HIGH LEVEL (THRESHOLD: holds
//               the loud parts with make-up; GAIN: the most that comes out). Meters: 0 energy, 1 low gain,
//               2 high level, 3 output peak.
//  WHEELMAN     vocal rider: TARGET, VOICE PICKUP (how loud the voice must be to count), ATTACK FAST /
//               SLOW, RANGE MIN / MAX (how far the wheel may turn), IDLE (where it sits with no voice),
//               STAGE + BLEED FLOOR (anything under BLEED FLOOR is other instruments in the mic), OUTPUT.
//               Meters: 0 ride (dB), 1 level (dB). Zero latency.
//  CHAUFFEUR    bass rider: TARGET, NOTE PICKUP, RESPONSE FAST / SLOW, IGNORE BLEED, IGNORE CLICKS (clicks and
//               finger noise), RANGE MIN / MAX, IDLE, OUTPUT. Meters as WHEELMAN. Zero latency.
//  BIG SHOT     (and the other maximizers): see Maximizers.hpp.
//  SILENCER     de-esser: THRESHOLD, FREQUENCY, AUDIO SPLIT (only the highs come down) / WIDE, SIDECHAIN
//               HIGH-PASS / BAND-PASS, MONITOR AUDIO / SIDECHAIN. Meters: 0 energy, 1 most taken off, 2 output.
//  TIGHT LIPS   de-esser: THRESHOLD, RANGE (the most it takes off), FREQUENCY, SIDECHAIN HIGH-PASS / BAND-PASS,
//               MODE SPLIT / WIDE, LISTEN.
//  OMERTA       smart de-esser (hears how much of a sound is highs, at any pitch): THRESHOLD, RANGE,
//               CATCH WIDTH, MODE 0 (wide) .. 100 (split), LOOKAHEAD, MONITOR (hear what's taken out).
//               Latency 240 samples, always.
//  BREATHLESS   breath remover: SENSITIVITY (how breath-like a sound must be), LEVEL GATE (breaths are quieter
//               than this), REDUCTION 0..-80 (-80 = all the way), SOFT END / SOFT START, ROOM FLOOR (a breath only
//               comes down to the room's level), LISTEN VOICE / BREATHS. Latency 480 samples.
//               Meters: 0 breath score (%), 1 level (dB), 2 reduction (dB).
//  TOMMY GUN    one-knob pump: AMOUNT 0..10 (how deep and how hard), RATE (a note of the tempo),
//               OFFSET (where in the beat it dips), SYNC HOST or BPM (the BPM knob). Zero latency.
// =============================================================================
#include "hem/rack/Maximizers.hpp"
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace switchblade {
enum P : int { CompOn, LevelRef, Makeup, Threshold, Ratio, Cancel, Attack, Release, Pdr, CompEq,
               GateOn, GateMode, Floor, ExpSlope, GateOpen, GateClose, GateAttack, GateRelease, GateHold, GateEq,
               Link, FilterType, FilterFreq, FilterQ, Lookahead, Key, Monitor, InputL, InputR, Output, kCount };
inline constexpr std::uint32_t kLookahead = 336;   // 7 ms at 48 kHz; the latency is the same with LOOKAHEAD off
} // namespace switchblade

namespace territory {   // 4 bands: per band THRESHOLD, GAIN, RANGE, ATTACK, RELEASE, SOLO, BYPASS; then MASTER
inline constexpr int kBands = 4, kPerBand = 7;
enum BandP : int { Threshold, Gain, Range, Attack, Release, Solo, Bypass };
inline constexpr int band(int b, int k) noexcept { return b * kPerBand + k; }
enum P : int { MThreshold = kBands * kPerBand, MGain, MRange, MAttack, MRelease, X1, X2, X3, Q, ReleaseMode, Behavior, Knee, Output,
               kCount };
} // namespace territory

namespace straightedge {   // 5 bands, linear phase; the same band set, then MASTER
inline constexpr int kBands = 5, kPerBand = 7;
enum BandP : int { Threshold, Gain, Range, Attack, Release, Solo, Bypass };
inline constexpr int band(int b, int k) noexcept { return b * kPerBand + k; }
enum P : int { MThreshold = kBands * kPerBand, MGain, MRange, MAttack, MRelease, X1, X2, X3, X4, Makeup, Adaptive, ReleaseMode,
               Behavior, Knee, Output, Trim, Dither, kCount };
inline constexpr std::uint32_t kLatency = 2559;
} // namespace straightedge

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
enum P : int { Attack, AttackSens, AttackTime, AttackShape, Sustain, SustainSens, SustainTime, SustainShape, Guard, Mix, Output, kCount };
} // namespace knockout

namespace haymaker {
enum P : int { Range, Sens, Duration, Release, Output, Trim, kCount };
} // namespace haymaker

namespace fourkings {   // 4 bands: RANGE, SENS, ON; then MASTER RANGE, SENS (added to every band)
inline constexpr int kBands = 4, kPerBand = 3;
enum BandP : int { Range, Sens, On };
inline constexpr int band(int b, int k) noexcept { return b * kPerBand + k; }
enum P : int { MRange = kBands * kPerBand, MSens, Duration, Release, Output, Trim, kCount };
inline constexpr float kCrossHz[3] = {150.0f, 1200.0f, 5000.0f};
} // namespace fourkings

namespace backbone {
enum P : int { LowLevel, HighLevel, Output, kCount };
} // namespace backbone

namespace fullhouse {
enum P : int { GateOn, Gate, LowOn, LowThreshold, LowGain, LevelerOn, Leveler, LevelerMode, HighOn, HighThreshold, HighGain, kCount };
} // namespace fullhouse

namespace wheelman {
enum P : int { Target, Sensitivity, Attack, RangeMin, RangeMax, Idle, Live, Spill, Output, kCount };
} // namespace wheelman

namespace chauffeur {
enum P : int { Target, NoteSens, Response, IgnoreSpill, IgnoreArtifacts, RangeMin, RangeMax, Idle, Output, kCount };
} // namespace chauffeur

namespace silencer {
enum P : int { Threshold, Freq, Audio, Sidechain, Monitor, kCount };
} // namespace silencer

namespace tightlips {
enum P : int { Threshold, Range, Freq, Filter, Mode, Listen, kCount };
} // namespace tightlips

namespace omerta {
enum P : int { Threshold, Range, Width, Mode, Lookahead, Monitor, kCount };
inline constexpr std::uint32_t kLookahead = 240;   // 5 ms at 48 kHz, always
} // namespace omerta

namespace breathless {
enum P : int { Sensitivity, LevelGate, Reduction, FadeOut, FadeIn, RoomTone, Listen, kCount };
inline constexpr std::uint32_t kLookahead = 480;   // 10 ms at 48 kHz
} // namespace breathless

namespace tommygun {
enum P : int { Amount, Rate, Offset, SyncSource, Bpm, kCount };
inline constexpr float kRateBeats[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};   // 1/16, 1/8, 1/4, 1/2, 1 BAR
} // namespace tommygun

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSwitchblade();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTerritory();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStraightEdge();
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
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHaymaker();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFourKings();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBackbone();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeChauffeur();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTightLips();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeOmerta();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFullHouse();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeWheelman();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSilencer();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBreathless();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTommyGun();

} // namespace hem::rack
