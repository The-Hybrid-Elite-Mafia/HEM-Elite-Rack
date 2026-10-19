#include "hem/ui/Face.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/ConsoleEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Restorer.hpp"

#include <cmath>
#include <tuple>

namespace hem::ui {
namespace {

constexpr float kFooterY = 832.0f;

Widget knob(Rect dial, int param, std::string caption, std::string lo = "", std::string hi = "", bool small = false) {
    Widget w;
    w.kind = WKind::Knob;
    w.r = dial;
    w.param = param;
    w.caption = std::move(caption);
    w.lo = std::move(lo);
    w.hi = std::move(hi);
    w.small = small;
    return w;
}
Widget bandKnob(Rect dial, int control, std::string caption) {
    Widget w = knob(dial, -1, std::move(caption), "", "", true);
    w.control = control;
    return w;
}
Widget toggle(Rect r, int param, std::string caption) {
    Widget w;
    w.kind = WKind::Toggle;
    w.r = r;
    w.param = param;
    w.caption = std::move(caption);
    return w;
}

// The parts every HEM-Elite-Rack plate has: header with crown and ghost, footer strip, screws.
void common(FaceLayout& L, const rack::PluginInfo& info) {
    L.face = info.face;
    L.name = info.name;
    L.suite = info.suite;
    L.tagline = info.tagline;
    L.header = {30, 18, kPlateW - 60, 140};
    L.crown = {68, 20, 132, 132};
    L.ghost = {kPlateW - 68 - 122, 16, 122, 127};
    L.footer = {14, kFooterY, kPlateW - 28, kPlateH - kFooterY - 12};
    L.footerPlate = {kPlateW * 0.5f - 260, kFooterY + 22, 520, 58};
    L.recesses.push_back({L.footerPlate, 8, 4, false, false});
    L.grooves.push_back({34, 166, kPlateW - 34, 166});
    L.screws = {{40, 40}, {kPlateW - 40, 40}, {40, kFooterY + 50}, {kPlateW - 40, kFooterY + 50}};
}

FaceLayout restorer(const rack::PluginInfo& info) {
    using namespace rack::restorer;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"CLEANER AUDIO", "TRUE DETAIL"};
    L.sideNotes = {"LESS NOISE", "MORE MUSIC", "PRESERVE", "DETAIL", "NATURAL", "SOUND"};
    L.title = {60, 200, 330, 120};
    L.display = {410, 190, 1020, 316};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget spec;
    spec.kind = WKind::Spectrum;
    spec.r = L.display;
    L.widgets.push_back(spec);
    // FREQUENCY RANGE: right under the graph, on the same frequency scale.
    Widget range;
    range.kind = WKind::RangeSlider;
    range.r = {plotArea(L.display).x, 520, plotArea(L.display).w, 40};
    range.param = LowHz;
    range.param2 = HighHz;
    range.caption = "FREQUENCY RANGE";
    L.recesses.push_back({range.r.inset(-6, 6), 10, 4, false, false});
    L.widgets.push_back(range);
    // The three dials of the mockup, plus OUTPUT.
    L.widgets.push_back(knob({480, 632, 140, 140}, Reduction, "NOISE REDUCTION", "0", "10"));
    L.widgets.push_back(knob({780, 632, 140, 140}, Focus, "FOCUS", "LO", "HI"));
    L.widgets.push_back(knob({1080, 632, 140, 140}, Air, "AIR PRESERVE", "0", "10"));
    L.widgets.push_back(knob({1318, 662, 96, 96}, Output, "OUTPUT", "-24", "+12", true));
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::Knob) L.recesses.push_back({w.r.inset(-16), w.r.w * 0.5f + 16, 3, true, false});
    // Left column under the name: what is being taken out, ADAPT, LISTEN.
    L.widgets.push_back(toggle({70, 600, 270, 56}, Adapt, "ADAPT"));
    L.widgets.push_back(toggle({70, 690, 270, 56}, Listen, "LISTEN"));
    // Right column: ON / BYPASS.
    Widget byp;
    byp.kind = WKind::Bypass;
    byp.r = {1495, 660, 100, 100};
    byp.caption = "ON / BYPASS";
    L.widgets.push_back(byp);
    L.recesses.push_back({byp.r.inset(-12), 62, 4, true, false});
    L.grooves.push_back({1450, 580, 1450, 800});
    return L;
}

