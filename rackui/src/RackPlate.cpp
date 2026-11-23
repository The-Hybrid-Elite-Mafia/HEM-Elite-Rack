// The static HEM-Elite-Rack plate: brushed gunmetal with recessed windows and dial seats,
// a raised footer strip, Phillips screws, the crown badge and the ghost. Rendered on the CPU
// once per plug-in and size (a height field lit from the top left), then uploaded as one texture.
#include "hem/ui/Face.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace hem::ui {
namespace {

struct V3 { float x, y, z; };
inline V3 norm3(V3 v) { const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return {v.x / l, v.y / l, v.z / l}; }

inline std::uint32_t hash2(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u;
    h ^= h >> 13; h *= 0x5BD1E995u; h ^= h >> 15;
    return h;
}
inline float vnoise(float x, float y) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi), fy = y - static_cast<float>(yi);
    const float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    auto h = [](int a, int b) { return static_cast<float>(hash2(a, b) & 0xFFFF) / 65535.0f; };
    const float a = h(xi, yi), b = h(xi + 1, yi), c = h(xi, yi + 1), d = h(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
inline float smooth01(float e0, float e1, float x) { const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

// Signed distance to a rounded rectangle (negative inside).
inline float sdRound(float px, float py, const Rect& r, float radius) {
    const float hx = r.w * 0.5f, hy = r.h * 0.5f;
    const float qx = std::fabs(px - r.cx()) - hx + radius, qy = std::fabs(py - r.cy()) - hy + radius;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

constexpr float kChamfer = 3.0f;

struct Surf {
    float h = 0;                // height in plate units (0 = plate top)
    std::uint8_t kind = 0;      // 0 metal, 1 chamfer/edge, 2 recess floor, 3 glass floor, 4 groove, 5 screw, 6 off-plate
};

// Straight-alpha sRGB image scaled with a box filter (small images, done once).
hc::ui::Image scaled(const hc::ui::Image& src, std::uint32_t w, std::uint32_t h) { return hc::ui::resizeSrgb(src, w, h); }

void blurAlpha(std::vector<float>& a, int w, int h, int radius) {
    std::vector<float> tmp(a.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            float acc = 0;
            for (int x = -radius; x <= radius; ++x) acc += a[static_cast<std::size_t>(y * w + std::clamp(x, 0, w - 1))];
            for (int x = 0; x < w; ++x) {
                tmp[static_cast<std::size_t>(y * w + x)] = acc / static_cast<float>(2 * radius + 1);
                acc += a[static_cast<std::size_t>(y * w + std::clamp(x + radius + 1, 0, w - 1))] - a[static_cast<std::size_t>(y * w + std::clamp(x - radius, 0, w - 1))];
            }
        }
        for (int x = 0; x < w; ++x) {
            float acc = 0;
            for (int y = -radius; y <= radius; ++y) acc += tmp[static_cast<std::size_t>(std::clamp(y, 0, h - 1) * w + x)];
            for (int y = 0; y < h; ++y) {
                a[static_cast<std::size_t>(y * w + x)] = acc / static_cast<float>(2 * radius + 1);
                acc += tmp[static_cast<std::size_t>(std::clamp(y + radius + 1, 0, h - 1) * w + x)] - tmp[static_cast<std::size_t>(std::clamp(y - radius, 0, h - 1) * w + x)];
            }
        }
    }
}

// Composites `art` (straight sRGB) into the linear-light plate at rect r (plate units), with a
// soft shadow down-right, and optionally a coloured glow around it.
void composite(std::vector<V3>& lin, int W, int H, float ppu, const hc::ui::Image& art, const Rect& r, float shadow, const Rgba* glow, float glowAmount) {
    if (art.empty()) return;
    const int pad = static_cast<int>(std::ceil(18.0f * ppu)) + 2;
    const int aw = std::max(1, static_cast<int>(std::lround(r.w * ppu))), ah = std::max(1, static_cast<int>(std::lround(r.h * ppu)));
    const hc::ui::Image img = scaled(art, static_cast<std::uint32_t>(aw), static_cast<std::uint32_t>(ah));
    const int x0 = static_cast<int>(std::lround(r.x * ppu)), y0 = static_cast<int>(std::lround(r.y * ppu));
    const int bw = aw + 2 * pad, bh = ah + 2 * pad;
    std::vector<float> alpha(static_cast<std::size_t>(bw * bh), 0.0f);
    for (int y = 0; y < ah; ++y)
        for (int x = 0; x < aw; ++x) alpha[static_cast<std::size_t>((y + pad) * bw + x + pad)] = img.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y))[3] / 255.0f;
    std::vector<float> soft = alpha;
    blurAlpha(soft, bw, bh, std::max(1, static_cast<int>(5.0f * ppu)));
    const int sx = static_cast<int>(std::lround(4.0f * ppu)), sy = static_cast<int>(std::lround(7.0f * ppu));
    for (int y = 0; y < bh; ++y)
        for (int x = 0; x < bw; ++x) {
            const int px = x0 - pad + x, py = y0 - pad + y;
            if (px < 0 || py < 0 || px >= W || py >= H) continue;
            V3& c = lin[static_cast<std::size_t>(py * W + px)];
            // shadow: the blurred alpha, shifted down-right
            const int qx = x - sx, qy = y - sy;
            if (qx >= 0 && qy >= 0 && shadow > 0) {
                const float s = soft[static_cast<std::size_t>(qy * bw + qx)] * shadow;
                c = {c.x * (1 - s), c.y * (1 - s), c.z * (1 - s)};
            }
            if (glow) {
                const float gl = soft[static_cast<std::size_t>(y * bw + x)] * glowAmount;
                c = {c.x + gl * hc::ui::srgbToLinear(glow->r), c.y + gl * hc::ui::srgbToLinear(glow->g), c.z + gl * hc::ui::srgbToLinear(glow->b)};
            }
        }
    for (int y = 0; y < ah; ++y)
        for (int x = 0; x < aw; ++x) {
            const int px = x0 + x, py = y0 + y;
            if (px < 0 || py < 0 || px >= W || py >= H) continue;
            const std::uint8_t* s = img.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const float a = s[3] / 255.0f;
            if (a <= 0) continue;
            V3& c = lin[static_cast<std::size_t>(py * W + px)];
            c = {c.x + a * (hc::ui::srgb8ToLinear(s[0]) - c.x), c.y + a * (hc::ui::srgb8ToLinear(s[1]) - c.y), c.z + a * (hc::ui::srgb8ToLinear(s[2]) - c.z)};
        }
}

} // namespace

