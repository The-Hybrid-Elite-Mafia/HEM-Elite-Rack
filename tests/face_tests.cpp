// The faceplates without a GPU: every control has a place, nothing overlaps, every knob / button /
// graph gesture changes the parameter it should, and the metal plate renders the same every time.
#include "Harness.hpp"
#include "TextRoom.hpp"

#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Hideout.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/ui/Face.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace hem::test {
namespace {

using namespace hem::ui;

// PanelHost straight onto an effect, counting begin / end edits (hosts need them paired).
class TestHost final : public PanelHost {
public:
    explicit TestHost(hc::fx::Effect& e) : e_(e) {}
    float normalized(int i) const override { return hc::fx::toNormalized(e_.params()[static_cast<std::size_t>(i)], e_.get(i)); }
    void beginEdit(int) override { ++open_; ++begins; }
    void setNormalized(int i, float v) override { e_.set(i, hc::fx::toPlain(e_.params()[static_cast<std::size_t>(i)], v)); ++sets; }
    void endEdit(int) override { --open_; }
    std::string text(int i) const override { return hc::fx::formatValue(e_.params()[static_cast<std::size_t>(i)], e_.get(i)); }
    bool active(int i) const override { return e_.paramActive(i); }
    bool bypassed() const override { return bypass; }
    void setBypassed(bool b) override { bypass = b; }
    int open() const { return open_; }
    int begins = 0, sets = 0;
    bool bypass = false;

private:
    hc::fx::Effect& e_;
    int open_ = 0;
};

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}

int find(const FaceLayout& L, WKind k, int param = -1, int control = -1, int choice = -1) {
    for (std::size_t i = 0; i < L.widgets.size(); ++i) {
        const Widget& w = L.widgets[i];
        if (w.kind == k && (param < 0 || w.param == param) && (control < 0 || w.control == control) && (choice < 0 || w.choice == choice))
            return static_cast<int>(i);
    }
    return -1;
}

void layoutRules(const rack::PluginInfo& info) {
    const FaceLayout L = faceLayout(info);
    auto fx = info.create();
    const int count = static_cast<int>(fx->params().size());
    bool inside = true, paramsOk = true, noOverlap = true;
    for (std::size_t i = 0; i < L.widgets.size(); ++i) {
        const Widget& w = L.widgets[i];
        if (w.r.x < 0 || w.r.y < 0 || w.r.right() > kPlateW || w.r.bottom() > L.footer.y) inside = false;
        if (w.param >= count || w.param2 >= count) paramsOk = false;
        if (w.kind == WKind::Keyboard && w.param + w.choice > count) paramsOk = false;
        if (w.pageStride > 0) {   // every page's control exists
            int pg = 0;
            for (const Widget& o : L.widgets) pg += o.kind == WKind::PageTab ? 1 : 0;
            if (w.param + (std::max(pg, 1) - 1) * w.pageStride >= count) paramsOk = false;
        }
        if (w.kind == WKind::Spectrum || w.kind == WKind::EqGraph || w.kind == WKind::GraphRange) continue;
        for (std::size_t j = i + 1; j < L.widgets.size(); ++j) {
            const Widget& o = L.widgets[j];
            if (o.kind == WKind::Spectrum || o.kind == WKind::EqGraph || o.kind == WKind::GraphRange) continue;
            if (exclusive(w, o)) continue;   // never on the plate together
            if (overlaps(w.r, o.r)) { noOverlap = false; std::printf("  overlap: %s / %s\n", w.caption.c_str(), o.caption.c_str()); }
        }
    }
    CHECK(inside);
    CHECK(paramsOk);
    CHECK(noOverlap);
    // ...and no caption, value, note or section name runs into a control or into other text.
    CHECK(textroom::clashes(L, info.name) == 0);
    CHECK(textroom::crampedButtons(L, fx->params(), info.name) == 0);
    // Every parameter can be reached from the faceplate.
    std::set<int> reach;
    int pages = 0;
    for (const Widget& w : L.widgets) pages += w.kind == WKind::PageTab ? 1 : 0;
    for (const Widget& w : L.widgets) {
        if (w.param >= 0 && w.pageStride > 0)
            for (int pg = 0; pg < std::max(pages, 1); ++pg) reach.insert(w.param + pg * w.pageStride);
        if (w.param >= 0) reach.insert(w.param);
        if (w.param2 >= 0) reach.insert(w.param2);
        if (w.kind == WKind::Keyboard) for (int k = 0; k < w.choice; ++k) reach.insert(w.param + k);
        if (w.control >= 0) for (int b = 0; b < rack::eq::kBands; ++b) reach.insert(rack::eq::bandParam(b, w.control));
    }
    int missing = 0;
    for (int p = 0; p < count; ++p)
        if (!reach.count(p) && (info.firstStored < 0 || p < info.firstStored)) { ++missing; std::printf("  no control for %s\n", fx->params()[static_cast<std::size_t>(p)].id); }
    CHECK(missing == 0);
    CHECK(find(L, WKind::Bypass) >= 0);
    std::printf("  %s: %zu controls, every parameter reachable, no overlaps, text has room\n", info.name, L.widgets.size());
}