FaceLayout eliteEq(const rack::PluginInfo& info) {
    using namespace rack::eq;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"SURGICAL OR MUSICAL", "16 BANDS  -  DYNAMIC"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 380};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget g;
    g.kind = WKind::EqGraph;
    g.r = L.display;
    L.widgets.push_back(g);
    // ±3 / ±6 / ±12 / ±30 dB (inside the graph, top right).
    for (int i = 0; i < 4; ++i) {
        Widget b;
        b.kind = WKind::GraphRange;
        b.choice = i;
        b.r = {L.display.right() - 20 - static_cast<float>(4 - i) * 76, L.display.y + 14, 70, 30};
        L.widgets.push_back(b);
    }
    // Band selectors 1..16 under the graph.
    const float bw = L.display.w / static_cast<float>(kBands);
    for (int b = 0; b < kBands; ++b) {
        Widget w;
        w.kind = WKind::BandButton;
        w.choice = b;
        w.r = {L.display.x + static_cast<float>(b) * bw + 3, 572, bw - 6, 36};
        L.widgets.push_back(w);
    }
    // The selected band: ON, TYPE, STEREO, SLOPE (steppers), FREQ, GAIN, Q, then DYNAMIC and its four dials.
    auto stepper = [&](float y, int control, const char* cap) {
        Widget w;
        w.kind = WKind::Stepper;
        w.control = control;
        w.caption = cap;
        w.r = {500, y, 150, 40};
        L.widgets.push_back(w);
        L.recesses.push_back({w.r.inset(-4), 9, 4, false, false});
    };
    Widget bon;
    bon.kind = WKind::Toggle;
    bon.control = On;
    bon.caption = "BAND";
    bon.r = {500, 620, 150, 36};
    L.widgets.push_back(bon);
    L.recesses.push_back({bon.r.inset(-4), 9, 4, false, false});
    stepper(668, TypeC, "TYPE");
    stepper(718, StereoC, "STEREO");
    stepper(768, Slope, "SLOPE");
    const float ky = 694, kd = 80;
    L.widgets.push_back(bandKnob({690, ky, kd, kd}, Freq, "FREQ"));
    L.widgets.push_back(bandKnob({805, ky, kd, kd}, Gain, "GAIN"));
    L.widgets.push_back(bandKnob({920, ky, kd, kd}, Q, "Q"));
    Widget dyn;
    dyn.kind = WKind::Toggle;
    dyn.control = Dyn;
    dyn.caption = "DYNAMIC";
    dyn.r = {1050, 620, 220, 36};
    L.widgets.push_back(dyn);
    L.recesses.push_back({dyn.r.inset(-4), 10, 4, false, false});
    const float dynPitch = 146;
    const int dynCtl[4] = {Threshold, Range, Attack, Release};
    const char* dynCap[4] = {"THRESHOLD", "RANGE", "ATTACK", "RELEASE"};
    for (int i = 0; i < 4; ++i)
        L.widgets.push_back(bandKnob({1050 + static_cast<float>(i) * dynPitch + (dynPitch - kd) * 0.5f, ky, kd, kd}, dynCtl[i], dynCap[i]));
    L.grooves.push_back({1022, 630, 1022, 806});
    L.grooves.push_back({670, 630, 670, 806});
    // Left column: CHARACTER, DRIVE, OUTPUT, AUTO GAIN, ON / BYPASS.
    for (int c = 0; c < 3; ++c) {
        Widget w;
        w.kind = WKind::ChoiceButton;
        w.param = Character;
        w.choice = c;
        w.caption = c == 0 ? "CHARACTER" : "";
        w.r = {70, 420 + static_cast<float>(c) * 46, 270, 40};
        L.widgets.push_back(w);
        L.recesses.push_back({w.r.inset(-4), 10, 4, false, false});
    }
    L.widgets.push_back(knob({92, 592, 86, 86}, Drive, "DRIVE", "", "", true));
    L.widgets.push_back(knob({232, 592, 86, 86}, Output, "OUTPUT", "", "", true));
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::Knob) L.recesses.push_back({w.r.inset(-12), w.r.w * 0.5f + 12, 3, true, false});
    L.widgets.push_back(toggle({70, 738, 180, 50}, AutoGain, "AUTO GAIN"));
    Widget byp;
    byp.kind = WKind::Bypass;
    byp.r = {282, 734, 58, 58};
    byp.caption = "";
    L.widgets.push_back(byp);
    L.recesses.push_back({byp.r.inset(-8), 37, 4, true, false});
    return L;
}

