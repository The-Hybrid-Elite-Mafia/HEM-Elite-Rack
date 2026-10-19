#pragma once
// The ITU-R BS.1770 K-weighting (high shelf + RLB high-pass) for any sample rate — the same design
// The Hybrid Core's export loudness uses, so every HEM loudness readout agrees with it and with
// what streaming services measure.
#include "hybrid/fx/Dsp.hpp"

#include <cmath>
#include <numbers>

namespace hem::rack {

inline void kWeighting(double fs, hc::fx::BiquadCoefs& shelf, hc::fx::BiquadCoefs& highPass) noexcept {
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(std::numbers::pi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(std::numbers::pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (K * K - 1.0) / a0;
        highPass.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

} // namespace hem::rack
