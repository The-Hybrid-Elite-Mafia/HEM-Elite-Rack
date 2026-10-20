#pragma once
// =============================================================================
//  STAKEOUT — analyzer (its own plate). The sound passes untouched.
//
//  The frequency display: bands 1/7 octave wide from 250 Hz up, and below 250 Hz bands RESOLUTION wide
//  (40 / 20 / 10 Hz: 52 / 58 / 70 bands in all). MODE PEAK (each band falls back over RESPONSE: 20 dB in that
//  time) or RMS (each band is the average over RESPONSE), CHANNELS L / R (two sets of bars) or L+R (one),
//  WEIGHTING NONE / A / B / C, FREEZE, MAX LINE (the most each band reached; CLEAR MAX starts it over).
//  The stereo position display: per band, how far left or right it sits (straight up = the middle, 45
//  degrees = hard left or right, past that = out of phase) and how loud it is (rings 20 dB apart).
//  The meters: PEAK L and R, and the SUM (half of L+R) as PEAK or RMS through the WEIGHTING, each with a
//  HOLD (its own reset), and a clip light per side (CLEAR CLIPS). The original's zoom and crosshair are
//  ways of looking at the display, not settings.
//
//  AnalyzerSource is how a plate reads the display data (any thread; the audio thread writes it).
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <cmath>
#include <memory>

namespace hem::rack {

inline constexpr int kRtaMaxBands = 70;

class AnalyzerSource {
public:
    virtual ~AnalyzerSource() = default;
    [[nodiscard]] virtual float rtaLevel(int side, int band) const noexcept = 0;   // dB (a full-scale sine reads 0)
    [[nodiscard]] virtual float rtaMax(int side, int band) const noexcept = 0;
    [[nodiscard]] virtual float spdAngle(int band) const noexcept = 0;             // degrees: 0 = middle, ±45 = hard L / R, ±90 = out of phase
    [[nodiscard]] virtual float spdLevel(int band) const noexcept = 0;             // dB
};

namespace stakeout {
enum P : int { Resolution, Mode, Response, Channels, Weighting, Freeze, ShowMax, ClearMax, MeterMode, HoldL, HoldR, HoldSum, ClearClips, kCount };
enum Meter : int { PeakL, PeakR, Sum, HoldPeakL, HoldPeakR, HoldSumM, ClipL, ClipR, kMeters };
inline constexpr float kLowStep[3] = {40.0f, 20.0f, 10.0f};
inline constexpr int kHighBands = 46;   // 250 Hz .. 20 kHz, 1/7 octave apart
[[nodiscard]] inline int lowBands(int resolution) noexcept { return static_cast<int>(240.0f / kLowStep[resolution < 0 ? 0 : resolution > 2 ? 2 : resolution]); }
[[nodiscard]] inline int bandCount(int resolution) noexcept { return lowBands(resolution) + kHighBands; }
// the centre of band b
[[nodiscard]] inline float bandHz(int resolution, int b) noexcept {
    const int low = lowBands(resolution);
    if (b < low) return kLowStep[resolution < 0 ? 0 : resolution > 2 ? 2 : resolution] * static_cast<float>(b + 1);
    return 250.0f * std::pow(80.0f, static_cast<float>(b - low) / static_cast<float>(kHighBands - 1));
}
} // namespace stakeout

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStakeout();

} // namespace hem::rack