// Knob seats (round recesses) for every knob added so far that has none yet.
void seatKnobs(FaceLayout& L, std::size_t from, float rim) {
    for (std::size_t i = from; i < L.widgets.size(); ++i)
        if (L.widgets[i].kind == WKind::Knob) L.recesses.push_back({L.widgets[i].r.inset(-rim), L.widgets[i].r.w * 0.5f + rim, 3, true, false});
}
Widget choiceButton(Rect r, int param, int choice, std::string caption = "") {
    Widget w;
    w.kind = WKind::ChoiceButton;
    w.r = r;
    w.param = param;
    w.choice = choice;
    w.caption = std::move(caption);
    return w;
}

FaceLayout consoleEq(const rack::PluginInfo& info) {
    using namespace rack::console;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"FOUR BANDS + FILTERS", "CLASSIC OR MODERN"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 250};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget curve;
    curve.kind = WKind::CurveDisplay;
    curve.r = L.display;
    L.widgets.push_back(curve);
    // Five channel-strip sections across, each with its name engraved above.
    const float pitch = L.display.w / 5.0f;
    const char* names[5] = {"FILTERS", "LF", "LMF", "HMF", "HF"};
    for (int i = 0; i < 5; ++i) {
        const float x0 = L.display.x + static_cast<float>(i) * pitch;
        L.sections.push_back({{x0, 446, pitch, 30}, names[i]});
        if (i > 0) L.grooves.push_back({x0, 456, x0, 808});
    }
    const std::size_t firstKnob = L.widgets.size();
    auto cx = [&](int i) { return L.display.x + (static_cast<float>(i) + 0.5f) * pitch; };
    const float gy = 516, gd = 92, ry = 680, rd = 72;
    // FILTERS
    L.widgets.push_back(knob({cx(0) - 90, gy + 10, rd, rd}, HpfHz, "HPF", "", "", true));
    L.widgets.push_back(knob({cx(0) + 18, gy + 10, rd, rd}, LpfHz, "LPF", "", "", true));
    L.widgets.push_back(toggle({cx(0) - 110, ry + 14, 104, 36}, HpfOn, "HPF"));
    L.widgets.push_back(toggle({cx(0) + 6, ry + 14, 104, 36}, LpfOn, "LPF"));
    // LF / HF: gain, frequency and the BELL switch
    for (const auto& [i, gain, hz, bell] : {std::tuple{1, LfGain, LfHz, LfBell}, std::tuple{4, HfGain, HfHz, HfBell}}) {
        L.widgets.push_back(knob({cx(i) - gd * 0.5f, gy, gd, gd}, gain, "GAIN", "-15", "+15"));
        L.widgets.push_back(knob({cx(i) - 90, ry, rd, rd}, hz, "FREQ", "", "", true));
        L.widgets.push_back(toggle({cx(i) + 6, ry + 18, 104, 36}, bell, "BELL"));
    }
    // LMF / HMF: gain, frequency, Q
    for (const auto& [i, gain, hz, q] : {std::tuple{2, LmfGain, LmfHz, LmfQ}, std::tuple{3, HmfGain, HmfHz, HmfQ}}) {
        L.widgets.push_back(knob({cx(i) - gd * 0.5f, gy, gd, gd}, gain, "GAIN", "-15", "+15"));
        L.widgets.push_back(knob({cx(i) - 90, ry, rd, rd}, hz, "FREQ", "", "", true));
        L.widgets.push_back(knob({cx(i) + 18, ry, rd, rd}, q, "Q", "", "", true));
    }
    // Left column: CURVE, DRIVE, OUTPUT, ON / BYPASS.
    L.widgets.push_back(choiceButton({70, 434, 270, 40}, Curve, 0, "CURVE"));
    L.widgets.push_back(choiceButton({70, 480, 270, 40}, Curve, 1));
    L.widgets.push_back(knob({92, 586, 86, 86}, Drive, "DRIVE", "", "", true));
    L.widgets.push_back(knob({232, 586, 86, 86}, Output, "OUTPUT", "", "", true));
    seatKnobs(L, firstKnob, 12);
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::ChoiceButton || w.kind == WKind::Toggle) L.recesses.push_back({w.r.inset(-4), 10, 4, false, false});
    Widget byp;
    byp.kind = WKind::Bypass;
    byp.r = {160, 722, 90, 90};
    L.widgets.push_back(byp);
    L.recesses.push_back({byp.r.inset(-10), 55, 4, true, false});
    return L;
}

