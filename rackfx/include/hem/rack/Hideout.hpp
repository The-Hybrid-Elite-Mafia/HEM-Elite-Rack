#pragma once
// =============================================================================
//  The HEM hideout: space, modulation, voice and monitoring (our own designs, our own names).
//
//  THE FIXER     three dynamic cuts that only work when a range gets nasty: MUD (centre 200 Hz–1 kHz),
//                HARSH (1.2–3 kHz) and FIZZ (3.5–12 kHz). Each: AMOUNT 0–100 (0 = nothing; up = it starts
//                on quieter sound and may take more off, at most 18 dB), FREQ 0–100 (where in its range),
//                LISTEN (hear just that band, as processed) and BYPASS. INPUT and OUTPUT ±18. Meters 0..2 =
//                what each band takes off (dB). Zero latency.
//  SIX SHOOTER   six-tap delay. Each TAP: ON, DELAY (1 ms–6 s), GAIN (0..-24 dB), PLACE (±45°: where it sits),
//                EQ (on, TYPE wide bell / narrow bell / high shelf / low shelf / high-pass / low-pass, FREQ
//                100 Hz–20 kHz, GAIN ±18). DRY (on, GAIN, PLACE). TEMPO: SYNC HOST or MANUAL (BPM 40–1200,
//                or TAP the tempo in), GRID (FREE, 1/4, 1/4 T, 1/8, 1/8 T, 1/16) and LOCK (taps land on the
//                grid). WOBBLE (on, RATE 0–20 Hz or per beat, DEPTH 0–500 ms of delay swing). FEEDBACK (on,
//                MODE ALL TAPS: every tap back to the input, or OWN DELAY; GAIN %, 50 = -6 dB; PLACE; FILTER low-pass,
//                low shelf or low fade, its FREQ and GAIN). OUTPUT ±12, held peaks with RESET. Setting the
//                taps by tapping a rhythm is not possible here. Zero latency.
//  BACK ALLEY    lo-fi space: a worn ECHO (on, TIME in ms or a NOTE: SYNC BPM / MS / HOST, BPM, FEEDBACK, PING PONG,
//                LEVEL) and a SPRING or PLATE reverb (on, LENGTH 0.5–20 s, LEVEL), both through the master HI-PASS
//                (30 Hz–1.5 kHz) and LO-PASS (1.5–21 kHz); MONO (10 = the stereo as it was, 0 = mono), MIX. The
//                wear (a narrower band, saturation, a slow wobble) is part of its sound and has no knob, as on
//                the original. Zero latency.
//  DRIVE-BY      a sound passing by on a track: PASS TIME (0.5–30 s for the whole pass), CLOSEST AT (when it
//                is closest, as a share of the track: early = it comes in fast and leaves slowly), START and END
//                (where along the line it begins and ends), CLOSE (how near the line passes), DIRECTION, MODE
//                (LOOP passes again and again, ONCE once per trigger), TRIGGER (SOUND: the sound
//                starting starts the pass; BUTTON: the FIRE button). How real it is: GAIN, PAN, PITCH and AIR LOSS
//                0–200 % (0 = none, 100 = as in life, 200 = twice that), each with its ON; REVERB (level, which
//                rises as it moves away), REVERB LENGTH, REVERB TONE, with its ON. Zero latency (the pitch bend comes
//                from the distance, as in life).
//  BODY DOUBLE   doubler with four voices. Each VOICE: ON, GAIN (0..-48 dB), PAN (±45°), DELAY (0–100 ms), DETUNE
//                (±100 cents), FEEDBACK (0–100 %, back into that voice through the feedback HI-PASS), OCTAVE (an octave
//                down), MOD DEPTH (±200 cents) and MOD RATE (0.1–400 Hz). DRY (on, GAIN, PAN), IN TIME (the
//                direct waits as long as the voices' own delay, so 0 ms means together), LOWEST NOTE (20 or 80 Hz: the
//                lowest note detuned cleanly; 20 Hz waits longer), MOD RESET (all the voices' wobbles start
//                together), FEED (SUM: each voice hears both sides; SPLIT: voices 1 and 3 hear the left, 2 and 4
//                the right), the voices' EQ (LOW SHELF and HIGH SHELF, each on, GAIN +12..-88 dB, FREQ), OUTPUT
//                (0..-24 dB), the held peaks with RESET. Zero latency.
//  HUSTLE        one LFO, three moves. RATE 0.01–60 Hz (SYNC HOST: one cycle a beat of the host's tempo), MULTIPLIER
//                (x0.25–x100), WAVE (SQUARE, TRIANGLE, SAW UP, SAW DOWN, SINE). LEVEL (DEPTH, ON, PHASE 0 / 90 /
//                180 / 270°), PITCH (DEPTH, ON, PHASE), SWING (where the picture sits: CENTER ±180, how far it
//                sweeps: RANGE 1–360, ON). GAIN ±12, MIX. Latency 10.5 ms (the pitch move needs room either way).
//  SWERVE        flanger: DELAY 0–20 ms (the middle of the sweep), DEPTH 0–100 % (how far it swings around it), RATE
//                0–20 Hz (SYNC FREE) or sweeps per beat (SYNC HOST), WAVE SINE / TRIANGLE, TIE (RATE and DEPTH move
//                together: the pitch wobble stays the same), HOLD (the sweep stops where it is), STEREO 0–180° (the
//                right side's sweep that far behind the left), FEEDBACK 0–100 % with LOOP FLIP, MIX 0–100 % with WET
//                FLIP, FILTER (OFF, LOW-PASS or HIGH-PASS; FREQ 20 Hz–20 kHz) in the wet path and the loop, TAPE (the dry
//                waits as long as DELAY, so the sweep goes through zero: the whole sound is late by DELAY), GAIN ±12 dB.
//                Peaks L / R with RESET. Zero latency.
//  COLD CASE     swept notches with a feedback loop. NOTCHES 1–6 pairs (+ HALF: one more at the top), DEPTH 0–100 (0 =
//                no notches: the loop alone), NOTCH FLIP (peaks instead of notches), STEREO 0–100 (the right side's
//                notches up to an octave apart from the left), LOW CUT (on, 20 Hz–1 kHz: what the notches get).
//                The sweep: RUN (on / paused), WAVE SAW UP / SAW DOWN / TRIANGLE / SINE / SQUARE, RATE 0.01–16
//                sweeps per beat x MULTIPLIER (x1/4 .. x16), TEMPO HOST or OWN (BPM 20–300), LOW and HIGH
//                (20 Hz–20 kHz: where it sweeps; equal = it stands still). The loop: LOOP (on), DECAY 0–100 (how
//                long it rings), LOOP FLIP, DELAY 0.1–100 ms, DENSITY 0–100 (the right side's echoes longer: 0 =
//                mono), HI TAME 0–1 (darker repeats), LOOP FILTER OFF / LOW-PASS / HIGH-PASS with its FREQ. The
//                wet sound: WIDTH 0–200 %, ROTATE ±45°, HIGH CUT (on, 1–20 kHz), PRE-DELAY 0–100 ms, WET MODE
//                STEREO / MONO / MONO FLIP (left flipped, then summed), MIX 0–100 %, GAIN ±18 dB. Peaks L / R
//                with RESET. Zero latency. Its own ranges: the original's manual does not give them.
//  THE INFORMANT vocoder: the MODULATOR (L+R, LEFT or RIGHT of the input: the words) shapes the CARRIER (its own
//                SYNTH, or the input's LEFT, RIGHT or STEREO). The synth: 48 keys (C2–B5, eight voices: more held keys
//                than voices are ignored), LATCH (HOLD: a key sounds while held; LATCH: a click latches it),
//                SOUND (ten of our own), TUNE (A = 220–880 Hz), SPREAD (0 = all in the middle, 1 = the voices across
//                the stereo). NOISE: white noise shaped by the words too (FILTER OFF / HI-PASS / LO-PASS, FREQ
//                16 Hz–16 kHz). Vocoding: SQUEEZE 0–50 dB (lifts quiet words, then holds the loud ones in), FORMANT
//                x0.5–x2 (moves the voice's shape down or up), LAG 60–800 (low = crisp and choppy, high = smooth),
//                HOLD 50–5000 ms (how long the last shape holds when the words stop; 5000 = for as long as the
//                carrier plays). A five-band EQ on the vocoded sound (low shelf, three bells, high shelf: ON, FREQ
//                86 Hz–16 kHz, GAIN ±30, Q 0.26–6.5). The mixer: CARRIER, MODULATOR, NOISE (-60..0 dB) and VOCODED
//                (-60..+6 dB) levels (-60 = off), each with SOLO and MUTE. Meters: the output peaks. Zero latency.
//                Not possible here: MIDI playing and a sidechain modulator or carrier.
//  STRAPPED      guitar mix desk in six faders. MODE (CLEAN TONE, EDGE, HIGH GAIN: each its own fixed voicing; BYPASS: none),
//                RE-AMP (a modelled amp for the mode, 4x oversampled). SENSITIVITY ±10 dB (in), BALANCE ±6 dB, OUTPUT
//                ±10 dB. Six faders ±10, each with a style (OFF bypasses or mutes it): BASS (a low shelf at 80 / 160 /
//                320 Hz), TREBLE (a high shelf at 3 / 6 / 12 kHz), COMPRESS (LIGHT / MEDIUM / HEAVY; the fader is how
//                hard), REVERB (SMALL / MEDIUM / LARGE; the fader is how much), DELAY (1/8, 1/8 D, 1/4 of the tempo;
//                how much) and PITCH (NARROW / WIDE / WIDEST doubled-and-detuned spread; how much). Meters: input and
//                output peaks. Latency 23 samples, always.
//  SAFE HOUSE    speakers in your headphones: two speakers in a mixing room, heard through your own head. ROOM 0–100
//                (how much of the room), SPEAKER ANGLE 0–180° (how wide the speakers sit, 60 = the usual triangle), HEAD TURN
//                ±180° (your head turned, + = to the right: the speakers move the other way), HEAD SIZE 35–75 cm and
//                EAR TO EAR 15–40 cm (your head's size: how late and how shaded the far ear is), MUTE L / MUTE R
//                (switch a speaker off), OUTPUT -24..+12 dB. For listening only: bypass it before exporting.
//                Not possible here: head tracking (a camera or sensor), headphone models. Zero latency.
//  DEAD DROP     hybrid delay: TIME (1 ms–3.5 s, or a NOTE when SYNC is HOST or BPM; TAP sets it from two
//                presses: the time in MS, the tempo in BPM, the nearest note in HOST), SPLIT TIMES (a separate right
//                time), FEEDBACK 0–200 % (past 100 it builds up, held in by the analog stage), PING PONG (and which
//                side it starts on), PHASE L / R, HI-PASS (off, 20 Hz–20 kHz) and LO-PASS (20 Hz–20 kHz, off)
//                with TIE FILTERS (moves both by the same factor), MOD DEPTH / RATE (the time wobbles), VINTAGE
//                (OFF, 1–4: more saturation, a little noise and a darker loop), LO-RES (a lower sample rate),
//                MIX, OUTPUT ±18, the peak. Zero latency.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace fixer {
inline constexpr int kBands = 3, kStride = 4;
enum BandP : int { Amount, Freq, Listen, Bypass };   // band b's control k: b * kStride + k
enum P : int { Input = kBands * kStride, Output, kCount };
constexpr int band(int b, int k) noexcept { return b * kStride + k; }
inline constexpr float kLowHz[kBands] = {200.0f, 1200.0f, 3500.0f}, kHighHz[kBands] = {1000.0f, 3000.0f, 12000.0f};
} // namespace fixer

