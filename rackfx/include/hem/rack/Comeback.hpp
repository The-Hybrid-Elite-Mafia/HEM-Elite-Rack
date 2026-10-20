#pragma once
// =============================================================================
//  The returning plug-ins: names that were retired in 0.13 / 0.14 and came back (Oct 2026), each now
//  built to the full control set of one original. Our own words on every label.
//
//  FOUNDRY     a British console channel EQ with its preamp's colour:
//                LOW CUT OFF / 45 / 50 / 70 / 80 / 160 / 300 / 360 Hz, 18 dB / oct
//                LOWS AT OFF / 35 / 60 / 100 / 110 / 220 Hz, a shelf, LOWS ±18 dB
//                MIDDLE AT OFF / 360 / 700 Hz / 1.2 / 1.6 / 3.2 / 3.8 / 4.8 / 7 / 7.2 kHz, a bell, MIDDLE ±18 dB
//                HIGHS AT OFF / 10 / 12 kHz, a shelf, HIGHS ±18 dB
//                EQ IN / OUT (out: the curves go flat, the colour stays), FLIP (polarity), OUTPUT ±18 dB,
//                VINTAGE (the hardware's harmonics and hiss; off with EQ out = a clean wire).
//                Readouts: PEAK (dBFS, held), TO TOP (how far OUTPUT can rise before the peak reaches
//                -0.1 dBFS), CLIPS (blocks that went over 0 dBFS); CLEAR resets them.
//  BOARDROOM   the British console's bigger four-band channel EQ:
//                HIGH-PASS OFF / 27 / 47 / 82 / 150 / 270 Hz and LOW-PASS OFF / 18 / 12 / 8.7 / 5.6 / 3.9 kHz (18 dB / oct)
//                LOWS AT OFF / 33 / 56 / 100 / 180 / 330 Hz, LOWS ±18 dB, SHELF or BELL
//                LOW MIDS AT OFF / 220 .. 1200 Hz (10 steps), LOW MIDS ±18 dB, TIGHT (narrow) or wide
//                HIGH MIDS AT OFF / 1.5 .. 8.2 kHz (9 steps), HIGH MIDS ±18 dB, TIGHT or wide
//                HIGHS AT 3.3 / 4.7 / 6.8 / 10 / 15 kHz, HIGHS ±18 dB, SHELF or BELL
//                (the wide mids reach ±12 dB at the knob's end, the tight ones ±18 dB)
//                EQ IN / OUT, FLIP, OUTPUT ±18 dB, VINTAGE, and the same PEAK / TO TOP / CLIPS readouts with CLEAR.
//  MIDTOWN     the tube midrange EQ:
//                LOW PEAK AT 200 / 300 / 500 / 700 / 1000 Hz, LOW PEAK 0..10 (0..+10 dB)
//                DIP AT 200 / 300 / 500 / 700 Hz / 1 / 1.5 / 2 / 3 / 4 / 5 / 7 kHz, DIP 0..10 (0..-10 dB)
//                HIGH PEAK AT 1.5 / 2 / 3 / 4 / 5 kHz, HIGH PEAK 0..10 (0..+8 dB)
//                EQ IN / OUT, HUM (OFF / 50 / 60 Hz: the tube box's hum and hiss), OUTPUT ±18 dB.
//                The tube stage is always in, as in the box: clean at normal levels, a little warmth when hit hard.
//  COIL        spring reverb: DRIVE 0..100 (the level into the springs: higher = hotter and grittier, peaks
//                rounded), PRE-DELAY 0..600 ms (before the springs), HEADROOM 0..100 (how soon the drive stage
//                rounds the peaks; small signals come out at the same level whatever it is), TANK (seven tanks of
//                our own: two or three springs, each tank its own lengths, drip and brightness), LENGTH SHORT /
//                MEDIUM / LONG, TREBLE and BASS ±18 dB on the reverb only, MIX 0..100 %, OUTPUT ±18 dB,
//                WIDTH STEREO / MONO (mono: the right side is a copy of the left). The dry sound is not delayed.
//                The springs are modelled (loops with dispersive all-pass chains), not recorded impulse responses.
//  MIXTAPE, CRATE DIGGER, SHREDDER and BACKROOM are our own designs, not rebuilt after one original:
//  MIXTAPE     cassette deck: DRIVE, TAPE TYPE I / II / IV, WOW, FLUTTER, HISS, AGE, OUTPUT
//  CRATE DIGGER record player: CRACKLE, NOISE (surface and rumble), WEAR, WARP (33 rpm), MONO LOWS, OUTPUT
//  SHREDDER    BITS 1..24, DOWNSAMPLE 1..50x, MIX, OUTPUT
//  The slopes of the filters and the widths of the bells are our own choices (the manuals give the steps
//  and ranges, not the curves). All three have a constant latency of 23 samples (the colour is 4x oversampled).
// =============================================================================
#include "hem/rack/Plugins.hpp"
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace foundry {
enum P : int { LowCut, LowFreq, LowGain, MidFreq, MidGain, HighFreq, HighGain, EqIn, Phase, Output, Vintage, Clear, kCount };
inline constexpr float kCutHz[] = {0, 45, 50, 70, 80, 160, 300, 360};
inline constexpr float kLowHz[] = {0, 35, 60, 100, 110, 220};
inline constexpr float kMidHz[] = {0, 360, 700, 1200, 1600, 3200, 3800, 4800, 7000, 7200};
inline constexpr float kHighHz[] = {0, 10000, 12000};
} // namespace foundry

