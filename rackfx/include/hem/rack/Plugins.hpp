#pragma once
// =============================================================================
//  The HEM-Elite-Rack plug-in list (what the rack's picker, the VST3 bundle and the
//  tests iterate). Names and UIDs are permanent once released: hosts and saved sessions
//  find a plug-in by them.
// =============================================================================
#include "hem/rack/Plate.hpp"
#include "hybrid/fx/Fx.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace hem::rack {

enum class Face : std::uint8_t { EliteEq, Restorer, BusGlue, Tape, TubeEq, Limiter,
                                Width, Exciter, Meter,
                                VcaComp, TubeSat, Hall, Plate, DigitalDelay,
                                Chorus, Flanger, Phaser, Tremolo, Rotary, AutoFilter,
                                Sub, VocalStrip, Hitman, Compander,
                                Spec,     // laid out from PluginInfo::plate
                                Channel,     // INSIDE MAN's channel-strip plate
                                Movers,      // THE SETUP's filter-with-movers plate
                                Taps,        // SIX SHOOTER's six-tap plate
                                Voices,      // BODY DOUBLE's four-voice plate
                                Keys,        // THE INFORMANT's vocoder plate with its 48-key keyboard
                                Notches,     // COLD CASE's notch / sweep / loop / wet plate
                                Pawn,        // PAWN SHOP's four modules and master
                                Audit };     // THE AUDITOR's readouts and settings

// Where a plug-in sits in the rack's picker.
enum class Group : std::uint8_t { Eq, Dynamics, Color, Space, Movement, Tools, kCount };
[[nodiscard]] const char* groupName(Group g) noexcept;   // "EQ", "DYNAMICS", ...

// For the EQs whose faceplate draws their curve: the exact response the audio runs, computed
// from plain parameter values only (so a VST3 editor, which has no effect object, can draw it too).
struct CurveInfo {
    double (*db)(const float* plain, double sampleRate, double hz) noexcept;
    int (*dots)(const float* plain, double* hz, int max) noexcept;   // a dot on the curve per band
    float range;                                                      // ± dB shown
    double (*dbRight)(const float* plain, double sampleRate, double hz) noexcept = nullptr;   // the right side's curve, when it has its own
    int scaleParam = -1;      // a choice: 0 = ±12 dB shown, 1 = ±30 dB (instead of `range`)
    int spectrumParam = -1;   // a switch: the music behind the curve on / off
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
    const PlateSpec* plate = nullptr;   // Face::Spec: what the plate holds
};

[[nodiscard]] std::span<const PluginInfo> plugins() noexcept;
[[nodiscard]] const PluginInfo* findPlugin(const char* name) noexcept;

} // namespace hem::rack
