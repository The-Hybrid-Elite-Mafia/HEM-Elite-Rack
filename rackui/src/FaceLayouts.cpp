#include "hem/ui/Face.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Tools.hpp"
#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/Hitman.hpp"
#include "hem/rack/ColorFx.hpp"
#include "hem/rack/Space.hpp"
#include "hem/rack/Movement.hpp"
#include "hem/rack/Specialty.hpp"
#include "hem/rack/Arsenal.hpp"
#include "hem/rack/ToneShop.hpp"
#include "hem/rack/Hideout.hpp"
#include "hem/rack/Garage.hpp"
#include "hem/rack/Analyzer.hpp"
#include "hem/rack/Streets.hpp"
#include "hem/rack/Holdouts.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

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

void bypassAt(FaceLayout& L, Rect r, std::string caption = "ON / BYPASS") {
    Widget byp;
    byp.kind = WKind::Bypass;
    byp.r = r;
    byp.caption = std::move(caption);
    L.widgets.push_back(byp);
    L.recesses.push_back({byp.r.inset(-10), byp.r.w * 0.5f + 10, 4, true, false});
}
void buttonSeats(FaceLayout& L) {
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::ChoiceButton || w.kind == WKind::Toggle) L.recesses.push_back({w.r.inset(-4), 10, 4, false, false});
}
// A row of knobs across under the display: {param, caption, size, lo, hi, small}
struct KnobSpec { int param; const char* caption; float size; const char* lo; const char* hi; bool small; };
void knobRow(FaceLayout& L, std::initializer_list<KnobSpec> ks, float topY) {
    const float pitch = 1226.0f / static_cast<float>(ks.size());
    const std::size_t first = L.widgets.size();
    int i = 0;
    for (const KnobSpec& k : ks) {
        const float cx = 410 + (static_cast<float>(i++) + 0.5f) * pitch;
        const float y = topY + (128.0f - k.size) * 0.5f;
        L.widgets.push_back(knob({cx - k.size * 0.5f, y, k.size, k.size}, k.param, k.caption, k.lo, k.hi, k.small));
    }
    seatKnobs(L, first, 14);
}
// The compressors: needle display on the left, buttons on the right, knobs underneath.
void needleDisplay(FaceLayout& L) {
    L.display = {410, 182, 700, 330};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget gr;
    gr.kind = WKind::GrMeter;
    gr.r = L.display;
    L.widgets.push_back(gr);
    L.grooves.push_back({410, 524, 1636, 524});
}
void buttonRow(FaceLayout& L, const char* caption, float y, int param, int count) {
    L.sections.push_back({{1150, y - 40, 486, 30}, caption});
    const float w = (486.0f - 6.0f * static_cast<float>(count - 1)) / static_cast<float>(count);
    for (int i = 0; i < count; ++i) L.widgets.push_back(choiceButton({1150 + static_cast<float>(i) * (w + 6.0f), y, w, 44}, param, i));
}

FaceLayout tape(const rack::PluginInfo& info) {
    using namespace rack::tape;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"RUN IT TO TAPE", "WARMTH AND GLUE"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 310};
    for (int ch = 0; ch < 2; ++ch) {
        Widget vu;
        vu.kind = WKind::VuMeter;
        vu.choice = ch;
        vu.caption = ch == 0 ? "LEFT" : "RIGHT";
        vu.r = {410 + static_cast<float>(ch) * 626, 182, 600, 310};
        L.widgets.push_back(vu);
        L.recesses.push_back({vu.r, 14, 9, false, true});
    }
    L.grooves.push_back({410, 524, 1636, 524});
    knobRow(L, {{Input, "INPUT", 128, "-12", "+18", false}, {Bump, "HEAD BUMP", 100, "0", "100", false}, {Wow, "WOW", 100, "0", "100", false},
                {Hiss, "HISS", 100, "0", "100", false}, {Output, "OUTPUT", 86, "", "", true}}, 568);
    L.widgets.push_back(choiceButton({70, 434, 270, 40}, Speed, 0, "SPEED"));
    L.widgets.push_back(choiceButton({70, 480, 270, 40}, Speed, 1));
    L.widgets.push_back(toggle({70, 560, 270, 44}, AutoTrim, "AUTO TRIM"));
    {   // VU CAL: which level reads 0 VU
        Widget cal;
        cal.kind = WKind::Stepper;
        cal.param = VuCal;
        cal.caption = "VU ZERO";
        cal.r = {170, 626, 170, 40};
        L.widgets.push_back(cal);
        L.recesses.push_back({cal.r.inset(-4), 9, 4, false, false});
    }
    buttonSeats(L);
    bypassAt(L, {160, 698, 90, 90});
    return L;
}

FaceLayout tubeEq(const rack::PluginInfo& info) {
    using namespace rack::tubeeq;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"BROAD AND MUSICAL", "BOOST AND ATTEN"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 250};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget curve;
    curve.kind = WKind::CurveDisplay;
    curve.r = L.display;
    L.widgets.push_back(curve);
    const float pitch = 1226.0f / 3.0f;
    const char* names[3] = {"LOW FREQUENCY", "HIGH FREQUENCY", "HIGH ATTENUATION"};
    for (int i = 0; i < 3; ++i) {
        const float x0 = 410 + static_cast<float>(i) * pitch;
        L.sections.push_back({{x0, 450, pitch, 30}, names[i]});
        if (i > 0) L.grooves.push_back({x0, 460, x0, 808});
    }
    const std::size_t first = L.widgets.size();
    auto cx = [&](int sec, int slot) { return 410 + static_cast<float>(sec) * pitch + pitch * (0.5f + 0.32f * static_cast<float>(slot - 1)); };
    const float y = 540;
    // LOW: frequency, boost, atten
    L.widgets.push_back(knob({cx(0, 0) - 40, y + 14, 80, 80}, LowFreq, "FREQ", "", "", true));
    L.widgets.push_back(knob({cx(0, 1) - 48, y + 6, 96, 96}, LowBoost, "BOOST", "0", "10"));
    L.widgets.push_back(knob({cx(0, 2) - 48, y + 6, 96, 96}, LowAtten, "ATTEN", "0", "10"));
    // HIGH: frequency, boost, bandwidth
    L.widgets.push_back(knob({cx(1, 0) - 40, y + 14, 80, 80}, HighFreq, "FREQ", "", "", true));
    L.widgets.push_back(knob({cx(1, 1) - 48, y + 6, 96, 96}, HighBoost, "BOOST", "0", "10"));
    L.widgets.push_back(knob({cx(1, 2) - 40, y + 14, 80, 80}, Bandwidth, "WIDTH", "", "", true));
    // HIGH ATTEN: select, atten
    L.widgets.push_back(knob({cx(2, 0) - 40, y + 14, 80, 80}, AttenSel, "SELECT", "", "", true));
    L.widgets.push_back(knob({cx(2, 1) - 48, y + 6, 96, 96}, HighAtten, "ATTEN", "0", "10"));
    // left column: the tube
    L.sections.push_back({{60, 400, 300, 30}, "TUBE"});
    L.widgets.push_back(knob({92, 468, 86, 86}, Drive, "DRIVE", "", "", true));
    L.widgets.push_back(knob({232, 468, 86, 86}, Output, "OUTPUT", "", "", true));
    seatKnobs(L, first, 12);
    L.widgets.push_back(toggle({70, 600, 270, 40}, EqIn, "EQ"));
    Widget an;
    an.kind = WKind::Stepper;
    an.param = Analog;
    an.caption = "HUM";
    an.r = {170, 652, 170, 40};
    L.widgets.push_back(an);
    L.recesses.push_back({an.r.inset(-4), 9, 4, false, false});
    bypassAt(L, {160, 712, 80, 80});
    return L;
}

FaceLayout limiterFace(const rack::PluginInfo& info) {
    using namespace rack::limiter;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"LOUD AND CLEAN", "NEVER OVER THE TOP"};
    L.title = {60, 190, 330, 110};
    needleDisplay(L);
    // Right: what comes out, in big numbers.
    for (int m = 0; m < 2; ++m) {
        Widget ro;
        ro.kind = WKind::Readout;
        ro.choice = m;
        ro.caption = m == 0 ? "LOUDNESS (3 s)" : "TRUE PEAK";
        ro.lo = m == 0 ? "LUFS" : "dBTP";
        ro.hi = m == 1 ? "CEIL" : "";
        ro.r = {1150, 190 + static_cast<float>(m) * 112, 486, 100};
        L.widgets.push_back(ro);
        L.recesses.push_back({ro.r, 12, 8, false, true});
    }
    L.widgets.push_back(toggle({1150, 430, 238, 44}, TruePeak, "TRUE PEAK"));
    L.widgets.push_back(toggle({1398, 430, 238, 44}, AutoRelease, "AUTO REL"));
    knobRow(L, {{Input, "INPUT", 128, "0", "+24", false}, {Ceiling, "CEILING", 128, "-6", "0", false}, {Release, "RELEASE", 100, "FAST", "SLOW", false},
                {Link, "LINK", 100, "0", "100", false}}, 568);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}


// A big-number window. fmt (Widget::hi): "" = 0.0, "+" = signed, "+2" = signed with 2 decimals,
// "CEIL" = red when above the LIMITER's ceiling.
void readoutAt(FaceLayout& L, Rect r, int meter, const char* caption, const char* unit, const char* fmt = "") {
    Widget ro;
    ro.kind = WKind::Readout;
    ro.choice = meter;
    ro.caption = caption;
    ro.lo = unit;
    ro.hi = fmt;
    ro.r = r;
    L.widgets.push_back(ro);
    L.recesses.push_back({ro.r, 12, 8, false, true});
}

