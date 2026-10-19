#pragma once
// =============================================================================
//  RackView — the HEM-Elite-Rack window around the faceplate: top bar (OPEN SONG, EXPORT WAV,
//  audio device), transport (play / stop, song overview you can click to jump, A / B, MATCH
//  LOUDNESS, LOOP, output meter), the chain (8 slots: select, ON, move, remove, + ADD) and the
//  plug-in picker. The selected plug-in's faceplate is drawn by hem::ui::FaceView in faceArea().
//  Design units: kW × kH, letterboxed into the window. Draws through the Renderer only and
//  reads a plain view state, so it never touches audio objects.
// =============================================================================
#include "hybrid/ui/Geometry.hpp"
#include "hybrid/ui/Renderer.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hem::app {

using hc::ui::Rect;

struct SlotView {
    bool        used = false;
    bool        on = true;
    std::string name, suite;
};

struct RackViewState {
    // song
    std::string songName;                    // empty = no song
    std::string songInfo;                    // "WAV 24-bit · 44.1 kHz → 48 kHz · 3:41"
    bool        loading = false;
    float       loadProgress = 0.0f;
    const std::vector<float>* overview = nullptr;   // min, max pairs (kOverviewColumns)
    double      position = 0, length = 0;    // seconds
    bool        playing = false, loop = false, original = false, match = true;
    // meters
    float peakDb[2] = {-120.0f, -120.0f};
    float outLufs = -144.0f, matchDb = 0.0f;
    float latencyMs = 0.0f, cpuPct = 0.0f;
    // chain
    std::array<SlotView, 8> slots{};
    int  selected = -1;
    bool picker = false;
    std::vector<std::pair<std::string, std::string>> pickerItems;   // name, blurb
    // status
    std::string device, status;
    bool  alert = false;
    float exportProgress = -1.0f;            // 0..1 while exporting
};

enum class Action : std::uint8_t {
    None, OpenSong, Export, Device, Play, Stop, Seek, PlayA, PlayB, Match, Loop,
    SlotSelect, SlotOn, SlotUp, SlotDown, SlotRemove, AddOpen, AddPlugin, PickerClose, Face,
};
struct Hit {
    Action action = Action::None;
    int    index = -1;      // slot / picker item
    float  value = 0.0f;    // Seek: 0..1
    [[nodiscard]] bool operator==(const Hit& o) const noexcept { return action == o.action && index == o.index; }
};

inline constexpr int kOverviewColumns = 1200;

class RackView {
public:
    static constexpr float kW = 2000.0f, kH = 1250.0f;

    [[nodiscard]] static Rect faceArea() noexcept { return {420, 262, 1560, 968}; }
    // Where the 1672 × 941 faceplate sits inside faceArea(), and its zoom.
    static void facePlacement(float& originX, float& originY, float& zoom) noexcept;

    [[nodiscard]] Hit hit(float x, float y, const RackViewState& s) const noexcept;
    // Draws everything except the faceplate (drawn between drawBack and drawFront by the caller).
    void drawBack(hc::ui::Renderer& r, const RackViewState& s, const Hit& hover) const;
    void drawFront(hc::ui::Renderer& r, const RackViewState& s, const Hit& hover) const;   // picker over the face

private:
    void button(hc::ui::Renderer& r, Rect b, std::string_view text, bool lit, bool hot, bool dim = false) const;
};

[[nodiscard]] std::vector<float> makeOverview(const std::vector<float>& left, const std::vector<float>& right);

} // namespace hem::app
