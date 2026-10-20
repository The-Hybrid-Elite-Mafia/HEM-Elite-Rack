# HEM-Elite-Rack 0.15.4

**Lights without the haze** (the sound is unchanged): everything that lights up stays lit, in the
same colours, but sharp, with no soft glow around it. That covers the teal lines across the plates,
plug-in names, numbers and labels, knob rings and their tips, meters, needles, EQ and spectrum
curves, and the rack app's title, times, meters and progress bars. The hooded ghost logo has no
halo around it either. On SURGEON, the band dot under the mouse now shows a ring instead of a glow.

# HEM-Elite-Rack 0.15.3

**Solid buttons, no haze** (the sound is unchanged):
- A lit button is now plain solid teal, with no glow around it: ON / BYPASS, every ON / OFF switch
  and choice button on the faceplates (L/R, RATIO, SATURATION...), SURGEON's band buttons, and in
  the rack app PLAY, the chain's ON buttons, A / B and MATCH LOUDNESS.
- The selected chain slot and a highlighted plug-in in the ADD list have no glow around them either.

# HEM-Elite-Rack 0.15.2

**HITMAN faceplate, part 2** (the sound is unchanged):
- The two needle meters fit their boxes: the number has its own space at the top, the scale numbers
  no longer run together, and the pivot no longer sits on the CHANNEL 1 / CHANNEL 2 names.
- The dB numbers in the history panel stay readable over a loud input line.

# HEM-Elite-Rack 0.15.1

**HITMAN faceplate fixes** (the sound is unchanged):
- The note under the HITMAN name no longer runs through the L/R and M/S buttons.
- The channel knobs are named once over the columns (INPUT, OUTPUT, ATTACK, RELEASE), with the rows
  marked 1 and 2, so channel 2's names no longer sit on channel 1's values.
- The test suite now also checks that no knob name, value or note on any faceplate runs into a
  control or into other text.

# HEM-Elite-Rack 0.15.0

**New: HITMAN** — a dual FET peak compressor (Dynamics tab, also as VST3 "HEM HITMAN"):
- Two channels, L/R or M/S, with LINK (channel 2 follows channel 1) or set apart.
- INPUT drives it, ATTACK / RELEASE 1–7, RATIO 4 / 8 / 12 / 20 / ALL.
- LOOKAHEAD up to 2 ms; the latency stays the same whatever it is set to.
- Sidechain: EQ (high-pass, bell, low-pass), LISTEN, SC LINK, and in the VST3 an external
  sidechain input (the rack app has no second input, so EXT listens to itself there).
- SATURATION after the compressor: DRY, CLIP (nothing over 0 dBFS), WARM, TRIODE, TAPE. MIX.
- Two gain-reduction needles and a scrolling history of both channels' gain reduction and the input.

**Kept safe:** every plug-in taken out of the rack is in the project's `retired/` folder, with a list
of where each one is, ready to bring back.

# HEM-Elite-Rack 0.14.0

**A trimmed collection: 22 plug-ins**, in the rack and as VST3:
- **EQ (2):** SURGEON, VELVET
- **DYNAMICS (3):** LOCKDOWN (limiter), THE FAMILY (bus glue), ENFORCER (all-round compressor)
- **COLOR (3):** ARCHIVE (tape), FURNACE (tube), SPARK (exciter)
- **SPACE (3):** CATHEDRAL (hall), CHROME (plate), REPEAT OFFENDER (delay)
- **MOVEMENT (6):** THE CREW, GETAWAY, SMOKESCREEN, SHAKEDOWN, CYCLONE, SHAPESHIFTER
- **TOOLS (5):** THE CLEANER, WIDE LOAD, THE ACCOUNTANT, UNDERGROUND, SPOTLIGHT

The rest were taken out of the rack and the VST3 bundle; they are kept in the project's history, so
any of them can come back. A saved chain that used one loads with that slot empty.

# HEM-Elite-Rack 0.13.0

**A tighter EQ line-up: two EQs instead of nine** — 42 plug-ins in all.
- **SURGEON** — the big one: 16 bands, every filter type, stereo / mid / side, dynamic bands,
  character and auto gain.
- **VELVET** — the tube EQ: broad, warm boosts and the low-end boost-and-cut trick.

BOARDROOM, FOUNDRY, KNUCKLES, MIDTOWN, PENTHOUSE, BLUEPRINT and LEVERAGE are gone from the rack
and the VST3 bundle; everything they did, SURGEON can do. A saved chain that used one of them
loads with that slot empty.

# HEM-Elite-Rack 0.12.0

**Every plug-in has its own HEM name now** — all 49, in the rack and as VST3. Nothing borrows a
name from anyone else's gear. The sound is unchanged; only the names on the plates, in the picker
and in your DAW's plug-in list are new.

