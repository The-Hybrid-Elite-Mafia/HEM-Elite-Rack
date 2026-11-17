# HEM Suno Repair

Takes a folder of Suno Studio stems (plus the original stereo mix), measures what is wrong with them
second by second, has Claude write a repair plan as strict JSON, runs the repairs, and checks the
result with numbers. Nobody touches a knob.

```
python -m suno_repair "C:\Songs\MySong" --out "C:\Songs\MySong\repaired" --target -14
```
(run from `tools/suno_repair`, after `pip install -r requirements.txt`; set `ANTHROPIC_API_KEY`.)

## What it writes

| File | What it is |
|---|---|
| `telemetry_full.json` | every measurement |
| `telemetry_compact.json` | the shorter version sent to Claude |
| `manifest.json` | the repair plan (what Claude chose, checked against the schema) |
| `plan_log.json` | Claude call log: model, tokens, rejected attempts |
| `stems/<name>_clean.wav` | repaired stem, 32-bit float |
| `stems/<name>_removed.wav` | exactly what the repairs took out of that stem (listen to it) |
| `master.wav` | the summed, loudness-matched master, 32-bit float |
| `delta_original_minus_processed.wav` | original mix (level-matched, lined up) minus the master |
| `verification.json` / `.md` | pass/fail checks measured from the rendered files |

Exit code 0 = all checks pass, 5 = a check failed (the files are still written), 2 = plan rejected.

## Stages

1. **Load** — every file at its own rate in full stereo (no mono fold-down). The session rate is the
   mix's; other files are resampled to it (noted in the telemetry). Short files are padded, never trimmed.
2. **Measure** (`forensics.py`, `loudness.py`)
   - BS.1770-4: integrated (pyloudnorm, cross-checked by our own gating), momentary 400 ms,
     short-term 3 s (timeline every 3 s), LRA (EBU Tech 3342), 4x oversampled true peak.
   - ShiftMap: GCC-PHAT of each stem against the mix in 8 s windows → sub-sample offset, drift
     (ms/min from the offset trend), polarity. Onset-envelope DTW per 20 s as a second drift estimate.
   - Stereo per band (<150, 150–500, 500–4k, >4k): L/R correlation, worst 1 s, M/S ratio, mono fold-down loss.
   - Coherence + cross-spectrum phase between every stem pair below 500 Hz → where two stems cancel.
   - Bleed matrix: energy in each stem's quiet gaps that follows another stem's envelope (dB, row leaks into column).
   - Kick/bass collision 40–350 Hz (overlap 0..1, worst sections, the frequency where it piles up),
     and 200–500 Hz overlap for every stem pair.
   - 2–8 kHz resonances: peak Hz, prominence, Q estimate, spectral kurtosis (a steady tone reads near −1,
     so steady AI whistles are told apart from moving musical notes), and when in the song it is there.
   - High-frequency brickwall (10–21 kHz).
   - Hot spots: 3 s sections where mud / harshness / sibilance / sub jump above that stem's own normal.
3. **Plan** (`brain.py`) — one forced tool call (`submit_repair_manifest`); the schema forbids extra
   keys and lists every key as required. The answer is validated locally (schema + ranges + rack
   catalog) and sent back with the errors up to 2 times. `--planner rules` makes the same kind of
   manifest with fixed rules and no API call.
4. **Render** (`render.py`) — per stem: polarity, fractional delay, gain, mono below, RBJ biquad EQ
   (bands can be limited to a time range, with 40 ms fades), dynamic notches (cut only while the narrow
   band stands out from its neighbourhood), de-esser, bleed gate, sidechain duck. Master: sum, EQ, glue,
   mono below, then loudness to target through a look-ahead true-peak limiter (re-measured until within 0.05 LU).
5. **Verify** (`verify.py`) — measured again from the output:
   integrated within ±0.2 LU of target · true peak ≤ ceiling · 0 inter-sample overs ·
   sub <150 Hz correlation ≥ +0.85 · mono fold-down loss < 1.5 dB · no NaN/Inf · DC < 0.0001 ·
   no samples over 0 dBFS · 200–500 Hz cross-stem overlap lower than before ·
   fewer 3–6 kHz peaks over 6 dB prominence than before.

## The real rack plug-ins (`tools/rack_render`)

`hem_rack_render.exe` is built with the rest of HEM-Elite-Rack. It runs any rack plug-in offline:

```
hem_rack_render --list --out rack_catalog.json
hem_rack_render --chain master.hemchain --in in.wav --out out.wav --report report.json
```

Pass `--rack-render path\to\hem_rack_render.exe` to the pipeline: it makes the catalog from the build
itself, offers Claude a short list of plug-ins (default SURGEON, THE CLEANER, SILENCER, OMERTA,
KNOCKOUT, THE FAMILY, LOCKDOWN; change with `--rack-plugins`) with their real control ids and ranges,
rejects any id or value the build does not have, and runs the chosen chain on that stem or the master.
The true-peak limiter in step 4 always runs last.

## Known limits

- Everything above is untested until it is run on real stems (see the PR).
- Bleed and collision checks find stems by file name (kick/drums, bass/808, vocal ...).
- The delta file includes the limiter's work, not only the repairs; the per-stem `_removed.wav` files show the repairs alone.
- The C++ test suite (`tests/`) tests the plug-ins themselves; the checks on the rendered files are in `verify.py` and in `hem_rack_render`'s report.
