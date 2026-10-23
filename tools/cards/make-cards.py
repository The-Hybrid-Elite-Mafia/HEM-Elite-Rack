#!/usr/bin/env python3
"""Makes the plug-in picker's faceplate cards (assets/cards/HEM_<NAME>.jpg, 384 x 216) from the finished
faceplate renders of the "Promo screenshots" workflow (HEM-Promo-Screenshots.zip, unpacked).
    python3 tools/cards/make-cards.py <unpacked HEM-Promo-Screenshots folder> assets/cards
Run it again whenever a faceplate changes, so the cards show the plates as they are."""
import glob, os, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
n = 0
for f in sorted(glob.glob(os.path.join(src, "**", "HEM_*.png"), recursive=True)):
    im = Image.open(f).convert("RGB").resize((384, 216), Image.LANCZOS)
    im.save(os.path.join(out, os.path.basename(f)[:-4] + ".jpg"), quality=90, optimize=True)
    n += 1
print(f"{n} cards written to {out}")
