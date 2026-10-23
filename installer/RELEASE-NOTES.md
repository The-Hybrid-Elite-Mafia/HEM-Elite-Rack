# HEM-Elite-Rack 0.50.0

- **POWER button, like a DAW** — the round button on every plate is now POWER: lit (ON) = the plug-in is working on the
  sound, dark (OFF) = bypassed, you hear the sound untouched. It used to say BYPASS while it was off, which read backwards.
- **Screens and needles go dark when a plug-in is off** — they show OFF instead of the sound you are not hearing.
- **Knob drags never run out of room** — when the mouse reaches the edge of the screen mid-drag it jumps to the other side
  and the knob keeps going, so every knob can reach its top and bottom (e.g. THE MIDDLEMAN ALL, -24 to +6 dB).

# HEM-Elite-Rack 0.49.0

- **Meters checked against a real song** — Steven's "Built from the Static" was played through all 120 plug-ins and every
  meter was compared with an independent measurement of the same audio (peak, VU, true peak, loudness, correlation,
  gain reduction). Fixed what it found:
  - STRAPPED, STREET TEAM, THE INFORMANT, DEAD DROP, HOT WIRE and BOTTOM FEEDER peak meters fell at a speed that depended on
    the host's buffer size; now they hold 1.5 s then fall 20 dB/s at any buffer size.
  - THE MIDDLEMAN CENTER readout smoothing no longer depends on the buffer size (30 ms).
  - GRAND LARCENY OUT meters now read the real final output (they missed the dry signal).
  - LOCKDOWN short-term loudness is now an exact 3-second window.

# HEM-Elite-Rack 0.48.0

- **Controls start at zero where zero means "not doing anything yet"** — HITMAN INPUT / OUTPUT 1 and 2 (were +6 / -4 dB),
  HAYMAKER RANGE and FOUR KINGS BAND 1-4 RANGE (were +6 dB), SURGEON DRIVE (was 30 %) and VELVET TUBE DRIVE (was 20 %) now
  start at 0. Every other control was checked (all 2,376): the rest already start at zero, centred, or where they have to
  be to work (thresholds, ceilings, MIX 100 %, WIDTH 100 %, volumes, times). Saved sessions keep their own settings.
- **SURGEON test:** all 16 bands dragged left, right, up and down (64 drags) - each band moves only itself; no other band's
  settings or line change, also for two bands right next to each other.

# HEM-Elite-Rack 0.47.0

- **No more black holes on switched-off knobs** — a knob whose section is turned off (DYNAMIC OFF, a band set to a cut, a
  release in HOST mode, and so on) now stays a whole knob with its pointer and value showing; only its lights go out (scale
  dark, name and value dimmed). The section's own switch light says whether it is on.

# HEM-Elite-Rack 0.46.0

- **New faceplates on all 120 plug-ins** — every plug-in now wears its own rendered 3D faceplate (brushed gunmetal, machined
  wells, slots and screen windows), made to the plug-in's exact layout. The live knobs, buttons, screens, meters and words
  sit in their pockets on top, working as before; the real crown and ghost are laid on each plate. Same in the rack app
  and in the VST3 plug-ins.
- **Fix:** the rack app could show the previous plug-in's plate under the next one's controls when both use the same kind of
  plate (the plate is now kept per plug-in).

# HEM-Elite-Rack 0.45.0

- **Every control has its own pocket in the plate** — some buttons sat flat on the metal with no slot cut for them (SURGEON's
  AUTO GAIN and its 16 band buttons, and others like them). Now every knob, button, chooser, keyboard and screen on all 120
  plates sits in its own seat, slot or window, and a test checks it on every plate.

# HEM-Elite-Rack 0.44.0

- **SURGEON: every band its own line** — each band now has its own colour (its dot, its band button and its own thin line on
  the graph, showing what that band does by itself). The bright teal line is still all the bands added together, so you
  can see which band is doing what where they overlap. A band set to MID / SIDE / LEFT / RIGHT shows M, S, L or R beside
  its dot (the colours used to mean that).

# HEM-Elite-Rack 0.43.0

- **Close a plate without removing the plug-in** — X CLOSE above every plate's top-right corner, or the ESC key. The plug-in
  keeps running in the chain with its settings; click its name in the CHAIN to open the plate again. With no plate open the
  middle shows the empty screen. ESC closes the plug-in picker too (it has an X CLOSE button as well).

# HEM-Elite-Rack 0.42.0

Label check, part two (names and menu order only; no sound changes):
- **TORCH** — amp types renamed to our own: STRAIGHT IN, PURE, SPARKLE, BITE, PUSH, GRIT, STACK, FIRE, LEAD, VENOM. The 4x12
  cabinets are now 4X12 OLD and 4X12 NEW (also on HOT WIRE).
