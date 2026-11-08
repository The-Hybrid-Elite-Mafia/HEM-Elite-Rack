"""Moves the pockets on a HEM-Elite-Rack faceplate picture to a new layout (Linux helper, not shipped).

    python3 replate.py <old.txt> <new.txt> <plates dir> <out dir> NAME [NAME ...]

old.txt / new.txt: every plug-in's pockets (R) and grooves (G) in plate units, from the old and new layout code.
For each plate: pockets and grooves that did not move are left as they are; the ones that moved or went away are
filled with the brushed metal around them (copied along the same row, which is how the brushing runs); the new
ones are cut from the same picture (the pocket of that kind and size closest to the new one, 9-slice stretched so
the bevel keeps its width) and laid in.
"""
import sys, os
import numpy as np
from PIL import Image, ImageFilter

PLATE_W, PLATE_H = 1672.0, 941.0
MARGIN = 11.0         # plate units around a pocket that belong to it (bevel, shadow)
GROOVE_HALF = 5.0     # plate units each side of a groove line


def load(path):
    d = {}
    for line in open(path):
        name, kind, rest = line.rstrip('\n').split('|', 2)
        d.setdefault(name, {'R': [], 'G': []})
        if kind == 'K':   # a knob: its seat and the value window under it, as one piece of the picture
            rect, small = rest.split('|')
            x, y, w, h = map(float, rect.split())
            es, eb = max(13.0, 0.23 * w), max(44.0, 0.55 * h)
            d[name]['R'].append((round(x - es, 1), round(y - 8, 1), round(w + 2 * es, 1), round(h + 8 + eb, 1), 0.0, 2, 0))
        elif kind == 'R':
            rect, extra = rest.split('|')
            x, y, w, h = map(float, rect.split())
            rad, rnd, glass = extra.split()
            if int(rnd) == 1:   # a round seat: part of a knob piece unless no knob piece holds it (a big knob)
                d[name].setdefault('round', []).append((round(x, 1), round(y, 1), round(w, 1), round(h, 1), float(rad), 1, int(glass)))
                continue
            d[name]['R'].append((round(x, 1), round(y, 1), round(w, 1), round(h, 1), float(rad), int(rnd), int(glass)))
        else:
            d[name]['G'].append(tuple(round(v, 1) for v in map(float, rest.split())))
    for v in d.values():
        for r in v.pop('round', []):
            cx, cy = r[0] + r[2] / 2, r[1] + r[3] / 2
            if not any(k[5] == 2 and k[0] <= cx <= k[0] + k[2] and k[1] <= cy <= k[1] + k[3] for k in v['R']):
                v['R'].append(r)
    return d


def px(v, s):
    return int(round(v * s))


def rect_px(r, s, m=0.0):
    x, y, w, h = r[:4]
    if len(r) > 5 and r[5] == 2:   # a knob piece already has its edges in it
        m = min(m, 4.0)
    return px(x - m, s), px(y - m, s), px(x + w + m, s), px(y + h + m, s)


def groove_px(g, s, half=GROOVE_HALF, ends=0.0):
    x0, y0, x1, y1 = g
    if abs(y0 - y1) < 0.5:   # horizontal
        return px(min(x0, x1) - half - ends, s), px(y0 - half, s), px(max(x0, x1) + half + ends, s), px(y0 + half, s)
    return px(x0 - half, s), px(min(y0, y1) - half - ends, s), px(x0 + half, s), px(max(y0, y1) + half + ends, s)


