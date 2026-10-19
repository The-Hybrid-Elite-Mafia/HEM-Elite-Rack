# HEM-Elite-Rack 0.9.0

**Space: six reverbs and delays — forty plug-ins in all**, in the rack and as VST3:
- **HALL / ROOM / PLATE** — an 8-line feedback-delay-network reverb voiced three ways. Measured
  reverb times come out 9–14 % under the DECAY setting (tested: 3.0 s reads 2.63 s, 0.8 s reads
  0.70 s); the left and right tails are uncorrelated (wide).
- **SPRING** — two springs with the dispersive "drip".
- **TAPE ECHO** — three heads, wobble, saturation; feedback up to 110 % stays bounded (tested
  for 10 s).
- **DIGITAL DELAY** — note values at a BPM (or ms), ping-pong, stereo offset, filtered repeats.
  Repeats land on the exact sample.

The rack has no tempo of its own yet, so DIGITAL DELAY takes the BPM as a knob.

# HEM-Elite-Rack 0.8.0

**Six new colour plug-ins — thirty-four in all**, in the rack and as VST3. Each shows the spectrum
before and after, so you can see what it adds:
- **TUBE SAT** — drive, BIAS for even harmonics, tone, mix. Quiet passages stay at the same level.
- **PREAMP** — up to +30 dB into a CLEAN, IRON or TUBE stage, 80 Hz low cut, phase invert.
- **CASSETTE** — TYPE I / II / IV, drive, wow, flutter, hiss, age.
- **VINYL** — crackle, surface noise and rumble, wear, warp (33 rpm), mono lows.
- **BIT CRUSHER** — bits and downsampling, mix.
- **TRANSFORMER** — output iron: the lows saturate far more than the highs, weight, nickel / steel.

**Faceplate fixes:** BRIT BUS recovery buttons fit their text; MULTIBAND and VCA COMP labels no
longer overlap.

# HEM-Elite-Rack 0.7.0

**Six new dynamics plug-ins — twenty-eight in all**, in the rack and as VST3:
- **VCA COMP** — clean modern compressor: ratio 1–20, knee, attack, release or AUTO, AUTO GAIN,
  sidechain HPF, mix.
- **PUNCH 160** — classic RMS compressor: threshold, compression up to 60:1, OVER EASY; grabs
  faster the harder it's hit, lets go at a steady ~120 dB per second.
- **THRUST BUS** — punchy bus compressor: THRUST keeps the bass from pumping it, knee, NEW
  (feed-forward) or OLD (feedback) style, stereo link.
- **BRIT BUS** — British diode-bridge bus compressor with a limiter after it, AUTO recovery,
  iron drive.
- **GATE** — gate or expander with range, hold, hysteresis, detector filters, listen.
- **MULTIBAND** — three-band compressor; with nothing compressing the bands add back up flat
  (tested: within 0.000 dB from 40 Hz to 12 kHz).

**Not in this round:** a DUCKER (it needs a second input — the rack doesn't route a sidechain
yet), so MULTIBAND took its place.

# HEM-Elite-Rack 0.6.0

**Six new EQs — twenty-two plug-ins in all**, in the rack and as VST3. Each draws its curve on the
faceplate, computed by the same code the audio runs (tested: curve and sound agree to 0.000 dB):
- **BRIT PRE** — British console channel: 18 dB/oct HIGH-PASS, LOW shelf, MID bell (narrows as you
  push it), 12 kHz HIGH shelf, PREAMP drive through iron (odd harmonics).
- **AMERICAN EQ** — three bands with proportional Q (a +3 dB move is about twice as wide as +12),
  PEAK or SHELF on low and high, FILTER (50 Hz – 15 kHz), DRIVE.
- **MID EQ** — tube midrange EQ: LOW PEAK, DIP, HIGH PEAK, tube DRIVE (even harmonics).
- **AIR EQ** — clean, broad mastering bands (10 / 40 / 160 / 650 Hz, 2.5 kHz) and an AIR shelf
  (2.5 / 5 / 10 / 20 kHz). Zero latency.
- **GRAPHIC EQ** — ten bands, 31 Hz – 16 kHz, ±12 dB. Zero latency.
- **TILT** — darker / brighter around a PIVOT, LOW CUT, HIGH CUT. Zero latency.

