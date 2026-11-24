"""Writes a synthetic 'Suno' session with KNOWN planted problems, so the pipeline can be checked
against answers we already know:
  - Vocals: a steady 4200 Hz whistle (AI artifact), sibilance bursts, drum bleed in its gaps
  - Bass: sub stereo phase problem (right side partly inverted below 150 Hz), collides with the kick
  - Guitar: 37.3 samples late versus the mix
  - Mix.wav: the clean-timed sum (ground truth)
python make_test_stems.py <out folder> [--seconds 60]
"""
import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

SR = 48000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--seconds", type=float, default=60.0)
    a = ap.parse_args()
    n = int(a.seconds * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(7)
    beat = 60.0 / 92.0                          # 92 BPM

    # Drums: kick on every beat, snare on 2 and 4, hats on eighths
    ph = np.mod(t, beat)
    kick = 0.6 * np.exp(-ph * 14) * np.sin(2 * np.pi * (48 + 90 * np.exp(-ph * 30)) * ph)
    ph2 = np.mod(t - beat, 2 * beat)
    snare = 0.25 * np.exp(-ph2 * 22) * rng.standard_normal(n)
    ph8 = np.mod(t, beat / 2)
    hats = 0.05 * np.exp(-ph8 * 80) * sosfilt(butter(2, 7000, "highpass", fs=SR, output="sos"), rng.standard_normal(n))
    drums = kick + snare + hats
    drums = np.stack([drums, drums * 0.98])

    # Bass / 808: long notes around 50-60 Hz, right channel's sub inverted (phase problem)
    notes = np.array([55.0, 49.0, 58.27, 43.65])
    f = notes[(t // (4 * beat)).astype(int) % 4]
    bass_m = 0.35 * np.sin(2 * np.pi * np.cumsum(f) / SR) * (0.6 + 0.4 * np.exp(-np.mod(t, beat) * 3))
    low = butter(4, 150, "lowpass", fs=SR, output="sos")
    sub = sosfilt(low, bass_m)
    bass = np.stack([bass_m, bass_m - 1.6 * sub])

    # Vocal: phrases (on 2 bars, off 1 bar), formant-ish tones, sibilance bursts, steady whistle
    phrase = (np.mod(t, 12 * beat) < 8 * beat).astype(float)
    env = sosfilt(butter(1, 8, "lowpass", fs=SR, output="sos"), phrase)
    f0 = 220 + 30 * np.sin(2 * np.pi * 0.3 * t)
    pitch = np.cumsum(f0) / SR
    voice = sum(0.12 / k * np.sin(2 * np.pi * k * pitch) for k in range(1, 9))
    ess_on = (np.mod(t, beat) > 0.75 * beat) & (np.mod(t, beat) < 0.85 * beat)
    ess = 0.10 * ess_on * sosfilt(butter(4, [5000, 9000], "bandpass", fs=SR, output="sos"), rng.standard_normal(n))
    whistle = 0.006 * np.sin(2 * np.pi * 4200 * t)
    vocal_m = env * (voice + ess) + whistle
    bleed = 0.03 * drums[0] * (1 - env)          # drums heard in the vocal gaps
    vocal = np.stack([vocal_m + bleed, vocal_m + bleed])

    # Guitar: chord stabs, wide stereo
    gph = np.mod(t, 2 * beat)
    gtr = 0.08 * np.exp(-gph * 4) * sum(np.sin(2 * np.pi * fr * t) for fr in (196.0, 246.9, 293.7))
    guitar = np.stack([gtr, 0.7 * gtr + 0.03 * np.sin(2 * np.pi * 330 * t) * np.exp(-gph * 4)])

    stems = {"Drums": drums, "Bass": bass, "Vocals": vocal, "Guitar": guitar}
    mix = sum(stems.values())
    # Guitar stem exported late by 37.3 samples (fractional: shift + linear interpolation)
    d = 37.3
    late = np.zeros_like(guitar)
    i = int(d)
    fr = d - i
    late[:, i + 1:] = (1 - fr) * guitar[:, 1:n - i] + fr * guitar[:, :n - i - 1]
    stems["Guitar"] = late

    a.out.mkdir(parents=True, exist_ok=True)
    for name, x in stems.items():
        sf.write(str(a.out / f"{name}.wav"), x.T.astype(np.float32), SR, subtype="FLOAT")
    sf.write(str(a.out / "Mix.wav"), mix.T.astype(np.float32), SR, subtype="FLOAT")
    print(f"wrote {len(stems)} stems + Mix.wav ({a.seconds:.0f} s) to {a.out}")


if __name__ == "__main__":
    main()
