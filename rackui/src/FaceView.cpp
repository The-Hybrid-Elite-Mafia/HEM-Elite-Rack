// FaceView: the live parts of a HEM-Elite-Rack faceplate, drawn over the static plate.
#include "hem/ui/Face.hpp"

#include "hybrid/ui/Parts.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace hem::ui {

using hc::ui::Align;
using hc::ui::Blend;
using hc::ui::Renderer;
using hc::ui::Sprite;
using hc::ui::TextFx;
using hc::ui::TextStyle;
using hc::ui::rgb8;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kKnobMin = -0.75f * kPi, kKnobMax = 0.75f * kPi;
const Rgba kGlass = rgb8(0x040607, 0.88f);
const Rgba kInk   = rgb8(0x061012);   // text on a lit teal button

// Text sizes the faceplate may use, smallest first.
constexpr TextStyle kLadder[] = {TextStyle::Caption, TextStyle::Value, TextStyle::Label, TextStyle::Title,
                                 TextStyle::Tab,     TextStyle::Panel, TextStyle::Heading, TextStyle::Display};
float em(TextStyle s) { return hc::ui::kTextStyles[static_cast<std::size_t>(s)].em; }

std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string hzText(float hz) { return hz >= 1000.0f ? fmt(hz >= 10000.0f ? "%.0fk" : "%.1fk", hz / 1000.0) : fmt("%.0f", hz); }

// SURGEON: every band its own colour, so each band's own line can be told apart on the graph (its dot,
// its line and its band button share it). Solid colours, no glow.
Rgba bandColour(int band) {
    static const Rgba k[16] = {rgb8(0x22D8DD), rgb8(0xE7B54A), rgb8(0xA87BFF), rgb8(0xFF5A5A), rgb8(0x5BE36B), rgb8(0x4C8DFF),
                               rgb8(0xFF6FC4), rgb8(0xF4E04D), rgb8(0x9FF0F2), rgb8(0xB6E35B), rgb8(0xE05BE6), rgb8(0x7FC4FF),
                               rgb8(0xFF9466), rgb8(0x6FE8B8), rgb8(0xC9B3FF), rgb8(0xFFB830)};
    return k[((band % 16) + 16) % 16];
}
// The band's stereo setting as a letter beside its dot (nothing for STEREO).
const char* stereoTag(rack::eq::Stereo s) {
    switch (s) {
    case rack::eq::Stereo::Mid:   return "M";
    case rack::eq::Stereo::Side:  return "S";
    case rack::eq::Stereo::Left:  return "L";
    case rack::eq::Stereo::Right: return "R";
    default: return "";
    }
}

} // namespace

struct FaceView::Ctx {
    Renderer&        r;
    const PanelHost& host;
    const FaceFeed&  feed;
    const FaceState& st;
    float ox, oy, z;
    [[nodiscard]] Rect  T(Rect a) const { return {ox + a.x * z, oy + a.y * z, a.w * z, a.h * z}; }
    [[nodiscard]] float X(float x) const { return ox + x * z; }
    [[nodiscard]] float Y(float y) const { return oy + y * z; }
    [[nodiscard]] TextStyle ts(TextStyle base) const {
        const float want = em(base) * z * 1.04f;
        TextStyle best = kLadder[0];
        for (TextStyle t : kLadder) if (em(t) <= want) best = t;
        return best;
    }
    // The base style at this zoom, stepped down until `text` fits `width` (design units).
    [[nodiscard]] TextStyle fit(TextStyle base, std::string_view text, float width) const {
        TextStyle t = ts(base);
        while (t != kLadder[0] && r.textWidth(t, text) > width)
            for (std::size_t i = 1; i < std::size(kLadder); ++i)
                if (kLadder[i] == t) { t = kLadder[i - 1]; break; }
        return t;
    }
    [[nodiscard]] float mid(Rect a, TextStyle s) const { return a.cy() + r.capHeight(s) * 0.5f; }
};

float FaceView::plain(const PanelHost& host, int param) const {
    if (param < 0 || param >= static_cast<int>(params_.size())) return 0.0f;
    return hc::fx::toPlain(params_[static_cast<std::size_t>(param)], host.normalized(param));
}

void FaceView::header(const Ctx& c) const {
    Renderer& r = c.r;
    const float z = c.z;
    // Lit channels in the metal: bright solid teal with a hot core, sharp edges, no glow around them.
    for (const auto& g : L_.grooves) {
        const float xs[2] = {c.X(g[0]), c.X(g[2])}, ys[2] = {c.Y(g[1]), c.Y(g[3])};
        r.polyline(xs, ys, 2, 2.4f * z, th_.teal, Blend::Over, false);
        r.polyline(xs, ys, 2, 1.0f * z, {1, 1, 1, 0.35f}, Blend::Add, false);
    }
    // Section names over their part of the plate ("LMF", "RATIO").
    for (const auto& [rect, text] : L_.sections) {
        const Rect sr = c.T(rect);
        const TextStyle st = c.fit(TextStyle::Label, text, sr.w - 10 * z);
        r.text(st, text, sr.cx(), c.mid(sr, st), th_.teal, Align::Center, TextFx::Shadowed);
    }
    // HYBRID ELITE MAFIA / suite between the crown and the ghost.
    const float cx = c.X(kPlateW * 0.5f);
    const TextStyle hs = c.fit(TextStyle::Heading, "HYBRID ELITE MAFIA", 760 * z);
    r.text(hs, "HYBRID ELITE MAFIA", cx, c.Y(84), th_.silver, Align::Center, TextFx::Shadowed);
    const TextStyle ss = c.fit(TextStyle::Label, L_.suite, 760 * z);
    r.text(ss, L_.suite, cx, c.Y(126), th_.teal, Align::Center, TextFx::Shadowed);
    // HEM AUDIO / EST. 2024 beside the ghost; HEM-ELITE-RACK beside the crown.
    const TextStyle ls = c.ts(TextStyle::Label), cs = c.ts(TextStyle::Caption);
    r.text(ls, "HEM AUDIO", c.X(L_.ghost.x - 26), c.Y(78), th_.silver, Align::Right, TextFx::Shadowed);
    r.text(cs, "EST. 2024", c.X(L_.ghost.x - 26), c.Y(112), th_.teal, Align::Right, TextFx::Shadowed);
    r.text(ls, "HEM-ELITE-RACK", c.X(L_.crown.right() + 26), c.Y(78), th_.silver, Align::Left, TextFx::Shadowed);
    r.text(cs, "AUDIO TOOLS", c.X(L_.crown.right() + 26), c.Y(112), th_.dim, Align::Left, TextFx::Shadowed);

    // The plug-in name, big, with its notes under it.
    const Rect ta = c.T(L_.title);
    const TextStyle ns = c.fit(TextStyle::Display, L_.name, ta.w);
    r.text(ns, L_.name, ta.x, ta.y + ta.h * 0.66f, th_.teal, Align::Left, TextFx::Shadowed);
    float y = L_.title.bottom() + 30;
    for (const std::string& n : L_.leftNotes) {
        const TextStyle s = c.fit(TextStyle::Label, n, ta.w);
        r.text(s, n, ta.x + 2 * z, c.Y(y), th_.label, Align::Left, TextFx::Shadowed);
        y += 36;
    }
    // Right column notes, in pairs (white line, teal line).
    if (!L_.sideNotes.empty()) {
        const float x = c.X(1545);
        float sy = 236;
        for (std::size_t i = 0; i < L_.sideNotes.size(); ++i) {
            const std::string& n = L_.sideNotes[i];
            const TextStyle s = c.fit(TextStyle::Label, n, 180 * z);
            r.text(s, n, x, c.Y(sy), i % 2 == 0 ? th_.silver : th_.teal, Align::Center, i % 2 == 0 ? TextFx::Shadowed : TextFx::Shadowed);
            sy += i % 2 == 0 ? 34 : 70;
        }
    }
    // Footer: the tagline in its window, names at the ends.
    const Rect fp = c.T(L_.footerPlate);
    r.fillRound(fp.inset(4 * z), 6 * z, kGlass, Blend::Over);
    const TextStyle tg = c.fit(TextStyle::Tab, L_.tagline, fp.w - 40 * z);
    r.text(tg, L_.tagline, fp.cx(), c.mid(fp, tg), th_.teal, Align::Center, TextFx::Shadowed);
    const Rect ft = c.T(L_.footer);
    r.text(cs, L_.name, c.X(80), c.mid(ft, cs), th_.dim, Align::Left, TextFx::Shadowed);
    r.text(cs, "HYBRID ELITE MAFIA", c.X(kPlateW - 80), c.mid(ft, cs), th_.dim, Align::Right, TextFx::Shadowed);
}