FaceLayout widthFace(const rack::PluginInfo& info) {
    using namespace rack::width;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"WIDE ON TOP", "MONO DOWN LOW"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 140};
    readoutAt(L, {410, 182, 292, 120}, 0, "CORRELATION", "PHASE", "+2");
    readoutAt(L, {722, 182, 292, 120}, 1, "SIDE VS MID", "dB", "+");
    readoutAt(L, {1034, 182, 292, 120}, 2, "LEFT / MID", "dBFS", "+");
    readoutAt(L, {1344, 182, 292, 120}, 3, "RIGHT / SIDE", "dBFS", "+");
    buttonRow(L, "MONO BASS", 392, MonoBass, 5);
    // the input: polarity per side, swap, and what comes in / what the meters show
    L.widgets.push_back(toggle({410, 352, 220, 44}, PolarityL, "POLARITY L"));
    L.widgets.push_back(toggle({644, 352, 220, 44}, PolarityR, "POLARITY R"));
    L.widgets.push_back(toggle({878, 352, 220, 44}, Swap, "SWAP"));
    auto stepper = [&](Rect r, int param, const char* caption) {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.caption = caption;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    stepper({508, 418, 190, 40}, InputMode, "INPUT");
    stepper({908, 418, 190, 40}, MeterMode, "METER");
    L.grooves.push_back({410, 524, 1636, 524});
    knobRow(L, {{Width, "WIDTH", 112, "MONO", "200%", false}, {Shuffle, "LOW WIDEN", 76, "", "", true}, {ShuffleFreq, "WIDEN BELOW", 76, "350", "1.4k", true},
                {BassTrim, "LOW TRIM", 76, "-12", "+12", true}, {Rotation, "AIM", 76, "L", "R", true},
                {Asymmetry, "CENTER SHIFT", 76, "L", "R", true}, {Balance, "BALANCE", 76, "L", "R", true}, {Output, "OUTPUT", 76, "", "", true}}, 568);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

FaceLayout exciterFace(const rack::PluginInfo& info) {
    using namespace rack::exciter;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"AIR AND PRESENCE", "WITHOUT EQ BOOST"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 330};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget spec;
    spec.kind = WKind::Spectrum;
    spec.r = L.display;
    L.widgets.push_back(spec);
    L.grooves.push_back({410, 524, 1636, 524});
    L.widgets.push_back(choiceButton({70, 434, 270, 40}, Mode, 0, "HARMONICS"));
    L.widgets.push_back(choiceButton({70, 480, 270, 40}, Mode, 1));
    knobRow(L, {{Freq, "FREQUENCY", 128, "1k", "10k", false}, {Drive, "DRIVE", 128, "0", "100", false},
                {Mix, "MIX", 100, "0", "100", false}}, 568);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

FaceLayout meterFace(const rack::PluginInfo& info) {
    using namespace rack::meter;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"MEASURES ONLY", "THE SOUND IS UNTOUCHED"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 600};
    // the big one, then two rows of three
    readoutAt(L, {410, 182, 800, 170}, Integrated, "INTEGRATED", "LUFS");
    readoutAt(L, {1226, 182, 410, 170}, ToTarget, "TO TARGET", "LU", "+");
    const int rest[5] = {Momentary, ShortTerm, Range, TruePeak, Correlation};
    const char* caps[5] = {"MOMENTARY", "SHORT-TERM", "LOUDNESS RANGE", "TRUE PEAK MAX", "CORRELATION"};
    const char* units[5] = {"LUFS", "LUFS", "LU", "dBTP", "PHASE"};
    for (int i = 0; i < 5; ++i) {
        const float x = 410 + static_cast<float>(i % 3) * 412, y = 376 + static_cast<float>(i / 3) * 160;
        readoutAt(L, {x, y, 402, 146}, rest[i], caps[i], units[i], i == 4 ? "+2" : "");
    }
    // the last cell: TARGET
    L.sections.push_back({{1234, 546, 402, 30}, "TARGET (LUFS)"});
    for (int i = 0; i < 4; ++i) L.widgets.push_back(choiceButton({1234 + static_cast<float>(i) * 102, 600, 96, 44}, Target, i));
    L.sections.push_back({{410, 720, 1226, 30}, "-14 SPOTIFY / YOUTUBE    -16 APPLE MUSIC    -11 / -9 LOUD"});
    L.widgets.push_back(toggle({70, 434, 270, 44}, Reset, "RESET"));
    L.widgets.back().hi = "PRESS";
    L.sections.push_back({{60, 490, 300, 30}, "CLICK TO START OVER"});
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

// ---- the classic EQs: the curve on top, engraved sections of knobs under it, colour on the left
struct SecKnob { int param; const char* cap; bool big; const char* lo; const char* hi; };
struct Sec { const char* name; std::vector<SecKnob> knobs; std::vector<std::pair<int, const char*>> toggles; };
void curveOnTop(FaceLayout& L) {
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 250};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget curve;
    curve.kind = WKind::CurveDisplay;
    curve.r = L.display;
    L.widgets.push_back(curve);
}
void sections(FaceLayout& L, const std::vector<Sec>& secs) {
    const float pitch = 1226.0f / static_cast<float>(secs.size());
    const std::size_t first = L.widgets.size();
    for (std::size_t si = 0; si < secs.size(); ++si) {
        const Sec& sec = secs[si];
        const float x0 = 410 + static_cast<float>(si) * pitch;
        L.sections.push_back({{x0, 450, pitch, 30}, sec.name});
        if (si > 0 && secs.size() <= 6) L.grooves.push_back({x0, 460, x0, 808});   // more than six: too busy
        const float n = static_cast<float>(sec.knobs.size());
        for (std::size_t k = 0; k < sec.knobs.size(); ++k) {
            const SecKnob& kn = sec.knobs[k];
            const float cx = x0 + pitch * (static_cast<float>(k) + 0.5f) / n, d = kn.big ? 96.0f : 76.0f;
            const float y = sec.toggles.empty() ? 548.0f : 512.0f;
            L.widgets.push_back(knob({cx - d * 0.5f, y + (96.0f - d) * 0.5f, d, d}, kn.param, kn.cap, kn.lo, kn.hi, !kn.big));
        }
        for (std::size_t t = 0; t < sec.toggles.size(); ++t) {
            const float tw = std::min(pitch - 40.0f, 200.0f);
            L.widgets.push_back(toggle({x0 + (pitch - tw) * 0.5f, 720 + static_cast<float>(t) * 50, tw, 40}, sec.toggles[t].first, sec.toggles[t].second));
        }
    }
    seatKnobs(L, first, 12);
}
// The left column: up to two small knobs (drive / output) under a caption, then the bypass.
void leftColumn(FaceLayout& L, const char* caption, std::vector<SecKnob> ks, float bypassY = 640) {
    L.sections.push_back({{60, 400, 300, 30}, caption});
    const std::size_t first = L.widgets.size();
    const float xs[2] = {ks.size() == 1 ? 162.0f : 92.0f, 232.0f};
    for (std::size_t i = 0; i < ks.size() && i < 2; ++i) L.widgets.push_back(knob({xs[i], 468, 86, 86}, ks[i].param, ks[i].cap, ks[i].lo, ks[i].hi, true));
    seatKnobs(L, first, 12);
    bypassAt(L, {160, bypassY, 90, 90});
}


// ---- wave 7 dynamics
// A group of choice buttons anywhere in the right column (x, width), with a caption above.
void buttonGroup(FaceLayout& L, const char* caption, float x, float w, float y, int param, int count) {
    L.sections.push_back({{x, y - 40, w, 30}, caption});
    const float bw = (w - 6.0f * static_cast<float>(count - 1)) / static_cast<float>(count);
    for (int i = 0; i < count; ++i) L.widgets.push_back(choiceButton({x + static_cast<float>(i) * (bw + 6.0f), y, bw, 44}, param, i));
}

FaceLayout vcaFace(const rack::PluginInfo& info) {
    using namespace rack::vca;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"CLEAN CONTROL", "ANY SOURCE"};
    L.title = {60, 190, 330, 110};
    needleDisplay(L);
    buttonRow(L, "SIDECHAIN HPF", 236, ScHpf, 5);
    L.widgets.push_back(toggle({1150, 330, 238, 44}, AutoRelease, "AUTO REL"));
    L.widgets.push_back(toggle({1398, 330, 238, 44}, AutoMakeup, "AUTO GAIN"));
    buttonGroup(L, "TAIL", 1150, 238, 466, Tail, 2);
    buttonGroup(L, "CHARACTER", 1398, 238, 466, Character, 2);
    knobRow(L, {{Threshold, "THRESHOLD", 120, "-60", "0", false}, {Ratio, "RATIO", 96, "1", "20", false}, {Knee, "KNEE", 86, "", "", true},
                {Attack, "ATTACK", 96, "0.1", "100", false}, {Release, "RELEASE", 96, "10", "2k", false}, {Makeup, "MAKEUP", 86, "", "", true},
                {Mix, "MIX", 86, "", "", true}, {Output, "OUTPUT", 86, "", "", true}}, 568);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

// ---- wave 8 colour: the spectrum on top (what the colour adds), knobs under it, switches on the left
void spectrumOnTop(FaceLayout& L) {
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 330};
    L.recesses.push_back({L.display, 14, 9, false, true});
    Widget spec;
    spec.kind = WKind::Spectrum;
    spec.r = L.display;
    L.widgets.push_back(spec);
    L.grooves.push_back({410, 524, 1636, 524});
}
void leftChoices(FaceLayout& L, const char* caption, int param, int count, float y = 434) {
    for (int i = 0; i < count; ++i) L.widgets.push_back(choiceButton({70, y + static_cast<float>(i) * 46, 270, 40}, param, i, i == 0 ? caption : ""));
}

FaceLayout tubeSatFace(const rack::PluginInfo& info) {
    using namespace rack::tubesat;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"WARMTH AND BODY", "SAME LEVEL, MORE TONE"};
    spectrumOnTop(L);
    knobRow(L, {{Drive, "DRIVE", 128, "0", "100", false}, {Bias, "BIAS", 100, "ODD", "EVEN", false}, {Tone, "TONE", 100, "DARK", "BRIGHT", false},
                {Mix, "MIX", 90, "DRY", "WET", false}, {Output, "OUTPUT", 86, "", "", true}}, 568);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

// ---- wave 9 space: two rows of knobs (no screen)
FaceLayout reverbFace(const rack::PluginInfo& info) {
    using namespace rack::verb;
    FaceLayout L;
    common(L, info);
    const bool plate = info.face == rack::Face::Plate;
    L.leftNotes = plate ? std::vector<std::string>{"BRIGHT AND DENSE", "VOCALS AND SNARES"} : std::vector<std::string>{"BIG AND LUSH", "CONCERT SPACE"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 330};
    knobRow(L, {{PreDelay, "PRE-DELAY", 100, "", "", false}, {Size, "SIZE", 110, "", "", false},
                {Decay, "DECAY", 128, "", "", false}, {Damping, "DAMPING", 100, "", "", false},
                {Early, plate ? "DIFFUSION" : "EARLY", 100, "", "", false}}, 240);
    L.grooves.push_back({410, 524, 1636, 524});
    knobRow(L, {{Mod, "MOD", 96, "", "", false}, {Width, "WIDTH", 96, "", "", false}, {LowCut, "LOW CUT", 96, "", "", false},
                {HighCut, "HIGH CUT", 96, "", "", false}, {Mix, "MIX", 110, "DRY", "WET", false}}, 590);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

FaceLayout digitalDelayFace(const rack::PluginInfo& info) {
    using namespace rack::ddelay;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"CLEAN REPEATS", "ON THE BEAT"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 330};
    buttonGroup(L, "NOTE (FREE = TIME IN ms)", 410, 1226, 236, Division, 8);
    L.widgets.push_back(toggle({70, 434, 270, 44}, PingPong, "PING-PONG"));
    knobRow(L, {{Bpm, "BPM", 110, "60", "200", false}, {Time, "TIME", 110, "1", "2000", false}, {Feedback, "FEEDBACK", 128, "0", "98", false},
                {Offset, "OFFSET", 90, "0", "30", false}}, 330);
    L.grooves.push_back({410, 524, 1636, 524});
    knobRow(L, {{LowCut, "LOW CUT", 96, "20", "1k", false}, {HighCut, "HIGH CUT", 96, "1k", "20k", false}, {Mix, "MIX", 110, "DRY", "WET", false}}, 590);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

// ---- wave 10 movement: one row of knobs in the middle, switches on the left
FaceLayout movementFace(const rack::PluginInfo& info) {
    FaceLayout L;
    common(L, info);
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 1226, 330};
    switch (info.face) {
    case rack::Face::Chorus: {
        using namespace rack::chorus;
        L.leftNotes = {"WIDE AND LUSH", "ONE TO THREE VOICES"};
        leftChoices(L, "VOICES", Voices, 3, 450);
        knobRow(L, {{Rate, "RATE", 110, "SLOW", "FAST", false}, {Depth, "DEPTH", 110, "0", "100", false}, {Delay, "DELAY", 100, "5", "30", false},
                    {Width, "WIDTH", 100, "MONO", "WIDE", false}, {Mix, "MIX", 110, "DRY", "WET", false}}, 470);
        break;
    }
    case rack::Face::Flanger: {
        using namespace rack::flanger;
        L.leftNotes = {"THE JET SWEEP", "+ OR - FEEDBACK"};
        knobRow(L, {{Rate, "RATE", 110, "SLOW", "FAST", false}, {Depth, "DEPTH", 110, "0", "100", false}, {Manual, "MANUAL", 110, "0.1", "10", false},
                    {Feedback, "FEEDBACK", 110, "-95", "+95", false}, {Mix, "MIX", 110, "DRY", "WET", false}}, 470);
        break;
    }
    case rack::Face::Phaser: {
        using namespace rack::phaser;
        L.leftNotes = {"SWIRL AND SWEEP", "4 TO 12 STAGES"};
        leftChoices(L, "STAGES", Stages, 4, 440);
        knobRow(L, {{Rate, "RATE", 110, "SLOW", "FAST", false}, {Depth, "DEPTH", 110, "0", "100", false}, {Center, "CENTER", 110, "200", "4k", false},
                    {Feedback, "FEEDBACK", 110, "-90", "+90", false}, {Mix, "MIX", 110, "DRY", "WET", false}}, 470);
        break;
    }
    case rack::Face::Tremolo: {
        using namespace rack::tremolo;
        L.leftNotes = {"PULSE AND MOVE", "OR PAN LEFT-RIGHT"};
        leftChoices(L, "SHAPE", Shape, 3, 440);
        L.widgets.push_back(toggle({70, 590, 270, 40}, Pan, "AUTO PAN"));
        knobRow(L, {{Rate, "RATE", 128, "0.1", "20", false}, {Depth, "DEPTH", 128, "0", "100", false}}, 470);
        break;
    }
    case rack::Face::Rotary: {
        using namespace rack::rotary;
        L.leftNotes = {"HORN AND DRUM", "SPIN UP, SPIN DOWN"};
        leftChoices(L, "SPEED", Speed, 2, 450);
        knobRow(L, {{Drive, "DRIVE", 110, "0", "100", false}, {Balance, "BALANCE", 110, "DRUM", "HORN", false}, {Spread, "SPREAD", 110, "0", "100", false},
                    {Mix, "MIX", 110, "DRY", "WET", false}}, 470);
        break;
    }
    default: {
        using namespace rack::autofilter;
        L.leftNotes = {"FOLLOWS THE MUSIC", "OR THE LFO"};
        leftChoices(L, "FILTER", Type, 3, 440);
        knobRow(L, {{Cutoff, "CUTOFF", 128, "40", "16k", false}, {Resonance, "RESONANCE", 110, "0.5", "10", false},
                    {Envelope, "ENVELOPE", 110, "-", "+", false}, {Mix, "MIX", 100, "DRY", "WET", false}}, 240);
        L.grooves.push_back({410, 524, 1636, 524});
        knobRow(L, {{Rate, "RATE", 100, "SLOW", "FAST", false}, {Depth, "DEPTH", 100, "0", "4 OCT", false}}, 590);
        break;
    }
    }
    buttonSeats(L);
    bypassAt(L, {160, 690, 90, 90});
    return L;
}

