# HEM-Elite-Rack

Hybrid Elite Mafia's plug-in rack for Windows: load a song, stack HEM plug-ins, hear them live,
compare A / B at matched loudness and export a WAV. The same plug-ins ship as VST3.

Built on **The Hybrid Core** (`external/HybridCore`, a git submodule), Hybrid Elite Mafia's own
C++23 audio framework, as a library.

| Plug-in | What it does |
|---|---|
| **SURGEON** | 16-band mastering / mixing EQ: bell, shelves, cuts (6–48 dB/oct), notch, tilt; per band stereo / mid / side / left / right; dynamic bands; CLEAN / CONSOLE / TUBE character; auto gain |
| **THE CLEANER** | Adaptive hiss and noise reduction that keeps the air and the attacks |
| **THE FAMILY** | Console-style mix-bus compressor: 2 / 4 / 10:1, stepped attack and release (+ AUTO), sidechain HPF, mix |
| **ARCHIVE** | Two-track tape machine: input drive, 15 / 30 IPS, head bump, wow & flutter, hiss, auto trim, VU meters |
| **VELVET** | Passive tube program EQ: low boost / atten (the low-end trick), high boost with bandwidth, high atten, tube drive |
| **LOCKDOWN** | True-peak mastering limiter: input push, ceiling, look-ahead, auto release, stereo link, LUFS and true-peak readouts |
| **WIDE LOAD** | Mono to 200 % width, MONO BASS (80–250 Hz), balance; correlation and width readouts |
| **SPARK** | Adds fresh harmonics above a frequency: TUBE (even) or TAPE (odd), drive, mix |
| **THE ACCOUNTANT** | EBU R128 / BS.1770: momentary, short-term, integrated LUFS, loudness range, true peak, correlation, distance to a target |
| **ENFORCER** | Clean modern compressor: ratio 1–20, knee, attack, release or AUTO, auto makeup, sidechain HPF, mix |
| **HITMAN** | Dual FET peak compressor: L/R or M/S, attack / release 1–7, ratios 4 / 8 / 12 / 20 / ALL, lookahead, sidechain EQ, external sidechain (VST3), saturation (clip, warm, triode, tape), mix, gain-reduction history |
| **FURNACE** | Tube stage: drive, bias (even harmonics), tone, mix — colour without a volume jump |
| **CATHEDRAL** | Concert hall reverb: pre-delay, size, decay, damping, early reflections, modulation, width, low / high cut |
| **CHROME** | Bright, dense plate reverb with diffusion control |
| **REPEAT OFFENDER** | Clean stereo delay: note values at a BPM or ms, ping-pong, stereo offset, filtered repeats |
| **THE CREW** | Stereo chorus: one to three voices, rate, depth, delay, width |
| **GETAWAY** | Jet flanger: manual, depth, positive or negative feedback |
| **SMOKESCREEN** | 4 / 6 / 8 / 12-stage phaser: center, depth, feedback |
| **SHAKEDOWN** | Tremolo or auto pan: sine, triangle, soft square |
| **CYCLONE** | Rotating speaker: slow / fast with real spin-up, drive, horn / drum balance, mic spread |
| **SHAPESHIFTER** | Resonant low / band / high-pass following the music (envelope) or an LFO |
| **UNDERGROUND** | More low end: a new sub an octave down, or harmonics small speakers can hear |
| **SPOTLIGHT** | Vocal chain: high-pass, body, one-knob compressor, one-knob de-esser, presence, air |

Twenty-three plug-ins, our own designs. Faceplates: brushed gunmetal and teal, with the Hybrid Elite Mafia crown and the ghost.

## Install
Download `HEM-Elite-Rack-Setup-x.y.z.exe` from **Releases** and run it. Needs Windows 10 / 11
64-bit, a processor with **AVX2** (Intel 2013 or newer, AMD 2015 or newer) and OpenGL 3.3.
The installer adds a desktop shortcut and (optionally) the VST3 plug-ins to `Common Files\VST3`.

## Build (maintainers)
Windows, Visual Studio 2022, CMake 3.28+:
```
git clone --recurse-submodules https://github.com/The-Hybrid-Elite-Mafia/HEM-Elite-Rack.git
cmake --preset rack
cmake --build --preset rack-release
ctest --test-dir build/rack -C Release
```
CI (`.github/workflows/windows-build.yml`, started by hand from the Actions tab) builds, runs the tests and Steinberg's VST3
validator, renders screenshots with software OpenGL, builds and verifies the installer and
publishes the release.

## Licences
No Steinberg ASIO code (WASAPI only). VST3 SDK 3.8.1: MIT (fetched at build time, not committed).
Poppins font: SIL OFL 1.1. stb, dr_libs: public domain / MIT. Everything else © Hybrid Elite Mafia.