- **EQ:** SURGEON, BOARDROOM, VELVET, FOUNDRY, KNUCKLES, MIDTOWN, PENTHOUSE, BLUEPRINT, LEVERAGE
- **DYNAMICS:** THE FAMILY, SMOKE, LANTERN, TRIGGER, LOCKDOWN, HUSH MONEY, RAZOR, SUCKER PUNCH,
  ENFORCER, HEAVY HAND, MUSCLE, UNDERBOSS, BOUNCER, TRIAD, CHOKEHOLD
- **COLOR:** ARCHIVE, SPARK, FURNACE, FRONT MAN, MIXTAPE, CRATE DIGGER, SHREDDER, ANVIL
- **SPACE:** CATHEDRAL, BACKROOM, CHROME, COIL, WIRETAP, REPEAT OFFENDER
- **MOVEMENT:** THE CREW, GETAWAY, SMOKESCREEN, SHAKEDOWN, CYCLONE, SHAPESHIFTER
- **TOOLS:** THE CLEANER, WIDE LOAD, THE ACCOUNTANT, UNDERGROUND, SPOTLIGHT

In a DAW they show up as "HEM <name>". A chain you saved with an earlier version won't load its
plug-ins (their names changed); build it again once.

# HEM-Elite-Rack 0.11.1

MUSCLE: the MODERN / VINTAGE buttons are wider so their names fit.

# HEM-Elite-Rack 0.11.0

Five plug-ins renamed and some of their switches relabelled (the sound is unchanged). The small
end labels under every knob now sit inside the knob's own corners, so they can't run into the
value or the next knob.

# HEM-Elite-Rack 0.10.1

Faceplate clean-up only (no change to the sound): the reverb, spring, tape echo and movement
plates no longer have captions running into knob names; the left-side switches on MIXTAPE,
FRONT MAN, WIRETAP and the movement plates sit clear of the notes above them.

# HEM-Elite-Rack 0.10.0

**Nine more — forty-nine plug-ins in all**, in the rack and as VST3. This finishes the planned
collection:
- **Movement:** THE CREW, GETAWAY, SMOKESCREEN, SHAKEDOWN (with auto pan), CYCLONE (horn and drum speed up at
  their own pace), SHAPESHIFTER (envelope or LFO).
- **UNDERGROUND** — a new sub an octave down, or harmonics for small speakers.
- **CHOKEHOLD** — tighten, punch and mono for the low end; at rest it adds back up flat.
- **SPOTLIGHT** — high-pass, body, one-knob compressor, one-knob de-esser, presence, air.

The picker has six tabs now: EQ, DYNAMICS, COLOR, SPACE, MOVEMENT, TOOLS.

# HEM-Elite-Rack 0.9.0

**Space: six reverbs and delays — forty plug-ins in all**, in the rack and as VST3:
- **CATHEDRAL / BACKROOM / CHROME** — an 8-line feedback-delay-network reverb voiced three ways. Measured
  reverb times come out 9–14 % under the DECAY setting (tested: 3.0 s reads 2.63 s, 0.8 s reads
  0.70 s); the left and right tails are uncorrelated (wide).
- **COIL** — two springs with the dispersive "drip".
- **WIRETAP** — three heads, wobble, saturation; feedback up to 110 % stays bounded (tested
  for 10 s).
- **REPEAT OFFENDER** — note values at a BPM (or ms), ping-pong, stereo offset, filtered repeats.
  Repeats land on the exact sample.

The rack has no tempo of its own yet, so REPEAT OFFENDER takes the BPM as a knob.

# HEM-Elite-Rack 0.8.0

**Six new colour plug-ins — thirty-four in all**, in the rack and as VST3. Each shows the spectrum
before and after, so you can see what it adds:
- **FURNACE** — drive, BIAS for even harmonics, tone, mix. Quiet passages stay at the same level.
- **FRONT MAN** — up to +30 dB into a CLEAN, IRON or TUBE stage, 80 Hz low cut, phase invert.
- **MIXTAPE** — TYPE I / II / IV, drive, wow, flutter, hiss, age.
- **CRATE DIGGER** — crackle, surface noise and rumble, wear, warp (33 rpm), mono lows.
- **SHREDDER** — bits and downsampling, mix.
- **ANVIL** — output iron: the lows saturate far more than the highs, weight, nickel / steel.

**Faceplate fixes:** UNDERBOSS recovery buttons fit their text; TRIAD and ENFORCER labels no
longer overlap.

# HEM-Elite-Rack 0.7.0

**Six new dynamics plug-ins — twenty-eight in all**, in the rack and as VST3:
- **ENFORCER** — clean modern compressor: ratio 1–20, knee, attack, release or AUTO, AUTO GAIN,
  sidechain HPF, mix.