// ---- wave 11
FaceLayout subFace(const rack::PluginInfo& info) {
    using namespace rack::sub;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"MORE LOW END", "ON ANY SPEAKER"};
    spectrumOnTop(L);
    leftChoices(L, "MODE", Mode, 2);
    knobRow(L, {{Freq, "FREQUENCY", 110, "40", "150", false}, {Amount, "AMOUNT", 128, "0", "100", false}, {Tone, "TONE", 100, "BRIGHT", "DARK", false},
                {Output, "OUTPUT", 86, "", "", true}}, 568);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

FaceLayout vocalFace(const rack::PluginInfo& info) {
    using namespace rack::vocal;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"THE VOCAL CHAIN", "ON ONE PLATE"};
    L.title = {60, 190, 330, 110};
    needleDisplay(L);
    buttonRow(L, "HIGH-PASS", 236, Hpf, 5);
    L.sections.push_back({{1150, 316, 486, 30}, "HIGH-PASS, BODY, COMPRESS,"});
    L.sections.push_back({{1150, 356, 486, 30}, "DE-ESS, PRESENCE, AIR"});
    knobRow(L, {{Body, "BODY", 96, "-6", "+6", false}, {Compress, "COMPRESS", 128, "0", "100", false}, {DeEss, "DE-ESS", 110, "0", "100", false},
                {Presence, "PRESENCE", 96, "-6", "+6", false}, {Air, "AIR", 96, "0", "+8", false}, {Output, "OUTPUT", 86, "", "", true}}, 568);
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

// ---- HITMAN: sidechain EQ + history on top, the sidechain strip, then the two channels, two needles
// and the saturation; mode, link, ratio, lookahead and mix on the left
FaceLayout hitmanFace(const rack::PluginInfo& info) {
    using namespace rack::hitman;
    FaceLayout L;
    common(L, info);
    // One note line only: the left column (mode, link, ratio...) starts right under it.
    L.leftNotes = {"DUAL FET"};
    L.title = {60, 180, 330, 100};
    L.display = {410, 182, 1226, 250};
    {   // the sidechain EQ's curve over what the detector hears, and the last few seconds
        Widget curve;
        curve.kind = WKind::CurveDisplay;
        curve.r = {410, 182, 600, 250};
        L.widgets.push_back(curve);
        L.recesses.push_back({curve.r, 14, 9, false, true});
        Widget hist;
        hist.kind = WKind::History;
        hist.r = {1026, 182, 610, 250};
        L.widgets.push_back(hist);
        L.recesses.push_back({hist.r, 14, 9, false, true});
    }
    // the sidechain strip
    L.widgets.push_back(toggle({410, 462, 150, 40}, ScEq, "SC EQ"));
    L.widgets.push_back(toggle({410, 516, 150, 40}, ScListen, "LISTEN"));
    L.widgets.push_back(choiceButton({575, 462, 150, 40}, ScSource, 0, ""));
    L.widgets.push_back(choiceButton({575, 516, 150, 40}, ScSource, 1, ""));
    {
        const std::size_t first = L.widgets.size();
        const struct { int p; const char* cap; } sk[6] = {{ScHpf, "SC HPF"}, {ScBellFreq, "BELL FREQ"}, {ScBellGain, "BELL GAIN"},
                                                         {ScBellQ, "BELL Q"}, {ScLpf, "SC LPF"}, {ScLink, "SC LINK"}};
        for (int i = 0; i < 6; ++i) {
            const float cx = 814 + static_cast<float>(i) * 149;
            L.widgets.push_back(knob({cx - 32, 476, 64, 64}, sk[i].p, sk[i].cap, "", "", true));
        }
        seatKnobs(L, first, 9);
    }
    L.grooves.push_back({410, 592, 1636, 592});
    // the two channels: INPUT, OUTPUT, ATTACK, RELEASE named once over the columns, the rows tagged 1 and 2
    // (two captioned rows do not fit between the sidechain strip and the footer)
    {
        const std::size_t first = L.widgets.size();
        const int ch1[4] = {Input1, Output1, Attack1, Release1}, ch2[4] = {Input2, Output2, Attack2, Release2};
        const char* cap[4] = {"INPUT", "OUTPUT", "ATTACK", "RELEASE"};
        constexpr float kRow1 = 628, kRow2 = 726, kDial = 60;
        for (int i = 0; i < 4; ++i) {
            const float cx = 490 + static_cast<float>(i) * 116;
            L.widgets.push_back(knob({cx - kDial * 0.5f, kRow1, kDial, kDial}, ch1[i], cap[i], "", "", true));
            L.widgets.push_back(knob({cx - kDial * 0.5f, kRow2, kDial, kDial}, ch2[i], "", "", "", true));
        }
        seatKnobs(L, first, 8);
        L.sections.push_back({{406, kRow1 + 15, 40, 30}, "1"});
        L.sections.push_back({{406, kRow2 + 15, 40, 30}, "2"});
    }
    L.grooves.push_back({895, 604, 895, 820});
    // the needles: channel 1 and channel 2
    for (int c = 0; c < 2; ++c) {
        Widget gr;
        gr.kind = WKind::GrMeter;
        gr.choice = c;
        gr.caption = c == 0 ? "CHANNEL 1  (L / M)" : "CHANNEL 2  (R / S)";
        gr.r = {910 + static_cast<float>(c) * 245, 606, 235, 212};
        L.widgets.push_back(gr);
        L.recesses.push_back({gr.r, 12, 8, false, true});
    }
    // saturation
    L.sections.push_back({{1400, 596, 236, 28}, "SATURATION"});
    for (int i = 0; i < 5; ++i) L.widgets.push_back(choiceButton({1400, 630 + static_cast<float>(i) * 38, 236, 32}, Saturation, i));
    // the left column: mode, link, ratio, lookahead, mix, power
    L.widgets.push_back(choiceButton({70, 330, 130, 40}, Mode, 0, ""));
    L.widgets.push_back(choiceButton({210, 330, 130, 40}, Mode, 1, ""));
    L.widgets.push_back(toggle({70, 380, 270, 40}, Link, "LINK"));
    L.sections.push_back({{60, 428, 290, 26}, "RATIO"});
    for (int i = 0; i < 4; ++i) L.widgets.push_back(choiceButton({70 + static_cast<float>(i) * 69, 456, 63, 30}, Ratio, i));
    L.widgets.push_back(choiceButton({70, 490, 270, 32}, Ratio, 4));   // STACKED: every ratio at once
    {
        const std::size_t first = L.widgets.size();
        L.widgets.push_back(knob({100, 560, 70, 70}, Lookahead, "LOOKAHEAD", "", "", true));
        L.widgets.push_back(knob({240, 560, 70, 70}, Mix, "MIX", "", "", true));
        seatKnobs(L, first, 10);
    }
    buttonSeats(L);
    bypassAt(L, {160, 692, 90, 90});
    return L;
}

// ---- TERRITORY: a column per band (its gain change on top, THRESHOLD / RANGE / GAIN under it), the
// global knobs and the crossovers taking turns across one row, the switches down the left
// SWITCHBLADE, the channel compander: the history of both gain changes and the filter on top, then the
// compressor row and the gate row, each with its switches and steppers on the right; input, output and
// look-ahead down the left.
FaceLayout companderFace(const rack::PluginInfo& info) {
    using namespace rack::switchblade;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"COMPRESS, GATE", "AND STEER IT"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 182, 620, 196};
    Widget hist;
    hist.kind = WKind::History;
    hist.r = L.display;
    hist.lo = "COMP";
    hist.hi = "GATE";
    L.widgets.push_back(hist);
    L.recesses.push_back({hist.r, 14, 9, false, true});
    auto stepper = [&](Rect r, int param, const char* caption) {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.caption = caption;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    // the filter
    buttonGroup(L, "FILTER", 1060, 576, 222, FilterType, 4);
    std::size_t first = L.widgets.size();
    L.widgets.push_back(knob({1090, 304, 60, 60}, FilterFreq, "FREQ", "", "", true));
    L.widgets.push_back(knob({1210, 304, 60, 60}, FilterQ, "Q", "", "", true));
    seatKnobs(L, first, 8);
    stepper({1466, 290, 170, 40}, Monitor, "MONITOR");
    stepper({1466, 340, 170, 40}, Key, "KEY");
    L.grooves.push_back({410, 404, 1636, 404});
    // the compressor row
    first = L.widgets.size();
    const struct { int p; const char* cap; } comp[6] = {{Threshold, "THRESHOLD"}, {Ratio, "RATIO"}, {Attack, "ATTACK"},
                                                         {Release, "RELEASE"}, {Pdr, "SMART REL"}, {Makeup, "MAKEUP"}};
    for (int k = 0; k < 6; ++k) {
        const float cx = 410 + (static_cast<float>(k) + 0.5f) * (820.0f / 6.0f);
        L.widgets.push_back(knob({cx - 35, 446, 70, 70}, comp[k].p, comp[k].cap, "", "", true));
    }
    seatKnobs(L, first, 8);
    L.widgets.push_back(toggle({1260, 424, 176, 44}, CompOn, "COMP ON"));
    L.widgets.push_back(toggle({1452, 424, 176, 44}, Cancel, "CANCEL"));
    stepper({1466, 480, 170, 40}, LevelRef, "LEVEL REF");
    stepper({1466, 530, 170, 40}, CompEq, "COMP EQ");
    L.grooves.push_back({410, 586, 1636, 586});
    // the gate row
    first = L.widgets.size();
    const struct { int p; const char* cap; } gate[7] = {{GateOpen, "GATE OPEN"}, {GateClose, "CLOSE"}, {Floor, "FLOOR"}, {ExpSlope, "SLOPE"},
                                                         {GateAttack, "ATTACK"}, {GateRelease, "RELEASE"}, {GateHold, "HOLD"}};
    for (int k = 0; k < 7; ++k) {
        const float cx = 410 + (static_cast<float>(k) + 0.5f) * (820.0f / 7.0f);
        L.widgets.push_back(knob({cx - 33, 632, 66, 66}, gate[k].p, gate[k].cap, "", "", true));
    }
    seatKnobs(L, first, 8);
    L.widgets.push_back(toggle({1260, 612, 176, 44}, GateOn, "GATE ON"));
    L.widgets.push_back(toggle({1452, 612, 176, 44}, Link, "LINK"));
    stepper({1466, 668, 170, 40}, GateMode, "MODE");
    stepper({1466, 718, 170, 40}, GateEq, "GATE EQ");
    // the left column: input left / right, output, look-ahead, the power
    first = L.widgets.size();
    L.widgets.push_back(knob({95, 470, 70, 70}, InputL, "INPUT L", "", "", true));
    L.widgets.push_back(knob({225, 470, 70, 70}, InputR, "INPUT R", "", "", true));
    L.widgets.push_back(knob({95, 616, 70, 70}, Output, "OUTPUT", "", "", true));
    seatKnobs(L, first, 8);
    L.widgets.push_back(toggle({70, 740, 270, 44}, Lookahead, "LOOKAHEAD"));
    buttonSeats(L);
    bypassAt(L, {230, 606, 90, 90});
    return L;
}

