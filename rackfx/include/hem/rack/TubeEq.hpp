#pragma once
// =============================================================================
//  HEM VELVET — a passive tube program EQ in the classic style (our own design): broad,
//  forgiving moves with a tube behind them.
//
//    LOW    FREQ 20 / 30 / 60 / 100 Hz, BOOST 0..10, ATTEN 0..10. The attenuation shelf sits about
//           three times higher than the boost, so boosting and attenuating together gives the
//           famous move: more weight at the very bottom, a dip in the mud just above it.
//    HIGH   FREQ 3 .. 16 kHz, BOOST 0..10, BANDWIDTH 1 (sharp) .. 10 (broad)
//    ATTEN  SEL 5 / 10 / 20 kHz shelf, ATTEN 0..10
//    TUBE   DRIVE 0..100 % (even-harmonic warmth, 4x oversampled), OUTPUT
//    EQ     IN / OUT (out: the EQ glides flat, the tube stays), ANALOG (the hardware's hum and hiss: OFF / 50 / 60 Hz)
//  Everything glides (30 ms). Latency 23 samples, always.
// =============================================================================
#include "hem/rack/EliteEq.hpp"
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace tubeeq {
enum P : int { LowFreq, LowBoost, LowAtten, HighFreq, HighBoost, Bandwidth, HighAtten, AttenSel, Drive, Output,
               EqIn, Analog, kCount };   // new ones at the end: saved sessions keep their numbers
inline constexpr double kLowHz[] = {20, 30, 60, 100};
inline constexpr double kHighHz[] = {3000, 4000, 5000, 8000, 10000, 12000, 16000};
inline constexpr double kAttenHz[] = {5000, 10000, 20000};

struct Settings {
    double lowHz = 60, lowBoost = 0, lowAtten = 0, highHz = 10000, highBoost = 0, bandwidth = 5, highAtten = 0, attenHz = 10000;
};
[[nodiscard]] Settings settingsFrom(const float* plain) noexcept;
struct Design { eq::Section lowBoost, lowAtten, highBoost, highAtten; };
[[nodiscard]] Design design(const Settings& s, double sampleRate) noexcept;
[[nodiscard]] double curveDb(const Settings& s, double sampleRate, double hz) noexcept;
} // namespace tubeeq

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTubeEq();

} // namespace hem::rack
