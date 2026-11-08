"""Rebuilds the work area of a HEM-Elite-Rack plate picture for a new layout (Linux helper, not shipped).

    python3 compose.py <old.txt> <new.txt> <plates dir> <out dir> NAME [NAME ...]

The header (logo, crown, screws), the frame and the footer stay as painted. Below the header line the metal is made
fresh: the plate's own light (blurred from the picture with the pockets left out) plus brushed grain with the same
streak length and strength as the picture's own grain. Then the grooves are drawn with the picture's own groove
cross-section, and every pocket is laid in from the original picture (a pocket that did not move is laid back
exactly where it was; a new one is cut from the closest pocket of its kind, 9-slice stretched so the bevel keeps
its width). Nothing is copied from beside a hole, so no logo or edge can end up in the middle of the plate.
"""
import sys, os
import numpy as np
from PIL import Image, ImageFilter, ImageDraw
from scipy import ndimage, signal
from replate import load, px, rect_px, nine_slice, pocket_mask, PLATE_W, MARGIN

TOP = 172.0      # plate units: just under the header groove (y 166)
BOTTOM = 806.0   # just above the main panel's lower edge
LEFT, RIGHT = 26.0, 1646.0
FEATHER = 6.0


def gray(a):
    return a.astype(np.float32).mean(axis=2)


def busy_mask(o, n, s, H, W):
    """Pixels of the original that are pocket or groove (old layout), grown a little."""
    m = np.zeros((H, W), bool)
    for r in o['R'] + n['R']:
        x0, y0, x1, y1 = rect_px(r, s, MARGIN + 6.0)
        m[max(y0, 0):y1, max(x0, 0):x1] = True
    for g in o['G']:
        x0, y0, x1, y1 = g
        a, b = px(min(x0, x1) - 8, s), px(max(x0, x1) + 8, s)
        c, d = px(min(y0, y1) - 8, s), px(max(y0, y1) + 8, s)
        m[max(c, 0):d, max(a, 0):b] = True
    return m


def light(img, busy, s):
    """The plate's light: a smooth surface (2-D polynomial) fitted to the plain metal only, with a second pass that
    leaves out anything darker or lighter than metal (painted shapes the layout file doesn't know about). Smooth by
    construction, so it can't pick up a smudge."""
    H, W, _ = img.shape
    step = 8
    ys, xs = np.mgrid[0:H:step, 0:W:step]
    k = ~busy[::step, ::step]
    v = img[::step, ::step].astype(np.float32)
    u, w = xs / W * 2 - 1, ys / H * 2 - 1
    deg = 4
    terms = [(i, j) for i in range(deg + 1) for j in range(deg + 1 - i)]
    def basis(uu, ww):
        return np.stack([uu ** i * ww ** j for i, j in terms], -1)
    B = basis(u[k], w[k])
    out = np.empty(img.shape, np.float32)
    bright = v.mean(axis=2)[k]
    keep = np.ones(len(bright), bool)
    for _ in range(3):
        c, *_ = np.linalg.lstsq(B[keep], bright[keep], rcond=None)
        r = bright - B @ c
        sd = r[keep].std()
        keep = np.abs(r) < 2.0 * sd
    U, Wv = np.meshgrid(np.arange(W) / W * 2 - 1, np.arange(H) / H * 2 - 1)
    full = np.zeros((H, W), np.float32)
    for (i, j), cc in zip(terms, c):
        full += cc * (U ** i) * (Wv ** j)
    tint = v[k][keep].mean(axis=0) - v[k][keep].mean()   # the metal's small colour cast, the same everywhere
    for ch in range(3):
        out[..., ch] = full + tint[ch]
    return out


