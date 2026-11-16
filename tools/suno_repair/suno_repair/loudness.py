"""ITU-R BS.1770-4 / EBU R128 loudness, EBU Tech 3342 loudness range and 4x true peak.

Integrated loudness comes from pyloudnorm (the reference the brief asks for). Momentary,
short-term and LRA are computed here with the same K-weighting, because pyloudnorm only gives
integrated loudness.
"""
from __future__ import annotations

import math

import numpy as np
import pyloudnorm as pyln
from scipy.signal import resample_poly, sosfilt

ABS_GATE = -70.0


def k_weighting_sos(rate: float) -> np.ndarray:
    """BS.1770 K-weighting (pre-filter shelf + RLB high-pass), designed for any sample rate."""
    # Stage 1: high shelf (the head's acoustic effect).
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / rate)
    Vh = 10.0 ** (G / 20.0)
    Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    s1 = [(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
          1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    # Stage 2: RLB high-pass.
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / rate)
    a0 = 1.0 + K / Q + K * K
    s2 = [1.0, -2.0, 1.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    return np.array([s1, s2])


def _block_power(x: np.ndarray, rate: int, window_s: float, hop_s: float) -> tuple[np.ndarray, np.ndarray]:
    """Mean-square K-weighted power summed over channels, per sliding block. Returns (power, block start times)."""
    y = sosfilt(k_weighting_sos(rate), x, axis=-1)
    W = int(round(window_s * rate))
    H = max(1, int(round(hop_s * rate)))
    n = y.shape[-1]
    if n < W:
        return np.zeros(0), np.zeros(0)
    cs = np.concatenate([np.zeros((y.shape[0], 1)), np.cumsum(y * y, axis=-1)], axis=-1)
    starts = np.arange(0, n - W + 1, H)
    p = ((cs[:, starts + W] - cs[:, starts]) / W).sum(axis=0)   # G = 1.0 for left and right
    return p, starts / rate


def _lufs(p: np.ndarray) -> np.ndarray:
    with np.errstate(divide="ignore"):
        return -0.691 + 10.0 * np.log10(np.maximum(p, 1e-30))


def momentary(x: np.ndarray, rate: int, hop_s: float = 0.1) -> tuple[np.ndarray, np.ndarray]:
    p, t = _block_power(x, rate, 0.4, hop_s)
    return _lufs(p), t


def short_term(x: np.ndarray, rate: int, hop_s: float = 0.1) -> tuple[np.ndarray, np.ndarray]:
    p, t = _block_power(x, rate, 3.0, hop_s)
    return _lufs(p), t


def integrated(x: np.ndarray, rate: int) -> float:
    """pyloudnorm (BS.1770-4). -inf for silence."""
    if x.shape[-1] < int(0.4 * rate):
        return float("-inf")
    meter = pyln.Meter(rate)  # K-weighting, 400 ms blocks, 75 % overlap, -70 / -10 gates
    v = meter.integrated_loudness(np.ascontiguousarray(x.T))
    return float(v)


def integrated_own(x: np.ndarray, rate: int) -> float:
    """Own BS.1770-4 gating, used as a cross-check of pyloudnorm."""
    p, _ = _block_power(x, rate, 0.4, 0.1)
    if p.size == 0:
        return float("-inf")
    l = _lufs(p)
    g = p[l > ABS_GATE]
    if g.size == 0:
        return float("-inf")
    rel = _lufs(np.array([g.mean()]))[0] - 10.0
    g2 = p[(l > ABS_GATE) & (l > rel)]
    return float(_lufs(np.array([g2.mean()]))[0]) if g2.size else float("-inf")


def loudness_range(x: np.ndarray, rate: int) -> float:
    """EBU Tech 3342 LRA: short-term (3 s, 10 Hz) values, -70 abs and -20 LU relative gates, 10th..95th percentile."""
    p, _ = _block_power(x, rate, 3.0, 0.1)
    if p.size == 0:
        return 0.0
    l = _lufs(p)
    g = p[l > ABS_GATE]
    if g.size == 0:
        return 0.0
    rel = _lufs(np.array([g.mean()]))[0] - 20.0
    v = l[(l > ABS_GATE) & (l > rel)]
    if v.size < 2:
        return 0.0
    return float(np.percentile(v, 95) - np.percentile(v, 10))


def oversampled_abs_max(x: np.ndarray, factor: int = 4, chunk: int = 1 << 19, pad: int = 512) -> np.ndarray:
    """|x| upsampled `factor` times (polyphase FIR), reduced back to one value per original sample:
    out[c, i] = max over the `factor` sub-samples belonging to sample i. Chunked to keep memory low."""
    C, n = x.shape
    out = np.empty((C, n))
    for s in range(0, n, chunk):
        a, b = max(0, s - pad), min(n, s + chunk + pad)
        up = resample_poly(x[:, a:b], factor, 1, axis=-1)
        up = np.abs(up[:, (s - a) * factor:(s - a + min(chunk, n - s)) * factor])
        out[:, s:s + up.shape[1] // factor] = up.reshape(C, -1, factor).max(axis=-1)
    return np.maximum(out, np.abs(x))


def true_peak_db(x: np.ndarray) -> float:
    m = float(oversampled_abs_max(x).max()) if x.size else 0.0
    return 20.0 * math.log10(m) if m > 0 else -300.0


def sample_peak_db(x: np.ndarray) -> float:
    m = float(np.abs(x).max()) if x.size else 0.0
    return 20.0 * math.log10(m) if m > 0 else -300.0


def inter_sample_overs(x: np.ndarray, ceiling_db: float = 0.0) -> int:
    """How many samples have a 4x-oversampled peak above the ceiling (0 dBFS = clipping on a DAC)."""
    return int((oversampled_abs_max(x).max(axis=0) > 10.0 ** (ceiling_db / 20.0)).sum())


def full_report(x: np.ndarray, rate: int, timeline_hop_s: float = 3.0) -> dict:
    m, _ = momentary(x, rate)
    s, _ = short_term(x, rate)
    st_tl, st_t = short_term(x, rate, hop_s=timeline_hop_s)
    i = integrated(x, rate)
    return {
        "integrated_lufs": _r(i),
        "integrated_lufs_crosscheck": _r(integrated_own(x, rate)),
        "momentary_max_lufs": _r(float(m.max()) if m.size else float("-inf")),
        "short_term_max_lufs": _r(float(s.max()) if s.size else float("-inf")),
        "lra_lu": _r(loudness_range(x, rate)),
        "true_peak_dbtp": _r(true_peak_db(x)),
        "sample_peak_dbfs": _r(sample_peak_db(x)),
        "short_term_timeline": {"hop_s": timeline_hop_s, "start_s": [round(float(t), 1) for t in st_t],
                                "lufs": [_r(float(v), 1) for v in st_tl]},
    }


def _r(v: float, nd: int = 2):
    if v is None or not math.isfinite(v):
        return None
    return round(float(v), nd)
