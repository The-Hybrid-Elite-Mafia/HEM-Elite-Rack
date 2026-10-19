#include "hem/ui/Face.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Tools.hpp"
#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/ColorFx.hpp"
#include "hem/rack/Space.hpp"
#include "hem/rack/Movement.hpp"
#include "hem/rack/Specialty.hpp"

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
        cal.caption = "VU CAL";
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
    bypassAt(L, {160, 640, 90, 90});
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
    readoutAt(L, {410, 182, 600, 120}, 0, "CORRELATION", "PHASE", "+2");
    readoutAt(L, {1036, 182, 600, 120}, 1, "SIDE VS MID", "dB", "+");
    buttonRow(L, "MONO BASS", 392, MonoBass, 5);
    L.sections.push_back({{410, 330, 700, 30}, "CORRELATION: +1 MONO, 0 WIDE, BELOW 0 CANCELS"});
    L.sections.push_back({{410, 380, 700, 30}, "MONO BASS: THE LOW END STAYS IN THE CENTRE"});
    L.grooves.push_back({410, 524, 1636, 524});
    knobRow(L, {{Width, "WIDTH", 128, "MONO", "200%", false}, {Balance, "BALANCE", 100, "L", "R", false},
                {Output, "OUTPUT", 86, "", "", true}}, 568);
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
    L.sections.push_back({{1150, 420, 486, 30}, "AUTO GAIN GIVES BACK HALF"});
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
