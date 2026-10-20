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
//  THE RACKET    drive pedal: TYPE OVERDRIVE / DISTORTION / FUZZ / OCTAVE (an octave up on top of
//                the fuzz), DRIVE, TONE, LEVEL, MIX. 4x oversampled; latency 23.
//  THE SNITCH    tuner: MODE CHROMATIC (any note) / GUITAR / DROP D / OPEN G / OPEN D / OPEN E / DADGAD /
//                HALF DOWN / MY NOTES (only the notes switched on below); REFERENCE A ±6 cents around 440 Hz;
//                the twelve note switches C .. B (used by MY NOTES; pick one for a single note). It tunes to the
//                switched-on note nearest the sound and shows that NOTE, how many CENTS off (±50) and the
//                pitch. The sound passes untouched. Meters: 0 note (MIDI number, -1 = none), 1 cents, 2 Hz,
//                3 how it stands: -1 flat (3 cents or more under), 0 in tune (within 6), +1 sharp.
//  PROMOTION     pitch shifter: SHIFT (semitones), FINE (cents), CHARACTER SMOOTH (longer grains,
//                for sustained parts) or TIGHT (shorter, for rhythm), MIX, OUTPUT.
//  WISEGUYS      harmonizer: three extra voices, each its INTERVAL (semitones), LEVEL and PAN, plus
//                the DRY level and OUTPUT.
//  MADE MAN      pitch correction for a single voice: KEY and SCALE (ALL NOTES / MAJOR / MINOR), SPEED
//                (how fast a note is pulled in: 0 = hard), AMOUNT, MIX. Meters: 0 the sung note, 1 how
//                far it was pulled (cents).
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
//  LIVE WIRE     test tones: ON, WAVE SINE / SQUARE / SAW / TRIANGLE / WHITE / PINK / SWEEP, FREQ, LEVEL
//                (dBFS), SWEEP TIME, MODE REPLACE (only the tone) or ADD (tone on top), CHANNELS BOTH /
//                LEFT / RIGHT / OPPOSED (right upside down). Off by default; when ON it makes sound out of
//                silence, on purpose.
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
enum P : int { Type, Drive, Tone, Level, Mix, kCount };
} // namespace racket

namespace snitch {
enum P : int { Mode, Reference, Note0, kCount = Note0 + 12 };
enum Tuning : int { Chromatic, Guitar, DropD, OpenG, OpenD, OpenE, Dadgad, HalfDown, MyNotes, kTunings };
} // namespace snitch

namespace promotion {
enum P : int { Shift, Fine, Character, Mix, Output, kCount };
} // namespace promotion

namespace wiseguys {
inline constexpr int kVoices = 3;
enum P : int { Interval1, Level1, Pan1, Interval2, Level2, Pan2, Interval3, Level3, Pan3, Dry, Output, kCount };
} // namespace wiseguys

namespace mademan {
enum P : int { Key, Scale, Speed, Amount, Mix, kCount };
} // namespace mademan

namespace detective {
enum P : int { Detect, Spell, Edit, Root, Scale, kCount };
enum State : int { Waiting, Listening, Found, Nothing, Edited };
} // namespace detective

namespace witness {
enum P : int { Headroom, DisplayDelay, Clear, kCount };
} // namespace witness

namespace livewire {
enum P : int { On, Wave, Freq, Level, SweepTime, Mode, Channels, kCount };
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
