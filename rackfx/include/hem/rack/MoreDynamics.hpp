#pragma once
// =============================================================================
//  More HEM-Elite-Rack dynamics (our own designs). Each publishes its gain reduction (dB, <= 0)
//  for the faceplate's needle.
//
//  HEM VCA COMP — a clean, modern compressor: THRESHOLD, RATIO 1..20, KNEE 0..24 dB, ATTACK,
//    RELEASE (or AUTO), MAKEUP (or AUTO MAKEUP), sidechain HPF, MIX, OUTPUT. Zero latency.
//  HEM PUNCH COMP — a one-knob-per-job RMS compressor: THRESHOLD, COMPRESSION (ratio up
//    to 60:1), SOFT KNEE (soft knee), OUTPUT, MIX. Attack and release follow the music: the further
//    over, the faster it grabs; it lets go at a steady 120 dB per second. Zero latency.
//  HEM FOCUS BUS — a punchy bus compressor: RATIO 1.5..10, stepped ATTACK and RELEASE, KNEE
//    HARD / MED / SOFT, LOW FOCUS (FLAT / LESS LOW / MUCH LESS: the detector listens less to the low
//    end, so the bass pumps less), MODERN (feed-forward) or VINTAGE (feedback) style, LINK, MAKEUP, MIX, OUTPUT.
//    Zero latency.
//  HEM DIODE BUS — a Diode-bridge bus compressor with a limiter after it: RATIO, THRESHOLD,
//    ATTACK FAST / SLOW, RECOVERY 100 ms .. 1.5 s or AUTO 1 / AUTO 2 (release follows the music),
//    LIMIT on / LEVEL, MAKEUP, iron DRIVE, MIX. Latency 23 samples (the iron is 4x oversampled).
//  HEM GATE — gate or expander: THRESHOLD (with 4 dB hysteresis so it doesn't chatter), RANGE
//    (how far it closes), ATTACK, HOLD, RELEASE, GATE or EXPAND (1:3 below the threshold), the
//    detector's HPF and LPF, LISTEN (hear the detector). Zero latency.
//  HEM MULTIBAND — a three-band compressor: crossovers LOW / MID and MID / HIGH (Linkwitz-Riley,
//    24 dB/oct, the bands add back up flat), THRESHOLD, RATIO and GAIN per band, ATTACK, RELEASE,
//    OUTPUT. Meters 0..2 = each band's gain reduction. Zero latency.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

// The usual soft-knee compressor curve (dB in, dB of gain change out, <= 0).
[[nodiscard]] float kneeGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept;

namespace vca {
enum P : int { Threshold, Ratio, Knee, Attack, Release, AutoRelease, Makeup, AutoMakeup, ScHpf, Mix, Output, kCount };
inline constexpr float kScHz[] = {0, 60, 90, 150, 250};
} // namespace vca
namespace punch {
enum P : int { Threshold, Compression, OverEasy, Output, Mix, kCount };
} // namespace punch
namespace focus {
enum P : int { Threshold, Ratio, Attack, Release, Knee, LowFocus, Style, Link, Makeup, Mix, Output, kCount };
inline constexpr float kRatios[] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f};
inline constexpr float kAttackMs[] = {0.03f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
inline constexpr float kReleaseMs[] = {50, 100, 200, 500, 1000, 2000, 3000};
inline constexpr float kKneeDb[] = {0.0f, 6.0f, 12.0f};
} // namespace focus
namespace diodebus {
enum P : int { Threshold, Ratio, Attack, Recovery, Limit, LimitLevel, Makeup, Drive, Mix, kCount };
inline constexpr float kRatios[] = {1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
inline constexpr float kRecoveryMs[] = {100, 400, 800, 1500};   // 4, 5 = AUTO 1, AUTO 2
} // namespace diodebus
namespace gate {
enum P : int { Threshold, Range, Attack, Hold, Release, Mode, ScHpf, ScLpf, Listen, kCount };
inline constexpr float kHpfHz[] = {0, 60, 120, 250, 500};
inline constexpr float kLpfHz[] = {0, 12000, 8000, 4000, 2000};
inline constexpr float kHysteresisDb = 4.0f;
} // namespace gate
namespace multiband {
enum P : int { LowX, HighX, LowThr, LowRatio, LowGain, MidThr, MidRatio, MidGain, HighThr, HighRatio, HighGain, Attack, Release, Output, kCount };
} // namespace multiband

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVcaComp();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makePunchComp();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFocusBus();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDiodeBus();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGate();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMultiband();

} // namespace hem::rack