// ---- INSIDE MAN: the EQ curve and the two reduction readouts on top; gate and compressor side by side,
// each a row of knobs over a row of switches (named above them); the four EQ bands under them; order,
// phase, output and rotate down the left.
FaceLayout channelFace(const rack::PluginInfo& info) {
    using namespace rack::insideman;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"GATE, SQUEEZE", "AND SHAPE IT"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 820, 150};
    Widget cv;
    cv.kind = WKind::CurveDisplay;
    cv.r = L.display;
    L.widgets.push_back(cv);
    L.recesses.push_back({cv.r, 14, 9, false, true});
    readoutAt(L, {1246, 180, 188, 150}, 0, "GATE", "dB", "+");
    readoutAt(L, {1448, 180, 188, 150}, 1, "COMP", "dB", "+");
    L.grooves.push_back({410, 342, 1636, 342});
    auto stepper = [&](Rect r, int param) {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    // a dynamics section: knobs (param, caption) over switches (param, name, stepper?) in [x0, x0 + w)
    struct K { int p; const char* cap; };
    struct S { int p; const char* cap; bool stepper; };
    auto section = [&](float x0, float w, const char* name, std::span<const K> ks, std::span<const S> ss) {
        L.sections.push_back({{x0, 346, w, 26}, name});
        const std::size_t first = L.widgets.size();
        const float kp = w / static_cast<float>(ks.size());
        for (std::size_t k = 0; k < ks.size(); ++k) {
            const float cx = x0 + (static_cast<float>(k) + 0.5f) * kp;
            L.widgets.push_back(knob({cx - 24, 408, 48, 48}, ks[k].p, ks[k].cap, "", "", true));
        }
        seatKnobs(L, first, 6);
        const float sp = w / static_cast<float>(ss.size());
        for (std::size_t k = 0; k < ss.size(); ++k) {
            const float x = x0 + static_cast<float>(k) * sp;
            L.sections.push_back({{x + 2, 482, sp - 4, 22}, ss[k].cap});
            const Rect r{x + 5, 508, sp - 10, 40};
            if (ss[k].stepper) stepper(r, ss[k].p);
            else L.widgets.push_back(toggle(r, ss[k].p, ""));
        }
    };
    constexpr K gk[] = {{GateThreshold, "THRESH"}, {Floor, "FLOOR"}, {GateRelease, "RELEASE"}, {GKeyFreq, "KEY HZ"}, {GKeyWidth, "WIDTH"}};
    constexpr S gs[] = {{GateOn, "GATE", false}, {GateMode, "MODE", true}, {GKeyOn, "KEY", false}, {GKeySource, "FROM", true},
                        {GKeyType, "FILTER", true}};
    constexpr K ck[] = {{CompThreshold, "THRESH"}, {Ratio, "RATIO"}, {Attack, "ATTACK"}, {Release, "RELEASE"}, {CKeyFreq, "KEY HZ"},
                        {CKeyWidth, "WIDTH"}};
    constexpr S cs[] = {{CompOn, "COMP", false}, {CompType, "TYPE", true}, {Arc, "AUTO REL", false}, {CKeyOn, "KEY", false},
                        {CKeySource, "FROM", true}, {CKeyType, "FILTER", true}};
    section(410, 590, "GATE / EXPANDER", gk, gs);
    L.grooves.push_back({1013, 352, 1013, 550});
    section(1026, 610, "COMPRESSOR", ck, cs);
    L.grooves.push_back({410, 562, 1636, 562});
    // the EQ: a block per band
    const float bw = 1226.0f / kBands;
    static const char* const kBandN[kBands] = {"BAND 1", "BAND 2", "BAND 3", "BAND 4"};
    for (int b = 0; b < kBands; ++b) {
        const float x0 = 410 + static_cast<float>(b) * bw;
        L.sections.push_back({{x0, 568, bw, 24}, kBandN[b]});
        const std::size_t first = L.widgets.size();
        const struct { int p; const char* cap; } kk[3] = {{kFreq[b], "FREQ"}, {kGain[b], "GAIN"}, {kQ[b], "Q"}};
        for (int k = 0; k < 3; ++k) {
            const float cx = x0 + (static_cast<float>(k) + 0.5f) * (bw / 3.0f);
            L.widgets.push_back(knob({cx - 24, 624, 48, 48}, kk[k].p, kk[k].cap, "", "", true));
        }
        seatKnobs(L, first, 6);
        if (kType[b] >= 0) {
            stepper({x0 + 8, 706, bw * 0.62f - 12, 40}, kType[b]);
            L.widgets.push_back(toggle({x0 + bw * 0.62f + 4, 706, bw * 0.38f - 12, 40}, kOn[b], ""));
        } else {
            L.widgets.push_back(toggle({x0 + bw * 0.25f, 706, bw * 0.5f, 40}, kOn[b], ""));
        }
        if (b > 0) L.grooves.push_back({x0, 572, x0, 748});
    }
    // the left column
    stepper({160, 452, 180, 40}, Order);
    L.sections.push_back({{60, 452, 96, 40}, "ORDER"});
    L.widgets.push_back(toggle({70, 502, 270, 40}, Phase, "PHASE"));
    const std::size_t first = L.widgets.size();
    L.widgets.push_back(knob({100, 580, 56, 56}, Output, "OUTPUT", "", "", true));
    L.widgets.push_back(knob({230, 580, 56, 56}, Rotate, "PLACE", "", "", true));
    seatKnobs(L, first, 6);
    buttonSeats(L);
    bypassAt(L, {160, 690, 80, 80});
    return L;
}

// ---- THE SETUP: the curve and the filter on top; the three movers with their depths; the sixteen steps;
// the delay; input, output and mix down the left.
FaceLayout moversFace(const rack::PluginInfo& info) {
    using namespace rack::thesetup;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"THE FILTER MOVES", "YOU CALL THE MOVES"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 490, 150};
    Widget cv;
    cv.kind = WKind::CurveDisplay;
    cv.r = L.display;
    L.widgets.push_back(cv);
    L.recesses.push_back({cv.r, 14, 9, false, true});
    auto stepper = [&](Rect r, int param, const char* caption = "") {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.caption = caption;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, float d, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - d * 0.5f, y, d, d}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 5);
    };
    // the filter, beside the curve
    stepper({1000, 186, 210, 38}, Kind, "TYPE");
    stepper({1330, 186, 160, 38}, Slope, "SLOPE");
    knobs(912, 724, 256, 40, {{Freq, "FREQ"}, {CombDelay, "DELAY"}, {Gain, "GAIN"}, {Res, "RES"}, {CombFeedback, "FEEDBACK"}});
    L.grooves.push_back({410, 344, 1636, 344});
    // the globals
    knobs(410, 1226, 376, 38, {{Spread, "SPREAD"}, {Smooth, "GLIDE"}, {Drive, "DRIVE"}, {Crush, "CRUSH"}, {Mix, "MIX"}, {Input, "INPUT"},
                               {Output, "OUTPUT"}});
    L.grooves.push_back({410, 448, 1636, 448});
    // the movers: the LFO, the sequencer, and the depths (mover x target)
    L.sections.push_back({{410, 452, 290, 22}, "LFO"});
    L.sections.push_back({{712, 452, 300, 22}, "SEQUENCER"});
    knobs(410, 120, 508, 38, {{LfoRate, "RATE"}});
    L.widgets.push_back(toggle({536, 480, 160, 34}, LfoSync, "SYNC"));
    stepper({536, 522, 160, 34}, LfoNote);
    stepper({536, 564, 160, 34}, LfoWave);
    knobs(712, 300, 508, 34, {{SeqRate, "RATE"}, {Swing, "SWING"}, {Last, "LAST"}});
    L.widgets.push_back(toggle({716, 582, 140, 34}, SeqSync, "SYNC"));
    stepper({866, 582, 140, 34}, SeqNote);
    L.grooves.push_back({706, 456, 706, 642});
    L.grooves.push_back({1018, 456, 1018, 642});
    static const char* const kTargetN[kTargets] = {"> FREQ", "> RES", "> TIME"};
    static const char* const kMoverN[kMovers] = {"LFO", "SEQ", "FOLLOW"};
    for (int t = 0; t < kTargets; ++t) L.sections.push_back({{1110 + static_cast<float>(t) * 175, 454, 175, 22}, kTargetN[t]});
    {
        const std::size_t first = L.widgets.size();
        for (int m = 0; m < kMovers; ++m) {
            const float y = 478 + static_cast<float>(m) * 58;
            L.sections.push_back({{1022, y, 84, 26}, kMoverN[m]});
            for (int t = 0; t < kTargets; ++t) {
                const float cx = 1110 + (static_cast<float>(t) + 0.5f) * 175;
                L.widgets.push_back(knob({cx - 12, y, 24, 24}, depth(m, t), "", "", "", true));
            }
        }
        seatKnobs(L, first, 3);
    }
    L.grooves.push_back({410, 650, 1636, 650});
    // the steps
    {
        const std::size_t first = L.widgets.size();
        static const char* const kNum[kSteps] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
        const float pitch = 1226.0f / kSteps;
        for (int k = 0; k < kSteps; ++k) {
            const float cx = 410 + (static_cast<float>(k) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - 15, 676, 30, 30}, Step1 + k, kNum[k], "", "", true));
        }
        seatKnobs(L, first, 3);
    }
    L.grooves.push_back({410, 740, 1636, 740});
    // the delay
    L.sections.push_back({{60, 772, 340, 22}, "DELAY"});
    knobs(410, 480, 770, 28, {{DelayTime, "TIME"}, {DelayFeedback, "FEEDBACK"}, {DelayMix, "DRY / WET"}});
    const struct { int p; const char* cap; bool stepper; } sw[5] = {
        {DelaySync, "SYNC", false}, {DelayNote, "NOTE", true}, {DelayRight, "RIGHT", true}, {DelayAnalog, "STYLE", true}, {DelayRoute, "ROUTE", true}};
    for (int k = 0; k < 5; ++k) {
        const float x = 900 + static_cast<float>(k) * 147.0f;
        L.sections.push_back({{x, 744, 143, 22}, sw[k].cap});
        const Rect r{x + 2, 770, 139, 32};
        if (sw[k].stepper) stepper(r, sw[k].p);
        else L.widgets.push_back(toggle(r, sw[k].p, ""));
    }
    buttonSeats(L);
    bypassAt(L, {160, 560, 90, 90});
    return L;
}

// ---- SIX SHOOTER: a column per tap on top; direct, tempo and the modulator under them; the feedback at
// the bottom; the held peaks down the left.
FaceLayout tapsFace(const rack::PluginInfo& info) {
    using namespace rack::sixshooter;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"SIX TAPS", "EACH ITS OWN"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 300};
    auto stepper = [&](Rect r, int param, const char* caption = "") {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.caption = caption;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, float d, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - d * 0.5f, y, d, d}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 4);
    };
    // the taps
    static const char* const kTapN[kTaps] = {"TAP 1", "TAP 2", "TAP 3", "TAP 4", "TAP 5", "TAP 6"};
    const float cw = 1226.0f / kTaps;
    for (int t = 0; t < kTaps; ++t) {
        const float x0 = 410 + static_cast<float>(t) * cw;
        L.sections.push_back({{x0 + 4, 180, cw * 0.5f - 8, 26}, kTapN[t]});
        L.widgets.push_back(toggle({x0 + cw * 0.5f + 2, 180, cw * 0.5f - 10, 30}, tap(t, On), ""));
        knobs(x0, cw, 236, 32, {{tap(t, Delay), "DELAY"}, {tap(t, Gain), "GAIN"}, {tap(t, Rotate), "PLACE"}});
        L.widgets.push_back(toggle({x0 + 6, 312, 88, 30}, tap(t, EqOn), "EQ"));
        stepper({x0 + 100, 312, cw - 108, 30}, tap(t, EqType));
        knobs(x0, cw, 376, 32, {{tap(t, EqFreq), "EQ FREQ"}, {tap(t, EqGain), "EQ GAIN"}});
        if (t > 0) L.grooves.push_back({x0, 184, x0, 446});
    }
    L.grooves.push_back({410, 454, 1636, 454});
    // direct, tempo, modulator
    L.sections.push_back({{410, 460, 230, 22}, "DRY"});
    L.sections.push_back({{648, 460, 420, 22}, "TEMPO"});
    L.sections.push_back({{1076, 460, 560, 22}, "WOBBLE"});
    L.widgets.push_back(toggle({414, 490, 80, 32}, DirectOn, ""));
    knobs(494, 146, 512, 32, {{DirectGain, "GAIN"}, {DirectRotate, "PLACE"}});
    stepper({652, 490, 130, 32}, Sync);
    knobs(782, 100, 512, 32, {{Bpm, "BPM"}});
    stepper({886, 490, 120, 32}, Grid);
    L.widgets.push_back(toggle({886, 530, 120, 32}, Snap, "LOCK"));
    L.widgets.push_back(toggle({652, 530, 130, 32}, Tap, "TAP"));
    L.widgets.back().hi = "PRESS";
    L.widgets.push_back(toggle({1080, 490, 110, 32}, ModOn, ""));
    stepper({1080, 530, 110, 32}, ModSync);
    knobs(1196, 210, 512, 32, {{ModRate, "RATE"}, {ModDepth, "DEPTH"}});
    stepper({1416, 510, 210, 32}, ModNote, "");
    L.grooves.push_back({644, 462, 644, 584});
    L.grooves.push_back({1072, 462, 1072, 584});
    L.grooves.push_back({410, 592, 1636, 592});
    // the feedback
    L.sections.push_back({{410, 598, 1226, 22}, "FEEDBACK"});
    L.widgets.push_back(toggle({414, 632, 90, 32}, FbOn, ""));
    stepper({414, 676, 90, 32}, FbMode);
    knobs(510, 450, 640, 32, {{FbGain, "GAIN"}, {FbDelay, "DELAY"}, {FbRotate, "PLACE"}});
    stepper({966, 654, 150, 32}, FbFilter);
    knobs(1120, 300, 640, 32, {{FbFreq, "FREQ"}, {FbFilterGain, "GAIN"}});
    knobs(1430, 206, 632, 44, {{Output, "OUTPUT"}});
    // the left column: the peaks
    readoutAt(L, {70, 404, 270, 64}, 0, "PEAK L", "dBFS", "+");
    readoutAt(L, {70, 478, 270, 64}, 1, "PEAK R", "dBFS", "+");
    L.widgets.push_back(toggle({70, 554, 270, 36}, Reset, "RESET PEAKS"));
    L.widgets.back().hi = "PRESS";
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

