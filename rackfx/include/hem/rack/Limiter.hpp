#pragma once
// =============================================================================
//  HEM LOCKDOWN — the mastering limiter at the end of the chain (our own design): makes a song
//  loud without it ever going over the ceiling.
//
//    INPUT      0..+24 dB of push into the limiter (more = louder, more limiting)
//    CEILING    -6..0 dBFS: nothing comes out above this
//    TRUE PEAK  ON: peaks are found 4x oversampled, so the peaks *between* samples (the ones that
//               clip a DAC or an MP3 / streaming encoder) stay under the ceiling too
//    RELEASE    10..1000 ms, or AUTO: quick after short hits, slower while it holds a dense passage
//    LINK       0..100 %: how much both sides share one gain (100 = the stereo image never moves)
//  Look-ahead 1.5 ms (the gain is already down when the peak arrives: no clipping, no overshoot),
//  then a final safety clip at the ceiling. Latency 84 samples at 48 kHz, always the same.
//  Meters: 0 = output loudness (LUFS, 3 s), 1 = output true peak (dBTP, held 1 s).
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace limiter {
enum P : int { Input, Ceiling, Release, AutoRelease, TruePeak, Link, kCount };
inline constexpr std::uint32_t kLookahead = 72;    // 1.5 ms at 48 kHz
inline constexpr std::uint32_t kDetectDelay = 12;  // the true-peak detector's own delay (rounded up)
inline constexpr std::uint32_t kLatency = kLookahead + kDetectDelay;
} // namespace limiter

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLimiter();

} // namespace hem::rack