def fill_rows(img, hole, keep):
    """Fills each hole with a block of clean metal of its own size from right beside it (left / right first: the
    brushing runs along the rows, so a block from the same rows matches best; then above / below), leveled to the
    brightness around the hole and feathered in. A hole with no clean block of its size anywhere near is split in
    two and each half filled the same way."""
    from scipy import ndimage
    H, W, _ = img.shape
    bad = hole | keep
    out = img.copy()
    lum = img.astype(np.float32).mean(axis=2)

    def clean_box(y0, y1, x0, x1):
        return y0 >= 0 and x0 >= 0 and y1 <= H and x1 <= W and not bad[y0:y1, x0:x1].any()

    def fill_box(y0, y1, x0, x1, m, depth=0):
        h, w = y1 - y0, x1 - x0
        if h <= 0 or w <= 0 or not m.any():
            return
        src = None
        for gap in (3, 8, 16, 30, 60, 110, 200, 350):
            for dy, dx in ((0, -(w + gap)), (0, w + gap), (-(h + gap), 0), (h + gap, 0)):
                if clean_box(y0 + dy, y1 + dy, x0 + dx, x1 + dx):
                    src = (dy, dx)
                    break
            if src:
                break
        if src is None and depth < 14:   # split along the longer side
            if w >= h:
                c = w // 2
                fill_box(y0, y1, x0, x0 + c, m[:, :c], depth + 1)
                fill_box(y0, y1, x0 + c, x1, m[:, c:], depth + 1)
            else:
                c = h // 2
                fill_box(y0, y0 + c, x0, x1, m[:c], depth + 1)
                fill_box(y0 + c, y1, x0, x1, m[c:], depth + 1)
            return
        if src is None:
            return
        dy, dx = src
        block = img[y0 + dy:y1 + dy, x0 + dx:x1 + dx].astype(np.float32)
        # brightness: the metal just around the hole against the metal just around the source block
        r = 4
        ya, yb, xa, xb = max(y0 - r, 0), min(y1 + r, H), max(x0 - r, 0), min(x1 + r, W)
        ring = ~bad[ya:yb, xa:xb]
        ring[(y0 - ya):(y0 - ya) + h, (x0 - xa):(x0 - xa) + w] = False
        sya, syb, sxa, sxb = ya + dy, yb + dy, xa + dx, xb + dx
        if ring.any() and sya >= 0 and sxa >= 0 and syb <= H and sxb <= W:
            sring = ~bad[sya:syb, sxa:sxb] & ring
            if sring.any():
                off = float(np.median(lum[ya:yb, xa:xb][sring]) - np.median(lum[sya:syb, sxa:sxb][sring]))
                block += np.clip(off, -25, 25)
        cur = out[y0:y1, x0:x1].astype(np.float32)
        mm = m.astype(np.float32)[..., None]
        out[y0:y1, x0:x1] = np.clip(cur * (1 - mm) + block * mm, 0, 255).astype(np.uint8)

    lab, n = ndimage.label(hole)
    for i, sl in enumerate(ndimage.find_objects(lab), start=1):
        if sl is None:
            continue
        y0, y1, x0, x1 = sl[0].start, sl[0].stop, sl[1].start, sl[1].stop
        fill_box(y0, y1, x0, x1, lab[y0:y1, x0:x1] == i)
    # soften the seams a touch: a 3 px blur only inside the filled pixels' edges
    edge = ndimage.binary_dilation(hole, iterations=2) & ~ndimage.binary_erosion(hole, iterations=2)
    blur = np.asarray(Image.fromarray(out).filter(ImageFilter.GaussianBlur(1.2)))
    out[edge] = blur[edge]
    return out


