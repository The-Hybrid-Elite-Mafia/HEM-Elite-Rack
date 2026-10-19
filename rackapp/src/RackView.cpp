#include "RackView.hpp"

#include "hem/ui/Face.hpp"
#include "hybrid/ui/Parts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace hem::app {

using hc::ui::Align;
using hc::ui::Blend;
using hc::ui::Renderer;
using hc::ui::Rgba;
using hc::ui::Sprite;
using hc::ui::TextFx;
using hc::ui::TextStyle;
using hc::ui::rgb8;

namespace {

const hem::ui::Theme kTh;
const Rgba kBg0   = rgb8(0x0A0D0F);
const Rgba kPanel = rgb8(0x151A1D);
const Rgba kEdge  = rgb8(0x252D32);
const Rgba kGlass = rgb8(0x040607, 0.92f);
const Rgba kInk   = rgb8(0x061012);
constexpr float kPi = std::numbers::pi_v<float>;

// Layout (design units).
constexpr Rect kTop{0, 0, RackView::kW, 86};
constexpr Rect kOpen{980, 18, 220, 52}, kExport{1214, 18, 220, 52}, kDevice{1448, 18, 516, 52};
constexpr Rect kTransport{16, 96, 1968, 150};
constexpr Rect kPlay{36, 116, 110, 110}, kStop{160, 128, 86, 86};
constexpr Rect kSongLine{268, 108, 912, 34}, kOverview{268, 150, 912, 76};
constexpr Rect kA{1196, 116, 204, 52}, kB{1196, 174, 204, 52};
constexpr Rect kMatch{1410, 116, 236, 52}, kLoop{1410, 174, 236, 52};
constexpr Rect kMeter{1656, 112, 312, 118};
constexpr Rect kChain{16, 262, 388, 950};
constexpr float kRow0 = 330, kRowPitch = 98, kRowH = 90;
constexpr Rect kAdd{32, 1124, 356, 72};
constexpr Rect kStatus{0, 1218, RackView::kW, 32};

Rect slotRow(int i) { return {32, kRow0 + static_cast<float>(i) * kRowPitch, 356, kRowH}; }
Rect slotOn(int i) { const Rect r = slotRow(i); return {r.x + 206, r.y + 8, 72, 34}; }
Rect slotDel(int i) { const Rect r = slotRow(i); return {r.x + 282, r.y + 8, 72, 34}; }
Rect slotUp(int i) { const Rect r = slotRow(i); return {r.x + 206, r.y + 48, 72, 34}; }
Rect slotDown(int i) { const Rect r = slotRow(i); return {r.x + 282, r.y + 48, 72, 34}; }

Rect pickerPanel() { return RackView::faceArea().inset(80, 40); }
// Two columns of cards, top to bottom then left to right.
Rect pickerItem(int i) {
    const Rect p = pickerPanel();
    const float w = (p.w - 100) * 0.5f;
    return {p.x + 40 + static_cast<float>(i / 5) * (w + 20), p.y + 110 + static_cast<float>(i % 5) * 136, w, 124};
}
Rect pickerClose() { const Rect p = pickerPanel(); return {p.right() - 200, p.y + 30, 160, 50}; }

float mid(const Renderer& r, Rect a, TextStyle s) { return a.cy() + r.capHeight(s) * 0.5f; }

TextStyle fit(const Renderer& r, TextStyle base, std::string_view t, float width) {
    constexpr TextStyle ladder[] = {TextStyle::Caption, TextStyle::Value, TextStyle::Label, TextStyle::Title, TextStyle::Tab, TextStyle::Panel, TextStyle::Heading};
    int i = 0;
    for (int k = 0; k < 7; ++k) if (ladder[k] == base) i = k;
    while (i > 0 && r.textWidth(ladder[i], t) > width) --i;
    return ladder[i];
}

// The text itself, shortened with "..." when even the smallest style is too wide.
std::string clip(const Renderer& r, TextStyle s, std::string t, float width) {
    if (r.textWidth(s, t) <= width) return t;
    while (t.size() > 1 && r.textWidth(s, t + "...") > width) t.pop_back();
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t + "...";
}

std::string clock(double s) {
    if (s < 0) s = 0;
    const int m = static_cast<int>(s / 60.0);
    char b[32];
    std::snprintf(b, sizeof b, "%d:%04.1f", m, s - m * 60.0);
    return b;
}

} // namespace

