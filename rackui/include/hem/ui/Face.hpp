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
#include "hem/rack/Analyzer.hpp"
#include "hem/rack/Clone.hpp"
#include "hem/rack/BusGlue.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hem/rack/Tools.hpp"
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
    [[nodiscard]] virtual float reductionDb() const { return 0.0f; }    // compressors: gain reduction now (<= 0)
    [[nodiscard]] virtual float meterDb(int) const { return -100.0f; }  // the effect's own meters (TAPE: VU L / R)
    // STAKEOUT's display (rack::AnalyzerSource): band levels and maxima per side, and the stereo position per band
    [[nodiscard]] virtual bool  hasAnalyzer() const { return false; }
    [[nodiscard]] virtual float rtaLevel(int, int) const { return -120.0f; }
    [[nodiscard]] virtual float rtaMax(int, int) const { return -120.0f; }
    [[nodiscard]] virtual float spdAngle(int) const { return 0.0f; }
    [[nodiscard]] virtual float spdLevel(int) const { return -120.0f; }
    // THE FORGER's curve (rack::CloneSource): rack::clone::kPoints points, dB
    [[nodiscard]] virtual bool  hasClone() const { return false; }
    [[nodiscard]] virtual float cloneDb(int) const { return 0.0f; }
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
    float reductionDb() const override { return fx_->reductionDb(); }
    float meterDb(int i) const override { return fx_->meter(i); }
    bool  hasAnalyzer() const override { return analyzer_ != nullptr; }
    float rtaLevel(int side, int b) const override;
    float rtaMax(int side, int b) const override;
    float spdAngle(int b) const override;
    float spdLevel(int b) const override;
    bool  hasClone() const override { return clone_ != nullptr; }
    float cloneDb(int i) const override { return clone_ ? clone_->cloneDb(i) : 0.0f; }
    double sampleRate() const override { return rate_; }
    void setSampleRate(double fs) noexcept { rate_ = fs; }

private:
    hc::fx::Effect*           fx_ = nullptr;
    rack::SpectrumSource*     spectrum_ = nullptr;
    rack::NoiseProfileSource* noise_ = nullptr;
    rack::EliteEqView*        eq_ = nullptr;
    rack::AnalyzerSource*     analyzer_ = nullptr;
    rack::CloneSource*        clone_ = nullptr;
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
    CurveDisplay,  // VELVET: spectrum + the EQ's curve (display only)
    GrMeter,       // compressors: gain-reduction needle (display only); choice >= 0: meter `choice` instead, caption under it
    VuMeter,       // TAPE: VU needle for meter `choice` (display only)
    Readout,       // a big number: meter `choice` of the effect, with `caption` and the meter's unit (display only)
    History,       // the last few seconds of meters 0 and 1 (gain reduction) and 2 (input level), scrolling (display only)
    PageTab,       // which page the paged controls edit (choice = page, caption = its name); a plate setting, not a parameter
    Keyboard,      // piano keys: key k is the on / off parameter param + k (choice = how many keys, from a C);
                   // param2 = the sustain switch (off: a key sounds while the mouse holds it; on: a click latches it)
    Rta,           // STAKEOUT: the frequency bands as bars (one set, or left and right), with the max line (display only)
    Polar,         // STAKEOUT: the stereo position of each band on a half circle (display only)
    CloneCurve,    // THE FORGER: the captured curve, 25 Hz .. 20 kHz (display only; param = its ±16 / ±30 dB choice)
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
    int         meter = -1;        // needle with a meter selector (param2): the input VU meter (output = meter + 1)
    int         pageStride = 0;    // > 0: the control edits param + page * pageStride (the plate's page tabs pick the page)
    int         showParam = -1;    // >= 0: the control is there only while choice parameter showParam is showValue
    int         showValue = 0;     // (THE RACKET: each pedal's controls share the same seats)
};
// Two controls that are never on the plate at the same time (they may share a place).
[[nodiscard]] inline bool exclusive(const Widget& a, const Widget& b) noexcept {
    return a.showParam >= 0 && a.showParam == b.showParam && a.showValue != b.showValue;
}

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
    std::vector<std::pair<Rect, std::string>> sections;   // engraved section names ("LMF")
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
    int page = 0;               // the page the paged controls edit (PageTab)
};
inline constexpr std::array<float, 4> kGraphRanges{3.0f, 6.0f, 12.0f, 30.0f};

// Whether a widget is on the plate now (its showParam, if any, holds its showValue).
[[nodiscard]] bool widgetShown(const Widget& w, std::span<const hc::fx::ParamInfo> params, const PanelHost& host) noexcept;

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
    [[nodiscard]] int heldKey() const noexcept { return heldKey_; }   // the key the mouse is holding (-1: none)
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
    int   heldKey_ = -1;     // keyboard: the key under the held mouse (NORMAL mode)
    bool  latchOn_ = true;   // keyboard, SUSTAIN mode: a drag latches keys on (true) or off
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
    void consoleCurve(const Ctx& c, const Widget& w) const;
    void grMeter(const Ctx& c, const Widget& w) const;
    void vuMeter(const Ctx& c, const Widget& w, int meter, const char* caption) const;
    void vuMeter(const Ctx& c, const Widget& w) const;
    void readout(const Ctx& c, const Widget& w) const;
    void keyboard(const Ctx& c, const Widget& w) const;
    void history(const Ctx& c, const Widget& w) const;
    void rta(const Ctx& c, const Widget& w) const;
    void polar(const Ctx& c, const Widget& w) const;
    void cloneCurve(const Ctx& c, const Widget& w) const;
    void spectrumBehind(const Ctx& c, Rect plot, float alpha) const;
    void grid(const Ctx& c, Rect plot, float dbTop, float dbBottom, float dbStep, bool signedLabels) const;
    [[nodiscard]] float plain(const PanelHost& host, int param) const;

    const FaceLayout& L_;
    ParamTable        params_;
    Theme             th_;
    mutable float     needle_ = 1.0f;   // the GR needle's position (it swings, it doesn't jump); > 0 = not drawn yet
    mutable std::array<float, 4> needles_{1.0f, 1.0f, 1.0f, 1.0f};   // needles that show a meter (by meter index)
    static constexpr int kHistory = 240;   // frames of meter history (about 4 s at 60 frames a second)
    mutable std::array<std::array<float, kHistory>, 3> hist_{};
    mutable int histPos_ = 0, histCount_ = 0;
    mutable float     vu_[2] = {100.0f, 100.0f};   // VU needles; 100 = not drawn yet
};

// Plot rectangle inside an EQ graph or spectrum widget (plate units), shared by view and controller.
[[nodiscard]] Rect plotArea(const Rect& widget) noexcept;
// The keyboard's keys (WKind::Keyboard, starting on a C): where key k is, whether it is black, and the key at a point (-1: none).
[[nodiscard]] bool keyIsBlack(int k) noexcept;
[[nodiscard]] Rect keyRect(const Rect& keyboard, int keys, int k) noexcept;
[[nodiscard]] int keyAt(const Rect& keyboard, int keys, float x, float y) noexcept;
[[nodiscard]] float freqToX(const Rect& plot, float hz) noexcept;
[[nodiscard]] float xToFreq(const Rect& plot, float x) noexcept;

} // namespace hem::ui
