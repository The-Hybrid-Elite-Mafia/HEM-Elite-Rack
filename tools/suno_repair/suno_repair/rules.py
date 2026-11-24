"""Offline planner: the same manifest format, built by fixed rules from the telemetry.
Used with --planner rules (no API key, no cost) and as the yardstick to compare Claude's plan against."""
from __future__ import annotations


def neutral_stem(name: str) -> dict:
    return {
        "stem": name, "gain_db": 0.0, "delay_samples": 0.0, "polarity_invert": False, "mono_below_hz": 0.0,
        "eq": [], "dynamic_notches": [],
        "deesser": {"enabled": False, "freq_hz": 6500.0, "q": 1.5, "threshold_db": -12.0, "max_cut_db": 6.0},
        "bleed_gate": {"enabled": False, "threshold_dbfs": -60.0, "reduction_db": 12.0, "attack_ms": 2.0, "release_ms": 120.0},
        "sidechain_duck": {"enabled": False, "trigger_stem": None, "band_low_hz": 40.0, "band_high_hz": 120.0,
                           "depth_db": 3.0, "attack_ms": 5.0, "release_ms": 90.0},
        "rationale": "no measured problem",
    }


def neutral_master() -> dict:
    return {"eq": [], "glue": {"enabled": False, "threshold_dbfs": -18.0, "ratio": 2.0, "attack_ms": 30.0,
                               "release_ms": 150.0, "makeup_db": 0.0},
            "mono_below_hz": 0.0, "rationale": "no measured problem"}