void RackView::facePlacement(float& ox, float& oy, float& zoom) noexcept {
    const Rect a = faceArea();
    zoom = std::min(a.w / hem::ui::kPlateW, a.h / hem::ui::kPlateH);
    ox = a.x + (a.w - hem::ui::kPlateW * zoom) * 0.5f;
    oy = a.y + (a.h - hem::ui::kPlateH * zoom) * 0.5f;
}

Hit RackView::hit(float x, float y, const RackViewState& s) const noexcept {
    if (s.picker) {
        if (pickerClose().contains(x, y)) return {Action::PickerClose};
        for (int i = 0; i < static_cast<int>(s.pickerItems.size()); ++i)
            if (pickerItem(i).contains(x, y)) return {Action::AddPlugin, i};
        if (faceArea().contains(x, y)) return {Action::None};
    }
    if (kOpen.contains(x, y)) return {Action::OpenSong};
    if (kExport.contains(x, y)) return {Action::Export};
    if (kDevice.contains(x, y)) return {Action::Device};
    {
        const float dx = x - kPlay.cx(), dy = y - kPlay.cy();
        if (dx * dx + dy * dy <= kPlay.w * kPlay.w * 0.25f) return {Action::Play};
        const float ex = x - kStop.cx(), ey = y - kStop.cy();
        if (ex * ex + ey * ey <= kStop.w * kStop.w * 0.25f) return {Action::Stop};
    }
    if (kOverview.contains(x, y)) return {Action::Seek, -1, std::clamp((x - kOverview.x) / kOverview.w, 0.0f, 1.0f)};
    if (kA.contains(x, y)) return {Action::PlayA};
    if (kB.contains(x, y)) return {Action::PlayB};
    if (kMatch.contains(x, y)) return {Action::Match};
    if (kLoop.contains(x, y)) return {Action::Loop};
    for (int i = 0; i < 8; ++i) {
        if (!slotRow(i).contains(x, y)) continue;
        if (!s.slots[static_cast<std::size_t>(i)].used) return {Action::AddOpen, i};
        if (slotOn(i).contains(x, y)) return {Action::SlotOn, i};
        if (slotDel(i).contains(x, y)) return {Action::SlotRemove, i};
        if (slotUp(i).contains(x, y)) return {Action::SlotUp, i};
        if (slotDown(i).contains(x, y)) return {Action::SlotDown, i};
        return {Action::SlotSelect, i};
    }
    if (kAdd.contains(x, y)) return {Action::AddOpen, -1};
    if (faceArea().contains(x, y)) return {Action::Face};
    return {};
}

void RackView::button(Renderer& r, Rect b, std::string_view text, bool lit, bool hot, bool dim) const {
    const float pad = std::min(16.0f, b.w * 0.11f);
    const TextStyle s = fit(r, TextStyle::Label, text, b.w - pad);
    const std::string shown = clip(r, s, std::string(text), b.w - pad);
    text = shown;
    if (lit && !dim) {
        r.glow(b.inset(4), 9, 12.0f, kTh.teal.withAlpha(0.35f), true);
        r.fillRound(b.inset(2), 9, kTh.teal.withAlpha(0.92f), Blend::Over, false);
        r.fillRound({b.x + 8, b.y + 5, b.w - 16, b.h * 0.34f}, 7, {1, 1, 1, 0.14f}, Blend::Add);
        r.text(s, text, b.cx(), mid(r, b, s), kInk, Align::Center, TextFx::Plain);
    } else {
        r.fillRound(b.inset(2), 9, kGlass, Blend::Over);
        r.fillRound({b.x + 6, b.y + 3, b.w - 12, 2}, 1, kTh.teal.withAlpha(dim ? 0.1f : hot ? 0.8f : 0.3f), Blend::Over, !dim);
        r.text(s, text, b.cx(), mid(r, b, s), dim ? kTh.dim : hot ? kTh.silver : kTh.label, Align::Center, TextFx::Shadowed);
    }
}

