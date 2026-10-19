#pragma once
// =============================================================================
//  HEM-Elite-Rack mastering tools (our own designs):
//
//  HEM HUSH MONEY — tames harsh S / T / SH sounds. FREQ 3..12 kHz (where the esses live), THRESHOLD,
//    RANGE (the most it turns down), SPLIT (only the ess band goes down) or WIDE (the whole signal
//    ducks, gentler), LISTEN (hear only the ess band the detector hears). Zero latency.
//
//  HEM WIDE LOAD — WIDTH 0..200 % (0 = mono, 100 = as it was, 200 = twice the sides),
//    MONO BASS (below 80..250 Hz the sides are removed: tight, centred low end), BALANCE, OUTPUT.
//    Meters: 0 = correlation (-1..+1), 1 = width of what comes out (side vs mid, dB). Zero latency.
//
//  HEM RAZOR — loudness by shaving the very tops of peaks: INPUT, CEILING, SHAPE (hard .. soft),
//    OUTPUT. 4x oversampled so the clipping doesn't fold back as harsh aliasing. Latency 23.
//
//  HEM SPARK — adds fresh harmonics above FREQ: brightness and air without EQ boost.
//    TUBE (even, warmer) or TAPE (odd, edgier), DRIVE, MIX (how much is added). Latency 23.
//
//  HEM SUCKER PUNCH — shapes the hit and the tail of every note, whatever its level:
//    ATTACK -100..+100 % (more / less punch), SUSTAIN -100..+100 % (more / less ring and room),
//    OUTPUT. Zero latency.
//
//  HEM THE ACCOUNTANT — measures, changes nothing (the sound passes untouched):
//    meters 0..6 = MOMENTARY (400 ms), SHORT-TERM (3 s), INTEGRATED (since reset, gated as
//    BS.1770 says), LOUDNESS RANGE (LU), TRUE PEAK max (dBTP), CORRELATION, TARGET difference (LU).
//    TARGET: -14 (Spotify / YouTube), -16 (Apple), -11, -9 (club / loud). RESET: any click restarts.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace deesser { enum P : int { Freq, Threshold, Range, Mode, Listen, kCount }; }
namespace width {
enum P : int { Width, MonoBass, Balance, Output, kCount };
inline constexpr float kMonoHz[] = {0.0f, 80.0f, 120.0f, 160.0f, 250.0f};   // 0 = off
} // namespace width
namespace clipper { enum P : int { Input, Ceiling, Shape, Output, kCount }; }
namespace exciter { enum P : int { Freq, Drive, Mix, Mode, kCount }; }
namespace transient { enum P : int { Attack, Sustain, Output, kCount }; }
namespace meter {
enum P : int { Target, Reset, kCount };
inline constexpr float kTargets[] = {-14.0f, -16.0f, -11.0f, -9.0f};
enum M : int { Momentary, ShortTerm, Integrated, Range, TruePeak, Correlation, ToTarget, kMeters };
} // namespace meter

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDeEsser();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStereoWidth();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeClipper();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeExciter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTransient();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLoudnessMeter();

} // namespace hem::rack
