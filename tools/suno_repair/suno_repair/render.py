"""Runs a repair manifest on the raw stems (offline, 64-bit float inside, 32-bit float WAV out).

Per stem, in this order: polarity -> delay -> gain -> mono below -> EQ (time-ranged bands
cross-fade in and out) -> dynamic notches -> de-esser -> bleed gate -> sidechain duck
[-> rack chain]. Master: sum -> EQ -> glue -> mono below [-> rack chain] -> loudness to target
with a true-peak limiter (always last, so the ceiling holds whatever came before).
"""
from __future__ import annotations

import math
import tempfile
from pathlib import Path

import numpy as np

from . import loudness as lu
from . import rack
from .audio_io import Session, write_wav
from .dsp import (ballistics, biquad, block_power, blocks_to_samples, db_to_gain, filt, fractional_delay,
                  mono_below, region_mask, to_db, true_peak_limit)

BLOCK_MS = 2.0     # control rate of the dynamic stages


def apply_eq(x: np.ndarray, rate: int, bands: list[dict]) -> np.ndarray:
    for b in bands:
        sos = biquad(b["type"], b["freq_hz"], rate, b.get("gain_db", 0.0), b.get("q", 0.7071))
        y = filt(x, sos)
        m = region_mask(x.shape[1], rate, b.get("start_s"), b.get("end_s"))
        x = y if m is None else x + (y - x) * m
    return x


def _dynamic_band(x: np.ndarray, rate: int, freq: float, q: float, cut_db_blocks: np.ndarray, block: int) -> np.ndarray:
    """y = x - k(t) * bandpass(x): an RBJ band-pass with 0 dB peak subtracted gives exactly the RBJ
    notch at k = 1, so k = 1 - 10^(-cut/20) is a cut of `cut` dB at the centre that fades to 0 away from it."""
    bp = filt(x, biquad("bandpass", freq, rate, 0.0, q))
    k = 1.0 - db_to_gain(-np.maximum(cut_db_blocks, 0.0))
    return x - bp * blocks_to_samples(k, x.shape[1], block)


def dynamic_notch(x: np.ndarray, rate: int, n: dict) -> tuple[np.ndarray, float]:
    """Cut when the narrow band stands more than threshold_db above its neighbourhood (a 2-octave
    band around it), corrected for the bandwidth difference so a flat spectrum reads 0 dB."""
    block = max(16, int(BLOCK_MS * 1e-3 * rate))
    f, q = n["freq_hz"], n["q"]
    narrow = block_power(filt(x, biquad("bandpass", f, rate, 0.0, q)), block)
    wide = block_power(filt(x, biquad("bandpass", f, rate, 0.0, 0.67)), block)
    excess = to_db(narrow) - to_db(wide) - 10.0 * math.log10(0.67 / q)
    want = -np.clip(excess - n["threshold_db"], 0.0, n["max_cut_db"])
    g = ballistics(want, n["attack_ms"], n["release_ms"], block / rate)
    return _dynamic_band(x, rate, f, q, -g, block), float(g.min()) if g.size else 0.0


def deesser(x: np.ndarray, rate: int, d: dict) -> tuple[np.ndarray, float]:
    block = max(16, int(BLOCK_MS * 1e-3 * rate))
    band = block_power(filt(x, biquad("bandpass", d["freq_hz"], rate, 0.0, d["q"])), block)
    full = block_power(x, block)
    rel = to_db(band) - to_db(full)
    want = -np.clip(rel - d["threshold_db"], 0.0, d["max_cut_db"])
    g = ballistics(want, 1.0, 60.0, block / rate)
    return _dynamic_band(x, rate, d["freq_hz"], d["q"], -g, block), float(g.min()) if g.size else 0.0


def bleed_gate(x: np.ndarray, rate: int, gte: dict) -> tuple[np.ndarray, float]:
    """Downward expander: below threshold the stem is turned down by reduction_db (soft 6 dB knee)."""
    block = max(16, int(BLOCK_MS * 1e-3 * rate))
    lvl = to_db(block_power(x, block))
    under = np.clip((gte["threshold_dbfs"] - lvl) / 6.0, 0.0, 1.0)
    want = -gte["reduction_db"] * under
    # ballistics() calls "more cut" attack; for a gate more cut is closing (its release), less cut is opening (its attack)
    g = ballistics(want, gte["release_ms"], gte["attack_ms"], block / rate)
    return x * blocks_to_samples(db_to_gain(g), x.shape[1], block), float(g.min()) if g.size else 0.0