- **WISEGUYS** — MODE: ALL ROUND, STEADY SINGER, LIVELY SINGER, BIG VIBRATO, STEADY PLAYER, PURE TONES, LOW END. LINK: OFF,
  SPEECH, SINGING, HORNS, REEDS, BOWED, FULL, OPPOSITE.
- **TOP DOG / QUICK CASH / SWEET SIXTEEN** — the last three profiles now come in the order of the original's menu: STRIPPED,
  QUIET LIFT, HEAVY.

# HEM-Elite-Rack 0.41.0

Label check (names only; no sound changes):
- **THE CHAIRMAN** — the British-console channel's new name (its old name borrowed another company's trademark). Same plug-in,
  same settings, same VST3 IDs.
- **CHAUFFEUR** — now described as a bass level rider.
- **MADE MAN** — NOTE TRANSITION is now NOTE GLIDE.
- **THE AUDITOR** — METHOD is now EBU / AVERAGE / DIALOGUE.

# HEM-Elite-Rack 0.40.0

- **THE MOLD** and **THE FORGER** (new) — the EQ cloner, with their originals' full control sets. THE MOLD measures a hardware EQ in a
  loop through your interface (send out, return in, headroom, transmit, clip lights, the loop's delay); THE FORGER plays that curve
  on any track: capture (follows the box live), hold (saved with the session), add, gain, display scale, clear, trim readout.
- The rack app now opens up to 16 outputs on an ASIO interface (the first two play the rack; THE MOLD can send to the others).

# HEM-Elite-Rack 0.39.0

- **LIVE INPUT** — the rack takes a mic or a guitar from an ASIO interface's inputs (a stereo pair or one input) through the chain,
  with the song playing alongside untouched; MONITOR on / off; an input level meter.
- **RECORD** — takes saved as 24-bit WAV in Documents\HEM-Elite-Rack Takes, twice: through the chain and dry. Named with where in the
  song they start and lined up with the song (the sound card's round trip and the chain's delay taken out).

# HEM-Elite-Rack 0.38.0

- **GRAND LARCENY** (new, its own plate) — electric grand piano with its original's full control set: velocity curve, formant,
  tune; main, key up, action, pedal sound, squeeze, sympathetic strings; bass / mid / treble; volume and limiter; tremolo, auto
  pan and phaser (rate synced or free), chorus, reverb. The first instrument in the rack.
- **MIDI keyboards** — the rack app now hears every MIDI keyboard plugged into the PC (picked up within a few seconds, also when
  plugged in later) and plays the instruments in the chain; the sustain pedal works. As a VST3, GRAND LARCENY is an instrument
  with a MIDI input; the sustain pedal (controller 64) moves its PEDAL.
- New **KEYS** tab in the plug-in picker.

# HEM-Elite-Rack 0.37.0

- **FINGERPRINT** (new) — convolution reverb with its original's full control set: space (sixteen impulse responses we make
  ourselves: booth, rooms, chambers, halls, church, plates, springs, garage; the original loads recordings), start, length,
  decay, mix, direct, pre-delay, dry delay, CPU full / low, output, crosstalk, and an RT60 readout. A new space is made in the
  background and faded in, so the sound never stops. Latency 512 samples.

# HEM-Elite-Rack 0.36.0

- **THE RACKET** rebuilt (its own plate) — now the whole pedalboard, one pedal at a time: 26 pedals, each with every control its
  original has. Drives: overdrive, hard clip, fuzz, rasp, heavy. Movement: flange, vib / trem, auto pan, phase, oct down, wah, chorus,
  thicken, pitch ride, bass ride. Space: echo, flip echo (reverse), room, spring. Basics: squeeze, slam, gate, clean up, tone stack,
  graphic, volume. Pick a pedal and only its controls show. Input stereo / mono / mono > wide and input gain. For a chain, stack
  several RACKETs. Wave 4 is complete.

# HEM-Elite-Rack 0.35.0

- **STAKEOUT** (new, its own plate) — analyzer with its original's full control set: frequency bands (LF resolution 40 / 20 / 10 Hz,
  peak or RMS, response, L / R or sum, weighting A / B / C, freeze, max line with clear), the stereo position display, and peak
  L / R and sum meters (peak or RMS, weighted) with holds and clip lights. Wave 5 is complete.

# HEM-Elite-Rack 0.34.0

**Wave 5 — pitch and meters.** Rebuilt to their originals' full control sets:
- **WISEGUYS** — six voices (on, pitch, formant, gain, pan, delay), the direct voice (on, gain, delay), animate, link, mode,
  min / max pitch. New voice engine (PSOLA): a voice's formants stay put unless you move them.
