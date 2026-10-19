#pragma once
// =============================================================================
//  HEM THE FAMILY — a mix-bus compressor in the spirit of the classic console bus compressors
//  (our own design): a few dB of gain reduction that pulls a mix together.
//
//    THRESHOLD  -40..0 dB          RATIO   2:1, 4:1, 10:1
//    ATTACK     0.1, 0.3, 1, 3, 10, 30 ms
//    RELEASE    0.1, 0.3, 0.6, 1.2 s or AUTO (short after short peaks, longer when it has been
//               working hard for a while — the pumping stays musical)
//    MAKEUP     0..20 dB           MIX     0..100 % (parallel / "New York" compression)
//    SC HPF     OFF, 60, 90, 150 Hz: the detector ignores the low end, so the kick and the bass
//               don't make the whole mix duck
//    OUTPUT
//  Detector: stereo-linked (both sides get the same gain, so the image never moves), soft knee
//  6 dB wide. Zero latency. The gain reduction (dB, <= 0) is published for the needle meter.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace glue {
enum P : int { Threshold, Ratio, Attack, Release, Makeup, Mix, ScHpf, Output, kCount };
inline constexpr float kRatios[] = {2.0f, 4.0f, 10.0f};
inline constexpr float kAttackMs[] = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
inline constexpr float kReleaseS[] = {0.1f, 0.3f, 0.6f, 1.2f};   // index 4 = AUTO
inline constexpr float kScHpfHz[] = {0.0f, 60.0f, 90.0f, 150.0f};   // 0 = OFF
inline constexpr float kKneeDb = 6.0f;
// The static curve: gain change (dB, <= 0) for a detector level in dB.
[[nodiscard]] float gainComputerDb(float levelDb, float thresholdDb, float ratio) noexcept;
} // namespace glue

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBusGlue();

} // namespace hem::rack
