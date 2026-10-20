# Retired plug-ins (kept safe, not built)

**Back in the rack since 0.32.0 (Oct 20, 2026):** BOARDROOM, FOUNDRY, MIDTOWN, BACKROOM, COIL, MIXTAPE, CRATE DIGGER,
SHREDDER (same IDs). The copies below are kept as they were.

These HEM-Elite-Rack plug-ins were taken out of the rack on Oct 19, 2026, so the collection stays
tight. Nothing here is compiled or installed; the files are exact copies of the working, tested
code from the version named in each folder. To bring one back, copy its class, its faceplate
layout and its tests back into the live code, register it in `rackfx/src/Plugins.cpp`, and run CI.

The plug-in names and permanent VST3 IDs are in `rackfx/src/Plugins.cpp` of that version
(`git show v0.13.0:rackfx/src/Plugins.cpp`); reuse the same IDs when bringing one back.

## from-0.13.0/ (removed in 0.14.0)

| Name | What it is | Code | Faceplate (FaceLayouts.cpp) |
|---|---|---|---|
| SMOKE | variable-mu tube compressor | `rackfx/src/VariMu.cpp`, `Dynamics.hpp` | `variMu` |
| LANTERN | optical leveler | `rackfx/src/Opto.cpp`, `Dynamics.hpp` | `optoFace` |
| TRIGGER | fast FET limiting amplifier | `rackfx/src/Fet.cpp`, `Dynamics.hpp` | `fetFace` |
| HUSH MONEY | de-esser | `rackfx/src/Tools.cpp` (namespace `des`) | `deEsserFace` |
| RAZOR | 4x oversampled clipper | `rackfx/src/Tools.cpp` (`clp`) | `clipperFace` |
| SUCKER PUNCH | transient shaper | `rackfx/src/Tools.cpp` (`trn`) | `transientFace` |
| HEAVY HAND | RMS compressor | `rackfx/src/MoreDynamics.cpp` (`pu`) | `punchFace` |
| MUSCLE | bus compressor with low focus | `rackfx/src/MoreDynamics.cpp` (`th`) | `focusFace` |
| UNDERBOSS | diode-bridge bus compressor + limiter | `rackfx/src/MoreDynamics.cpp` (`bb`) | `diodeBusFace` |
| BOUNCER | gate / expander | `rackfx/src/MoreDynamics.cpp` (`ga`) | `gateFace` |
| TRIAD | three-band compressor | `rackfx/src/MoreDynamics.cpp` (`mb`) | `multibandFace` |
| CHOKEHOLD | low-end tightener | `rackfx/src/Specialty.cpp` (`bt`) | `tightenerFace` |
| FRONT MAN | mic / line preamp | `rackfx/src/ColorFx.cpp` (`pa`) | `preampFace` |
| MIXTAPE | cassette deck | `rackfx/src/ColorFx.cpp` (`cs`) | `cassetteFace` |
| CRATE DIGGER | vinyl | `rackfx/src/ColorFx.cpp` (`vn`) | `vinylFace` |
| SHREDDER | bit crusher | `rackfx/src/ColorFx.cpp` (`bc`) | `crusherFace` |
| ANVIL | output transformer | `rackfx/src/ColorFx.cpp` (`tf`) | `transformerFace` |
| BACKROOM | room reverb | `rackfx/src/Space.cpp` (`rv`, Kind::Room) | `reverbFace` |
| COIL | spring reverb | `rackfx/src/Space.cpp` (`sp`) | `springFace` |
| WIRETAP | tape echo | `rackfx/src/Space.cpp` (`te`) | `tapeEchoFace` |

Tests for all of them: `from-0.13.0/tests/rackfx_tests.cpp`. Snapshot presets: `from-0.13.0/tools/rack_snapshot/main.cpp`.

## from-0.12.0/ (removed in 0.13.0)

| Name | What it is | Code | Faceplate |
|---|---|---|---|
| BOARDROOM | big-console channel EQ | `rackfx/src/ConsoleEq.cpp` | `consoleEq` |
| FOUNDRY | console channel preamp EQ | `rackfx/src/ClassicEqs.cpp` | `ironPre` |
| KNUCKLES | punchy three-band EQ | `rackfx/src/ClassicEqs.cpp` | `punchEq` |
| MIDTOWN | tube midrange EQ | `rackfx/src/ClassicEqs.cpp` | `midEq` |
| PENTHOUSE | clean mastering EQ with air | `rackfx/src/ClassicEqs.cpp` | `airEq` |
| BLUEPRINT | ten-band graphic EQ | `rackfx/src/ClassicEqs.cpp` | `graphicEq` |
| LEVERAGE | tilt EQ | `rackfx/src/ClassicEqs.cpp` | `tiltFace` |

The curve display these EQs used is in `from-0.12.0/rackui/src/FaceView.cpp` (`consoleCurve`).