- **HEAVY HAND** — classic RMS compressor: threshold, compression up to 60:1, SOFT KNEE; grabs
  faster the harder it's hit, lets go at a steady ~120 dB per second.
- **MUSCLE** — punchy bus compressor: LOW FOCUS keeps the bass from pumping it, knee, MODERN
  (feed-forward) or VINTAGE (feedback) style, stereo link.
- **UNDERBOSS** — Diode-bridge bus compressor with a limiter after it, AUTO recovery,
  iron drive.
- **BOUNCER** — gate or expander with range, hold, hysteresis, detector filters, listen.
- **TRIAD** — three-band compressor; with nothing compressing the bands add back up flat
  (tested: within 0.000 dB from 40 Hz to 12 kHz).

**Not in this round:** a DUCKER (it needs a second input — the rack doesn't route a sidechain
yet), so TRIAD took its place.

# HEM-Elite-Rack 0.6.0

**Six new EQs — twenty-two plug-ins in all**, in the rack and as VST3. Each draws its curve on the
faceplate, computed by the same code the audio runs (tested: curve and sound agree to 0.000 dB):
- **FOUNDRY** — Console channel: 18 dB/oct HIGH-PASS, LOW shelf, MID bell (narrows as you
  push it), 12 kHz HIGH shelf, FRONT MAN drive through iron (odd harmonics).
- **KNUCKLES** — three bands with proportional Q (a +3 dB move is about twice as wide as +12),
  PEAK or SHELF on low and high, FILTER (50 Hz – 15 kHz), DRIVE.
- **MIDTOWN** — tube midrange EQ: LOW PEAK, DIP, HIGH PEAK, tube DRIVE (even harmonics).
- **PENTHOUSE** — clean, broad mastering bands (10 / 40 / 160 / 650 Hz, 2.5 kHz) and an AIR shelf
  (2.5 / 5 / 10 / 20 kHz). Zero latency.
- **BLUEPRINT** — ten bands, 31 Hz – 16 kHz, ±12 dB. Zero latency.
- **LEVERAGE** — darker / brighter around a PIVOT, LOW CUT, HIGH CUT. Zero latency.

**Fixes:** THE ACCOUNTANT RESET is a push button now (no confusing ON / OFF); WIDE LOAD's
correlation label no longer runs into the number; SPARK's display dims the range below
FREQUENCY (where nothing is added) and marks the edge.

# HEM-Elite-Rack 0.5.0

**Six new plug-ins — sixteen in all**, in the rack and as VST3:
- **HUSH MONEY** — tames harsh S / T sounds. SPLIT turns down only the ess band, WIDE the whole
  signal; LISTEN plays what the detector hears; a needle shows how much it takes off.
- **WIDE LOAD** — mono to 200 %, MONO BASS (below 80 / 120 / 160 / 250 Hz the low end stays
  in the centre), BALANCE; CORRELATION and SIDE VS MID readouts.
- **RAZOR** — 4x oversampled peak shaving with a HARD-to-SOFT shape; nothing comes out above
  the CEILING; the needle shows how much the peaks are shaved.
- **SPARK** — new TUBE (even) or TAPE (odd) harmonics above a frequency: brightness and air
  without boosting EQ. The display shows before / after.
- **SUCKER PUNCH** — more or less ATTACK (punch) and SUSTAIN (ring, room), whatever the level.
- **THE ACCOUNTANT** — EBU R128: momentary, short-term and integrated LUFS (gated like the
  standard), loudness range, true peak max, correlation, and how far you are from a target
  (-14 / -16 / -11 / -9). The sound passes through untouched.

**Also:** the plug-in picker has tabs (EQ, DYNAMICS, COLOR, TOOLS) so the growing list stays easy
to browse.

# HEM-Elite-Rack 0.4.0

**New: LOCKDOWN** — the mastering limiter for the end of the chain (in the rack and as VST3):
- INPUT pushes the song louder; nothing ever comes out above the CEILING.
- TRUE PEAK: peaks are found 4x oversampled, so the ones *between* samples stay under the ceiling too
  (those are the ones that clip in MP3 / streaming encoders).
- 1.5 ms look-ahead (the gain is already down when a peak arrives), RELEASE or AUTO, stereo LINK.
- Readouts: the output loudness in LUFS (the same measurement streaming services use) and the true peak.

# HEM-Elite-Rack 0.3.0