// ---- BODY DOUBLE: a column per voice on top; the direct, the shared settings and the voices' EQ under them.
FaceLayout voicesFace(const rack::PluginInfo& info) {
    using namespace rack::bodydouble;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"TWO OF YOU", "OR FOUR"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 300};
    auto stepper = [&](Rect r, int param, const char* caption = "") {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.caption = caption;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, float d, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - d * 0.5f, y, d, d}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 4);
    };
    static const char* const kVoiceN[kVoices] = {"VOICE 1", "VOICE 2", "VOICE 3", "VOICE 4"};
    const float cw = 1226.0f / kVoices;
    for (int v = 0; v < kVoices; ++v) {
        const float x0 = 410 + static_cast<float>(v) * cw;
        L.sections.push_back({{x0 + 4, 180, 110, 28}, kVoiceN[v]});
        L.widgets.push_back(toggle({x0 + 112, 180, 76, 30}, voice(v, On), ""));
        L.widgets.push_back(toggle({x0 + 194, 180, cw - 200, 30}, voice(v, Octave), "OCT"));
        knobs(x0, cw, 240, 34, {{voice(v, Gain), "GAIN"}, {voice(v, Pan), "PAN"}, {voice(v, Delay), "DELAY"}, {voice(v, Detune), "DETUNE"}});
        knobs(x0, cw, 336, 34, {{voice(v, Feedback), "FEEDBACK"}, {voice(v, ModDepth), "MOD DEPTH"}, {voice(v, ModRate), "MOD RATE"}});
        if (v > 0) L.grooves.push_back({x0, 184, x0, 410});
    }
    L.grooves.push_back({410, 420, 1636, 420});
    // the direct and the shared settings
    L.sections.push_back({{410, 426, 400, 22}, "DRY"});
    L.widgets.push_back(toggle({414, 456, 130, 32}, DirectOn, ""));
    knobs(560, 250, 474, 32, {{DirectGain, "GAIN"}, {DirectPan, "PAN"}});
    L.widgets.push_back(toggle({414, 500, 130, 32}, AlignDirect, "IN TIME"));
    L.sections.push_back({{820, 426, 816, 22}, "ALL VOICES"});
    stepper({920, 456, 150, 32}, Range, "LOWEST");
    stepper({920, 500, 150, 32}, Routing, "FEED");
    L.widgets.push_back(toggle({1084, 456, 170, 32}, ModReset, "MOD RESET"));
    L.widgets.back().hi = "PRESS";
    knobs(1260, 376, 466, 32, {{FbHighPass, "FB HI-PASS"}, {Output, "OUTPUT"}});
    L.grooves.push_back({814, 428, 814, 552});
    L.grooves.push_back({410, 560, 1636, 560});
    // the voices' EQ
    L.sections.push_back({{410, 566, 600, 22}, "LOW SHELF"});
    L.sections.push_back({{1036, 566, 600, 22}, "HIGH SHELF"});
    L.widgets.push_back(toggle({420, 600, 110, 32}, LowOn, ""));
    knobs(540, 470, 614, 34, {{LowGain, "GAIN"}, {LowFreq, "FREQ"}});
    L.widgets.push_back(toggle({1046, 600, 110, 32}, HighOn, ""));
    knobs(1166, 470, 614, 34, {{HighGain, "GAIN"}, {HighFreq, "FREQ"}});
    L.grooves.push_back({1023, 568, 1023, 690});
    // the left column: the peaks
    readoutAt(L, {70, 404, 270, 64}, 0, "PEAK L", "dBFS", "+");
    readoutAt(L, {70, 478, 270, 64}, 1, "PEAK R", "dBFS", "+");
    L.widgets.push_back(toggle({70, 554, 270, 36}, Reset, "RESET PEAKS"));
    L.widgets.back().hi = "PRESS";
    buttonSeats(L);
    bypassAt(L, {160, 640, 90, 90});
    return L;
}

FaceLayout keysFace(const rack::PluginInfo& info) {
    using namespace rack::informant;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"THE WORDS TALK", "THE SYNTH SINGS"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    auto stepper = [&](Rect r, int param) {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    auto small = [&](float cx, float y, int param, const char* caption) {
        const std::size_t first = L.widgets.size();
        L.widgets.push_back(knob({cx - 17, y, 34, 34}, param, caption, "", "", true));
        seatKnobs(L, first, 4);
    };
    // the sources, the synth and the noise
    L.sections.push_back({{410, 180, 150, 22}, "MODULATOR"});
    stepper({410, 208, 150, 34}, Modulator);
    L.sections.push_back({{572, 180, 150, 22}, "CARRIER"});
    stepper({572, 208, 150, 34}, Carrier);
    L.sections.push_back({{734, 180, 180, 22}, "SOUND"});
    stepper({734, 208, 180, 34}, Sound);
    L.sections.push_back({{926, 180, 150, 22}, "NOISE"});
    stepper({926, 208, 150, 34}, NoiseFilter);
    small(690, 284, Tune, "TUNE");
    small(840, 284, Spread, "SPREAD");
    small(1001, 284, NoiseFreq, "NOISE FREQ");
    // the morphing
    L.grooves.push_back({1088, 184, 1088, 344});
    L.sections.push_back({{1100, 180, 536, 22}, "VOCODING"});
    small(1167, 240, Pressure, "SQUEEZE");
    small(1301, 240, Formant, "FORMANT");
    small(1435, 240, Smoothing, "LAG");
    small(1569, 240, Release, "HOLD");
    L.grooves.push_back({410, 352, 1636, 352});
    // the morphed sound's EQ
    static const char* const kEqN[kEqBands] = {"LOW SHELF", "BELL 2", "BELL 3", "BELL 4", "HIGH SHELF"};
    const float cw = 1226.0f / kEqBands;
    for (int b = 0; b < kEqBands; ++b) {
        const float x0 = 410 + static_cast<float>(b) * cw;
        L.widgets.push_back(toggle({x0 + 10, 364, cw - 20, 32}, eq(b, EqOn), kEqN[b]));
        small(x0 + cw * 0.5f - 76, 430, eq(b, EqFreq), "FREQ");
        small(x0 + cw * 0.5f, 430, eq(b, EqGain), "GAIN");
        small(x0 + cw * 0.5f + 76, 430, eq(b, EqQ), "Q");
        if (b > 0) L.grooves.push_back({x0, 360, x0, 500});
    }
    L.grooves.push_back({410, 508, 1636, 508});
    // the mixer
    static const char* const kPathN[kPaths] = {"CARRIER", "MODULATOR", "NOISE", "VOCODED"};
    const float mw = 1226.0f / kPaths;
    for (int q = 0; q < kPaths; ++q) {
        const float x0 = 410 + static_cast<float>(q) * mw;
        small(x0 + 60, 548, Level0 + q, kPathN[q]);
        L.widgets.push_back(toggle({x0 + 112, 536, 180, 32}, Solo0 + q, "SOLO"));
        L.widgets.push_back(toggle({x0 + 112, 574, 180, 32}, Mute0 + q, "MUTE"));
        if (q > 0) L.grooves.push_back({x0, 516, x0, 620});
    }
    // the keys
    Widget keys;
    keys.kind = WKind::Keyboard;
    keys.param = Key0;
    keys.choice = kKeys;
    keys.param2 = Sustain;
    keys.r = {410, 640, 1226, 172};
    L.widgets.push_back(keys);
    L.recesses.push_back({keys.r.inset(-6), 8, 6, false, false});
    // the left column: the output peaks and SUSTAIN
    readoutAt(L, {70, 404, 270, 64}, 0, "OUT L", "dBFS", "+");
    readoutAt(L, {70, 478, 270, 64}, 1, "OUT R", "dBFS", "+");
    L.widgets.push_back(toggle({70, 560, 270, 40}, Sustain, "LATCH"));
    buttonSeats(L);
    bypassAt(L, {160, 650, 90, 90});
    return L;
}

FaceLayout notchesFace(const rack::PluginInfo& info) {
    using namespace rack::coldcase;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"THE NOTCHES MOVE", "THE LOOP RINGS"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    auto stepper = [&](Rect r, int param, const char* label) {
        L.sections.push_back({{r.x, r.y - 26, r.w, 22}, label});
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - 19, y, 38, 38}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 4);
    };
    // the notches
    stepper({410, 210, 120, 32}, Notches, "PAIRS");
    L.widgets.push_back(toggle({540, 210, 110, 32}, Half, "HALF"));
    L.widgets.push_back(toggle({660, 210, 110, 32}, NotchFlip, "FLIP"));
    L.widgets.push_back(toggle({780, 210, 220, 32}, LowCutOn, "LOW CUT"));
    knobs(410, 590, 276, {{Depth, "DEPTH"}, {StereoSpread, "STEREO"}, {LowCut, "LOW CUT HZ"}});
    L.grooves.push_back({1012, 184, 1012, 344});
    // the sweep
    L.widgets.push_back(toggle({1024, 210, 110, 32}, Run, "RUN"));
    stepper({1144, 210, 150, 32}, Wave, "WAVE");
    stepper({1304, 210, 160, 32}, Multiplier, "MULTIPLIER");
    stepper({1474, 210, 162, 32}, Tempo, "TEMPO");
    knobs(1024, 612, 276, {{Rate, "RATE"}, {Bpm, "BPM"}, {LowHz, "LOW"}, {HighHz, "HIGH"}});
    L.grooves.push_back({410, 352, 1636, 352});
    // the loop
    L.widgets.push_back(toggle({410, 392, 140, 32}, LoopOn, "LOOP"));
    L.widgets.push_back(toggle({560, 392, 140, 32}, LoopFlip, "FLIP"));
    stepper({710, 392, 170, 32}, LoopFilter, "LOOP FILTER");
    knobs(890, 746, 380, {{Decay, "DECAY"}, {Delay, "DELAY"}, {Density, "DENSITY"}, {HiTame, "HI TAME"}, {LoopFreq, "FILTER HZ"}});
    L.grooves.push_back({410, 472, 1636, 472});
    // the wet sound
    L.widgets.push_back(toggle({410, 520, 180, 32}, HighCutOn, "HIGH CUT"));
    stepper({600, 520, 180, 32}, WetMode, "WET MODE");
    knobs(790, 846, 510, {{HighCut, "HIGH CUT HZ"}, {Width, "WIDTH"}, {Rotate, "ROTATE"}, {PreDelay, "PRE-DELAY"}, {Mix, "MIX"}, {Gain, "GAIN"}});
    // the left column
    readoutAt(L, {70, 404, 270, 64}, 0, "PEAK L", "dBFS", "+");
    readoutAt(L, {70, 478, 270, 64}, 1, "PEAK R", "dBFS", "+");
    L.widgets.push_back(toggle({70, 554, 270, 36}, Reset, "RESET PEAKS"));
    L.widgets.back().hi = "PRESS";
    buttonSeats(L);
    bypassAt(L, {160, 650, 90, 90});
    return L;
}

