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
| **VELVET** | Passive tube program EQ: low boost / atten (the low-end trick), high boost with bandwidth, high atten, tube drive, EQ in / out, analog hum |
| **LOCKDOWN** | True-peak mastering limiter: input push, ceiling, look-ahead, auto release, stereo link, LUFS and true-peak readouts |
| **WIDE LOAD** | Mono to 200 % width, MONO BASS (80–250 Hz), shuffle and its frequency, bass trim, rotation, asymmetry, balance, polarity, swap, M / S in; correlation, width and L / R or M / S meters |
| **SPARK** | Adds fresh harmonics above a frequency: TUBE (even) or TAPE (odd), drive, mix |
| **THE ACCOUNTANT** | EBU R128 / BS.1770: momentary, short-term, integrated LUFS, loudness range, true peak, correlation, distance to a target |
| **ENFORCER** | Clean modern compressor: ratio 1–20, knee, attack, release or AUTO, auto makeup, sidechain HPF, mix; tail STEADY / LINGER; character CLEAN / WARM |
| **HITMAN** | Dual FET peak compressor: L/R or M/S, attack / release 1–7, ratios 4 / 8 / 12 / 20 / STACKED, lookahead, sidechain EQ, external sidechain (VST3), saturation (clip, warm, triode, tape), mix, gain-reduction history |
| **SWITCHBLADE** | Channel compander: compressor / expander (level ref, makeup, ratio 0.5-50 or cancel, attack, release, PDR) and gate / expander (open, close, floor, slope, hold), each wide, keyed or split by the filter; look-ahead, key L>R / R>L, monitor, input L / R |
| **TERRITORY** | 4-band dynamics: threshold, gain, range, attack, release, solo, bypass per band; master offsets; crossovers and their Q; auto / manual release, ease out / snap back, knee |
| **STRAIGHT EDGE** | 5-band linear-phase dynamics: the same band and master controls, adaptive, auto makeup, one-push trim, dither |
| **CONSIGLIERE** | Light-cell leveler: gain, peak reduction, compress / limit, hi freq, auto makeup, analog hum, mix, trim, IN / GR / OUT meter |
| **STONE COLD** | Solid-state light-cell leveler: quicker and firmer, the same controls on 0-10 dials |
| **HEAVYWEIGHT** | Punch compressor: threshold, ratio, attack, punch, release in ms or on the beat, four analog flavours, limiter or clipper, mix, meter |
| **THE DON** | Old-school broadcast bus compressor (listens to its own output): stepped threshold, ratio, decay; VU meter with calibration, analog hum |
| **SLOW BURN** | Stereo variable-mu tube limiter: input, threshold, six time constants, separate / linked / mid-side, mains, compress on / off |
| **LONE WOLF** | One-channel variable-mu tube limiter: input, threshold, six time constants, mains, compress on / off |
| **IRON FIST** | Console channel compressor + limiter you drive with INPUT: ratio, release, de-ess, limit level / attack / release, analog, auto makeup, mix, trim |
| **QUICK DRAW** | Fast transistor limiting amp: input, output, attack and release 1-7, 4 / 8 / 12 / 20 / STACKED, early or late era, auto makeup, mix, trim |
| **OLD MONEY** | Smooth all-purpose compressor: ratio 0.5-50, auto or manual release, snap back or ease out, warm or smooth, output limiter, mix, trim |
| **KNOCKOUT** | Transient designer: attack and sustain each with level, sensitivity, length and shape; guard, mix, output |
| **HAYMAKER** | Transient shaper: range, sensitivity, duration, release, output, one-push trim |
| **FOUR KINGS** | Four-band transient shaper: range, sensitivity, on per band, master, duration, release, trim |
| **BACKBONE** | Two-fader loudness: low level lifts the quiet parts, high level holds the loud ones, output |
| **FULL HOUSE** | Loudness without squash: leveler (loud / soft), gate, low level (threshold, gain), high level (threshold, ceiling), each switchable |
| **WHEELMAN** | Vocal rider: target, vocal sensitivity, attack, range min / max, idle, stage bleed floor, output |
| **CHAUFFEUR** | Bass rider: target, note detect, response, ignore bleed, ignore artifacts, range, idle, output |
| **BIG SHOT** | One-band maximizer: threshold, ceiling, auto release; word length, dither and noise shaping |
| **LAST WORD** | One-band peak limiter: input left / right with link, release, auto release, digital / analog / true-peak ceiling; word length, dither, shaping |
| **HIGH STAKES** | Five-band linear-phase maximizer: four crossovers, separation, release type, gain / priority / link / release / solo per band; word length, dither, shaping |
| **TOP DOG** | Five-band linear-phase maximizer run from nine profiles: threshold, ceiling, link, release; word length, dither, shaping |
| **SWEET SIXTEEN** | Sixteen-band linear-phase maximizer: six EQ / priority bands (gain, priority, link, freq, Q, type, in), 22 release characters, separation; word length, dither, shaping |
| **FAST MONEY** | HIGH STAKES with a low-latency band split (2 ms instead of 55 ms) |
| **QUICK CASH** | TOP DOG with the low-latency band split |
| **SILENCER** | De-esser: threshold, frequency, split or wide, high-pass or band-pass sidechain, monitor |
| **TIGHT LIPS** | De-esser: threshold, range, frequency, sidechain filter, split or wide, listen |
| **OMERTA** | Smart de-esser that hears esses at any pitch: threshold, range, detection width, wide-to-split, look-ahead, monitor |
| **BREATHLESS** | Breath remover: breath and level thresholds, reduction, fades, room tone, listen voice / breaths |
| **TOMMY GUN** | One-knob tempo pump: amount 0-10, rate, offset (tempo from the host or a BPM) |
| **STRAIGHT SHOOTER** | Six-band linear-phase EQ: bells, precision (variable-slope) and resonant cuts and shelves, ±30 dB, trim, dither, no phase shift at all |
| **BOTTOM LINE** | Three-band linear-phase EQ for the low end (11–600 Hz), the same nine shapes |
| **TEN TOES** | Ten-band EQ with separate left / right sets: bells, shelves, cuts, tight bells; input, phase and output per side |
| **FOUR CORNERS** | Four-filter EQ with separate left / right sets: bells, shelves, resonant shelves, cuts |
| **SMOOTH TALKER** | Six-band EQ with separate left / right sets, resonant shelves, spectrum on / off, trim |
| **CUTTHROAT** | High-pass + low-pass, 18 dB / oct, separate left / right sets with link |
| **TUNNEL VISION** | One-knob filter: a low-pass closing from wide open, resonance in four steps |
| **THE SETUP** | A filter played by an LFO, a 16-step sequencer and a follower: low / high / band pass, notch, comb, amp; drive, crush, spread, delay |
| **THE BOSS** | British-console channel: mic / line preamp, stepped 10 kHz shelf, mid boost / cut, low shelf, 50 Hz cut, analog noise, VU |
| **INSIDE MAN** | Channel strip: gate / expander and compressor with key filters, four-band EQ, order, phase, output, rotate, clip guard |
| **KINGPIN** | Drum channel: pick the drum (kick … room), then sensitivity, compress, gate, FX ambience, bass, treble |
| **GOLD CHAIN** | One-knob tone: warmth, density and sheen together; input, link, output, analog |
| **IVORIES** | Piano tone: one PIANO knob, slap delay and doubler (on + level), input, output, analog |
| **HIGH ROLLER** | Five-band parallel enhancer: per band gain, width, solo, bypass; four crossovers, input, output, direct, punch |
| **THE MIDDLEMAN** | Center / sides faders; low, high and punch follow the center or the sides; master; center meter |
| **DOUBLE CROSS** | Mono to stereo by frequency sweeps that fold back to mono exactly: spread, sweeps, density, tweak, split shapes, input width / rotation |
| **LOW RIDER** | A new low octave under the bass: range, low air, lo, direct, output, trim, align |
| **UNDERTOW** | Subs one and two octaves down: range, levels, drive, dynamics, mono sub, mix |
| **DEEP POCKETS** | Bass small speakers can play: harmonics in place of the lows, decay, ratio, response, high-pass, monitor |
| **LOW PROFILE** | Bass harmonics in one move: frequency, original bass in / out, intensity, gain |
| **THE FIXER** | Three dynamic cuts (mud, harsh, fizz) that only work when a range gets nasty: per band amount, freq, listen, bypass; input, output |
| **DEAD DROP** | Hybrid delay: time or note (host / BPM / ms, tap), dual, feedback past 100 %, ping pong, phase, hi / lo-pass, wobble, analog, lo-fi, mix |
| **SIX SHOOTER** | Six-tap delay: per tap on, time, gain, position and EQ; direct; tempo (host, BPM, tap), grid and snap; modulator; feedback with its own filter |
| **BACK ALLEY** | Lo-fi space: a worn echo (time or note, feedback, ping pong) and a spring or plate reverb; hi-pass, lo-pass, mono, mix |
| **DRIVE-BY** | A sound passing by on a track: track time, closest point, start / end, distance, direction; gain, pan, pitch, air and reverb amounts; on its own or on a trigger |
| **BODY DOUBLE** | Doubler: four voices with gain, pan, delay, detune, feedback, octave and wobble; direct, routing, low / high shelves |
| **HUSTLE** | One LFO for level, pitch and position: rate (or the tempo) x factor, five waves, phase per move, gain, mix |
| **THE INFORMANT** | Vocoder with its own 8-voice synth on 48 keys (sustain), ten sounds, tune, spread; noise with its filter; pressure, formant, smoothing, release; five-band EQ; carrier / modulator / noise / morph mixer with solo and mute |
| **STRAPPED** | Guitar mix desk: clean / crunch / heavy, re-amp, then six faders with a colour each: bass, treble, compress, reverb, delay, pitch |
| **SAFE HOUSE** | Speakers in your headphones: room amount, speaker spread, head turn, head size (circumference, arc), mute left / right (for listening; bypass before export) |
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

Ninety-two plug-ins, our own designs. Faceplates: brushed gunmetal and teal, with the Hybrid Elite Mafia crown and the ghost.

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

## Licence
HEM-Elite-Rack is free software under the GNU General Public License, version 3 or (at your option) any later
version (`LICENSE`). Copyright (C) 2026 Steven Reliford / Hybrid Elite Mafia. Each release carries its complete
source code as `HEM-Elite-Rack-<version>-source.zip`.

Third-party: Steinberg ASIO SDK 2.3.4, used under its GPLv3 option (fetched from Steinberg at build time and
checked, never committed; ASIO is a trademark of Steinberg Media Technologies GmbH). VST3 SDK 3.8.1: MIT (fetched at
build time). Poppins font: SIL OFL 1.1. stb, dr_libs: public domain / MIT.