def nine_slice(src, w, h, border):
    """Stretches a pocket to w x h keeping `border` px at each edge as they are."""
    sh, sw, _ = src.shape
    b = max(1, min(border, sw // 2 - 1, sh // 2 - 1, w // 2 - 1, h // 2 - 1))
    out = np.zeros((h, w, 3), dtype=src.dtype)
    xs_src = [0, b, sw - b, sw]
    ys_src = [0, b, sh - b, sh]
    xs_dst = [0, b, w - b, w]
    ys_dst = [0, b, h - b, h]
    for i in range(3):
        for j in range(3):
            piece = src[ys_src[i]:ys_src[i + 1], xs_src[j]:xs_src[j + 1]]
            tw, th = xs_dst[j + 1] - xs_dst[j], ys_dst[i + 1] - ys_dst[i]
            if tw <= 0 or th <= 0 or piece.size == 0:
                continue
            im = Image.fromarray(piece).resize((tw, th), Image.LANCZOS)
            out[ys_dst[i]:ys_dst[i + 1], xs_dst[j]:xs_dst[j + 1]] = np.asarray(im)
    return out


def soft_mask(w, h, feather):
    m = Image.new('L', (w, h), 0)
    inner = Image.new('L', (max(1, w - 2 * feather), max(1, h - 2 * feather)), 255)
    m.paste(inner, (feather, feather))
    return np.asarray(m.filter(ImageFilter.GaussianBlur(feather * 0.6))).astype(np.float32) / 255.0


def pocket_mask(r, s, tx0, ty0, tw, th):
    """Only the pocket and its bevel are laid in, not the metal around it (that came from somewhere else)."""
    m = Image.new('L', (tw, th), 0)
    from PIL import ImageDraw
    dr = ImageDraw.Draw(m)
    x, y, w, h, rad, rnd = r[0], r[1], r[2], r[3], r[4], r[5]
    def P(ux, uy):
        return px(ux, s) - tx0, px(uy, s) - ty0
    bev = 5.0
    if rnd == 2:   # a knob piece: the round seat and the value window under it
        es, eb = max(13.0, 0.23 * (w - 2 * max(13.0, 0)) ), 0
        # recover the knob's own rect from the piece: (x - es, y - 8, w + 2es, h + 8 + eb) with es / eb from its width
        # (solved numerically: the knob is the piece's centre column)
        kw = (w - 26.0) if w - 26.0 < 13.0 / 0.23 else w / 1.46
        cx = x + w / 2
        ky = y + 8
        rr = kw / 2 + 6 + bev
        dr.ellipse([*P(cx - rr, ky + kw / 2 - rr), *P(cx + rr, ky + kw / 2 + rr)], fill=255)
        sw = kw / 2 + 11.5 + bev
        dr.rounded_rectangle([*P(cx - sw, ky + kw + 5 - bev), *P(cx + sw, ky + kw + 29 + bev)], radius=px(8, s), fill=255)
    else:
        dr.rounded_rectangle([*P(x - bev, y - bev), *P(x + w + bev, y + h + bev)], radius=px(rad + bev, s), fill=255)
    return np.asarray(m.filter(ImageFilter.GaussianBlur(px(1.6, s)))).astype(np.float32) / 255.0


def best_source(old_pockets, target):
    x, y, w, h, rad, rnd, glass = target
    best, score = None, 1e18
    for r in old_pockets:
        if r[5] != rnd or r[6] != glass:
            continue
        ow, oh = r[2], r[3]
        if rnd:
            sc = abs(ow - w) * 10 + abs(oh - h) * 10 + abs((ow / max(oh, 1)) - (w / max(h, 1))) * 500
        else:
            sc = abs(oh - h) * 30 + abs(ow - w) + abs(r[4] - rad) * 20   # height matters most: the bevel is in it
        if sc < score:
            best, score = r, sc
    if best is None:   # no pocket of that kind on this plate: take any rectangle
        for r in old_pockets:
            sc = abs(r[3] - h) * 30 + abs(r[2] - w)
            if sc < score:
                best, score = r, sc
    return best


def groove_profile(img, grooves, s):
    for g in grooves:
        x0, y0, x1, y1 = g
        if abs(y0 - y1) < 0.5 and abs(x1 - x0) > 100:
            cx = px((x0 + x1) / 2, s)
            y = px(y0, s)
            half = px(GROOVE_HALF, s)
            return img[y - half:y + half + 1, cx - 40:cx + 40].copy()
    return None


def replate(name, old, new, plates, outdir):
    path = os.path.join(plates, 'HEM_' + name.replace(' ', '_') + '.jpg')
    im = Image.open(path).convert('RGB')
    img = np.asarray(im).copy()
    H, W, _ = img.shape
    s = W / PLATE_W
    o, n = old[name], new[name]
    keepR = [r for r in o['R'] if r in n['R']]
    goneR = [r for r in o['R'] if r not in n['R']]
    addR = [r for r in n['R'] if r not in o['R']]
    keepG = [g for g in o['G'] if g in n['G']]
    goneG = [g for g in o['G'] if g not in n['G']]
    addG = [g for g in n['G'] if g not in o['G']]
    prof = groove_profile(img, o['G'], s)
    # 1. empty the pockets and grooves that moved
    hole = np.zeros((H, W), bool)
    keep = np.zeros((H, W), bool)
    for r in goneR:
        x0, y0, x1, y1 = rect_px(r, s, MARGIN + 5.0) if r[5] != 2 else rect_px(r, s, 0.0)
        hole[max(y0, 0):y1, max(x0, 0):x1] = True
    for g in goneG:
        x0, y0, x1, y1 = groove_px(g, s, GROOVE_HALF + 1.0, 6.0)
        hole[max(y0, 0):y1, max(x0, 0):x1] = True
    for r in keepR:
        x0, y0, x1, y1 = rect_px(r, s, MARGIN) if r[5] != 2 else rect_px(r, s, 0.0)
        keep[max(y0, 0):y1, max(x0, 0):x1] = True
    for g in keepG:
        x0, y0, x1, y1 = groove_px(g, s, 2.5)
        keep[max(y0, 0):y1, max(x0, 0):x1] = True
    hole &= ~keep
    filled = fill_rows(img, hole, keep)
    # 2. the new grooves (the old groove's cross-section, laid along the line)
    if prof is not None:
        ph = prof.shape[0]
        line = prof[:, prof.shape[1] // 2:prof.shape[1] // 2 + 1, :]   # one column of the cross-section
        for g in addG:
            x0, y0, x1, y1 = g
            if abs(y0 - y1) < 0.5:
                a, b = px(min(x0, x1), s), px(max(x0, x1), s)
                y = px(y0, s) - ph // 2
                seg = np.repeat(line, b - a, axis=1).astype(np.float32)
                base = filled[y:y + ph, a:b].astype(np.float32)
                # lay the groove's difference from plain metal on top of what is there
                plain = np.concatenate([prof[:2], prof[-2:]]).mean(axis=(0, 1))
                filled[y:y + ph, a:b] = np.clip(base + (seg - plain), 0, 255).astype(np.uint8)
            else:
                a, b = px(min(y0, y1), s), px(max(y0, y1), s)
                x = px(x0, s) - ph // 2
                colp = np.transpose(line, (1, 0, 2))
                seg = np.repeat(colp, b - a, axis=0).astype(np.float32)
                base = filled[a:b, x:x + ph].astype(np.float32)
                plain = np.concatenate([prof[:2], prof[-2:]]).mean(axis=(0, 1))
                filled[a:b, x:x + ph] = np.clip(base + (seg - plain), 0, 255).astype(np.uint8)
    # 3. the new pockets, cut from the original picture
    for r in addR:
        src = best_source(o['R'], r)
        if src is None:
            continue
        sx0, sy0, sx1, sy1 = rect_px(src, s, MARGIN)
        piece = img[max(sy0, 0):sy1, max(sx0, 0):sx1]
        tx0, ty0, tx1, ty1 = rect_px(r, s, MARGIN)
        tw, th = tx1 - tx0, ty1 - ty0
        if r[5]:   # round: scale as a whole
            pic = np.asarray(Image.fromarray(piece).resize((tw, th), Image.LANCZOS))
        else:
            border = px(MARGIN + max(r[4], src[4]) + 2, s)
            pic = nine_slice(piece, tw, th, border)
        # level the piece to where it goes: the plate's light changes over its height, so a piece cut from another
        # row came out lighter or darker than the metal around it
        ring = max(2, px(2.0, s))
        def ring_mean(a):   # brightness only (one offset for all three colours: no tint), the median (a groove nearby doesn't count)
            px_ = np.concatenate([a[:ring].reshape(-1, 3), a[-ring:].reshape(-1, 3), a[:, :ring].reshape(-1, 3), a[:, -ring:].reshape(-1, 3)]).astype(np.float32)
            return float(np.median(px_.mean(axis=1)))
        yy0, xx0, yy1, xx1 = max(ty0, 0), max(tx0, 0), min(ty1, H), min(tx1, W)
        dest = filled[yy0:yy1, xx0:xx1]
        if dest.shape[0] > 2 * ring and dest.shape[1] > 2 * ring:
            pic = np.clip(pic.astype(np.float32) + (ring_mean(dest) - ring_mean(pic)), 0, 255).astype(np.uint8)
        mask = pocket_mask(r, s, tx0, ty0, tw, th)[..., None]
        y0c, x0c = max(ty0, 0), max(tx0, 0)
        y1c, x1c = min(ty1, H), min(tx1, W)
        sub = pic[y0c - ty0:y1c - ty0, x0c - tx0:x1c - tx0].astype(np.float32)
        mk = mask[y0c - ty0:y1c - ty0, x0c - tx0:x1c - tx0]
        base = filled[y0c:y1c, x0c:x1c].astype(np.float32)
        filled[y0c:y1c, x0c:x1c] = (base * (1 - mk) + sub * mk).astype(np.uint8)
    os.makedirs(outdir, exist_ok=True)
    Image.fromarray(filled).save(os.path.join(outdir, os.path.basename(path)), quality=92)
    return len(goneR), len(addR), len(goneG), len(addG)


if __name__ == '__main__':
    old, new = load(sys.argv[1]), load(sys.argv[2])
    for name in sys.argv[5:]:
        print(name, replate(name, old, new, sys.argv[3], sys.argv[4]))
