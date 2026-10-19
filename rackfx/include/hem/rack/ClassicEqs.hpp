#pragma once
// =============================================================================
//  The classic-style equalizers (our own designs, after the console and outboard styles they
//  are named for). They share one engine (EqEngine): every move glides (30 ms), the curve on the
//  faceplate is computed by the same design function the audio runs, and the ones with a DRIVE
//  knob run their saturation 4x oversampled (latency 23 samples); the clean ones have none.
//
//  HEM FOUNDRY — Console channel: 18 dB/oct HIGH-PASS (OFF / 50 / 80 / 160 / 300 Hz),
//    LOW shelf 35 / 60 / 110 / 220 Hz, MID bell 360 Hz .. 7.2 kHz, HIGH shelf 12 kHz (all ±16 /
//    ±18 dB), PREAMP drive through iron (transformer-style, odd harmonics), OUTPUT.
//  HEM KNUCKLES — punchy three-band: LOW 50..400 Hz and HIGH 5..15 kHz (PEAK or SHELF),
//    MID 400 Hz..5 kHz, ±12 dB each with proportional Q (bigger moves get narrower, like the
//    originals), FILTER (50 Hz – 15 kHz band-pass), DRIVE, OUTPUT.
//  HEM MIDTOWN — tube midrange EQ: LOW PEAK 200 Hz..1 kHz boost, DIP 200 Hz..7 kHz cut,
//    HIGH PEAK 1.5..5 kHz boost (0..10 each), tube DRIVE, OUTPUT.
//  HEM PENTHOUSE — clean mastering EQ with broad bands at 10 Hz (SUB), 40, 160, 650 Hz and 2.5 kHz
//    (±10 dB) and an AIR shelf (2.5 / 5 / 10 / 20 kHz, boost 0..10 dB). No colour, zero latency.
//  HEM BLUEPRINT — 10 bands, 31 Hz .. 16 kHz, ±12 dB, constant Q. Zero latency.
//  HEM LEVERAGE — one knob tips the whole balance darker or brighter (±6 dB around a PIVOT of
//    300 / 650 Hz, 1.2 / 2.5 kHz), LOW CUT and HIGH CUT (12 dB/oct), OUTPUT. Zero latency.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace iron {
enum P : int { Hpf, LowFreq, LowGain, MidFreq, MidGain, HighGain, Drive, Output, kCount };
inline constexpr double kHpfHz[] = {0, 50, 80, 160, 300};
inline constexpr double kLowHz[] = {35, 60, 110, 220};
inline constexpr double kMidHz[] = {360, 700, 1600, 3200, 4800, 7200};
inline constexpr double kHighHz = 12000;
} // namespace iron
namespace punchq {
enum P : int { LowFreq, LowGain, LowShelf, MidFreq, MidGain, HighFreq, HighGain, HighShelf, Filter, Drive, Output, kCount };
inline constexpr double kLowHz[] = {50, 100, 200, 300, 400};
inline constexpr double kMidHz[] = {400, 800, 1500, 3000, 5000};
inline constexpr double kHighHz[] = {5000, 7500, 10000, 12500, 15000};
} // namespace punchq
namespace mideq {
enum P : int { LowFreq, LowPeak, DipFreq, Dip, HighFreq, HighPeak, Drive, Output, kCount };
inline constexpr double kLowHz[] = {200, 300, 500, 700, 1000};
inline constexpr double kDipHz[] = {200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000};
inline constexpr double kHighHz[] = {1500, 2000, 3000, 4000, 5000};
} // namespace mideq
namespace air {
enum P : int { Sub, B40, B160, B650, B2k5, AirFreq, AirGain, Output, kCount };
inline constexpr double kBandHz[] = {10, 40, 160, 650, 2500};
inline constexpr double kAirHz[] = {2500, 5000, 10000, 20000};
} // namespace air
namespace graphic {
inline constexpr int kBands = 10;
inline constexpr double kHz[kBands] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
enum P : int { Output = kBands, kCount };   // 0..9 = the bands
} // namespace graphic
namespace tilt {
enum P : int { Tilt, Pivot, LowCut, HighCut, Output, kCount };
inline constexpr double kPivotHz[] = {300, 650, 1200, 2500};
inline constexpr double kLowCutHz[] = {0, 20, 30, 40, 60, 80};
inline constexpr double kHighCutHz[] = {0, 20000, 16000, 12000, 8000};
} // namespace tilt

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeIronPre();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePunchEq();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMidEq();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeAirEq();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGraphicEq();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTilt();

struct CurveInfo;
extern const CurveInfo kIronCurve, kPunchEqCurve, kMidEqCurve, kAirCurve, kGraphicCurve, kTiltCurve;

} // namespace hem::rack