FaceLayout busGlue(const rack::PluginInfo& info) {
    using namespace rack::glue;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"GLUE THE MIX", "KEEP THE PUNCH"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 700, 330};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget gr;
    gr.kind = WKind::GrMeter;
    gr.r = L.display;
    L.widgets.push_back(gr);
    // Right of the meter: RATIO and the sidechain high-pass as buttons.
    L.sections.push_back({{1150, 196, 486, 30}, "RATIO"});
    for (int i = 0; i < 3; ++i) L.widgets.push_back(choiceButton({1150 + static_cast<float>(i) * 164, 236, 158, 44}, Ratio, i));
    L.sections.push_back({{1150, 316, 486, 30}, "SIDECHAIN HPF"});
    for (int i = 0; i < 4; ++i) L.widgets.push_back(choiceButton({1150 + static_cast<float>(i) * 122, 356, 116, 44}, ScHpf, i));
    L.sections.push_back({{1150, 436, 486, 30}, "BASS KEEPS ITS PUNCH"});
    // The knob row under everything.
    const std::size_t firstKnob = L.widgets.size();
    const float pitch = 1226.0f / 6.0f;
    auto cx = [&](int i) { return 410 + (static_cast<float>(i) + 0.5f) * pitch; };
    L.widgets.push_back(knob({cx(0) - 64, 568, 128, 128}, Threshold, "THRESHOLD", "-40", "0"));
    L.widgets.push_back(knob({cx(1) - 50, 582, 100, 100}, Attack, "ATTACK"));
    L.widgets.push_back(knob({cx(2) - 50, 582, 100, 100}, Release, "RELEASE"));
    L.widgets.push_back(knob({cx(3) - 50, 582, 100, 100}, Makeup, "MAKEUP", "0", "+20"));
    L.widgets.push_back(knob({cx(4) - 50, 582, 100, 100}, Mix, "MIX", "DRY", "WET"));
    L.widgets.push_back(knob({cx(5) - 43, 589, 86, 86}, Output, "OUTPUT", "", "", true));
    seatKnobs(L, firstKnob, 14);
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::ChoiceButton) L.recesses.push_back({w.r.inset(-4), 10, 4, false, false});
    L.grooves.push_back({410, 524, 1636, 524});
    Widget byp;
    byp.kind = WKind::Bypass;
    byp.r = {160, 640, 90, 90};
    byp.caption = "ON / BYPASS";
    L.widgets.push_back(byp);
    L.recesses.push_back({byp.r.inset(-10), 55, 4, true, false});
    return L;
}

} // namespace

FaceLayout faceLayout(const rack::PluginInfo& info) {
    switch (info.face) {
    case rack::Face::EliteEq: return eliteEq(info);
    case rack::Face::ConsoleEq: return consoleEq(info);
    case rack::Face::BusGlue: return busGlue(info);
    case rack::Face::Restorer: break;
    }
    return restorer(info);
}

int widgetParam(const Widget& w, const FaceState& st) noexcept {
    if (w.control >= 0) return rack::eq::bandParam(std::clamp(st.selectedBand, 0, rack::eq::kBands - 1), w.control);
    return w.param;
}

Rect plotArea(const Rect& r) noexcept { return {r.x + 70, r.y + 20, r.w - 90, r.h - 62}; }

float freqToX(const Rect& plot, float hz) noexcept {
    const float t = std::log(std::clamp(hz, 20.0f, 20000.0f) / 20.0f) / std::log(1000.0f);
    return plot.x + t * plot.w;
}
float xToFreq(const Rect& plot, float x) noexcept {
    const float t = std::clamp((x - plot.x) / plot.w, 0.0f, 1.0f);
    return 20.0f * std::pow(1000.0f, t);
}

} // namespace hem::ui
