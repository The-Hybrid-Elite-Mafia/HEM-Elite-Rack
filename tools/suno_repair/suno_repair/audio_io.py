"""Loading a Suno Studio stem folder: full rate, full stereo, no mono fold-down, no normalising."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from math import gcd
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

MIX_HINTS = ("mix", "master", "full song", "fullsong", "original", "reference", "stereo")

# Role keywords, checked in order (first hit wins). Used to pick kick/bass/vocal stems for the
# cross-stem checks; the names themselves are never changed.
ROLE_RULES = (
    ("backing_vocal", ("backing", "bgv", "bvox", "harmony", "choir", "adlib", "ad-lib")),
    ("vocal", ("vocal", "vox", "voice", "rap", "sing")),
    ("kick", ("kick", "bd")),
    ("snare", ("snare", "clap")),
    ("drums", ("drum", "perc", "beat", "hat", "cymbal", "loop")),
    ("bass", ("808", "bass", "sub")),
    ("guitar", ("guitar", "gtr", "banjo", "dobro", "mandolin")),
    ("keys", ("piano", "keys", "keyboard", "organ", "rhodes")),
    ("synth", ("synth", "pad", "lead synth", "arp")),
    ("strings", ("string", "violin", "fiddle", "cello", "orchestra")),
    ("brass", ("brass", "horn", "trumpet", "sax", "woodwind", "flute")),
    ("fx", ("fx", "sfx", "riser", "impact", "noise")),
)


def guess_role(name: str) -> str:
    n = name.lower()
    for role, keys in ROLE_RULES:
        for k in keys:
            if re.search(r"(^|[^a-z])" + re.escape(k), n):
                return role
    return "other"


@dataclass
class Track:
    name: str            # file stem, used as the id everywhere ("Vocals", "Drums" ...)
    path: Path
    audio: np.ndarray    # float64, shape (2, n): row 0 = left, row 1 = right
    role: str
    file_rate: int
    file_channels: int
    file_subtype: str
    notes: list[str] = field(default_factory=list)


@dataclass
class Session:
    rate: int
    stems: list[Track]
    mix: Track | None

    @property
    def length(self) -> int:
        return self.stems[0].audio.shape[1] if self.stems else 0

    def stem(self, name: str) -> Track:
        for s in self.stems:
            if s.name == name:
                return s
        raise KeyError(name)


def read_stereo(path: Path, target_rate: int | None = None) -> tuple[np.ndarray, int, dict]:
    info = sf.info(str(path))
    x, rate = sf.read(str(path), dtype="float64", always_2d=True)
    notes = []
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
        notes.append("mono file: copied to both sides")
    elif x.shape[1] > 2:
        notes.append(f"{x.shape[1]} channels: using 1+2")
        x = x[:, :2]
    if target_rate and rate != target_rate:
        g = gcd(int(rate), int(target_rate))
        x = resample_poly(x, target_rate // g, rate // g, axis=0)
        notes.append(f"resampled {rate} -> {target_rate} Hz")
        rate = target_rate
    meta = {"rate": int(info.samplerate), "channels": int(info.channels), "subtype": str(info.subtype), "notes": notes}
    return np.ascontiguousarray(x.T), int(rate), meta


def write_wav(path: Path, audio: np.ndarray, rate: int) -> None:
    """32-bit float WAV, never clipped or dithered (the verifier checks the real values)."""
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), np.asarray(audio, dtype=np.float32).T, rate, subtype="FLOAT")


def _is_mix(path: Path) -> bool:
    n = path.stem.lower()
    return any(h in n for h in MIX_HINTS)


def load_session(folder: Path, mix_path: Path | None = None) -> Session:
    folder = Path(folder)
    wavs = sorted(p for p in folder.iterdir() if p.suffix.lower() in (".wav", ".wave", ".flac", ".aif", ".aiff"))
    if mix_path is None:
        cands = [p for p in wavs if _is_mix(p)]
        mix_path = cands[0] if len(cands) == 1 else None
    else:
        mix_path = Path(mix_path)
    stem_paths = [p for p in wavs if mix_path is None or p.resolve() != mix_path.resolve()]
    if not stem_paths:
        raise SystemExit(f"no stem files found in {folder}")

    # The session rate is the mix's rate (ground truth), else the first stem's.
    first = mix_path if mix_path is not None else stem_paths[0]
    rate = int(sf.info(str(first)).samplerate)

    def load(p: Path) -> Track:
        a, _, meta = read_stereo(p, rate)
        return Track(p.stem, p, a, guess_role(p.stem), meta["rate"], meta["channels"], meta["subtype"], meta["notes"])

    stems = [load(p) for p in stem_paths]
    names = [s.name for s in stems]
    if len(set(names)) != len(names):
        raise SystemExit("two stems have the same file name (without extension); rename one")
    mix = load(mix_path) if mix_path is not None else None

    # Same length for everything (pad the short ones with silence at the end; never trim).
    n = max([s.audio.shape[1] for s in stems] + ([mix.audio.shape[1]] if mix else []))
    for t in stems + ([mix] if mix else []):
        if t.audio.shape[1] < n:
            t.notes.append(f"padded {n - t.audio.shape[1]} samples of silence at the end")
            t.audio = np.pad(t.audio, ((0, 0), (0, n - t.audio.shape[1])))
    return Session(rate, stems, mix)
