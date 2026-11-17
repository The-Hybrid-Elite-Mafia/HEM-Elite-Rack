"""The repair manifest: the only thing Claude is allowed to answer with (forced tool_use).

Every object lists all of its keys as required and forbids extra keys, so a manifest either
matches exactly or is rejected. Ranges are also checked locally (validate()), because not every
API mode enforces numeric limits.
"""
from __future__ import annotations

import copy

from .dsp import FILTER_TYPES

NUM_NULL = {"type": ["number", "null"]}


def _obj(props: dict) -> dict:
    return {"type": "object", "properties": props, "required": list(props.keys()), "additionalProperties": False}


def _num(lo: float, hi: float, desc: str = "") -> dict:
    d = {"type": "number", "minimum": lo, "maximum": hi}
    if desc:
        d["description"] = desc
    return d


EQ_BAND = _obj({
    "type": {"type": "string", "enum": list(FILTER_TYPES)},
    "freq_hz": _num(20, 20000),
    "gain_db": _num(-18, 12, "ignored by highpass/lowpass/notch/bandpass"),
    "q": _num(0.1, 40),
    "start_s": {"type": ["number", "null"], "description": "null = whole song; else the band only acts in this time range"},
    "end_s": {"type": ["number", "null"]},
})

DYN_NOTCH = _obj({
    "freq_hz": _num(1000, 16000),
    "q": _num(2, 40),
    "threshold_db": _num(0, 18, "cut starts when the narrow band stands this many dB above its neighbourhood"),
    "max_cut_db": _num(0, 18),
    "attack_ms": _num(0.5, 100),
    "release_ms": _num(10, 1000),
})

DEESSER = _obj({
    "enabled": {"type": "boolean"},
    "freq_hz": _num(3000, 12000),
    "q": _num(0.5, 4),
    "threshold_db": _num(-40, 0, "sibilant band level relative to the full-band level"),
    "max_cut_db": _num(0, 18),
})

BLEED_GATE = _obj({
    "enabled": {"type": "boolean"},
    "threshold_dbfs": _num(-90, -10),
    "reduction_db": _num(0, 40),
    "attack_ms": _num(0.5, 50),
    "release_ms": _num(10, 1000),
})

SIDECHAIN = _obj({
    "enabled": {"type": "boolean"},
    "trigger_stem": {"type": ["string", "null"]},
    "band_low_hz": _num(20, 2000),
    "band_high_hz": _num(30, 4000),
    "depth_db": _num(0, 18),
    "attack_ms": _num(0.5, 100),
    "release_ms": _num(10, 1000),
})

GLUE = _obj({
    "enabled": {"type": "boolean"},
    "threshold_dbfs": _num(-40, 0),
    "ratio": _num(1, 10),
    "attack_ms": _num(0.1, 100),
    "release_ms": _num(20, 2000),
    "makeup_db": _num(0, 12),
})

RACK_SETTING = _obj({"id": {"type": "string"}, "value": {"type": "number"}})


def manifest_schema(stem_names: list[str], rack_plugins: list[str] | None) -> dict:
    stem = _obj({
        "stem": {"type": "string", "enum": stem_names},
        "gain_db": _num(-24, 12),
        "delay_samples": _num(-19200, 19200, "positive = later; fixes the stem's measured offset"),
        "polarity_invert": {"type": "boolean"},
        "mono_below_hz": _num(0, 300, "0 = off"),
        "eq": {"type": "array", "items": EQ_BAND, "maxItems": 12},
        "dynamic_notches": {"type": "array", "items": DYN_NOTCH, "maxItems": 24},
        "deesser": DEESSER,
        "bleed_gate": BLEED_GATE,
        "sidechain_duck": SIDECHAIN,
        "rationale": {"type": "string", "maxLength": 400},
    })
    master = _obj({
        "eq": {"type": "array", "items": EQ_BAND, "maxItems": 8},
        "glue": GLUE,
        "mono_below_hz": _num(0, 300),
        "rationale": {"type": "string", "maxLength": 400},
    })
    props = {
        "target_lufs": _num(-24, -6),
        "true_peak_ceiling_dbtp": _num(-3, -1),
        "stems": {"type": "array", "items": stem},
        "master": master,
    }
    if rack_plugins:
        props["rack_chain"] = {"type": "array", "maxItems": 24, "items": _obj({
            "target": {"type": "string", "enum": stem_names + ["master"]},
            "plugin": {"type": "string", "enum": rack_plugins},
            "settings": {"type": "array", "items": RACK_SETTING, "maxItems": 64},
        })}
    return _obj(props)


def strip_numeric_limits(schema: dict) -> dict:
    """A copy without minimum/maximum/maxItems/maxLength (for API modes that refuse them).
    Limits are still checked by validate()."""
    s = copy.deepcopy(schema)

    def walk(node):
        if isinstance(node, dict):
            for k in ("minimum", "maximum", "maxItems", "maxLength"):
                node.pop(k, None)
            for v in node.values():
                walk(v)
        elif isinstance(node, list):
            for v in node:
                walk(v)
    walk(s)
    return s


def validate(manifest: dict, schema: dict, catalog: dict | None = None, all_stems: list[str] | None = None) -> list[str]:
    """Schema errors plus rack checks (plug-in exists, id exists, value in range, not stored memory)."""
    import jsonschema

    errors = [f"{'/'.join(map(str, e.absolute_path)) or '(root)'}: {e.message}"
              for e in jsonschema.Draft202012Validator(schema).iter_errors(manifest)]
    names = [s["stem"] for s in manifest.get("stems", []) if isinstance(s, dict) and "stem" in s]
    if len(set(names)) != len(names):
        errors.append("stems: a stem is listed twice")
    for s in manifest.get("stems", []):
        if not isinstance(s, dict):
            continue
        sc = s.get("sidechain_duck") or {}
        if sc.get("enabled"):
            if sc.get("trigger_stem") not in (all_stems or names):
                errors.append(f"stems/{s.get('stem')}: sidechain trigger_stem '{sc.get('trigger_stem')}' is not a stem")
            if sc.get("trigger_stem") == s.get("stem"):
                errors.append(f"stems/{s.get('stem')}: a stem cannot duck itself")
            if sc.get("band_high_hz", 0) <= sc.get("band_low_hz", 0):
                errors.append(f"stems/{s.get('stem')}: sidechain band_high_hz must be above band_low_hz")
        for b in s.get("eq", []):
            if b.get("start_s") is not None and b.get("end_s") is not None and b["end_s"] <= b["start_s"]:
                errors.append(f"stems/{s.get('stem')}/eq: end_s must be after start_s")
    if catalog is not None:
        for k, step in enumerate(manifest.get("rack_chain", []) or []):
            p = catalog["by_name"].get(step.get("plugin"))
            if p is None:
                errors.append(f"rack_chain/{k}: no plug-in '{step.get('plugin')}' in this build")
                continue
            params = {q["id"]: q for q in p["params"]}
            for st in step.get("settings", []):
                q = params.get(st.get("id"))
                if q is None:
                    errors.append(f"rack_chain/{k}: {p['name']} has no parameter '{st.get('id')}'")
                elif q.get("stored"):
                    errors.append(f"rack_chain/{k}: '{q['id']}' is stored memory, not a control")
                elif not (q["min"] <= st["value"] <= q["max"]):
                    errors.append(f"rack_chain/{k}: {q['id']}={st['value']} outside {q['min']}..{q['max']}")
    return errors
