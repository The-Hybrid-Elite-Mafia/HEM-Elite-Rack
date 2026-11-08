// Where the words on a faceplate go, without a GPU or a font: each knob's caption (above the dial) and
// value (under it), the notes under the plug-in's big name and the section names, as boxes in plate
// units. The sizes are the plate's own text sizes at zoom 1 (cap height, average letter width),
// measured from the CI screenshots, with the same "shrink to fit" widths FaceView uses. The faceplate
// test checks that no box runs into a control or into another box, which the control-only overlap
// check could not see (0.15.0: HITMAN's notes ran through L/R - M/S, channel 2's captions sat on
// channel 1's values).
#pragma once

#include "hem/ui/Face.hpp"
#include "FontMetrics.hpp"
#include "hybrid/fx/Fx.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <cstdio>
#include <string>
#include <vector>

namespace hem::test::textroom {

struct Box {
    hc::ui::Rect r;
    std::string what;
    int owner = -1;   // the widget the text belongs to (-1: plate text)
};

// The plate's text the way FaceView sizes it: a style from the ladder for the zoom (the largest one not
// bigger than the base style at that zoom), stepped down until the words fit their width, and drawn with the
// fonts' real letter widths (FontMetrics.hpp). Two zooms are checked, and the wider result counts: the VST3
// window draws the plate at zoom 1, the rack app at 0.933 (faceArea 1560 x 968), where the styles can't step
// down any further and the words come out larger next to the plate. Before 0.52.1 this file guessed letter
// widths from the CI screenshots, which only show zoom 1; on Steven's screen (the rack app) BODY DOUBLE,
// STREET TEAM, INSIDE MAN and SIX SHOOTER ran their words together though this test passed.
enum class St { Caption, Value, Label, Title, Tab, Panel, Heading, Display };
struct StSpec { float em; bool bold; };
inline constexpr StSpec kSt[] = {{24, false}, {28, false}, {30, false}, {34, false}, {40, false}, {52, true}, {56, true}, {96, true}};
inline constexpr float kZooms[] = {1.0f, 0.9330144f};
inline constexpr float kPad = 3.0f;
inline constexpr float kPlateEdge = 816.0f;   // the main plate's lower edge in the pictures (the footer strip starts at 832)   // plate units of clear metal each word wants on either side

inline float textAdvance(const std::string& s, bool bold) {
    float a = 0;
    for (char ch : s) {
        const int i = static_cast<unsigned char>(ch) - 32;
        a += (i >= 0 && i < 95) ? (bold ? kBoldAdv[i] : kSemiBoldAdv[i]) : 0.6f;
    }
    return a;
}
// width in design units of `s` in style `t`
inline float designW(St t, const std::string& s) { return kSt[static_cast<int>(t)].em * textAdvance(s, kSt[static_cast<int>(t)].bold); }
inline St styleAt(St base, float z) {
    const float want = kSt[static_cast<int>(base)].em * z * 1.04f;
    St best = St::Caption;
    for (int i = 0; i < 8; ++i) if (kSt[i].em <= want) best = static_cast<St>(i);
    return best;
}
inline St fitAt(St base, const std::string& s, float maxWpu, float z) {
    St t = styleAt(base, z);
    while (t != St::Caption && designW(t, s) > maxWpu * z) t = static_cast<St>(static_cast<int>(t) - 1);
    return t;
}
// the word's width and cap height in plate units, the larger of the two zooms
struct Ext { float w, cap; };
inline Ext extent(St base, const std::string& s, float maxWpu) {
    Ext e{0, 0};
    for (float z : kZooms) {
        const St t = fitAt(base, s, maxWpu, z);
        e.w = std::max(e.w, designW(t, s) / z);
        e.cap = std::max(e.cap, kSt[static_cast<int>(t)].em * kSemiBoldCap / z);
    }
    return e;
}
inline hc::ui::Rect centred(const std::string& t, float cx, float baseline, St base, float maxW) {
    const Ext e = extent(base, t, maxW);
    return {cx - e.w * 0.5f - kPad, baseline - e.cap, e.w + 2 * kPad, e.cap + 2.0f};
}
inline hc::ui::Rect leftAligned(const std::string& t, float x, float baseline, St base, float maxW) {
    const Ext e = extent(base, t, maxW);
    return {x - kPad, baseline - e.cap, e.w + 2 * kPad, e.cap + 2.0f};
}

// What a knob shows under it, the way FaceView writes it (".00 deg" / ".00 cents" and ".0 dB" past 10 dropped).
inline std::string shown(std::string v) {
    for (const char* unit : {" deg", " cents"}) {
        const std::size_t u = std::char_traits<char>::length(unit);
        if (v.size() >= 3 + u && v.compare(v.size() - u, u, unit) == 0 && v.compare(v.size() - u - 3, 3, ".00") == 0) v.erase(v.size() - u - 3, 3);
    }
    return hem::ui::shortDb(v);
}
// The widest thing a knob can show: its value at the ends, the default and nine steps between.
inline std::string widestValue(const hc::fx::ParamInfo& p, bool tight) {
    std::string best;
    float bw = -1;
    auto take = [&](float plain) {
        std::string s = shown(hc::fx::formatValue(p, plain));
        if (tight) s = hem::ui::tightValue(s);
        const float w = textAdvance(s, false);
        if (w > bw) { bw = w; best = s; }
    };
    take(p.min); take(p.max); take(p.def);
    for (int i = 1; i < 10; ++i) take(hc::fx::toPlain(p, static_cast<float>(i) / 10.0f));
    return best;
}

inline std::vector<Box> textBoxes(const hem::ui::FaceLayout& L, std::span<const hc::fx::ParamInfo> ps) {
    using hem::ui::WKind;
    std::vector<Box> out;
    for (std::size_t i = 0; i < L.widgets.size(); ++i) {
        const hem::ui::Widget& w = L.widgets[i];
        if (w.kind != WKind::Knob) continue;
        const float cx = w.r.cx();
        if (!w.caption.empty())
            out.push_back({centred(w.caption, cx, w.r.y - (w.small ? 12.0f : 16.0f), w.small ? St::Caption : St::Label, w.r.w + (w.small ? 26.0f : 70.0f)),
                           w.caption + " (caption)", static_cast<int>(i)});
        const std::string v = (w.param >= 0 && w.param < static_cast<int>(ps.size())) ? widestValue(ps[static_cast<std::size_t>(w.param)], w.tight) : "+14.0 dB";
        out.push_back({centred(v, cx, w.r.bottom() + (w.small ? 26.0f : 32.0f), w.small ? St::Value : St::Title, w.r.w + (w.small ? 26.0f : 50.0f)),
                       w.caption + " (value '" + v + "')", static_cast<int>(i)});
    }
    float y = L.title.bottom() + 30.0f;
    for (const std::string& n : L.leftNotes) {
        out.push_back({leftAligned(n, L.title.x + 2.0f, y, St::Label, L.title.w), "note '" + n + "'", -1});
        y += 36.0f;
    }
    for (const auto& [rect, text] : L.sections) {
        const Ext e = extent(St::Label, text, rect.w - 10.0f);
        out.push_back({centred(text, rect.cx(), rect.cy() + e.cap * 0.5f, St::Label, rect.w - 10.0f), "section '" + text + "'", -1});
    }
    return out;
}

inline bool hits(const hc::ui::Rect& a, const hc::ui::Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}
// a groove (a lit line in the metal) through the words
inline bool crosses(const hc::ui::Rect& a, const std::array<float, 4>& g) {
    const float x0 = std::min(g[0], g[2]), x1 = std::max(g[0], g[2]), y0 = std::min(g[1], g[3]), y1 = std::max(g[1], g[3]);
    return hits(a, {x0 - 1.0f, y0 - 1.0f, x1 - x0 + 2.0f, y1 - y0 + 2.0f});
}

// Number of clashes; each one is printed.
inline int clashes(const hem::ui::FaceLayout& L, std::span<const hc::fx::ParamInfo> ps, const char* name) {
    using hem::ui::WKind;
    const std::vector<Box> boxes = textBoxes(L, ps);
    int n = 0;
    for (std::size_t a = 0; a < boxes.size(); ++a) {
        for (std::size_t b = a + 1; b < boxes.size(); ++b)
            if (hits(boxes[a].r, boxes[b].r)) {
                if (boxes[a].owner >= 0 && boxes[b].owner >= 0 &&
                    hem::ui::exclusive(L.widgets[static_cast<std::size_t>(boxes[a].owner)], L.widgets[static_cast<std::size_t>(boxes[b].owner)]))
                    continue;   // never on the plate together
                ++n;
                std::printf("  %s: text %s runs into %s\n", name, boxes[a].what.c_str(), boxes[b].what.c_str());
            }
        for (std::size_t i = 0; i < L.widgets.size(); ++i) {
            const hem::ui::Widget& w = L.widgets[i];
            if (static_cast<int>(i) == boxes[a].owner) continue;
            if (boxes[a].owner >= 0 && hem::ui::exclusive(L.widgets[static_cast<std::size_t>(boxes[a].owner)], w)) continue;
            // text drawn inside a graph is the graph's own business
            if (w.kind == WKind::Spectrum || w.kind == WKind::EqGraph || w.kind == WKind::GraphRange) continue;
            if (hits(boxes[a].r, w.r)) {
                ++n;
                std::printf("  %s: text %s runs into control '%s'\n", name, boxes[a].what.c_str(), w.caption.c_str());
            }
        }
        if (boxes[a].r.bottom() > kPlateEdge) {   // past the main plate's lower edge, onto its frame
            ++n;
            std::printf("  %s: text %s runs off the plate's edge\n", name, boxes[a].what.c_str());
        }
        for (const auto& g : L.grooves)
            if (crosses(boxes[a].r, g)) {
                ++n;
                std::printf("  %s: text %s sits on a groove\n", name, boxes[a].what.c_str());
            }
    }
    return n;
}

// Width of a line at the plate's smallest text size, letter by letter (measured from the screenshots:
// wide capitals ~12.5, digits ~8.5, I / space / punctuation ~5, lower case ~8).
inline float captionWidth(const std::string& t) {
    float w = 0;
    for (char ch : t) {
        if (ch == 'M' || ch == 'W') w += 15.0f;
        else if (ch == 'L' || ch == 'T' || ch == 'F' || ch == 'E') w += 10.5f;
        else if (ch == 'I' || ch == ' ' || ch == '.' || ch == ',' || ch == ':' || ch == '/' || ch == '-' || ch == '|') w += 5.0f;
        else if (ch >= '0' && ch <= '9') w += 8.5f;
        else if (ch >= 'a' && ch <= 'z') w += 8.0f;
        else w += 12.5f;
    }
    return w;
}

// Every button's words fit inside it, even at the smallest text size the plate shrinks to
// (choice buttons: the choice's name; switches: the caption + "  OFF").
inline int crampedButtons(const hem::ui::FaceLayout& L, std::span<const hc::fx::ParamInfo> ps, const char* name) {
    using hem::ui::WKind;
    int n = 0;
    for (const hem::ui::Widget& w : L.widgets) {
        std::string t;
        // choice buttons: measured on the 0.18.0 screenshots, their words come out ~30 % wider than the
        // estimate and may run to 3 units from each edge; switches keep the original margin
        float scale = 1.0f, margin = 16.0f;
        if (w.kind == WKind::ChoiceButton && w.param >= 0 && w.param < static_cast<int>(ps.size()) && ps[static_cast<std::size_t>(w.param)].choices) {
            t = ps[static_cast<std::size_t>(w.param)].choices[w.choice];
            scale = 1.3f;
            margin = 6.0f;
        } else if (w.kind == WKind::Toggle && w.hi != "PRESS") {
            t = w.caption + "  OFF";
        } else {
            continue;
        }
        const float need = captionWidth(t) * scale;
        if (need > w.r.w - margin) {
            ++n;
            std::printf("  %s: the words '%s' don't fit their %.0f-wide button\n", name, t.c_str(), static_cast<double>(w.r.w));
        }
    }
    return n;
}

} // namespace hem::test::textroom
