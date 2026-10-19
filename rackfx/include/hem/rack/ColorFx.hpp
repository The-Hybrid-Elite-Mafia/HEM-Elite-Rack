#pragma once
// =============================================================================
//  HEM-Elite-Rack colour plug-ins (our own designs). Saturation stages run 4x oversampled.
//
//  HEM TUBE SAT — a tube stage: DRIVE (how hard), BIAS (more even harmonics = warmer, fuller),
//    TONE (darker .. brighter after the tube), MIX, OUTPUT. Quiet signals pass at the same level;
//    drive changes the colour, not the volume. Latency 23 samples.
//  HEM PREAMP — a mic / line preamp: GAIN 0..+30 dB into the stage (it really gets louder),
//    CHARACTER CLEAN / IRON / TUBE, 80 Hz LOW CUT, PHASE invert, OUTPUT trim. Latency 23.
//  HEM CASSETTE — a cassette deck: DRIVE, TYPE I / II / IV (normal, chrome, metal: how much top
//    end survives and how early it saturates), WOW, FLUTTER, HISS, AGE (duller, softer), OUTPUT.
//    Latency 119 samples (the tape transport needs room to wobble).
//  HEM VINYL — a record: CRACKLE, NOISE (surface and rumble), WEAR (duller, a little grit),
//    WARP (the slow pitch wobble of a bent record), MONO LOWS, OUTPUT. Latency 96 samples.
//  HEM BIT CRUSHER — lo-fi: BITS 1..24, DOWNSAMPLE 1..50 (sample-and-hold, aliasing on purpose),
//    MIX, OUTPUT. Zero latency.
//  HEM TRANSFORMER — output iron: DRIVE saturates the low end most (like real transformers),
//    WEIGHT (a low bump), TYPE NICKEL (tight) / STEEL (thick), MIX, OUTPUT. Latency 23.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace tubesat { enum P : int { Drive, Bias, Tone, Mix, Output, kCount }; }
namespace preamp { enum P : int { Gain, Character, LowCut, Phase, Output, kCount }; }
namespace cassette {
enum P : int { Drive, Type, Wow, Flutter, Hiss, Age, Output, kCount };
inline constexpr double kTopHz[] = {10000, 13000, 15500};   // TYPE I, II, IV
} // namespace cassette
namespace vinyl { enum P : int { Crackle, Noise, Wear, Warp, MonoLows, Output, kCount }; }
namespace crusher { enum P : int { Bits, Downsample, Mix, Output, kCount }; }
namespace transformer { enum P : int { Drive, Weight, Type, Mix, Output, kCount }; }

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTubeSat();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePreamp();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeCassette();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVinyl();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBitCrusher();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTransformer();

} // namespace hem::rack
