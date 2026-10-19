#pragma once
// =============================================================================
//  The HEM-Elite-Rack faceplates: brushed gunmetal, teal light, the HEM crown badge in the
//  top-left corner, the ghost in the top-right corner, "EST. 2024".
//
//  Layers (same split as The Hybrid Core's FxPanel, so the app, the VST3 editor and the
//  snapshot tool share them):
//    faceLayout()      where everything goes, in plate units (1672 × 941)       — pure
//    renderRackPlate() the static metal plate with recesses, screws, crown, ghost  — CPU, once
//    FaceView          live parts: text, knobs, buttons, spectrum, EQ curve       — Renderer
//    FaceController    mouse / wheel → parameter edits through hc::ui::PanelHost  — pure
//  Everything is drawn at a zoom (plate units × zoom = renderer design units); text picks the
//  matching larger style, so it stays sharp at any size.
// =============================================================================
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hybrid/ui/FxPanel.hpp"
#include "hybrid/ui/Geometry.hpp"
#include "hybrid/ui/Image.hpp"
#include "hybrid/ui/Renderer.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hem::ui {

using hc::ui::PanelHost;
using hc::ui::Rect;
using hc::ui::Rgba;

inline constexpr float kPlateW = 1672.0f, kPlateH = 941.0f;

struct Theme {
    Rgba teal     = hc::ui::rgb8(0x22D8DD);   // the light: arcs, values, the AFTER spectrum, lit buttons
    Rgba tealDeep = hc::ui::rgb8(0x0E8E96);
    Rgba silver   = hc::ui::rgb8(0xD8DEE2);   // engraved titles
    Rgba label    = hc::ui::rgb8(0xB9C2C7);   // captions
    Rgba dim      = hc::ui::rgb8(0x6C767C);   // scale labels, unused controls
    Rgba before   = hc::ui::rgb8(0xA9B0B5);   // the BEFORE spectrum
    Rgba gold     = hc::ui::rgb8(0xE7B54A);   // mid bands
    Rgba violet   = hc::ui::rgb8(0xA87BFF);   // side bands
    Rgba red      = hc::ui::rgb8(0xFF5A5A);   // left / right bands
};

// ---- live data a faceplate shows besides the parameters ------------------------------
class FaceFeed {
public:
    virtual ~FaceFeed() = default;
    [[nodiscard]] virtual bool  hasSpectrum() const { return false; }
    [[nodiscard]] virtual float before(int) const { return -120.0f; }   // display bin, dB
    [[nodiscard]] virtual float after(int) const { return -120.0f; }
    virtual void setSpectrumActive(bool) {}
    [[nodiscard]] virtual bool  hasNoiseFloor() const { return false; }
    [[nodiscard]] virtual float noiseFloor(int) const { return -120.0f; }
    [[nodiscard]] virtual float removedDb() const { return 0.0f; }
    [[nodiscard]] virtual float dynamicGainDb(int) const { return 0.0f; }
    [[nodiscard]] virtual float autoGainDb() const { return 0.0f; }
    [[nodiscard]] virtual double sampleRate() const { return 48000.0; }
};

// FaceFeed straight onto a running hem::rack effect (the rack app, snapshots, tests).
class EffectFeed final : public FaceFeed {
public:
    explicit EffectFeed(hc::fx::Effect& e) noexcept;
    bool  hasSpectrum() const override { return spectrum_ != nullptr; }
    float before(int b) const override;
    float after(int b) const override;
    void  setSpectrumActive(bool on) override;
    bool  hasNoiseFloor() const override { return noise_ != nullptr; }
    float noiseFloor(int b) const override;
    float removedDb() const override;
    float dynamicGainDb(int band) const override;
    float autoGainDb() const override;
    double sampleRate() const override { return rate_; }
    void setSampleRate(double fs) noexcept { rate_ = fs; }

private:
    rack::SpectrumSource*     spectrum_ = nullptr;
    rack::NoiseProfileSource* noise_ = nullptr;
    rack::EliteEqView*        eq_ = nullptr;
    double rate_ = 48000.0;
};

// ---- layout ----------------------------------------------------------------------------
enum class WKind : std::uint8_t {
    Knob,          // big or small dial, vertical drag
    Toggle,        // ON / OFF button
    ChoiceButton,  // one option of a choice parameter (lit when selected)
    Stepper,       // ◀ value ▶ for a choice parameter
    RangeSlider,   // two handles (param = low, param2 = high), log frequency
    Bypass,        // the round ON / BYPASS button (host bypass)
    Spectrum,      // before / after spectrum (+ noise curve when the feed has one)
    EqGraph,       // ELITE EQ: spectrum + curve + draggable band nodes
    GraphRange,    // ELITE EQ: ±3 / ±6 / ±12 / ±30 dB buttons (choice = index)
    BandButton,    // ELITE EQ: band 1..16 selector (choice = band)
};

struct Widget {
    WKind       kind = WKind::Knob;
    Rect        r;                 // plate units (knob: the dial's square)
    int         param = -1;        // fixed parameter, or -1 when `control` follows the selected band
    int         param2 = -1;
    int         control = -1;      // ELITE EQ band control (eq::Control) for the selected band
    int         choice = -1;
    std::string caption;           // above the control
    std::string lo, hi;            // knob scale labels ("0" .. "10", "LO" .. "HI")
    bool        small = false;
};

struct Recess { Rect r; float radius = 10; float depth = 8; bool round = false; bool glass = false; };

