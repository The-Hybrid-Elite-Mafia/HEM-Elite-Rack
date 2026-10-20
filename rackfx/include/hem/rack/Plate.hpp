#pragma once
// =============================================================================
//  PlateSpec — what goes on a faceplate, as plain data, for the plug-ins whose plate is
//  laid out from a description instead of a hand-placed layout (Face::Spec).
//
//  It lives with the plug-ins (no UI types) so a plug-in's controls and its plate are described
//  in one place; hem::ui builds the actual layout from it (FaceLayouts.cpp, specFace), placing
//  each part by what the plate holds: a screen (or none), a column beside the screen, one or
//  two rows of knobs, and the left column under the plug-in's name.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <cstdint>
#include <span>

namespace hem::rack {

enum class Screen : std::uint8_t {
    None,       // no window: the knobs get the room
    Needle,     // gain-reduction needle (reductionDb)
    Spectrum,   // before / after spectrum (the effect is a SpectrumSource)
    History,    // scrolling history of meters 0 and 1 (gain change, dB) and 2 (input level)
    Readouts,   // big-number windows (PlateSpec::readouts), in a row
    Curve,      // the EQ / filter curve (PluginInfo::curve) over the music's spectrum
};

enum class KnobSize : std::uint8_t { Small, Normal, Big };

struct PlateKnob {
    int         param;
    const char* caption;
    const char* lo = "";     // scale end labels (left / right of the arc)
    const char* hi = "";
    KnobSize    size = KnobSize::Normal;
};

// One choice parameter shown as a row (or column) of buttons, one per choice.
struct PlateChoice {
    int         param;
    const char* caption;
};

struct PlateToggle {
    int         param;
    const char* caption;
};

// A big-number window: one of the effect's meters.
// fmt: "" = 0.0, "+" = signed, "+2" = signed with 2 decimals, "0" = whole numbers,
// "note" = a MIDI note number shown as a name (57 = A3), "key" = 0..11 C..B major, 12..23 C..B minor.
struct PlateReadout {
    int         meter;
    const char* caption;
    const char* unit;
    const char* fmt = "";
};

// Multiband plates: one column of controls per band. The parameters of band b are
// first + b * stride + offset (knobs / steppers / toggles give the offset).
struct PlateBands {
    int count = 0;                              // columns
    int first = 0;                              // band 0's first parameter
    int stride = 0;                             // parameters per band
    std::span<const PlateKnob>   knobs{};       // per band, stacked (two side by side when there are more than three)
    std::span<const PlateChoice> steppers{};    // per band, under the knobs
    std::span<const PlateToggle> toggles{};     // per band, side by side at the bottom
    std::span<const PlateKnob>   borders{};     // knobs on the borders between the columns (absolute parameters)
    int meter = -1;                             // band b's readout on top is meter `meter + b` (-1: none)
    bool lastIsMaster = false;                  // the last column is MASTER: its knobs only (offsets for every band)
};

struct PlateSpec {
    const char* note1 = "";             // the two lines under the plug-in's name
    const char* note2 = "";
    Screen      screen = Screen::None;
    std::span<const PlateReadout> readouts{};   // Readouts screen (up to 4); with Needle: up to 2 beside it
    std::span<const PlateChoice>  choices{};    // button groups: beside the screen, or across the top
    std::span<const PlateToggle>  toggles{};    // ON / OFF switches, in pairs, after the button groups
    std::span<const PlateKnob>    row1{};       // the main row of knobs
    std::span<const PlateKnob>    row2{};       // an optional second row
    std::span<const PlateChoice>  left{};       // choice lists down the left column, under the name
    std::span<const PlateToggle>  leftToggles{};
    std::span<const PlateChoice>  steppers{};    // compact ◀ value ▶ choices, down the left column
    const char* historyA = "";                  // History screen: names of meters 0 and 1 ("" = GR 1 / GR 2)
    const char* historyB = "";
    const PlateBands* bands = nullptr;          // multiband: the columns; row1 and toggles go on the top row
    bool        reportsReduction = false;       // publishes gain reduction (hosts get a GR meter)
    int         meterSelect = -1;               // Needle: a choice IN / GR / OUT says what the needle reads
    int         vuMeter = -1;                   // ... the input VU level is this meter, the output VU level the next
    void (*demo)(hc::fx::Effect&) = nullptr;    // settings for the CI screenshot (something to see)
};

} // namespace hem::rack
