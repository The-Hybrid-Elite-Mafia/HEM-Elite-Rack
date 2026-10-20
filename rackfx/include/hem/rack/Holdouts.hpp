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

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFingerprint();

} // namespace hem::rack
