#pragma once
// =============================================================================
//  HEM-Elite-Rack dynamics (our own designs). Each publishes its gain reduction (dB, <= 0)
//  for the faceplate's needle.
//
//  HEM ENFORCER — a clean, modern compressor: THRESHOLD, RATIO 1..20, KNEE 0..24 dB, ATTACK,
//    RELEASE (or AUTO), MAKEUP (or AUTO MAKEUP), sidechain HPF, MIX, OUTPUT, TAIL (STEADY: the same
//    release speed all the way; LINGER: eases out slowly near the end), CHARACTER (CLEAN, or
//    WARM: a little low-end harmonic weight that grows with the compression). Zero latency.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

// The usual soft-knee compressor curve (dB in, dB of gain change out, <= 0).
[[nodiscard]] float kneeGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept;

namespace vca {
enum P : int { Threshold, Ratio, Knee, Attack, Release, AutoRelease, Makeup, AutoMakeup, ScHpf, Mix, Output, Tail, Character, kCount };
inline constexpr float kScHz[] = {0, 60, 90, 150, 250};
} // namespace vca
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVcaComp();

} // namespace hem::rack