std::optional<BrandArt> BrandArt::load(const std::filesystem::path& dir, std::string* error) {
    BrandArt a;
    auto crown = hc::ui::loadImageFile(dir / "brand" / "crown_badge.png", error);
    if (!crown) return std::nullopt;
    auto ghost = hc::ui::loadImageFile(dir / "brand" / "ghost.png", error);
    if (!ghost) return std::nullopt;
    a.crown = std::move(*crown);
    a.ghost = std::move(*ghost);
    return a;
}

std::filesystem::path plateArtFile(const std::filesystem::path& assetDir, std::string_view pluginName) {
    std::string file = "HEM_";
    for (const char c : pluginName) file += c == ' ' ? '_' : c;
    return assetDir / "plates" / (file + ".jpg");
}

std::optional<hc::ui::Image> loadPlateArt(const std::filesystem::path& assetDir, std::string_view pluginName, std::string* error) {
    const std::filesystem::path file = plateArtFile(assetDir, pluginName);
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        if (error) *error = "no faceplate picture " + file.filename().string();
        return std::nullopt;
    }
    return hc::ui::loadImageFile(file, error);
}

std::filesystem::path cardFile(const std::filesystem::path& assetDir, std::string_view pluginName) {
    std::string file = "HEM_";
    for (const char c : pluginName) file += c == ' ' ? '_' : c;
    return assetDir / "cards" / (file + ".jpg");
}

