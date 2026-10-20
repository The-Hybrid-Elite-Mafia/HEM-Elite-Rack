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
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace heavyhitter {
enum P : int { Drum, Sensitivity, Thump, Treble, Snap, Output, Meter, kCount };
enum Kind : int { Kick, SnareTop, SnareUnder, Hats, Toms, Overheads, Room, kKinds };
} // namespace heavyhitter

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHeavyHitter();

} // namespace hem::rack
