"""HEM 'Obsidian Glass' look for the Suno Repair window.

Everything is drawn here with Pillow at the screen's real pixel size (DPI-aware), so it stays sharp
on a 4K monitor: studio-bokeh backdrop, gold-rimmed obsidian frame, glass panels, bevelled metal
buttons, the gold EXECUTE bar and the green terminal well. The window code places live widgets
(text boxes, log) on top of these pictures using LAYOUT.
"""
from __future__ import annotations

import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

BASE_W, BASE_H = 1180, 790          # design size at 96 DPI (scaled by s)
GOLD = (212, 160, 74)
GOLD_HI = (255, 214, 138)
GOLD_LO = (120, 78, 28)
TEXT = (236, 232, 224)
GREEN = (104, 236, 120)

# Base-unit rectangles (x0, y0, x1, y1); multiplied by s at draw time.
LAYOUT = {
    "frame": (20, 14, 1160, 776),
    "header": (38, 30, 1142, 98),
    "inputs": (44, 114, 1136, 404),
    "rows": {  # label x, field box, button box
        "stems":  (70, 134, (272, 134, 852, 178), (868, 134, 1110, 178)),
        "mix":    (70, 188, (272, 188, 852, 232), (868, 188, 1110, 232)),
        "target": (70, 242, (272, 242, 852, 286), None),
        "model":  (70, 296, (272, 296, 852, 340), None),
        "key":    (70, 350, (272, 350, 852, 394), (868, 350, 1110, 394)),
    },
    "execute_well": (44, 420, 1136, 512),
    "execute": (60, 432, 1120, 500),
    "status": (44, 528, 1136, 762),
    "status_text": (210, 556),
    "terminal": (64, 590, 1116, 706),
    "open_btn": (880, 716, 1116, 752),
}

_FONT_DIR_CANDIDATES = []


def font_dir() -> Path | None:
    import sys
    here = Path(__file__).resolve().parent
    for d in (Path(getattr(sys, "_MEIPASS", here)) / "fonts", here / "fonts",
              here.parent.parent.parent / "assets" / "brand"):
        if (d / "Poppins-Bold.ttf").exists():
            return d
    return None


def font(weight: str, px: int) -> ImageFont.FreeTypeFont:
    d = font_dir()
    name = {"bold": "Poppins-Bold.ttf", "semi": "Poppins-SemiBold.ttf", "medium": "Poppins-Medium.ttf"}[weight]
    if d is not None:
        return ImageFont.truetype(str(d / name), px)
    for fallback in ("segoeuib.ttf", "arialbd.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(fallback, px)
        except OSError:
            continue
    return ImageFont.load_default()


# --------------------------------------------------------------------------- primitives

def _sc(box, s):
    return tuple(int(round(v * s)) for v in box)


def _mask(size, box, r, ss=3):
    """Anti-aliased rounded-rect mask (drawn 3x, scaled down)."""
    W, H = size
    m = Image.new("L", (W * ss, H * ss), 0)
    ImageDraw.Draw(m).rounded_rectangle([v * ss for v in box], radius=r * ss, fill=255)
    return m.resize(size, Image.LANCZOS)


def _vgrad(size, stops):
    """Vertical gradient. stops = [(pos 0..1, (r,g,b)), ...]."""
    W, H = size
    y = np.linspace(0, 1, max(H, 1))[:, None]
    out = np.zeros((H, W, 3))
    pos = [p for p, _ in stops]
    for c in range(3):
        col = np.interp(y[:, 0], pos, [rgb[c] for _, rgb in stops])
        out[:, :, c] = col[:, None]
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGB")


def _paste(base, layer, mask):
    base.paste(layer, (0, 0), mask)


def _shadow(base, box, r, s, blur=14, offset=(0, 6), alpha=170):
    W, H = base.size
    m = _mask((W, H), (box[0] + offset[0] * s, box[1] + offset[1] * s, box[2] + offset[0] * s, box[3] + offset[1] * s), r)
    m = m.filter(ImageFilter.GaussianBlur(blur * s)).point(lambda v: v * alpha // 255)
    base.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), m)