float plain(const hc::fx::Effect& e, int p) { return e.get(p); }

void restorerGestures() {
    const rack::PluginInfo* info = rack::findPlugin("THE CLEANER");
    auto fx = info->create();
    const FaceLayout L = faceLayout(*info);
    TestHost host(*fx);
    FaceFeed feed;
    FaceState st;
    FaceController c(L, fx->params(), host, feed, st);
    using namespace rack::restorer;

    // NOISE REDUCTION: drag up the full-range distance = 0 → 10, double-click = default.
    const Widget& red = L.widgets[static_cast<std::size_t>(find(L, WKind::Knob, Reduction))];
    fx->set(Reduction, 0.0f);
    c.mouseDown(red.r.cx(), red.r.cy(), false, false);
    c.mouseMove(red.r.cx(), red.r.cy() - FaceController::kDragForFullRange, false);
    c.mouseUp();
    CHECK(std::fabs(plain(*fx, Reduction) - 10.0f) < 1e-3f);
    // fine drag (Shift): the same distance moves 15 %
    fx->set(Reduction, 0.0f);
    c.mouseDown(red.r.cx(), red.r.cy(), false, true);
    c.mouseMove(red.r.cx(), red.r.cy() - FaceController::kDragForFullRange, true);
    c.mouseUp();
    CHECK(std::fabs(plain(*fx, Reduction) - 1.5f) < 1e-3f);
    c.mouseDown(red.r.cx(), red.r.cy(), true, false);
    CHECK(std::fabs(plain(*fx, Reduction) - fx->params()[Reduction].def) < 1e-4f);
    c.mouseUp();
    // wheel
    const float before = plain(*fx, Reduction);
    c.wheel(red.r.cx(), red.r.cy(), 1.0f, false);
    CHECK(plain(*fx, Reduction) > before);
    // ADAPT / LISTEN toggles flip.
    for (int p : {Adapt, Listen}) {
        const Widget& t = L.widgets[static_cast<std::size_t>(find(L, WKind::Toggle, p))];
        const float v0 = plain(*fx, p);
        c.mouseDown(t.r.cx(), t.r.cy(), false, false);
        c.mouseUp();
        CHECK(plain(*fx, p) != v0);
    }
    // FREQUENCY RANGE: drag the low handle to ~200 Hz; the high one to ~5 kHz; never crossing.
    const Widget& rs = L.widgets[static_cast<std::size_t>(find(L, WKind::RangeSlider))];
    const Rect plot = plotArea(L.display);
    c.mouseDown(freqToX(plot, plain(*fx, LowHz)), rs.r.cy(), false, false);
    c.mouseMove(freqToX(plot, 200.0f), rs.r.cy(), false);
    c.mouseUp();
    CHECK(std::fabs(plain(*fx, LowHz) - 200.0f) < 4.0f);
    c.mouseDown(freqToX(plot, plain(*fx, HighHz)), rs.r.cy(), false, false);
    c.mouseMove(freqToX(plot, 5000.0f), rs.r.cy(), false);
    c.mouseUp();
    CHECK(std::fabs(plain(*fx, HighHz) - 5000.0f) < 80.0f);
    c.mouseDown(freqToX(plot, plain(*fx, LowHz)), rs.r.cy(), false, false);
    c.mouseMove(freqToX(plot, 15000.0f), rs.r.cy(), false);
    c.mouseUp();
    CHECK(plain(*fx, LowHz) < plain(*fx, HighHz));
    // ON / BYPASS
    const Widget& by = L.widgets[static_cast<std::size_t>(find(L, WKind::Bypass))];
    c.mouseDown(by.r.cx(), by.r.cy(), false, false);
    c.mouseUp();
    CHECK(host.bypass);
    CHECK(host.open() == 0);   // every beginEdit had its endEdit
}

