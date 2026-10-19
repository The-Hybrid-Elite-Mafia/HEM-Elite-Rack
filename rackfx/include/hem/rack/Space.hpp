#pragma once
// =============================================================================
//  HEM-Elite-Rack space: reverbs and echoes (our own designs).
//
//  HEM CATHEDRAL / HEM CHROME — one 8-line feedback-delay-network reverb, voiced two ways.
//    PRE-DELAY, SIZE, DECAY (seconds to fall 60 dB), DAMPING (highs die sooner), EARLY (CATHEDRAL: the
//    first reflections) or DIFFUSION (CHROME: how quickly it turns dense), MOD (slow
//    movement in the tail: lusher, no metallic ring), WIDTH, LOW CUT and HIGH CUT on the reverb,
//    MIX. The dry sound is never delayed: zero latency.
//  HEM REPEAT OFFENDER — clean delay: TIME in ms or a note value at BPM, FEEDBACK, PING-PONG,
//    STEREO OFFSET, LOW CUT and HIGH CUT in the repeats, MIX. Zero latency.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace verb {
// Shared by CATHEDRAL and CHROME. Index 4 is EARLY (CATHEDRAL) or DIFFUSION (CHROME).
enum P : int { PreDelay, Size, Decay, Damping, Early, Mod, Width, LowCut, HighCut, Mix, kCount };
enum class Kind : int { Hall, Plate };
} // namespace verb
namespace ddelay {
enum P : int { Division, Bpm, Time, Feedback, PingPong, Offset, LowCut, HighCut, Mix, kCount };
inline constexpr float kBeats[] = {0.0f, 1.0f, 0.5f, 0.75f, 0.25f, 2.0f / 3.0f, 1.0f / 3.0f, 2.0f};   // FREE, 1/4, 1/8, 1/8 D, 1/16, 1/4 T, 1/8 T, 1/2
[[nodiscard]] float delayMs(int division, float bpm, float freeMs) noexcept;
} // namespace ddelay

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHall();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePlate();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDigitalDelay();

} // namespace hem::rack