def sidechain_duck(x: np.ndarray, trigger: np.ndarray, rate: int, sc: dict) -> tuple[np.ndarray, float]:
    """Turn the target's band down while the trigger's same band is loud (envelope-followed, by depth)."""
    block = max(16, int(BLOCK_MS * 1e-3 * rate))
    lo, hi = sc["band_low_hz"], sc["band_high_hz"]
    fc = math.sqrt(lo * hi)
    q = fc / max(hi - lo, 1.0)
    trig = to_db(block_power(filt(trigger, biquad("bandpass", fc, rate, 0.0, q)), block))
    act = trig > -140.0
    if not act.any():
        return x, 0.0
    top = np.percentile(trig[act], 98)
    amount = np.clip((trig - (top - 24.0)) / 24.0, 0.0, 1.0)      # 0 when the trigger is 24 dB under its peaks, 1 at its peaks
    want = -sc["depth_db"] * amount
    g = ballistics(want, sc["attack_ms"], sc["release_ms"], block / rate)
    return _dynamic_band(x, rate, fc, q, -g, block), float(g.min())


def glue(x: np.ndarray, rate: int, c: dict) -> tuple[np.ndarray, float]:
    block = max(16, int(BLOCK_MS * 1e-3 * rate))
    lvl = to_db(block_power(x, block))
    over = np.maximum(lvl - c["threshold_dbfs"], 0.0)
    want = -over * (1.0 - 1.0 / c["ratio"])
    g = ballistics(want, c["attack_ms"], c["release_ms"], block / rate)
    return x * blocks_to_samples(db_to_gain(g + c["makeup_db"]), x.shape[1], block), float(g.min())


def finish_loudness(x: np.ndarray, rate: int, target_lufs: float, ceiling_dbtp: float, log: dict) -> np.ndarray:
    """Gain to target, true-peak limit, re-measure, correct; then confirm the ceiling on the result."""
    current = lu.integrated(x, rate)
    if not math.isfinite(current):
        log["error"] = "master is silent"
        return x
    limit_at = ceiling_dbtp - 0.2       # margin for inter-sample peaks the gain changes create
    g = target_lufs - current
    y, gr = x, 0.0
    for outer in range(4):
        for it in range(8):
            y, gr = true_peak_limit(x * 10 ** (g / 20.0), rate, limit_at)
            got = lu.integrated(y, rate)
            err = target_lufs - got
            log.setdefault("iterations", []).append({"gain_db": round(g, 3), "lufs": round(got, 3), "limiter_gr_db": round(gr, 2),
                                                     "ceiling_dbtp": round(limit_at, 2)})
            if abs(err) <= 0.05:
                break
            g += err
        tp = lu.true_peak_db(y)
        if tp <= ceiling_dbtp:
            break
        limit_at -= (tp - ceiling_dbtp) + 0.05
    log["final_gain_db"] = round(g, 3)
    log["max_limiter_gr_db"] = round(gr, 2)
    return y