CardAtlas buildCardAtlas(const std::filesystem::path& assetDir, const std::vector<std::string>& names, int cellW) {
    CardAtlas A;
    const int n = static_cast<int>(names.size());
    A.cell.assign(names.size(), -1);
    if (n == 0) return A;
    A.cellW = std::clamp(cellW, 16, kCardFileW);
    A.cellH = std::max(9, static_cast<int>(std::lround(A.cellW * 9.0 / 16.0)));
    const int maxCols = std::max(1, static_cast<int>(hc::ui::kMaxTextureSide) / A.cellW);
    // close to square, so neither side runs into the 4096 px cap
    A.cols = std::clamp(static_cast<int>(std::ceil(std::sqrt(n * static_cast<double>(A.cellH) / A.cellW))), 1, maxCols);
    A.rows = (n + A.cols - 1) / A.cols;
    if (A.rows * A.cellH > static_cast<int>(hc::ui::kMaxTextureSide)) {   // cannot happen at <= 384 px cells and 120 cards
        A.cols = maxCols;
        A.rows = (n + A.cols - 1) / A.cols;
    }
    A.image.width = static_cast<std::uint32_t>(A.cols * A.cellW);
    A.image.height = static_cast<std::uint32_t>(A.rows * A.cellH);
    A.image.rgba.assign(static_cast<std::size_t>(A.image.width) * A.image.height * 4, 0);
    for (std::size_t p = 3; p < A.image.rgba.size(); p += 4) A.image.rgba[p] = 255;
    for (int i = 0; i < n; ++i) {
        std::error_code ec;
        const std::filesystem::path f = cardFile(assetDir, names[static_cast<std::size_t>(i)]);
        std::optional<hc::ui::Image> pic = std::filesystem::exists(f, ec) ? hc::ui::loadImageFile(f) : std::nullopt;
        if (!pic || pic->empty()) { ++A.missing; continue; }
        const hc::ui::Image small = hc::ui::resizeSrgb(*pic, static_cast<std::uint32_t>(A.cellW), static_cast<std::uint32_t>(A.cellH));
        const int x0 = (i % A.cols) * A.cellW, y0 = (i / A.cols) * A.cellH;
        for (int y = 0; y < A.cellH; ++y) {
            std::uint8_t* dst = &A.image.rgba[((static_cast<std::size_t>(y0 + y) * A.image.width) + static_cast<std::size_t>(x0)) * 4];
            const std::uint8_t* src = &small.rgba[static_cast<std::size_t>(y) * small.width * 4];
            for (int x = 0; x < A.cellW; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 0];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 2];
                dst[x * 4 + 3] = 255;   // cards are opaque
            }
        }
        A.cell[static_cast<std::size_t>(i)] = i;
    }
    return A;
}