void FaceView::knob(const Ctx& c, const Widget& w, int wi) const {
    namespace kn = hc::ui::knob;
    Renderer& r = c.r;
    const int param = widgetParam(w, c.st);
    if (param < 0 || param >= static_cast<int>(params_.size())) return;
    const hc::fx::ParamInfo& pi = params_[static_cast<std::size_t>(param)];
    const float z = c.z;
    const Rect dial = c.T(w.r);
    const float cx = dial.cx(), cy = dial.cy(), R = dial.w * 0.5f;
    const float n = std::clamp(c.host.normalized(param), 0.0f, 1.0f);
    const float ang = kKnobMin + (kKnobMax - kKnobMin) * n;
    const bool bipolar = pi.min < 0.0f && pi.max > 0.0f;
    const float a0 = kKnobMin + (kKnobMax - kKnobMin) * (bipolar ? hc::fx::toNormalized(pi, 0.0f) : 0.0f);
    const bool on = c.host.active(param);
    const bool hot = c.st.hover == wi || c.st.active == wi;

    // Dashed teal scale: 31 dashes, lit up to the value.
    const float ar = kn::kArcRadius * R, aw = kn::kArcWidth * R * 1.1f;
    r.sprite(Sprite::KnobTrack, {cx - R, cy - R, 2 * R, 2 * R});
    constexpr int kDashes = 31;
    const float span = (kKnobMax - kKnobMin) / static_cast<float>(kDashes);
    const float lo = std::min(a0, ang), hi = std::max(a0, ang);
    for (int i = 0; i < kDashes; ++i) {
        const float d0 = kKnobMin + span * static_cast<float>(i) + span * 0.18f, d1 = d0 + span * 0.64f;
        const float m = 0.5f * (d0 + d1);
        const bool lit = on && m >= lo - span * 0.5f && m <= hi + span * 0.5f && std::fabs(ang - a0) > 0.004f;
        r.arc(cx, cy, ar, aw, d0, d1, lit ? th_.teal : th_.teal.withAlpha(0.14f), false);
    }
    if (on) {   // the lit tip of the scale: a solid bright dot, no glow around it
        const float tx = cx + std::sin(ang) * ar, ty = cy - std::cos(ang) * ar, dr = aw * 1.15f;
        const Rgba tip{th_.teal.r + (1.0f - th_.teal.r) * 0.45f, th_.teal.g + (1.0f - th_.teal.g) * 0.45f, th_.teal.b + (1.0f - th_.teal.b) * 0.45f, 1.0f};
        r.fillRound({tx - dr, ty - dr, 2 * dr, 2 * dr}, dr, tip, Blend::Over, false);
    }
    const float bodyR = kn::kBody * R;
    r.sprite(Sprite::KnobShadow, {cx - bodyR * 1.14f + 0.06f * bodyR, cy - bodyR * 1.14f + 0.10f * bodyR, bodyR * 2.28f, bodyR * 2.28f});
    const float bd = bodyR * 2.0f * 256.0f / 254.0f;
    r.spriteRotated(Sprite::KnobBody, cx, cy, bd, bd, ang);
    const float len = (kn::kIndicatorTo - kn::kIndicatorFrom) * bodyR, mid = 0.5f * (kn::kIndicatorTo + kn::kIndicatorFrom) * bodyR;
    const float ih = len * 96.0f / 72.0f, iw = ih * 24.0f / 96.0f;
    r.spriteRotated(Sprite::KnobIndicator, cx + std::sin(ang) * mid, cy - std::cos(ang) * mid, iw, ih, ang,
                    {0.55f + 0.45f * th_.teal.r, 0.55f + 0.45f * th_.teal.g, 0.55f + 0.45f * th_.teal.b, 1.0f}, Blend::Over, false);
    r.sprite(Sprite::KnobLight, {cx - bd / 2, cy - bd / 2, bd, bd});
    // A knob its section's switch has turned off stays a whole knob, pointer and value showing: only its lights go
    // out (scale and tip unlit, caption and value dim). The switch's own light says the section is off.

    // Caption above, value under, scale ends at the arc's ends.
    const TextStyle cap = c.fit(w.small ? TextStyle::Caption : TextStyle::Label, w.caption, dial.w + (w.small ? 26 : 70) * z);
    r.text(cap, w.caption, cx, dial.y - (w.small ? 12 : 16) * z, on ? th_.label : th_.dim, Align::Center, TextFx::Shadowed);
    const std::string v = c.host.text(param);
    const TextStyle vs = c.fit(w.small ? TextStyle::Value : TextStyle::Title, v, dial.w + (w.small ? 26 : 50) * z);
    r.text(vs, v, cx, dial.bottom() + (w.small ? 26 : 32) * z, on ? (hot ? th_.silver : th_.teal) : th_.dim, Align::Center,
           on ? TextFx::Shadowed : TextFx::Shadowed);
    // The scale's end labels sit in the dial's own bottom corners, under the ends of the arc and above
    // the value, so they can never run into the value or into the next knob. A label too wide for its
    // corner is left out (both go, so the pair stays balanced) - the value underneath says it anyway.
    if (!w.lo.empty() || !w.hi.empty()) {
        const TextStyle ss = c.ts(TextStyle::Caption);
        const float room = R * 0.62f;
        if (r.textWidth(ss, w.lo) <= room && r.textWidth(ss, w.hi) <= room) {
            const float y = dial.bottom() - 2 * z, dx = R * 0.70f;
            r.text(ss, w.lo, cx - dx, y, th_.dim, Align::Center, TextFx::Shadowed);
            r.text(ss, w.hi, cx + dx, y, th_.dim, Align::Center, TextFx::Shadowed);
        }
    }
}

void FaceView::button(const Ctx& c, Rect b, std::string_view text, bool lit, bool hot, bool dim, Rgba colour) const {
    Renderer& r = c.r;
    const float z = c.z;
    const TextStyle bs = c.fit(TextStyle::Label, text, b.w - 16 * z);
    if (lit && !dim) {   // a solid lit button: no glow or bloom around it (Steven: no haze)
        r.fillRound(b.inset(2 * z), 9 * z, colour.withAlpha(0.92f), Blend::Over, false);
        r.fillRound({b.x + 8 * z, b.y + 5 * z, b.w - 16 * z, b.h * 0.34f}, 7 * z, {1, 1, 1, 0.14f}, Blend::Add);
        r.text(bs, text, b.cx(), c.mid(b, bs), kInk, Align::Center, TextFx::Plain);
    } else {
        r.fillRound(b.inset(2 * z), 9 * z, kGlass, Blend::Over);
        r.fillRound({b.x + 6 * z, b.y + 3 * z, b.w - 12 * z, 2 * z}, 1 * z, colour.withAlpha(dim ? 0.12f : hot ? 0.8f : 0.32f), Blend::Over, false);
        r.text(bs, text, b.cx(), c.mid(b, bs), dim ? th_.dim : hot ? th_.silver : th_.label, Align::Center, TextFx::Shadowed);
    }
}

// Piano keys: white ivory-grey and black keys; a sounding key lights teal (solid, no glow). The C keys carry their octave.
void FaceView::keyboard(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rgba ivory = rgb8(0xC9CFD2), ebony = rgb8(0x0B0E10);
    const TextStyle ls = c.ts(TextStyle::Caption);
    for (int pass = 0; pass < 2; ++pass)   // whites first, the blacks on top
        for (int k = 0; k < w.choice; ++k) {
            const bool black = keyIsBlack(k);
            if (black != (pass == 1)) continue;
            const Rect kr = c.T(keyRect(w.r, w.choice, k));
            const bool on = plain(c.host, w.param + k) >= 0.5f, live = c.host.active(w.param + k);
            const Rgba col = on ? th_.teal : black ? ebony : (live ? ivory : ivory.withAlpha(0.55f));
            const Rect body = black ? Rect{kr.x, kr.y, kr.w, kr.h} : kr.inset(1.0f * z);
            r.fillRound(body, (black ? 3.0f : 4.0f) * z, col, Blend::Over, false);
            if (!black && k % 12 == 0) {
                const std::string name = "C" + std::to_string(2 + k / 12);
                r.text(ls, name, kr.cx(), kr.bottom() - 8 * z, on ? kInk : rgb8(0x384046), Align::Center, TextFx::Plain);
            }
        }
}

void FaceView::stepper(const Ctx& c, const Widget& w, int wi) const {
    Renderer& r = c.r;
    const float z = c.z;
    const int param = widgetParam(w, c.st);
    const Rect b = c.T(w.r);
    const bool on = c.host.active(param), hot = c.st.hover == wi;
    r.fillRound(b.inset(2 * z), 8 * z, kGlass, Blend::Over);
    // a narrow one (the 10-band grids) gives the arrows' room to the words; its halves still step
    const bool arrows = w.r.w >= 130.0f;
    const float s = arrows ? b.h * 0.42f : 0.0f;
    const Rgba ic = on ? (hot ? th_.silver : th_.teal) : th_.dim;
    if (arrows) {
        r.sprite(Sprite::IconPrev, {b.x + 8 * z, b.cy() - s * 0.5f, s, s}, ic, Blend::Over, false);
        r.sprite(Sprite::IconNext, {b.right() - 8 * z - s, b.cy() - s * 0.5f, s, s}, ic, Blend::Over, false);
    }
    const std::string v = c.host.text(param);
    const TextStyle vs = c.fit(TextStyle::Label, v, b.w - 2 * s - (arrows ? 26.0f : 12.0f) * z);
    r.text(vs, v, b.cx(), c.mid(b, vs), on ? th_.teal : th_.dim, Align::Center, on ? TextFx::Shadowed : TextFx::Shadowed);
    const TextStyle cs = c.ts(TextStyle::Caption);
    r.text(cs, w.caption, b.x - 12 * z, c.mid(b, cs), on ? th_.label : th_.dim, Align::Right, TextFx::Shadowed);
}

void FaceView::rangeSlider(const Ctx& c, const Widget& w, int wi) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect plot = plotArea(L_.display);
    const float lo = plain(c.host, w.param), hi = plain(c.host, w.param2);
    const Rect t = c.T(w.r);
    const Rect track{t.x, t.cy() - 4 * z, t.w, 8 * z};
    r.fillRound(track, 4 * z, kGlass, Blend::Over);
    const float x0 = c.X(freqToX(plot, lo)), x1 = c.X(freqToX(plot, hi));
    const Rect sel{x0, track.y, std::max(x1 - x0, 1.0f), track.h};
    r.fillRound(sel, 4 * z, th_.teal, Blend::Over, false);
    const bool hot = c.st.hover == wi || c.st.active == wi;
    for (float x : {x0, x1}) {
        const Rect h{x - 9 * z, t.y + 4 * z, 18 * z, t.h - 8 * z};
        r.fillRound(h.offset(2 * z, 3 * z), 6 * z, rgb8(0x000000, 0.5f), Blend::Over);
        r.fillRound(h, 6 * z, hot ? th_.silver : rgb8(0xA8B2B8), Blend::Over);
        r.fillRound({h.cx() - 1.5f * z, h.y + 6 * z, 3 * z, h.h - 12 * z}, 1.5f * z, th_.tealDeep, Blend::Over, false);
    }
    const std::string text = w.caption + "   " + c.host.text(w.param) + "  -  " + c.host.text(w.param2);
    const TextStyle s = c.fit(TextStyle::Caption, text, t.w);
    r.text(s, text, t.cx(), t.bottom() + 24 * z, th_.label, Align::Center, TextFx::Shadowed);
}