def render(session: Session, manifest: dict, out: Path, rack_exe: Path | None = None, tempo: float | None = None) -> dict:
    rate = session.rate
    log = {"stems": {}, "master": {}, "rack": []}
    by_name = {m["stem"]: m for m in manifest["stems"]}
    rack_steps = manifest.get("rack_chain") or []
    if rack_steps and rack_exe is None:
        log["rack_skipped"] = "rack_chain present but no --rack-render exe given; built-in stages only"

    # Pass 1: alignment (every stem, so sidechain triggers are aligned too)
    aligned = {}
    for s in session.stems:
        m = by_name[s.name]
        x = s.audio * (-1.0 if m["polarity_invert"] else 1.0)
        if abs(m["delay_samples"]) > 1e-6:
            x = fractional_delay(x, m["delay_samples"])
        aligned[s.name] = x * 10 ** (m["gain_db"] / 20.0)

    # Pass 2: repairs
    clean = {}
    for s in session.stems:
        m = by_name[s.name]
        x = aligned[s.name]
        L = {}
        x = mono_below(x, rate, m["mono_below_hz"])
        x = apply_eq(x, rate, m["eq"])
        L["dynamic_notches_max_cut_db"] = []
        for n in m["dynamic_notches"]:
            x, gr = dynamic_notch(x, rate, n)
            L["dynamic_notches_max_cut_db"].append(round(gr, 2))
        if m["deesser"]["enabled"]:
            x, L["deesser_max_cut_db"] = deesser(x, rate, m["deesser"])
        if m["bleed_gate"]["enabled"]:
            x, L["bleed_gate_max_cut_db"] = bleed_gate(x, rate, m["bleed_gate"])
        sc = m["sidechain_duck"]
        if sc["enabled"] and sc.get("trigger_stem") in aligned:
            x, L["sidechain_max_cut_db"] = sidechain_duck(x, aligned[sc["trigger_stem"]], rate, sc)
        x = _rack(x, rate, [st for st in rack_steps if st["target"] == s.name], rack_exe, tempo, log, s.name)
        clean[s.name] = x
        log["stems"][s.name] = L

    stems_dir = out / "stems"
    for name, x in clean.items():
        write_wav(stems_dir / f"{name}_clean.wav", x, rate)
        write_wav(stems_dir / f"{name}_removed.wav", aligned[name] - x, rate)     # what the repairs took out

    # Master
    mm = manifest["master"]
    mix = np.sum(list(clean.values()), axis=0)
    mix = apply_eq(mix, rate, mm["eq"])
    if mm["glue"]["enabled"]:
        mix, log["master"]["glue_max_gr_db"] = glue(mix, rate, mm["glue"])
    mix = mono_below(mix, rate, mm["mono_below_hz"])
    mix = _rack(mix, rate, [st for st in rack_steps if st["target"] == "master"], rack_exe, tempo, log, "master")
    master = finish_loudness(mix, rate, manifest["target_lufs"], manifest["true_peak_ceiling_dbtp"], log["master"])
    write_wav(out / "master.wav", master, rate)

    # Delta: the reference (or the raw stem sum), level-matched to the master, minus the master.
    ref = session.mix.audio if session.mix is not None else np.sum([s.audio for s in session.stems], axis=0)
    from .forensics import gcc_phat
    seg = slice(0, min(ref.shape[1], rate * 30))
    lag, _, _ = gcc_phat(0.5 * (ref[0, seg] + ref[1, seg]), 0.5 * (master[0, seg] + master[1, seg]), int(0.05 * rate))
    ref_al = fractional_delay(ref, -lag) if abs(lag) > 1e-3 else ref
    li, lm = lu.integrated(ref_al, rate), lu.integrated(master, rate)
    match_db = (lm - li) if math.isfinite(li) and math.isfinite(lm) else 0.0
    write_wav(out / "delta_original_minus_processed.wav", ref_al * 10 ** (match_db / 20.0) - master, rate)
    log["delta"] = {"reference": "mix" if session.mix is not None else "raw stem sum", "ref_lag_samples": round(lag, 2),
                    "ref_level_match_db": round(match_db, 2)}
    return {"log": log, "clean": clean, "aligned": aligned, "master": master}


def _rack(x: np.ndarray, rate: int, steps: list[dict], exe: Path | None, tempo, log: dict, target: str) -> np.ndarray:
    if not steps or exe is None:
        return x
    with tempfile.TemporaryDirectory(prefix="hem_rack_") as td:
        td = Path(td)
        rack.write_chain(steps, td / "chain.hemchain")
        write_wav(td / "in.wav", x, rate)
        rep = rack.run_chain(exe, td / "chain.hemchain", td / "in.wav", td / "out.wav", td / "report.json", tempo)
        from .audio_io import read_stereo
        y, _, _ = read_stereo(td / "out.wav", rate)
    rep["target"] = target
    log["rack"].append(rep)
    return y