void RackView::drawBack(Renderer& r, const RackViewState& s, const Hit& hv) const {
    // Background and panels.
    r.fillRound({-400, -400, kW + 800, kH + 800}, 0, kBg0, Blend::Over);
    r.fillRound(kTop, 0, rgb8(0x101417), Blend::Over);
    r.fillRound({0, kTop.bottom() - 2, kW, 2}, 1, kTh.tealDeep.withAlpha(0.6f), Blend::Over, true);
    r.fillRound(kTransport, 14, kPanel, Blend::Over);
    r.fillRound(kChain, 14, kPanel, Blend::Over);
    r.fillRound(faceArea().inset(-4), 16, rgb8(0x07090A), Blend::Over);

    // Top bar.
    r.text(TextStyle::Heading, "HEM-ELITE-RACK", 36, 60, kTh.teal, Align::Left, TextFx::Neon);
    const float tw = r.textWidth(TextStyle::Heading, "HEM-ELITE-RACK");
    r.text(TextStyle::Caption, "HYBRID ELITE MAFIA  -  EST. 2024", 36 + tw + 24, 56, kTh.label, Align::Left, TextFx::Shadowed);
    button(r, kOpen, "OPEN SONG", false, hv.action == Action::OpenSong);
    button(r, kExport, s.exportProgress >= 0.0f ? "EXPORTING..." : "EXPORT WAV", false, hv.action == Action::Export, s.songName.empty());
    {
        const std::string d = "OUTPUT: " + (s.device.empty() ? std::string("none") : s.device);
        button(r, kDevice, d, false, hv.action == Action::Device);
    }

    // Transport.
    {
        const bool hot = hv.action == Action::Play;
        const Rect p = kPlay;
        if (s.playing) r.glow(p.inset(14), p.w * 0.4f, 18.0f, kTh.teal.withAlpha(0.45f), true);
        r.fillRound(p.inset(2), p.w * 0.5f - 2, s.playing ? kTh.teal.withAlpha(0.95f) : kGlass, Blend::Over, s.playing);
        r.arc(p.cx(), p.cy(), p.w * 0.47f, 2.5f, -kPi, kPi, kTh.teal.withAlpha(hot ? 0.9f : 0.35f), hot);
        const float ic = p.w * 0.42f;
        r.sprite(s.playing ? Sprite::IconPause : Sprite::IconPlay, {p.cx() - ic * 0.5f, p.cy() - ic * 0.5f, ic, ic},
                 s.playing ? kInk : kTh.teal, Blend::Over, !s.playing);
        const Rect q = kStop;
        r.fillRound(q.inset(2), q.w * 0.5f - 2, kGlass, Blend::Over);
        r.arc(q.cx(), q.cy(), q.w * 0.47f, 2.0f, -kPi, kPi, kTh.teal.withAlpha(hv.action == Action::Stop ? 0.9f : 0.3f), false);
        const float ic2 = q.w * 0.4f;
        r.sprite(Sprite::IconStop, {q.cx() - ic2 * 0.5f, q.cy() - ic2 * 0.5f, ic2, ic2}, kTh.label, Blend::Over, false);
    }
    {
        const std::string name = s.loading ? "LOADING..." : s.songName.empty() ? "OPEN A SONG  (WAV, MP3, FLAC)  OR DROP ONE ON THIS WINDOW" : s.songName;
        const std::string time = s.songName.empty() ? std::string() : clock(s.position) + " / " + clock(s.length);
        const float timeW = time.empty() ? 0.0f : r.textWidth(TextStyle::Title, time) + 30;
        const TextStyle ns = fit(r, TextStyle::Title, name, kSongLine.w - timeW);
        r.text(ns, clip(r, ns, name, kSongLine.w - timeW), kSongLine.x, kSongLine.bottom() - 6, s.songName.empty() ? kTh.label : kTh.silver, Align::Left, TextFx::Shadowed);
        if (!time.empty()) r.text(TextStyle::Title, time, kSongLine.right(), kSongLine.bottom() - 6, kTh.teal, Align::Right, TextFx::NeonCrisp);
        const Rect o = kOverview;
        r.fillRound(o, 8, kGlass, Blend::Over);
        if (s.loading) {
            r.fillRound({o.x + 4, o.cy() - 3, (o.w - 8) * std::clamp(s.loadProgress, 0.0f, 1.0f), 6}, 3, kTh.teal, Blend::Over, true);
        } else if (s.overview && !s.overview->empty()) {
            const int cols = static_cast<int>(s.overview->size() / 2);
            const float colW = (o.w - 8) / static_cast<float>(cols);
            const float played = s.length > 0 ? static_cast<float>(s.position / s.length) : 0.0f;
            for (int c = 0; c < cols; ++c) {
                const float mn = (*s.overview)[static_cast<std::size_t>(2 * c)], mx = (*s.overview)[static_cast<std::size_t>(2 * c + 1)];
                const float y0 = o.cy() - mx * (o.h * 0.45f), y1 = o.cy() - mn * (o.h * 0.45f);
                const bool past = static_cast<float>(c) / static_cast<float>(cols) < played;
                r.fillRound({o.x + 4 + static_cast<float>(c) * colW, y0, std::max(colW, 1.0f), std::max(y1 - y0, 1.0f)}, 0,
                            past ? kTh.teal.withAlpha(0.85f) : kTh.before.withAlpha(0.45f), Blend::Over, false);
            }
            const float px = o.x + 4 + (o.w - 8) * std::clamp(played, 0.0f, 1.0f);
            r.fillRound({px - 1.5f, o.y + 2, 3, o.h - 4}, 1.5f, kTh.silver, Blend::Over, true);
        }
        if (hv.action == Action::Seek && !s.songName.empty()) r.fillRound({o.x + o.w * hv.value - 1, o.y + 2, 2, o.h - 4}, 1, kTh.teal.withAlpha(0.6f), Blend::Over, true);
    }
    button(r, kA, "A  PROCESSED", !s.original, hv.action == Action::PlayA);
    button(r, kB, "B  ORIGINAL", s.original, hv.action == Action::PlayB);
    button(r, kMatch, "MATCH LOUDNESS", s.match, hv.action == Action::Match);
    button(r, kLoop, "LOOP", s.loop, hv.action == Action::Loop);
    {   // output meter: L / R bars -48..0 dBFS, then loudness and the A/B match
        const Rect m = kMeter;
        for (int ch = 0; ch < 2; ++ch) {
            const Rect bar{m.x + 34, m.y + 6 + static_cast<float>(ch) * 26, m.w - 40, 18};
            r.fillRound(bar, 5, kGlass, Blend::Over);
            const float f = std::clamp((s.peakDb[ch] + 48.0f) / 48.0f, 0.0f, 1.0f);
            if (f > 0.002f) {
                const float hot = std::clamp((s.peakDb[ch] + 3.0f) / 3.0f, 0.0f, 1.0f);
                const Rgba c{kTh.teal.r + (1.0f - kTh.teal.r) * hot, kTh.teal.g * (1.0f - 0.6f * hot), kTh.teal.b * (1.0f - 0.6f * hot), 1.0f};
                r.fillRound({bar.x + 2, bar.y + 2, (bar.w - 4) * f, bar.h - 4}, 4, c, Blend::Over, true);
            }
            r.text(TextStyle::Caption, ch == 0 ? "L" : "R", m.x + 14, bar.y + 15, kTh.label, Align::Center, TextFx::Shadowed);
        }
        char b[96];
        if (s.outLufs > -100.0f) std::snprintf(b, sizeof b, "OUT %.1f LUFS", s.outLufs);
        else std::snprintf(b, sizeof b, "OUT  --  LUFS");
        r.text(TextStyle::Value, b, m.x + 34, m.y + 82, kTh.teal, Align::Left, TextFx::NeonCrisp);
        if (s.match) std::snprintf(b, sizeof b, "B MATCHED %+.1f dB  -  DELAY %.0f ms", s.matchDb, s.latencyMs);
        else std::snprintf(b, sizeof b, "B NOT MATCHED  -  DELAY %.0f ms", s.latencyMs);
        const TextStyle cs = fit(r, TextStyle::Caption, b, m.w - 40);
        r.text(cs, clip(r, cs, b, m.w - 40), m.x + 34, m.y + 110, kTh.label, Align::Left, TextFx::Shadowed);
    }

    // Chain.
    r.text(TextStyle::Label, "CHAIN", kChain.x + 18, kChain.y + 42, kTh.silver, Align::Left, TextFx::Shadowed);
    r.text(TextStyle::Caption, "TOP TO BOTTOM", kChain.right() - 18, kChain.y + 42, kTh.dim, Align::Right, TextFx::Shadowed);
    for (int i = 0; i < 8; ++i) {
        const SlotView& sv = s.slots[static_cast<std::size_t>(i)];
        const Rect row = slotRow(i);
        const bool sel = i == s.selected;
        const bool hotRow = hv.index == i && (hv.action == Action::SlotSelect || hv.action == Action::AddOpen);
        if (sel) r.glow(row.inset(3), 10, 12.0f, kTh.teal.withAlpha(0.3f), true);
        r.fillRound(row, 10, sel ? rgb8(0x0F2427) : rgb8(0x0D1113), Blend::Over);
        r.fillRound({row.x, row.y + 10, 3, row.h - 20}, 1.5f, sel ? kTh.teal : kEdge, Blend::Over, sel);
        r.text(TextStyle::Label, std::to_string(i + 1), row.x + 22, row.y + 54, kTh.dim, Align::Center, TextFx::Shadowed);
        if (!sv.used) {
            r.text(TextStyle::Caption, hotRow ? "+ ADD A PLUG-IN HERE" : "EMPTY", row.x + 46, row.y + 52, hotRow ? kTh.teal : kTh.dim, Align::Left,
                   TextFx::Shadowed);
            continue;
        }
        const TextStyle ns = fit(r, TextStyle::Title, sv.name, 152);
        r.text(ns, clip(r, ns, sv.name, 152), row.x + 46, row.y + 42, sv.on ? kTh.silver : kTh.dim, Align::Left, TextFx::Shadowed);
        const TextStyle ss = fit(r, TextStyle::Caption, sv.suite, 152);
        r.text(ss, clip(r, ss, sv.suite, 152), row.x + 46, row.y + 74, sv.on ? kTh.teal : kTh.dim, Align::Left, sv.on ? TextFx::NeonCrisp : TextFx::Shadowed);
        button(r, slotOn(i), sv.on ? "ON" : "OFF", sv.on, hv.action == Action::SlotOn && hv.index == i);
        button(r, slotDel(i), "DEL", false, hv.action == Action::SlotRemove && hv.index == i);
        // UP / DOWN: arrows (no word fits these little buttons at every window size)
        for (int k = 0; k < 2; ++k) {
            const Rect b = k == 0 ? slotUp(i) : slotDown(i);
            const bool hot = hv.index == i && hv.action == (k == 0 ? Action::SlotUp : Action::SlotDown);
            const bool dim = k == 0 ? i == 0 : i == 7;
            button(r, b, "", false, hot, dim);
            const float sz = b.h * 0.62f;
            r.spriteRotated(k == 0 ? Sprite::IconPrev : Sprite::IconNext, b.cx(), b.cy(), sz, sz, kPi * 0.5f,
                            dim ? kTh.dim : hot ? kTh.silver : kTh.teal, Blend::Over, !dim);
        }
    }
    button(r, kAdd, "+  ADD PLUG-IN", false, hv.action == Action::AddOpen && hv.index < 0);

    // Empty face area.
    if (s.selected < 0 || !s.slots[static_cast<std::size_t>(std::clamp(s.selected, 0, 7))].used) {
        const Rect a = faceArea();
        r.text(TextStyle::Panel, "NO PLUG-IN SELECTED", a.cx(), a.cy() - 20, kTh.label, Align::Center, TextFx::Shadowed);
        r.text(TextStyle::Label, "PRESS  + ADD PLUG-IN  ON THE LEFT", a.cx(), a.cy() + 36, kTh.teal, Align::Center, TextFx::NeonCrisp);
    }

    // Status line.
    r.fillRound(kStatus, 0, rgb8(0x0D1012), Blend::Over);
    {
        const Rgba c = s.alert ? kTh.red : kTh.label;
        float right = kStatus.right() - 20;
        if (s.exportProgress >= 0.0f) {
            const Rect bar{kStatus.right() - 420, kStatus.y + 9, 400, 14};
            r.fillRound(bar, 7, kGlass, Blend::Over);
            r.fillRound({bar.x + 2, bar.y + 2, (bar.w - 4) * std::clamp(s.exportProgress, 0.0f, 1.0f), bar.h - 4}, 5, kTh.teal, Blend::Over, true);
            right = bar.x - 20;
        } else {
            char b[64];
            std::snprintf(b, sizeof b, "AUDIO CPU %.1f%%", s.cpuPct);
            r.text(TextStyle::Caption, b, right, kStatus.y + 24, kTh.dim, Align::Right, TextFx::Shadowed);
            right -= r.textWidth(TextStyle::Caption, b) + 30;
        }
        const TextStyle st = fit(r, TextStyle::Caption, s.status, right - 40);
        r.text(st, clip(r, st, s.status, right - 40), 20, kStatus.y + 24, c, Align::Left, TextFx::Shadowed);
    }
}