def _stroke(base, box, r, color, width, alpha=255):
    W, H = base.size
    outer = _mask((W, H), box, r)
    inner = _mask((W, H), (box[0] + width, box[1] + width, box[2] - width, box[3] - width), max(r - width, 0))
    ring = ImageChops.subtract(outer, inner).point(lambda v: v * alpha // 255)
    base.paste(Image.new("RGB", (W, H), color), (0, 0), ring)


def _gold_stroke(base, box, r, width):
    """A metallic gold rim: gradient (bright top, dark bottom) through a ring mask."""
    W, H = base.size
    outer = _mask((W, H), box, r)
    inner = _mask((W, H), (box[0] + width, box[1] + width, box[2] - width, box[3] - width), max(r - width, 0))
    ring = ImageChops.subtract(outer, inner)
    g = _vgrad((W, H), [(0, GOLD_HI), (box[1] / H + 0.02, GOLD), (box[3] / H, GOLD_LO), (1, GOLD_LO)])
    base.paste(g, (0, 0), ring)


def _glass(base, box, r, s, tint=(22, 22, 26), alpha=210):
    """Smoked-glass panel: dark fill, top sheen, diagonal reflection band, fine rim."""
    W, H = base.size
    x0, y0, x1, y1 = box
    m = _mask((W, H), box, r)
    fill = _vgrad((W, H), [(0, (tint[0] + 14, tint[1] + 14, tint[2] + 16)), (y0 / H, (tint[0] + 12, tint[1] + 12, tint[2] + 14)),
                           (y1 / H, (tint[0] - 8, tint[1] - 8, tint[2] - 6)), (1, tint)])
    base.paste(fill, (0, 0), m.point(lambda v: v * alpha // 255))
    # diagonal reflection band
    band = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(band)
    bx = x0 + (x1 - x0) * 0.58
    d.polygon([(bx, y0), (bx + 120 * s, y0), (bx + 40 * s, y1), (bx - 80 * s, y1)], fill=26)
    band = band.filter(ImageFilter.GaussianBlur(8 * s))
    band = ImageChops.multiply(band, m)
    base.paste(Image.new("RGB", (W, H), (255, 255, 255)), (0, 0), band)
    # top sheen line + dark bottom edge
    _stroke(base, box, r, (90, 84, 74), max(1, int(1.2 * s)), alpha=200)
    top = Image.new("L", (W, H), 0)
    ImageDraw.Draw(top).line([(x0 + r, y0 + 1.5 * s), (x1 - r, y0 + 1.5 * s)], fill=120, width=max(1, int(1.2 * s)))
    base.paste(Image.new("RGB", (W, H), (255, 236, 200)), (0, 0), top.filter(ImageFilter.GaussianBlur(0.6 * s)))


def _well(base, box, r, s, color=(10, 10, 12), rim=(60, 54, 44)):
    """Recessed field: dark fill with inner shadow at the top."""
    W, H = base.size
    m = _mask((W, H), box, r)
    base.paste(Image.new("RGB", (W, H), color), (0, 0), m)
    inner = ImageChops.subtract(m, _mask((W, H), (box[0], box[1] + 3 * s, box[2], box[3] + 3 * s), r))
    inner = inner.filter(ImageFilter.GaussianBlur(2.5 * s)).point(lambda v: min(255, v * 2))
    inner = ImageChops.multiply(inner, m)
    base.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), inner)
    _stroke(base, box, r, rim, max(1, int(1 * s)))


def _text(base, xy, txt, fnt, fill, emboss=True, anchor="la", shadow=True):
    d = ImageDraw.Draw(base)
    x, y = xy
    if shadow:
        d.text((x, y + max(1, fnt.size // 18)), txt, font=fnt, fill=(0, 0, 0), anchor=anchor)
    if emboss:
        d.text((x, y - max(1, fnt.size // 40)), txt, font=fnt, fill=(255, 255, 255), anchor=anchor)
    d.text((x, y), txt, font=fnt, fill=fill, anchor=anchor)


def _gold_text(base, xy, txt, fnt, anchor="la"):
    """Gold gradient lettering with a dark drop shadow."""
    W, H = base.size
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).text(xy, txt, font=fnt, fill=255, anchor=anchor)
    bb = m.getbbox() or (0, 0, W, H)
    sh = m.filter(ImageFilter.GaussianBlur(max(1, fnt.size // 14)))
    base.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), ImageChops.offset(sh, 0, max(1, fnt.size // 16)))
    g = _vgrad((W, H), [(0, GOLD_HI), (bb[1] / H, GOLD_HI), ((bb[1] + bb[3]) / 2 / H, GOLD), (bb[3] / H, (176, 116, 44)), (1, GOLD_LO)])
    base.paste(g, (0, 0), m)


# --------------------------------------------------------------------------- backdrop

def _backdrop(W, H, s):
    rng = random.Random(108)
    bg = _vgrad((W, H), [(0, (30, 24, 20)), (0.45, (16, 14, 14)), (1, (8, 8, 9))])
    glow = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(glow)
    # warm studio lights (soft bokeh), a few cool monitor glows
    for _ in range(46):
        x, y = rng.uniform(0, W), rng.uniform(0, H * 0.75)
        rad = rng.uniform(6, 34) * s
        warm = rng.random() < 0.8
        c = (rng.randint(150, 230), rng.randint(95, 150), rng.randint(40, 80)) if warm else (60, 110, rng.randint(150, 210))
        k = rng.uniform(0.15, 0.45)
        d.ellipse([x - rad, y - rad, x + rad, y + rad], fill=tuple(int(v * k) for v in c))
    glow = glow.filter(ImageFilter.GaussianBlur(10 * s))
    bg = ImageChops.add(bg, glow)
    # ceiling spot
    spot = Image.new("L", (W, H), 0)
    ImageDraw.Draw(spot).ellipse([W * 0.42, -H * 0.25, W * 0.58, H * 0.12], fill=90)
    bg.paste(Image.new("RGB", (W, H), (255, 220, 170)), (0, 0), spot.filter(ImageFilter.GaussianBlur(40 * s)))
    # vignette
    yy, xx = np.mgrid[0:H, 0:W]
    v = 1.0 - 0.75 * (((xx - W / 2) / (W / 1.6)) ** 2 + ((yy - H / 2) / (H / 1.4)) ** 2)
    arr = (np.asarray(bg).astype(float) * v.clip(0.15, 1)[:, :, None]).clip(0, 255).astype(np.uint8)
    # fine film grain
    noise = np.random.default_rng(3).normal(0, 2.2, (H, W, 1))
    return Image.fromarray((arr + noise).clip(0, 255).astype(np.uint8), "RGB")


# --------------------------------------------------------------------------- the window art

LABELS = {"stems": "Stems:", "mix": "Original Mix:", "target": "Loudness Target:", "model": "Claude Model:", "key": "Claude API Key:"}


def render_chrome(s: float) -> Image.Image:
    W, H = int(BASE_W * s), int(BASE_H * s)
    img = _backdrop(W, H, s)
    L = {k: _sc(v, s) for k, v in LAYOUT.items() if isinstance(v, tuple) and len(v) == 4}

    # main obsidian frame with gold rim
    fr = L["frame"]
    _shadow(img, fr, int(18 * s), s, blur=26, offset=(0, 14), alpha=220)
    m = _mask((W, H), fr, int(18 * s))
    img.paste(_vgrad((W, H), [(0, (34, 32, 31)), (fr[1] / H, (30, 28, 27)), (0.5, (20, 19, 19)), (fr[3] / H, (13, 13, 13)), (1, (10, 10, 10))]), (0, 0), m)
    _gold_stroke(img, fr, int(18 * s), max(2, int(2.5 * s)))

    # header plate (brushed metal) with title
    hd = L["header"]
    _shadow(img, hd, int(10 * s), s, blur=6, offset=(0, 4), alpha=200)
    hm = _mask((W, H), hd, int(10 * s))
    plate = _vgrad((W, H), [(0, (90, 88, 86)), (hd[1] / H, (78, 76, 74)), ((hd[1] + hd[3]) / 2 / H, (46, 45, 44)), (hd[3] / H, (28, 27, 27)), (1, (20, 20, 20))])
    streak = np.random.default_rng(5).normal(0, 5, (1, W, 1)).repeat(H, 0)
    plate = Image.fromarray((np.asarray(plate).astype(float) + streak).clip(0, 255).astype(np.uint8), "RGB")
    img.paste(plate, (0, 0), hm)
    _stroke(img, hd, int(10 * s), (150, 140, 122), max(1, int(1.2 * s)))
    title_f = font("bold", int(30 * s))
    cy = (hd[1] + hd[3]) // 2
    _text(img, (hd[0] + int(26 * s), cy), "HYBRID ELITE MAFIA · SUNO REPAIR SUITE", title_f, TEXT, anchor="lm")
    _gold_text(img, (hd[2] - int(24 * s), cy), "[ 4K OBSIDIAN GLASS EDITION ]", font("semi", int(24 * s)), anchor="rm")

    # input glass panel + labels + field wells
    _glass(img, L["inputs"], int(12 * s), s)
    lab_f = font("semi", int(19 * s))
    for k, (lx, ly, field, btn) in LAYOUT["rows"].items():
        f = _sc(field, s)
        _text(img, (int(lx * s), (f[1] + f[3]) // 2), LABELS[k], lab_f, TEXT, anchor="lm")
        _well(img, f, int(8 * s), s, rim=(GOLD[0] // 2 + 20, GOLD[1] // 2 + 10, 40) if k == "target" else (64, 58, 48))
        if k == "target":   # dropdown arrow box
            ab = (f[2] - int(46 * s), f[1] + int(4 * s), f[2] - int(4 * s), f[3] - int(4 * s))
            _well(img, ab, int(6 * s), s, color=(26, 24, 22), rim=(90, 74, 46))
            ax, ay = (ab[0] + ab[2]) / 2, (ab[1] + ab[3]) / 2
            ImageDraw.Draw(img).polygon([(ax - 9 * s, ay - 5 * s), (ax + 9 * s, ay - 5 * s), (ax, ay + 7 * s)], fill=GOLD)

    # execute slot
    _well(img, L["execute_well"], int(14 * s), s, color=(14, 13, 12), rim=(70, 64, 54))

    # status glass panel + label + terminal well
    _glass(img, L["status"], int(12 * s), s)
    st = LAYOUT["status_text"]
    _text(img, (int(70 * s), int(st[1] * s)), "Status:", font("semi", int(21 * s)), TEXT, anchor="lm")
    _well(img, L["terminal"], int(8 * s), s, color=(7, 20, 11), rim=(40, 70, 46))
    return img


def metal_button(w: int, h: int, label: str, s: float, state: str = "normal") -> Image.Image:
    """Dark bevelled metal button. state: normal | hover | pressed | disabled."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    base = Image.new("RGB", (w, h), (0, 0, 0))
    top, bot = {"normal": ((78, 76, 74), (30, 29, 29)), "hover": ((104, 98, 90), (40, 38, 36)),
                "pressed": ((28, 27, 27), (58, 56, 54)), "disabled": ((44, 43, 42), (26, 26, 26))}[state]
    base.paste(_vgrad((w, h), [(0, top), (0.5, tuple((a + b) // 2 for a, b in zip(top, bot))), (1, bot)]))
    r = int(8 * s)
    m = _mask((w, h), (0, 0, w - 1, h - 1), r)
    _stroke(base, (0, 0, w - 1, h - 1), r, (150, 136, 110) if state != "disabled" else (70, 66, 60), max(1, int(1.2 * s)))
    hl = Image.new("L", (w, h), 0)
    ImageDraw.Draw(hl).line([(r, 2 * s), (w - r, 2 * s)], fill=110 if state != "pressed" else 30, width=max(1, int(s)))
    base.paste(Image.new("RGB", (w, h), (255, 240, 210)), (0, 0), hl)
    px = int(17 * s)
    f = font("semi", px)
    while px > 8 and f.getlength(label) > w - 18 * s:
        px -= 1
        f = font("semi", px)
    col = TEXT if state != "disabled" else (120, 116, 110)
    _text(base, (w // 2, h // 2 + (int(s) if state == "pressed" else 0)), label, f, col, emboss=False, anchor="mm")
    img.paste(base, (0, 0), m)
    return img


def gold_bar(w: int, h: int, label: str, s: float, state: str = "normal") -> Image.Image:
    """The big polished-gold EXECUTE bar with engraved dark lettering."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    k = {"normal": 1.0, "hover": 1.12, "pressed": 0.82, "disabled": 0.45}[state]

    def c(rgb):
        return tuple(min(255, int(v * k)) for v in rgb)
    if state == "disabled":
        stops = [(0, (96, 92, 86)), (0.5, (70, 68, 64)), (1, (40, 39, 37))]
    else:
        stops = [(0, c((250, 214, 150))), (0.18, c((236, 178, 104))), (0.5, c((255, 232, 186))),
                 (0.56, c((222, 150, 72))), (0.85, c((176, 104, 40))), (1, c((120, 70, 26)))]
    base = _vgrad((w, h), stops)
    # horizontal sheen
    x = np.linspace(-1, 1, w)
    sheen = (1 - np.abs(x) ** 2.2) * (40 if state != "disabled" else 10)
    arr = np.asarray(base).astype(float) + sheen[None, :, None]
    base = Image.fromarray(arr.clip(0, 255).astype(np.uint8), "RGB")
    r = int(10 * s)
    m = _mask((w, h), (0, 0, w - 1, h - 1), r)
    _stroke(base, (0, 0, w - 1, h - 1), r, (255, 236, 196) if state != "disabled" else (120, 116, 110), max(1, int(1.5 * s)), alpha=220)
    _stroke(base, (int(2 * s), int(2 * s), w - 1 - int(2 * s), h - 1 - int(2 * s)), r, (110, 62, 20), max(1, int(1 * s)), alpha=160)
    f = font("bold", int(34 * s))
    d = ImageDraw.Draw(base)
    cx, cy = w // 2, h // 2 + (int(s) if state == "pressed" else 0)
    d.text((cx, cy + max(1, int(1.5 * s))), label, font=f, fill=(255, 238, 205) if state != "disabled" else (110, 106, 100), anchor="mm")  # engraved highlight
    d.text((cx, cy), label, font=f, fill=(58, 30, 10) if state != "disabled" else (40, 38, 36), anchor="mm")
    img.paste(base, (0, 0), m)
    if state == "hover":   # outer glow
        g = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        return Image.alpha_composite(g, img)
    return img


def toggle(w: int, h: int, label: str, on: bool, s: float) -> Image.Image:
    """Gold 'Show' pill with a check box."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    base = _vgrad((w, h), [(0, (226, 180, 104)), (0.5, (196, 146, 72)), (1, (130, 88, 36))]) if on else \
        _vgrad((w, h), [(0, (70, 68, 64)), (1, (34, 33, 32))])
    r = int(8 * s)
    m = _mask((w, h), (0, 0, w - 1, h - 1), r)
    _stroke(base, (0, 0, w - 1, h - 1), r, (255, 230, 180) if on else (120, 110, 96), max(1, int(1.2 * s)))
    bs = int(h * 0.62)
    bx, by = int(10 * s), (h - bs) // 2
    ImageDraw.Draw(base).rounded_rectangle([bx, by, bx + bs, by + bs], radius=int(4 * s), fill=(22, 20, 18), outline=(200, 160, 90), width=max(1, int(s)))
    if on:
        d = ImageDraw.Draw(base)
        d.line([(bx + bs * 0.22, by + bs * 0.52), (bx + bs * 0.42, by + bs * 0.74), (bx + bs * 0.8, by + bs * 0.26)],
               fill=GOLD_HI, width=max(2, int(3 * s)), joint="curve")
    _text(base, (bx + bs + int(12 * s), h // 2), label, font("semi", int(18 * s)), (40, 26, 12) if on else TEXT, emboss=False, shadow=not on, anchor="lm")
    img.paste(base, (0, 0), m)
    return img
