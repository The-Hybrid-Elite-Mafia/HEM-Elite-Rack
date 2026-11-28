#pragma once
// =============================================================================
//  HEM staging list — the 47 new HEM engines (New plugins 01–52, build roster of 2026-11-27).
//  Kept apart from the rack's plug-in list on purpose: these have no faceplate art yet, so they
//  are not in hem::rack::plugins(), not in the rack app and not in the VST3 bundle. They run
//  every DSP and parameter test in hem_rack_tests (tests/newline_tests.cpp).
//  When their art lands they move into the rack list and the faceplate rule applies to them.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>
#include <span>

namespace hem::staging {

struct StagingInfo {
    int         number;        // New-plugins number (01–52)
    const char* name;          // HEM name
    const char* tagline;       // what it is, one line
    const char* subCategory;   // VST3 sub-category for when it ships
    std::unique_ptr<hc::fx::Effect> (*create)();
};

// Built so far, in New-plugins number order.
[[nodiscard]] std::span<const StagingInfo> plugins() noexcept;
[[nodiscard]] const StagingInfo* find(const char* name) noexcept;

// Batch 1
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDynamicEq8();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVpra();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSubArchitect();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVoiceCircuit();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeNoteForge();
// Batch 2
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeThroatShift();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVoiceVeil();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLevelPilot();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSipa();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLeadSuite();
// Batch 3
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeChoirForge();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVoicePress();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeResonanceGuard();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBandMotion();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGraphicForge();
// Batch 4
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSilkContour();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGlowLevel();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStrikeClamp();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBusFoundry();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBandCouncil();
// Batch 5
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeImpactGuide();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeChamberPlate();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeEchoForge();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMotionField();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDepthMatrix();
// Batch 6
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeConsoleCore();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeChannelMatrix();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeEmberStrip();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMixBond();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeReelAura();
// Batch 7
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePeakCitadel();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeImpactCrown();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMasterCouncil();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHitContour();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDrumKey();
// Batch 8
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeAirEmber();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBassBeacon();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeRoomEraser();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMonitorSphere();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDeEsser();
// Batches 9 + 10
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMasterLimiter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeOpticalComp();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeParametricEq();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHemReverb();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStereoDelay();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTubePreamp();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVcaComp();

// NoteForge's live readouts (meter indices), used by its faceplate and the pitch-accuracy test.
namespace noteforge {
inline constexpr int kMeterInDb = 0, kMeterOutDb = 1, kMeterDetectedHz = 2, kMeterTargetHz = 3, kMeterOffsetCents = 4;
}

} // namespace hem::staging
