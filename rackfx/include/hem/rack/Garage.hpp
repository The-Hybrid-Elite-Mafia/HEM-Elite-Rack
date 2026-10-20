#pragma once
// =============================================================================
//  Wave 4 (colour, tape, drums, lo-fi): new HEM plug-ins, each built to the full control set of one original.
//  Our own names and our own words on every label.
//
//  HEAVY HITTER  drum enhancer. DRUM picks the voicing (KICK, SNARE TOP, SNARE UNDER, HATS, TOMS, OVERHEADS,
//                ROOM): where THUMP and TREBLE work and how SNAP hears a hit. SENSITIVITY ±18 dB (the level in),
//                THUMP ±12 dB (the drum's low end), TREBLE ±12 dB (its top), SNAP 0..100 (how much the front of
//                each hit is pushed out), OUTPUT ±18 dB. METER IN / OUT picks what the level window shows
//                (-24..0 dBFS); the INPUT window is the level going in (aim for about -12 .. -6 dBFS).
//                Our voicings, not the original's.
//  REEL MONEY    tube tape machine: SPEED 7.5 / 15 IPS, MONITOR TAPE / INPUT (input = the tube stage only),
//                BIAS NORMAL / HOT (hot = 3 dB over: softer top, cleaner), INTO TAPE ±18 dB (how hard the tube
//                and the tape are hit), OFF TAPE ±18 dB (the level out), LINK (on: OFF TAPE also takes back what
//                INTO TAPE adds, so the level stays put), TAPE LEVEL 150..1020 nWb/m (185 = the reference: more =
//                more tape saturation, the level made up), WOBBLE 0..100 (wow and flutter, 50 = a healthy machine),
//                HISS OFF / -40..0 dB (0 = as noisy as the real machine), the echo off the tape: ECHO TIME
//                1..500 ms, ECHO STYLE SLAP (one repeat) / REPEATS, ECHO LEVEL OFF..100, ECHO TONE 200 Hz..16 kHz;
//                the VU meter: METER IN / OUT, VU ZERO -24..-8 dBFS. Constant latency (kReelLatency).
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace heavyhitter {
enum P : int { Drum, Sensitivity, Thump, Treble, Snap, Output, Meter, kCount };
enum Kind : int { Kick, SnareTop, SnareUnder, Hats, Toms, Overheads, Room, kKinds };
} // namespace heavyhitter

namespace reelmoney {
enum P : int { Speed, Monitor, Bias, IntoTape, OffTape, Link, TapeLevel, Wobble, Hiss, EchoTime, EchoStyle, EchoLevel, EchoTone,
               Meter, VuZero, kCount };
inline constexpr std::uint32_t kReelLatency = 23 + 23 + 64;
} // namespace reelmoney

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHeavyHitter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeReelMoney();

} // namespace hem::rack