void FaceView::bypass(const Ctx& c, const Widget& w, int wi) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect b = c.T(w.r);
    const bool on = !c.host.bypassed(), hot = c.st.hover == wi;
    if (on) {   // solid teal: no glow and not emissive, so the bloom leaves no haze around it
        r.fillRound(b.inset(3 * z), b.w * 0.5f - 3 * z, th_.teal.withAlpha(0.95f), Blend::Over, false);
        r.fillRound({b.x + b.w * 0.22f, b.y + b.h * 0.12f, b.w * 0.56f, b.h * 0.3f}, b.h * 0.15f, {1, 1, 1, 0.18f}, Blend::Add);
    } else {
        r.fillRound(b.inset(3 * z), b.w * 0.5f - 3 * z, kGlass, Blend::Over);
        r.arc(b.cx(), b.cy(), b.w * 0.44f, 2.5f * z, -kPi, kPi, th_.teal.withAlpha(hot ? 0.7f : 0.3f), false);
    }
    const char* t = on ? "ON" : "BYPASS";
    const TextStyle s = c.fit(TextStyle::Label, t, b.w * 0.8f);
    r.text(s, t, b.cx(), c.mid(b, s), on ? kInk : th_.label, Align::Center, on ? TextFx::Plain : TextFx::Shadowed);
    if (!w.caption.empty()) {
        const TextStyle cs = c.ts(TextStyle::Caption);
        r.text(cs, w.caption, b.cx(), b.bottom() + 30 * z, th_.label, Align::Center, TextFx::Shadowed);
    }
}

void FaceView::grid(const Ctx& c, Rect plot, float dbTop, float dbBottom, float dbStep, bool signedLabels) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect p = c.T(plot);
    const TextStyle s = c.ts(TextStyle::Caption);
    // on a narrow screen only the decades get a number (the in-between ones would run together)
    const bool narrow = plot.w < 700.0f;
    for (float hz : {20.f, 30.f, 40.f, 50.f, 60.f, 70.f, 80.f, 90.f, 100.f, 200.f, 300.f, 400.f, 500.f, 600.f, 700.f, 800.f, 900.f, 1000.f, 2000.f,
                     3000.f, 4000.f, 5000.f, 6000.f, 7000.f, 8000.f, 9000.f, 10000.f, 20000.f}) {
        const float x = c.X(freqToX(plot, hz));
        const float lead = std::log10(hz);
        const bool major = std::fabs(lead - std::round(lead)) < 1e-4f;
        r.fillRound({x - 0.5f * z, p.y, 1.0f * z, p.h}, 0.5f * z, {1, 1, 1, major ? 0.10f : 0.04f}, Blend::Over);
        if ((major && hz >= 100.f) || (!narrow && (hz == 50.f || hz == 200.f || hz == 500.f || hz == 2000.f || hz == 5000.f)))
            r.text(s, hzText(hz), x, p.bottom() + 26 * z, th_.dim, Align::Center, TextFx::Shadowed);
    }
    // on a short screen only every k-th line gets its number (always including 0), so they never touch
    const float stepH = plot.h * dbStep / std::max(dbTop - dbBottom, 1e-3f);
    const int every = std::max(1, static_cast<int>(std::ceil(26.0f / std::max(stepH, 1.0f))));
    for (float db = dbTop; db >= dbBottom - 1e-3f; db -= dbStep) {
        const float y = p.y + (dbTop - db) / (dbTop - dbBottom) * p.h;
        r.fillRound({p.x, y - 0.5f * z, p.w, 1.0f * z}, 0.5f * z, {1, 1, 1, std::fabs(db) < 1e-3f ? 0.16f : 0.06f}, Blend::Over);
        if (static_cast<int>(std::lround(db / dbStep)) % every != 0) continue;
        const std::string t = signedLabels && db > 0 ? fmt("+%.0f", db) : fmt("%.0f", db);
        r.text(s, t, p.x - 12 * z, y + r.capHeight(s) * 0.5f, th_.dim, Align::Right, TextFx::Shadowed);
    }
}

void FaceView::spectrum(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect plot = plotArea(w.r);
    const Rect p = c.T(plot);
    constexpr float kTop = 0.0f, kBottom = -96.0f;
    grid(c, plot, kTop, kBottom, 12.0f, false);
    auto yOf = [&](float db) { return p.y + std::clamp((kTop - db) / (kTop - kBottom), 0.0f, 1.0f) * p.h; };
    const bool isRestorer = L_.face == rack::Face::Restorer;
    // Outside FREQUENCY RANGE: shaded (nothing is touched there).
    if (isRestorer) {
        const float x0 = c.X(freqToX(plot, plain(c.host, rack::restorer::LowHz)));
        const float x1 = c.X(freqToX(plot, plain(c.host, rack::restorer::HighHz)));
        if (x0 > p.x + 0.5f) r.fillRound({p.x, p.y, x0 - p.x, p.h}, 0, rgb8(0x000000, 0.45f), Blend::Over);
        if (x1 < p.right() - 0.5f) r.fillRound({x1, p.y, p.right() - x1, p.h}, 0, rgb8(0x000000, 0.45f), Blend::Over);
    }
    if (c.feed.hasSpectrum()) {
        float xs[rack::kSpectrumBins], yb[rack::kSpectrumBins], ya[rack::kSpectrumBins];
        for (int i = 0; i < rack::kSpectrumBins; ++i) {
            xs[i] = c.X(freqToX(plot, rack::spectrumBinHz(i)));
            yb[i] = yOf(c.feed.before(i));
            ya[i] = yOf(c.feed.after(i));
        }
        // AFTER: teal light filling down to the floor.
        for (int i = 0; i + 1 < rack::kSpectrumBins; ++i) {
            const float qx[4] = {xs[i], xs[i + 1], xs[i + 1], xs[i]}, qy[4] = {ya[i], ya[i + 1], p.bottom(), p.bottom()};
            const Rgba top = th_.teal.withAlpha(0.30f), bot = th_.teal.withAlpha(0.03f);
            const Rgba cols[4] = {top, top, bot, bot};
            r.colorQuad(qx, qy, cols, Blend::Over, false);
        }
        r.polyline(xs, yb, rack::kSpectrumBins, 1.6f * z, th_.before.withAlpha(0.75f), Blend::Over, false);
        r.polyline(xs, ya, rack::kSpectrumBins, 2.4f * z, th_.teal, Blend::Over, false);
    }
    if (L_.face == rack::Face::Exciter) {   // below FREQUENCY nothing is added: dim it, mark the edge
        const float x1 = c.X(freqToX(plot, plain(c.host, rack::exciter::Freq)));
        if (x1 > p.x + 0.5f) r.fillRound({p.x, p.y, x1 - p.x, p.h}, 0, rgb8(0x000000, 0.55f), Blend::Over);
        const float lx[2] = {x1, x1}, ly[2] = {p.y, p.bottom()};
        r.polyline(lx, ly, 2, 2.0f * z, th_.gold.withAlpha(0.8f), Blend::Over, false);
    }
    const TextStyle s = c.ts(TextStyle::Caption);
    float lx = p.x + 14 * z;
    auto legend = [&](const char* t, Rgba col) {
        r.fillRound({lx, p.y + 14 * z, 18 * z, 4 * z}, 2 * z, col, Blend::Over, false);
        r.text(s, t, lx + 26 * z, p.y + 14 * z + r.capHeight(s) * 0.6f, col, Align::Left, TextFx::Shadowed);
        lx += 26 * z + r.textWidth(s, t) + 30 * z;
    };
    legend("BEFORE", th_.before);
    legend("AFTER", th_.teal);
    if (c.feed.hasNoiseFloor()) {
        legend("NOISE FLOOR", th_.gold);
        float xs[rack::kSpectrumBins], ys[rack::kSpectrumBins];
        for (int i = 0; i < rack::kSpectrumBins; ++i) {
            xs[i] = c.X(freqToX(plot, rack::spectrumBinHz(i)));
            ys[i] = yOf(c.feed.noiseFloor(i));
        }
        for (int i = 0; i + 2 < rack::kSpectrumBins; i += 4) r.polyline(xs + i, ys + i, 3, 2.0f * z, th_.gold.withAlpha(0.9f), Blend::Over, false);
        const float rem = c.feed.removedDb();
        const std::string t = rem < -0.05f ? fmt("TAKING OUT %.1f dB", -rem) : std::string("NOTHING TAKEN OUT");
        const TextStyle vs = c.ts(TextStyle::Value);
        r.text(vs, t, p.right() - 14 * z, p.y + 14 * z + r.capHeight(vs), th_.teal, Align::Right, TextFx::Shadowed);
    }
    if (isRestorer && plain(c.host, rack::restorer::Listen) >= 0.5f) {
        const char* t = "LISTEN IS ON: YOU HEAR ONLY WHAT IS TAKEN OUT";
        const TextStyle ls = c.fit(TextStyle::Label, t, p.w - 40 * z);
        r.text(ls, t, p.cx(), p.bottom() - 18 * z, th_.gold, Align::Center, TextFx::Shadowed);
    }
}

