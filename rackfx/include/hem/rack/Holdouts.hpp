#pragma once
// =============================================================================
//  The holdouts: the plug-ins that waited for something the rack didn't have yet. Each built to the full
//  control set of one original; our own names and words on every label.
//
//  FINGERPRINT   convolution reverb: the sound of a real space, played through its impulse response (its
//                "fingerprint"). SPACE picks one of sixteen impulse responses we make ourselves (booth, rooms,
//                chambers, halls, a church, plates, springs, a garage: the original loads recordings), START
//                0..1 s (skips the front of the response), LENGTH 0..6 s (6 = all of it), DECAY 0.25..1 (shortens
//                the reverb time: the response dies that much faster), MIX 0..100 % wet (100 = wet only, for a
//                send), DIRECT (the response's own direct sound in or out), PRE-DELAY 0..500 ms, DRY DELAY
//                0..30 ms (holds the dry sound back against the reverb), CPU FULL / LOW (LOW drops the part of
//                the response 60 dB down), OUTPUT 0..-50 dB, CROSSTALK 0..100 (how much of each side feeds the
//                other's reverb). Readouts: RT60 (the response's reverb time as it is now) and READY (off while
//                a new response is being made, in the background, about a tenth of a second).
//                Partitioned FFT convolution, 512-sample blocks: latency 512 samples.
//  GRAND LARCENY electric grand piano (an instrument: play it from a MIDI keyboard, a DAW's MIDI track, or the 61
//                keys on its plate, C2..C7). KEYS: VELOCITY CURVE -50..+50 (0 = as played; plus = harder to reach
//                the loud end), FORMANT -12..+12 semitones (the body of the tone moves, the pitch stays), TUNE
//                ±100 cents. MIX: MAIN (the strings), KEY UP (the damper landing), MECHANICS (the action's thump),
//                PEDAL (the pedal's own sound), SQUEEZE (a parallel compressor), SYMPATHY (the open strings
//                ringing along while the pedal is down). TONE: BASS (shelf at 203 Hz), MID (bell at 900 Hz),
//                TREBLE (shelf at 1.875 kHz), ±50 = ±18 dB. OUTPUT: VOLUME 0..10 (7 = 0 dB), LIMIT (a brickwall at
//                -0.3 dBFS). EFFECTS (one switch for all): TREMOLO, AUTO PAN and PHASER, each on / off with its
//                rate (synced to a note, or free in Hz) and depth (the phaser also its mix); CHORUS on / off and
//                depth; REVERB mix, pre-delay 20..180 ms, time 0.4..6 s, damp 0.1..2 (the highs' decay against
//                the rest). PEDAL is the sustain pedal (MIDI controller 64 moves it). LATCH: a click on a plate
//                key holds it until clicked again. Our own piano: partials with the stretch of real strings,
//                two decay stages, the hammer's place and the player's force in the tone; 32 voices. The
//                original's rate knob is one dial (sync to the left, free to the right): ours is a switch, a
//                note and a rate. Latency 48 samples (the limiter's look-ahead). Meters: OUT L / R (dBFS),
//                VOICES (how many are sounding).
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace fingerprint {
enum P : int { Space, Start, Length, Decay, Mix, Direct, PreDelay, DryDelay, Cpu, Output, Crosstalk, kCount };
enum Meter : int { Rt60, Ready, kMeters };
inline constexpr int kSpaces = 16;
inline constexpr unsigned kBlock = 512;   // the convolution's block (and the latency)
} // namespace fingerprint

namespace grandlarceny {
inline constexpr int kKeys = 61;           // the plate's keyboard: C2 .. C7
enum P : int {
    VelCurve, Formant, Tune,
    Main, KeyUp, Mechanics, PedalNoise, Squeeze, Sympathy,
    Bass, Mid, Treble,
    Volume, Limit,
    FxOn,
    TremOn, TremSync, TremNote, TremHz, TremDepth,
    PanOn, PanSync, PanNote, PanHz, PanDepth,
    PhOn, PhMix, PhSync, PhNote, PhHz, PhDepth,
    ChOn, ChDepth,
    RvMix, RvPre, RvTime, RvDamp,
    Pedal, Latch,
    Key0, kCount = Key0 + kKeys };
enum Meter : int { OutL, OutR, Voices, kMeters };
inline constexpr int kFirstKeyNote = 36;   // the plate's keyboard starts on C2
inline constexpr int kVoices = 32;
inline constexpr unsigned kLatency = 48;
} // namespace grandlarceny

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFingerprint();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGrandLarceny();

} // namespace hem::rack