namespace sixshooter {
inline constexpr int kTaps = 6, kStride = 8;
enum TapP : int { On, Delay, Gain, Rotate, EqOn, EqType, EqFreq, EqGain };   // tap t's control k: t * kStride + k
enum P : int { DirectOn = kTaps * kStride, DirectGain, DirectRotate, Sync, Bpm, Grid, Snap, Tap,
               ModOn, ModSync, ModRate, ModNote, ModDepth,
               FbOn, FbMode, FbGain, FbDelay, FbRotate, FbFilter, FbFreq, FbFilterGain, Output, Reset, kCount };
constexpr int tap(int t, int k) noexcept { return t * kStride + k; }
} // namespace sixshooter

namespace backalley {
enum P : int { EchoOn, Sync, Bpm, Time, Note, Feedback, PingPong, EchoLevel, ReverbOn, Length, Type, ReverbLevel, HighPass, LowPass, Mono, Mix,
               kCount };
} // namespace backalley

namespace driveby {
enum P : int { TrackTime, CenterTime, Start, End, Close, Direction, Mode, Trigger, Fire, Gain, GainOn, Pan, PanOn, Pitch, PitchOn, Air, AirOn,
               Reverb, ReverbOn, ReverbTime, Brightness, kCount };
} // namespace driveby

