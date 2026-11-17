"""Shared DSP building blocks: RBJ biquads, band detectors, block-rate envelopes, fractional delay."""
from __future__ import annotations

import math

import numpy as np
from scipy.ndimage import minimum_filter1d, uniform_filter1d
from scipy.signal import fftconvolve, sosfilt

FILTER_TYPES = ("peak", "lowshelf", "highshelf", "highpass", "lowpass", "notch", "bandpass")


def biquad(kind: str, freq: float, rate: float, gain_db: float = 0.0, q: float = 0.7071) -> np.ndarray:
    """RBJ Audio EQ Cookbook biquad as one SOS row [b0 b1 b2 1 a1 a2]. bandpass = 0 dB peak gain."""
    freq = float(np.clip(freq, 5.0, 0.49 * rate))
    q = float(max(q, 0.05))
    w0 = 2.0 * math.pi * freq / rate
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2.0 * q)
    A = 10.0 ** (gain_db / 40.0)
    if kind == "peak":
        b = [1 + alpha * A, -2 * cw, 1 - alpha * A]
        a = [1 + alpha / A, -2 * cw, 1 - alpha / A]
    elif kind == "lowshelf":
        sa = 2 * math.sqrt(A) * alpha
        b = [A * ((A + 1) - (A - 1) * cw + sa), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - sa)]
        a = [(A + 1) + (A - 1) * cw + sa, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - sa]
    elif kind == "highshelf":
        sa = 2 * math.sqrt(A) * alpha
        b = [A * ((A + 1) + (A - 1) * cw + sa), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - sa)]
        a = [(A + 1) - (A - 1) * cw + sa, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - sa]
    elif kind == "highpass":
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == "lowpass":
        b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == "notch":
        b = [1, -2 * cw, 1]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == "bandpass":
        b = [alpha, 0.0, -alpha]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    else:
        raise ValueError(f"unknown filter type {kind}")
    a0 = a[0]
    return np.array([[b[0] / a0, b[1] / a0, b[2] / a0, 1.0, a[1] / a0, a[2] / a0]])


def filt(x: np.ndarray, sos: np.ndarray) -> np.ndarray:
    return sosfilt(sos, x, axis=-1)


def region_mask(n: int, rate: int, start_s: float | None, end_s: float | None, fade_ms: float = 40.0) -> np.ndarray | None:
    """0..1 weight that is 1 inside [start, end] with raised-cosine fades, None for the whole file."""
    if start_s is None and end_s is None:
        return None
    a = 0 if start_s is None else int(max(0.0, start_s) * rate)
    b = n if end_s is None else int(min(n / rate, end_s) * rate)
    if b <= a:
        return np.zeros(n)
    m = np.zeros(n)
    m[a:b] = 1.0
    f = max(1, int(fade_ms * 1e-3 * rate))
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, math.pi, f))
    lo = max(0, a - f)
    m[lo:a] = ramp[f - (a - lo):]
    hi = min(n, b + f)
    m[b:hi] = ramp[::-1][:hi - b]
    return m


def block_power(x: np.ndarray, block: int) -> np.ndarray:
    """Mean square per block of a (C, n) or (n,) signal, channels summed/2 (linked detector)."""
    x = np.atleast_2d(x)
    n = x.shape[-1]
    nb = n // block + (1 if n % block else 0)
    pad = nb * block - n
    y = np.pad(x, ((0, 0), (0, pad)))
    return (y * y).reshape(x.shape[0], nb, block).mean(axis=-1).mean(axis=0)


def to_db(p: np.ndarray, floor: float = -150.0) -> np.ndarray:
    with np.errstate(divide="ignore"):
        return np.maximum(10.0 * np.log10(np.maximum(p, 1e-30)), floor)


