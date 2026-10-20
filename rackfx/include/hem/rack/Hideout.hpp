#pragma once
// =============================================================================
//  The HEM hideout: space, modulation, voice and monitoring (our own designs, our own names).
//
//  THE FIXER     three dynamic cuts that only work when a range gets nasty: MUD, HARSH and FIZZ,
//                each with FREQ, THRESHOLD and RANGE (the most it takes off); SPEED; LISTEN to one
//                band; OUTPUT. Meters 0..2 = what each band takes off (dB). Zero latency.
//  SIX SHOOTER   rhythmic multi-tap delay: SHOTS (1..6 repeats in a row), TIME between shots (ms,
//                or a NOTE of the tempo), DECAY (each shot quieter), SPREAD (shots alternate left /
//                right), SWING (every second shot late), FEEDBACK (the round starts again), TONE
//                (darker repeats), MIX, OUTPUT.
//  BACK ALLEY    lo-fi space: a SPRING or PLATE style reverb and a worn tape-style echo, both
//                through GRIT (narrow band, saturation); REVERB, DECAY, ECHO, TIME, FEEDBACK,
//                WOBBLE (wow and flutter on the echo), NARROW (the whole stereo image), MIX, OUTPUT.
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
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace fixer {
inline constexpr int kBands = 3;
enum P : int { Freq1, Thr1, Range1, Freq2, Thr2, Range2, Freq3, Thr3, Range3, Speed, Listen, Output, kCount };
} // namespace fixer

namespace sixshooter {
enum P : int { Shots, Note, Time, SyncSource, Bpm, Decay, Spread, Swing, Feedback, Tone, Mix, Output, kCount };
} // namespace sixshooter

namespace backalley {
enum P : int { Space, Reverb, Decay, Echo, Time, Feedback, Wobble, Grit, Narrow, Mix, Output, kCount };
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

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFixer();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSixShooter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBackAlley();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDriveBy();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBodyDouble();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHustle();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeInformant();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStrapped();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSafeHouse();

} // namespace hem::rack
