# HEM-Elite-Rack

Hybrid Elite Mafia's plug-in rack for Windows: load a song, stack HEM plug-ins, hear them live,
compare A / B at matched loudness and export a WAV. The same plug-ins ship as VST3.

Built on **The Hybrid Core** (`external/HybridCore`, a git submodule), Hybrid Elite Mafia's own
C++23 audio framework, as a library.

| Plug-in | What it does |
|---|---|
| **ELITE EQ** | 16-band mastering / mixing EQ: bell, shelves, cuts (6–48 dB/oct), notch, tilt; per band stereo / mid / side / left / right; dynamic bands; CLEAN / CONSOLE / TUBE character; auto gain |
| **RESTORER** | Adaptive hiss and noise reduction that keeps the air and the attacks |
| **CONSOLE EQ** | Big-console channel EQ: HPF / LPF, LF, LMF, HMF, HF; classic or modern curves; drive |
| **BUS GLUE** | Console-style mix-bus compressor: 2 / 4 / 10:1, stepped attack and release (+ AUTO), sidechain HPF, mix |
| **TAPE** | Two-track tape machine: input drive, 15 / 30 IPS, head bump, wow & flutter, hiss, auto trim, VU meters |
| **TUBE EQ** | Passive tube program EQ: low boost / atten (the low-end trick), high boost with bandwidth, high atten, tube drive |
| **VARI-MU** | Variable-mu tube compressor: ratio grows with level, six time constants, stereo or mid / side, tube |
| **OPTO** | Optical leveler: peak reduction and gain, compress / limit, two-stage release, sidechain emphasis |
| **FET** | Fast FET limiting amplifier: input / output, attack and release 1–7, ratios 4 / 8 / 12 / 20 / ALL |
| **LIMITER** | True-peak mastering limiter: input push, ceiling, look-ahead, auto release, stereo link, LUFS and true-peak readouts |
| **DE-ESSER** | Tames harsh S / T sounds: frequency, threshold, range; SPLIT (only the ess band) or WIDE; LISTEN |
| **STEREO WIDTH** | Mono to 200 % width, MONO BASS (80–250 Hz), balance; correlation and width readouts |
| **CLIPPER** | 4x oversampled clipper with a hard-to-soft SHAPE: shave peaks before the limiter |
| **EXCITER** | Adds fresh harmonics above a frequency: TUBE (even) or TAPE (odd), drive, mix |
| **TRANSIENT** | Transient shaper: more or less ATTACK and SUSTAIN, the same at any level |
| **LOUDNESS METER** | EBU R128 / BS.1770: momentary, short-term, integrated LUFS, loudness range, true peak, correlation, distance to a target |
| **BRIT PRE** | British console channel: 18 dB/oct high-pass, low shelf, mid bell, 12 kHz high shelf, preamp drive through iron |
| **AMERICAN EQ** | Punchy three-band EQ with proportional Q, PEAK or SHELF low / high, 50 Hz–15 kHz band-pass filter, drive |
| **MID EQ** | Tube midrange EQ: low peak, dip and high peak, tube drive |
| **AIR EQ** | Clean mastering EQ: broad bands at 10 / 40 / 160 / 650 Hz and 2.5 kHz, AIR shelf up to 20 kHz |
| **GRAPHIC EQ** | Ten bands, 31 Hz – 16 kHz, ±12 dB, constant Q |
| **TILT** | Darker or brighter with one knob around a pivot, low cut, high cut |
| **VCA COMP** | Clean modern compressor: ratio 1–20, knee, attack, release or AUTO, auto makeup, sidechain HPF, mix |
| **PUNCH 160** | Classic RMS compressor: threshold, compression up to 60:1, OVER EASY, program-dependent timing |
| **THRUST BUS** | Punchy bus compressor: THRUST (bass pumps less), knee, NEW (feed-forward) / OLD (feedback), link |
| **BRIT BUS** | British diode-bridge bus compressor + limiter: AUTO recovery, iron drive |
| **GATE** | Gate / expander: range, hold, hysteresis, detector HPF / LPF, listen |
| **MULTIBAND** | Three-band compressor: Linkwitz-Riley crossovers, threshold / ratio / gain per band |
| **TUBE SAT** | Tube stage: drive, bias (even harmonics), tone, mix — colour without a volume jump |
| **PREAMP** | Mic / line preamp: up to +30 dB into CLEAN, IRON or TUBE; low cut, phase invert |
| **CASSETTE** | Cassette deck: TYPE I / II / IV, drive, wow, flutter, hiss, age |
| **VINYL** | Record: crackle, surface noise and rumble, wear, warp, mono lows |
| **BIT CRUSHER** | Bits 1–24, downsample 1–50x, mix |
| **TRANSFORMER** | Output iron: the lows saturate most, weight, nickel or steel |
| **HALL** | Concert hall reverb: pre-delay, size, decay, damping, early reflections, modulation, width, low / high cut |
| **ROOM** | Room reverb, small to large, early reflections up front |
| **PLATE** | Bright, dense plate reverb with diffusion control |
| **SPRING** | Spring tank: decay, tension, drip, tone |
| **TAPE ECHO** | Tape delay: three heads, feedback to 110 % (saturates, never blows up), wobble, tone |
| **DIGITAL DELAY** | Clean stereo delay: note values at a BPM or ms, ping-pong, stereo offset, filtered repeats |

Our own designs. Faceplates: brushed gunmetal and teal, with the Hybrid Elite Mafia crown and the ghost.

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
