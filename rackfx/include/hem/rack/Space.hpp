#pragma once
// =============================================================================
//  HEM-Elite-Rack space: reverbs and echoes (our own designs).
//
//  HEM CATHEDRAL / HEM BACKROOM / HEM CHROME — one 8-line feedback-delay-network reverb, voiced three ways.
//    PRE-DELAY, SIZE, DECAY (seconds to fall 60 dB), DAMPING (highs die sooner), EARLY (HALL and
//    ROOM: the first reflections) or DIFFUSION (PLATE: how quickly it turns dense), MOD (slow
//    movement in the tail: lusher, no metallic ring), WIDTH, LOW CUT and HIGH CUT on the reverb,
//    MIX. The dry sound is never delayed: zero latency.
//  HEM COIL — a spring tank: DECAY, TENSION (the spring's length: the "boing" pitch), DRIP (the
//    chirpy, dispersive splash), TONE, MIX. Zero latency.
//  HEM WIRETAP — a tape loop with playback heads: TIME 40..1000 ms (changes bend the pitch, like
//    tape), HEADS 1 / 1+2 / 1+2+3 (the heads sit at 1x, 2x and 3x TIME), FEEDBACK up to 110 % (the
//    tape saturates, so it runs away musically instead of blowing up), TONE, WOBBLE, SATURATION, MIX.
//  HEM REPEAT OFFENDER — clean delay: TIME in ms or a note value at BPM, FEEDBACK, PING-PONG,
//    STEREO OFFSET, LOW CUT and HIGH CUT in the repeats, MIX. Zero latency.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace verb {
// Shared by HALL, ROOM and PLATE. Index 4 is EARLY (HALL, ROOM) or DIFFUSION (PLATE).
enum P : int { PreDelay, Size, Decay, Damping, Early, Mod, Width, LowCut, HighCut, Mix, kCount };
enum class Kind : int { Hall, Room, Plate };
} // namespace verb
namespace spring { enum P : int { Decay, Tension, Drip, Tone, Mix, kCount }; }
namespace tapeecho {
enum P : int { Time, Heads, Feedback, Tone, Wobble, Saturation, Mix, kCount };
} // namespace tapeecho
namespace ddelay {
enum P : int { Division, Bpm, Time, Feedback, PingPong, Offset, LowCut, HighCut, Mix, kCount };
inline constexpr float kBeats[] = {0.0f, 1.0f, 0.5f, 0.75f, 0.25f, 2.0f / 3.0f, 1.0f / 3.0f, 2.0f};   // FREE, 1/4, 1/8, 1/8 D, 1/16, 1/4 T, 1/8 T, 1/2
[[nodiscard]] float delayMs(int division, float bpm, float freeMs) noexcept;
} // namespace ddelay

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHall();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeRoom();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePlate();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSpring();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTapeEcho();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDigitalDelay();

} // namespace hem::rack
