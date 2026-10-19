#pragma once
// =============================================================================
//  The HEM-Elite-Rack plug-in list (what the rack's picker, the VST3 bundle and the
//  tests iterate). Names and UIDs are permanent once released: hosts and saved sessions
//  find a plug-in by them.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace hem::rack {

enum class Face : std::uint8_t { EliteEq, Restorer, ConsoleEq, BusGlue, Tape, TubeEq, VariMu, Opto, Fet, Limiter };

struct PluginInfo {
    const char*   name;          // "ELITE EQ"
    const char*   suite;         // "MASTERING EQUALIZER" (under HYBRID ELITE MAFIA on the plate)
    const char*   tagline;       // bottom strip: "SHAPE WHAT MATTERS"
    const char*   subCategory;   // VST3 sub-category
    const char*   blurb;         // one line for the picker
    std::uint32_t processorUid[4];
    std::uint32_t controllerUid[4];
    Face          face;
    std::unique_ptr<hc::fx::Effect> (*create)();
};

[[nodiscard]] std::span<const PluginInfo> plugins() noexcept;
[[nodiscard]] const PluginInfo* findPlugin(const char* name) noexcept;

} // namespace hem::rack
