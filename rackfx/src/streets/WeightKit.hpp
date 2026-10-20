#pragma once
// Level weightings shared by the meters (private to rackfx): first-order sections, the ITU-R 468 curve, and
// the A / B / C curves both as filters (from their poles) and as plain dB at a frequency.
#include "../arsenal/Common.hpp"

namespace hem::rack::weights {
using namespace arsenal;

// first-order sections by the bilinear transform, the corner pre-warped
inline C onePole(double fs, double hz, bool highPass) noexcept {
    const double k = std::tan(std::numbers::pi * std::min(hz, fs * 0.49) / fs);
    C c;
    c.b0 = highPass ? 1.0 / (1.0 + k) : k / (1.0 + k);
    c.b1 = highPass ? -c.b0 : c.b0;
    c.b2 = 0.0;
    c.a1 = (k - 1.0) / (k + 1.0);
    c.a2 = 0.0;
    return c;
}
// ITU-R 468 response (linear), from its published formula
inline double r468(double f) noexcept {
    const double h1 = -4.737338981378384e-24 * std::pow(f, 6) + 2.043828333606125e-15 * std::pow(f, 4) - 1.363894795463638e-7 * f * f + 1.0;
    const double h2 = 1.306612257412824e-19 * std::pow(f, 5) - 2.118150887518656e-11 * std::pow(f, 3) + 5.559488023498642e-4 * f;
    return 1.246332637532143e-4 * f / std::sqrt(h1 * h1 + h2 * h2);
}


// the A / B / C pole frequencies (Hz)
inline constexpr double kF1 = 20.598997, kF2 = 107.65265, kF3 = 737.86223, kF4 = 12194.217, kF5 = 158.5;
enum Curve : int { Flat, A, B, C };
// the curve's response at hz (dB, 0 at 1 kHz), from the analog formulas
inline double curveDb(int curve, double hz) noexcept {
    if (curve == Flat) return 0.0;
    const double f2 = hz * hz;
    auto ra = [](double ff) {
        return 12194.217 * 12194.217 * ff * ff /
               ((ff + kF1 * kF1) * std::sqrt((ff + kF2 * kF2) * (ff + kF3 * kF3)) * (ff + kF4 * kF4));
    };
    auto rb = [](double ff) {
        return 12194.217 * 12194.217 * ff * std::sqrt(ff) / ((ff + kF1 * kF1) * std::sqrt(ff + kF5 * kF5) * (ff + kF4 * kF4));
    };
    auto rc = [](double ff) { return 12194.217 * 12194.217 * ff / ((ff + kF1 * kF1) * (ff + kF4 * kF4)); };
    const double r = curve == A ? ra(f2) / ra(1e6) : curve == B ? rb(f2) / rb(1e6) : rc(f2) / rc(1e6);
    return 20.0 * std::log10(std::max(r, 1e-12));
}

} // namespace hem::rack::weights
