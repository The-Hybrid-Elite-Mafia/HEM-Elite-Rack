# HEM-Elite-Rack 0.2.0

**Two new plug-ins** (in the rack and as VST3):
- **CONSOLE EQ** — a big-console channel EQ: HPF (18 dB/oct) and LPF with IN switches, LF and HF
  (shelf or BELL), LMF and HMF with Q, CLASSIC or MODERN curves (modern: small moves broad, big
  moves narrower), DRIVE for console colour. The display shows the music and the EQ curve.
- **BUS GLUE** — a mix-bus compressor with a gain-reduction needle: THRESHOLD, RATIO 2 / 4 / 10:1,
  ATTACK 0.1–30 ms, RELEASE 0.1–1.2 s or AUTO, MAKEUP, MIX (parallel), SIDECHAIN HPF so the low
  end doesn't pump the mix. Stereo-linked, zero latency.

**Fixed:** the chain's DOWN button showed "D...". The plug-in picker fits four plug-ins.

# HEM-Elite-Rack 0.1.1

**Fixed (looks only, the sound is unchanged):** labels that ran into each other on the rack
screen and on ELITE EQ — slot names under the UP / DOWN buttons, DYNAMIC over THRESHOLD, the
cut-off "12 dB" graph button, the crowded A / B and MATCH LOUDNESS buttons, the meter line.
Text that is still too long for its spot now ends in "..." instead of running over.

# HEM-Elite-Rack 0.1.0 — first release

Hybrid Elite Mafia's plug-in rack: load a song, stack HEM plug-ins, hear them live, compare
with the original at the same loudness, export a WAV. The plug-ins also come as VST3 for any DAW.

**Download:** `HEM-Elite-Rack-Setup-0.1.0.exe` below. Windows 10 / 11 64-bit, a processor with
AVX2 (Intel 2013+, AMD 2015+), a graphics card with OpenGL 3.3.

## What's in it
- **ELITE EQ** — 16 bands (bell, shelves, cuts 6–48 dB/oct, notch, tilt), each on stereo, mid,
  side, left or right; dynamic bands that only move when that part of the song pokes out;
  CLEAN / CONSOLE / TUBE character; AUTO GAIN. Draw on the graph: double-click adds a band,
  drag moves it, the wheel sets its width.
- **RESTORER** — learns the hiss / noise by itself and takes it out while keeping the air and
  the attacks. NOISE REDUCTION, FOCUS, AIR PRESERVE, FREQUENCY RANGE, ADAPT, LISTEN.
- **The rack app** — up to 8 plug-ins in a chain (ON / OFF, reorder, remove), A / B with
  MATCH LOUDNESS, loop, output meter with LUFS, export 24-bit / 48 kHz WAV, remembers your chain.
- **VST3** — HEM ELITE EQ and HEM RESTORER with the same faceplates (live spectrum in the DAW).

## Good to know
- Sound goes out through the sound card in Windows **exclusive mode** at 48 kHz (lowest
  latency): other programs go quiet while the rack plays. OUTPUT (top right) picks the card.
- No ASIO in this build (public, shareable).
- Settings stay in `%APPDATA%\HEM-Elite-Rack` across upgrades and uninstalls.
