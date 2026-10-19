#pragma once
// =============================================================================
//  HEM TAPE — a two-track tape machine (our own design): the warmth, weight and glue of
//  running a mix to tape.
//
//    INPUT     -12..+18 dB into the tape: more = more saturation (highs first, like real tape:
//              the signal is pre-emphasised before the "tape" and de-emphasised after)
//    SPEED     15 IPS (more head bump, softer top) or 30 IPS (tighter low end, more top)
//    BUMP      0..100 %: the low-end head bump (and the little dip above it)
//    WOW       0..100 %: slow pitch drift and fast flutter (0 = rock steady)
//    HISS      0..100 %: tape noise (0 = none)
//    AUTO TRIM ON: output turned down by what INPUT adds, so you hear colour, not volume
//    OUTPUT
//  Saturation 4x oversampled. Latency: 23 (oversampler) + 48 (the wow transport) = 71 samples,
//  always the same. Meters 0 / 1: VU left / right (dB, a full-scale sine reads 0).
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace tape {
enum P : int { Input, Speed, Bump, Wow, Hiss, AutoTrim, Output, kCount };
inline constexpr std::uint32_t kTransportDelay = 48;
} // namespace tape

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTape();

} // namespace hem::rack
