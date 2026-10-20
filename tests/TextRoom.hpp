// Where the words on a faceplate go, without a GPU or a font: each knob's caption (above the dial) and
// value (under it), the notes under the plug-in's big name and the section names, as boxes in plate
// units. The sizes are the plate's own text sizes at zoom 1 (cap height, average letter width),
// measured from the CI screenshots, with the same "shrink to fit" widths FaceView uses. The faceplate
// test checks that no box runs into a control or into another box, which the control-only overlap
// check could not see (0.15.0: HITMAN's notes ran through L/R - M/S, channel 2's captions sat on
// channel 1's values).
#pragma once

#include "hem/ui/Face.hpp"

#include <algorithm>
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

// cap height / average letter width at zoom 1, plate units
struct Size { float cap, letter; };
inline constexpr Size kCaption{14.0f, 10.4f};
inline constexpr Size kLabel{18.0f, 13.0f};
inline constexpr Size kValue{18.0f, 10.5f};
inline constexpr Size kTitle{24.0f, 14.0f};

// A line of text centred on cx with its baseline at y; never wider than maxW (FaceView shrinks it).
inline hc::ui::Rect centred(const std::string& t, float cx, float baseline, Size s, float maxW) {
    const float w = std::min(static_cast<float>(t.size()) * s.letter, maxW);
    return {cx - w * 0.5f, baseline - s.cap, w, s.cap + 2.0f};
}
inline hc::ui::Rect leftAligned(const std::string& t, float x, float baseline, Size s, float maxW) {
    const float w = std::min(static_cast<float>(t.size()) * s.letter, maxW);
    return {x, baseline - s.cap, w, s.cap + 2.0f};
}

// A knob's value text is as long as its longest value; "+14.0 dB" is the long case on the plates.
inline std::vector<Box> textBoxes(const hem::ui::FaceLayout& L) {
    using hem::ui::WKind;
    std::vector<Box> out;
    for (std::size_t i = 0; i < L.widgets.size(); ++i) {
        const hem::ui::Widget& w = L.widgets[i];
        if (w.kind != WKind::Knob) continue;
        const float cx = w.r.cx();
        if (!w.caption.empty()) {
            const Size s = w.small ? kCaption : kLabel;
            out.push_back({centred(w.caption, cx, w.r.y - (w.small ? 12.0f : 16.0f), s, w.r.w + (w.small ? 26.0f : 70.0f)),
                           w.caption + " (caption)", static_cast<int>(i)});
        }
        const Size vs = w.small ? kValue : kTitle;
        out.push_back({centred("+14.0 dB", cx, w.r.bottom() + (w.small ? 26.0f : 32.0f), vs, w.r.w + (w.small ? 26.0f : 50.0f)),
                       w.caption + " (value)", static_cast<int>(i)});
    }
    float y = L.title.bottom() + 30.0f;
    for (const std::string& n : L.leftNotes) {
        out.push_back({leftAligned(n, L.title.x + 2.0f, y, kLabel, L.title.w), "note '" + n + "'", -1});
        y += 36.0f;
    }
    for (const auto& [rect, text] : L.sections)
        out.push_back({centred(text, rect.cx(), rect.cy() + kLabel.cap * 0.5f, kLabel, rect.w - 10.0f), "section '" + text + "'", -1});
    return out;
}

inline bool hits(const hc::ui::Rect& a, const hc::ui::Rect& b) {
    return a.x < b.right() - 0.5f && b.x < a.right() - 0.5f && a.y < b.bottom() - 0.5f && b.y < a.bottom() - 0.5f;
}

// Number of clashes; each one is printed.
inline int clashes(const hem::ui::FaceLayout& L, const char* name) {
    using hem::ui::WKind;
    const std::vector<Box> boxes = textBoxes(L);
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