// THE INFORMANT's keyboard: NORMAL holds a key while the mouse is down and glides; SUSTAIN latches; SUSTAIN off lets go.
void keyboardGestures() {
    using namespace rack::informant;
    const rack::PluginInfo* info = rack::findPlugin("THE INFORMANT");
    auto fx = info->create();
    const FaceLayout L = faceLayout(*info);
    TestHost host(*fx);
    FaceFeed feed;
    FaceState st;
    FaceController c(L, fx->params(), host, feed, st);
    const int kb = find(L, WKind::Keyboard);
    CHECK(kb >= 0);
    if (kb < 0) return;
    const Widget& w = L.widgets[static_cast<std::size_t>(kb)];
    auto at = [&](int k) { const Rect r = keyRect(w.r, w.choice, k); return std::pair<float, float>{r.cx(), r.y + r.h * 0.8f}; };
    CHECK(keyAt(w.r, w.choice, at(1).first, at(1).second) == 1);    // C#2 (black)
    CHECK(keyAt(w.r, w.choice, at(0).first, at(0).second) == 0);    // C2
    CHECK(keyAt(w.r, w.choice, at(47).first, at(47).second) == 47); // B5
    // NORMAL: E2 sounds while held, the glide to G2 lets E2 go, letting go ends it
    c.mouseDown(at(4).first, at(4).second, false, false);
    CHECK(plain(*fx, Key0 + 4) >= 0.5f);
    c.mouseMove(at(7).first, at(7).second, false);
    CHECK(plain(*fx, Key0 + 4) < 0.5f && plain(*fx, Key0 + 7) >= 0.5f);
    c.mouseUp();
    CHECK(plain(*fx, Key0 + 7) < 0.5f);
    // SUSTAIN: clicks latch two keys, a second click lets one go
    const Widget& sus = L.widgets[static_cast<std::size_t>(find(L, WKind::Toggle, Sustain))];
    c.mouseDown(sus.r.cx(), sus.r.cy(), false, false);
    c.mouseUp();
    CHECK(plain(*fx, Sustain) >= 0.5f);
    for (int k : {12, 16, 19}) { c.mouseDown(at(k).first, at(k).second, false, false); c.mouseUp(); }
    CHECK(plain(*fx, Key0 + 12) >= 0.5f && plain(*fx, Key0 + 16) >= 0.5f && plain(*fx, Key0 + 19) >= 0.5f);
    c.mouseDown(at(16).first, at(16).second, false, false);
    c.mouseUp();
    CHECK(plain(*fx, Key0 + 16) < 0.5f);
    // SUSTAIN off: every latched key goes
    c.mouseDown(sus.r.cx(), sus.r.cy(), false, false);
    c.mouseUp();
    int held = 0;
    for (int k = 0; k < kKeys; ++k) held += plain(*fx, Key0 + k) >= 0.5f ? 1 : 0;
    CHECK(held == 0);
    CHECK(host.open() == 0);
    std::printf("  THE INFORMANT keyboard: hold, glide, latch and let go all reach the key switches\n");
}