// PAWN SHOP: one row per module (its switch, its choices, its knobs), the master section beside the reverb.
FaceLayout pawnFace(const rack::PluginInfo& info) {
    using namespace rack::pawnshop;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"OLD GEAR", "NEW TRICKS"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    auto stepper = [&](Rect r, int param, const char* label) {
        L.sections.push_back({{r.x, r.y - 26, r.w, 22}, label});
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - 19, y, 38, 38}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 4);
    };
    // DEVICE
    L.widgets.push_back(toggle({410, 220, 140, 32}, DeviceOn, "DEVICE"));
    stepper({560, 220, 140, 32}, Device, "SOUND");
    stepper({710, 220, 130, 32}, Styler, "DECADE");
    knobs(850, 786, 208, {{Tone, "TONE"}, {Squash, "SQUASH"}, {Ringer, "RINGER"}, {DeviceMix, "MIX"}});
    L.grooves.push_back({410, 300, 1636, 300});
    // SPACE: the echo
    L.widgets.push_back(toggle({410, 340, 120, 32}, SpaceOn, "SPACE"));
    stepper({540, 340, 110, 32}, Sync, "SYNC");
    stepper({660, 340, 120, 32}, Note, "NOTE");
    L.widgets.push_back(toggle({790, 340, 180, 32}, PingPong, "PING PONG"));
    knobs(980, 656, 328, {{TimeMs, "TIME"}, {Bpm, "BPM"}, {EchoLevel, "ECHO"}, {Feedback, "FEEDBACK"}});
    // SPACE: the reverb, and the master section
    stepper({410, 460, 140, 32}, ReverbType, "REVERB");
    knobs(560, 452, 448, {{ReverbLevel, "LEVEL"}, {ReverbLength, "LENGTH"}, {SpaceMix, "SPACE MIX"}});
    L.grooves.push_back({1018, 420, 1018, 520});
    knobs(1024, 612, 448, {{Input, "INPUT"}, {Output, "OUTPUT"}, {LowCut, "LOW CUT"}, {HighCut, "HIGH CUT"}, {Mono, "MONO"}});
    L.grooves.push_back({410, 540, 1636, 540});
    // NOISE
    L.widgets.push_back(toggle({410, 580, 120, 32}, NoiseOn, "NOISE"));
    stepper({540, 580, 170, 32}, NoiseType, "TYPE");
    stepper({720, 580, 120, 32}, NoiseMode, "FOLLOWS");
    stepper({850, 580, 110, 32}, NoisePlace, "PLACE");
    knobs(970, 666, 568, {{Threshold, "THRESHOLD"}, {NoiseLevel, "LEVEL"}});
    L.grooves.push_back({410, 660, 1636, 660});
    // MECHANICS
    L.widgets.push_back(toggle({410, 700, 170, 32}, MechOn, "MECHANICS"));
    knobs(590, 1046, 688, {{WowA, "WOW A"}, {WobbleA, "WOBBLE A"}, {SpeedA, "SPEED A"}, {WowB, "WOW B"}, {WobbleB, "WOBBLE B"}, {SpeedB, "SPEED B"}});
    // the left column
    readoutAt(L, {70, 404, 270, 64}, 0, "IN", "dBFS", "+");
    readoutAt(L, {70, 478, 270, 64}, 1, "OUT", "dBFS", "+");
    buttonSeats(L);
    bypassAt(L, {160, 650, 90, 90});
    return L;
}

// THE AUDITOR: the readouts across the top, the settings under them, the filters at the bottom.
FaceLayout auditFace(const rack::PluginInfo& info) {
    using namespace rack::auditor;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"EVERY NUMBER", "ACCOUNTED FOR"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    auto stepper = [&](Rect r, int param, const char* label) {
        L.sections.push_back({{r.x, r.y - 26, r.w, 22}, label});
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - 19, y, 38, 38}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 4);
    };
    // the readouts
    readoutAt(L, {410, 190, 300, 96}, ShortTerm, "SHORT TERM", "LUFS", "+");
    readoutAt(L, {718, 190, 300, 96}, LongTerm, "LONG TERM", "LUFS", "+");
    readoutAt(L, {1026, 190, 300, 96}, Momentary, "MOMENTARY", "LUFS", "+");
    readoutAt(L, {1334, 190, 302, 96}, Range, "RANGE", "LU");
    readoutAt(L, {410, 296, 198, 64}, TruePeak, "TRUE PEAK", "dBTP", "+");
    readoutAt(L, {614, 296, 198, 64}, Reduction, "LIMITER", "dB", "+");
    readoutAt(L, {818, 296, 198, 64}, Trim, "TRIM", "dB", "+");
    readoutAt(L, {1022, 296, 198, 64}, Timer, "TIMER", "s", "0");
    readoutAt(L, {1226, 296, 200, 64}, Unders, "UNDERS", "", "0");
    readoutAt(L, {1432, 296, 204, 64}, Overs, "OVERS", "", "0");
    // the settings
    stepper({410, 420, 150, 32}, Method, "METHOD");
    stepper({570, 420, 150, 32}, Channels, "CHANNELS");
    stepper({730, 420, 180, 32}, Weighting, "WEIGHTING");
    stepper({920, 420, 170, 32}, ShortTime, "SHORT TIME");
    stepper({1100, 420, 170, 32}, Scale, "SCALE");
    stepper({1280, 420, 150, 32}, Measure, "MEASURE");
    knobs(410, 1226, 480, {{ShortMax, "SHORT MAX"}, {ShortMin, "SHORT MIN"}, {TruePeakMax, "TRUE PEAK MAX"}, {Target, "TARGET"}, {Gain, "GAIN"}});
    L.grooves.push_back({410, 590, 1636, 590});
    // the measuring filters
    L.widgets.push_back(toggle({410, 630, 190, 32}, LpfOn, "LOW-PASS"));
    stepper({610, 630, 120, 32}, LpfSlope, "LP SLOPE");
    L.widgets.push_back(toggle({740, 630, 200, 32}, HpfOn, "HIGH-PASS"));
    stepper({950, 630, 120, 32}, HpfSlope, "HP SLOPE");
    knobs(1080, 556, 618, {{LpfFreq, "LP FREQ"}, {HpfFreq, "HP FREQ"}});
    // the left column
    L.widgets.push_back(toggle({70, 404, 270, 36}, Limiter, "LIMITER"));
    L.widgets.push_back(toggle({70, 452, 130, 36}, Reset, "RESET"));
    L.widgets.back().hi = "PRESS";
    L.widgets.push_back(toggle({210, 452, 130, 36}, ClearPeaks, "PEAKS"));
    L.widgets.back().hi = "PRESS";
    buttonSeats(L);
    bypassAt(L, {160, 650, 90, 90});
    return L;
}

// STAKEOUT: the bands on the left, the stereo position on the right, the meters and settings under them.
// THE RACKET: the 26 pedals as buttons by kind across the top; under them the chosen pedal's own knobs (up to
// six seats) and switches (up to three seats). Every pedal's controls are laid out, each shown only while its
// pedal is picked, so they share the seats.
FaceLayout stompFace(const rack::PluginInfo& info) {
    using namespace rack::racket;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"ONE PEDAL", "AT A TIME"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    // the picker
    struct Row { const char* label; int first, count; };
    constexpr Row kRows[] = {{"DRIVE", PdOverdrive, 5}, {"MOVEMENT", PdFlange, 5}, {"", PdOctDown, 5}, {"SPACE", PdEcho, 4}, {"BASICS", PdSqueeze, 7}};
    const float pitch = 1226.0f / 7.0f;
    float y = 184.0f;
    for (const Row& row : kRows) {
        if (row.label[0] != '\0') L.sections.push_back({{410, y, pitch * static_cast<float>(row.count), 22}, row.label});
        for (int k = 0; k < row.count; ++k)
            L.widgets.push_back(choiceButton({410 + static_cast<float>(k) * pitch + 4, y + 24, pitch - 8, 34}, Which, row.first + k));
        y += 64.0f;
    }
    L.grooves.push_back({410, 512, 1636, 512});
    // the seats
    const float kPitch = 1226.0f / static_cast<float>(kMaxKnobs), sPitch = 1226.0f / static_cast<float>(kMaxSwitches);
    auto knobRect = [&](int s) { const float cx = 410 + (static_cast<float>(s) + 0.5f) * kPitch; return Rect{cx - 48, 566, 96, 96}; };
    for (int s = 0; s < kMaxKnobs; ++s) L.recesses.push_back({knobRect(s).inset(-14), 48 + 14, 3, true, false});
    for (int s = 0; s < kMaxSwitches; ++s) L.recesses.push_back({{410 + static_cast<float>(s) * sPitch + 16, 738, 378, 42}, 10, 4, false, false});
    // each pedal's controls
    const auto fx = info.create();
    const auto params = fx->params();
    for (int ped = 0; ped < kPedals; ++ped) {
        const PedalFace pf = pedalFace(ped);
        for (int k = 0; k < pf.knobCount && k < kMaxKnobs; ++k) {
            Widget w = knob(knobRect(k), pf.knobs[k].param, pf.knobs[k].caption);
            w.showParam = Which;
            w.showValue = ped;
            L.widgets.push_back(w);
        }
        for (int k = 0; k < pf.switchCount && k < kMaxSwitches; ++k) {
            const float x0 = 410 + static_cast<float>(k) * sPitch;
            const int prm = pf.switches[k].param;
            Widget w;
            if (params[static_cast<std::size_t>(prm)].taper == hc::fx::Taper::Toggle) {
                w = toggle({x0 + 20, 742, 370, 34}, prm, pf.switches[k].caption);
            } else {
                w.kind = WKind::Stepper;
                w.param = prm;
                w.caption = pf.switches[k].caption;
                w.r = {x0 + 150, 742, 240, 34};
            }
            w.showParam = Which;
            w.showValue = ped;
            L.widgets.push_back(w);
        }
    }
    // the board: input and its gain
    L.sections.push_back({{70, 384, 320, 22}, "THE BOARD"});
    Widget in;
    in.kind = WKind::Stepper;
    in.param = Input;
    in.caption = "INPUT";
    in.r = {150, 420, 230, 34};
    L.widgets.push_back(in);
    L.recesses.push_back({in.r.inset(-4), 9, 4, false, false});
    const std::size_t firstKnob = L.widgets.size();
    L.widgets.push_back(knob({170, 516, 70, 70}, InputGain, "INPUT GAIN", "-18", "+18", true));
    seatKnobs(L, firstKnob, 6);
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::ChoiceButton) L.recesses.push_back({w.r.inset(-4), 10, 4, false, false});
    bypassAt(L, {160, 660, 90, 90});
    return L;
}

