#pragma once
// The plate descriptions of the arsenal plug-ins (rackfx-private; Plugins.cpp points at them).
#include "hem/rack/Plate.hpp"

namespace hem::rack::plates {
extern const PlateSpec kSwitchblade, kConsigliere, kHeavyweight, kTheDon, kSlowBurn, kIronFist, kKnockout, kFullHouse, kWheelman,
    kBigShot, kSilencer, kBreathless, kTommyGun;
// the tone shop (tone/TonePlates.cpp)
extern const PlateSpec kStraightShooter, kTheBoss, kKingpin, kGoldChain, kIvories, kHighRoller, kMiddleman, kDoubleCross, kCutthroat;
} // namespace hem::rack::plates
