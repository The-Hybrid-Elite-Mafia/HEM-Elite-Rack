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

enum class Face : std::uint8_t { EliteEq, Restorer, ConsoleEq, BusGlue, Tape, TubeEq, VariMu, Opto, Fet, Limiter,
                                DeEsser, Width, Clipper, Exciter, Transient, Meter,
                                BritPre, AmericanEq, MidEq, AirEq, GraphicEq, Tilt };

// Where a plug-in sits in the rack's picker.
enum class Group : std::uint8_t { Eq, Dynamics, Color, Space, Movement, Tools, kCount };
[[nodiscard]] const char* groupName(Group g) noexcept;   // "EQ", "DYNAMICS", ...

// For the EQs whose faceplate draws their curve: the exact response the audio runs, computed
// from plain parameter values only (so a VST3 editor, which has no effect object, can draw it too).
struct CurveInfo {
    double (*db)(const float* plain, double sampleRate, double hz) noexcept;
    int (*dots)(const float* plain, double* hz, int max) noexcept;   // a dot on the curve per band
    float range;                                                      // ± dB shown
};

struct PluginInfo {
    const char*   name;          // "ELITE EQ"
    const char*   suite;         // "MASTERING EQUALIZER" (under HYBRID ELITE MAFIA on the plate)
    const char*   tagline;       // bottom strip: "SHAPE WHAT MATTERS"
    const char*   subCategory;   // VST3 sub-category
    const char*   blurb;         // one line for the picker
    std::uint32_t processorUid[4];
    std::uint32_t controllerUid[4];
    Face          face;
    Group         group;
    std::unique_ptr<hc::fx::Effect> (*create)();
    const CurveInfo* curve = nullptr;
};

[[nodiscard]] std::span<const PluginInfo> plugins() noexcept;
[[nodiscard]] const PluginInfo* findPlugin(const char* name) noexcept;

} // namespace hem::rack
