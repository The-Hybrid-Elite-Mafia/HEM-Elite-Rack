#pragma once
// =============================================================================
//  Wave 4 (colour, tape, drums, lo-fi): new HEM plug-ins, each built to the full control set of one original.
//  Our own names and our own words on every label.
//
//  HEAVY HITTER  drum enhancer. DRUM picks the voicing (KICK, SNARE TOP, SNARE UNDER, HATS, TOMS, OVERHEADS,
//                ROOM): where THUMP and TREBLE work and how SNAP hears a hit. SENSITIVITY ±18 dB (the level in),
//                THUMP ±12 dB (the drum's low end), TREBLE ±12 dB (its top), SNAP 0..100 (how much the front of
//                each hit is pushed out), OUTPUT ±18 dB. METER IN / OUT picks what the level window shows
//                (-24..0 dBFS); the INPUT window is the level going in (aim for about -12 .. -6 dBFS).
//                Our voicings, not the original's.
//  REEL MONEY    tube tape machine: SPEED 7.5 / 15 IPS, MONITOR TAPE / INPUT (input = the tube stage only),
//                BIAS NORMAL / HOT (hot = 3 dB over: softer top, cleaner), INTO TAPE ±18 dB (how hard the tube
//                and the tape are hit), OFF TAPE ±18 dB (the level out), LINK (on: OFF TAPE also takes back what
//                INTO TAPE adds, so the level stays put), TAPE LEVEL 150..1020 nWb/m (185 = the reference: more =
//                more tape saturation, the level made up), WOBBLE 0..100 (wow and flutter, 50 = a healthy machine),
//                HISS OFF / -40..0 dB (0 = as noisy as the real machine), the echo off the tape: ECHO TIME
//                1..500 ms, ECHO STYLE SLAP (one repeat) / REPEATS, ECHO LEVEL OFF..100, ECHO TONE 200 Hz..16 kHz;
//                the VU meter: METER IN / OUT, VU ZERO -24..-8 dBFS. Constant latency (kReelLatency).
//  PAWN SHOP     lo-fi box in four modules, each with its own on switch, then the master section.
//                DEVICE  SOUND (AIRY / VINTAGE / DEEP / FLAT: the band the old device lets through), TONE 0..100 (from
//                        its darker to its brighter side), SQUASH -10..+10 (minus: squeezes the loud parts; plus:
//                        pushes them out), RINGER 0..100 (ring modulation and digital clipping), DECADE 50s / 60s /
//                        70s / 80s (the band, limiting and distortion of the era), MIX.
//                SPACE   the echo: SYNC BPM / HOST / MS, TIME 1..3500 ms (MS), NOTE (BPM, HOST), BPM, ECHO 0..100 (the
//                        first repeat's level), PING PONG, FEEDBACK 0..100 (100 = repeats forever); the reverb:
//                        REVERB SPRING / PLATE, REVERB LEVEL, REVERB LENGTH; SPACE MIX.
//                NOISE   TYPE (twelve noises we make ourselves: the original plays 64 recordings), DUCK (the noise drops
//                        when the music plays) or GATE (it plays only with the music), THRESHOLD -60..0 dBFS (-60 =
//                        the noise just plays), PLACE PRE (into the space module) / POST, LEVEL -60..0 dB.
//                MECHANICS  two sets, A (tape-like, drifting) and B (record-like, turning): WOW (pitch), WOBBLE
//                        (level), SPEED (how fast they move).
//                MASTER  INPUT ±18 dB, OUTPUT ±18 dB, LOW CUT 30..1500 Hz, HIGH CUT 15..21 kHz, MONO 0..10 (10 = the
//                        stereo as it was). Readouts: IN and OUT peaks. Constant latency (kPawnLatency).
//  HOT WIRE      guitar amp head and cabinets: INPUT ±12 dB, AMP (five of our own: CLASSIC, CROWN, STEEL, BLAZE,
//                BEAST), CHANNEL CLEAN / DRIVE / LEAD, FOCUS 0..10 (0 = off; up from there a lift sweeping 250 Hz to
//                4 kHz), GATE -60..0 dB (-60 = off) and GATE SPEED 0..10, GAIN, BASS, MIDDLE, TREBLE, VOLUME, DEPTH
//                and PRESENCE (0..10), CABINET (six pairs: each a darker and a brighter cabinet), CAB TONE 0..100
//                (the mix of the pair, 50 = equal), ROOM 0..100 (early reflections; 0 = off), CAB OFF (for your own
//                cabinet), OUTPUT ±12 dB; the output never passes -0.1 dBFS (a limiter). Readouts: INPUT, OUTPUT,
//                CLIPS (times the limiter caught a peak) with CLEAR. The original's preset locks are not in a rack.
//                4x oversampled; latency 23.
//  BOTTOM FEEDER bass amp and cabinet: INPUT ±12 dB, AMP WARM / DARK; the direct (DI) side: DI TONE 0..10 (0 =
//                untouched), DI LEVEL 0..10, BLEND 0..10 (0 = all amp, 10 = all DI), DI FLIP; the drive: DRIVE
//                TYPE OFF / FAT / TIGHT / SHARP, DRIVE TONE, DRIVE GAIN, DRIVE MIX (0..10); VOICE 1 / 2 / 3 (a mid
//                lift from narrow to wide); GAIN, BASS, MID, HI MID, HIGH (0..10); CAB TONE, ROOM, CAB OFF, OUTPUT;
//                the output limiter and readouts as on HOT WIRE. 4x oversampled; latency 23.
//  THE AUDITOR   loudness meter with gain and a true-peak limiter (its own plate). Settings: METHOD EBU / LM1 / DIAL,
//                CHANNELS BOTH / LEFT / RIGHT, WEIGHTING BS.1770 / LEQ(A) / LEQ(B) / LEQ(C) / LEQ(M) / NONE, SHORT TIME
//                400 ms .. 2 min (EBU: always 3 s), SHORT MAX (OFF, -36..-12) and SHORT MIN (OFF, -90..-12): the
//                OVERS / UNDERS counters, TRUE PEAK MAX -25..0 dBTP (also the limiter's ceiling), TARGET (OFF,
//                -33..-9), SCALE LKFS / LU / LUFS (LU = LUFS + 23), and the pre-filters LOW-PASS (on, 2..18 kHz, 6 or
//                12 dB / oct) and HIGH-PASS (on, 20..2000 Hz, 6 or 12 dB / oct) on what is measured. Processing:
//                GAIN ±18 dB (before everything), LIMITER on / off (true peak, no make-up). MEASURE PLAY / PAUSE,
//                RESET (long term, range, counters, timer), CLEAR PEAKS (true peak and limiter holds). Readouts:
//                SHORT TERM, LONG TERM, RANGE, MOMENTARY, TRUE PEAK (held), LIMITER (held), UNDERS, OVERS, TIMER,
//                TRIM (what GAIN would need to land LONG TERM on TARGET: up to +12). The original's TRIM button
//                moves its gain for you, its CSV logs and its follow-transport need a host link this rack doesn't
//                have: here TRIM is shown and you set GAIN. DIAL measures the speech band only (our simple
//                stand-in for the original's dialogue detection). Latency kAuditorLatency.
//  HOTHEAD       one-knob overdrive: DRIVE 0..10 (0 = untouched; up from there a mid push, the clipping and a
//                warmth filter that closes as it goes, the level kept about the same). The original's guide
//                names no other control. Latency 23 samples (the clipping is 4x oversampled).
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace heavyhitter {
enum P : int { Drum, Sensitivity, Thump, Treble, Snap, Output, Meter, kCount };
enum Kind : int { Kick, SnareTop, SnareUnder, Hats, Toms, Overheads, Room, kKinds };
} // namespace heavyhitter