namespace bodydouble {
inline constexpr int kVoices = 4, kStride = 9;
enum VoiceP : int { On, Gain, Pan, Delay, Detune, Feedback, Octave, ModDepth, ModRate };   // voice v's control k: v * kStride + k
enum P : int { DirectOn = kVoices * kStride, DirectGain, DirectPan, AlignDirect, FbHighPass, Range, ModReset, Routing,
               LowOn, LowGain, LowFreq, HighOn, HighGain, HighFreq, Output, Reset, kCount };
constexpr int voice(int v, int k) noexcept { return v * kStride + k; }
} // namespace bodydouble

namespace hustle {
enum P : int { Sync, Rate, Factor, Wave, AmDepth, AmOn, AmPhase, FmDepth, FmOn, FmPhase, RotCenter, RotOn, RotRange, Gain, Mix, kCount };
} // namespace hustle

namespace swerve {
enum P : int { Delay, Depth, Rate, Sync, Wave, Tie, Hold, StereoOffset, Feedback, LoopFlip, Mix, WetFlip, FilterType, FilterFreq, Tape,
               Gain, Reset, kCount };
} // namespace swerve
namespace coldcase {
enum P : int { Notches, Half, Depth, NotchFlip, StereoSpread, LowCutOn, LowCut, Run, Wave, Rate, Multiplier, Tempo, Bpm, LowHz, HighHz,
               LoopOn, Decay, LoopFlip, Delay, Density, HiTame, LoopFilter, LoopFreq, Width, Rotate, HighCutOn, HighCut, PreDelay,
               WetMode, Mix, Gain, Reset, kCount };
} // namespace coldcase
namespace informant {
inline constexpr int kEqBands = 5, kEqStride = 4, kKeys = 48, kLowKey = 36;   // keys: MIDI 36 (C2) .. 83 (B5)
inline constexpr int kPaths = 4, CarrierPath = 0, ModPath = 1, NoisePath = 2, MorphPath = 3;
enum EqP : int { EqOn, EqFreq, EqGain, EqQ };   // EQ band b's control k: Eq0 + b * kEqStride + k
enum P : int { Modulator, Carrier, NoiseFilter, NoiseFreq, Sound, Tune, Spread, Eq0,
               Pressure = Eq0 + kEqBands * kEqStride, Formant, Smoothing, Release,
               Level0, Solo0 = Level0 + kPaths, Mute0 = Solo0 + kPaths, Sustain = Mute0 + kPaths, Key0, kCount = Key0 + kKeys };
constexpr int eq(int b, int k) noexcept { return Eq0 + b * kEqStride + k; }
} // namespace informant

namespace strapped {
inline constexpr int BassF = 0, TrebleF = 1, CompressF = 2, ReverbF = 3, DelayF = 4, PitchF = 5, kFaders = 6;   // the faders, in order
enum P : int { Mode, ReAmp, Sensitivity, Balance, Output, Fader0, Color0 = Fader0 + kFaders, kCount = Color0 + kFaders };
} // namespace strapped

namespace safehouse {
enum P : int { Amount, Front, Rotate, Head, Arc, MuteL, MuteR, Output, kCount };
} // namespace safehouse

namespace deaddrop {
enum P : int { Sync, Bpm, Time, Note, Tap, Dual, TimeR, NoteR, Feedback, PingPong, PingStart, PhaseL, PhaseR, HighPass, LowPass, Link,
               ModDepth, ModRate, Analog, LoFi, Mix, Output, kCount };
} // namespace deaddrop

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFixer();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDeadDrop();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSwerve();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeColdCase();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSixShooter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBackAlley();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDriveBy();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBodyDouble();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHustle();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeInformant();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStrapped();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSafeHouse();

} // namespace hem::rack
