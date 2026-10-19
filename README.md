# HEM-Elite-Rack

Hybrid Elite Mafia's plug-in rack for Windows: load a song, stack HEM plug-ins, hear them live,
compare A / B at matched loudness and export a WAV. The same plug-ins ship as VST3.

Built on **The Hybrid Core** (`external/HybridCore`, a git submodule), Hybrid Elite Mafia's own
C++23 audio framework, as a library.

| Plug-in | What it does |
|---|---|
| **ELITE EQ** | 16-band mastering / mixing EQ: bell, shelves, cuts (6–48 dB/oct), notch, tilt; per band stereo / mid / side / left / right; dynamic bands; CLEAN / CONSOLE / TUBE character; auto gain |
| **RESTORER** | Adaptive hiss and noise reduction that keeps the air and the attacks |

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
