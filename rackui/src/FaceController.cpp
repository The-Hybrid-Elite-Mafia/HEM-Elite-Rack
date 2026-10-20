// FaceController: mouse and wheel on a HEM-Elite-Rack faceplate → parameter edits.
//   Knobs: drag up / down (Shift or Ctrl = fine), wheel, double-click = default value.
//   Toggles / choice buttons: click.  Steppers: click the left half = back, right half = forward; wheel.
//   FREQUENCY RANGE: drag either handle (the nearer one is picked).
//   ELITE EQ graph: double-click empty space = new band there; drag a node = frequency and gain;
//   wheel on a node = Q (width); double-click a node = band off. Band buttons select (double-click = on / off).
#include "hem/ui/Face.hpp"

#include <algorithm>
#include <cmath>

namespace hem::ui {

namespace {
constexpr bool kBlack[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
constexpr int kWhiteIndex[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};   // a black key: the white key on its left
int whiteKeys(int keys) noexcept {
    int n = 0;
    for (int k = 0; k < keys; ++k) n += kBlack[k % 12] ? 0 : 1;
    return n;
}
float dist2(float ax, float ay, float bx, float by) { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); }
}

bool keyIsBlack(int k) noexcept { return kBlack[((k % 12) + 12) % 12]; }

Rect keyRect(const Rect& kb, int keys, int k) noexcept {
    const float ww = kb.w / static_cast<float>(std::max(1, whiteKeys(keys)));
    const int white = (k / 12) * 7 + kWhiteIndex[k % 12];
    if (!kBlack[k % 12]) return {kb.x + static_cast<float>(white) * ww, kb.y, ww, kb.h};
    const float bw = ww * 0.6f;
    return {kb.x + static_cast<float>(white + 1) * ww - bw * 0.5f, kb.y, bw, kb.h * 0.62f};
}

int keyAt(const Rect& kb, int keys, float x, float y) noexcept {
    if (!kb.contains(x, y)) return -1;
    for (int k = 0; k < keys; ++k)   // the black keys sit on top
        if (kBlack[k % 12] && keyRect(kb, keys, k).contains(x, y)) return k;
    for (int k = 0; k < keys; ++k)
        if (!kBlack[k % 12] && keyRect(kb, keys, k).contains(x, y)) return k;
    return -1;
}

float FaceController::plain(int param) const {
    if (param < 0 || param >= static_cast<int>(params_.size())) return 0.0f;
    return hc::fx::toPlain(params_[static_cast<std::size_t>(param)], host_.normalized(param));
}

void FaceController::setPlain(int param, float v) {
    if (param < 0 || param >= static_cast<int>(params_.size())) return;
    const auto& pi = params_[static_cast<std::size_t>(param)];
    host_.setNormalized(param, hc::fx::toNormalized(pi, hc::fx::clampPlain(pi, v)));
}

void FaceController::setNorm(int param, float v) { host_.setNormalized(param, std::clamp(v, 0.0f, 1.0f)); }

void FaceController::edit(int param, float normalized) {
    if (param < 0) return;
    host_.beginEdit(param);
    setNorm(param, normalized);
    host_.endEdit(param);
}

void FaceController::step(int param, int delta) {
    if (param < 0 || param >= static_cast<int>(params_.size())) return;
    const auto& pi = params_[static_cast<std::size_t>(param)];
    const int count = static_cast<int>(pi.max) + 1;
    const int cur = static_cast<int>(std::lround(plain(param)));
    const int next = ((cur + delta) % count + count) % count;
    host_.beginEdit(param);
    setPlain(param, static_cast<float>(next));
    host_.endEdit(param);
}

int FaceController::hit(float x, float y) const noexcept {
    for (int i = static_cast<int>(L_.widgets.size()) - 1; i >= 0; --i) {
        const Widget& w = L_.widgets[static_cast<std::size_t>(i)];
        if (w.kind == WKind::Knob || w.kind == WKind::Bypass) {
            const float rr = w.r.w * 0.5f + (w.kind == WKind::Knob ? 10.0f : 0.0f);
            if (dist2(x, y, w.r.cx(), w.r.cy()) <= rr * rr) return i;
        } else if (w.kind == WKind::RangeSlider) {
            if (w.r.inset(-14, -6).contains(x, y)) return i;
        } else if (w.r.contains(x, y)) {
            return i;
        }
    }
    return -1;
}

Rect FaceController::plot() const noexcept { return plotArea(L_.display); }

int FaceController::nodeAt(float x, float y) const noexcept {
    using namespace rack::eq;
    if (L_.face != rack::Face::EliteEq) return -1;
    const Rect p = plot();
    const float range = kGraphRanges[static_cast<std::size_t>(std::clamp(st_.graphRange, 0, 3))];
    int best = -1;
    float bestD = 22.0f * 22.0f;
    for (int b = 0; b < kBands; ++b) {
        if (plain(bandParam(b, On)) < 0.5f) continue;
        const auto type = static_cast<Type>(std::clamp(static_cast<int>(std::lround(plain(bandParam(b, TypeC)))), 0, static_cast<int>(Type::kCount) - 1));
        const float nx = freqToX(p, plain(bandParam(b, Freq)));
        const float g = typeHasGain(type) ? std::clamp(plain(bandParam(b, Gain)) / range, -1.04f, 1.04f) : 0.0f;
        const float ny = p.cy() - g * p.h * 0.5f;
        const float d = dist2(x, y, nx, ny);
        if (d < bestD || (d == bestD && b == st_.selectedBand)) { bestD = d; best = b; }
    }
    return best;
}

int FaceController::firstFreeBand() const {
    for (int b = 0; b < rack::eq::kBands; ++b)
        if (plain(rack::eq::bandParam(b, rack::eq::On)) < 0.5f) return b;
    return -1;
}

void FaceController::graphDown(float x, float y, bool doubleClick) {
    using namespace rack::eq;
    const Rect p = plot();
    const int node = nodeAt(x, y);
    if (node >= 0) {
        st_.selectedBand = node;
        if (doubleClick) { edit(bandParam(node, On), 0.0f); return; }
        st_.dragNode = node;
        dragValue_ = host_.normalized(bandParam(node, Freq));
        dragValue2_ = host_.normalized(bandParam(node, Gain));
        host_.beginEdit(bandParam(node, Freq));
        host_.beginEdit(bandParam(node, Gain));
        return;
    }
    if (!doubleClick || !p.contains(x, y)) return;
    const int b = firstFreeBand();
    if (b < 0) return;
    const float range = kGraphRanges[static_cast<std::size_t>(std::clamp(st_.graphRange, 0, 3))];
    const float gain = std::clamp((p.cy() - y) / (p.h * 0.5f) * range, -30.0f, 30.0f);
    const float hz = xToFreq(p, x);
    // A new band: bell where you clicked, cuts at the ends of the graph.
    const Type type = hz < 30.0f ? Type::LowCut : hz > 16000.0f ? Type::HighCut : Type::Bell;
    for (int ctl : {On, TypeC, Freq, Gain, Q}) host_.beginEdit(bandParam(b, ctl));
    setPlain(bandParam(b, TypeC), static_cast<float>(static_cast<int>(type)));
    setPlain(bandParam(b, Freq), hz);
    setPlain(bandParam(b, Gain), type == Type::Bell ? gain : 0.0f);
    setPlain(bandParam(b, Q), type == Type::Bell ? 1.0f : 0.71f);
    setPlain(bandParam(b, On), 1.0f);
    for (int ctl : {On, TypeC, Freq, Gain, Q}) host_.endEdit(bandParam(b, ctl));
    st_.selectedBand = b;
}

void FaceController::graphDrag(float x, float y, bool fine) {
    using namespace rack::eq;
    const int b = st_.dragNode;
    if (b < 0) return;
    const Rect p = plot();
    const float k = fine ? 0.15f : 1.0f;
    const float range = kGraphRanges[static_cast<std::size_t>(std::clamp(st_.graphRange, 0, 3))];
    dragValue_ = std::clamp(dragValue_ + (x - lastX_) / p.w * k, 0.0f, 1.0f);
    setNorm(bandParam(b, Freq), dragValue_);
    const auto type = static_cast<Type>(std::clamp(static_cast<int>(std::lround(plain(bandParam(b, TypeC)))), 0, static_cast<int>(Type::kCount) - 1));
    if (typeHasGain(type)) {
        // Gain range of the parameter is ±30 dB; on screen p.h covers 2 × range.
        const float dGainDb = -(y - lastY_) / p.h * 2.0f * range * k;
        dragValue2_ = std::clamp(dragValue2_ + dGainDb / 60.0f, 0.0f, 1.0f);
        setNorm(bandParam(b, Gain), dragValue2_);
    }
}

void FaceController::mouseDown(float x, float y, bool doubleClick, bool fine) {
    (void)fine;
    lastX_ = x;
    lastY_ = y;
    const int i = hit(x, y);
    st_.active = -1;
    if (i < 0) return;
    const Widget& w = L_.widgets[static_cast<std::size_t>(i)];
    const int param = widgetParam(w, st_);
    switch (w.kind) {
    case WKind::Knob:
        if (param < 0) return;
        if (doubleClick) { edit(param, hc::fx::toNormalized(params_[static_cast<std::size_t>(param)], params_[static_cast<std::size_t>(param)].def)); return; }
        st_.active = i;
        dragValue_ = host_.normalized(param);
        host_.beginEdit(param);
        return;
    case WKind::Toggle: {
        const bool turnOff = plain(param) >= 0.5f;
        edit(param, turnOff ? 0.0f : 1.0f);
        // a keyboard's SUSTAIN switched off lets go of every latched key
        if (turnOff)
            for (const Widget& kb : L_.widgets)
                if (kb.kind == WKind::Keyboard && kb.param2 == param)
                    for (int k = 0; k < kb.choice; ++k)
                        if (plain(kb.param + k) >= 0.5f) edit(kb.param + k, 0.0f);
        return;
    }
    case WKind::Keyboard: {
        const int k = keyAt(w.r, w.choice, x, y);
        if (k < 0) return;
        st_.active = i;
        if (w.param2 >= 0 && plain(w.param2) >= 0.5f) {   // SUSTAIN: a click latches or lets go; a drag does the same to every key it crosses
            latchOn_ = plain(w.param + k) < 0.5f;
            edit(w.param + k, latchOn_ ? 1.0f : 0.0f);
            heldKey_ = k;
        } else {   // NORMAL: the key sounds while the mouse holds it
            heldKey_ = k;
            edit(w.param + k, 1.0f);
        }
        return;
    }
    case WKind::ChoiceButton: {
        const auto& pi = params_[static_cast<std::size_t>(param)];
        host_.beginEdit(param);
        setPlain(param, static_cast<float>(w.choice));
        host_.endEdit(param);
        (void)pi;
        return;
    }
    case WKind::Stepper: step(param, x < w.r.cx() ? -1 : 1); return;
    case WKind::RangeSlider: {
        const Rect p = plot();
        const float xl = freqToX(p, plain(w.param)), xh = freqToX(p, plain(w.param2));
        dragHandle_ = std::fabs(x - xl) <= std::fabs(x - xh) ? 1 : 2;
        if (std::fabs(x - xl) == std::fabs(x - xh)) dragHandle_ = x < xl ? 1 : 2;
        const int pr = dragHandle_ == 1 ? w.param : w.param2;
        host_.beginEdit(pr);
        if (doubleClick) {
            setNorm(pr, hc::fx::toNormalized(params_[static_cast<std::size_t>(pr)], params_[static_cast<std::size_t>(pr)].def));
            host_.endEdit(pr);
            return;
        }
        st_.active = i;
        mouseMove(x, y, false);
        return;
    }
    case WKind::Bypass: host_.setBypassed(!host_.bypassed()); return;
    case WKind::GraphRange: st_.graphRange = std::clamp(w.choice, 0, 3); return;
    case WKind::PageTab: st_.page = w.choice; return;
    case WKind::BandButton:
        st_.selectedBand = w.choice;
        if (doubleClick) {
            const int on = rack::eq::bandParam(w.choice, rack::eq::On);
            edit(on, plain(on) >= 0.5f ? 0.0f : 1.0f);
        }
        return;
    case WKind::EqGraph:
        graphDown(x, y, doubleClick);
        if (st_.dragNode >= 0) st_.active = i;
        return;
    case WKind::Spectrum:
    case WKind::CurveDisplay:
    case WKind::GrMeter:
    case WKind::VuMeter:
    case WKind::Readout:
    case WKind::History:
    case WKind::Rta:
    case WKind::Polar: return;
    }
}

void FaceController::mouseMove(float x, float y, bool fine) {
    if (st_.active >= 0) {
        const Widget& w = L_.widgets[static_cast<std::size_t>(st_.active)];
        const float k = fine ? 0.15f : 1.0f;
        if (w.kind == WKind::Knob) {
            const int param = widgetParam(w, st_);
            dragValue_ = std::clamp(dragValue_ + (lastY_ - y) / kDragForFullRange * k, 0.0f, 1.0f);
            setNorm(param, dragValue_);
        } else if (w.kind == WKind::RangeSlider) {
            const Rect p = plot();
            const float hz = xToFreq(p, x);
            // keep the handles at least a third of an octave apart
            if (dragHandle_ == 1) setPlain(w.param, std::min(hz, plain(w.param2) / 1.26f));
            else setPlain(w.param2, std::max(hz, plain(w.param) * 1.26f));
        } else if (w.kind == WKind::EqGraph) {
            graphDrag(x, y, fine);
        } else if (w.kind == WKind::Keyboard) {   // a glide across the keys
            const int k = keyAt(w.r, w.choice, x, y);
            if (k != heldKey_) {
                const bool sustain = w.param2 >= 0 && plain(w.param2) >= 0.5f;
                if (!sustain && heldKey_ >= 0) edit(w.param + heldKey_, 0.0f);
                if (k >= 0) edit(w.param + k, sustain ? (latchOn_ ? 1.0f : 0.0f) : 1.0f);
                heldKey_ = k;
            }
        }
        lastX_ = x;
        lastY_ = y;
        return;
    }
    lastX_ = x;
    lastY_ = y;
    st_.hover = hit(x, y);
    st_.hoverNode = st_.hover >= 0 && L_.widgets[static_cast<std::size_t>(st_.hover)].kind == WKind::EqGraph ? nodeAt(x, y) : -1;
}

void FaceController::mouseUp() {
    if (st_.active >= 0) {
        const Widget& w = L_.widgets[static_cast<std::size_t>(st_.active)];
        if (w.kind == WKind::Knob) host_.endEdit(widgetParam(w, st_));
        else if (w.kind == WKind::RangeSlider) host_.endEdit(dragHandle_ == 1 ? w.param : w.param2);
        else if (w.kind == WKind::Keyboard) {
            const bool sustain = w.param2 >= 0 && plain(w.param2) >= 0.5f;
            if (!sustain && heldKey_ >= 0) edit(w.param + heldKey_, 0.0f);
        } else if (w.kind == WKind::EqGraph && st_.dragNode >= 0) {
            host_.endEdit(rack::eq::bandParam(st_.dragNode, rack::eq::Freq));
            host_.endEdit(rack::eq::bandParam(st_.dragNode, rack::eq::Gain));
        }
    }
    st_.active = -1;
    st_.dragNode = -1;
    dragHandle_ = 0;
    heldKey_ = -1;
}

void FaceController::wheel(float x, float y, float notches, bool fine) {
    if (st_.active >= 0) return;
    const int i = hit(x, y);
    if (i < 0) return;
    const Widget& w = L_.widgets[static_cast<std::size_t>(i)];
    const float k = fine ? 0.2f : 1.0f;
    if (w.kind == WKind::Knob) {
        const int param = widgetParam(w, st_);
        if (param < 0) return;
        edit(param, host_.normalized(param) + notches * 0.02f * k);
    } else if (w.kind == WKind::Stepper) {
        step(widgetParam(w, st_), notches > 0 ? 1 : -1);
    } else if (w.kind == WKind::EqGraph) {
        const int node = nodeAt(x, y);
        if (node < 0) return;
        st_.selectedBand = node;
        const int q = rack::eq::bandParam(node, rack::eq::Q);
        edit(q, host_.normalized(q) + notches * 0.03f * k);
    }
}

} // namespace hem::ui