namespace reelmoney {
enum P : int { Speed, Monitor, Bias, IntoTape, OffTape, Link, TapeLevel, Wobble, Hiss, EchoTime, EchoStyle, EchoLevel, EchoTone,
               Meter, VuZero, kCount };
inline constexpr std::uint32_t kReelLatency = 23 + 23 + 64;
} // namespace reelmoney

namespace pawnshop {
enum P : int { DeviceOn, Device, Tone, Squash, Ringer, Styler, DeviceMix,
               SpaceOn, Sync, TimeMs, Note, Bpm, EchoLevel, PingPong, Feedback, ReverbLevel, ReverbLength, ReverbType, SpaceMix,
               NoiseOn, NoiseType, NoiseMode, Threshold, NoisePlace, NoiseLevel,
               MechOn, WowA, WobbleA, SpeedA, WowB, WobbleB, SpeedB,
               Input, Output, LowCut, HighCut, Mono, kCount };
enum Noise : int { TapeHiss, White, Pink, Rumble, Crackle, Dust, Hum50, Hum60, Radio, Rain, Fire, Wind, kNoises };
inline constexpr float kNoteBeats[] = {4.0f, 2.0f, 3.0f, 1.0f, 1.5f, 2.0f / 3.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};
inline constexpr std::uint32_t kPawnLatency = 160;
} // namespace pawnshop

namespace hothead {
enum P : int { Drive, kCount };
inline constexpr std::uint32_t kLatency = 23;
} // namespace hothead

namespace hotwire {
enum P : int { Input, Amp, Channel, Focus, Gate, GateSpeed, Gain, Bass, Middle, Treble, Volume, Depth, Presence,
               Cabinet, CabTone, Room, CabOff, Output, Clear, kCount };
enum Ch : int { Clean, Drive, Lead };
} // namespace hotwire
namespace bottomfeeder {
enum P : int { Input, Amp, DiTone, DiLevel, Blend, DiFlip, DriveType, DriveTone, DriveGain, DriveMix, Voice,
               Gain, Bass, Mid, HiMid, High, CabTone, Room, CabOff, Output, Clear, kCount };
enum Drive : int { DriveOff, Fat, Tight, Sharp };
} // namespace bottomfeeder

namespace auditor {
enum P : int { Method, Channels, Weighting, ShortTime, ShortMax, ShortMin, TruePeakMax, Target, Scale,
               LpfOn, LpfFreq, LpfSlope, HpfOn, HpfFreq, HpfSlope, Measure, Reset, ClearPeaks, Limiter, Gain, kCount };
enum Meter : int { ShortTerm, LongTerm, Range, Momentary, TruePeak, Reduction, Unders, Overs, Timer, Trim, kMeters };
enum Methods : int { Ebu, Lm1, Dial };
enum Weights : int { Bs1770, LeqA, LeqB, LeqC, LeqM, NoWeight };
inline constexpr std::uint32_t kAuditorLatency = 23 + 48;
} // namespace auditor

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHeavyHitter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeAuditor();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHotWire();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBottomFeeder();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHothead();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePawnShop();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeReelMoney();

} // namespace hem::rack
