# HEM-Elite-Rack

Hybrid Elite Mafia's plug-in rack for Windows: load a song, stack HEM plug-ins, hear them live,
compare A / B at matched loudness and export a WAV. The same plug-ins ship as VST3.

Built on **The Hybrid Core** (`external/HybridCore`, a git submodule), Hybrid Elite Mafia's own
C++23 audio framework, as a library.

| Plug-in | What it does |
|---|---|
| **SURGEON** | 16-band mastering / mixing EQ: bell, shelves, cuts (6–48 dB/oct), notch, tilt; per band stereo / mid / side / left / right; dynamic bands; CLEAN / CONSOLE / TUBE character; auto gain |
| **THE CLEANER** | Adaptive hiss and noise reduction that keeps the air and the attacks |
| **BOARDROOM** | Big-console channel EQ: HPF / LPF, LF, LMF, HMF, HF; classic or modern curves; drive |
| **THE FAMILY** | Console-style mix-bus compressor: 2 / 4 / 10:1, stepped attack and release (+ AUTO), sidechain HPF, mix |
| **ARCHIVE** | Two-track tape machine: input drive, 15 / 30 IPS, head bump, wow & flutter, hiss, auto trim, VU meters |
| **VELVET** | Passive tube program EQ: low boost / atten (the low-end trick), high boost with bandwidth, high atten, tube drive |
| **SMOKE** | Variable-mu tube compressor: ratio grows with level, six time constants, stereo or mid / side, tube |
| **LANTERN** | Optical leveler: peak reduction and gain, compress / limit, two-stage release, sidechain emphasis |
| **TRIGGER** | Fast FET limiting amplifier: input / output, attack and release 1–7, ratios 4 / 8 / 12 / 20 / ALL |
| **LOCKDOWN** | True-peak mastering limiter: input push, ceiling, look-ahead, auto release, stereo link, LUFS and true-peak readouts |
| **HUSH MONEY** | Tames harsh S / T sounds: frequency, threshold, range; SPLIT (only the ess band) or WIDE; LISTEN |
| **WIDE LOAD** | Mono to 200 % width, MONO BASS (80–250 Hz), balance; correlation and width readouts |
| **RAZOR** | 4x oversampled clipper with a hard-to-soft SHAPE: shave peaks before the limiter |
| **SPARK** | Adds fresh harmonics above a frequency: TUBE (even) or TAPE (odd), drive, mix |
| **SUCKER PUNCH** | Transient shaper: more or less ATTACK and SUSTAIN, the same at any level |
| **THE ACCOUNTANT** | EBU R128 / BS.1770: momentary, short-term, integrated LUFS, loudness range, true peak, correlation, distance to a target |
| **FOUNDRY** | Console channel: 18 dB/oct high-pass, low shelf, mid bell, 12 kHz high shelf, preamp drive through iron |
| **KNUCKLES** | Punchy three-band EQ with proportional Q, PEAK or SHELF low / high, 50 Hz–15 kHz band-pass filter, drive |
| **MIDTOWN** | Tube midrange EQ: low peak, dip and high peak, tube drive |
| **PENTHOUSE** | Clean mastering EQ: broad bands at 10 / 40 / 160 / 650 Hz and 2.5 kHz, AIR shelf up to 20 kHz |
| **BLUEPRINT** | Ten bands, 31 Hz – 16 kHz, ±12 dB, constant Q |
| **LEVERAGE** | Darker or brighter with one knob around a pivot, low cut, high cut |
| **ENFORCER** | Clean modern compressor: ratio 1–20, knee, attack, release or AUTO, auto makeup, sidechain HPF, mix |
| **HEAVY HAND** | Classic RMS compressor: threshold, compression up to 60:1, SOFT KNEE, program-dependent timing |
| **MUSCLE** | Punchy bus compressor: LOW FOCUS (bass pumps less), knee, MODERN (feed-forward) / VINTAGE (feedback), link |
| **UNDERBOSS** | Diode-bridge bus compressor + limiter: AUTO recovery, iron drive |
| **BOUNCER** | Gate / expander: range, hold, hysteresis, detector HPF / LPF, listen |
| **TRIAD** | Three-band compressor: Linkwitz-Riley crossovers, threshold / ratio / gain per band |
| **FURNACE** | Tube stage: drive, bias (even harmonics), tone, mix — colour without a volume jump |
| **FRONT MAN** | Mic / line preamp: up to +30 dB into CLEAN, IRON or TUBE; low cut, phase invert |
| **MIXTAPE** | Cassette deck: TYPE I / II / IV, drive, wow, flutter, hiss, age |
| **CRATE DIGGER** | Record: crackle, surface noise and rumble, wear, warp, mono lows |
| **SHREDDER** | Bits 1–24, downsample 1–50x, mix |
| **ANVIL** | Output iron: the lows saturate most, weight, nickel or steel |
| **CATHEDRAL** | Concert hall reverb: pre-delay, size, decay, damping, early reflections, modulation, width, low / high cut |
| **BACKROOM** | Room reverb, small to large, early reflections up front |
| **CHROME** | Bright, dense plate reverb with diffusion control |
| **COIL** | Spring tank: decay, tension, drip, tone |
| **WIRETAP** | Tape delay: three heads, feedback to 110 % (saturates, never blows up), wobble, tone |
| **REPEAT OFFENDER** | Clean stereo delay: note values at a BPM or ms, ping-pong, stereo offset, filtered repeats |
| **THE CREW** | Stereo chorus: one to three voices, rate, depth, delay, width |
| **GETAWAY** | Jet flanger: manual, depth, positive or negative feedback |
| **SMOKESCREEN** | 4 / 6 / 8 / 12-stage phaser: center, depth, feedback |
| **SHAKEDOWN** | Tremolo or auto pan: sine, triangle, soft square |
| **CYCLONE** | Rotating speaker: slow / fast with real spin-up, drive, horn / drum balance, mic spread |
| **SHAPESHIFTER** | Resonant low / band / high-pass following the music (envelope) or an LFO |
| **UNDERGROUND** | More low end: a new sub an octave down, or harmonics small speakers can hear |
| **CHOKEHOLD** | Low-end control: tighten boomy notes, punch kick attacks, mono lows |
| **SPOTLIGHT** | Vocal chain: high-pass, body, one-knob compressor, one-knob de-esser, presence, air |

Forty-nine plug-ins, our own designs. Faceplates: brushed gunmetal and teal, with the Hybrid Elite Mafia crown and the ghost.

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
