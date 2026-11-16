"""python -m suno_repair <stem folder> --out <folder> [options]

  analyze -> telemetry_full.json, telemetry_compact.json
  plan    -> manifest.json (+ plan_log.json when Claude planned it, rack_chains/*.hemchain)
  render  -> stems/<name>_clean.wav, stems/<name>_removed.wav, master.wav, delta_original_minus_processed.wav
  verify  -> verification.json, verification.md   (exit code 5 when a check fails)
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from . import forensics, rack, render, rules, verify
from .audio_io import load_session
from .schema import manifest_schema, validate


def _dump(obj, path: Path) -> None:
    path.write_text(json.dumps(obj, indent=2, default=lambda o: o.item() if hasattr(o, "item") else str(o)), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="suno_repair", description="Zero-touch Suno stem forensics, planning, repair and verification")
    ap.add_argument("stems", type=Path, help="folder with the Suno stem WAVs (and the original mix, if its name has 'mix' or 'master')")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--mix", type=Path, help="the original stereo mix (ground truth); found by name if not given")
    ap.add_argument("--target", type=float, default=-14.0, help="integrated LUFS target (-14 streaming, -11 heavy country-trap)")
    ap.add_argument("--ceiling", type=float, default=-1.0, help="true-peak ceiling dBTP")
    ap.add_argument("--planner", choices=("claude", "rules"), default="claude")
    ap.add_argument("--model", default=None, help="Claude model id (default: HEM_REPAIR_MODEL or claude-sonnet-5-5)")
    ap.add_argument("--manifest", type=Path, help="render an existing manifest.json instead of planning")
    ap.add_argument("--stop-after", choices=("analyze", "plan"))
    ap.add_argument("--rack-render", type=Path, help="hem_rack_render.exe from the HEM-Elite-Rack build")
    ap.add_argument("--rack-catalog", type=Path, help="catalog.json from 'hem_rack_render --list' (made automatically if --rack-render is given)")
    ap.add_argument("--rack-plugins", default=None, help="comma list of rack plug-in names to offer Claude")
    ap.add_argument("--tempo", type=float, default=None, help="song BPM for tempo-synced rack plug-ins")
    ap.add_argument("--save-stft", action="store_true", help="also save the raw complex STFT (L, R, M, S) per stem as .npz (large)")
    a = ap.parse_args(argv)

    out = a.out
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()

    def say(msg):
        print(f"[{time.time() - t0:7.1f}s] {msg}", flush=True)

    session = load_session(a.stems, a.mix)
    names = [s.name for s in session.stems]
    say(f"loaded {len(names)} stems at {session.rate} Hz, {session.length / session.rate:.1f} s"
        f"{', reference mix: ' + session.mix.name if session.mix else ', no reference mix'}")

    full, _ = forensics.analyze(session, out / "stft" if a.save_stft else None)
    tel = forensics.compact(full)
    _dump(full, out / "telemetry_full.json")
    _dump(tel, out / "telemetry_compact.json")
    say("telemetry written")
    if a.stop_after == "analyze":
        return 0

    catalog, offer, missing = None, None, []
    if a.rack_catalog:
        catalog = rack.load_catalog(a.rack_catalog)
    elif a.rack_render:
        catalog = rack.generate_catalog(a.rack_render, out / "rack_catalog.json")
    if catalog is not None:
        offer, missing = rack.offer(catalog, a.rack_plugins.split(",") if a.rack_plugins else None)
        if missing:
            say(f"rack plug-ins not in this build (skipped): {', '.join(missing)}")

    schema = manifest_schema(names, [p["name"] for p in offer] if offer else None)
    if a.manifest:
        manifest = json.loads(a.manifest.read_text(encoding="utf-8"))
        say(f"manifest from {a.manifest}")
    elif a.planner == "rules":
        manifest = rules.plan(tel, a.target, a.ceiling)
        say("manifest from the built-in rules")
    else:
        from . import brain
        manifest, _ = brain.plan(tel, names, a.target, a.ceiling, offer, catalog, model=a.model or brain.DEFAULT_MODEL,
                                 log_path=out / "plan_log.json")
        say("manifest from Claude")
    manifest = rules.fill_missing(manifest, names)
    if not offer:
        manifest.pop("rack_chain", None)
    errors = validate(manifest, schema, catalog, names)
    if errors:
        _dump({"errors": errors, "manifest": manifest}, out / "manifest_rejected.json")
        print("manifest rejected:\n  " + "\n  ".join(errors[:30]), file=sys.stderr)
        return 2
    manifest["target_lufs"], manifest["true_peak_ceiling_dbtp"] = a.target, a.ceiling   # the request decides these
    _dump(manifest, out / "manifest.json")
    if manifest.get("rack_chain"):
        targets = sorted({st["target"] for st in manifest["rack_chain"]})
        for t in targets:
            rack.write_chain([st for st in manifest["rack_chain"] if st["target"] == t], out / "rack_chains" / f"{t}.hemchain")
    if a.stop_after == "plan":
        return 0

    result = render.render(session, manifest, out, a.rack_render, a.tempo)
    _dump(result["log"], out / "render_log.json")
    say("rendered stems, master and delta")

    ref = session.mix.audio if session.mix is not None else None
    res = verify.verify(session.rate, result["aligned"], result["clean"], result["master"], a.target, a.ceiling, ref, out)
    say("verification " + ("ALL PASS" if res["all_pass"] else "FAILED") + f" -> {out / 'verification.md'}")
    for c in res["checks"]:
        v = c["value"] if not isinstance(c["value"], dict) else {k: w for k, w in c["value"].items() if not isinstance(w, dict)}
        print(f"  {'PASS' if c['pass'] else 'FAIL'}  {c['check']}: {v}  (rule {c['rule']})")
    return 0 if res["all_pass"] else 5