hc::ui::Image renderRackPlate(const FaceLayout& L, const BrandArt& art, float ppu, const hc::ui::Image* plateArt) {
    const int W = std::max(1, static_cast<int>(std::lround(kPlateW * ppu))), H = std::max(1, static_cast<int>(std::lround(kPlateH * ppu)));
    if (plateArt && !plateArt->empty()) {
        // The picture is the plate: scaled to the size asked for (in linear light), the real crown and ghost on top
        // (no extra shadow: the picture has its own), opaque.
        const hc::ui::Image pic = hc::ui::resizeSrgb(*plateArt, static_cast<std::uint32_t>(W), static_cast<std::uint32_t>(H));
        std::vector<V3> lin(static_cast<std::size_t>(W) * static_cast<std::size_t>(H));
        for (std::size_t i = 0; i < lin.size(); ++i)
            lin[i] = {hc::ui::srgb8ToLinear(pic.rgba[i * 4 + 0]), hc::ui::srgb8ToLinear(pic.rgba[i * 4 + 1]), hc::ui::srgb8ToLinear(pic.rgba[i * 4 + 2])};
        composite(lin, W, H, ppu, art.crown, L.crown, 0.0f, nullptr, 0.0f);
        composite(lin, W, H, ppu, art.ghost, L.ghost, 0.0f, nullptr, 0.0f);
        hc::ui::Image img;
        img.width = static_cast<std::uint32_t>(W);
        img.height = static_cast<std::uint32_t>(H);
        img.rgba.resize(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4);
        for (std::size_t i = 0; i < lin.size(); ++i) {
            img.rgba[i * 4 + 0] = hc::ui::linearToSrgb8(lin[i].x);
            img.rgba[i * 4 + 1] = hc::ui::linearToSrgb8(lin[i].y);
            img.rgba[i * 4 + 2] = hc::ui::linearToSrgb8(lin[i].z);
            img.rgba[i * 4 + 3] = 255;
        }
        return img;
    }
    const Rect plate{4, 4, kPlateW - 8, L.footer.y - 10};
    const Rect footer = L.footer;
    const float bevel = 9.0f;

    auto surfAt = [&](float x, float y) {
        Surf s;
        // which slab: the main plate or the footer strip (a gap between them)
        const float dMain = sdRound(x, y, plate, 20.0f), dFoot = sdRound(x, y, footer, 16.0f);
        const float d = std::min(dMain, dFoot);
        if (d > 0.0f) { s.h = -14.0f; s.kind = 6; return s; }
        const float in = -d;
        if (in < bevel) { const float t = 1.0f - in / bevel; s.h = -6.0f * t * t; s.kind = 1; }
        for (const Recess& r : L.recesses) {
            float inside;
            if (r.round) { const float dx = x - r.r.cx(), dy = y - r.r.cy(); inside = r.r.w * 0.5f - std::sqrt(dx * dx + dy * dy); }
            else inside = -sdRound(x, y, r.r, r.radius);
            if (inside <= 0) continue;
            float h;
            std::uint8_t k;
            if (inside < kChamfer) { h = -inside; k = 1; }
            else { h = -kChamfer - (inside - kChamfer) * 3.0f; k = 1; }
            if (h <= -r.depth) { h = -r.depth; k = r.glass ? 3 : 2; }
            if (h < s.h) { s.h = h; s.kind = k; }
        }
        for (const auto& g : L.grooves) {
            const float vx = g[2] - g[0], vy = g[3] - g[1], len2 = vx * vx + vy * vy;
            const float t = std::clamp(((x - g[0]) * vx + (y - g[1]) * vy) / std::max(len2, 1e-6f), 0.0f, 1.0f);
            const float dx = x - (g[0] + t * vx), dy = y - (g[1] + t * vy), dd = std::sqrt(dx * dx + dy * dy);
            if (dd < 2.2f) { const float h = -2.5f * std::sqrt(1.0f - (dd / 2.2f) * (dd / 2.2f)); if (h < s.h) { s.h = h; s.kind = 4; } }
        }
        for (const auto& sc : L.screws) {
            const float dx = x - sc[0], dy = y - sc[1], r = std::sqrt(dx * dx + dy * dy);
            if (r < 15.0f) {
                const bool slot = (std::fabs(dx) < 1.6f && std::fabs(dy) < 8.5f) || (std::fabs(dy) < 1.6f && std::fabs(dx) < 8.5f);
                s.h = slot ? -1.0f : 3.2f * std::sqrt(std::max(0.0f, 1.0f - (r / 15.0f) * (r / 15.0f)));
                s.kind = 5;
            }
        }
        return s;
    };

    std::vector<Surf> surf(static_cast<std::size_t>(W) * static_cast<std::size_t>(H));
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) surf[static_cast<std::size_t>(py * W + px)] = surfAt((static_cast<float>(px) + 0.5f) / ppu, (static_cast<float>(py) + 0.5f) / ppu);
    auto hAt = [&](int px, int py) { return surf[static_cast<std::size_t>(std::clamp(py, 0, H - 1) * W + std::clamp(px, 0, W - 1))].h; };

    const V3 light = norm3({-0.45f, -0.6f, 0.9f});
    const V3 half = norm3({light.x, light.y, light.z + 1.0f});
    std::vector<V3> lin(static_cast<std::size_t>(W) * static_cast<std::size_t>(H));
    for (int py = 0; py < H; ++py) {
        for (int px = 0; px < W; ++px) {
            const Surf& s = surf[static_cast<std::size_t>(py * W + px)];
            const float x = (static_cast<float>(px) + 0.5f) / ppu, y = (static_cast<float>(py) + 0.5f) / ppu;
            const float step = 1.0f / ppu;
            const float dhx = (hAt(px + 1, py) - hAt(px - 1, py)) / (2 * step), dhy = (hAt(px, py + 1) - hAt(px, py - 1)) / (2 * step);
            const V3 n = norm3({-dhx, -dhy, 1.0f});
            const float diff = std::max(0.0f, n.x * light.x + n.y * light.y + n.z * light.z);
            const float spec = std::pow(std::max(0.0f, n.x * half.x + n.y * half.y + n.z * half.z), 40.0f);
            V3 c{};
            if (s.kind == 6) {
                c = {0.004f, 0.005f, 0.006f};
            } else if (s.kind == 3) {   // display glass: near black with a faint blue depth
                const float v = 0.0035f + 0.0015f * vnoise(x * 0.05f, y * 0.05f);
                c = {v, v * 1.25f, v * 1.45f};
            } else if (s.kind == 5) {   // chrome screw
                const float m = 0.10f + 0.55f * diff + 1.2f * spec;
                c = {m, m * 1.01f, m * 1.03f};
            } else {
                // brushed gunmetal: long horizontal streaks, fine grain, a broad diagonal sheen
                const float streak = 0.55f * vnoise(x * 0.0035f, y * 0.85f) + 0.30f * vnoise(x * 0.011f, y * 2.7f) + 0.15f * vnoise(x * 0.03f, y * 6.0f);
                const float grain = static_cast<float>(hash2(static_cast<int>(x * 3.0f), static_cast<int>(y * 4.0f)) & 0xFF) / 255.0f;
                const float u = x / kPlateW * 0.75f + y / kPlateH * 0.45f;
                const float sheen = std::exp(-(u - 0.42f) * (u - 0.42f) / 0.028f) * 0.020f + std::exp(-(u - 0.86f) * (u - 0.86f) / 0.010f) * 0.008f;
                float a = 0.020f + 0.016f * streak + 0.004f * grain + sheen;
                if (s.kind == 2) a *= 0.45f;                        // recess floors: darker milled metal
                if (s.kind == 4) a *= 0.35f;                        // grooves
                const float lit = 0.35f + 0.85f * diff;
                const float sp = (s.kind == 1 ? 0.35f : 0.06f) * spec;
                c = {a * lit + sp, a * lit * 1.03f + sp, a * lit * 1.07f + sp};
                // ambient occlusion toward the bottom of recesses
                if (s.h < -1.0f && s.kind != 1) {
                    const float ao = smooth01(-10.0f, 0.0f, s.h);
                    c = {c.x * (0.5f + 0.5f * ao), c.y * (0.5f + 0.5f * ao), c.z * (0.5f + 0.5f * ao)};
                }
            }
            // gentle vignette
            const float vx = (x / kPlateW - 0.5f) * 2.0f, vy = (y / kPlateH - 0.5f) * 2.0f;
            const float vig = 1.0f - 0.22f * (vx * vx * 0.6f + vy * vy * 0.8f);
            lin[static_cast<std::size_t>(py * W + px)] = {c.x * vig, c.y * vig, c.z * vig};
        }
    }
    // The crown badge (top left) and the ghost (top right), with shadow; no halo around the ghost.
    composite(lin, W, H, ppu, art.crown, L.crown, 0.75f, nullptr, 0.0f);
    composite(lin, W, H, ppu, art.ghost, L.ghost, 0.0f, nullptr, 0.0f);

    hc::ui::Image img;
    img.width = static_cast<std::uint32_t>(W);
    img.height = static_cast<std::uint32_t>(H);
    img.rgba.resize(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4);
    for (std::size_t i = 0; i < lin.size(); ++i) {
        img.rgba[i * 4 + 0] = hc::ui::linearToSrgb8(lin[i].x);
        img.rgba[i * 4 + 1] = hc::ui::linearToSrgb8(lin[i].y);
        img.rgba[i * 4 + 2] = hc::ui::linearToSrgb8(lin[i].z);
        img.rgba[i * 4 + 3] = 255;
    }
    return img;
}

