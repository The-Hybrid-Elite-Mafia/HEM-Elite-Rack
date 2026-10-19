#pragma once
// =============================================================================
//  HEM-Elite-Rack movement: modulation effects (our own designs). All zero latency.
//
//  HEM CHORUS — 1, 2 or 3 voices of gently moving delay: RATE, DEPTH, DELAY, VOICES, WIDTH, MIX.
//  HEM FLANGER — a short swept delay with feedback (the jet sweep): RATE, DEPTH, MANUAL (where the
//    sweep sits), FEEDBACK (+ or -), MIX.
//  HEM PHASER — 4 / 6 / 8 / 12 all-pass stages swept around a CENTER: RATE, DEPTH, STAGES,
//    FEEDBACK, MIX.
//  HEM TREMOLO — volume (or, with PAN, left-right) pulsing: RATE, DEPTH, SHAPE SINE / TRIANGLE /
//    SQUARE (with soft edges), PAN.
//  HEM ROTARY — a rotating speaker cabinet: SPEED SLOW / FAST (the horn and the drum speed up and
//    slow down at their own pace, like the real thing), DRIVE (its tube amp), BALANCE (horn vs drum),
//    SPREAD (the two mics), MIX.
//  HEM AUTO FILTER — a resonant low-pass / band-pass / high-pass whose CUTOFF follows the music
//    (ENVELOPE, + opens, - closes) and / or an LFO (RATE, DEPTH in octaves): RESONANCE, MIX.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace chorus { enum P : int { Rate, Depth, Delay, Voices, Width, Mix, kCount }; }
namespace flanger { enum P : int { Rate, Depth, Manual, Feedback, Mix, kCount }; }
namespace phaser {
enum P : int { Rate, Depth, Center, Stages, Feedback, Mix, kCount };
inline constexpr int kStages[] = {4, 6, 8, 12};
} // namespace phaser
namespace tremolo { enum P : int { Rate, Depth, Shape, Pan, kCount }; }
namespace rotary { enum P : int { Speed, Drive, Balance, Spread, Mix, kCount }; }
namespace autofilter { enum P : int { Type, Cutoff, Resonance, Envelope, Rate, Depth, Mix, kCount }; }

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeChorus();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFlanger();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePhaser();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTremolo();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeRotary();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeAutoFilter();

} // namespace hem::rack
