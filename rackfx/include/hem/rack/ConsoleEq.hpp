#pragma once
// =============================================================================
//  HEM BOARDROOM — a large-format-console channel EQ (our own design, in the spirit of the
//  classic big-console style): fast to dial in, musical, four bands plus filters.
//
//    FILTERS  HPF 16..350 Hz (18 dB/oct) and LPF 3..22 kHz (12 dB/oct), each with its own IN switch
//    LF       ±15 dB, 30..450 Hz, shelf (or BELL)
//    LMF      ±15 dB, 200 Hz..2.5 kHz, Q 0.5..2.5
//    HMF      ±15 dB, 600 Hz..7 kHz, Q 0.5..2.5
//    HF       ±15 dB, 1.5..16 kHz, shelf (or BELL)
//    CURVE    CLASSIC: the width stays where it is set.
//             MODERN:  the width follows the gain (small moves are broad and gentle, big moves
//                      get narrower); shelves get a slight dip / bump before the shelf.
//    DRIVE    console colour (4x oversampled soft saturation), OUTPUT
//
//  Switches (IN, BELL) cross-fade over 10 ms, every knob glides over 30 ms: no zips, no clicks.
//  Latency: 23 samples (the oversampler), always the same.
// =============================================================================
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Fx.hpp"

#include <array>
#include <memory>

namespace hem::rack {

namespace console {
enum P : int { HpfOn, HpfHz, LpfOn, LpfHz, LfGain, LfHz, LfBell, LmfGain, LmfHz, LmfQ, HmfGain, HmfHz, HmfQ,
               HfGain, HfHz, HfBell, Curve, Drive, Output, kCount };
enum class CurveMode : int { Classic, Modern };

struct Settings {
    bool   hpf = false, lpf = false, lfBell = false, hfBell = false;
    double hpfHz = 80, lpfHz = 16000;
    double lfGain = 0, lfHz = 100, lmfGain = 0, lmfHz = 800, lmfQ = 1, hmfGain = 0, hmfHz = 3000, hmfQ = 1, hfGain = 0, hfHz = 8000;
    CurveMode mode = CurveMode::Classic;
};
// Settings from plain parameter values (kCount of them, in P order).
[[nodiscard]] Settings settingsFrom(const float* plain) noexcept;

// The filters one setting makes (exactly what the audio runs once it has settled).
struct Design {
    eq::Section hpf1, hpf2, lf, lmf, hmf, hf, lpf;
    bool hpfOn = false, lpfOn = false;
};
[[nodiscard]] Design design(const Settings& s, double sampleRate) noexcept;
// Total response in dB at `hz` (filters that are switched IN only) — for the faceplate curve.
[[nodiscard]] double curveDb(const Settings& s, double sampleRate, double hz) noexcept;
// The width a band really uses (MODERN narrows it with gain).
[[nodiscard]] double effectiveQ(double q, double gainDb, CurveMode mode) noexcept;
} // namespace console

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeConsoleEq();

} // namespace hem::rack