void FaceView::eqGraph(const Ctx& c, const Widget& w) const {
    using namespace rack::eq;
    Renderer& r = c.r;
    const float z = c.z;
    const Rect plot = plotArea(w.r);
    const Rect p = c.T(plot);
    const float range = kGraphRanges[static_cast<std::size_t>(std::clamp(c.st.graphRange, 0, 3))];
    const float step = range <= 3 ? 1.0f : range <= 6 ? 2.0f : range <= 12 ? 3.0f : 10.0f;
    // The music behind the curve (its own scale: 0 .. -96 dB over the whole height).
    if (c.feed.hasSpectrum()) {
        float xs[rack::kSpectrumBins], ya[rack::kSpectrumBins], yb[rack::kSpectrumBins];
        for (int i = 0; i < rack::kSpectrumBins; ++i) {
            xs[i] = c.X(freqToX(plot, rack::spectrumBinHz(i)));
            ya[i] = p.y + std::clamp(-c.feed.after(i) / 96.0f, 0.0f, 1.0f) * p.h;
            yb[i] = p.y + std::clamp(-c.feed.before(i) / 96.0f, 0.0f, 1.0f) * p.h;
        }
        for (int i = 0; i + 1 < rack::kSpectrumBins; ++i) {
            const float qx[4] = {xs[i], xs[i + 1], xs[i + 1], xs[i]}, qy[4] = {ya[i], ya[i + 1], p.bottom(), p.bottom()};
            const Rgba top = th_.teal.withAlpha(0.16f), bot = th_.teal.withAlpha(0.02f);
            const Rgba cols[4] = {top, top, bot, bot};
            r.colorQuad(qx, qy, cols, Blend::Over, false);
        }
        r.polyline(xs, yb, rack::kSpectrumBins, 1.2f * z, th_.before.withAlpha(0.35f), Blend::Over, false);
    }
    grid(c, plot, range, -range, step, true);
    auto yOf = [&](double db) { return p.cy() - static_cast<float>(std::clamp(db / range, -1.04, 1.04)) * p.h * 0.5f; };

    // Band settings as the audio has them.
    std::array<Band, kBands> bands{};
    std::array<Sections, kBands> secs{};
    std::array<Sections, kBands> live{};
    bool anyOn = false, anyDyn = false;
    const double fs = c.feed.sampleRate();
    for (int b = 0; b < kBands; ++b) {
        Band& bd = bands[static_cast<std::size_t>(b)];
        bd.on = plain(c.host, bandParam(b, On)) >= 0.5f;
        bd.type = static_cast<Type>(std::clamp(static_cast<int>(std::lround(plain(c.host, bandParam(b, TypeC)))), 0, static_cast<int>(Type::kCount) - 1));
        bd.freq = plain(c.host, bandParam(b, Freq));
        bd.gainDb = plain(c.host, bandParam(b, Gain));
        bd.q = plain(c.host, bandParam(b, Q));
        bd.slopeDb = kSlopes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(plain(c.host, bandParam(b, Slope)))), 0, 5))];
        bd.stereo = static_cast<Stereo>(std::clamp(static_cast<int>(std::lround(plain(c.host, bandParam(b, StereoC)))), 0, 4));
        if (!bd.on) continue;
        anyOn = true;
        secs[static_cast<std::size_t>(b)] = design(bd, fs);
        const bool dyn = plain(c.host, bandParam(b, Dyn)) >= 0.5f && typeCanBeDynamic(bd.type);
        Band lb = bd;
        if (dyn) { lb.gainDb += c.feed.dynamicGainDb(b); anyDyn = true; }
        live[static_cast<std::size_t>(b)] = design(lb, fs);
    }
    constexpr int kN = 220;
    float xs[kN], total[kN], liveY[kN];
    const int sel = std::clamp(c.st.selectedBand, 0, kBands - 1);
    const Band& sb = bands[static_cast<std::size_t>(sel)];
    for (int i = 0; i < kN; ++i) {
        const float x = plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kN - 1);
        const double hz = xToFreq(plot, x);
        double t = 0, lv = 0;
        for (int b = 0; b < kBands; ++b) {
            if (!bands[static_cast<std::size_t>(b)].on) continue;
            t += magnitudeDb(secs[static_cast<std::size_t>(b)], fs, hz);
            lv += magnitudeDb(live[static_cast<std::size_t>(b)], fs, hz);
        }
        xs[i] = c.X(x);
        total[i] = yOf(t);
        liveY[i] = yOf(lv);
    }
    // Every band's own line, in its own colour (the bright teal line is all of them added together).
    for (int b = 0; b < kBands; ++b) {
        if (!bands[static_cast<std::size_t>(b)].on || b == sel) continue;
        float own[kN];
        for (int i = 0; i < kN; ++i)
            own[i] = yOf(magnitudeDb(secs[static_cast<std::size_t>(b)], fs, xToFreq(plot, plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kN - 1))));
        r.polyline(xs, own, kN, 1.6f * z, bandColour(b).withAlpha(0.8f), Blend::Over, false);
    }
    // The selected band's own shape: filled from 0 dB, its line drawn stronger.
    if (sb.on) {
        const Rgba col = bandColour(sel);
        float own[kN];
        float prevY = 0;
        for (int i = 0; i < kN; ++i) {
            const double hz = xToFreq(plot, plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kN - 1));
            const float y = yOf(magnitudeDb(secs[static_cast<std::size_t>(sel)], fs, hz));
            own[i] = y;
            if (i > 0) {
                const float qx[4] = {xs[i - 1], xs[i], xs[i], xs[i - 1]}, qy[4] = {prevY, y, p.cy(), p.cy()};
                const Rgba a = col.withAlpha(0.22f), b2 = col.withAlpha(0.05f);
                const Rgba cols[4] = {a, a, b2, b2};
                r.colorQuad(qx, qy, cols, Blend::Over, false);
            }
            prevY = y;
        }
        r.polyline(xs, own, kN, 2.2f * z, col, Blend::Over, false);
    }
    if (anyOn) {
        r.polyline(xs, total, kN, 2.8f * z, th_.teal, Blend::Over, false);
        if (anyDyn) r.polyline(xs, liveY, kN, 1.6f * z, th_.silver.withAlpha(0.85f), Blend::Over, false);
    } else {
        const float xs2[2] = {p.x, p.right()}, ys2[2] = {p.cy(), p.cy()};
        r.polyline(xs2, ys2, 2, 2.0f * z, th_.teal.withAlpha(0.6f), Blend::Over, false);
        const char* t = "DOUBLE-CLICK THE GRAPH TO ADD A BAND";
        const TextStyle hs = c.fit(TextStyle::Label, t, p.w * 0.8f);
        r.text(hs, t, p.cx(), p.cy() - 30 * z, th_.label, Align::Center, TextFx::Shadowed);
    }
    // Band nodes.
    const TextStyle ns = c.ts(TextStyle::Caption);
    for (int b = 0; b < kBands; ++b) {
        const Band& bd = bands[static_cast<std::size_t>(b)];
        if (!bd.on) continue;
        const Rgba col = bandColour(b);
        const float x = c.X(freqToX(plot, static_cast<float>(bd.freq)));
        const float y = typeHasGain(bd.type) ? yOf(bd.gainDb) : p.cy();
        const bool selected = b == sel, hot = c.st.hoverNode == b || c.st.dragNode == b;
        const float rad = (selected ? 15.0f : 12.0f) * z;
        if (typeCanBeDynamic(bd.type) && plain(c.host, bandParam(b, Dyn)) >= 0.5f) {
            const float yd = yOf(bd.gainDb + c.feed.dynamicGainDb(b));
            const float lx[2] = {x, x}, ly[2] = {y, yd};
            r.polyline(lx, ly, 2, 2.0f * z, col.withAlpha(0.7f), Blend::Over, false);
            r.arc(x, yd, rad * 0.75f, 2.0f * z, -kPi, kPi, col, false);
        }
        r.fillRound({x - rad + 1.5f * z, y - rad + 2.5f * z, 2 * rad, 2 * rad}, rad, rgb8(0x000000, 0.55f), Blend::Over);
        r.fillRound({x - rad, y - rad, 2 * rad, 2 * rad}, rad, col, Blend::Over, false);
        if (selected || hot) r.arc(x, y, rad + 4 * z, 2.0f * z, -kPi, kPi, selected ? th_.silver : th_.silver.withAlpha(0.6f), false);   // a ring, no glow
        const std::string t = std::to_string(b + 1);
        r.text(ns, t, x, y + r.capHeight(ns) * 0.5f, kInk, Align::Center, TextFx::Plain);
        if (const char* tag = stereoTag(bd.stereo); *tag)   // MID / SIDE / LEFT / RIGHT band: its letter beside the dot
            r.text(ns, tag, x + rad + 6 * z, y - rad * 0.4f, col, Align::Left, TextFx::Shadowed);
    }
    // AUTO GAIN readout.
    if (plain(c.host, Global::AutoGain) >= 0.5f) {
        const std::string t = fmt("AUTO GAIN %+.1f dB", c.feed.autoGainDb());
        const TextStyle vs = c.ts(TextStyle::Value);
        r.text(vs, t, p.x + 14 * z, p.y + 14 * z + r.capHeight(vs), th_.teal, Align::Left, TextFx::Shadowed);
    }
}