void RackView::drawFront(Renderer& r, const RackViewState& s, const Hit& hv) const {
    if (!s.picker) return;
    const Rect a = faceArea();
    r.fillRound(a, 16, rgb8(0x000000, 0.72f), Blend::Over);
    const Rect p = pickerPanel();
    r.glow(p.inset(6), 16, 22.0f, kTh.teal.withAlpha(0.25f), true);
    r.fillRound(p, 16, kPanel, Blend::Over);
    r.text(TextStyle::Panel, "ADD A PLUG-IN", p.x + 40, p.y + 70, kTh.teal, Align::Left, TextFx::Neon);
    button(r, pickerClose(), "CLOSE", false, hv.action == Action::PickerClose);
    for (int i = 0; i < static_cast<int>(s.pickerItems.size()); ++i) {
        const Rect it = pickerItem(i);
        const bool hot = hv.action == Action::AddPlugin && hv.index == i;
        if (hot) r.glow(it.inset(4), 12, 14.0f, kTh.teal.withAlpha(0.35f), true);
        r.fillRound(it, 12, hot ? rgb8(0x0F2427) : rgb8(0x0D1113), Blend::Over);
        r.text(TextStyle::Panel, s.pickerItems[static_cast<std::size_t>(i)].first, it.x + 30, it.y + 58, hot ? kTh.teal : kTh.silver, Align::Left,
               hot ? TextFx::Neon : TextFx::Shadowed);
        const std::string& blurb = s.pickerItems[static_cast<std::size_t>(i)].second;
        const TextStyle bs = fit(r, TextStyle::Label, blurb, it.w - 60);
        r.text(bs, clip(r, bs, blurb, it.w - 60), it.x + 30, it.y + 104, kTh.label, Align::Left, TextFx::Shadowed);
    }
}

std::vector<float> makeOverview(const std::vector<float>& left, const std::vector<float>& right) {
    std::vector<float> out(2 * kOverviewColumns, 0.0f);
    const std::size_t n = std::min(left.size(), right.size());
    if (n == 0) return out;
    float peak = 1e-6f;
    for (int c = 0; c < kOverviewColumns; ++c) {
        const std::size_t a = n * static_cast<std::size_t>(c) / kOverviewColumns, b = std::max(a + 1, n * static_cast<std::size_t>(c + 1) / kOverviewColumns);
        float mn = 0.0f, mx = 0.0f;
        for (std::size_t i = a; i < b && i < n; ++i) {
            const float v = 0.5f * (left[i] + right[i]);
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        out[static_cast<std::size_t>(2 * c)] = mn;
        out[static_cast<std::size_t>(2 * c + 1)] = mx;
        peak = std::max({peak, -mn, mx});
    }
    for (float& v : out) v /= peak;   // fill the strip whatever the level
    return out;
}

} // namespace hem::app
