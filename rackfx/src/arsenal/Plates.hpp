#pragma once
// The plate descriptions of the arsenal plug-ins (rackfx-private; Plugins.cpp points at them).
#include "hem/rack/Plate.hpp"

namespace hem::rack::plates {
extern const PlateSpec kKnockout, kHaymaker, kFourKings, kBackbone, kFullHouse, kWheelman, kChauffeur, kSilencer, kTightLips, kOmerta,
    kBreathless, kTommyGun;
// the hardware-style compressors (arsenal/CompPlates.cpp)
extern const PlateSpec kTerritory, kStraightEdge, kConsigliere, kStoneCold, kHeavyweight, kTheDon, kSlowBurn, kLoneWolf, kIronFist, kQuickDraw, kOldMoney;
// the tone shop (tone/TonePlates.cpp)
extern const PlateSpec kLowRider, kUndertow, kDeepPockets, kLowProfile, kTunnelVision;
extern const PlateSpec kStraightShooter, kBottomLine, kTenToes, kFourCorners, kSmoothTalker, kChairman, kKingpin, kGoldChain, kIvories, kHighRoller, kMiddleman, kDoubleCross, kCutthroat;
extern const PlateSpec kFoundry, kBoardroom, kMidtown, kCoil, kMixtape, kCrateDigger, kShredder;
extern const PlateSpec kHeavyHitter, kReelMoney, kHothead, kHotWire, kBottomFeeder, kMold, kForger;
// the hideout (hideout/HideoutPlates.cpp)
extern const PlateSpec kFixer, kDeadDrop, kBackAlley, kDriveBy, kHustle, kSwerve, kBigHouse, kSpeakeasy, kMouthpiece, kSlinger, kStreetTeam, kStrapped, kSafeHouse, kFingerprint;
// the streets (streets/StreetsPlates.cpp)
extern const PlateSpec kTorch, kSnitch, kPromotion, kWiseguys, kMadeMan, kDetective, kWitness, kLiveWire;
// the maximizers (maxim/MaximPlates.cpp)
extern const PlateSpec kBigShot, kLastWord, kHighStakes, kFastMoney, kTopDog, kQuickCash, kSweetSixteen;
} // namespace hem::rack::plates