void FaceView::spectrumBehind(const Ctx& c, Rect plot, float alpha) const {
    if (!c.feed.hasSpectrum()) return;
    Renderer& r = c.r;
    const Rect p = c.T(plot);
    float xs[rack::kSpectrumBins], ya[rack::kSpectrumBins], yb[rack::kSpectrumBins];
    for (int i = 0; i < rack::kSpectrumBins; ++i) {
        xs[i] = c.X(freqToX(plot, rack::spectrumBinHz(i)));
        ya[i] = p.y + std::clamp(-c.feed.after(i) / 96.0f, 0.0f, 1.0f) * p.h;
        yb[i] = p.y + std::clamp(-c.feed.before(i) / 96.0f, 0.0f, 1.0f) * p.h;
    }
    for (int i = 0; i + 1 < rack::kSpectrumBins; ++i) {
        const float qx[4] = {xs[i], xs[i + 1], xs[i + 1], xs[i]}, qy[4] = {ya[i], ya[i + 1], p.bottom(), p.bottom()};
        const Rgba top = th_.teal.withAlpha(alpha), bot = th_.teal.withAlpha(0.02f);
        const Rgba cols[4] = {top, top, bot, bot};
        r.colorQuad(qx, qy, cols, Blend::Over, false);
    }
    r.polyline(xs, yb, rack::kSpectrumBins, 1.2f * c.z, th_.before.withAlpha(0.35f), Blend::Over, false);
}

// CONSOLE EQ / TUBE EQ: the music behind, the EQ's curve in front (the same filter maths the audio runs).
void FaceView::consoleCurve(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect plot = plotArea(w.r);
    const Rect p = c.T(plot);
    const bool tube = L_.face == rack::Face::TubeEq;
    const rack::PluginInfo* info = rack::findPlugin(L_.name.c_str());
    const rack::CurveInfo* ci = info ? info->curve : nullptr;   // the classic EQs describe their own curve
    if (!ci || ci->spectrumParam < 0 || plain(c.host, ci->spectrumParam) >= 0.5f) spectrumBehind(c, plot, 0.16f);
    float kRange = ci ? ci->range : tube ? 20.0f : 18.0f;
    if (ci && ci->scaleParam >= 0) kRange = plain(c.host, ci->scaleParam) >= 0.5f ? 30.0f : 12.0f;
    grid(c, plot, kRange, -kRange, kRange >= 30.0f ? 10.0f : kRange >= 18.0f ? 6.0f : kRange >= 15.0f ? 5.0f : 3.0f, true);
    std::array<float, hc::fx::Effect::kMaxParams> v{};
    const int count = static_cast<int>(std::min<std::size_t>(params_.size(), v.size()));
    for (int i = 0; i < count; ++i) v[i] = plain(c.host, i);
    const double fs = c.feed.sampleRate();
    const rack::tubeeq::Settings ts = rack::tubeeq::settingsFrom(v.data());
    auto curve = [&](double hz) {
        if (ci) return ci->db(v.data(), fs, hz);
        return rack::tubeeq::curveDb(ts, fs, hz);
    };
    auto yOf = [&](double db) { return p.cy() - static_cast<float>(std::clamp(db / kRange, -1.04, 1.04)) * p.h * 0.5f; };
    constexpr int kN = 220;
    float xs[kN], ys[kN];
    for (int i = 0; i < kN; ++i) {
        const float x = plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kN - 1);
        xs[i] = c.X(x);
        ys[i] = yOf(curve(xToFreq(plot, x)));
    }
    for (int i = 0; i + 1 < kN; ++i) {   // light between the curve and 0 dB
        const float qx[4] = {xs[i], xs[i + 1], xs[i + 1], xs[i]}, qy[4] = {ys[i], ys[i + 1], p.cy(), p.cy()};
        const Rgba a = th_.teal.withAlpha(0.20f), b = th_.teal.withAlpha(0.04f);
        const Rgba cols[4] = {a, a, b, b};
        r.colorQuad(qx, qy, cols, Blend::Over, false);
    }
    r.polyline(xs, ys, kN, 2.8f * z, th_.teal, Blend::Over, false);
    if (ci && ci->dbRight) {   // the right side, in gold, when it isn't the same as the left
        float yr[kN];
        bool differs = false;
        for (int i = 0; i < kN; ++i) {
            yr[i] = yOf(ci->dbRight(v.data(), fs, xToFreq(plot, plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kN - 1))));
            differs = differs || std::fabs(yr[i] - ys[i]) > 0.5f;
        }
        if (differs) r.polyline(xs, yr, kN, 2.2f * z, th_.gold, Blend::Over, false);
    }
    // A dot on the curve at each band's frequency.
    struct Dot { double hz; Rgba col; };
    std::array<Dot, 12> dots{};
    int nd = 0;
    if (ci) {
        double hz[12];
        const Rgba cols[3] = {th_.teal, th_.gold, th_.violet};
        nd = ci->dots(v.data(), hz, 12);
        for (int i = 0; i < nd; ++i) dots[static_cast<std::size_t>(i)] = {hz[i], cols[i % 3]};
    } else {
        dots[nd++] = {ts.lowHz, th_.gold};
        dots[nd++] = {ts.highHz, th_.violet};
        dots[nd++] = {ts.attenHz, th_.teal};
    }
    for (int i = 0; i < nd; ++i) {
        const double hz = std::clamp(dots[static_cast<std::size_t>(i)].hz, 20.0, 20000.0);
        const float x = c.X(freqToX(plot, static_cast<float>(hz))), y = yOf(curve(hz));
        r.fillRound({x - 7 * z, y - 7 * z, 14 * z, 14 * z}, 7 * z, dots[static_cast<std::size_t>(i)].col, Blend::Over, false);
    }
}

// TAPE: a VU meter (0 VU = -18 dBFS), its scale linear in voltage like the real thing.
void FaceView::vuMeter(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect d = c.T(w.r);
    const int ch = std::clamp(w.choice, 0, 1);
    const int calIdx = std::clamp(static_cast<int>(std::lround(plain(c.host, rack::tape::VuCal))), 0, 3);
    const float cal = rack::tape::kVuCalDbfs[calIdx];
    const float vu = std::clamp(c.feed.meterDb(ch) - cal, -22.0f, 4.0f);
    if (vu_[ch] > 50.0f) vu_[ch] = vu;
    vu_[ch] += (vu - vu_[ch]) * 0.35f;
    const float px = d.cx(), py = d.bottom() - 30 * z, R = std::min(d.w * 0.44f, d.h * 0.78f);
    auto angle = [](float v) {
        const float a = std::pow(10.0f, v / 20.0f), lo = 0.1f, hi = std::pow(10.0f, 3.0f / 20.0f);
        const float t = std::clamp((a - lo) / (hi - lo), 0.0f, 1.0f);
        return (-50.0f + 100.0f * t) * kPi / 180.0f;
    };
    r.arc(px, py, R, 3.0f * z, angle(-20.0f), angle(0.0f), th_.teal.withAlpha(0.45f), false);
    r.arc(px, py, R, 5.0f * z, angle(0.0f), angle(3.0f), th_.red.withAlpha(0.85f), false);
    const TextStyle ts = c.ts(TextStyle::Caption);
    for (float v : {-20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f}) {
        const float a = angle(v), sx = std::sin(a), sy = -std::cos(a);
        const float xs[2] = {px + sx * (R - 14 * z), px + sx * (R + 8 * z)}, ys[2] = {py + sy * (R - 14 * z), py + sy * (R + 8 * z)};
        r.polyline(xs, ys, 2, 2.2f * z, v > 0.0f ? th_.red : th_.silver.withAlpha(0.8f), Blend::Over, false);
        if (v == -20.0f || v == -10.0f || v == -7.0f || v == -5.0f || v == -3.0f || v == 0.0f || v == 3.0f) {
            char b[8];
            std::snprintf(b, sizeof b, v > 0.0f ? "+%.0f" : "%.0f", static_cast<double>(v));
            r.text(ts, b, px + sx * (R + 30 * z), py + sy * (R + 30 * z) + r.capHeight(ts) * 0.5f, v > 0.0f ? th_.red : th_.label, Align::Center,
                   TextFx::Shadowed);
        }
    }
    const float a = angle(vu_[ch]);
    const float nx[2] = {px, px + std::sin(a) * R * 0.97f}, ny[2] = {py, py - std::cos(a) * R * 0.97f};
    r.polyline(nx, ny, 2, 2.6f * z, th_.silver, Blend::Over, false);
    r.fillRound({px - 13 * z, py - 13 * z, 26 * z, 26 * z}, 13 * z, rgb8(0x9AA4AA), Blend::Over);
    r.fillRound({px - 6 * z, py - 6 * z, 12 * z, 12 * z}, 6 * z, th_.tealDeep, Blend::Over, false);
    const TextStyle ls = c.ts(TextStyle::Label);
    r.text(ls, "VU", d.x + 24 * z, d.y + 40 * z, th_.teal, Align::Left, TextFx::Shadowed);
    r.text(ls, w.caption, d.right() - 24 * z, d.y + 40 * z, th_.label, Align::Right, TextFx::Shadowed);
    char calText[32];
    std::snprintf(calText, sizeof calText, "0 VU = %.0f dBFS", static_cast<double>(cal));
    r.text(c.ts(TextStyle::Caption), calText, d.x + 24 * z, d.bottom() - 18 * z, th_.dim, Align::Left, TextFx::Shadowed);
}

