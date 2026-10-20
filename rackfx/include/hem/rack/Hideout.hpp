#pragma once
// =============================================================================
//  The HEM hideout: space, modulation, voice and monitoring (our own designs, our own names).
//
//  THE FIXER     three dynamic cuts that only work when a range gets nasty: MUD (centre 200 Hz–1 kHz),
//                HARSH (1.2–3 kHz) and FIZZ (3.5–12 kHz). Each: AMOUNT 0–100 (0 = nothing; up = it starts
//                on quieter sound and may take more off, at most 18 dB), FREQ 0–100 (where in its range),
//                LISTEN (hear just that band, as processed) and BYPASS. INPUT and OUTPUT ±18. Meters 0..2 =
//                what each band takes off (dB). Zero latency.
//  SIX SHOOTER   six-tap delay. Each TAP: ON, DELAY (1 ms–6 s), GAIN (0..-24 dB), ROTATE (±45°: where it sits),
//                EQ (on, TYPE wide bell / narrow bell / high shelf / low shelf / high-pass / low-pass, FREQ
//                100 Hz–20 kHz, GAIN ±18). DIRECT (on, GAIN, ROTATE). TEMPO: SYNC HOST or MANUAL (BPM 40–1200,
//                or TAP the tempo in), GRID (FREE, 1/4, 1/4 T, 1/8, 1/8 T, 1/16) and SNAP (taps land on the
//                grid). MODULATOR (on, RATE 0–20 Hz or per beat, DEPTH 0–500 ms of delay swing). FEEDBACK (on,
//                MODE: every tap back to the input, or its own DELAY; GAIN %, 50 = -6 dB; ROTATE; FILTER low-pass,
//                low shelf or low decay, its FREQ and GAIN). OUTPUT ±12, held peaks with RESET. Setting the
//                taps by tapping a rhythm is not possible here. Zero latency.
//  BACK ALLEY    lo-fi space: a worn ECHO (on, TIME in ms or a NOTE: SYNC BPM / MS / HOST, BPM, FEEDBACK, PING PONG,
//                LEVEL) and a SPRING or PLATE reverb (on, LENGTH 0.5–20 s, LEVEL), both through the master HI-PASS
//                (30 Hz–1.5 kHz) and LO-PASS (1.5–21 kHz); MONO (10 = the stereo as it was, 0 = mono), MIX. The
//                wear (a narrower band, saturation, a slow wobble) is part of its sound and has no knob, as on
//                the original. Zero latency.
//  DRIVE-BY      a sound passing by on a track: TRACK TIME (0.5–30 s for the whole pass), CENTER TIME (when it
//                is closest, as a share of the track: early = it comes in fast and leaves slowly), START and END
//                (where along the line it begins and ends), CLOSE (how near the line passes), DIRECTION, MODE
//                (CONTINUOUS passes again and again, ONE-SHOT once per trigger), TRIGGER (ENERGY: the sound
//                starting starts the pass; MANUAL: the FIRE button). How real it is: GAIN, PAN, PITCH and AIR
//                0–200 % (0 = none, 100 = as in life, 200 = twice that), each with its ON; REVERB (level, which
//                rises as it moves away), REVERB TIME, BRIGHTNESS, with its ON. Zero latency (the pitch bend comes
//                from the distance, as in life).
//  BODY DOUBLE   doubler with four voices. Each VOICE: ON, GAIN (0..-48 dB), PAN (±45°), DELAY (0–100 ms), DETUNE
//                (±100 cents), FEEDBACK (0–100 %, back into that voice through the feedback HI-PASS), OCTAVE (an octave
//                down), MOD DEPTH (±200 cents) and MOD RATE (0.1–400 Hz). DIRECT (on, GAIN, PAN), ALIGN DIRECT (the
//                direct waits as long as the voices' own delay, so 0 ms means together), RANGE (20 or 80 Hz: the
//                lowest note detuned cleanly; 20 Hz waits longer), MOD RESET (all the voices' wobbles start
//                together), ROUTING (SUM: each voice hears both sides; SPLIT: voices 1 and 3 hear the left, 2 and 4
//                the right), the voices' EQ (LOW SHELF and HIGH SHELF, each on, GAIN +12..-88 dB, FREQ), OUTPUT
//                (0..-24 dB), the held peaks with RESET. Zero latency.
//  HUSTLE        one LFO, three moves. RATE 0.01–60 Hz (SYNC AUTO: one cycle a beat of the host's tempo), x FACTOR
//                (x0.25–x100), WAVE (SQUARE, TRIANGLE, SAW UP, SAW DOWN, SINE). AM (level: DEPTH, ON, PHASE 0 / 90 /
//                180 / 270°), FM (pitch: DEPTH, ON, PHASE), ROTATION (where the picture sits: CENTER ±180, how far it
//                sweeps: RANGE 1–360, ON). GAIN ±12, MIX. Latency 10.5 ms (the pitch move needs room either way).
//  THE INFORMANT vocoder with its own voice: the input's words shape a built-in carrier (NOTE,
//                OCTAVE, CHORD, WAVE SAW / PULSE / NOISE). BANDS 16 / 24 / 32, REACT (how quickly the
//                bands follow), SHIFT (moves the voice's formants), SIBILANCE (lets the esses
//                through), MIX, OUTPUT.
//  STRAPPED      guitar mix desk in six faders. MODE (CLEAN, CRUNCH, HEAVY: each its own fixed voicing; BYPASS: none),
//                RE-AMP (a modelled amp for the mode, 4x oversampled). SENSITIVITY ±10 dB (in), BALANCE ±6 dB, OUTPUT
//                ±10 dB. Six faders ±10, each with a colour (OFF bypasses or mutes it): BASS (a low shelf at 80 / 160 /
//                320 Hz), TREBLE (a high shelf at 3 / 6 / 12 kHz), COMPRESS (LIGHT / MEDIUM / HEAVY; the fader is how
//                hard), REVERB (SMALL / MEDIUM / LARGE; the fader is how much), DELAY (1/8, 1/8 D, 1/4 of the tempo;
//                how much) and PITCH (NARROW / WIDE / WIDEST doubled-and-detuned spread; how much). Meters: input and
//                output peaks. Latency 23 samples, always.
//  SAFE HOUSE    speakers in your headphones: two speakers in a mixing room, heard through your own head. AMOUNT 0–100
//                (how much of the room), FRONT 0–180° (how wide the speakers sit, 60 = the usual triangle), ROTATE
//                ±180° (your head turned, + = to the right: the speakers move the other way), HEAD CIRCUMFERENCE 35–75 cm and
//                INTER-AURAL ARC 15–40 cm (your head's size: how late and how shaded the far ear is), MUTE L / MUTE R
//                (switch a speaker off), OUTPUT -24..+12 dB. For listening only: bypass it before exporting.
//                Not possible here: head tracking (a camera or sensor), headphone models. Zero latency.
//  DEAD DROP     hybrid delay: TIME (1 ms–3.5 s, or a NOTE when SYNC is HOST or BPM; TAP sets it from two
//                presses: the time in MS, the tempo in BPM, the nearest note in HOST), DUAL (a separate right
//                time), FEEDBACK 0–200 % (past 100 it builds up, held in by the analog stage), PING PONG (and which
//                side it starts on), PHASE L / R, HI-PASS (off, 20 Hz–20 kHz) and LO-PASS (20 Hz–20 kHz, off)
//                with FILTER LINK (moves both by the same factor), MOD DEPTH / RATE (the time wobbles), ANALOG
//                (OFF, 1–4: more saturation, a little noise and a darker loop), LOFI (a lower sample rate),
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

namespace informant {
enum P : int { Note, Octave, Chord, Wave, Bands, React, Shift, Sibilance, Mix, Output, kCount };
} // namespace informant

namespace strapped {
enum Fader : int { BassF, TrebleF, CompressF, ReverbF, DelayF, PitchF, kFaders };
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
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSixShooter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBackAlley();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDriveBy();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBodyDouble();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHustle();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeInformant();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStrapped();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSafeHouse();

} // namespace hem::rack
