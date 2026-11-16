"""Objective pass/fail checks on the rendered files (measured again from the output, not taken from the plan)."""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
from scipy.signal import butter, sosfiltfilt

from . import loudness as lu
from .forensics import _corr, masking_matrix, mono_loss_db, resonance_count, spectra


def sub_corr(x: np.ndarray, rate: int, hz: float = 150.0) -> float | None:
    sos = butter(4, hz, btype="lowpass", fs=rate, output="sos")
    b = sosfiltfilt(sos, x, axis=-1)
    return _corr(b[0], b[1])


def verify(rate: int, aligned: dict, clean: dict, master: np.ndarray, target_lufs: float, ceiling_dbtp: float,
           reference: np.ndarray | None, out: Path) -> dict:
    checks = []

    def check(name, value, ok, rule):
        checks.append({"check": name, "value": value, "pass": bool(ok), "rule": rule})

    I = lu.integrated(master, rate)
    tp = lu.true_peak_db(master)
    overs = lu.inter_sample_overs(master, 0.0)
    sc = sub_corr(master, rate)
    ml = mono_loss_db(master)
    finite = bool(np.isfinite(master).all())
    dc = [float(master[0].mean()), float(master[1].mean())]
    clips = int((np.abs(master) > 1.0).sum())

    check("integrated loudness (LUFS)", _r(I), math.isfinite(I) and abs(I - target_lufs) <= 0.2, f"{target_lufs} +/- 0.2 LU")
    check("true peak (dBTP, 4x oversampled)", _r(tp), tp <= ceiling_dbtp, f"<= {ceiling_dbtp}")
    check("inter-sample overs above 0 dBFS", overs, overs == 0, "0")
    check("sub < 150 Hz L/R correlation", _r(sc), sc is None or sc >= 0.85, ">= +0.85")
    check("mono fold-down loss (dB)", _r(ml), ml < 1.5, "< 1.5 dB")
    check("NaN / Inf samples", 0 if finite else int((~np.isfinite(master)).sum()), finite, "0")
    check("DC offset (max of L/R)", _r(max(abs(d) for d in dc), 6), max(abs(d) for d in dc) < 1e-4, "< 0.0001 (-80 dBFS)")
    check("samples over 0 dBFS", clips, clips == 0, "0")

    names = list(clean.keys())
    sp_pre = {n: spectra(aligned[n], rate) for n in names}
    sp_post = {n: spectra(clean[n], rate) for n in names}
    mpre = masking_matrix(names, sp_pre)["mean_overlap"]
    mpost = masking_matrix(names, sp_post)["mean_overlap"]
    check("200-500 Hz cross-stem overlap (mean, 0..1)", {"pre": mpre, "post": mpost}, mpost < mpre or mpre < 0.05,
          "post lower than pre (or nothing to fix: pre < 0.05)")
    rpre = {n: resonance_count(sp_pre[n]) for n in names}
    rpost = {n: resonance_count(sp_post[n]) for n in names}
    tpre, tpost = sum(rpre.values()), sum(rpost.values())
    check("3-6 kHz resonant peaks > 6 dB prominence (all stems)", {"pre": tpre, "post": tpost, "per_stem_pre": rpre,
          "per_stem_post": rpost}, tpost < tpre or tpre == 0, "fewer after (or none before)")

    stems_ok = {}
    for n, x in clean.items():
        stems_ok[n] = {"finite": bool(np.isfinite(x).all()), "dc": _r(float(np.abs(x.mean(axis=1)).max()), 6),
                       "sample_peak_dbfs": _r(lu.sample_peak_db(x))}
    bad = [n for n, v in stems_ok.items() if not v["finite"]]
    check("clean stems: NaN / Inf", bad, not bad, "none")

    pre_ref = None
    if reference is not None:
        pre_ref = {"integrated_lufs": _r(lu.integrated(reference, rate)), "true_peak_dbtp": _r(lu.true_peak_db(reference)),
                   "sub_lt150_corr": _r(sub_corr(reference, rate)), "mono_fold_down_loss_db": _r(mono_loss_db(reference))}
    res = {"all_pass": all(c["pass"] for c in checks), "checks": checks, "clean_stems": stems_ok,
           "original_mix_for_comparison": pre_ref}
    (out / "verification.json").write_text(json.dumps(res, indent=2), encoding="utf-8")
    (out / "verification.md").write_text(_md(res), encoding="utf-8")
    return res


def _md(res: dict) -> str:
    lines = [f"# Verification: {'ALL PASS' if res['all_pass'] else 'FAILED'}", "", "| Check | Value | Rule | Result |", "|---|---|---|---|"]
    for c in res["checks"]:
        v = c["value"]
        if isinstance(v, dict):
            v = ", ".join(f"{k} {w}" for k, w in v.items() if not isinstance(w, dict))
        lines.append(f"| {c['check']} | {v} | {c['rule']} | {'PASS' if c['pass'] else 'FAIL'} |")
    if res.get("original_mix_for_comparison"):
        lines += ["", "Original mix: " + ", ".join(f"{k} {v}" for k, v in res["original_mix_for_comparison"].items())]
    return "\n".join(lines) + "\n"


def _r(v, nd=2):
    if v is None:
        return None
    v = float(v)
    return round(v, nd) if math.isfinite(v) else None