namespace boardroom {
enum P : int { HighPass, LowPass, LowFreq, LowGain, LowBell, LoMidFreq, LoMidGain, LoMidTight, HiMidFreq, HiMidGain, HiMidTight,
               HighFreq, HighGain, HighBell, EqIn, Phase, Output, Vintage, Clear, kCount };
inline constexpr float kHpHz[] = {0, 27, 47, 82, 150, 270};
inline constexpr float kLpHz[] = {0, 18000, 12000, 8700, 5600, 3900};
inline constexpr float kLowHz[] = {0, 33, 56, 100, 180, 330};
inline constexpr float kLoMidHz[] = {0, 220, 270, 330, 390, 470, 560, 680, 820, 1000, 1200};
inline constexpr float kHiMidHz[] = {0, 1500, 1800, 2200, 2700, 3300, 3900, 4700, 5600, 8200};
inline constexpr float kHighHz[] = {3300, 4700, 6800, 10000, 15000};
} // namespace boardroom

namespace midtown {
enum P : int { LowFreq, LowPeak, DipFreq, Dip, HighFreq, HighPeak, EqIn, Hum, Output, kCount };
inline constexpr float kLowHz[] = {200, 300, 500, 700, 1000};
inline constexpr float kDipHz[] = {200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000};
inline constexpr float kHighHz[] = {1500, 2000, 3000, 4000, 5000};
extern const CurveInfo kCurve;
} // namespace midtown

namespace coil {
enum P : int { Drive, PreDelay, Headroom, Tank, Length, Treble, Bass, Mix, Output, Width, kCount };
} // namespace coil

namespace mixtape {
enum P : int { Drive, Type, Wow, Flutter, Hiss, Age, Output, kCount };
inline constexpr double kTopHz[] = {10000, 13000, 15500};   // TYPE I, II, IV
} // namespace mixtape
namespace cratedigger { enum P : int { Crackle, Noise, Wear, Warp, MonoLows, Output, kCount }; }
namespace shredder { enum P : int { Bits, Downsample, Mix, Output, kCount }; }
inline constexpr std::uint32_t kMixtapeLatency = 23 + 96, kCrateDiggerLatency = 96;

inline constexpr std::uint32_t kComebackEqLatency = 23;

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFoundry();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBoardroom();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMidtown();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeCoil();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMixtape();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeCrateDigger();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeShredder();

} // namespace hem::rack