def plan(tel: dict, target_lufs: float, ceiling_dbtp: float) -> dict:
    stems = []
    bleed = tel["cross_stem"]["bleed"]
    rows = bleed["rows_leak_into_columns"]
    kb = tel["cross_stem"]["kick_bass_40_350"]
    for name, s in tel["stems"].items():
        m = neutral_stem(name)
        why = []
        sh = s.get("shift") or {}
        if sh.get("offset_samples") is not None and abs(sh["offset_samples"]) >= 0.5 and (sh.get("match_strength_psr") or 0) >= 6.0:
            m["delay_samples"] = round(-float(sh["offset_samples"]), 2)
            why.append(f"offset {sh['offset_samples']} samples")
        if sh.get("polarity_inverted"):
            m["polarity_invert"] = True
            why.append("polarity inverted vs mix")
        st = s["stereo"]
        if st.get("sub_phase_problem"):
            m["mono_below_hz"] = 120.0
            why.append(f"sub corr {st.get('sub_lt150_corr')}")
        for r in s.get("resonances_2k_8k", []):
            if r["kind"] == "stationary_artifact" and (r.get("present_pct_of_song") or 0) >= 90 and len(m["eq"]) < 12:
                # always there and steady: a fixed notch takes it out completely
                m["eq"].append({"type": "notch", "freq_hz": float(r["hz"]), "gain_db": 0.0,
                                "q": float(min(max(r["q_estimate"] or 20.0, 8.0), 40.0)), "start_s": None, "end_s": None})
                why.append(f"steady tone {r['hz']} Hz (notch)")
            elif r["kind"] == "stationary_artifact" and len(m["dynamic_notches"]) < 8:
                m["dynamic_notches"].append({"freq_hz": float(r["hz"]), "q": float(min(max(r["q_estimate"] or 8.0, 4.0), 40.0)),
                                             "threshold_db": 3.0, "max_cut_db": float(min((r["prominence_db"] or 6.0) + 3.0, 18.0)),
                                             "attack_ms": 5.0, "release_ms": 120.0})
                why.append(f"stationary tone {r['hz']} Hz")
        for h in s.get("hot_spots", []):
            if len(m["eq"]) >= 12:
                break
            if h["band"] == "mud":
                m["eq"].append({"type": "peak", "freq_hz": 320.0, "gain_db": -min(3.0, 0.5 * (h["excess_db"] or 4.0)), "q": 1.0,
                                "start_s": h["start_s"], "end_s": h["end_s"]})
                why.append(f"mud {h['start_s']}-{h['end_s']} s")
            elif h["band"] == "harsh":
                m["eq"].append({"type": "peak", "freq_hz": 3500.0, "gain_db": -min(3.0, 0.5 * (h["excess_db"] or 4.0)), "q": 1.2,
                                "start_s": h["start_s"], "end_s": h["end_s"]})
                why.append(f"harsh {h['start_s']}-{h['end_s']} s")
            elif h["band"] == "sibilance" and s["role"] in ("vocal", "backing_vocal"):
                m["deesser"].update(enabled=True)
        if m["deesser"]["enabled"]:
            why.append("sibilance spikes")
        # bleed into this stem
        if name in rows and (s.get("active_pct") or 100) <= 85:
            j = rows.index(name)
            worst = max((row[j] for row in bleed["bleed_db"] if row[j] is not None), default=None)
            if worst is not None and worst > -35.0:
                thr = bleed["gate_threshold_hint_dbfs"].get(name)
                if thr is not None:
                    m["bleed_gate"].update(enabled=True, threshold_dbfs=float(max(-90.0, min(-10.0, thr))), reduction_db=15.0)
                    why.append(f"bleed {worst} dB in gaps")
        if kb.get("overlap") is not None and kb["overlap"] > 0.35 and name == kb.get("bass_stem"):
            c = kb.get("most_collision_hz") or 60.0
            m["sidechain_duck"].update(enabled=True, trigger_stem=kb["kick_stem"], band_low_hz=float(max(20.0, c / 1.6)),
                                       band_high_hz=float(min(4000.0, c * 1.6)), depth_db=3.0)
            why.append(f"kick/bass overlap {kb['overlap']} at {c} Hz")
        if why:
            m["rationale"] = "; ".join(why)[:400]
        stems.append(m)
    by = {m["stem"]: m for m in stems}
    # T4: every 3-6 kHz spike over 6 dB gets a dynamic notch (cut only while it stands out)
    for name, s in tel["stems"].items():
        m = by[name]
        have = {round(n["freq_hz"]) for n in m["dynamic_notches"]} | {round(b["freq_hz"]) for b in m["eq"] if b["type"] == "notch"}
        sp = sorted(s.get("spikes_3k_6k", []), key=lambda r: -(r["prominence_db"] or 0))
        added = 0
        for r in sp:
            if round(r["hz"]) in have:
                continue
            q = float(min(max(r["q_estimate"] or 8.0, 4.0), 40.0))
            if len(m["dynamic_notches"]) < 24:
                m["dynamic_notches"].append({"freq_hz": float(r["hz"]), "q": q, "threshold_db": 2.0,
                                             "max_cut_db": float(min(r["prominence_db"] or 6.0, 18.0)),
                                             "attack_ms": 2.0, "release_ms": 80.0})
            elif len(m["eq"]) < 12:
                m["eq"].append({"type": "peak", "freq_hz": float(r["hz"]), "gain_db": -float(min(max((r["prominence_db"] or 6.0) - 4.0, 1.0), 18.0)),
                                "q": q, "start_s": None, "end_s": None})
            added += 1
        if added:
            m["rationale"] = (m["rationale"] if m["rationale"] != "no measured problem" else "") + f"; {added} spikes 3-6 kHz"
    # T3: carve 200-500 Hz in the less important stem of every pair that overlaps >= 0.08
    quieter = tel["cross_stem"]["masking_200_500"].get("quieter_in_band", {})
    cut = {}
    pairs = sorted(((p, o) for p, o in (tel["cross_stem"]["masking_200_500"]["pairs"] or {}).items() if o), key=lambda x: -x[1])
    chosen = [(p, o) for p, o in pairs if o >= 0.08] or [(p, o) for p, o in pairs[:2] if o > 0.005]
    for pair, ov in chosen:
        t = quieter.get(pair)
        if t is None:
            continue
        for t in [t]:
            cut[t] = max(cut.get(t, 0.0), min(4.0, 2.0 + 5.0 * (ov - 0.08)))
    for name, db in cut.items():
        m = by[name]
        if len(m["eq"]) < 12:
            m["eq"].append({"type": "peak", "freq_hz": 300.0, "gain_db": -round(db, 1), "q": 1.0, "start_s": None, "end_s": None})
            m["rationale"] = (m["rationale"] if m["rationale"] != "no measured problem" else "") + f"; 200-500 Hz masking cut {round(db, 1)} dB"
    for m in stems:
        m["rationale"] = m["rationale"].lstrip("; ")[:400]
    master = neutral_master()
    ss = tel["stem_sum"]
    if ss.get("sub_lt150_corr") is not None and ss["sub_lt150_corr"] < 0.85:
        master["mono_below_hz"] = 120.0
        master["rationale"] = f"sum sub corr {ss['sub_lt150_corr']}"
    return {"target_lufs": float(target_lufs), "true_peak_ceiling_dbtp": float(ceiling_dbtp), "stems": stems, "master": master}


def fill_missing(manifest: dict, stem_names: list[str]) -> dict:
    """Stems Claude left out get neutral settings (the session always renders every stem)."""
    have = {s["stem"] for s in manifest.get("stems", [])}
    for n in stem_names:
        if n not in have:
            manifest.setdefault("stems", []).append(neutral_stem(n))
    manifest.setdefault("rack_chain", [])
    return manifest