// A big number in a glass window: one of the effect's meters (LIMITER: loudness, true peak).
void FaceView::readout(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect b = c.T(w.r);
    float v = c.feed.meterDb(w.choice);
    if (std::fabs(v) < 0.05f) v = 0.0f;   // no "-0.0"
    const TextStyle cs = c.ts(TextStyle::Caption);
    const bool small = w.r.h < 110.0f;   // a short window: name and unit on one line, a smaller number
    r.text(cs, w.caption, b.x + 18 * z, b.y + 30 * z, th_.label, Align::Left, TextFx::Shadowed);
    if (small) r.text(cs, w.lo, b.x + 18 * z, b.bottom() - 16 * z, th_.teal, Align::Left, TextFx::Shadowed);
    else r.text(cs, w.lo, b.x + 18 * z, b.y + 60 * z, th_.teal, Align::Left, TextFx::Shadowed);   // the unit, under the name
    char t[24];
    static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int idx = static_cast<int>(std::lround(v));
    if (v <= -99.0f || ((w.hi == "note" || w.hi == "key") && v < -0.5f)) {
        std::snprintf(t, sizeof t, "--");
    } else if (w.hi == "note") {   // a MIDI note number as a name and octave: 57 -> A3
        std::snprintf(t, sizeof t, "%s%d", kNames[std::clamp(idx, 0, 127) % 12], std::clamp(idx, 0, 127) / 12 - 1);
    } else if (w.hi == "key") {    // 0..11 = C..B major, 12..23 = C..B minor; 24..47 the same spelled with flats
        static constexpr const char* kFlats[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
        const int k = std::clamp(idx, 0, 47);
        std::snprintf(t, sizeof t, "%s %s", (k >= 24 ? kFlats : kNames)[k % 12], k % 24 >= 12 ? "MIN" : "MAJ");
    } else {
        const char* f = w.hi == "+2" ? "%+.2f" : w.hi == "+" ? "%+.1f" : w.hi == "0" ? "%.0f" : "%.1f";
        std::snprintf(t, sizeof t, f, static_cast<double>(v));
    }
    const bool over = w.hi == "CEIL" && v > plain(c.host, rack::limiter::Ceiling) + 0.05f;   // true peak above the ceiling
    // the left side keeps the name and unit; on a narrow window the number gets only what they leave
    const float words = std::max(r.textWidth(cs, w.caption), r.textWidth(cs, w.lo));
    const float room = std::max(b.w - 36 * z - words - 14 * z, b.w * 0.3f);
    const TextStyle big = c.fit(small ? TextStyle::Panel : TextStyle::Display, t, std::min(b.w * (small ? 0.5f : 0.55f), room));
    r.text(big, t, b.right() - 18 * z, b.bottom() - (small ? 14 : 18) * z, over ? th_.red : th_.teal, Align::Right, TextFx::Shadowed);
}

// STAKEOUT: the frequency bands as bars from the floor (-90 dB) to their level; with L / R two thin bars per
// band (left teal, right gold), otherwise one; the max line over them when it's on.
void FaceView::rta(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect plot = plotArea(w.r);
    const Rect p = c.T(plot);
    constexpr float kTop = 0.0f, kBottom = -90.0f;
    grid(c, plot, kTop, kBottom, 10.0f, false);
    if (!c.feed.hasAnalyzer()) return;
    const int res = std::clamp(static_cast<int>(std::lround(plain(c.host, rack::stakeout::Resolution))), 0, 2);
    const bool dual = plain(c.host, rack::stakeout::Channels) < 0.5f, showMax = plain(c.host, rack::stakeout::ShowMax) >= 0.5f;
    const int nb = rack::stakeout::bandCount(res), low = rack::stakeout::lowBands(res);
    auto yOf = [&](float db) { return p.y + std::clamp((kTop - db) / (kTop - kBottom), 0.0f, 1.0f) * p.h; };
    float mx[2][rack::kRtaMaxBands], my[2][rack::kRtaMaxBands];
    for (int b = 0; b < nb; ++b) {
        const float hz = rack::stakeout::bandHz(res, b);
        const float lo = b < low ? hz - 0.5f * rack::stakeout::kLowStep[res] : b == low ? 240.0f + 0.5f * rack::stakeout::kLowStep[res]
                                                                                       : std::sqrt(rack::stakeout::bandHz(res, b - 1) * hz);
        const float hi = b < low ? hz + 0.5f * rack::stakeout::kLowStep[res] : b + 1 < nb ? std::sqrt(hz * rack::stakeout::bandHz(res, b + 1)) : 21000.0f;
        float x0 = c.X(freqToX(plot, std::max(lo, 20.0f))), x1 = c.X(freqToX(plot, std::min(hi, 20000.0f)));
        x0 += 0.6f * z;
        x1 -= 0.6f * z;
        if (x1 <= x0) continue;
        const int sides = dual ? 2 : 1;
        for (int sd = 0; sd < sides; ++sd) {
            const float a = dual ? x0 + (x1 - x0) * 0.5f * static_cast<float>(sd) : x0, e = dual ? a + (x1 - x0) * 0.5f : x1;
            const float y = yOf(c.feed.rtaLevel(sd, b));
            const Rgba col = sd == 0 ? th_.teal : th_.gold;
            if (y < p.bottom() - 0.5f) r.fillRound({a, y, std::max(e - a - 0.4f * z, 0.6f * z), p.bottom() - y}, 0.0f, col.withAlpha(0.75f), Blend::Over);
            mx[sd][b] = 0.5f * (a + e);
            my[sd][b] = yOf(c.feed.rtaMax(sd, b));
        }
    }
    if (showMax)
        for (int sd = 0; sd < (dual ? 2 : 1); ++sd) r.polyline(mx[sd], my[sd], nb, 1.4f * z, (sd == 0 ? th_.label : th_.gold).withAlpha(0.8f), Blend::Over, false);
    const TextStyle s = c.ts(TextStyle::Caption);
    r.text(s, dual ? "LEFT" : "L+R", p.x + 14 * z, p.y + 26 * z, th_.teal, Align::Left, TextFx::Shadowed);
    if (dual) r.text(s, "RIGHT", p.x + 90 * z, p.y + 26 * z, th_.gold, Align::Left, TextFx::Shadowed);
}

// THE FORGER: the captured curve over a ±16 or ±30 dB grid.
void FaceView::cloneCurve(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const Rect plot = plotArea(w.r);
    const Rect p = c.T(plot);
    const float range = w.param >= 0 && plain(c.host, w.param) >= 0.5f ? 30.0f : 16.0f;
    grid(c, plot, range, -range, range > 20.0f ? 10.0f : 4.0f, true);
    if (!c.feed.hasClone()) return;
    float xs[rack::clone::kPoints], ys[rack::clone::kPoints];
    for (int i = 0; i < rack::clone::kPoints; ++i) {
        xs[i] = c.X(freqToX(plot, static_cast<float>(rack::clone::pointHz(i))));
        ys[i] = p.cy() - std::clamp(c.feed.cloneDb(i) / range, -1.0f, 1.0f) * p.h * 0.5f;
    }
    r.polyline(xs, ys, rack::clone::kPoints, 2.4f * c.z, th_.teal, Blend::Over, false);
}

// STAKEOUT: a half circle; straight up = the middle, the dashed lines at 45 degrees = hard left / right, red past
// 60 degrees = out of phase. Each band is a dot: its angle where it sits, its distance how loud (rings 20 dB apart).
void FaceView::polar(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect b = c.T(w.r);
    const float cx = b.cx(), cy = b.bottom() - 30 * z, rad = std::min(b.w * 0.5f - 20 * z, b.h - 60 * z);
    constexpr float kRange = 80.0f;   // dB from the rim to the centre
    const float d2r = 3.14159265f / 180.0f;
    // the out-of-phase zones
    for (int sd = -1; sd <= 1; sd += 2)
        for (int k = 0; k < 15; ++k) {
            const float a0 = (60.0f + 2.0f * static_cast<float>(k)) * d2r * static_cast<float>(sd), a1 = (62.0f + 2.0f * static_cast<float>(k)) * d2r * static_cast<float>(sd);
            const float qx[4] = {cx, cx + rad * std::sin(a0), cx + rad * std::sin(a1), cx}, qy[4] = {cy, cy - rad * std::cos(a0), cy - rad * std::cos(a1), cy};
            const Rgba red = th_.red.withAlpha(0.16f);
            const Rgba cols[4] = {red, red, red, red};
            r.colorQuad(qx, qy, cols, Blend::Over, false);
        }
    // the rings and the 0 / 45 degree lines
    for (int ring = 0; ring <= 3; ++ring) {
        const float rr = rad * (1.0f - 20.0f * static_cast<float>(ring) / kRange);
        float xs[61], ys[61];
        for (int k = 0; k <= 60; ++k) {
            const float a = (-90.0f + 3.0f * static_cast<float>(k)) * d2r;
            xs[k] = cx + rr * std::sin(a);
            ys[k] = cy - rr * std::cos(a);
        }
        r.polyline(xs, ys, 61, 1.0f * z, Rgba{1, 1, 1, ring == 0 ? 0.18f : 0.08f}, Blend::Over, false);
    }
    for (float deg : {-90.0f, -45.0f, 0.0f, 45.0f, 90.0f}) {
        const float lx[2] = {cx, cx + rad * std::sin(deg * d2r)}, ly[2] = {cy, cy - rad * std::cos(deg * d2r)};
        r.polyline(lx, ly, 2, 1.0f * z, Rgba{1, 1, 1, deg == 0.0f ? 0.16f : 0.08f}, Blend::Over, false);
    }
    const TextStyle s = c.ts(TextStyle::Caption);
    r.text(s, "L", cx - rad * 0.74f, cy - rad * 0.74f - 8 * z, th_.label, Align::Center, TextFx::Shadowed);
    r.text(s, "R", cx + rad * 0.74f, cy - rad * 0.74f - 8 * z, th_.label, Align::Center, TextFx::Shadowed);
    r.text(s, "MONO", cx, cy - rad - 10 * z, th_.label, Align::Center, TextFx::Shadowed);
    if (!c.feed.hasAnalyzer()) return;
    const int res = std::clamp(static_cast<int>(std::lround(plain(c.host, rack::stakeout::Resolution))), 0, 2);
    const int nb = rack::stakeout::bandCount(res);
    for (int k = 0; k < nb; ++k) {
        const float lv = c.feed.spdLevel(k);
        if (lv < -kRange) continue;
        const float rr = rad * std::clamp(1.0f + lv / kRange, 0.0f, 1.0f), a = std::clamp(c.feed.spdAngle(k), -90.0f, 90.0f) * d2r;
        const float px = cx + rr * std::sin(a), py = cy - rr * std::cos(a);
        const bool out = std::fabs(c.feed.spdAngle(k)) > 45.0f;
        r.fillRound({px - 3.0f * z, py - 3.0f * z, 6.0f * z, 6.0f * z}, 3.0f * z, (out ? th_.red : th_.teal).withAlpha(0.9f), Blend::Over);
    }
}

// HITMAN: the last few seconds, scrolling right to left — gain reduction of both channels hanging
// from the top (0 dB at the top, -24 at the bottom) over the input level (0 dBFS at the top, -60 at the bottom).
void FaceView::history(const Ctx& c, const Widget& w) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect b = c.T(w.r);
    const Rect p = b.inset(16 * z);
    // one new column per frame
    hist_[0][static_cast<std::size_t>(histPos_)] = std::clamp(c.feed.meterDb(0), -24.0f, 0.0f);
    hist_[1][static_cast<std::size_t>(histPos_)] = std::clamp(c.feed.meterDb(1), -24.0f, 0.0f);
    hist_[2][static_cast<std::size_t>(histPos_)] = std::clamp(c.feed.meterDb(2), -60.0f, 0.0f);
    histPos_ = (histPos_ + 1) % kHistory;
    histCount_ = std::min(histCount_ + 1, kHistory);
    const TextStyle ts = c.ts(TextStyle::Caption);
    constexpr float kGrid[] = {0.0f, -6.0f, -12.0f, -18.0f, -24.0f};
    for (float db : kGrid) {
        const float y = p.y + (-db / 24.0f) * p.h;
        const float xs[2] = {p.x, p.right()}, ys[2] = {y, y};
        r.polyline(xs, ys, 2, 1.0f * z, th_.silver.withAlpha(0.10f), Blend::Over, false);
    }
    const int n = histCount_;
    if (n >= 2) {
        float xs[kHistory], ys[kHistory];
        auto line = [&](int which, float lo, Rgba col, float width, bool fill) {
            for (int i = 0; i < n; ++i) {
                const int idx = (histPos_ - n + i + 2 * kHistory) % kHistory;
                xs[i] = p.right() - static_cast<float>(n - 1 - i) / static_cast<float>(kHistory - 1) * p.w;
                ys[i] = p.y + std::clamp(hist_[static_cast<std::size_t>(which)][static_cast<std::size_t>(idx)] / lo, 0.0f, 1.0f) * p.h;
            }
            if (fill)
                for (int i = 0; i + 1 < n; ++i) {
                    const float qx[4] = {xs[i], xs[i + 1], xs[i + 1], xs[i]}, qy[4] = {ys[i], ys[i + 1], p.bottom(), p.bottom()};
                    const Rgba a = col.withAlpha(0.22f), bb = col.withAlpha(0.03f);
                    const Rgba cols[4] = {a, a, bb, bb};
                    r.colorQuad(qx, qy, cols, Blend::Over, false);
                }
            r.polyline(xs, ys, n, width * z, col, Blend::Over, false);
        };
        // input level: from the bottom up (louder = higher)
        for (int i = 0; i < n; ++i) {
            const int idx = (histPos_ - n + i + 2 * kHistory) % kHistory;
            xs[i] = p.right() - static_cast<float>(n - 1 - i) / static_cast<float>(kHistory - 1) * p.w;
            ys[i] = p.y + std::clamp(hist_[2][static_cast<std::size_t>(idx)] / -60.0f, 0.0f, 1.0f) * p.h;
        }
        for (int i = 0; i + 1 < n; ++i) {
            const float qx[4] = {xs[i], xs[i + 1], xs[i + 1], xs[i]}, qy[4] = {ys[i], ys[i + 1], p.bottom(), p.bottom()};
            const Rgba a = th_.silver.withAlpha(0.18f), bb = th_.silver.withAlpha(0.02f);
            const Rgba cols[4] = {a, a, bb, bb};
            r.colorQuad(qx, qy, cols, Blend::Over, false);
        }
        r.polyline(xs, ys, n, 1.4f * z, th_.silver.withAlpha(0.6f), Blend::Over, false);
        line(1, -24.0f, th_.gold, 2.0f, false);
        line(0, -24.0f, th_.red, 2.4f, false);
    }
    // the dB numbers (gain reduction scale) on top of the traces, on a dark chip so a loud input
    // line can't hide them
    for (float db : kGrid) {
        const float y = p.y + (-db / 24.0f) * p.h;
        char t[8];
        std::snprintf(t, sizeof t, "%.0f", static_cast<double>(db));
        const float base = y + (db == 0.0f ? r.capHeight(ts) + 4 * z : -4 * z);
        const float tw = r.textWidth(ts, t);
        r.fillRound({p.x + 1 * z, base - r.capHeight(ts) - 3 * z, tw + 7 * z, r.capHeight(ts) + 6 * z}, 3 * z, rgb8(0x0A0E12, 0.78f), Blend::Over);
        r.text(ts, t, p.x + 4 * z, base, th_.dim, Align::Left, TextFx::Shadowed);
    }
    float lx = p.right() - 8 * z;
    auto legend = [&](const char* t, Rgba col) {
        const float tw = r.textWidth(ts, t);
        r.text(ts, t, lx, p.bottom() - 6 * z, col, Align::Right, TextFx::Shadowed);
        lx -= tw + 26 * z;
    };
    legend("INPUT", th_.silver);
    legend(w.hi.empty() ? "GR 2" : w.hi.c_str(), th_.gold);
    legend(w.lo.empty() ? "GR 1" : w.lo.c_str(), th_.red);
}

