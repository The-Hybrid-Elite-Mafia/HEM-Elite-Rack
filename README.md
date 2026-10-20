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
| **WIDE LOAD** | Mono to 200 % width, MONO BASS (80–250 Hz), shuffle (wider lows), rotation, asymmetry, balance; correlation and width readouts |
| **SPARK** | Adds fresh harmonics above a frequency: TUBE (even) or TAPE (odd), drive, mix |
| **THE ACCOUNTANT** | EBU R128 / BS.1770: momentary, short-term, integrated LUFS, loudness range, true peak, correlation, distance to a target |
| **ENFORCER** | Clean modern compressor: ratio 1–20, knee, attack, release or AUTO, auto makeup, sidechain HPF, mix; tail STEADY / LINGER; character CLEAN / WARM |
| **HITMAN** | Dual FET peak compressor: L/R or M/S, attack / release 1–7, ratios 4 / 8 / 12 / 20 / ALL, lookahead, sidechain EQ, external sidechain (VST3), saturation (clip, warm, triode, tape), mix, gain-reduction history |
| **SWITCHBLADE** | Channel compressor + gate (open / close levels, range, hold) + a filter that steers the detector or limits the work to one band; look-ahead, listen |
| **TERRITORY** | 4- or 5-band dynamics: each band turned down or lifted up (range), threshold, gain; natural or linear phase |
| **CONSIGLIERE** | Two-knob leveler: smooth light-cell or firmer solid-state, compress or limit, high emphasis, mix |
| **HEAVYWEIGHT** | Punch compressor: holds the body and lets the hits through, analog stage, release on the beat, soft limit |
| **THE DON** | Old-school broadcast bus compressor that listens to its own output; stepped ratio, attack, decay or AUTO; warmth |
| **SLOW BURN** | Variable-mu tube limiter: six time settings, linked / dual / mid-side, tube drive |
| **IRON FIST** | Console channel compressor you drive with INPUT; limiter, de-ess, output iron |
| **KNOCKOUT** | Transient designer: attack and sustain up or down, wide or four bands, guard (clip / limit) |
| **FULL HOUSE** | Loudness without squash: gate, lift the quiet parts, leveler, hold the loud parts |
| **WHEELMAN** | Automatic level rider for a vocal or a bass: target, max boost / cut, holds still in the gaps |
| **BIG SHOT** | One-band maximizer: threshold, ceiling, auto release; word length, dither and noise shaping |
| **LAST WORD** | One-band peak limiter: input left / right with link, release, auto release, digital / analog / true-peak ceiling; word length, dither, shaping |
| **HIGH STAKES** | Five-band linear-phase maximizer: four crossovers, separation, release type, gain / priority / link / release / solo per band; word length, dither, shaping |
| **TOP DOG** | Five-band linear-phase maximizer run from nine profiles: threshold, ceiling, link, release; word length, dither, shaping |
| **SWEET SIXTEEN** | Sixteen-band linear-phase maximizer: six EQ / priority bands (gain, priority, link, freq, Q, type, in), 22 release characters, separation; word length, dither, shaping |
| **FAST MONEY** | HIGH STAKES with a low-latency band split (2 ms instead of 55 ms) |
| **QUICK CASH** | TOP DOG with the low-latency band split |
| **SILENCER** | De-esser: classic filter or smart detection, split or wide, look-ahead, listen |
| **BREATHLESS** | Breath remover: finds breaths between words, turns them down with smooth fades |
| **TOMMY GUN** | One-knob tempo pump: ducks on the beat and pumps back up, no sidechain needed |
| **STRAIGHT SHOOTER** | Linear-phase EQ: low / high cut (12 / 24 / 48 dB), shelves, three bells, no phase shift at all |
| **CUTTHROAT** | Sweep filter: high-pass + low-pass, one-knob SWEEP, resonance, 12 / 24 dB, drive, mix |
| **THE BOSS** | Channel strip: gate, compressor, four-band EQ, low cut, EQ before or after the dynamics, limiter |
| **KINGPIN** | Drum channel: gate, thump, snap, punch, crush (a squashed copy underneath), drive |
| **GOLD CHAIN** | One-knob tone: warmth, density and sheen together; input, link, mix |
| **IVORIES** | Piano tone: darker / brighter, body, compression, stereo slap delay, doubler, width |
| **HIGH ROLLER** | Five-band enhancer: lift and width for sub, low, mid, high and air |
| **THE MIDDLEMAN** | Center / sides: levels, low and high balance, hits steered to the middle or the sides |
| **DOUBLE CROSS** | Mono to stereo that folds back to mono perfectly; tight / medium / wide, low stays mono |
| **THE FIXER** | Three dynamic cuts (mud, harsh, fizz) that only work when a range gets nasty; listen per band |
| **SIX SHOOTER** | Rhythmic multi-tap delay: 1–6 shots on a note or in ms, decay, left / right spread, swing, feedback, tone |
| **BACK ALLEY** | Lo-fi space: spring or plate style reverb and a worn tape echo, grit, wobble, narrow |
| **DRIVE-BY** | A sound passing by: the pitch bends as it passes, level, pan and air; manual or passes on the beat |
| **BODY DOUBLE** | Doubler: 2 or 4 late, slightly detuned voices spread left / right |
| **HUSTLE** | One LFO for tremolo, vibrato and pan; free or on a note of the tempo |
| **THE INFORMANT** | Vocoder with its own synth carrier: note, octave, chord, saw / pulse / noise, 16–32 bands, formant shift |
| **STRAPPED** | Guitar mix strip: tight, compress, bass / mids / treble, chorus, delay on the beat, room, width |
| **SAFE HOUSE** | Headphone room: hear speakers at an angle in a dry, studio or live room (for listening; bypass before export) |
| **TORCH** | Guitar / bass amp: clean, crunch, lead or bass; gate, gain, bass / mid / treble, presence, 1x12 / 4x12 / 8x10 cabinet, room |
| **THE RACKET** | Drive pedal: overdrive, distortion, fuzz or octave fuzz; drive, tone, level, mix |
| **THE SNITCH** | Tuner: note, cents and frequency; reference A 430–450 Hz; mute while tuning |
| **PROMOTION** | Pitch shifter: ±12 semitones and cents, smooth or tight |
| **WISEGUYS** | Harmonizer: three voices with their own interval, level and pan |
| **MADE MAN** | Pitch correction for one voice: key, scale, speed, amount |
| **THE DETECTIVE** | Key finder: names the song's key and how sure it is |
| **THE WITNESS** | VU and peak meters for left and right, calibration, peak hold |
| **LIVE WIRE** | Test tones: sine, square, saw, triangle, white / pink noise, sweeps; left / right / both / opposed |
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

Seventy plug-ins, our own designs. Faceplates: brushed gunmetal and teal, with the Hybrid Elite Mafia crown and the ghost.

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