// GRAND LARCENY: keys / mix / tone / output across the top, the five effects in columns, the keyboard below.
FaceLayout pianoFace(const rack::PluginInfo& info) {
    using namespace rack::grandlarceny;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"SIXTY-ONE KEYS", "NO QUESTIONS"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    struct K { int p; const char* cap; };
    auto knobs = [&](float x0, float w, float y, std::initializer_list<K> ks) {
        const std::size_t first = L.widgets.size();
        const float pitch = w / static_cast<float>(ks.size());
        int k = 0;
        for (const K& kk : ks) {
            const float cx = x0 + (static_cast<float>(k++) + 0.5f) * pitch;
            L.widgets.push_back(knob({cx - 19, y, 38, 38}, kk.p, kk.cap, "", "", true));
        }
        seatKnobs(L, first, 4);
    };
    auto stepper = [&](Rect r, int param, const char* caption) {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.caption = caption;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    // the top: keys, mix, tone, output
    L.sections.push_back({{410, 184, 230, 22}, "KEYS"});
    knobs(410, 230, 238, {{VelCurve, "VEL CURVE"}, {Formant, "FORMANT"}, {Tune, "TUNE"}});
    L.sections.push_back({{650, 184, 530, 22}, "MIX"});
    knobs(650, 530, 238, {{Main, "MAIN"}, {KeyUp, "KEY UP"}, {Mechanics, "ACTION"}, {PedalNoise, "PEDAL"}, {Squeeze, "SQUEEZE"}, {Sympathy, "SYMPATHY"}});
    L.sections.push_back({{1190, 184, 270, 22}, "TONE"});
    knobs(1190, 270, 238, {{Bass, "BASS"}, {Mid, "MID"}, {Treble, "TREBLE"}});
    L.sections.push_back({{1466, 184, 170, 22}, "OUTPUT"});
    knobs(1466, 170, 238, {{Volume, "VOLUME"}});
    for (float x : {645.0f, 1185.0f, 1463.0f}) L.grooves.push_back({x, 190, x, 300});
    L.grooves.push_back({410, 316, 1636, 316});
    // the effects, a column each
    const float cw = 1226.0f / 5.0f;
    struct Col { int on, sync, note, hz, depth, mix; const char* name; };
    const Col cols[3] = {{TremOn, TremSync, TremNote, TremHz, TremDepth, -1, "TREMOLO"},
                         {PanOn, PanSync, PanNote, PanHz, PanDepth, -1, "AUTO PAN"},
                         {PhOn, PhSync, PhNote, PhHz, PhDepth, PhMix, "PHASER"}};
    for (int c = 0; c < 3; ++c) {
        const float x0 = 410 + static_cast<float>(c) * cw;
        L.widgets.push_back(toggle({x0 + 8, 330, cw - 16, 30}, cols[c].on, cols[c].name));
        stepper({x0 + 72, 372, cw - 80, 30}, cols[c].sync, "RATE");
        stepper({x0 + 72, 412, cw - 80, 30}, cols[c].note, "NOTE");
        if (cols[c].mix >= 0) knobs(x0, cw, 478, {{cols[c].hz, "HZ"}, {cols[c].depth, "DEPTH"}, {cols[c].mix, "MIX"}});
        else knobs(x0, cw, 478, {{cols[c].hz, "HZ"}, {cols[c].depth, "DEPTH"}});
    }
    {
        const float x0 = 410 + 3 * cw;
        L.widgets.push_back(toggle({x0 + 8, 330, cw - 16, 30}, ChOn, "CHORUS"));
        knobs(x0, cw, 478, {{ChDepth, "DEPTH"}});
    }
    {
        const float x0 = 410 + 4 * cw;
        L.sections.push_back({{x0, 334, cw, 22}, "REVERB"});
        knobs(x0, cw, 382, {{RvMix, "MIX"}, {RvPre, "PRE-DELAY"}});
        knobs(x0, cw, 478, {{RvTime, "TIME"}, {RvDamp, "DAMP"}});
    }
    for (int c = 1; c < 5; ++c) L.grooves.push_back({410 + static_cast<float>(c) * cw, 326, 410 + static_cast<float>(c) * cw, 552});
    L.grooves.push_back({410, 568, 1636, 568});
    // the keyboard
    Widget keys;
    keys.kind = WKind::Keyboard;
    keys.param = Key0;
    keys.choice = kKeys;
    keys.param2 = Latch;
    keys.r = {410, 596, 1226, 214};
    L.widgets.push_back(keys);
    L.recesses.push_back({keys.r.inset(-6), 8, 6, false, false});
    // the left column
    readoutAt(L, {70, 396, 270, 52}, OutL, "OUT L", "dBFS", "+");
    readoutAt(L, {70, 456, 270, 52}, OutR, "OUT R", "dBFS", "+");
    L.widgets.push_back(toggle({70, 520, 270, 32}, Pedal, "PEDAL"));
    L.widgets.push_back(toggle({70, 558, 270, 32}, Latch, "LATCH"));
    L.widgets.push_back(toggle({70, 596, 270, 32}, FxOn, "EFFECTS"));
    L.widgets.push_back(toggle({70, 634, 270, 32}, Limit, "LIMIT"));
    buttonSeats(L);
    bypassAt(L, {160, 692, 90, 90});
    return L;
}

FaceLayout stakeoutFace(const rack::PluginInfo& info) {
    using namespace rack::stakeout;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {"EYES ON", "EVERY BAND"};
    L.title = {60, 190, 330, 110};
    L.display = {410, 180, 1226, 640};
    auto stepper = [&](Rect r, int param, const char* label) {
        L.sections.push_back({{r.x, r.y - 26, r.w, 22}, label});
        Widget st;
        st.kind = WKind::Stepper;
        st.param = param;
        st.r = r;
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
    };
    Widget rtaW;
    rtaW.kind = WKind::Rta;
    rtaW.r = {410, 180, 820, 380};
    L.recesses.push_back({rtaW.r, 14, 9, false, true});
    L.widgets.push_back(rtaW);
    Widget pol;
    pol.kind = WKind::Polar;
    pol.r = {1240, 180, 396, 300};
    L.recesses.push_back({pol.r, 14, 9, false, true});
    L.widgets.push_back(pol);
    readoutAt(L, {1240, 490, 196, 70}, ClipL, "CLIP L", "", "0");
    readoutAt(L, {1440, 490, 196, 70}, ClipR, "CLIP R", "", "0");
    // the meters
    readoutAt(L, {410, 580, 198, 64}, PeakL, "PEAK L", "dBFS", "+");
    readoutAt(L, {614, 580, 198, 64}, PeakR, "PEAK R", "dBFS", "+");
    readoutAt(L, {818, 580, 198, 64}, Sum, "SUM", "dB", "+");
    readoutAt(L, {1022, 580, 198, 64}, HoldPeakL, "HOLD L", "dBFS", "+");
    readoutAt(L, {1226, 580, 200, 64}, HoldPeakR, "HOLD R", "dBFS", "+");
    readoutAt(L, {1432, 580, 204, 64}, HoldSumM, "HOLD SUM", "dB", "+");
    // the settings
    stepper({410, 700, 130, 32}, Resolution, "LF RESOLUTION");
    stepper({550, 700, 110, 32}, Mode, "MODE");
    stepper({670, 700, 120, 32}, Channels, "CHANNELS");
    stepper({800, 700, 110, 32}, Weighting, "WEIGHTING");
    stepper({920, 700, 110, 32}, MeterMode, "SUM METER");
    L.widgets.push_back(toggle({1040, 700, 140, 32}, Freeze, "FREEZE"));
    L.widgets.push_back(toggle({1190, 700, 160, 32}, ShowMax, "MAX LINE"));
    {
        const std::size_t first = L.widgets.size();
        L.widgets.push_back(knob({1460, 688, 38, 38}, Response, "RESPONSE", "", "", true));
        seatKnobs(L, first, 4);
    }
    // the left column: the resets
    L.widgets.push_back(toggle({70, 404, 130, 36}, ClearMax, "MAX"));
    L.widgets.back().hi = "PRESS";
    L.widgets.push_back(toggle({210, 404, 130, 36}, ClearClips, "CLIPS"));
    L.widgets.back().hi = "PRESS";
    L.widgets.push_back(toggle({70, 452, 85, 36}, HoldL, "HOLD L"));
    L.widgets.back().hi = "PRESS";
    L.widgets.push_back(toggle({162, 452, 85, 36}, HoldR, "HOLD R"));
    L.widgets.back().hi = "PRESS";
    L.widgets.push_back(toggle({254, 452, 86, 36}, HoldSum, "SUM"));
    L.widgets.back().hi = "PRESS";
    buttonSeats(L);
    bypassAt(L, {160, 650, 90, 90});
    return L;
}

// ---- plates laid out from a PlateSpec (Face::Spec) -------------------------------------------
// The parts go where the plate's content leaves room: a screen (or none) on top, a column of
// buttons and switches beside it, one or two rows of knobs under it, choice lists down the left.
float specKnobSize(rack::KnobSize s) noexcept { return s == rack::KnobSize::Big ? 128.0f : s == rack::KnobSize::Small ? 76.0f : 100.0f; }
void specRow(FaceLayout& L, std::span<const rack::PlateKnob> ks, float topY, float maxSize = 128.0f) {
    if (ks.empty()) return;
    const float pitch = 1226.0f / static_cast<float>(ks.size());
    const std::size_t first = L.widgets.size();
    for (std::size_t i = 0; i < ks.size(); ++i) {
        const rack::PlateKnob& k = ks[i];
        const float d = std::min(specKnobSize(k.size), maxSize);
        const float cx = 410 + (static_cast<float>(i) + 0.5f) * pitch, y = topY + (128.0f - d) * 0.5f;
        L.widgets.push_back(knob({cx - d * 0.5f, y, d, d}, k.param, k.caption, k.lo, k.hi, d < 90.0f));
    }
    seatKnobs(L, first, 12);
}
// Button groups and switches stacked in a column (x, w) from y; returns where the column ends.
float specColumn(FaceLayout& L, std::span<const rack::PlateChoice> groups, std::span<const rack::PlateToggle> toggles,
                 const rack::PluginInfo& info, float x, float w, float y) {
    auto fx = info.create();
    const auto ps = fx->params();
    for (const rack::PlateChoice& g : groups) {
        const int count = static_cast<int>(ps[static_cast<std::size_t>(g.param)].max) + 1;
        buttonGroup(L, g.caption, x, w, y + 40, g.param, count);
        y += 96;
    }
    for (std::size_t i = 0; i < toggles.size(); i += 2) {
        const bool pair = i + 1 < toggles.size();
        const float tw = pair ? (w - 10) * 0.5f : w;
        L.widgets.push_back(toggle({x, y, tw, 44}, toggles[i].param, toggles[i].caption));
        if (pair) L.widgets.push_back(toggle({x + tw + 10, y, tw, 44}, toggles[i + 1].param, toggles[i + 1].caption));
        y += 56;
    }
    return y;
}

// A switch's words at the plate's smallest text size (the same estimate the layout tests use).
float switchWordsW(const char* caption) noexcept {
    float w = 0;
    for (const char* c = caption; *c; ++c) {
        const char ch = *c;
        if (ch == 'M' || ch == 'W') w += 15.0f;
        else if (ch == 'L' || ch == 'T' || ch == 'F' || ch == 'E') w += 10.5f;
        else if (ch == 'I' || ch == ' ' || ch == '.' || ch == ',' || ch == ':' || ch == '/' || ch == '-' || ch == '|') w += 5.0f;
        else if (ch >= '0' && ch <= '9') w += 8.5f;
        else w += 12.5f;
    }
    return w + 43.5f;   // "  OFF"
}

// The multiband plates, as a grid: a readout per column on top, the globals (row1, then the switches) at
// the column centres with the borders' knobs (crossovers) between the columns, then one row per band
// control, named once at the row's left end (knob rows, stepper rows, switch rows).
void specBands(FaceLayout& L, const rack::PlateSpec& S) {
    const rack::PlateBands& B = *S.bands;
    const int n = std::max(B.count, 1);
    constexpr float gx = 530.0f, labelX = 410.0f, labelW = 112.0f;
    const float colW = (1636.0f - gx) / static_cast<float>(n);
    auto colX = [&](int b) { return gx + static_cast<float>(b) * colW; };
    auto cx = [&](float c) { return gx + (c + 0.5f) * colW; };
    static const char* const kNames[] = {"BAND 1", "BAND 2", "BAND 3", "BAND 4", "BAND 5", "BAND 6", "BAND 7", "BAND 8"};
    // EQs (screen Curve): the curve on top, the grid (type first) under it, the globals at the bottom
    const bool eq = S.screen == rack::Screen::Curve;
    if (eq) {
        const Rect scr{labelX, 180, 1226, 160};
        L.display = scr;
        Widget cv;
        cv.kind = WKind::CurveDisplay;
        cv.r = scr;
        L.widgets.push_back(cv);
        L.recesses.push_back({scr, 14, 9, false, true});
    } else {
        L.display = {gx, 180, 1636.0f - gx, 64};
        if (B.meter >= 0)
            for (int b = 0; b < n && b < 8; ++b) {
                const bool master = B.lastIsMaster && b == n - 1;
                readoutAt(L, {colX(b) + 6, 180, colW - 12, 64}, B.meter + b, master ? "OUTPUT" : kNames[b], master ? "dBFS" : "dB", "+");
            }
    }
    // the globals: row1, the switches, the borders' knobs (crossovers) between the columns
    constexpr float d = 48.0f;
    auto globals = [&](float top) {
        const std::size_t first = L.widgets.size();
        const int items = static_cast<int>(S.row1.size() + S.toggles.size());
        // EQs spread the globals across the whole width; the multiband plates keep one per column
        const float pitch = items > n || eq ? static_cast<float>(n) / static_cast<float>(std::max(items, 1)) : 1.0f;
        int slot = 0;
        for (const rack::PlateKnob& k : S.row1) {
            const float x = gx + (static_cast<float>(slot++) + 0.5f) * pitch * colW;
            L.widgets.push_back(knob({x - d * 0.5f, top, d, d}, k.param, k.caption, k.lo, k.hi, true));
        }
        for (const rack::PlateToggle& t : S.toggles) {
            const float x = gx + (static_cast<float>(slot++) + 0.5f) * pitch * colW;
            const float tw = std::max(112.0f, switchWordsW(t.caption) + 18.0f);
            L.widgets.push_back(toggle({x - tw * 0.5f, top + 4, tw, 44}, t.param, t.caption));
            if (t.press) L.widgets.back().hi = "PRESS";
        }
        for (std::size_t i = 0; i < B.borders.size() && static_cast<int>(i) + 1 < n; ++i) {
            const float x = colX(static_cast<int>(i) + 1);
            const rack::PlateKnob& k = B.borders[i];
            L.widgets.push_back(knob({x - d * 0.5f, top, d, d}, k.param, k.caption, k.lo, k.hi, true));
        }
        seatKnobs(L, first, 6);
    };
    if (!eq) globals(270);
    const float gridTop = eq ? 356.0f : 360.0f;
    L.grooves.push_back({labelX, gridTop - 8, 1636, gridTop - 8});
    // the grid
    float y = gridTop;
    constexpr float knobRowH = 72.0f, rowH = 50.0f, kd = 40.0f;
    auto stepperRows = [&] {
        for (const rack::PlateChoice& st : B.steppers) {
            L.sections.push_back({{labelX, y + 6, labelW, 30}, st.caption});
            for (int b = 0; b < n; ++b) {
                if (B.lastIsMaster && b == n - 1) continue;
                Widget w;
                w.kind = WKind::Stepper;
                w.param = B.first + b * B.stride + st.param;
                const float sw = std::min(colW - 12.0f, 200.0f);
                w.r = {cx(static_cast<float>(b)) - sw * 0.5f, y + 4, sw, 40};
                if (B.pages > 1) w.pageStride = B.pageStride;
                L.widgets.push_back(w);
                L.recesses.push_back({w.r.inset(-4), 9, 4, false, false});
            }
            y += rowH;
        }
    };
    if (!B.columnNames.empty()) {   // the columns' names over the grid
        for (int b = 0; b < n && b < static_cast<int>(B.columnNames.size()); ++b)
            L.sections.push_back({{colX(b) + 4, y + 2, colW - 8, 24}, B.columnNames[static_cast<std::size_t>(b)]});
        y += 30;
    }
    if (eq) stepperRows();
    std::size_t first = L.widgets.size();
    for (const rack::PlateKnob& k : B.knobs) {
        L.sections.push_back({{labelX, y + 6, labelW, 30}, k.caption});
        for (int b = 0; b < n; ++b) {
            const float c = cx(static_cast<float>(b));
            L.widgets.push_back(knob({c - kd * 0.5f, y + 2, kd, kd}, B.first + b * B.stride + k.param, "", k.lo, k.hi, true));
            if (B.pages > 1) L.widgets.back().pageStride = B.pageStride;
        }
        y += knobRowH;
    }
    seatKnobs(L, first, 6);
    if (!eq) stepperRows();
    for (const rack::PlateToggle& t : B.toggles) {
        L.sections.push_back({{labelX, y + 6, labelW, 30}, t.caption});
        for (int b = 0; b < n; ++b) {
            if (B.lastIsMaster && b == n - 1) continue;
            L.widgets.push_back(toggle({colX(b) + 10, y + 4, colW - 20, 42}, B.first + b * B.stride + t.param, ""));
            if (B.pages > 1) L.widgets.back().pageStride = B.pageStride;
        }
        y += rowH;
    }
    for (int b = 1; b < n; ++b) L.grooves.push_back({colX(b), gridTop, colX(b), y});
    if (eq) {
        L.grooves.push_back({labelX, y + 8, 1636, y + 8});
        globals(y + 40);
    }
    // the page tabs (which set the grid edits), in the left column under the name
    if (B.pages > 1) {
        const float tw = (270.0f - 8.0f * static_cast<float>(B.pages - 1)) / static_cast<float>(B.pages);
        for (int pg = 0; pg < B.pages; ++pg) {
            Widget t;
            t.kind = WKind::PageTab;
            t.choice = pg;
            t.caption = pg < static_cast<int>(B.pageNames.size()) ? B.pageNames[static_cast<std::size_t>(pg)] : std::to_string(pg + 1);
            t.r = {70 + static_cast<float>(pg) * (tw + 8.0f), 396, tw, 40};
            L.widgets.push_back(t);
            L.recesses.push_back({t.r.inset(-4), 10, 4, false, false});
        }
    }
}

FaceLayout specFace(const rack::PluginInfo& info) {
    using rack::Screen;
    const rack::PlateSpec& S = *info.plate;
    FaceLayout L;
    common(L, info);
    L.leftNotes = {S.note1, S.note2};
    L.title = {60, 190, 330, 110};
    const bool screen = S.screen != Screen::None && !S.bands;
    const bool twoRows = !S.row2.empty();
    // what sits beside the screen
    const bool needleReadouts = S.screen == Screen::Needle && !S.readouts.empty();
    const bool column = screen && (!S.choices.empty() || !S.toggles.empty() || needleReadouts);
    if (S.bands) {
        specBands(L, S);
    } else if (screen) {
        const float sh = twoRows ? 250.0f : 330.0f;
        const Rect scr{410, 182, column ? 700.0f : 1226.0f, sh};
        L.display = scr;
        switch (S.screen) {
        case Screen::Needle: {
            Widget gr;
            gr.kind = WKind::GrMeter;
            gr.r = scr;
            gr.param2 = S.meterSelect;
            gr.meter = S.vuMeter;
            L.widgets.push_back(gr);
            L.recesses.push_back({scr, 14, 9, false, true});
            break;
        }
        case Screen::Spectrum: {
            Widget sp;
            sp.kind = WKind::Spectrum;
            sp.r = scr;
            L.widgets.push_back(sp);
            L.recesses.push_back({scr, 14, 9, false, true});
            break;
        }
        case Screen::Curve: {
            Widget cv;
            cv.kind = WKind::CurveDisplay;
            cv.r = scr;
            L.widgets.push_back(cv);
            L.recesses.push_back({scr, 14, 9, false, true});
            break;
        }
        case Screen::History: {
            Widget h;
            h.kind = WKind::History;
            h.r = scr;
            h.lo = S.historyA;
            h.hi = S.historyB;
            L.widgets.push_back(h);
            L.recesses.push_back({scr, 14, 9, false, true});
            break;
        }
        case Screen::Readouts: {
            const int n = static_cast<int>(S.readouts.size());
            const float gap = 16.0f, rw = (scr.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
            for (int i = 0; i < n; ++i) {
                const rack::PlateReadout& ro = S.readouts[static_cast<std::size_t>(i)];
                readoutAt(L, {scr.x + static_cast<float>(i) * (rw + gap), scr.y, rw, std::min(scr.h, 170.0f)}, ro.meter, ro.caption, ro.unit, ro.fmt);
            }
            break;
        }
        case Screen::None: break;
        }
        if (column) {
            float y = 182;
            if (needleReadouts)
                for (const rack::PlateReadout& ro : S.readouts) {
                    readoutAt(L, {1150, y, 486, 100}, ro.meter, ro.caption, ro.unit, ro.fmt);
                    y += 112;
                }
            specColumn(L, S.choices, S.toggles, info, 1150, 486, y);
        }
        const float groove = scr.bottom() + 12;
        L.grooves.push_back({410, groove, 1636, groove});
        if (twoRows) {
            specRow(L, S.row1, 470, 100);
            specRow(L, S.row2, 660, 76);
        } else {
            specRow(L, S.row1, 568);
        }
    } else {
        L.display = {410, 182, 1226, 600};
        float top = 182;
        int groupRows = 0;
        if (!S.choices.empty()) {   // the button groups across the top, at most two side by side
            auto fx = info.create();
            const auto ps = fx->params();
            const int perRow = S.choices.size() > 2 ? 2 : static_cast<int>(S.choices.size());
            const float gw = (1226.0f - 30.0f * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
            for (std::size_t i = 0; i < S.choices.size(); ++i) {
                const int count = static_cast<int>(ps[static_cast<std::size_t>(S.choices[i].param)].max) + 1;
                const int row = static_cast<int>(i) / perRow, colI = static_cast<int>(i) % perRow;
                buttonGroup(L, S.choices[i].caption, 410 + static_cast<float>(colI) * (gw + 30.0f), gw, 236 + static_cast<float>(row) * 94, S.choices[i].param, count);
                groupRows = row + 1;
            }
            top = 300;
        }
        if (twoRows) {
            const float r1 = groupRows == 0 ? 260.0f : groupRows == 1 ? 340.0f : 440.0f;
            specRow(L, S.row1, r1, groupRows == 2 ? 110.0f : 128.0f);
            const float g = groupRows == 2 ? 610.0f : 540.0f;
            L.grooves.push_back({410, g, 1636, g});
            specRow(L, S.row2, groupRows == 2 ? 660.0f : 600.0f, groupRows == 2 ? 76.0f : 128.0f);
        } else {
            specRow(L, S.row1, top == 182 ? 420.0f : (groupRows == 2 ? 500.0f : 470.0f));
        }
    }
    // the left column: choice lists, then switches (and the switches that found no room on top)
    float y = 448;
    for (const rack::PlateChoice& c : S.left) {
        auto fx = info.create();
        const int count = static_cast<int>(fx->params()[static_cast<std::size_t>(c.param)].max) + 1;
        for (int i = 0; i < count; ++i) L.widgets.push_back(choiceButton({70, y + static_cast<float>(i) * 46, 270, 40}, c.param, i, i == 0 ? c.caption : ""));
        y += static_cast<float>(count) * 46 + 44;
    }
    std::vector<rack::PlateToggle> lt(S.leftToggles.begin(), S.leftToggles.end());
    if (!screen && !S.bands) lt.insert(lt.end(), S.toggles.begin(), S.toggles.end());
    if (!S.left.empty()) y -= 30;
    for (const rack::PlateChoice& c : S.steppers) {
        Widget st;
        st.kind = WKind::Stepper;
        st.param = c.param;
        st.caption = c.caption;
        st.r = {170, y, 170, 40};
        L.widgets.push_back(st);
        L.recesses.push_back({st.r.inset(-4), 9, 4, false, false});
        y += 50;
    }
    for (const rack::PlateToggle& t : lt) {
        L.widgets.push_back(toggle({70, y, 270, 44}, t.param, t.caption));
        if (t.press) L.widgets.back().hi = "PRESS";
        y += 54;
    }
    buttonSeats(L);
    bypassAt(L, {160, std::clamp(y + 10, 640.0f, 698.0f), 90, 90});
    return L;
}

} // namespace

FaceLayout faceLayout(const rack::PluginInfo& info) {
    switch (info.face) {
    case rack::Face::EliteEq: return eliteEq(info);
    case rack::Face::BusGlue: return busGlue(info);
    case rack::Face::Tape: return tape(info);
    case rack::Face::TubeEq: return tubeEq(info);
    case rack::Face::Limiter: return limiterFace(info);
    case rack::Face::Width: return widthFace(info);
    case rack::Face::Exciter: return exciterFace(info);
    case rack::Face::Meter: return meterFace(info);
    case rack::Face::VcaComp: return vcaFace(info);
    case rack::Face::TubeSat: return tubeSatFace(info);
    case rack::Face::Hall:
    case rack::Face::Plate: return reverbFace(info);
    case rack::Face::DigitalDelay: return digitalDelayFace(info);
    case rack::Face::Chorus:
    case rack::Face::Flanger:
    case rack::Face::Phaser:
    case rack::Face::Tremolo:
    case rack::Face::Rotary:
    case rack::Face::AutoFilter: return movementFace(info);
    case rack::Face::Sub: return subFace(info);
    case rack::Face::VocalStrip: return vocalFace(info);
    case rack::Face::Hitman: return hitmanFace(info);
    case rack::Face::Compander: return companderFace(info);
    case rack::Face::Channel: return channelFace(info);
    case rack::Face::Movers: return moversFace(info);
    case rack::Face::Taps: return tapsFace(info);
    case rack::Face::Voices: return voicesFace(info);
    case rack::Face::Keys: return keysFace(info);
    case rack::Face::Notches: return notchesFace(info);
    case rack::Face::Pawn: return pawnFace(info);
    case rack::Face::Audit: return auditFace(info);
    case rack::Face::Stakeout: return stakeoutFace(info);
    case rack::Face::Stomp: return stompFace(info);
    case rack::Face::Piano: return pianoFace(info);
    case rack::Face::Spec: if (info.plate) return specFace(info); break;
    case rack::Face::Restorer: break;
    }
    return restorer(info);
}

int widgetParam(const Widget& w, const FaceState& st) noexcept {
    if (w.control >= 0) return rack::eq::bandParam(std::clamp(st.selectedBand, 0, rack::eq::kBands - 1), w.control);
    if (w.pageStride > 0 && w.param >= 0) return w.param + std::max(st.page, 0) * w.pageStride;
    return w.param;
}

bool widgetShown(const Widget& w, std::span<const hc::fx::ParamInfo> params, const PanelHost& host) noexcept {
    if (w.showParam < 0 || w.showParam >= static_cast<int>(params.size())) return true;
    const float v = hc::fx::toPlain(params[static_cast<std::size_t>(w.showParam)], host.normalized(w.showParam));
    return static_cast<int>(std::lround(v)) == w.showValue;
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