**Fixes:** LOUDNESS METER RESET is a push button now (no confusing ON / OFF); STEREO WIDTH's
correlation label no longer runs into the number; EXCITER's display dims the range below
FREQUENCY (where nothing is added) and marks the edge.

# HEM-Elite-Rack 0.5.0

**Six new plug-ins — sixteen in all**, in the rack and as VST3:
- **DE-ESSER** — tames harsh S / T sounds. SPLIT turns down only the ess band, WIDE the whole
  signal; LISTEN plays what the detector hears; a needle shows how much it takes off.
- **STEREO WIDTH** — mono to 200 %, MONO BASS (below 80 / 120 / 160 / 250 Hz the low end stays
  in the centre), BALANCE; CORRELATION and SIDE VS MID readouts.
- **CLIPPER** — 4x oversampled peak shaving with a HARD-to-SOFT shape; nothing comes out above
  the CEILING; the needle shows how much the peaks are shaved.
- **EXCITER** — new TUBE (even) or TAPE (odd) harmonics above a frequency: brightness and air
  without boosting EQ. The display shows before / after.
- **TRANSIENT** — more or less ATTACK (punch) and SUSTAIN (ring, room), whatever the level.
- **LOUDNESS METER** — EBU R128: momentary, short-term and integrated LUFS (gated like the
  standard), loudness range, true peak max, correlation, and how far you are from a target
  (-14 / -16 / -11 / -9). The sound passes through untouched.

**Also:** the plug-in picker has tabs (EQ, DYNAMICS, COLOR, TOOLS) so the growing list stays easy
to browse.

# HEM-Elite-Rack 0.4.0

**New: LIMITER** — the mastering limiter for the end of the chain (in the rack and as VST3):
- INPUT pushes the song louder; nothing ever comes out above the CEILING.
- TRUE PEAK: peaks are found 4x oversampled, so the ones *between* samples stay under the ceiling too
  (those are the ones that clip in MP3 / streaming encoders).
- 1.5 ms look-ahead (the gain is already down when a peak arrives), RELEASE or AUTO, stereo LINK.
- Readouts: the output loudness in LUFS (the same measurement streaming services use) and the true peak.

# HEM-Elite-Rack 0.3.0

**Five new plug-ins** — nine in all, in the rack and as VST3:
- **TAPE** — a two-track tape machine: INPUT drive (highs saturate first, like real tape),
  15 / 30 IPS, HEAD BUMP, WOW & FLUTTER, HISS, AUTO TRIM, a pair of VU meters with VU CAL
  (what reads 0 VU: -18 / -14 / -10 / -6 dBFS, so loud songs don't pin the needles).
- **TUBE EQ** — a passive tube program EQ: LOW boost and atten (together: more weight at the bottom,
  less mud above), HIGH boost with WIDTH, HIGH ATTENUATION, tube DRIVE. Its display shows the curve.
- **VARI-MU** — a variable-mu tube compressor: the ratio grows the harder you push, six TIME
  CONSTANTs (5 and 6 release by themselves), STEREO or MID / SIDE, TUBE, MAKEUP, MIX.
- **OPTO** — an optical leveler: PEAK REDUCTION and GAIN, COMPRESS / LIMIT, a two-stage release
  (fast, then slow), EMPHASIS for the highs, MIX.
- **FET** — a fast FET limiting amplifier: INPUT / OUTPUT, ATTACK and RELEASE 1–7, RATIO 4 / 8 /
  12 / 20 / ALL (all buttons in: aggressive and gritty), MIX.

**Also:** the compressor needles show the real value from the first moment (they used to swing in
from zero); BUS GLUE's meter title no longer touches the needle; the plug-in picker shows nine in
two columns.

# HEM-Elite-Rack 0.2.1

**Looks only (the sound is unchanged):** CONSOLE EQ's section names no longer sit on the GAIN
labels and the filter scale numbers no longer pile up; BUS GLUE's meter title moved off the
scale, the needle scale reads "0" (not "-0"), the ATTACK / RELEASE labels no longer collide;
the chain's UP / DOWN buttons are arrows now (the word DOWN didn't fit).

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
