#pragma once
// =============================================================================
//  RackView — the HEM-Elite-Rack window around the faceplate: top bar (OPEN SONG, EXPORT WAV,
//  audio device), transport (play / stop, song overview you can click to jump, LIVE INPUT with MONITOR,
//  RECORD and its meter, A / B, MATCH LOUDNESS, LOOP, output meter), the chain (8 slots: select, ON, move, remove, + ADD) and the
//  plug-in picker. The selected plug-in's faceplate is drawn by hem::ui::FaceView in faceArea(), with an X CLOSE
//  button in the strip above its top-right corner (closing hides the plate only; the plug-in keeps running).
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
    // live input
    std::string input = "INPUT: OFF";       // what INPUT is set to
    bool  inputOn = false, monitor = true, recording = false;
    float inputDb = -120.0f;                 // its peak
    double recSeconds = 0.0;
    // chain
    std::array<SlotView, 8> slots{};
    int  selected = -1;
    bool faceOpen = false;                   // a plate is showing (it has its CLOSE button)
    bool picker = false;
    struct PickerItem { std::string name, blurb; int group = 0; int plugin = 0; };
    std::vector<PickerItem> pickerItems;     // every plug-in; the picker shows the chosen tab's
    std::vector<std::string> pickerTabs;     // "EQ", "DYNAMICS", ...
    int pickerTab = 0;
    // status
    std::string device, status;
    bool  alert = false;
    float exportProgress = -1.0f;            // 0..1 while exporting
};

enum class Action : std::uint8_t {
    None, OpenSong, Export, Device, Play, Stop, Seek, PlayA, PlayB, Match, Loop, Input, Monitor, Record,
    SlotSelect, SlotOn, SlotUp, SlotDown, SlotRemove, AddOpen, AddPlugin, PickerClose, PickerTab, Face, FaceClose,
};
struct Hit {
    Action action = Action::None;
    int    index = -1;      // slot / plug-in (AddPlugin: index into rack::plugins()) / picker tab
    float  value = 0.0f;    // Seek: 0..1
    [[nodiscard]] bool operator==(const Hit& o) const noexcept { return action == o.action && index == o.index; }
};

inline constexpr int kOverviewColumns = 1200;

class RackView {
public:
    static constexpr float kW = 2000.0f, kH = 1250.0f;

    [[nodiscard]] static Rect faceArea() noexcept { return {420, 262, 1560, 968}; }
    // The plate's X CLOSE button: in the strip between faceArea()'s top and the plate's top edge.
    [[nodiscard]] static Rect faceClose() noexcept;
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