def ballistics(target_db: np.ndarray, attack_ms: float, release_ms: float, block_s: float) -> np.ndarray:
    """Smooth a per-block gain change in dB (<= 0 cuts). Attack = moving to more cut, release = back."""
    ka = math.exp(-block_s / max(attack_ms * 1e-3, 1e-6))
    kr = math.exp(-block_s / max(release_ms * 1e-3, 1e-6))
    out = np.empty_like(target_db)
    g = 0.0
    for i, t in enumerate(target_db.tolist()):
        k = ka if t < g else kr
        g = t + (g - t) * k
        out[i] = g
    return out


def blocks_to_samples(v: np.ndarray, n: int, block: int) -> np.ndarray:
    centres = (np.arange(v.size) + 0.5) * block
    return np.interp(np.arange(n), centres, v)


def db_to_gain(db):
    return np.power(10.0, np.asarray(db) / 20.0)


def fractional_delay(x: np.ndarray, delay: float, taps: int = 64) -> np.ndarray:
    """Delay (positive) or advance (negative) a (C, n) signal by any number of samples.
    Whole samples are a shift with silence fill; the fraction is a Kaiser-windowed sinc."""
    n = x.shape[-1]
    whole = math.floor(delay)
    frac = delay - whole
    y = np.zeros_like(x)
    if whole >= 0:
        if whole < n:
            y[:, whole:] = x[:, :n - whole]
    else:
        if -whole < n:
            y[:, :n + whole] = x[:, -whole:]
    if frac < 1e-6:
        return y
    centre = taps // 2 - 1
    k = np.arange(taps)
    h = np.sinc(k - centre - frac) * np.kaiser(taps, 8.0)
    h /= h.sum()
    out = np.stack([fftconvolve(c, h)[centre:centre + n] for c in y])
    return out


def mono_below(x: np.ndarray, rate: int, hz: float) -> np.ndarray:
    """Remove the side signal below `hz` (zero-phase 4th-order low-pass on S, subtracted)."""
    if hz <= 0:
        return x
    from scipy.signal import butter, sosfiltfilt
    m = 0.5 * (x[0] + x[1])
    s = 0.5 * (x[0] - x[1])
    sos = butter(2, min(hz, 0.45 * rate), btype="lowpass", fs=rate, output="sos")   # x2 passes = 4th order, zero phase
    s = s - sosfiltfilt(sos, s)
    return np.stack([m + s, m - s])


def true_peak_limit(x: np.ndarray, rate: int, ceiling_db: float, release_ms: float = 80.0,
                    lookahead_ms: float = 2.5) -> tuple[np.ndarray, float]:
    """Offline look-ahead limiter with a 4x oversampled (true-peak) detector, linked stereo.
    The gain curve is built so it is never above what each sample needs (min filter + box smooth
    of the same width), so peaks are caught without overshoot from the smoothing.
    Returns (limited signal, most gain reduction in dB)."""
    from .loudness import oversampled_abs_max
    c = 10.0 ** (ceiling_db / 20.0)
    peak = oversampled_abs_max(x).max(axis=0)
    need = np.minimum(1.0, c / np.maximum(peak, 1e-12))
    # Release: block-rate hold-and-recover so the gain does not snap back between peaks.
    blk = 64
    nb = need.size // blk + (1 if need.size % blk else 0)
    padded = np.pad(need, (0, nb * blk - need.size), constant_values=1.0)
    bmin = padded.reshape(nb, blk).min(axis=1)
    kr = math.exp(-(blk / rate) / (release_ms * 1e-3))
    r = np.empty(nb)
    g = 1.0
    for i, v in enumerate(bmin.tolist()):
        g = v if v < g else v + (g - v) * kr
        r[i] = g
    pre = np.repeat(r, blk)[:need.size]
    K = max(3, int(lookahead_ms * 1e-3 * rate) | 1)
    gain = uniform_filter1d(minimum_filter1d(pre, size=K, mode="nearest"), size=K, mode="nearest")
    gain = np.minimum(gain, 1.0)
    return x * gain, float(20.0 * np.log10(max(gain.min(), 1e-12)))
