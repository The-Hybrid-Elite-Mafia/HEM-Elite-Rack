#pragma once
// The plate descriptions of the arsenal plug-ins (rackfx-private; Plugins.cpp points at them).
#include "hem/rack/Plate.hpp"

namespace hem::rack::plates {
extern const PlateSpec kSwitchblade, kConsigliere, kHeavyweight, kTheDon, kSlowBurn, kIronFist, kKnockout, kFullHouse, kWheelman,
    kSilencer, kBreathless, kTommyGun;
// the tone shop (tone/TonePlates.cpp)
extern const PlateSpec kStraightShooter, kTheBoss, kKingpin, kGoldChain, kIvories, kHighRoller, kMiddleman, kDoubleCross, kCutthroat;
// the hideout (hideout/HideoutPlates.cpp)
extern const PlateSpec kFixer, kSixShooter, kBackAlley, kDriveBy, kBodyDouble, kHustle, kInformant, kStrapped, kSafeHouse;
// the streets (streets/StreetsPlates.cpp)
extern const PlateSpec kTorch, kRacket, kSnitch, kPromotion, kWiseguys, kMadeMan, kDetective, kWitness, kLiveWire;
// the maximizers (maxim/MaximPlates.cpp)
extern const PlateSpec kBigShot, kLastWord, kHighStakes, kFastMoney, kTopDog, kQuickCash, kSweetSixteen;
} // namespace hem::rack::plates
