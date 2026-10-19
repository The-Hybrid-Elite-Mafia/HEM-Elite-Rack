#pragma once
// =============================================================================
//  The chain as data: what is in each slot (plug-in name, ON, every parameter), as text for
//  the settings file, and an offline render of a song through it (WAV export).
//  Message / worker threads only.
// =============================================================================
#include "RackEngine.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hem::app {

struct SlotState {
    int         slot = 0;
    std::string name;                                    // "ELITE EQ"
    bool        on = true;
    std::vector<std::pair<std::string, float>> values;   // parameter id -> plain value
};

// The registered name of a plug-in object ("ELITE EQ"), "" if it is not one of ours.
[[nodiscard]] const char* pluginName(const hc::fx::Effect& e);
// What the chain holds right now (reads the live plug-ins' atomics).
[[nodiscard]] std::vector<SlotState> snapshotChain(const RackProcessor& rack);
// One line per used slot:  slot1=ELITE EQ|on=1|b1on=1|b1freq=120 ...
[[nodiscard]] std::string chainToText(const std::vector<SlotState>& chain);
// Unknown plug-ins and parameter ids are skipped (`note` says what).
[[nodiscard]] std::vector<SlotState> chainFromText(const std::string& text, std::string* note = nullptr);
// A new plug-in with the slot's settings, prepared for the rack (48 kHz, kChunk). Null if unknown.
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePlugin(const SlotState& state);
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePlugin(const char* name);

// Renders `song` through fresh copies of the chain (slots that are OFF are left out), with the
// chain's latency removed so the result lines up with the song sample for sample.
// progress(0..1) may return false to cancel. Returns false (and `error`) on cancel / failure.
bool renderOffline(const Song& song, const std::vector<SlotState>& chain, std::vector<float>& outL, std::vector<float>& outR,
                   const std::function<bool(float)>& progress, std::string* error);

} // namespace hem::app