void eqGestures() {
    using namespace rack::eq;
    const rack::PluginInfo* info = rack::findPlugin("SURGEON");
    auto fx = info->create();
    const FaceLayout L = faceLayout(*info);
    TestHost host(*fx);
    FaceFeed feed;
    FaceState st;
    FaceController c(L, fx->params(), host, feed, st);
    const Rect plot = plotArea(L.display);
    const float range = kGraphRanges[static_cast<std::size_t>(st.graphRange)];
    auto yOf = [&](float db) { return plot.cy() - db / range * plot.h * 0.5f; };

    // Nothing on: inactive band controls, and the band knobs follow the selection.
    CHECK(!fx->paramActive(bandParam(0, Gain)));
    // Double-click the graph at 1 kHz, +6 dB: band 1 turns on as a bell there.
    c.mouseDown(freqToX(plot, 1000.0f), yOf(6.0f), true, false);
    c.mouseUp();
    CHECK(plain(*fx, bandParam(0, On)) >= 0.5f);
    CHECK(static_cast<int>(plain(*fx, bandParam(0, TypeC))) == static_cast<int>(Type::Bell));
    CHECK(std::fabs(plain(*fx, bandParam(0, Freq)) - 1000.0f) < 15.0f);
    CHECK(std::fabs(plain(*fx, bandParam(0, Gain)) - 6.0f) < 0.15f);
    CHECK(st.selectedBand == 0);
    // A second double-click elsewhere makes band 2; at the far left it is a low cut.
    c.mouseDown(freqToX(plot, 22.0f), yOf(0.0f), true, false);
    c.mouseUp();
    CHECK(plain(*fx, bandParam(1, On)) >= 0.5f);
    CHECK(static_cast<int>(plain(*fx, bandParam(1, TypeC))) == static_cast<int>(Type::LowCut));
    CHECK(st.selectedBand == 1);
    // Drag band 1's node one octave up and 3 dB down.
    {
        const float x0 = freqToX(plot, plain(*fx, bandParam(0, Freq))), y0 = yOf(plain(*fx, bandParam(0, Gain)));
        c.mouseDown(x0, y0, false, false);
        CHECK(st.selectedBand == 0);
        c.mouseMove(freqToX(plot, 2000.0f), yOf(3.0f), false);
        c.mouseUp();
        CHECK(std::fabs(plain(*fx, bandParam(0, Freq)) - 2000.0f) < 30.0f);
        CHECK(std::fabs(plain(*fx, bandParam(0, Gain)) - 3.0f) < 0.15f);
    }
    // Wheel on the node: Q up.
    {
        const float q0 = plain(*fx, bandParam(0, Q));
        c.wheel(freqToX(plot, plain(*fx, bandParam(0, Freq))), yOf(plain(*fx, bandParam(0, Gain))), 2.0f, false);
        CHECK(plain(*fx, bandParam(0, Q)) > q0);
    }
    // The band knobs edit the selected band.
    {
        const Widget& g = L.widgets[static_cast<std::size_t>(find(L, WKind::Knob, -1, Gain))];
        c.mouseDown(g.r.cx(), g.r.cy(), true, false);   // double-click: default (0 dB)
        c.mouseUp();
        CHECK(std::fabs(plain(*fx, bandParam(0, Gain))) < 1e-4f);
    }
    // TYPE stepper: right half steps forward (bell -> low shelf), left half back.
    {
        const Widget& t = L.widgets[static_cast<std::size_t>(find(L, WKind::Stepper, -1, TypeC))];
        c.mouseDown(t.r.right() - 5, t.r.cy(), false, false);
        c.mouseUp();
        CHECK(static_cast<int>(plain(*fx, bandParam(0, TypeC))) == static_cast<int>(Type::LowShelf));
        c.mouseDown(t.r.x + 5, t.r.cy(), false, false);
        c.mouseUp();
        CHECK(static_cast<int>(plain(*fx, bandParam(0, TypeC))) == static_cast<int>(Type::Bell));
        c.mouseDown(t.r.x + 5, t.r.cy(), false, false);   // wraps to the last type
        c.mouseUp();
        CHECK(static_cast<int>(plain(*fx, bandParam(0, TypeC))) == static_cast<int>(Type::Tilt));
    }
    // DYNAMIC on enables THRESHOLD etc. (tilt can be dynamic).
    {
        CHECK(!fx->paramActive(bandParam(0, Threshold)));
        const Widget& d = L.widgets[static_cast<std::size_t>(find(L, WKind::Toggle, -1, Dyn))];
        c.mouseDown(d.r.cx(), d.r.cy(), false, false);
        c.mouseUp();
        CHECK(plain(*fx, bandParam(0, Dyn)) >= 0.5f);
        CHECK(fx->paramActive(bandParam(0, Threshold)));
        CHECK(!fx->paramActive(bandParam(0, Q)));   // tilt has no Q
    }
    // Band buttons select; double-click turns a band on / off.
    {
        const Widget& b5 = L.widgets[static_cast<std::size_t>(find(L, WKind::BandButton, -1, -1, 4))];
        c.mouseDown(b5.r.cx(), b5.r.cy(), false, false);
        c.mouseUp();
        CHECK(st.selectedBand == 4);
        c.mouseDown(b5.r.cx(), b5.r.cy(), true, false);
        c.mouseUp();
        CHECK(plain(*fx, bandParam(4, On)) >= 0.5f);
    }
    // Graph range buttons.
    {
        const Widget& r30 = L.widgets[static_cast<std::size_t>(find(L, WKind::GraphRange, -1, -1, 3))];
        c.mouseDown(r30.r.cx(), r30.r.cy(), false, false);
        c.mouseUp();
        CHECK(st.graphRange == 3);
    }
    // CHARACTER buttons; DRIVE only does something away from CLEAN.
    {
        CHECK(!fx->paramActive(Drive));
        const Widget& tube = L.widgets[static_cast<std::size_t>(find(L, WKind::ChoiceButton, Character, -1, 2))];
        c.mouseDown(tube.r.cx(), tube.r.cy(), false, false);
        c.mouseUp();
        CHECK(static_cast<int>(plain(*fx, Character)) == 2);
        CHECK(fx->paramActive(Drive));
    }
    // Double-click a node: that band goes off.
    {
        st.graphRange = 2;
        const float x0 = freqToX(plot, plain(*fx, bandParam(1, Freq)));
        c.mouseDown(x0, plot.cy(), true, false);
        c.mouseUp();
        CHECK(plain(*fx, bandParam(1, On)) < 0.5f);
    }
    // 16 bands fill up; a 17th double-click does nothing.
    for (int i = 0; i < 20; ++i) {
        c.mouseDown(plot.x + 10 + static_cast<float>(i) * 40.0f, plot.y + 30, true, false);
        c.mouseUp();
    }
    int on = 0;
    for (int b = 0; b < kBands; ++b) on += plain(*fx, bandParam(b, On)) >= 0.5f ? 1 : 0;
    CHECK(on == kBands);
    CHECK(host.open() == 0);
}