**Five new plug-ins** — nine in all, in the rack and as VST3:
- **ARCHIVE** — a two-track tape machine: INPUT drive (highs saturate first, like real tape),
  15 / 30 IPS, HEAD BUMP, WOW & FLUTTER, HISS, AUTO TRIM, a pair of VU meters with VU CAL
  (what reads 0 VU: -18 / -14 / -10 / -6 dBFS, so loud songs don't pin the needles).
- **VELVET** — a passive tube program EQ: LOW boost and atten (together: more weight at the bottom,
  less mud above), HIGH boost with WIDTH, HIGH ATTENUATION, tube DRIVE. Its display shows the curve.
- **SMOKE** — a variable-mu tube compressor: the ratio grows the harder you push, six TIME
  CONSTANTs (5 and 6 release by themselves), STEREO or MID / SIDE, TUBE, MAKEUP, MIX.
- **LANTERN** — an optical leveler: PEAK REDUCTION and GAIN, COMPRESS / LIMIT, a two-stage release
  (fast, then slow), EMPHASIS for the highs, MIX.
- **TRIGGER** — a fast TRIGGER limiting amplifier: INPUT / OUTPUT, ATTACK and RELEASE 1–7, RATIO 4 / 8 /
  12 / 20 / ALL (all buttons in: aggressive and gritty), MIX.

**Also:** the compressor needles show the real value from the first moment (they used to swing in
from zero); THE FAMILY's meter title no longer touches the needle; the plug-in picker shows nine in
two columns.

# HEM-Elite-Rack 0.2.1

**Looks only (the sound is unchanged):** BOARDROOM's section names no longer sit on the GAIN
labels and the filter scale numbers no longer pile up; THE FAMILY's meter title moved off the
scale, the needle scale reads "0" (not "-0"), the ATTACK / RELEASE labels no longer collide;
the chain's UP / DOWN buttons are arrows now (the word DOWN didn't fit).

# HEM-Elite-Rack 0.2.0

**Two new plug-ins** (in the rack and as VST3):
- **BOARDROOM** — a big-console channel EQ: HPF (18 dB/oct) and LPF with IN switches, LF and HF
  (shelf or BELL), LMF and HMF with Q, CLASSIC or MODERN curves (modern: small moves broad, big
  moves narrower), DRIVE for console colour. The display shows the music and the EQ curve.
- **THE FAMILY** — a mix-bus compressor with a gain-reduction needle: THRESHOLD, RATIO 2 / 4 / 10:1,
  ATTACK 0.1–30 ms, RELEASE 0.1–1.2 s or AUTO, MAKEUP, MIX (parallel), SIDECHAIN HPF so the low
  end doesn't pump the mix. Stereo-linked, zero latency.

**Fixed:** the chain's DOWN button showed "D...". The plug-in picker fits four plug-ins.

# HEM-Elite-Rack 0.1.1

**Fixed (looks only, the sound is unchanged):** labels that ran into each other on the rack
screen and on SURGEON — slot names under the UP / DOWN buttons, DYNAMIC over THRESHOLD, the
cut-off "12 dB" graph button, the crowded A / B and MATCH LOUDNESS buttons, the meter line.
Text that is still too long for its spot now ends in "..." instead of running over.

# HEM-Elite-Rack 0.1.0 — first release

Hybrid Elite Mafia's plug-in rack: load a song, stack HEM plug-ins, hear them live, compare
with the original at the same loudness, export a WAV. The plug-ins also come as VST3 for any DAW.

**Download:** `HEM-Elite-Rack-Setup-0.1.0.exe` below. Windows 10 / 11 64-bit, a processor with
AVX2 (Intel 2013+, AMD 2015+), a graphics card with OpenGL 3.3.

## What's in it
- **SURGEON** — 16 bands (bell, shelves, cuts 6–48 dB/oct, notch, tilt), each on stereo, mid,
  side, left or right; dynamic bands that only move when that part of the song pokes out;
  CLEAN / CONSOLE / TUBE character; AUTO GAIN. Draw on the graph: double-click adds a band,
  drag moves it, the wheel sets its width.
- **THE CLEANER** — learns the hiss / noise by itself and takes it out while keeping the air and
  the attacks. NOISE REDUCTION, FOCUS, AIR PRESERVE, FREQUENCY RANGE, ADAPT, LISTEN.
- **The rack app** — up to 8 plug-ins in a chain (ON / OFF, reorder, remove), A / B with
  MATCH LOUDNESS, loop, output meter with LUFS, export 24-bit / 48 kHz WAV, remembers your chain.
- **VST3** — HEM SURGEON and HEM THE CLEANER with the same faceplates (live spectrum in the DAW).

## Good to know
- Sound goes out through the sound card in Windows **exclusive mode** at 48 kHz (lowest
  latency): other programs go quiet while the rack plays. OUTPUT (top right) picks the card.
- No ASIO in this build (public, shareable).
- Settings stay in `%APPDATA%\HEM-Elite-Rack` across upgrades and uninstalls.
