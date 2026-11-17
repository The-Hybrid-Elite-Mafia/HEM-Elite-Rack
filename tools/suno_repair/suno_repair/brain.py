"""Claude as the deterministic planner: telemetry in, repair manifest out, through one forced tool call.

- tool_choice forces the `submit_repair_manifest` tool, so the answer is JSON, never prose.
- The answer is checked against the schema (and the rack catalog) locally; if it fails, the
  errors go back to Claude as a tool_result with is_error, up to `retries` times.
- temperature 0 for repeatable plans.
- If the API refuses `strict` tools or `temperature` for the chosen model, the call is repeated
  without them (logged in the plan log) — local validation still applies.
"""
from __future__ import annotations

import json
import os
import time

from .schema import manifest_schema, strip_numeric_limits, validate

DEFAULT_MODEL = os.environ.get("HEM_REPAIR_MODEL", "claude-sonnet-4-5")   # project prompt: claude-sonnet-4-5 or claude-opus-4-1
TOOL_NAME = "submit_repair_manifest"

SYSTEM = """You are the planning stage of an automatic stem-repair pipeline for AI-generated (Suno) music.
You receive measured telemetry and must answer ONLY by calling submit_repair_manifest.

Rules:
1. Fix measured problems only. A stem with no problem gets neutral settings: gain 0, delay 0, no EQ, everything disabled.
2. Alignment: if shift.offset_samples is not null and |offset| >= 0.5 with match_strength_psr >= 6, set delay_samples = -offset_samples. If polarity_inverted is true, set polarity_invert true.
3. Sub phase: when a stem's sub_phase_problem is true (or sub_lt150_corr < 0.85), set mono_below_hz between 100 and 150.
4. Resonances: for resonances_2k_8k with kind "stationary_artifact": if present_pct_of_song >= 90, add an EQ band type "notch" at that hz, q = clamp(q_estimate, 8, 40), whole song; otherwise add a dynamic notch at that hz, q = max(q_estimate, 4), threshold_db about 3, max_cut_db = min(prominence_db + 3, 18). Musical/intermittent peaks: leave alone unless prominence_db > 12.
5. Hot spots are time-coded: fix them with EQ bands limited to start_s/end_s (mud 200-500 Hz cut 1.5-4 dB, harsh 2-5 kHz cut 1.5-3 dB). Use the whole-song form (null times) only when the problem covers most of the song.
6. Sibilance hot spots on vocal stems: enable the de-esser (freq 5500-8000 Hz, threshold about -12, max_cut 4-8).
7. Bleed: a bleed_db value (row leaks into column) above -35 means audible leakage in the column stem's gaps: enable that stem's bleed_gate with threshold_dbfs = gate_threshold_hint_dbfs and reduction 10-20 dB. Never gate a stem with active_pct above 85.
8. Kick/bass: if overlap > 0.35, duck the bass stem from the kick/drums stem in a band around most_collision_hz (depth 2-5 dB, attack 2-10 ms, release 60-150 ms).
9. Boosts: never more than +3 dB on any EQ band. Keep stem gains within +/-3 dB unless the telemetry shows a stem far out of balance.
10. Master: target_lufs and true_peak_ceiling_dbtp come from the request; do not change them. Use glue only if the stem sum LRA is above 10 LU (ratio <= 2, threshold so it works about 2 dB). Master mono_below_hz 120 if the sum's sub_lt150_corr < 0.85.
11. HF cutoffs (brickwall) cannot be restored by EQ: do not boost above a cutoff.
12. rack_chain (only if offered): use only the listed plug-ins and control ids, values in their min..max (choices by index). Leave rack_chain empty unless a listed plug-in clearly does the job better than the built-in stages.
13. rationale: one short plain sentence per stem naming the measurement it answers."""


def _client():
    try:
        import anthropic
    except ImportError as e:
        raise SystemExit("the anthropic package is not installed (pip install -r requirements.txt)") from e
    if not os.environ.get("ANTHROPIC_API_KEY"):
        raise SystemExit("ANTHROPIC_API_KEY is not set (or use --planner rules)")
    return anthropic


def plan(telemetry: dict, stem_names: list[str], target_lufs: float, ceiling_dbtp: float,
         rack_offer: list[dict] | None = None, catalog: dict | None = None, model: str = DEFAULT_MODEL,
         retries: int = 2, max_tokens: int = 12000, log_path=None) -> tuple[dict, dict]:
    anthropic = _client()
    client = anthropic.Anthropic()
    rack_names = [p["name"] for p in rack_offer] if rack_offer else None
    schema = manifest_schema(stem_names, rack_names)
    log = {"model": model, "attempts": [], "dropped_options": []}

    payload = {"target_lufs": target_lufs, "true_peak_ceiling_dbtp": ceiling_dbtp, "stems": stem_names,
               "telemetry": telemetry}
    if rack_offer:
        payload["rack_plugins_offered"] = rack_offer
    messages = [{"role": "user", "content": json.dumps(payload, separators=(",", ":"))}]

    opts = {"strict": True, "temperature": True, "limits": True}

    def tool_def():
        t = {"name": TOOL_NAME,
             "description": "Submit the complete repair manifest for this session. Call exactly once.",
             "input_schema": schema if opts["limits"] else strip_numeric_limits(schema)}
        if opts["strict"]:
            t["strict"] = True
        return t

    def call():
        while True:
            kw = dict(model=model, max_tokens=max_tokens, system=SYSTEM, tools=[tool_def()],
                      tool_choice={"type": "tool", "name": TOOL_NAME}, messages=messages)
            if opts["temperature"]:
                kw["temperature"] = 0
            try:
                return client.messages.create(**kw)
            except anthropic.BadRequestError as e:
                msg = str(e).lower()
                for key, words in (("strict", ("strict",)), ("temperature", ("temperature",)),
                                   ("limits", ("minimum", "maximum", "maxitems", "maxlength", "schema"))):
                    if opts[key] and any(w in msg for w in words):
                        opts[key] = False
                        log["dropped_options"].append({"option": key, "api_error": str(e)[:300]})
                        break
                else:
                    raise

    for attempt in range(retries + 1):
        t0 = time.time()
        resp = call()
        block = next((b for b in resp.content if getattr(b, "type", None) == "tool_use"), None)
        entry = {"attempt": attempt + 1, "seconds": round(time.time() - t0, 1), "stop_reason": resp.stop_reason,
                 "usage": {"input_tokens": resp.usage.input_tokens, "output_tokens": resp.usage.output_tokens}}
        if block is None:
            entry["error"] = "no tool_use block in the answer"
            log["attempts"].append(entry)
            messages.append({"role": "assistant", "content": resp.content})
            messages.append({"role": "user", "content": f"Answer only by calling {TOOL_NAME}."})
            continue
        manifest = block.input
        errors = validate(manifest, schema, catalog, stem_names)
        entry["errors"] = errors
        log["attempts"].append(entry)
        if not errors:
            log["ok"] = True
            _save(log, log_path)
            return manifest, log
        messages.append({"role": "assistant", "content": resp.content})
        messages.append({"role": "user", "content": [{"type": "tool_result", "tool_use_id": block.id, "is_error": True,
                                                      "content": "Rejected. Fix these and call the tool again:\n" + "\n".join(errors[:40])}]})
    log["ok"] = False
    _save(log, log_path)
    raise SystemExit("Claude's manifest still failed validation after retries; see plan_log.json\n" +
                     json.dumps(log, indent=1)[:4000])


def _save(log: dict, path) -> None:
    if path is not None:
        from pathlib import Path
        Path(path).write_text(json.dumps(log, indent=2), encoding="utf-8")
