"""Time-coded forensic metering of a Suno stem session.

Everything here reads the stereo stems at full rate. Results are plain dicts (JSON-ready).
Numbers are measurements; where a value is an estimate the key says so (e.g. q_estimate).
"""
from __future__ import annotations

import itertools
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.ndimage import median_filter, uniform_filter1d
from scipy.signal import butter, coherence, csd, find_peaks, resample_poly, sosfilt, stft

from . import loudness as lu
from .audio_io import Session, Track
from .dsp import block_power, to_db

N_FFT = 4096
HOP = 2048
SEGMENT_S = 3.0                       # time-coding resolution for hot spots and resonance coverage
BANDS = {                             # analysis bands (Hz)
    "sub": (20, 150), "low": (150, 500), "mud": (200, 500), "low_mid": (500, 2000),
    "harsh": (2000, 5000), "sibilance": (5000, 10000), "air": (10000, 20000),
}
STEREO_BANDS = {"sub_lt150": (20, 150), "low_150_500": (150, 500), "mid_500_4k": (500, 4000), "high_gt4k": (4000, 20000)}


@dataclass
class StemSpectra:
    """What later stages need from one STFT pass (the full STFT itself is not kept)."""
    freqs: np.ndarray
    frame_t: np.ndarray
    band_db: dict            # band name -> per-frame dB
    total_db: np.ndarray     # per-frame broadband dB
    low_pow: np.ndarray      # (frames, bins 40..500 Hz) power, for kick/bass collision and masking
    low_freqs: np.ndarray
    mean_db: np.ndarray      # long-term mean power spectrum (active frames), dB
    seg_db: np.ndarray       # (segments, bins) mean power per 3 s segment, dB
    seg_t: np.ndarray
    sk: np.ndarray           # spectral kurtosis per bin over active frames
    active: np.ndarray       # bool per frame


def _mid(x: np.ndarray) -> np.ndarray:
    return 0.5 * (x[0] + x[1])


