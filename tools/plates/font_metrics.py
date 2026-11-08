"""Writes tests/FontMetrics.hpp: the plate fonts' letter widths (Poppins SemiBold / Bold advance widths as a fraction
of the em, for ' ' .. '~') and cap heights, which tests/TextRoom.hpp uses to size the plate's words like the renderer.
    python3 tools/plates/font_metrics.py      (run from the repo root; needs fontTools)
"""
from fontTools.ttLib import TTFont

out = ["// Advance widths of the plate fonts, as a fraction of the em, for ' ' .. '~' (Poppins SemiBold / Bold, from",
       "// external/HybridCore/assets/fonts, made by tools/plates/font_metrics.py). Used by TextRoom.hpp to size the plate's",
       "// words the way the renderer does (stb_truetype advances, no kerning).",
       "#pragma once", "", "namespace hem::test::textroom {", ""]
for face in ["SemiBold", "Bold"]:
    f = TTFont(f"external/HybridCore/assets/fonts/Poppins-{face}.ttf")
    upm = f['head'].unitsPerEm
    cmap = f.getBestCmap()
    hm = f['hmtx']
    cap = getattr(f['OS/2'], 'sCapHeight', 0) / upm
    vals = [hm[cmap[c]][0] / upm if c in cmap else 0.6 for c in range(32, 127)]
    out.append(f"inline constexpr float k{face}Cap = {cap:.4f}f;")
    out.append(f"inline constexpr float k{face}Adv[95] = {{")
    for i in range(0, 95, 10):
        out.append("    " + ", ".join(f"{v:.4f}f" for v in vals[i:i + 10]) + ",")
    out.append("};")
out += ["", "} // namespace hem::test::textroom", ""]
open("tests/FontMetrics.hpp", "w").write("\n".join(out))