def clean_patch(busy, area, ph, pw):
    """Top-left of a ph x pw block of plain metal inside the work area (largest-first search)."""
    bad = (busy | ~area).astype(np.int32)
    ii = np.pad(bad.cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    H, W = bad.shape
    for w in (pw, pw * 3 // 4, pw // 2, pw // 3):
        for h in (ph, ph * 3 // 4, ph // 2):
            if h > H or w > W:
                continue
            s = ii[h:, w:] - ii[:-h, w:] - ii[h:, :-w] + ii[:-h, :-w]
            ys, xs = np.nonzero(s == 0)
            if len(ys):
                k = len(ys) // 2
                return ys[k], xs[k], h, w
    return None


def grain(img, lit, busy, area, s, H, W, seed):
    """Brushed grain with the picture's own streak shape: white noise through a filter taken from the picture's
    own grain (its autocorrelation), scaled to the same strength."""
    p = clean_patch(busy, area, px(70, s), px(420, s))
    if p is None:
        raise RuntimeError('no plain metal to learn the grain from')
    y, x, h, w = p
    res = gray(img[y:y + h, x:x + w]) - lit[y:y + h, x:x + w].mean(axis=2)
    res -= res.mean()
    std = res.std()
    win = np.outer(np.hanning(h), np.hanning(w))
    A = np.abs(np.fft.fft2(res * win))
    k = np.real(np.fft.fftshift(np.fft.ifft2(A)))   # zero-phase filter with the same spectrum
    kh, kw = min(h, 41) | 1, min(w, 401) | 1
    cy, cx = h // 2, w // 2
    k = k[cy - kh // 2:cy + kh // 2 + 1, cx - kw // 2:cx + kw // 2 + 1]
    k *= np.outer(np.hanning(kh + 2)[1:-1], np.hanning(kw + 2)[1:-1])
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal((H + kh, W + kw)).astype(np.float32)
    g = signal.fftconvolve(noise, k.astype(np.float32), mode='same')[kh // 2:kh // 2 + H, kw // 2:kw // 2 + W]
    g -= g.mean()
    g *= std / max(g.std(), 1e-6)
    return g, std, (y, x, h, w)


def groove_cross(img, grooves, s, vertical):
    """The picture's own groove cross-section (across the line), averaged along a stretch of it, minus plain metal."""
    best = None
    for g in grooves:
        x0, y0, x1, y1 = g
        isv = abs(x0 - x1) < 0.5
        if isv != vertical:
            continue
        L = abs(y1 - y0) if isv else abs(x1 - x0)
        if best is None or L > best[0]:
            best = (L, g)
    if best is None:
        return None
    x0, y0, x1, y1 = best[1]
    half = px(6, s)
    if vertical:
        cx = px(x0, s)
        a, b = px(min(y0, y1) + 20, s), px(max(y0, y1) - 20, s)
        strip = img[a:b, cx - half:cx + half + 1].astype(np.float32)
        prof = np.median(strip, axis=0)                    # (2h+1, 3)
    else:
        cy = px(y0, s)
        a, b = px(min(x0, x1) + 40, s), px(max(x0, x1) - 40, s)
        strip = img[cy - half:cy + half + 1, a:b].astype(np.float32)
        prof = np.median(strip, axis=1)
    edge = np.concatenate([prof[:2], prof[-2:]]).mean(axis=0)
    return prof - edge    # (2h+1, 3) difference from plain metal


def lay_groove(out, prof, g, s, H, W):
    x0, y0, x1, y1 = g
    n = prof.shape[0]
    hh = n // 2
    if abs(y0 - y1) < 0.5:
        a, b = px(min(x0, x1), s), px(max(x0, x1), s)
        y = px(y0, s)
        ramp = np.clip(np.minimum(np.arange(b - a) + 1, b - a - np.arange(b - a)) / 2.0, 0, 1)[None, :, None]
        out[y - hh:y - hh + n, a:b] += prof[:, None, :] * ramp
    else:
        a, b = px(min(y0, y1), s), px(max(y0, y1), s)
        x = px(x0, s)
        ramp = np.clip(np.minimum(np.arange(b - a) + 1, b - a - np.arange(b - a)) / 2.0, 0, 1)[:, None, None]
        out[a:b, x - hh:x - hh + n] += prof[None, :, :] * ramp


def choose_source(pool, r):
    """Closest pocket of the same kind among all plates' original pockets (pool: list of (plate img, s, rect))."""
    x, y, w, h, rad, rnd, glass = r
    best, score = None, 1e18
    for item in pool:
        o = item[2]
        if o[5] != rnd:
            continue
        sc = 0.0
        if o[6] != glass:
            sc += 5000
        if item[5] == 2:       # a groove runs inside its margin: a bit of it would come along
            sc += 4000
        elif item[5]:          # another pocket sits inside its margin: its edge would come along
            sc += 3000 if rnd != 2 else 1000   # a knob piece is cut to its seat and window, a neighbour mostly stays out
        if item[3]:            # same plate: prefer it (same light, same picture)
            sc -= 40 if rnd != 2 else 400
        if rnd == 2:
            sc += abs(o[2] - w) * 12 + abs(o[3] - h) * 12
        else:
            sc += abs(o[3] - h) * 30 + abs(o[2] - w) * 1.0 + abs(o[4] - rad) * 20
            if o[2] < w * 0.5:   # stretching a short one far wide shows
                sc += (w * 0.5 - o[2]) * 10
        if sc < score:
            best, score = item, sc
    return best


_LIGHT = {}


def plate_light(nm, img, layout, s):
    if nm not in _LIGHT:
        H, W, _ = img.shape
        _LIGHT[nm] = light(img, busy_mask(layout, layout, s, H, W), s).mean(axis=2)
    return _LIGHT[nm]


def compose(name, old, new, plates, outdir, pool):
    path = os.path.join(plates, 'HEM_' + name.replace(' ', '_') + '.jpg')
    img = np.asarray(Image.open(path).convert('RGB')).copy()
    H, W, _ = img.shape
    s = W / PLATE_W
    o, n = old[name], new[name]
    busy = busy_mask(o, n, s, H, W)
    area = np.zeros((H, W), bool)
    area[px(TOP, s):px(BOTTOM, s), px(LEFT, s):px(RIGHT, s)] = True
    lit = light(img, busy, s)
    _LIGHT[name] = lit.mean(axis=2)
    g, std, patch = grain(img, lit, busy, area, s, H, W, seed=hash(name) & 0xffff)
    fresh = lit + g[..., None]
    cov = np.zeros((H, W), np.float32)   # how much of each pixel is pocket
    goneG = [g for g in o['G'] if g not in n['G']]
    # pockets
    for r in n['R']:
        if r[1] + r[3] < TOP or r[1] > BOTTOM:
            continue
        mx = MARGIN + 6
        near_gone = any(min(g[0], g[2]) - 2 < r[0] + r[2] + mx and max(g[0], g[2]) + 2 > r[0] - mx and
                        min(g[1], g[3]) - 2 < r[1] + r[3] + mx and max(g[1], g[3]) + 2 > r[1] - mx for g in goneG)
        if r in o['R'] and not near_gone:   # a pocket that stayed put, with nothing gone from around it
            src = (img, s, r, True)
        else:
            src = choose_source(pool, r)
            if src is None:
                continue
        simg, ss, sr = src[0], src[1], src[2]
        sx0, sy0, sx1, sy1 = rect_px(sr, ss, MARGIN)
        piece = simg[max(sy0, 0):sy1, max(sx0, 0):sx1]
        tx0, ty0, tx1, ty1 = rect_px(r, s, MARGIN)
        tw, th = tx1 - tx0, ty1 - ty0
        if piece.shape[1] == tw and piece.shape[0] == th:
            pic = piece.astype(np.float32)
        elif r[5]:
            pic = np.asarray(Image.fromarray(piece).resize((tw, th), Image.LANCZOS)).astype(np.float32)
        else:
            pic = nine_slice(piece, tw, th, px(MARGIN + max(r[4], sr[4]) + 2, s)).astype(np.float32)
        mask = pocket_mask(r, s, tx0, ty0, tw, th)
        # level the piece to the fresh metal under it (outside the pocket shape, where the piece is plain metal)
        y0c, x0c, y1c, x1c = max(ty0, 0), max(tx0, 0), min(ty1, H), min(tx1, W)
        sub = pic[y0c - ty0:y1c - ty0, x0c - tx0:x1c - tx0]
        mk = mask[y0c - ty0:y1c - ty0, x0c - tx0:x1c - tx0]
        base = fresh[y0c:y1c, x0c:x1c]
        # level the piece by the two plates' own light (where it was cut vs where it goes), so neighbours inside
        # the cut can't throw it off
        if not (src[3] and sr == r):
            sl = plate_light(src[4], simg, old[src[4]], ss)
            a = sl[max(sy0, 0):sy1, max(sx0, 0):sx1].mean()
            b = lit[y0c:y1c, x0c:x1c].mean()
            sub = sub + float(np.clip(b - a, -30, 30))
        fresh[y0c:y1c, x0c:x1c] = base * (1 - mk[..., None]) + sub * mk[..., None]
        cov[y0c:y1c, x0c:x1c] = np.maximum(cov[y0c:y1c, x0c:x1c], mk)
    # grooves
    ph = groove_cross(img, o['G'], s, False)
    pv = groove_cross(img, o['G'], s, True)
    if pv is None:
        pv = ph
    for gg in n['G']:
        x0, y0, x1, y1 = gg
        horiz = abs(y0 - y1) < 0.5
        if y0 < TOP - 1:          # the header groove stays as painted
            continue
        layer = np.zeros_like(fresh)
        lay_groove(layer, ph if horiz else pv, gg, s, H, W)
        fresh += layer * (1.0 - np.clip((cov - 0.6) * 3.0, 0, 1))[..., None]   # a groove never runs over a pocket
    # put the fresh work area into the picture, feathered at its edge
    m = area.astype(np.float32)
    m = ndimage.gaussian_filter(m, px(FEATHER, s) / 2.0)
    m[~ndimage.binary_dilation(area, iterations=px(FEATHER, s))] = 0
    res = img.astype(np.float32) * (1 - m[..., None]) + fresh * m[..., None]
    os.makedirs(outdir, exist_ok=True)
    Image.fromarray(np.clip(res, 0, 255).astype(np.uint8)).save(os.path.join(outdir, os.path.basename(path)), quality=93)
    return dict(grain_std=round(float(std), 2), patch=patch)


def build_pool(old, plates, names):
    pool = []
    cache = {}
    for nm in names:
        p = os.path.join(plates, 'HEM_' + nm.replace(' ', '_') + '.jpg')
        if not os.path.exists(p) or nm not in old:
            continue
        cache[nm] = np.asarray(Image.open(p).convert('RGB'))
        s = cache[nm].shape[1] / PLATE_W
        rs = old[nm]['R']
        for r in rs:
            m = MARGIN + 4
            crowded = any(q is not r and q[0] < r[0] + r[2] + m and q[0] + q[2] > r[0] - m and
                          q[1] < r[1] + r[3] + m and q[1] + q[3] > r[1] - m for q in rs)
            gm = MARGIN + 6
            grooved = any(min(g[0], g[2]) - 2 < r[0] + r[2] + gm and max(g[0], g[2]) + 2 > r[0] - gm and
                          min(g[1], g[3]) - 2 < r[1] + r[3] + gm and max(g[1], g[3]) + 2 > r[1] - gm for g in old[nm]['G'])
            crowded = 2 if grooved else (1 if crowded else 0)
            pool.append((cache[nm], s, r, False, nm, crowded))
    return pool


if __name__ == '__main__':
    old, new = load(sys.argv[1]), load(sys.argv[2])
    plates, outdir = sys.argv[3], sys.argv[4]
    names = sys.argv[5:]
    # pockets may come from any plate whose picture matches its layout (unchanged plates) or from the plate itself
    unchanged = [k for k in old if k in new and old[k] == new[k]]
    base_pool = build_pool(old, plates, unchanged[:40])
    for name in names:
        pool = base_pool + [(t[0], t[1], t[2], True, t[4], t[5]) for t in build_pool(old, plates, [name])]
        print(name, compose(name, old, new, plates, outdir, pool))
