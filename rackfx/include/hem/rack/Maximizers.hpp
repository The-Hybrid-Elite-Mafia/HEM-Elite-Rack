#pragma once
// =============================================================================
//  The HEM maximizers (brick-wall limiters for loudness). Each one is its own plug-in with its own
//  full set of controls; they share their insides (rackfx/src/maxim/LimitKit.hpp).
//
//  Every one ends with the same IDR section: QUANTIZE (the word length), DITHER TYPE 1 (flat
//  triangular, +-1 LSB) / TYPE 2 (half as much, tilted to the highs) / NONE, and SHAPING MODERATE /
//  NORMAL / ULTRA / NONE (pushes the dither noise away from the 3-4 kHz region the ear hears best).
//  Neither dither type adds anything to digital silence.
//
//  BIG SHOT      one band: THRESHOLD (pushes in, made up after), CEILING, AUTO RELEASE (on: lets go
//                in 20..400 ms depending on how hard it has been working; off: a steady 100 ms),
//                IDR. Look-ahead 1.5 ms. Meters: 0 the most it took off lately (dB), 1 input peak.
//  LAST WORD     one band: INPUT LEFT / RIGHT and LINK (on: both sides follow INPUT LEFT), THRESHOLD,
//                CEILING, RELEASE 0.01..1000 ms, AUTO RELEASE (on: shorter on single peaks, up to
//                RELEASE when it works hard), DOMAIN DIGITAL (sample peaks never pass the ceiling) /
//                ANALOG (the peaks between the samples, rounded softly into the ceiling) / TRUE PEAK
//                (the peaks between the samples never pass), IDR with QUANTIZE NONE / 8..24 BIT.
//                Meters: 0 / 1 input peak left / right.
//  HIGH STAKES   five bands, linear phase: THRESHOLD, CEILING, LINK (on: the ceiling comes down with
//                the threshold — you hear the limiting without the loudness going up), RELEASE TYPE
//                (AUTO / AUTO LOWS / AUTO HIGHS / HARD / MANUAL), RELEASE (x0.1..x10 on every band),
//                four CROSSOVERS, SEPARATION (0: every band gives the same; 100: each band by its own
//                energy), per band GAIN (before the limiter), PRIORITY (gives up less, the others
//                more), LINK (GAIN also raises PRIORITY by as much), RELEASE (0.1..5000 ms), SOLO; IDR.
//                Latency 2559 + 72 + 32 samples. Meters: 0..4 the bands' reduction (dB), 5 / 6 input /
//                output peak (dBFS).
//  FAST MONEY    HIGH STAKES with a low-latency (minimum phase) band split; band RELEASE 0.1..2500 ms.
//  TOP DOG       the five-band limiter driven by a PROFILE (nine of ours: each sets the bands, gains,
//                priorities, releases, release type and separation); THRESHOLD, CEILING, LINK,
//                RELEASE (x0.1..x10), IDR. Linear phase. Meters as HIGH STAKES.
//  QUICK CASH    TOP DOG with the low-latency band split.
//  SWEET SIXTEEN sixteen bands, linear phase: THRESHOLD, CEILING, LINK, RELEASE (x0.1..x10), RELEASE
//                CHARACTER (22 of ours), SEPARATION LOW / MEDIUM / HIGH (how sharp the 16 bands are
//                cut apart); six EQ / priority bands, each GAIN (an EQ before the limiter), PRIORITY
//                (the same curve, steering where the limiting goes), LINK, FREQ, Q, TYPE BELL / LOW
//                SHELF / HIGH SHELF, IN; IDR. Meters: 0..5 the reduction at each of the six bands.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <cstdint>
#include <memory>

namespace hem::rack {

namespace bigshot {
enum P : int { Threshold, Ceiling, AutoRelease, Quantize, Dither, Shaping, kCount };
inline constexpr std::uint32_t kLookahead = 72;   // 1.5 ms at 48 kHz
inline constexpr std::uint32_t kLatency = kLookahead + 4;
} // namespace bigshot

namespace lastword {
enum P : int { InputL, InputR, InputLink, Threshold, Ceiling, Release, AutoRelease, Domain, Quantize, Dither, Shaping, kCount };
inline constexpr std::uint32_t kLatency = 72 + 4;
} // namespace lastword

// HIGH STAKES and FAST MONEY
namespace multimax {
inline constexpr int kBands = 5, kPerBand = 5;
enum P : int { Threshold, Ceiling, Link, ReleaseType, Release, X1, X2, X3, X4, Separation, Quantize, Dither, Shaping, Band0,
               kCount = Band0 + kBands * kPerBand };
enum BandP : int { Gain, Priority, BandLink, BandRelease, Solo };
inline constexpr int band(int b, int k) noexcept { return Band0 + b * kPerBand + k; }
inline constexpr std::uint32_t kLook = 72;
inline constexpr std::uint32_t kLatencyLinear = 2559 + kLook + 32;
inline constexpr std::uint32_t kLatencyFast = kLook + 32;
} // namespace multimax

// TOP DOG and QUICK CASH
namespace ultramax {
enum P : int { Threshold, Ceiling, Link, Release, Profile, Quantize, Dither, Shaping, kCount };
inline constexpr int kProfiles = 9;
} // namespace ultramax

namespace sixteen {
inline constexpr int kEqBands = 6, kPerBand = 7, kBands = 16;
enum P : int { Threshold, Ceiling, Link, Release, Character, Separation, Quantize, Dither, Shaping, Band0,
               kCount = Band0 + kEqBands * kPerBand };
enum BandP : int { Gain, Priority, BandLink, Freq, Q, Type, In };
inline constexpr int band(int b, int k) noexcept { return Band0 + b * kPerBand + k; }
inline constexpr int kCharacters = 22;
inline constexpr std::uint32_t kLatency = 2559 + 72 + 32;
} // namespace sixteen

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBigShot();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLastWord();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHighStakes();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFastMoney();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTopDog();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeQuickCash();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSweetSixteen();

} // namespace hem::rack