// Every knob, button and switch on every faceplate does what it should to its own parameter.
void everyControl(const rack::PluginInfo& info) {
    auto fx = info.create();
    const FaceLayout L = faceLayout(info);
    TestHost host(*fx);
    FaceFeed feed;
    FaceState st;
    FaceController c(L, fx->params(), host, feed, st);
    int knobs = 0, buttons = 0, toggles = 0, bad = 0;
    for (const Widget& w : L.widgets) {
        const int p = w.param;
        if (p < 0 || w.control >= 0) continue;   // ELITE EQ's band controls are covered above
        const auto& pi = fx->params()[static_cast<std::size_t>(p)];
        if (w.showParam >= 0) fx->set(w.showParam, static_cast<float>(w.showValue));   // bring its page up first
        if (w.kind == WKind::Knob) {
            ++knobs;
            fx->set(p, pi.min);
            c.mouseDown(w.r.cx(), w.r.cy(), false, false);
            c.mouseMove(w.r.cx(), w.r.cy() - FaceController::kDragForFullRange, false);
            c.mouseUp();
            const bool up = std::fabs(fx->get(p) - pi.max) <= (pi.max - pi.min) * 1e-4f;
            c.mouseDown(w.r.cx(), w.r.cy(), true, false);   // double-click: default
            c.mouseUp();
            const bool def = std::fabs(fx->get(p) - pi.def) <= (pi.max - pi.min) * 1e-4f;
            if (!up || !def) { ++bad; std::printf("  %s: knob %s did not reach max / default\n", info.name, w.caption.c_str()); }
        } else if (w.kind == WKind::ChoiceButton) {
            ++buttons;
            c.mouseDown(w.r.cx(), w.r.cy(), false, false);
            c.mouseUp();
            if (static_cast<int>(std::lround(fx->get(p))) != w.choice) { ++bad; std::printf("  %s: button %d of %s not selected\n", info.name, w.choice, pi.id); }
        } else if (w.kind == WKind::Toggle) {
            ++toggles;
            const float v0 = fx->get(p);
            c.mouseDown(w.r.cx(), w.r.cy(), false, false);
            c.mouseUp();
            if (fx->get(p) == v0) { ++bad; std::printf("  %s: switch %s did not flip\n", info.name, w.caption.c_str()); }
        }
    }
    std::printf("  %s: %d knobs, %d buttons, %d switches - all respond\n", info.name, knobs, buttons, toggles);
    CHECK(bad == 0);
    CHECK(host.open() == 0);
}

void plateRules() {
    std::string err;
    auto art = BrandArt::load(exeDir() / "assets", &err);
    CHECK(art.has_value());
    if (!art) { std::printf("  brand art: %s\n", err.c_str()); return; }
    for (const auto& info : rack::plugins()) {
        const FaceLayout L = faceLayout(info);
        const hc::ui::Image a = renderRackPlate(L, *art, 0.25f), b = renderRackPlate(L, *art, 0.25f);
        CHECK(a.width == static_cast<std::uint32_t>(std::lround(kPlateW * 0.25f)));
        CHECK(a.rgba == b.rgba);
        // The crown badge is gold: its area must hold warm, saturated pixels; the metal around is grey.
        int warm = 0;
        for (std::uint32_t y = static_cast<std::uint32_t>(L.crown.y * 0.25f); y < static_cast<std::uint32_t>(L.crown.bottom() * 0.25f); ++y)
            for (std::uint32_t x = static_cast<std::uint32_t>(L.crown.x * 0.25f); x < static_cast<std::uint32_t>(L.crown.right() * 0.25f); ++x) {
                const std::uint8_t* p = a.at(x, y);
                if (p[0] > p[2] + 40 && p[0] > 90) ++warm;
            }
        CHECK(warm > 50);
    }
}

} // namespace

void testFaces() {
    std::printf("[faceplates]\n");
    for (const auto& info : rack::plugins()) layoutRules(info);
    restorerGestures();
    keyboardGestures();
    eqGestures();
    for (const auto& info : rack::plugins()) everyControl(info);
    plateRules();
}

} // namespace hem::test