RackFrameMap rackFrameMap() noexcept {
    using namespace rackframe;
    RackFrameMap m;
    m.s = std::min(kContent.w / kBody.w, kContent.h / kBody.h);
    m.ox = kContent.cx() - kBody.cx() * m.s;
    m.oy = kContent.cy() - kBody.cy() * m.s;
    return m;
}

hc::ui::Image renderRackPlateFramed(const FaceLayout& L, float ppu, const hc::ui::Image& frameArt) {
    using namespace rackframe;
    const auto W = static_cast<std::uint32_t>(std::max(1L, std::lround(kW * ppu))), H = static_cast<std::uint32_t>(std::max(1L, std::lround(kH * ppu)));
    // 1) the frame art at the output size
    hc::ui::Image img = frameArt.empty() ? hc::ui::Image{} : hc::ui::resizeSrgb(frameArt, W, H);
    if (img.empty() || img.width != W || img.height != H) {   // no art on disk: plain dark chassis
        img = hc::ui::Image{};
        img.width = W; img.height = H;
        img.rgba.assign(static_cast<std::size_t>(W) * H * 4, 255);
        for (std::size_t i = 0; i < img.rgba.size(); i += 4) { img.rgba[i] = img.rgba[i + 1] = 14; img.rgba[i + 2] = 17; }
    }
    const RackFrameMap m = rackFrameMap();
    // Per-pixel painter over a box (frame pixels), linear light.
    auto paint = [&](Rect box, auto&& f) {
        const int x0 = std::max(0, static_cast<int>(box.x * ppu)), x1 = std::min(static_cast<int>(W), static_cast<int>(box.right() * ppu) + 1);
        const int y0 = std::max(0, static_cast<int>(box.y * ppu)), y1 = std::min(static_cast<int>(H), static_cast<int>(box.bottom() * ppu) + 1);
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) {
                std::uint8_t* o = &img.rgba[(static_cast<std::size_t>(py) * W + static_cast<std::size_t>(px)) * 4];
                float c[3] = {hc::ui::srgb8ToLinear(o[0]), hc::ui::srgb8ToLinear(o[1]), hc::ui::srgb8ToLinear(o[2])};
                f((static_cast<float>(px) + 0.5f) / ppu, (static_cast<float>(py) + 0.5f) / ppu, c);
                for (int k = 0; k < 3; ++k) o[k] = hc::ui::linearToSrgb8(std::clamp(c[k], 0.0f, 1.0f));
                o[3] = 255;
            }
    };
    // Chamfered rectangle (cut corners), signed distance: negative inside.
    auto sdChamfer = [](float x, float y, Rect r, float cut) {
        const float cx = r.cx(), cy = r.cy(), hx = r.w * 0.5f, hy = r.h * 0.5f;
        const float qx = std::fabs(x - cx), qy = std::fabs(y - cy);
        const float box = std::max(qx - hx, qy - hy);
        const float diag = (qx + qy - (hx + hy - cut)) * 0.70710678f;
        return std::max(box, diag);
    };
    const float cyan[3] = {0.0f, 0.78f, 1.0f}, amber[3] = {1.0f, 0.34f, 0.01f};   // neon (linear)
    const float s = m.s;

    // 2) Steven's reference: the controls on CLEAR glass plates (the board shows through), cut corners, a thin
    //    steel bevel, a thin neon line (cyan top-left, amber bottom-right), small screws. Screens: darker glass in a
    //    chrome bezel. Big milled panels a layout draws become their own plates; otherwise the whole body is one.
    std::vector<std::pair<Rect, bool>> plates;   // frame rect, screen window
    for (const Recess& rc : L.recesses) {
        if (rc.r.y >= L.footer.y || rc.r.bottom() <= kBody.y || rc.round) continue;
        if (rc.glass) plates.push_back({m.toFrame(rc.r), true});
        else if (rc.r.w >= 260.0f && rc.r.h >= 140.0f) plates.push_back({m.toFrame(rc.r), false});
    }
    if (std::none_of(plates.begin(), plates.end(), [](const auto& p) { return !p.second; })) {
        const Rect body = m.toFrame(kBody);
        plates.insert(plates.begin(), {{body.x - 18, body.y - 18, body.w + 36, body.h + 36}, false});
    }
    for (const auto& [r, screen] : plates) {
        const float cut = std::min({(screen ? 16.0f : 40.0f) * s, r.w * 0.22f, r.h * 0.22f}), reach = 16.0f;
        paint(r.inset(-reach - 2), [&](float x, float y, float* c) {
            const float d = sdChamfer(x, y, r, cut);
            const float t = ((x - r.x) / r.w + (y - r.y) / r.h) * 0.5f;   // 0 top-left .. 1 bottom-right
            const float mixA = std::clamp((t - 0.46f) / 0.08f, 0.0f, 1.0f);
            float nc[3];
            for (int k = 0; k < 3; ++k) nc[k] = screen ? cyan[k] : cyan[k] * (1.0f - mixA) + amber[k] * mixA;
            const float bevelW = screen ? 7.0f : 6.0f;
            if (d < 0.0f) {
                if (screen && d < -bevelW) {   // deep display glass, the board a ghost behind it
                    const float v = 0.008f + 0.006f * std::clamp(1.0f - (y - r.y) / r.h, 0.0f, 1.0f);
                    c[0] = c[0] * 0.10f + v; c[1] = c[1] * 0.10f + v * 1.18f; c[2] = c[2] * 0.10f + v * 1.45f;
                } else if (!screen) {
                    for (int k = 0; k < 3; ++k) c[k] *= 0.93f;   // clear: the board shows through
                }
                if (d >= -bevelW) {   // brushed steel, lit from the top-left
                    const float u = -d / bevelW;
                    const float light = 0.5f + 0.5f * std::clamp(1.0f - 2.0f * t, -1.0f, 1.0f);
                    const float v = 0.06f + 0.16f * light * (1.0f - 0.5f * u) + 0.05f * std::sin(u * 3.14159f);
                    for (int k = 0; k < 3; ++k) c[k] = v * (k == 2 ? 1.08f : 1.0f);
                }
            }
            if (std::fabs(d) < 1.2f) for (int k = 0; k < 3; ++k) c[k] *= 0.35f;   // dark seam where plate meets board
            const float line = std::fabs(d + bevelW + 2.0f);   // the neon line, colour only
            const float core = std::clamp(1.0f - line / (screen ? 1.6f : 2.2f), 0.0f, 1.0f), glow = std::exp(-line / 7.0f) * (screen ? 0.18f : 0.30f);
            const float g = d < -bevelW ? core * (screen ? 0.8f : 1.2f) + glow : core * 1.2f + glow * 0.4f;
            for (int k = 0; k < 3; ++k) c[k] += nc[k] * g;
            if (d >= 0.0f) {   // faint contact shadow on the board just outside
                const float sh = 0.86f + 0.14f * std::clamp(d / reach, 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k) c[k] *= sh;
            }
        });
        if (screen) continue;
        const float sr = std::clamp(6.0f * s, 6.0f, 11.0f), in = cut * 0.45f + sr + 8.0f;   // small corner screws
        for (auto [sx, sy] : {std::pair{r.x + in, r.y + in}, std::pair{r.right() - in, r.y + in}, std::pair{r.x + in, r.bottom() - in},
                                    std::pair{r.right() - in, r.bottom() - in}}) {
            paint({sx - sr - 4, sy - sr - 4, 2 * sr + 8, 2 * sr + 8}, [&](float x, float y, float* c) {
                const float dx = x - sx, dy = y - sy, d = std::sqrt(dx * dx + dy * dy);
                if (d > sr + 2.0f) return;
                if (d > sr) { for (int k = 0; k < 3; ++k) c[k] *= 0.55f; return; }
                const float n = (-dx * 0.6f - dy * 0.8f) / sr;
                float v = 0.12f + 0.20f * std::max(0.0f, n);
                if (std::fabs((dx - dy) * 0.7071f) < sr * 0.16f && d < sr * 0.8f) v *= 0.25f;
                c[0] = v; c[1] = v * 1.02f; c[2] = v * 1.06f;
            });
        }
    }

    // 3) knobs: a soft shadow where the chrome knob sits, and a tick scale round it (the knob itself is a live part)
    std::vector<Rect> seats;   // knobs that share a seat (only one shows at a time) get one shadow and one scale
    for (const Widget& w : L.widgets) {
        if (w.kind != WKind::Knob || w.r.w <= 0.0f) continue;
        if (std::any_of(seats.begin(), seats.end(), [&](const Rect& q) {
                return std::fabs(q.cx() - w.r.cx()) < q.w * 0.3f && std::fabs(q.cy() - w.r.cy()) < q.w * 0.3f; }))
            continue;
        seats.push_back(w.r);
        const Rect dial = m.toFrame(w.r);
        const float cx = dial.cx(), cy = dial.cy(), R = dial.w * 0.5f;
        paint(dial.inset(-R * 0.30f), [&](float x, float y, float* c) {
            const float dx = x - cx - R * 0.04f, dy = y - cy - R * 0.07f, dd = std::sqrt(dx * dx + dy * dy) / R;
            if (dd < 1.15f) { const float sh = 1.0f - 0.55f * (1.15f - dd) / 1.15f; for (int q = 0; q < 3; ++q) c[q] *= sh; }
            const float rx = x - cx, ry = y - cy, rr = std::sqrt(rx * rx + ry * ry) / R;
            const float ang = std::atan2(rx, -ry);   // 0 = up, clockwise
            constexpr float kMin = -0.75f * 3.14159265f, kMax = 0.75f * 3.14159265f;
            if (ang < kMin - 0.05f || ang > kMax + 0.05f) return;
            const float step = (kMax - kMin) / 10.0f;
            const float idx = (ang - kMin) / step, nearest = std::round(idx);
            const bool major = nearest == 0.0f || nearest == 10.0f || nearest == 5.0f;
            if (rr < 1.06f || rr > (major ? 1.22f : 1.17f)) return;
            const float distPx = std::fabs(idx - nearest) * step * rr * R;
            const float a = std::clamp(1.0f - (distPx - 1.2f * s) / 1.2f, 0.0f, 1.0f) * (major ? 0.85f : 0.6f);
            for (int q = 0; q < 3; ++q) c[q] = c[q] * (1.0f - a) + 0.62f * a;
        });
    }
    return img;
}

} // namespace hem::ui