- **MADE MAN** — root, twenty scales or your own notes, speed, note glide, ratio, formant corrected / off, shift (octave
  drops), reference A, min / max pitch. Same new engine.
- **PROMOTION** — transpose (intervals and cents) or frequency (from / to Hz), modes sync / smooth / transient / punchy. (The
  original's time stretching works on whole files, not a live stream.)
- **THE DETECTIVE** — key, two alternatives, the song's tuning; detect / stop, your own key, sharps or flats.
- **THE WITNESS** — VU meters with headroom, display delay, clip lights.
- **LIVE WIRE** — pink, white or sine; frequency and gain with quick settings; send L / R / both; flip.
- **THE AUDITOR** (new, its own plate) — loudness meter: method, channels, six weightings, short time, overs / unders, true
  peak max, target and trim, scale, pre-filters, gain, true-peak limiter.

# HEM-Elite-Rack 0.33.0

**Wave 4 — colour, tape, drums and guitar.** Each built to the full control set of one original:
- **HEAVY HITTER** (new) — drum enhancer: drum, sensitivity, thump, treble, snap, output, meter in / out.
- **REEL MONEY** (new) — tube tape machine: speed, monitor, bias, into / off tape with link, tape level, wobble, hiss, the tape
  echo (time, slap or repeats, level, tone), VU with its zero.
- **PAWN SHOP** (new, its own plate) — lo-fi box: device (sound, tone, squash, ringer, decade, mix), space (echo with sync / note /
  BPM / ping pong / feedback, spring or plate reverb, mix), noise (twelve of our own, duck or gate, threshold, pre / post, level),
  mechanics (wow, wobble, speed for A and B), master (input, output, low cut, high cut, mono). The original plays 64 recorded
  noises; ours are twelve we generate.
- **HOTHEAD** (new) — one-knob overdrive (the original's guide names no other control).
- **HOT WIRE** (new) — guitar head: five amps, three channels, focus, gate and speed, the tone stack, depth, presence, six cabinet
  pairs with cab tone, room, cab off, an output limiter with readouts. (The original's preset locks belong to its own preset
  system, not to a rack.)
- **BOTTOM FEEDER** (new) — bass rig: two amps, DI tone / level / blend / flip, a drive pedal, voice, five-knob EQ, cabinet tone,
  room, cab off, output limiter.
- **TORCH** rebuilt — ten amp types, drive, the tone stack, presence, master, input, output; two cabinet paths (eight cabinets,
  six mic placements, volume, pan, phase on B), dual or stereo routing. Its old GATE and ROOM are gone (the original has neither).
- **THE SNITCH** rebuilt — tuning modes (chromatic, guitar, drop D, open G / D / E, DADGAD, half down, your own notes), reference
  A ±6 cents, twelve note switches. Its old MUTE is gone (the original has none).

# HEM-Elite-Rack 0.32.0

**The eight retired plug-ins are back** (with their old IDs, so old sessions find them):
- **FOUNDRY** — now the full British console channel EQ: stepped low cut, lows, middle and highs at the console's own
  frequencies (±18 dB), EQ in / out, flip, vintage colour, output, and peak / to-top / clip readouts with clear.
- **BOARDROOM** — now the full four-band console EQ: stepped high- and low-pass, lows and highs as shelf or bell, low and high
  mids wide or tight, EQ in / out, flip, vintage, output, readouts.
- **MIDTOWN** — now the full tube midrange EQ: low peak, dip and high peak at stepped frequencies, EQ in / out, hum, output.
- **COIL** — now the full spring reverb: drive, headroom, pre-delay, seven tanks, short / medium / long, treble and bass on the
  springs, mix, output, stereo or mono.
- **BACKROOM**, **MIXTAPE**, **CRATE DIGGER**, **SHREDDER** — back as they were (our own designs; the cassette's drive stage
  no longer dulls the top octave).
- On FOUNDRY and BOARDROOM the original's one-click "set the output so the peak hits the top" can't save with a session in
  the rack, so the TO TOP window shows how far to turn OUTPUT instead.

**Fixed:** THE CHAIRMAN, GOLD CHAIN and STRAPPED lost about 4 dB at 20 kHz even with nothing turned (their colour stage ran the
whole sound through the oversampler's filters). Now only the added harmonics go through it: flat to 20 kHz.

# HEM-Elite-Rack 0.31.0

- **SPEAKEASY** (new) — a real room around the sound, every control: room size, distance, room shape, real room (levels and
  pre-delay follow the room and the distance), balance, decay, pre-delay, density, tail / early dulling above a frequency, low and
  high decay with ratios, early / tail variations, input, and direct / early / reverb each switchable with its own level.
- **BIG HOUSE** (new) — reverb with twelve kinds of space and every control: variation, pre-delay either way, time, size,
  diffusion, decay shape, early and reverb levels, low and high damping with ratios, tail EQ, mix, gain.
- **MOUTHPIECE** (new) — vocal leveler in one move: gate, press, ceiling, with total / output / input readouts.
- **SLINGER** (new) — guitar compressor: threshold, attack, gain into a limiter, with input / attenuation / output / limiter readouts.
- **STREET TEAM** (new) — channel strip: input, four-band EQ, gate with floor, compressor / expander, output.
- Each control label on these five was checked against the original's wording and given our own words.

# HEM-Elite-Rack 0.30.0

- **COLD CASE** (new) — swept notches and a ringing loop, every control on its own plate: notches, depth, flip, stereo, low cut;
  the sweep's run, wave, rate, multiplier, tempo and range; the loop's decay, flip, delay, density, hi tame and filter; high cut,
  width, rotate, pre-delay, wet mode, mix, gain. (Its ranges are our own: the original's manual does not give them.)
- **SWERVE** (new) — flanger with every control: delay, depth, rate (or sweeps per beat), sine / triangle, tie, hold,
  stereo, feedback with loop flip, mix with wet flip, filter, tape through-zero, gain, peaks. (Its filter is an ordinary one, with
  a little phase shift, where the original's has none.)
- **THE INFORMANT** — rebuilt with every control: a 48-key keyboard on its plate (eight voices, HOLD or LATCH), ten
  synth sounds, TUNE, SPREAD; carrier and modulator sources; noise with its filter; SQUEEZE, FORMANT, LAG, HOLD;
  a five-band EQ on the vocoded sound; a mixer with CARRIER, MODULATOR, NOISE and VOCODED levels, SOLO and MUTE.
  Not possible in the rack: playing it from MIDI, and a sidechain carrier or modulator.

**Our own words on every control:** every rebuilt plug-in's labels were checked word for word against the original's
manual; the ones that copied its wording are renamed to our own words that mean the same thing. Among them: LET-GO EASE OUT /
SNAP BACK, STACKED ratios, ERA, HUM (the old ANALOG mains hum), VINTAGE, SQUEEZE, BRIGHT DETECT, TIMING, SNAP, PICKUP,
LENGTH, SAFETY, CURVE, QUIET UP / LOUD DOWN, EVENER, STAGE + BLEED FLOOR, VOICE / NOTE PICKUP, LOWEST / HIGHEST RIDE,
REST POINT, BIT DEPTH, NOISE SHAPE, PEAK MODE, WEIGHT, BAND FREEDOM, RELEASE FEEL, BAND ONLY / WHOLE, CATCH WIDTH,
SOFT START / END, ROOM FLOOR, SMART REL, AUTO REL, PLACE, BUMP shelves, VIEW RANGE, VU ZERO, AMBIENCE, VOICING, SLAP,
THICKEN, DRY, AIM, COMBS, SWEEP FOCUS / SPACING, FINE SHIFT, NEW OCTAVE, LOWS, LINE UP, OCTAVE DOWN, TWO DOWN, FOLLOW,
REAL BASS, OVERTONES, SPLIT TIMES, LO-RES, TIE FILTERS, LOCK, WOBBLE, PASS TIME, CLOSEST AT, AIR LOSS, LOOP / ONCE,
IN TIME, FEED, MULTIPLIER, LEVEL / PITCH / SWING, CLEAN TONE / EDGE / HIGH GAIN, STYLE, ROOM, SPEAKER ANGLE, HEAD TURN,
HEAD SIZE, EAR TO EAR. Only the words changed: the sound is the same and saved settings keep working.

# HEM-Elite-Rack 0.29.0

**ASIO is in, and the rack is now free software (GPLv3)**
- The OUTPUT button now lists your interface's own **ASIO** drivers ("ASIO: ..."), first. With nothing picked yet the
  rack tries ASIO first, then Windows' default output. Steinberg's ASIO SDK is used under its GPLv3 option, so
  HEM-Elite-Rack is now licensed GPLv3 too: free to use, share and change; anyone who shares a changed copy must share
  its code. The complete source of this build is attached to this release.
- Not yet tested on a real ASIO interface: the delay you get depends on your driver's buffer size (set in its own
  control panel). If your ASIO driver won't open, the message says so and the OUTPUT button moves to the next output.

**Space and modulation, wave 3 part 1** — one plug-in per original, every control:
- **THE FIXER** — rebuilt: three dynamic bands (mud, harsh, fizz), each AMOUNT, FREQ, LISTEN, BYPASS; INPUT, OUTPUT
- **DEAD DROP** (new) — hybrid delay: time or note, tap, dual, feedback past 100 %, ping pong, phase, filters with link,
  wobble, analog, lo-fi, mix, output
- **SIX SHOOTER** — rebuilt: six taps with their own time, gain, position and EQ; direct; tempo, grid and snap;
  modulator; feedback with its own delay and filter
- **BACK ALLEY** — rebuilt: echo (time / note, feedback, ping pong, level) and spring or plate reverb (length, level),
  hi-pass, lo-pass, mono, mix
- **DRIVE-BY** — rebuilt: a pass along a track with track time, center time, start, end, distance, direction,
  continuous or one-shot (on the sound or the FIRE button); gain, pan, pitch, air and reverb amounts
- **BODY DOUBLE** — rebuilt: four voices (gain, pan, delay, detune, feedback, octave, wobble), direct, align, routing,
  low and high shelves
- **HUSTLE** — rebuilt: one LFO for level, pitch and position, each with depth, on and phase; rate x factor, five waves
- **STRAPPED** — rebuilt: guitar desk with CLEAN / CRUNCH / HEAVY, re-amp, sensitivity, balance, output and six faders
  with a colour each (bass, treble, compress, reverb, delay, pitch)
- **SAFE HOUSE** — rebuilt: AMOUNT of room, FRONT spread, ROTATE your head, HEAD CIRCUMFERENCE, INTER-AURAL ARC,
  MUTE L / R, OUTPUT

Not possible in the rack (said plainly): setting SIX SHOOTER's taps by tapping a rhythm; SAFE HOUSE's head tracking
(it needs a camera or sensor) and headphone models.

# HEM-Elite-Rack 0.28.0

**Filters and VELVET, the end of wave 2** — one plug-in per original, every control:
- **CUTTHROAT** — rebuilt: HIGH-PASS and LOW-PASS (18 dB per octave), each with ON, a left and a right set, LINK
- **TUNNEL VISION** (new) — the one-knob filter: FILTER 0-10, RESONANCE in four steps
- **THE SETUP** (new) — the filter with movers: six types, slope, spread, smooth, drive, crush; LFO, 16-step sequencer
  and follower with depths on frequency, resonance and delay time; a delay with right ratio, feedback, style and route
- **VELVET** — added EQ IN / OUT and ANALOG hum (your saved settings keep working)

Not possible in the rack (said plainly): THE SETUP's MIDI keyboard tracking and outside sidechain.
EQ and filter glides are finer, so a fast move doesn't ring. Readout numbers on narrow windows leave the name room.

# HEM-Elite-Rack 0.27.0

**Bass and stereo, wave 2 part 3** — one plug-in per original, every control:
- **HIGH ROLLER** — rebuilt as a five-band parallel enhancer: per band GAIN, WIDTH, SOLO, BYPASS; four movable
  crossovers; INPUT, OUTPUT, DIRECT, PUNCH
- **THE MIDDLEMAN** — CENTER and SIDES faders, LOW / HIGH / PUNCH (0 = follow the center, 100 = the sides), MASTER,
  center meter and output peaks
- **DOUBLE CROSS** — rebuilt: frequency-swept spread (SPREAD, SPREAD 10, SPLIT), FREQ, LF SPREAD, SWEEPS, F CENTER,
  F DENSITY, TWEAK, SHAPE; INPUT, WIDTH, ROTATION, POLARITY, SWAP; held peaks with RESET. Still folds back to mono exactly
- **WIDE LOAD** — added SHUFFLE FREQ, BASS TRIM, INPUT M / S, POLARITY L / R, SWAP and an L / R or M / S meter
  (your saved settings keep working)
- **LOW RIDER** (new) — a new low octave: RANGE, LOW AIR, LO, DIRECT, OUTPUT, TRIM, ALIGN
- **UNDERTOW** (new) — subs one and two octaves down: range, levels, drive, dynamics, mono sub, mix, output
- **DEEP POCKETS** (new) — bass harmonics for small speakers: frequency, input, original bass, harmonics, high-pass,
  decay, ratio, response, monitor
- **LOW PROFILE** (new) — the one-move bass harmonics: frequency, original bass in / out, intensity, gain

UNDERGROUND stays as it was (our own design).
INSIDE MAN's captions are shorter so nothing touches.

# HEM-Elite-Rack 0.26.0

**Channel strips, drums and tone, wave 2 part 2** — one plug-in per original, every control:
- **THE CHAIRMAN** — rebuilt as a British-console channel: MIC / LINE, PREAMP 20-70, INPUT, stepped HIGH shelf, MID
  boost / cut at eight frequencies, LOW shelf at four, LOW CUT at 50 Hz, EQ in / out, ANALOG hum, NOISE, PHASE,
  OUTPUT, TRIM, VU meter IN / OUT and VU calibration
- **INSIDE MAN** (new) — channel strip: gate / expander and compressor, each with its own key filter, four-band
  EQ, order, phase, output, rotate, clip guard (the gate and compressor of THE CHAIRMAN's old version live on here)
- **KINGPIN** — rebuilt as a drum channel: TYPE (kick, snare, hats, toms, overhead, room), SENS, COMPRESS, GATE,
  FX, BASS, TREBLE, OUTPUT, VU meter, ANALOG
- **GOLD CHAIN** — input and output now ±18, MIX made way for ANALOG
- **IVORIES** — one PIANO knob, DELAY and DOUBLER each with a level, INPUT, OUTPUT, ANALOG

Not possible in the rack (said plainly): INSIDE MAN's sidechain from another track.
EQ screens: the dB numbers no longer crowd on short screens; ten-band type buttons give their arrows'
room to the words; the EQs' bottom row spreads out.

# HEM-Elite-Rack 0.25.0

**The EQs, wave 2 part 1** — one plug-in per original, every control:
- **STRAIGHT SHOOTER** — rebuilt as a six-band linear-phase EQ: per band ON, TYPE, FREQ, GAIN (±30), Q; bells,
  precision (variable-slope) cuts and shelves, resonant cuts and shelves; GAIN, TRIM, FILTER, DITHER, SCALE
- **BOTTOM LINE** (new) — the three-band low-end linear-phase EQ (11–600 Hz), same shapes and globals
- **TEN TOES** (new) — ten-band EQ with separate left and right sets, LINK, input / phase / output per side, TRIM
- **FOUR CORNERS** (new) — four-filter EQ with separate left and right sets, resonant shelves, GAIN L / R
- **SMOOTH TALKER** (new) — six-band EQ with separate left and right sets, SPECTRUM switch, OUTPUT, TRIM

STRAIGHT SHOOTER's old settings don't carry over (it never shipped in a version you installed).
EQ plates now show the curve on top, the bands under it and the globals at the bottom; the
left / right EQs have LEFT / RIGHT tabs, and the screen draws the right side in gold when it differs.

# HEM-Elite-Rack 0.24.0

**Transients, riders and de-essers, rebuilt with every control** — one plug-in per original:
- **KNOCKOUT** — the transient designer, wide (its 4-band mode is now its own plug-in)
- **HAYMAKER** (new) — transient shaper: range, sensitivity, duration, release, one-push trim
- **FOUR KINGS** (new) — the same in four bands, each with on, range and sensitivity, plus a master
- **BACKBONE** (new) — two faders: lift the quiet parts, hold the loud ones
- **FULL HOUSE** — leveler (loud / soft), gate, low level (threshold + gain), high level (threshold + ceiling), each switchable
- **WHEELMAN** — vocal rider: target, vocal sensitivity, attack, range min / max, idle, stage bleed floor
- **CHAUFFEUR** (new) — bass rider: note detect, response, ignore bleed, ignore clicks, range, idle
- **SILENCER** — the classic de-esser: threshold, frequency, split / wide, high-pass / band-pass, monitor
- **TIGHT LIPS** (new) — de-esser with a range control and listen
- **OMERTA** (new) — smart de-esser that hears esses at any pitch, wide-to-split blend, look-ahead
- **BREATHLESS** — reduction all the way to silence, and ROOM TONE
- **TOMMY GUN** — AMOUNT on a 0-10 dial

Not possible in the rack (said plainly): the vocal rider's music sidechain and automation writing.

Multiband plates now use a grid: one row per band control, named once on the left.

# HEM-Elite-Rack 0.23.0

**The last three compressors, rebuilt with every control:**
- **SWITCHBLADE** — now the full channel compander: compressor / expander with LEVEL REF, ratio 0.5-50 or
  CANCEL, PDR; gate or expander with FLOOR, SLOPE, HOLD; each one WIDE, SIDECHAIN or SPLIT through the
  filter; LINK, LOOKAHEAD, KEY L > R / R > L, MONITOR, separate INPUT L / R. New plate.
- **TERRITORY** — four bands, each with THRESHOLD, GAIN, RANGE, ATTACK, RELEASE, SOLO, BYPASS; a MASTER
  column; crossovers with Q; AUTO / MANUAL release, EASE OUT / SNAP BACK, KNEE. (Its old linear-phase mode is
  now its own plug-in.)
- **STRAIGHT EDGE** (new) — five linear-phase bands with the same controls, plus ADAPTIVE, AUTO MAKEUP,
  one-push TRIM and DITHER.

# HEM-Elite-Rack 0.22.0

**The hardware-style compressors, rebuilt with every control** — one plug-in per original:
- **CONSIGLIERE** — light-cell leveler, now with AUTO MAKEUP, HI FREQ, ANALOG, TRIM and an IN / GR / OUT meter
- **STONE COLD** (new) — the solid-state light-cell leveler (its own plug-in now, not a CONSIGLIERE mode)
- **HEAVYWEIGHT** — release in ms or on the host / BPM tempo with a NOTE, four ANALOG flavours, limiter or clipper, meter
- **THE DON** — stepped THRESHOLD, RATIO and DECAY like the hardware, VU meter and VU CALIBRATE, ANALOG
- **SLOW BURN** — INPUT, THRESHOLD 0-10, six time constants, SEPARATE / LINKED / MID-SIDE, MAINS, COMPRESS on / off
- **LONE WOLF** (new) — one channel of SLOW BURN's circuit
- **IRON FIST** — COMPRESS, RELEASE, DE-ESS switch, LIMITER with LEVEL / ATTACK / RELEASE, ANALOG, AUTO MAKEUP, MIX, TRIM
- **QUICK DRAW** (new) — fast transistor limiting amp: attack / release 1-7, 4 / 8 / 12 / 20 / STACKED, early or late era
- **OLD MONEY** (new) — smooth all-purpose compressor: auto / manual release, snap back / ease out, warm / smooth

The needle on these plates can show the input or output VU level (METER IN / OUT) as well as the gain
reduction. ANALOG hum follows the music, so silence stays silent.

Also: the maximizer plates got roomier band columns, and TOP DOG / QUICK CASH show input and output peaks.

# HEM-Elite-Rack 0.21.0

**The maximizers, rebuilt: seven plug-ins, each with its full set of controls** (in the rack and as VST3):
- **BIG SHOT** — one band: threshold, ceiling, auto release
- **LAST WORD** (new) — one band: input left / right with link, release, auto release, and a ceiling
  that holds the samples (DIGITAL), the peaks between them (TRUE PEAK) or rounds into it (ANALOG)
- **HIGH STAKES** (new) — five bands, linear phase: crossovers, separation, release type, and per band
  gain, priority, link, release and solo
- **TOP DOG** (new) — the five-band limiter run from nine profiles
- **SWEET SIXTEEN** (new) — sixteen bands with a six-band EQ / priority curve and 22 release characters
- **FAST MONEY** and **QUICK CASH** (new) — HIGH STAKES and TOP DOG with a 2 ms band split

All seven end with the same word length / dither (TYPE 1, TYPE 2) / noise shaping (MODERATE, NORMAL,
ULTRA) section. BIG SHOT's old BANDS / PRIORITY / STYLE controls are gone: those jobs now belong to
HIGH STAKES and SWEET SIXTEEN, which do them in full.

# HEM-Elite-Rack 0.20.0

**WIDE LOAD gets three new knobs**: SHUFFLE (wider lows and low mids), ROTATION (turn the whole
stereo picture left or right) and ASYMMETRY (slide the middle over without moving the edges). At
their defaults it sounds exactly as before, and saved sessions open as they were.

Small fixes: WISEGUYS' voices and PROMOTION's SHIFT move in whole semitones (FINE still does cents),
so harmonies always land in tune; MADE MAN's first scale reads ALL NOTES; LIVE WIRE's switch reads TONE.

# HEM-Elite-Rack 0.19.0

**Nine new plug-ins: guitar, pitch, meters and test tones** (in the rack and as VST3), our own designs:
- **TORCH** — guitar / bass amp with cabinet
- **THE RACKET** — drive pedal: overdrive, distortion, fuzz, octave fuzz
- **THE SNITCH** — tuner
- **PROMOTION** — pitch shifter
- **WISEGUYS** — three-voice harmonizer
- **MADE MAN** — pitch correction for one voice
- **THE DETECTIVE** — finds the song's key
- **THE WITNESS** — VU and peak meters
- **LIVE WIRE** — test tones and sweeps (off until you switch it on)

Better pitch shifting everywhere it's used (BODY DOUBLE too): each splice now lands where the sound
lines up, so held notes stay steady instead of warbling. Small fixes: HUSTLE's wave buttons read
SINE / TRI / SQUARE / RISE / FALL, the first NOTE choice on SIX SHOOTER is MS and on HUSTLE HZ, and
"BAR" replaces "1 BAR" on the note buttons so every word fits its button.

# HEM-Elite-Rack 0.18.0

**Nine new plug-ins: space, modulation, voice and monitoring** (in the rack and as VST3), our own designs:
- **THE FIXER** — three dynamic cuts for mud, harshness and fizz
- **SIX SHOOTER** — rhythmic multi-tap delay (1 to 6 shots, swing, spread)
- **BACK ALLEY** — lo-fi spring / plate and worn tape echo
- **DRIVE-BY** — a sound passing by, pitch bending as it goes
- **BODY DOUBLE** — 2 or 4 voice doubler
- **HUSTLE** — tremolo, vibrato and pan from one LFO
- **THE INFORMANT** — vocoder with its own synth
- **STRAPPED** — guitar mix strip
- **SAFE HOUSE** — hear speakers in a room on headphones (for listening only)

Small fixes: STRAIGHT SHOOTER's top row reads LOW / MID 1 / MID 2 / MID 3 / HIGH; CUTTHROAT's SWEEP,
THE MIDDLEMAN's balances and IVORIES' TONE show as percentages.

# HEM-Elite-Rack 0.17.0

**Nine new plug-ins: EQ, channel strips, tone and stereo** (in the rack and as VST3), our own designs:
- **STRAIGHT SHOOTER** — linear-phase EQ (no phase shift; 96 ms delay that DAWs make up for)
- **CUTTHROAT** — sweep filter: high-pass + low-pass, one-knob sweep, resonance, drive
- **THE CHAIRMAN** — channel strip: gate, compressor, four-band EQ, low cut, limiter
- **KINGPIN** — drum channel: gate, thump, snap, punch, crush, drive
- **GOLD CHAIN** — one-knob tone: warmth, density and sheen together
- **IVORIES** — piano tone: tone, body, compression, slap delay, doubler, width
- **HIGH ROLLER** — five-band enhancer with lift and width per band
- **THE MIDDLEMAN** — center / sides balance with low, high and punch steering
- **DOUBLE CROSS** — mono to stereo that folds back to mono perfectly

The EQ plates (STRAIGHT SHOOTER, CUTTHROAT) draw their curve over the music.

# HEM-Elite-Rack 0.16.1

**Tidier new plates** (the sound is unchanged):
- Button words never spill out of their buttons: plates with many switches put at most two button
  groups per row, and a few names got shorter (SWITCHBLADE's filter: OFF / LO CUT / HI CUT / BAND /
  NOTCH; KNOCKOUT's sustain shape: EVEN / CURVED / SOFT).
- TERRITORY's band knobs are a little smaller so the rows breathe.
- Small number boxes show the unit under the number, and no screen ever shows "-0.0".
- The history screens' labels match what they draw.

# HEM-Elite-Rack 0.16.0

**Fourteen new dynamics plug-ins** (in the rack and as VST3), our own designs:
- **SWITCHBLADE** — channel compressor + gate + steering filter
- **TERRITORY** — 4- or 5-band dynamics, each band down or up; natural or linear phase
- **CONSIGLIERE** — two-knob leveler, smooth light-cell or firmer solid-state
- **HEAVYWEIGHT** — punch compressor with an analog stage and release on the beat
- **THE DON** — old-school broadcast bus compressor that breathes with the music
- **SLOW BURN** — variable-mu tube limiter
- **IRON FIST** — console channel compressor you drive with INPUT, with limiter, de-ess and iron
- **KNOCKOUT** — transient designer, wide or four bands
- **FULL HOUSE** — gate, lift the quiet, leveler, hold the loud
- **WHEELMAN** — automatic level rider for vocals and bass
- **BIG SHOT** — loudness maximizer, one band or shared across 5 / 16 bands, with dither
- **SILENCER** — de-esser, classic or smart detection
- **BREATHLESS** — breath remover (Tools tab)
- **TOMMY GUN** — one-knob tempo pump

**ENFORCER** gets two new switches: TAIL (STEADY / LINGER) and CHARACTER (CLEAN / WARM). At their
defaults it sounds exactly as before.

The plug-in picker shows more, smaller cards when a tab holds more than 15. VST3 plug-ins now read
the song's tempo from the host (for the ones that sync to it).

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
- INPUT drives it, ATTACK / RELEASE 1–7, RATIO 4 / 8 / 12 / 20 / STACKED.
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
  ENFORCER, HEAVY HAND, MUSCLE, CAPO, BOUNCER, TRIAD, CHOKEHOLD
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

**Faceplate fixes:** CAPO recovery buttons fit their text; TRIAD and ENFORCER labels no
longer overlap.

# HEM-Elite-Rack 0.7.0

**Six new dynamics plug-ins — twenty-eight in all**, in the rack and as VST3:
- **ENFORCER** — clean modern compressor: ratio 1–20, knee, attack, release or AUTO, AUTO GAIN,
  sidechain HPF, mix.
- **HEAVY HAND** — classic RMS compressor: threshold, compression up to 60:1, SOFT KNEE; grabs
  faster the harder it's hit, lets go at a steady ~120 dB per second.
- **MUSCLE** — punchy bus compressor: LOW FOCUS keeps the bass from pumping it, knee, MODERN
  (feed-forward) or VINTAGE (feedback) style, stereo link.
- **CAPO** — Diode-bridge bus compressor with a limiter after it, AUTO recovery,
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
  12 / 20 / STACKED (all ratios at once: aggressive and gritty), MIX.

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
