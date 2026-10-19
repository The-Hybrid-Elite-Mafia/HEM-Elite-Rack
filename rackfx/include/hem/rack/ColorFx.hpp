#pragma once
// =============================================================================
//  HEM-Elite-Rack colour plug-ins (our own designs). Saturation stages run 4x oversampled.
//
//  HEM FURNACE — a tube stage: DRIVE (how hard), BIAS (more even harmonics = warmer, fuller),
//    TONE (darker .. brighter after the tube), MIX, OUTPUT. Quiet signals pass at the same level;
//    drive changes the colour, not the volume. Latency 23 samples.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace tubesat { enum P : int { Drive, Bias, Tone, Mix, Output, kCount }; }
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTubeSat();

} // namespace hem::rack