// Compressors: a gain-reduction needle with a console-meter scale (0 on the right, -20 on the left).
// The same needle reading a VU level (-20 .. +3), when the plate's meter selector says IN or OUT.
void FaceView::vuMeter(const Ctx& c, const Widget& w, int meter, const char* caption) const {
    Renderer& r = c.r;
    const float z = c.z;
    const Rect d = c.T(w.r);
    const float target = std::clamp(c.feed.meterDb(meter), -20.0f, 3.0f);
    needle_ += (target - needle_) * 0.35f;
    const TextStyle ls = c.ts(TextStyle::Caption), ts = c.ts(TextStyle::Caption), big = c.ts(TextStyle::Panel);
    const float px = d.cx(), py = d.bottom() - 34 * z, bigBaseline = d.y + 52 * z;
    const float R = std::min(d.w * 0.42f, d.h * 0.78f);
    auto angle = [](float db) {   // the VU scale: even in voltage, -20 at -50 degrees, +3 at +50
        const float v = std::pow(10.0f, std::clamp(db, -20.0f, 3.0f) / 20.0f), lo = 0.1f, hi = std::pow(10.0f, 3.0f / 20.0f);
        return (-50.0f + 100.0f * (v - lo) / (hi - lo)) * kPi / 180.0f;
    };
    r.arc(px, py, R, 3.0f * z, angle(-20.0f), angle(0.0f), th_.teal.withAlpha(0.35f), false);
    r.arc(px, py, R, 5.0f * z, angle(0.0f), angle(3.0f), rgb8(0xD04040).withAlpha(0.85f), false);
    constexpr float kMarks[] = {-20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f};
    const float capH = r.capHeight(ts);
    float lastRight = -1e9f;
    for (float m : kMarks) {
        const float a = angle(m), sx = std::sin(a), sy = -std::cos(a);
        const float xs[2] = {px + sx * (R - 14 * z), px + sx * (R + 8 * z)}, ys[2] = {py + sy * (R - 14 * z), py + sy * (R + 8 * z)};
        r.polyline(xs, ys, 2, 2.2f * z, th_.silver.withAlpha(0.8f), Blend::Over, false);
        char t[8];
        std::snprintf(t, sizeof t, m > 0 ? "+%.0f" : "%.0f", static_cast<double>(m));
        const float tw = r.textWidth(ts, t), cx = px + sx * (R + 30 * z), cy = py + sy * (R + 30 * z);
        if (cx - tw * 0.5f < lastRight + 4 * z) continue;   // no number touches the one before it
        lastRight = cx + tw * 0.5f;
        r.text(ts, t, cx, cy + capH * 0.5f, m > 0 ? rgb8(0xE06060) : th_.label, Align::Center, TextFx::Shadowed);
    }
    const float a = angle(needle_);
    const float nx[2] = {px, px + std::sin(a) * R * 0.97f}, ny[2] = {py, py - std::cos(a) * R * 0.97f};
    r.polyline(nx, ny, 2, 2.6f * z, th_.silver, Blend::Over, false);
    r.fillRound({px - 13 * z, py - 13 * z, 26 * z, 26 * z}, 13 * z, rgb8(0x9AA4AA), Blend::Over);
    r.fillRound({px - 6 * z, py - 6 * z, 12 * z, 12 * z}, 6 * z, th_.tealDeep, Blend::Over, false);
    r.text(ls, caption, d.x + 20 * z, d.bottom() - 18 * z, th_.label, Align::Left, TextFx::Shadowed);
    char b[32];
    std::snprintf(b, sizeof b, "%+.1f", static_cast<double>(target));
    r.text(big, b, d.right() - 24 * z, bigBaseline, th_.teal, Align::Right, TextFx::Shadowed);
}