def spectra(x: np.ndarray, rate: int, save_stft_to: Path | None = None) -> StemSpectra:
    f, t, Z = stft(_mid(x).astype(np.float32), fs=rate, window="hann", nperseg=N_FFT, noverlap=N_FFT - HOP,
                   boundary=None, padded=False)
    if save_stft_to is not None:
        # Raw complex STFT of L and R (magnitude and phase in radians recoverable as abs/angle).
        save_stft_to.parent.mkdir(parents=True, exist_ok=True)
        _, _, ZL = stft(x[0].astype(np.float32), fs=rate, nperseg=N_FFT, noverlap=N_FFT - HOP, boundary=None, padded=False)
        _, _, ZR = stft(x[1].astype(np.float32), fs=rate, nperseg=N_FFT, noverlap=N_FFT - HOP, boundary=None, padded=False)
        np.savez(save_stft_to, freqs=f, times=t, L=ZL.astype(np.complex64), R=ZR.astype(np.complex64),
                 M=Z.astype(np.complex64), S=(0.5 * (ZL - ZR)).astype(np.complex64))
        del ZL, ZR
    P = (np.abs(Z) ** 2).astype(np.float64)      # (bins, frames)
    del Z
    total = P.sum(axis=0)
    total_db = to_db(total)
    peak_db = np.percentile(total_db, 95) if total_db.size else -150.0
    active = (total_db > peak_db - 40.0) & (total_db > -120.0)
    band_db = {}
    for name, (lo, hi) in BANDS.items():
        sel = (f >= lo) & (f < min(hi, rate / 2))
        band_db[name] = to_db(P[sel].sum(axis=0)) if sel.any() else np.full(P.shape[1], -150.0)
    lsel = (f >= 40) & (f <= 500)
    Pa = P[:, active] if active.any() else P
    mean_db = to_db(Pa.mean(axis=1))
    m2 = Pa.mean(axis=1)
    m4 = (Pa * Pa).mean(axis=1)
    sk = m4 / np.maximum(m2 * m2, 1e-60) - 2.0
    seg_frames = max(1, int(round(SEGMENT_S * rate / HOP)))
    nseg = max(1, P.shape[1] // seg_frames)
    seg = P[:, :nseg * seg_frames].reshape(P.shape[0], nseg, seg_frames).mean(axis=2).T
    return StemSpectra(f, t, band_db, total_db, P[lsel].T.copy(), f[lsel], mean_db, to_db(seg),
                       np.arange(nseg) * seg_frames * HOP / rate, sk, active)


# --------------------------------------------------------------------------- stereo

def stereo_report(x: np.ndarray, rate: int, frame_s: float = 1.0) -> dict:
    out = {}
    L, R = x[0], x[1]
    M, S = 0.5 * (L + R), 0.5 * (L - R)
    em, es = float(np.mean(M * M)), float(np.mean(S * S))
    out["mid_side_ratio_db"] = _r(10 * math.log10(max(em, 1e-30) / max(es, 1e-30)))
    out["side_energy_pct"] = _r(100.0 * es / max(em + es, 1e-30), 1)
    total = float(np.mean(L * L + R * R)) + 1e-30
    bands = {}
    for name, (lo, hi) in STEREO_BANDS.items():
        hi = min(hi, 0.45 * rate)
        sos = butter(4, [lo, hi], btype="bandpass", fs=rate, output="sos")
        b = sosfilt(sos, x, axis=-1)
        e = float(np.mean(b[0] ** 2 + b[1] ** 2))
        corr = _corr(b[0], b[1])
        # worst 1 s frame (only frames that carry energy in this band)
        F = int(frame_s * rate)
        nf = b.shape[1] // F
        worst, worst_t = None, None
        if nf:
            l = b[0, :nf * F].reshape(nf, F)
            r = b[1, :nf * F].reshape(nf, F)
            el = (l * l).sum(1)
            er = (r * r).sum(1)
            c = (l * r).sum(1) / np.sqrt(np.maximum(el * er, 1e-30))
            ok = (el + er) > 1e-3 * max(float((el + er).max()), 1e-30)
            if ok.any():
                i = int(np.argmin(np.where(ok, c, 2.0)))
                worst, worst_t = float(c[i]), float(i * frame_s)
        bands[name] = {"correlation": _r(corr), "worst_1s_correlation": _r(worst), "worst_at_s": worst_t,
                       "energy_share_pct": _r(100.0 * e / total, 1)}
    out["bands"] = bands
    sub = bands["sub_lt150"]
    out["sub_phase_problem"] = bool(sub["correlation"] is not None and sub["correlation"] < 0.85 and (sub["energy_share_pct"] or 0) > 1.0)
    out["mono_fold_down_loss_db"] = _r(mono_loss_db(x))
    return out


def mono_loss_db(x: np.ndarray) -> float:
    avg = float(np.mean(0.5 * (x[0] ** 2 + x[1] ** 2)))
    mono = float(np.mean((0.5 * (x[0] + x[1])) ** 2))
    if avg <= 1e-30:
        return 0.0
    return 10.0 * math.log10(avg / max(mono, 1e-30))


def _corr(a: np.ndarray, b: np.ndarray) -> float | None:
    d = math.sqrt(float(np.dot(a, a)) * float(np.dot(b, b)))
    return float(np.dot(a, b)) / d if d > 1e-20 else None


# --------------------------------------------------------------------------- resonances / cutoff

def resonances(sp: StemSpectra, lo: float = 2000.0, hi: float = 8000.0, min_prom_db: float = 6.0, top: int = 8) -> list[dict]:
    f = sp.freqs
    df = f[1] - f[0]
    base = median_filter(sp.mean_db, size=41, mode="nearest")      # local spectral floor (~480 Hz wide at 48 kHz)
    resid = sp.mean_db - base
    sel = np.where((f >= lo) & (f <= hi))[0]
    if sel.size < 3:
        return []
    pk, props = find_peaks(resid[sel], prominence=min_prom_db, width=(0.5, 40), rel_height=0.5)
    seg_resid = (sp.seg_db - median_filter(sp.seg_db, size=(1, 41), mode="nearest")) if (pk.size and sp.seg_db.size) else None
    out = []
    for j, i0 in enumerate(pk):
        i = sel[i0]
        # parabolic interpolation of the peak frequency
        if 0 < i < len(f) - 1:
            a, b, c = sp.mean_db[i - 1], sp.mean_db[i], sp.mean_db[i + 1]
            den = a - 2 * b + c
            off = 0.5 * (a - c) / den if abs(den) > 1e-12 else 0.0
        else:
            off = 0.0
        hz = float(f[i] + off * df)
        width_hz = max(float(props["widths"][j]) * df, df)
        on = seg_resid[:, i] > min_prom_db if seg_resid is not None else np.zeros(0, dtype=bool)
        sk = float(sp.sk[i])
        out.append({
            "hz": round(hz, 1),
            "prominence_db": _r(float(props["prominences"][j]), 1),
            "q_estimate": _r(hz / width_hz, 1),
            "spectral_kurtosis": _r(sk),
            "present_pct_of_song": _r(100.0 * float(on.mean()) if on.size else 0.0, 0),
            "present_ranges_s": _ranges(sp.seg_t, on, SEGMENT_S),
            "kind": "stationary_artifact" if (sk < -0.5 and (on.mean() if on.size else 0) > 0.5) else "musical_or_intermittent",
        })
    out.sort(key=lambda d: -(d["prominence_db"] or 0))
    return out[:top]


def resonance_count(sp: StemSpectra, lo: float = 3000.0, hi: float = 6000.0, min_prom_db: float = 6.0) -> int:
    return len(resonances(sp, lo, hi, min_prom_db, top=10_000))


def hf_cutoff(sp: StemSpectra, rate: int) -> dict:
    f = sp.freqs
    db = uniform_filter1d(sp.mean_db, size=9, mode="nearest")
    df = f[1] - f[0]
    lo, hi = 12000.0, min(16000.0, 0.47 * rate)      # project prompt: brickwalls 12-16 kHz
    idx = np.where((f >= lo) & (f <= hi))[0]
    if idx.size == 0:
        return {"cutoff_hz": None, "drop_db": None, "brickwall": False}
    b1, b2 = int(1500 / df), int(300 / df)
    a1, a2 = int(300 / df), int(1500 / df)
    best, best_i = 0.0, None
    for i in idx:
        if i - b1 < 0 or i + a2 >= len(db):
            continue
        drop = float(db[i - b1:i - b2].mean() - db[i + a1:i + a2].mean())
        if drop > best:
            best, best_i = drop, i
    if best_i is None:
        return {"cutoff_hz": None, "drop_db": None, "brickwall": False}
    return {"cutoff_hz": round(float(f[best_i]), 0), "drop_db": _r(best, 1), "brickwall": bool(best >= 24.0)}


# --------------------------------------------------------------------------- hot spots (localized problems)

def hot_spots(sp: StemSpectra, excess_db: float = 4.0) -> list[dict]:
    """3 s segments where a band's share of the stem is well above that stem's own normal."""
    seg_frames = max(1, int(round(SEGMENT_S / (sp.frame_t[1] - sp.frame_t[0])))) if sp.frame_t.size > 1 else 1
    nseg = max(1, sp.total_db.size // seg_frames)
    out = []

    def seg_mean_db(db):
        p = np.power(10.0, db[:nseg * seg_frames] / 10.0).reshape(nseg, seg_frames).mean(axis=1)
        return to_db(p)

    tot = seg_mean_db(sp.total_db)
    act = tot > (np.percentile(tot, 95) - 30.0)
    for band in ("sub", "mud", "harsh", "sibilance"):
        rel = seg_mean_db(sp.band_db[band]) - tot
        if not act.any():
            continue
        norm = float(np.median(rel[act]))
        over = act & (rel - norm > excess_db) & (rel > -30.0)   # the band must actually carry sound
        for a, b in _ranges(np.arange(nseg) * SEGMENT_S, over, SEGMENT_S):
            i0, i1 = int(a / SEGMENT_S), int(round(b / SEGMENT_S))
            out.append({"band": band, "start_s": a, "end_s": b,
                        "excess_db": _r(float((rel[i0:i1] - norm).max()), 1)})
    out.sort(key=lambda d: -(d["excess_db"] or 0))
    return out[:12]


# --------------------------------------------------------------------------- alignment (ShiftMap)

def gcc_phat(sig: np.ndarray, ref: np.ndarray, max_lag: int) -> tuple[float, float, float]:
    """Lag (samples, sub-sample) by which `sig` is LATE vs `ref`, peak-to-sidelobe ratio (peak / RMS of the
    correlation away from the peak; > 6 = a clear match), plain-correlation sign."""
    n = sig.size + ref.size
    nfft = 1 << (n - 1).bit_length()
    S = np.fft.rfft(sig, nfft)
    Rf = np.fft.rfft(ref, nfft)
    X = S * np.conj(Rf)
    cc = np.fft.irfft(X / np.maximum(np.abs(X), 1e-20), nfft)
    cc = np.concatenate([cc[-max_lag:], cc[:max_lag + 1]])
    k = int(np.argmax(np.abs(cc)))
    lag = refine_lag(X, nfft, float(k - max_lag))
    side = np.delete(cc, np.arange(max(0, k - 20), min(cc.size, k + 21)))
    psr = float(abs(cc[k]) / max(float(np.sqrt(np.mean(side * side))), 1e-20)) if side.size else 0.0
    plain = np.fft.irfft(X, nfft)
    li = int(round(lag)) % nfft
    sign = float(np.sign(plain[li])) if plain[li] != 0 else 1.0
    return float(lag), psr, sign


def refine_lag(X: np.ndarray, nfft: int, k0: float, beta: float = 0.7, step: float = 0.02) -> float:
    """Sub-sample lag: PHAT-beta cross-correlation evaluated on a fine grid within +/- 1 sample of the
    whole-sample peak (on the synthetic test: 37.15-37.25 for a true 37.3 samples)."""
    W = X / np.maximum(np.abs(X), 1e-20) ** beta
    keep = np.abs(W) > 1e-6 * float(np.abs(W).max())
    W, w = W[keep], (2.0 * np.pi / nfft) * np.nonzero(keep)[0]
    taus = np.arange(k0 - 1.0, k0 + 1.0 + step / 2, step)
    best, best_v = k0, -np.inf
    for tau in taus:
        v = float(np.real(np.dot(W, np.exp(1j * w * tau))))
        if v > best_v:
            best, best_v = float(tau), v
    return best


def shift_map(stem: np.ndarray, ref: np.ndarray, rate: int, win_s: float = 8.0, hop_s: float = 10.0,
              max_lag_ms: float = 100.0) -> dict:
    s, r = _mid(stem), _mid(ref)
    W, H = int(win_s * rate), int(hop_s * rate)
    L = int(max_lag_ms * 1e-3 * rate)
    stem_db = to_db(block_power(s, W))
    thr = (np.percentile(stem_db, 95) - 30.0) if stem_db.size else 0.0
    points = []
    for a in range(0, max(1, s.size - W), H):
        seg = s[a:a + W]
        if seg.size < W // 2 or to_db(np.array([np.mean(seg * seg)]))[0] < thr:
            continue
        lag, h, sign = gcc_phat(seg, r[a:a + W], L)
        if h >= 6.0:
            points.append((a / rate + win_s / 2, lag, h, sign))
    if not points:
        return {"offset_samples": None, "offset_ms": None, "drift_ms_per_min": None, "polarity_inverted": None,
                "match_strength_psr": None, "windows": 0, "note": "stem silent or no clear match with the reference"}
    t = np.array([p[0] for p in points])
    lag = np.array([p[1] for p in points])
    h = np.array([p[2] for p in points])
    sign = np.array([p[3] for p in points])
    med = float(np.median(lag))
    agree = np.abs(lag - med) <= 1.0
    if agree.mean() < 0.6 or agree.sum() < 2:
        return {"offset_samples": None, "offset_ms": None, "drift_ms_per_min": None, "polarity_inverted": None,
                "match_strength_psr": _r(float(np.median(h)), 1), "windows": len(points),
                "window_lags": [round(float(v), 2) for v in lag],
                "note": "windows disagree (tonal stem: the match repeats every cycle); no offset applied"}
    t, lag, h, sign = t[agree], lag[agree], h[agree], sign[agree]
    off = float(np.average(lag, weights=h))
    drift = None
    if len(points) >= 3 and (t.max() - t.min()) > 20:
        slope = np.polyfit(t, lag, 1, w=h)[0]       # samples per second
        drift = float(slope / rate * 1000.0 * 60.0)   # ms per minute
    neg = float(np.average(sign < 0, weights=h))
    return {"offset_samples": round(off, 2), "offset_ms": round(off / rate * 1000.0, 3),
            "drift_ms_per_min": None if drift is None else round(drift, 3),
            "polarity_inverted": bool(neg > 0.75), "match_strength_psr": _r(float(np.median(h)), 1), "windows": len(points)}


def onset_dtw_drift(stem: np.ndarray, ref: np.ndarray, rate: int, seg_s: float = 20.0) -> dict:
    """Progressive drift from onset envelopes + DTW, per segment (librosa). Coarse (one hop = 512 samples)."""
    try:
        import librosa
    except ImportError:
        return {"drift_ms_per_min": None, "note": "librosa not installed"}
    hop = 512
    a = librosa.onset.onset_strength(y=_mid(stem).astype(np.float32), sr=rate, hop_length=hop)
    b = librosa.onset.onset_strength(y=_mid(ref).astype(np.float32), sr=rate, hop_length=hop)
    n = min(a.size, b.size)
    seg = int(seg_s * rate / hop)
    ts, offs = [], []
    for s0 in range(0, n - seg + 1, seg):
        x, y = a[s0:s0 + seg], b[s0:s0 + seg]
        if x.max() <= 1e-6 or y.max() <= 1e-6:
            continue
        D, wp = librosa.sequence.dtw(X=x[np.newaxis, :], Y=y[np.newaxis, :], metric="euclidean",
                                     global_constraints=True, band_rad=0.05)
        wp = wp[::-1]
        mid = wp[len(wp) // 4: 3 * len(wp) // 4]        # ignore the pinned ends
        offs.append(float(np.median(mid[:, 0] - mid[:, 1])) * hop / rate * 1000.0)
        ts.append((s0 + seg / 2) * hop / rate)
    if len(ts) < 3:
        return {"drift_ms_per_min": None, "note": "too few active segments"}
    slope = float(np.polyfit(ts, offs, 1)[0])      # ms per second
    res_ms = hop / rate * 1000.0
    resid = np.asarray(offs) - np.polyval(np.polyfit(ts, offs, 1), ts)
    if float(np.std(resid)) > 3 * res_ms or abs(slope * 60.0) > 200.0:
        return {"drift_ms_per_min": None, "note": "onset match not steady (tonal or sparse stem); use the GCC-PHAT drift",
                "segment_offsets_ms": [round(o, 1) for o in offs]}
    return {"drift_ms_per_min": round(slope * 60.0, 2), "segment_offsets_ms": [round(o, 1) for o in offs],
            "resolution_ms": round(hop / rate * 1000.0, 2)}


# --------------------------------------------------------------------------- cross-stem

def frame_db(x: np.ndarray, block: int) -> np.ndarray:
    return to_db(block_power(_mid(x), block))


def bleed_matrix(stems: list[Track], rate: int, block_ms: float = 21.3) -> dict:
    """Energy left in each stem's 'silent' gaps that follows another stem: source i leaks into target j."""
    B = max(64, int(block_ms * 1e-3 * rate))
    env = {s.name: frame_db(s.audio, B) for s in stems}
    names = [s.name for s in stems]
    res = {"rows_leak_into_columns": names, "bleed_db": [], "envelope_corr": [], "gap_floor_dbfs": {}, "gap_pct": {}}
    for i in names:
        res["bleed_db"].append([])
        res["envelope_corr"].append([])
    for jj, j in enumerate(names):
        ej = env[j]
        p95 = np.percentile(ej, 95)
        gap = (ej < min(otsu(ej[ej > -140.0]), p95 - 12.0)) & (ej > -140.0)
        res["gap_pct"][j] = _r(100.0 * float(gap.mean()), 1)
        res["gap_floor_dbfs"][j] = _r(float(np.median(ej[gap])) if gap.any() else None, 1)
        for ii, i in enumerate(names):
            if i == j or gap.sum() < 20:
                res["bleed_db"][ii].append(None)
                res["envelope_corr"][ii].append(None)
                continue
            ei = env[i]
            act = ei > np.percentile(ei, 95) - 20.0
            F = gap & act
            c = _corr(ej[gap] - ej[gap].mean(), ei[gap] - ei[gap].mean()) if gap.sum() > 2 else None
            if F.sum() < 20 or c is None or c < 0.3:
                res["bleed_db"][ii].append(None)
            else:
                pj = np.power(10.0, ej[F] / 10.0).mean()
                pi = np.power(10.0, ei[F] / 10.0).mean()
                res["bleed_db"][ii].append(_r(10 * math.log10(max(pj, 1e-30) / max(pi, 1e-30)), 1))
            res["envelope_corr"][ii].append(_r(c, 2))
    res["gate_threshold_hint_dbfs"] = {}
    for j in names:
        ej = env[j]
        live = ej[ej > -140.0]
        if live.size:
            split = min(otsu(live), float(np.percentile(ej, 95)) - 12.0)
            gp = live[live < split]
            floor = float(np.median(gp)) if gp.size else split - 12.0
            res["gate_threshold_hint_dbfs"][j] = _r(floor + 0.5 * (split - floor) + 3.0, 1)   # just above the gap floor
        else:
            res["gate_threshold_hint_dbfs"][j] = None
    return res


def otsu(db: np.ndarray, bins: int = 120) -> float:
    """Level (dB) that best splits frames into 'gap' and 'playing' (Otsu's method on the histogram)."""
    if db.size < 4:
        return float(db.mean()) if db.size else -140.0
    h, e = np.histogram(db, bins=bins)
    c = 0.5 * (e[1:] + e[:-1])
    w0 = np.cumsum(h)
    w1 = w0[-1] - w0
    m0 = np.cumsum(h * c) / np.maximum(w0, 1)
    m1 = ((h * c).sum() - np.cumsum(h * c)) / np.maximum(w1, 1)
    var = w0 * w1 * (m0 - m1) ** 2
    return float(e[int(np.argmax(var)) + 1])


def overlap_coeff(Pa: np.ndarray, Pb: np.ndarray, freqs: np.ndarray, lo: float, hi: float) -> tuple[float, np.ndarray, np.ndarray]:
    """Spectral overlap 0..1 per frame (2*sum(min)/sum(a+b)), energy-weighted mean, and where it happens."""
    sel = (freqs >= lo) & (freqs <= hi)
    a, b = Pa[:, sel], Pb[:, sel]
    mn = np.minimum(a, b)
    den = (a + b).sum(axis=1)
    per = np.where(den > 0, 2.0 * mn.sum(axis=1) / np.maximum(den, 1e-30), 0.0)
    w = den / max(float(den.sum()), 1e-30)
    return float((per * w).sum()), per, mn.mean(axis=0)


def masking_matrix(names: list[str], sp: dict, lo: float = 200.0, hi: float = 500.0) -> dict:
    m, vals = {}, []
    for a, b in itertools.combinations(names, 2):
        if sp[a].low_pow.shape[0] != sp[b].low_pow.shape[0]:
            continue
        oc, _, _ = overlap_coeff(sp[a].low_pow, sp[b].low_pow, sp[a].low_freqs, lo, hi)
        m[f"{a} | {b}"] = _r(oc, 3)
        vals.append(oc)
    return {"band_hz": [lo, hi], "pairs": m, "mean_overlap": _r(float(np.mean(vals)) if vals else 0.0, 4)}


def kick_bass_collision(stems: list[Track], sp: dict) -> dict:
    kick = next((s for s in stems if s.role == "kick"), None) or next((s for s in stems if s.role == "drums"), None)
    bass = next((s for s in stems if s.role == "bass"), None)
    if kick is None or bass is None:
        return {"kick_stem": kick.name if kick else None, "bass_stem": bass.name if bass else None,
                "note": "need a kick/drums stem and a bass/808 stem (by file name) for this check"}
    a, b = sp[kick.name], sp[bass.name]
    oc, per, where = overlap_coeff(a.low_pow, b.low_pow, a.low_freqs, 40.0, 350.0)
    sel = (a.low_freqs >= 40) & (a.low_freqs <= 350)
    fz = a.low_freqs[sel]
    peak_hz = float(fz[int(np.argmax(where))]) if where.size else None
    dt = a.frame_t[1] - a.frame_t[0] if a.frame_t.size > 1 else HOP / 48000
    seg = max(1, int(round(SEGMENT_S / dt)))
    nseg = max(1, per.size // seg)
    tl = per[:nseg * seg].reshape(nseg, seg).mean(axis=1)
    worst = np.argsort(-tl)[:5]
    return {"kick_stem": kick.name, "bass_stem": bass.name, "band_hz": [40, 350], "overlap": _r(oc, 3),
            "most_collision_hz": None if peak_hz is None else round(peak_hz, 1),
            "worst_sections": [{"start_s": round(float(i * SEGMENT_S), 1), "overlap": _r(float(tl[i]), 3)} for i in worst]}


def phase_cancellation_pairs(stems: list[Track], rate: int, fmax: float = 500.0) -> list[dict]:
    """Magnitude-squared coherence + cross-spectrum phase between stems below `fmax`.
    High coherence with phase near 180 degrees = the two stems cancel there when summed."""
    dec = 8 if rate >= 32000 else 4
    lows = {s.name: resample_poly(_mid(s.audio), 1, dec) for s in stems}
    fs = rate / dec
    out = []
    for a, b in itertools.combinations(lows.keys(), 2):
        x, y = lows[a], lows[b]
        if np.dot(x, x) < 1e-9 or np.dot(y, y) < 1e-9:
            continue
        f, C = coherence(x, y, fs=fs, nperseg=1024)
        _, Pxy = csd(x, y, fs=fs, nperseg=1024)
        ph = np.angle(Pxy)
        bad = (f >= 20) & (f <= fmax) & (C > 0.6) & (np.abs(ph) > math.radians(120))
        if bad.any():
            rng = _ranges(f, bad, f[1] - f[0])
            out.append({"pair": [a, b], "ranges_hz": [[round(r0, 1), round(r1, 1)] for r0, r1 in rng][:4],
                        "max_coherence": _r(float(C[bad].max()), 2)})
    return out


# --------------------------------------------------------------------------- whole session

def analyze(session: Session, stft_dir: Path | None = None) -> tuple[dict, dict]:
    rate = session.rate
    sp: dict[str, StemSpectra] = {}
    stems_out = {}
    # Timing reference (project prompt: "against the reference mix/drum grid"): the mix, else the drum stem.
    drums = next((s for s in session.stems if s.role in ("drums", "kick")), None)
    if session.mix is not None:
        ref, ref_name = session.mix.audio, session.mix.name
    elif drums is not None:
        ref, ref_name = drums.audio, drums.name
    else:
        ref, ref_name = None, None
    for s in session.stems:
        use_ref = ref is not None and s.name != ref_name
        sp[s.name] = spectra(s.audio, rate, (stft_dir / f"{s.name}_stft.npz") if stft_dir else None)
        a = s.audio
        stems_out[s.name] = {
            "role": s.role,
            "file": {"rate": s.file_rate, "channels": s.file_channels, "subtype": s.file_subtype, "notes": s.notes},
            "integrity": integrity(a),
            "loudness": lu.full_report(a, rate),
            "stereo": stereo_report(a, rate),
            "shift": (shift_map(a, ref, rate) | {"reference": ref_name}) if use_ref else {"note": "this stem is the timing reference" if ref is not None else "no mix and no drum stem to time against"},
            "onset_drift": (onset_dtw_drift(a, ref, rate) | {"reference": ref_name}) if use_ref else None,
            "resonances_2k_8k": resonances(sp[s.name]),
            "resonance_count_3k_6k": resonance_count(sp[s.name]),
            "hf_cutoff": hf_cutoff(sp[s.name], rate),
            "hot_spots": hot_spots(sp[s.name]),
            "active_pct": _r(100.0 * float(sp[s.name].active.mean()), 1),
        }
    stem_sum = np.sum([s.audio for s in session.stems], axis=0)
    mix_out = None
    if session.mix is not None:
        msp = spectra(session.mix.audio, rate)
        resid = session.mix.audio - stem_sum
        mix_out = {
            "name": session.mix.name,
            "integrity": integrity(session.mix.audio),
            "loudness": lu.full_report(session.mix.audio, rate),
            "stereo": stereo_report(session.mix.audio, rate),
            "resonances_2k_8k": resonances(msp),
            "hf_cutoff": hf_cutoff(msp, rate),
            "hot_spots": hot_spots(msp),
            "stems_sum_null_db": _r(10 * math.log10(max(float(np.mean(resid ** 2)), 1e-30) /
                                                    max(float(np.mean(session.mix.audio ** 2)), 1e-30)), 1),
        }
    names = [s.name for s in session.stems]
    full = {
        "format": "hem-suno-telemetry/1",
        "sample_rate": rate,
        "length_s": round(session.length / rate, 3),
        "stems": stems_out,
        "reference_mix": mix_out,
        "stem_sum": {"loudness": lu.full_report(stem_sum, rate), "stereo": stereo_report(stem_sum, rate)},
        "cross_stem": {
            "bleed": bleed_matrix(session.stems, rate),
            "masking_200_500": masking_matrix(names, sp),
            "kick_bass_40_350": kick_bass_collision(session.stems, sp),
            "phase_cancellation_lt500": phase_cancellation_pairs(session.stems, rate),
        },
    }
    return full, sp


def integrity(x: np.ndarray) -> dict:
    bad = int((~np.isfinite(x)).sum())
    xs = np.where(np.isfinite(x), x, 0.0)
    return {"non_finite_samples": bad, "dc_offset": [_r(float(xs[0].mean()), 6), _r(float(xs[1].mean()), 6)],
            "samples_over_0dbfs": int((np.abs(xs) > 1.0).sum()), "digital_silence": bool(np.abs(xs).max() == 0.0)}


def compact(full: dict) -> dict:
    """The smaller view sent to Claude: same facts, shorter lists."""
    def slim_loud(l):
        return {k: l[k] for k in ("integrated_lufs", "momentary_max_lufs", "short_term_max_lufs", "lra_lu", "true_peak_dbtp")} | {
            "short_term_lufs_every_3s": l["short_term_timeline"]["lufs"]}

    stems = {}
    for n, s in full["stems"].items():
        st = s["stereo"]
        stems[n] = {
            "role": s["role"], "active_pct": s["active_pct"], "integrity": s["integrity"],
            "loudness": slim_loud(s["loudness"]),
            "stereo": {"sub_lt150_corr": st["bands"]["sub_lt150"]["correlation"],
                       "sub_worst_1s": st["bands"]["sub_lt150"]["worst_1s_correlation"],
                       "sub_worst_at_s": st["bands"]["sub_lt150"]["worst_at_s"],
                       "mid_side_ratio_db": st["mid_side_ratio_db"], "sub_phase_problem": st["sub_phase_problem"]},
            "shift": s["shift"], "onset_drift_ms_per_min": (s["onset_drift"] or {}).get("drift_ms_per_min"),
            "resonances_2k_8k": [{k: r[k] for k in ("hz", "prominence_db", "q_estimate", "spectral_kurtosis", "present_pct_of_song", "kind")} | {
                "present_ranges_s": r["present_ranges_s"][:4]} for r in s["resonances_2k_8k"][:5]],
            "hf_cutoff": s["hf_cutoff"], "hot_spots": s["hot_spots"][:8],
        }
    out = {"sample_rate": full["sample_rate"], "length_s": full["length_s"], "stems": stems,
           "stem_sum": {"integrated_lufs": full["stem_sum"]["loudness"]["integrated_lufs"],
                        "true_peak_dbtp": full["stem_sum"]["loudness"]["true_peak_dbtp"],
                        "sub_lt150_corr": full["stem_sum"]["stereo"]["bands"]["sub_lt150"]["correlation"],
                        "mono_fold_down_loss_db": full["stem_sum"]["stereo"]["mono_fold_down_loss_db"]}}
    if full["reference_mix"]:
        m = full["reference_mix"]
        out["reference_mix"] = {"loudness": slim_loud(m["loudness"]), "sub_lt150_corr": m["stereo"]["bands"]["sub_lt150"]["correlation"],
                                "mono_fold_down_loss_db": m["stereo"]["mono_fold_down_loss_db"], "hf_cutoff": m["hf_cutoff"],
                                "stems_sum_null_db": m["stems_sum_null_db"], "hot_spots": m["hot_spots"][:8]}
    cs = full["cross_stem"]
    out["cross_stem"] = {
        "bleed": {"rows_leak_into_columns": cs["bleed"]["rows_leak_into_columns"], "bleed_db": cs["bleed"]["bleed_db"],
                  "gap_floor_dbfs": cs["bleed"]["gap_floor_dbfs"], "gate_threshold_hint_dbfs": cs["bleed"]["gate_threshold_hint_dbfs"]},
        "masking_200_500": cs["masking_200_500"],
        "kick_bass_40_350": cs["kick_bass_40_350"],
        "phase_cancellation_lt500": cs["phase_cancellation_lt500"][:10],
    }
    return out


def _ranges(t: np.ndarray, mask: np.ndarray, step: float) -> list[list[float]]:
    out, start = [], None
    for k, on in enumerate(mask.tolist()):
        if on and start is None:
            start = float(t[k])
        if not on and start is not None:
            out.append([round(start, 1), round(float(t[k - 1]) + step, 1)])
            start = None
    if start is not None:
        out.append([round(start, 1), round(float(t[len(mask) - 1]) + step, 1)])
    return out


def _r(v, nd: int = 2):
    if v is None:
        return None
    v = float(v)
    return round(v, nd) if math.isfinite(v) else None
