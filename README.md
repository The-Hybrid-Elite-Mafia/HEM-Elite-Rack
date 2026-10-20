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
| **ARCHIVE** | Two-track tape machine: input drive, 15 / 30 IPS, head bump, wow & flutter, hiss, auto trim, VU meters with VU zero |
| **VELVET** | Passive tube program EQ: low boost / atten (the low-end trick), high boost with bandwidth, high atten, tube drive, EQ in / out, hum |
| **FOUNDRY** | British console channel EQ: stepped low cut (18 dB / oct), lows, middle and highs at the console's own steps, EQ in / out, flip, vintage colour; peak, to-top and clip readouts |
| **BOARDROOM** | British console four-band EQ: stepped high- and low-pass, lows and highs as shelf or bell, wide or tight low and high mids, EQ in / out, flip, vintage; peak readouts |
| **MIDTOWN** | Tube midrange EQ: low peak, dip and high peak at stepped frequencies, EQ in / out, hum, output; the curve on the plate |
| **LOCKDOWN** | True-peak mastering limiter: input push, ceiling, look-ahead, auto release, stereo link, LUFS and true-peak readouts |
| **WIDE LOAD** | Mono to 200 % width, MONO BASS (80–250 Hz), low widen and its frequency, low trim, aim, center shift, balance, polarity, swap, M / S in; correlation, width and L / R or M / S meters |
| **SPARK** | Adds fresh harmonics above a frequency: TUBE (even) or TAPE (odd), drive, mix |
| **THE ACCOUNTANT** | EBU R128 / BS.1770: momentary, short-term, integrated LUFS, loudness range, true peak, correlation, distance to a target |
| **ENFORCER** | Clean modern compressor: ratio 1–20, knee, attack, release or AUTO, auto makeup, sidechain HPF, mix; tail STEADY / LINGER; character CLEAN / WARM |
| **HITMAN** | Dual FET peak compressor: L/R or M/S, attack / release 1–7, ratios 4 / 8 / 12 / 20 / STACKED, lookahead, sidechain EQ, external sidechain (VST3), saturation (clip, warm, triode, tape), mix, gain-reduction history |
| **SWITCHBLADE** | Channel compander: compressor / expander (level ref, makeup, ratio 0.5-50 or cancel, attack, release, smart release) and gate / expander (open, close, floor, slope, hold), each wide, keyed or split by the filter; look-ahead, key L>R / R>L, monitor, input L / R |
| **TERRITORY** | 4-band dynamics: threshold, gain, range, attack, release, solo, bypass per band; master offsets; crossovers and their Q; auto / manual release, let-go ease out / snap back, knee |
| **STRAIGHT EDGE** | 5-band linear-phase dynamics: the same band and master controls, adaptive floor, auto makeup, one-push trim, dither |
| **CONSIGLIERE** | Light-cell leveler: gain, squeeze, compress / limit, bright detect, auto makeup, hum, mix, trim, IN / GR / OUT meter |
| **STONE COLD** | Solid-state light-cell leveler: quicker and firmer, the same controls on 0-10 dials |
| **HEAVYWEIGHT** | Punch compressor: threshold, ratio, attack, snap, release in ms or on the beat, four vintage flavours, limiter or clipper, mix, meter |
| **THE DON** | Old-school broadcast bus compressor (listens to its own output): stepped threshold, ratio, decay; VU meter with VU zero, hum |
| **SLOW BURN** | Stereo variable-mu tube limiter: input, threshold, six timings, separate / linked / mid-side, hum, compress on / off |
| **LONE WOLF** | One-channel variable-mu tube limiter: input, threshold, six timings, hum, compress on / off |
| **IRON FIST** | Console channel compressor + limiter you drive with INPUT: ratio, release, de-ess, limit at / attack / release, vintage, auto makeup, mix, trim |
| **QUICK DRAW** | Fast transistor limiting amp: input, output, attack and release 1-7, 4 / 8 / 12 / 20 / STACKED, early or late era, auto makeup, mix, trim |
| **OLD MONEY** | Smooth all-purpose compressor: ratio 0.5-50, auto or manual release, snap back or ease out, warm or smooth, output limiter, mix, trim |
| **KNOCKOUT** | Transient designer: attack and sustain each with level, pickup, length and curve; safety, mix, output |
| **HAYMAKER** | Transient shaper: range, pickup, length, release, output, one-push trim |
| **FOUR KINGS** | Four-band transient shaper: range, pickup, on per band, master, length, release, trim |
| **BACKBONE** | Two-fader loudness: quiet up lifts the quiet parts, loud down holds the loud ones, output |
| **FULL HOUSE** | Loudness without squash: evener (strong / gentle), gate, low (threshold, gain), high (threshold, ceiling), each switchable |
| **WHEELMAN** | Vocal rider: target, voice pickup, attack, lowest / highest ride, rest point, stage bleed floor, output |
| **CHAUFFEUR** | Bass rider: target, note pickup, speed, ignore bleed, ignore clicks, lowest / highest ride, rest point, output |
| **BIG SHOT** | One-band maximizer: threshold, ceiling, auto release; bit depth, dither and noise shape |
| **LAST WORD** | One-band peak limiter: input left / right with link, release, auto release, sample / smoothed / true-peak ceiling; bit depth, dither, noise shape |
| **HIGH STAKES** | Five-band linear-phase maximizer: four crossovers, band freedom, release type, gain / weight / link / release / solo per band; bit depth, dither, noise shape |
| **TOP DOG** | Five-band linear-phase maximizer run from nine profiles: threshold, ceiling, link, release; bit depth, dither, noise shape |
| **SWEET SIXTEEN** | Sixteen-band linear-phase maximizer: six EQ / weight bands (gain, weight, link, freq, Q, type, in), 22 release feels, band freedom; bit depth, dither, noise shape |
| **FAST MONEY** | HIGH STAKES with a low-latency band split (2 ms instead of 55 ms) |
| **QUICK CASH** | TOP DOG with the low-latency band split |
| **SILENCER** | De-esser: threshold, frequency, band only or whole, high-pass or band-pass detector, monitor |
| **TIGHT LIPS** | De-esser: threshold, range, frequency, detector filter, band only or whole, listen |
| **OMERTA** | Smart de-esser that hears esses at any pitch: threshold, range, catch width, whole-to-band, look-ahead, monitor |
| **BREATHLESS** | Breath remover: breath and level thresholds, reduction, soft start / end, room floor, listen cleaned / removed |
| **TOMMY GUN** | One-knob tempo pump: amount 0-10, rate, offset (tempo from the host or a BPM) |
| **STRAIGHT SHOOTER** | Six-band linear-phase EQ: bells, variable-slope and bump cuts and shelves, ±30 dB, trim, dither, no phase shift at all |
| **BOTTOM LINE** | Three-band linear-phase EQ for the low end (11–600 Hz), the same nine shapes |
| **TEN TOES** | Ten-band EQ with separate left / right sets: bells, shelves, cuts, tight bells; input, phase and output per side |
| **FOUR CORNERS** | Four-filter EQ with separate left / right sets: bells, shelves, resonant shelves, cuts |
| **SMOOTH TALKER** | Six-band EQ with separate left / right sets, resonant shelves, spectrum on / off, trim |
| **CUTTHROAT** | High-pass + low-pass, 18 dB / oct, separate left / right sets with link |
| **TUNNEL VISION** | One-knob filter: a low-pass closing from wide open, resonance in four steps |
| **THE SETUP** | A filter played by an LFO, a 16-step sequencer and a follower: low / high / band pass, notch, comb, level; drive, crush, spread, delay |
| **THE BOSS** | British-console channel: mic / line preamp, stepped 10 kHz shelf, mid boost / cut, low shelf, 50 Hz cut, hum and hiss, VU |
| **INSIDE MAN** | Channel strip: gate / expander and compressor with key filters, four-band EQ, order, phase, output, place, clip guard |
| **HEAVY HITTER** | Drum enhancer: pick the drum (kick, snare top / under, hats, toms, overheads, room), then sensitivity, thump, treble, snap, output; meter in or out |
| **KINGPIN** | Drum channel: pick the drum (kick … room), then pickup, compress, gate, ambience, bass, treble |
| **GOLD CHAIN** | One-knob tone: warmth, density and sheen together; input, link, output, hum |
| **IVORIES** | Piano tone: one VOICING knob, slap and thicken (on + level), input, output, hum |
| **HIGH ROLLER** | Five-band parallel enhancer: per band gain, width, solo, bypass; four crossovers, input, output, dry, snap |
| **THE MIDDLEMAN** | Center / sides faders; low, high and snap follow the center or the sides; all; center meter |
| **DOUBLE CROSS** | Mono to stereo by frequency sweeps that fold back to mono exactly: spread, combs, sweep spacing, fine shift, split curves, input width / aim |
| **LOW RIDER** | A new low octave under the bass: range, new octave, lows, dry, output, trim, line up |
| **UNDERTOW** | Subs one and two octaves down: range, levels, drive, follow, sub in middle, mix |
| **DEEP POCKETS** | Bass small speakers can play: overtones in place of the lows, decay, ratio, speed, high-pass, monitor |
| **LOW PROFILE** | Bass harmonics in one move: frequency, real bass in / out, amount, gain |
| **THE FIXER** | Three dynamic cuts (mud, harsh, fizz) that only work when a range gets nasty: per band amount, freq, listen, bypass; input, output |
| **DEAD DROP** | Hybrid delay: time or note (host / BPM / ms, tap), split times, feedback past 100 %, ping pong, phase, hi / lo-pass, wobble, vintage, lo-res, mix |
| **SIX SHOOTER** | Six-tap delay: per tap on, time, gain, place and EQ; dry; tempo (host, BPM, tap), grid and lock; wobble; feedback with its own filter |
| **BACK ALLEY** | Lo-fi space: a worn echo (time or note, feedback, ping pong) and a spring or plate reverb; hi-pass, lo-pass, mono, mix |
| **DRIVE-BY** | A sound passing by on a track: pass time, closest at, start / end, distance, direction; gain, pan, pitch, air loss and reverb amounts; loop or once on a trigger |
| **BODY DOUBLE** | Doubler: four voices with gain, pan, delay, detune, feedback, octave and wobble; dry, in time, feed, lowest note, low / high shelves |
| **HUSTLE** | One LFO for level, pitch and position: rate (or the tempo) x multiplier, five waves, phase per move, gain, mix |
| **SWERVE** | Flanger: delay, depth, rate or sweeps per beat, sine / triangle, tie, hold, stereo, feedback and mix with flips, filter, tape (through zero), gain, peaks |
| **COLD CASE** | Swept notches with a ringing loop: pairs, half, depth, flip, stereo, low cut; sweep run, wave, rate x multiplier, tempo, low / high; loop decay, flip, delay, density, hi tame, filter; high cut, width, rotate, pre-delay, wet mode, mix, gain |
| **MOUTHPIECE** | Vocal leveler in one move: gate, press, ceiling; total, output and input readouts |
| **SLINGER** | Guitar compressor: threshold (the ratio follows), attack, gain into a limiter; input, attenuation, output, limiter readouts |
| **STREET TEAM** | Channel strip: input, four-band EQ (cut / shelves / bells), gate with floor, compressor / expander, output |
| **BIG HOUSE** | Reverb with twelve kinds of space (halls, room, chamber, sanctuary, plates, backwards, gated, shaped, echo room, ringing): variation, pre-delay either way, time, size, diffusion, decay shape, early / reverb levels, damping ratios, tail EQ, mix, gain |
| **SPEAKEASY** | A real room around the sound: room size, distance (sets the first-reflection gap, and with REAL ROOM the levels and pre-delay as in a real room), room shape, balance, decay, pre-delay, density, dulling of the tail and the early reflections, low / high decay ratios, two variations, input, direct / early / reverb on with levels |
| **GRAND LARCENY** | Electric grand piano (an instrument: a MIDI keyboard in the rack, a MIDI track in a DAW, or its 61 plate keys): velocity curve, formant, tune; main, key up, action, pedal sound, squeeze, sympathetic strings; bass / mid / treble; volume, limit; tremolo, auto pan, phaser (rate synced or free), chorus, reverb; sustain pedal |
| **FINGERPRINT** | Convolution reverb: sixteen spaces of our own (booth, rooms, chambers, halls, church, plates, springs, garage), start, length, decay, mix, direct, pre-delay, dry delay, CPU full / low, output, crosstalk; RT60 readout |
| **COIL** | Spring reverb: drive, headroom, pre-delay, seven tanks, short / medium / long, treble and bass on the springs, mix, output, stereo or mono |
| **THE INFORMANT** | Vocoder with its own 8-voice synth on 48 keys (hold or latch), ten sounds, tune, spread; noise with its filter; squeeze, formant, lag, hold; five-band EQ; carrier / modulator / noise / vocoded mixer with solo and mute |
| **STRAPPED** | Guitar mix desk: clean tone / edge / high gain, re-amp, then six faders with a style each: bass, treble, compress, reverb, delay, pitch |
| **SAFE HOUSE** | Speakers in your headphones: room, speaker angle, head turn, head size, ear to ear, mute left / right (for listening; bypass before export) |
| **TORCH** | Guitar amp: ten amp types, drive, bass / mid / treble, presence, master, input, output; two cabinet paths (eight cabinets, six mic placements, volume, pan, phase), dual or stereo |
| **HOT WIRE** | Guitar head: five amps, clean / drive / lead, focus, gate and gate speed, gain, bass, middle, treble, volume, depth, presence; six cabinet pairs with cab tone, room, cab off; output limiter with readouts |
| **BOTTOM FEEDER** | Bass rig: two amps; DI tone, level, blend and flip; a drive pedal (fat, tight, sharp) with tone, gain, mix; voice; gain, bass, mid, hi mid, high; cabinet tone, room, cab off; output limiter |
| **THE RACKET** | Pedalboard (its own plate), one pedal at a time: 26 pedals (drives, modulation, echoes, rooms, squeezes, gates, EQs, volume), each with its own controls; input mode and gain |
| **THE SNITCH** | Tuner: chromatic, guitar and six other tunings or your own notes (twelve switches); reference A ±6 cents; note, cents and frequency |
| **PROMOTION** | Pitch shifter: by transpose (semitones as intervals, cents) or by frequency (from / to Hz); sync, smooth, transient or punchy |
| **WISEGUYS** | Voice harmonizer: six voices (on, pitch, formant, gain, pan, delay) and the direct voice (on, gain, delay); animate, link, mode, min / max pitch |
| **MADE MAN** | Pitch correction for one voice: root, twenty scales or your own notes, speed, note transition, ratio, formant corrected, shift, reference A, min / max pitch |
| **THE DETECTIVE** | Key finder: the key, two alternatives and the song's tuning; detect / stop, your own key (root, scale), sharps or flats |
| **THE AUDITOR** | Loudness meter (its own plate): EBU / LM1 / DIAL, six weightings, short time, short max / min with overs and unders, true peak max, target and trim, LU or LUFS, pre-filters; gain and a true-peak limiter |
| **STAKEOUT** | Analyzer (its own plate): frequency bands with LF resolution, peak or RMS, response, L / R or sum, weighting, freeze, max line; stereo position; peak and sum meters with holds, clip lights |
| **THE WITNESS** | VU meters for left and right: headroom, display delay, clip lights with clear |
| **LIVE WIRE** | Signal generator: pink, white or sine; frequency and gain with quick settings; send L / R / both; flip |
| **FURNACE** | Tube stage: drive, bias (even harmonics), tone, mix — colour without a volume jump |
| **REEL MONEY** | Tube tape machine: 7.5 / 15 IPS, tape or input monitor, normal or hot bias, into / off tape with link, tape level, wobble, hiss, slap or repeating tape echo with tone, VU with its zero |
| **PAWN SHOP** | Lo-fi box in four modules: an old device (sound, tone, squash, ringer, decade, mix), space (synced echo, ping pong, feedback, spring or plate), twelve noises (duck or gate, pre or post), tape and record mechanics; low / high cut, mono, in / out |
| **HOTHEAD** | One-knob overdrive: from untouched to full distortion |
| **MIXTAPE** | Cassette deck: tape type I / II / IV, drive, wow, flutter, hiss, age |
| **CRATE DIGGER** | Record player: crackle, surface noise and rumble, wear, warp, mono lows |
| **SHREDDER** | Bits and sample rate down: bits, downsample, mix |
| **CATHEDRAL** | Concert hall reverb: pre-delay, size, decay, damping, early reflections, modulation, width, low / high cut |
| **BACKROOM** | Room reverb, small to large: early reflections up front, short natural tails; pre-delay, size, decay, damping, early, modulation, width, low / high cut |
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

One hundred and eighteen plug-ins, under our own names and in our own words. Faceplates: brushed gunmetal and teal, with the Hybrid Elite Mafia crown and the ghost.

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
