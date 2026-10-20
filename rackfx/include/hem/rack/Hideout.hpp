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
//  DRIVE-BY      a sound passing by: the pitch bends as it comes and goes, it gets louder
//                and quieter and swings across. MANUAL (POSITION knob) or AUTO (passes on the beat).
//                SPEED (how fast: how far the pitch bends), DISTANCE (how close it passes), AIR (far =
//                darker), MIX, OUTPUT.
//  BODY DOUBLE   doubler: 2 or 4 voices, each a little late and a little out of tune, spread
//                left / right. DELAY, DETUNE (cents), SPREAD, VOICES level, LOW CUT on the voices,
//                DRY level, OUTPUT.
//  HUSTLE        modulator: one LFO (RATE in Hz or a NOTE of the tempo, WAVE) drives TREMOLO
//                (level), VIBRATO (pitch) and PAN (left-right swing, OFFSET = where in the cycle it
//                swings); MIX, OUTPUT.
//  THE INFORMANT vocoder with its own voice: the input's words shape a built-in carrier (NOTE,
//                OCTAVE, CHORD, WAVE SAW / PULSE / NOISE). BANDS 16 / 24 / 32, REACT (how quickly the
//                bands follow), SHIFT (moves the voice's formants), SIBILANCE (lets the esses
//                through), MIX, OUTPUT.
//  STRAPPED      guitar mix strip: TIGHT (low cut), COMPRESS, BASS, MIDS, TREBLE, then CHORUS, DELAY
//                (a NOTE of the tempo) and ROOM, WIDTH, OUTPUT.
//  SAFE HOUSE    speakers in your headphones: each ear hears both speakers, the far one later and
//                darker, plus the room. ROOM DRY / STUDIO / LIVE, ANGLE (how wide the speakers sit),
//                CROSSFEED, ROOM level, BASS, OUTPUT. For listening only: bypass it before exporting.
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
enum P : int { Mode, Position, Rate, SyncSource, Bpm, Speed, Distance, Air, Mix, Output, kCount };
} // namespace driveby

namespace bodydouble {
enum P : int { Voices, Delay, Detune, Spread, VoiceLevel, LowCut, Dry, Output, kCount };
} // namespace bodydouble

namespace hustle {
enum P : int { Rate, Note, SyncSource, Bpm, Wave, Tremolo, Vibrato, Pan, Offset, Mix, Output, kCount };
} // namespace hustle

namespace informant {
enum P : int { Note, Octave, Chord, Wave, Bands, React, Shift, Sibilance, Mix, Output, kCount };
} // namespace informant

namespace strapped {
enum P : int { Tight, Compress, Bass, Mids, Treble, Chorus, Delay, Note, Room, Width, Output, kCount };
} // namespace strapped

namespace safehouse {
enum P : int { Room, Angle, Crossfeed, RoomLevel, Bass, Output, kCount };
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