struct FaceLayout {
    rack::Face  face = rack::Face::Restorer;
    std::string name, suite, tagline;
    std::vector<std::string> sideNotes;   // right column ("LESS NOISE / MORE MUSIC" ...)
    std::vector<std::string> leftNotes;   // under the big name ("CLEANER AUDIO", "TRUE DETAIL")
    Rect header, crown, ghost, title, display, footer, footerPlate;
    std::vector<Recess> recesses;
    std::vector<std::array<float, 2>> screws;
    std::vector<std::array<float, 4>> grooves;   // x0, y0, x1, y1
    std::vector<Widget> widgets;
};

[[nodiscard]] FaceLayout faceLayout(const rack::PluginInfo& info);

// ---- the plate --------------------------------------------------------------------------
struct BrandArt {
    hc::ui::Image crown;   // straight-alpha sRGB (assets/brand/crown_badge.png)
    hc::ui::Image ghost;   // (assets/brand/ghost.png)
    [[nodiscard]] static std::optional<BrandArt> load(const std::filesystem::path& assetDir, std::string* error);
};
// Opaque straight sRGB, (kPlateW × zoom × pixelsPerUnit) wide. Deterministic.
[[nodiscard]] hc::ui::Image renderRackPlate(const FaceLayout& layout, const BrandArt& art, float pixelsPerUnit);

// ---- interaction ---------------------------------------------------------------------------
struct FaceState {
    int selectedBand = 0;       // ELITE EQ
    int graphRange = 2;         // index into kGraphRanges
    int hover = -1;             // widget index
    int active = -1;            // widget being dragged
    int hoverNode = -1;         // EQ band under the mouse
    int dragNode = -1;          // EQ band being dragged
};
inline constexpr std::array<float, 4> kGraphRanges{3.0f, 6.0f, 12.0f, 30.0f};

// Effective parameter of a widget for the current band selection.
[[nodiscard]] int widgetParam(const Widget& w, const FaceState& st) noexcept;

// `params` is the effect's parameter table (hc::fx::Effect::params()); the controller needs the
// defaults (double-click) and the ranges, the view the band settings for the EQ curve.
using ParamTable = std::span<const hc::fx::ParamInfo>;

class FaceController {
public:
    FaceController(const FaceLayout& layout, ParamTable params, PanelHost& host, FaceFeed& feed, FaceState& state) noexcept
        : L_(layout), params_(params), host_(host), feed_(feed), st_(state) {}
    // Coordinates in plate units.
    void mouseDown(float x, float y, bool doubleClick, bool fine);
    void mouseMove(float x, float y, bool fine);
    void mouseUp();
    void wheel(float x, float y, float notches, bool fine);
    static constexpr float kDragForFullRange = 360.0f;   // plate units

private:
    int  hit(float x, float y) const noexcept;
    void setNorm(int param, float v);
    void step(int param, int delta);
    // EQ graph helpers
    [[nodiscard]] Rect plot() const noexcept;
    [[nodiscard]] int nodeAt(float x, float y) const noexcept;
    void graphDown(float x, float y, bool doubleClick);
    void graphDrag(float x, float y, bool fine);
    [[nodiscard]] float plain(int param) const;
    void setPlain(int param, float v);
    void edit(int param, float normalized);   // begin + set + end (clicks)
    [[nodiscard]] int firstFreeBand() const;

    const FaceLayout& L_;
    ParamTable        params_;
    PanelHost&        host_;
    FaceFeed&         feed_;
    FaceState&        st_;
    float lastX_ = 0, lastY_ = 0, dragValue_ = 0, dragValue2_ = 0;
    int   dragHandle_ = 0;   // range slider: 1 = low, 2 = high
};

// ---- drawing -----------------------------------------------------------------------------------
// Draws a faceplate at (originX, originY) with every plate unit = `zoom` renderer design units.
// The plate texture (renderRackPlate, uploaded with Renderer::setPanel) is drawn first unless
// plate = false (it is already in the renderer's static layer). Reads values through the
// PanelHost and FaceFeed only; never touches DSP objects.
class FaceView {
public:
    FaceView(const FaceLayout& layout, ParamTable params, Theme theme = {}) noexcept : L_(layout), params_(params), th_(theme) {}
    void draw(hc::ui::Renderer& r, const PanelHost& host, const FaceFeed& feed, const FaceState& st, float originX, float originY,
              float zoom, bool plate = true) const;

private:
    struct Ctx;
    void header(const Ctx& c) const;
    void knob(const Ctx& c, const Widget& w, int wi) const;
    void button(const Ctx& c, Rect r, std::string_view text, bool lit, bool hot, bool dim, Rgba colour) const;
    void stepper(const Ctx& c, const Widget& w, int wi) const;
    void rangeSlider(const Ctx& c, const Widget& w, int wi) const;
    void bypass(const Ctx& c, const Widget& w, int wi) const;
    void spectrum(const Ctx& c, const Widget& w) const;
    void eqGraph(const Ctx& c, const Widget& w) const;
    void grid(const Ctx& c, Rect plot, float dbTop, float dbBottom, float dbStep, bool signedLabels) const;
    [[nodiscard]] float plain(const PanelHost& host, int param) const;

    const FaceLayout& L_;
    ParamTable        params_;
    Theme             th_;
};

// Plot rectangle inside an EQ graph or spectrum widget (plate units), shared by view and controller.
[[nodiscard]] Rect plotArea(const Rect& widget) noexcept;
[[nodiscard]] float freqToX(const Rect& plot, float hz) noexcept;
[[nodiscard]] float xToFreq(const Rect& plot, float x) noexcept;

} // namespace hem::ui