void FaceView::grMeter(const Ctx& c, const Widget& w) const {
    if (w.param2 >= 0 && w.meter >= 0) {   // a meter selector: 0 IN, 1 GR, 2 OUT (or, with two choices, IN / OUT)
        const bool twoWay = static_cast<std::size_t>(w.param2) < params_.size() && params_[static_cast<std::size_t>(w.param2)].max < 1.5f;
        const int raw = static_cast<int>(std::lround(plain(c.host, w.param2)));
        const int sel = twoWay ? (raw >= 1 ? 2 : 0) : std::clamp(raw, 0, 2);
        if (sel != 1) {
            vuMeter(c, w, w.meter + (sel == 2 ? 1 : 0), sel == 0 ? "INPUT  VU" : "OUTPUT  VU");
            return;
        }
        if (needle_ > 0.0f) needle_ = 0.0f;   // back from a VU reading: start from rest
    }
    Renderer& r = c.r;
    const float z = c.z;
    const Rect d = c.T(w.r);
    const float target = std::clamp(w.choice >= 0 ? c.feed.meterDb(w.choice) : c.feed.reductionDb(), -20.0f, 0.0f);
    float& needle_ = w.choice >= 0 ? needles_[static_cast<std::size_t>(std::clamp(w.choice, 0, 3))] : this->needle_;
    if (needle_ > 0.5f) needle_ = target;    // the first frame shows where it is
    needle_ += (target - needle_) * 0.35f;   // then it swings like a real needle, ~0.1 s
    if (std::fabs(needle_) < 0.01f) needle_ = 0.0f;
    // A small meter (HITMAN's pair) gets a smaller number in a band of its own at the top, and its pivot
    // lifted clear of the caption when the caption reaches the middle; the scale shrinks to fit under
    // the number. Big meters keep the layout they always had.
    const TextStyle ls = c.ts(TextStyle::Caption);
    const std::string caption = w.caption.empty() ? std::string("GAIN REDUCTION  dB") : w.caption;
    const bool small = w.r.h < 260.0f;
    const float px = d.cx();
    const bool captionUnderPivot = d.x + 20 * z + r.textWidth(ls, caption) > px - 16 * z;
    const float py = small && captionUnderPivot ? d.bottom() - 18 * z - r.capHeight(ls) - 8 * z - 13 * z : d.bottom() - 34 * z;
    const TextStyle ts = c.ts(TextStyle::Caption);
    const TextStyle big = c.ts(small ? TextStyle::Title : TextStyle::Panel);
    const float bigBaseline = d.y + (small ? 40 : 52) * z;
    float R = std::min(d.w * 0.42f, d.h * 0.78f);
    if (small) R = std::max(30.0f * z, std::min(R, py - (bigBaseline + 8 * z) - 30 * z - r.capHeight(ts) * 0.5f - 2 * z));
    auto angle = [](float db) {   // 0 dB at +50°, -20 dB at -50° (more room for the first few dB)
        const float t = std::pow(std::clamp(-db / 20.0f, 0.0f, 1.0f), 0.6f);
        return (50.0f - 100.0f * t) * kPi / 180.0f;
    };
    // the scale: a lit arc, ticks and numbers
    r.arc(px, py, R, 3.0f * z, angle(-20.0f), angle(0.0f), th_.teal.withAlpha(0.35f), false);
    r.arc(px, py, R, 5.0f * z, angle(-3.0f), angle(0.0f), th_.teal.withAlpha(0.75f), false);
    // Every tick is drawn; a number is left out when it would touch the one before it (20 and 0 always stay).
    constexpr float kMarks[] = {0.0f, -1.0f, -2.0f, -3.0f, -4.0f, -6.0f, -8.0f, -10.0f, -14.0f, -20.0f};
    constexpr int kN = static_cast<int>(std::size(kMarks));
    Rect boxes[kN];
    char texts[kN][8];
    bool show[kN];
    const float capH = r.capHeight(ts);
    for (int i = 0; i < kN; ++i) {
        const float a = angle(kMarks[i]), sx = std::sin(a), sy = -std::cos(a);
        const float xs[2] = {px + sx * (R - 14 * z), px + sx * (R + 8 * z)}, ys[2] = {py + sy * (R - 14 * z), py + sy * (R + 8 * z)};
        r.polyline(xs, ys, 2, 2.2f * z, th_.silver.withAlpha(0.8f), Blend::Over, false);
        std::snprintf(texts[i], sizeof texts[i], "%.0f", static_cast<double>(std::fabs(kMarks[i])));
        const float tw = r.textWidth(ts, texts[i]), cx = px + sx * (R + 30 * z), cy = py + sy * (R + 30 * z);
        boxes[i] = {cx - tw * 0.5f - 3 * z, cy - capH * 0.5f - 2 * z, tw + 6 * z, capH + 4 * z};
        show[i] = true;
    }
    auto touch = [](const Rect& p, const Rect& q) { return p.x < q.right() && q.x < p.right() && p.y < q.bottom() && q.y < p.bottom(); };
    int last = 0;
    for (int i = 1; i < kN - 1; ++i) {
        if (touch(boxes[last], boxes[i])) show[i] = false;
        else last = i;
    }
    for (int i = kN - 2; i > 0; --i)   // 20 stays: anything in its way goes instead
        if (show[i] && touch(boxes[i], boxes[kN - 1])) show[i] = false;
    for (int i = 0; i < kN; ++i)
        if (show[i]) r.text(ts, texts[i], boxes[i].cx(), boxes[i].cy() + capH * 0.5f, th_.label, Align::Center, TextFx::Shadowed);
    // the needle and its pivot
    const float a = angle(needle_);
    const float nx[2] = {px, px + std::sin(a) * R * 0.97f}, ny[2] = {py, py - std::cos(a) * R * 0.97f};
    r.polyline(nx, ny, 2, 2.6f * z, th_.silver, Blend::Over, false);
    r.fillRound({px - 13 * z, py - 13 * z, 26 * z, 26 * z}, 13 * z, rgb8(0x9AA4AA), Blend::Over);
    r.fillRound({px - 6 * z, py - 6 * z, 12 * z, 12 * z}, 6 * z, th_.tealDeep, Blend::Over, false);
    // labels and the number
    r.text(ls, caption, d.x + 20 * z, d.bottom() - 18 * z, th_.label, Align::Left, TextFx::Shadowed);
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", static_cast<double>(std::fabs(target) < 0.05f ? 0.0f : target));
    r.text(big, b, d.right() - 24 * z, bigBaseline, th_.teal, Align::Right, TextFx::Shadowed);
}

void FaceView::draw(Renderer& r, const PanelHost& host, const FaceFeed& feed, const FaceState& st, float ox, float oy, float zoom,
                    bool plate) const {
    const Ctx c{r, host, feed, st, ox, oy, zoom};
    if (plate) r.panel({ox, oy, kPlateW * zoom, kPlateH * zoom});
    header(c);
    for (std::size_t i = 0; i < L_.widgets.size(); ++i) {
        const Widget& w = L_.widgets[i];
        if (!widgetShown(w, params_, host)) continue;
        const int wi = static_cast<int>(i);
        const bool hot = st.hover == wi;
        switch (w.kind) {
        case WKind::Knob: knob(c, w, wi); break;
        case WKind::Toggle: {
            const int param = widgetParam(w, st);
            const bool on = plain(host, param) >= 0.5f;
            std::string t = w.caption;
            if (w.control == rack::eq::On) t = "BAND " + std::to_string(std::clamp(st.selectedBand, 0, rack::eq::kBands - 1) + 1);
            if (w.hi == "PRESS") {   // a push button (any click does it): no ON / OFF state to show
                button(c, c.T(w.r), t, false, hot, false, th_.teal);
                break;
            }
            t += t.empty() ? (on ? "ON" : "OFF") : (on ? "  ON" : "  OFF");   // a grid row's switch: the row says what it is
            button(c, c.T(w.r), t, on, hot, !host.active(param), th_.teal);
            break;
        }
        case WKind::ChoiceButton: {
            const hc::fx::ParamInfo& pi = params_[static_cast<std::size_t>(w.param)];
            const bool lit = static_cast<int>(std::lround(plain(host, w.param))) == w.choice;
            button(c, c.T(w.r), pi.choices ? pi.choices[w.choice] : "", lit, hot, false, th_.teal);
            if (!w.caption.empty()) {
                const TextStyle cs = c.ts(TextStyle::Label);
                r.text(cs, w.caption, c.X(w.r.x + 2), c.Y(w.r.y - 14), th_.label, Align::Left, TextFx::Shadowed);
            }
            break;
        }
        case WKind::Stepper: stepper(c, w, wi); break;
        case WKind::RangeSlider: rangeSlider(c, w, wi); break;
        case WKind::Bypass: bypass(c, w, wi); break;
        case WKind::Spectrum: spectrum(c, w); break;
        case WKind::EqGraph: eqGraph(c, w); break;
        case WKind::CurveDisplay: consoleCurve(c, w); break;
        case WKind::GrMeter: grMeter(c, w); break;
        case WKind::VuMeter: vuMeter(c, w); break;
        case WKind::Readout: readout(c, w); break;
        case WKind::History: history(c, w); break;
        case WKind::Rta: rta(c, w); break;
        case WKind::Polar: polar(c, w); break;
        case WKind::CloneCurve: cloneCurve(c, w); break;
        case WKind::Keyboard: keyboard(c, w); break;
        case WKind::GraphRange: {
            const std::string t = fmt("%.0f dB", kGraphRanges[static_cast<std::size_t>(w.choice)]);
            button(c, c.T(w.r), t, st.graphRange == w.choice, hot, false, th_.teal);
            break;
        }
        case WKind::PageTab: button(c, c.T(w.r), w.caption, st.page == w.choice, hot, false, th_.teal); break;
        case WKind::BandButton: {
            using namespace rack::eq;
            const bool on = plain(host, bandParam(w.choice, On)) >= 0.5f;
            const Rgba col = bandColour(w.choice);
            const Rect b = c.T(w.r);
            button(c, b, std::to_string(w.choice + 1), on, hot, false, col);
            if (st.selectedBand == w.choice) {
                // the selected band: a solid silver bar under it, no halo
                r.fillRound({b.x + 6 * zoom, b.bottom() - 4 * zoom, b.w - 12 * zoom, 3 * zoom}, 1.5f * zoom, th_.silver, Blend::Over, false);
            }
            break;
        }
        }
    }
}

} // namespace hem::ui
