#pragma once
// Small helpers shared by the dynamics arsenal (private to rackfx).
#include "hybrid/fx/Dsp.hpp"
#include "hybrid/fx/Fx.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace hem::rack::arsenal {

using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;
using OS = hc::fx::Oversampler4;

inline const char* const kOnOff[] = {"OFF", "ON"};

inline float dbOf(float x) noexcept { return 20.0f * std::log10(std::max(x, 1e-9f)); }
inline float powDb(double ms) noexcept { return static_cast<float>(10.0 * std::log10(std::max(ms, 1e-18))); }
// one-pole coefficient for a time constant in ms
inline float coefMs(double fs, double ms) noexcept { return static_cast<float>(std::exp(-1.0 / (fs * std::max(ms, 0.001) * 0.001))); }
// one step of a dB-domain gain smoother: attack when more reduction is wanted, release otherwise
inline float follow(float gr, float target, float att, float rel) noexcept { return target + (target < gr ? att : rel) * (gr - target); }

// Gain change (dB, <= 0) for a level with a soft knee (kneeDb wide around the threshold).
inline float downDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept {
    const float over = levelDb - thresholdDb, slope = 1.0f / std::max(ratio, 1.0f) - 1.0f;
    if (kneeDb <= 1e-3f) return over > 0.0f ? slope * over : 0.0f;
    if (over <= -kneeDb * 0.5f) return 0.0f;
    if (over >= kneeDb * 0.5f) return slope * over;
    const float k = over + kneeDb * 0.5f;
    return slope * k * k / (2.0f * kneeDb);
}

// "How hard has it been working": charges while it holds more than `deepDb` (over chargeS),
// drains while it rests (over drainS). 0..1. Program-dependent releases use it.
struct Memory {
    float v = 0, charge = 0, drain = 0;
    void set(double fs, double chargeS, double drainS) noexcept {
        charge = static_cast<float>(1.0 - std::exp(-1.0 / (fs * chargeS)));
        drain = static_cast<float>(1.0 - std::exp(-1.0 / (fs * drainS)));
    }
    void tick(bool working) noexcept { v += working ? (1.0f - v) * charge : -v * drain; }
};

// A Linkwitz-Riley (24 dB / oct) crossover tree for up to kMax bands. The bands add back up to an
// all-pass of the input (flat magnitude): every lower band also runs through the all-pass of each
// higher crossover, so they line up in phase. One per channel.
template <int kMax>
class Crossover {
public:
    void design(double fs, const float* hz, int bands) noexcept {
        bands_ = std::clamp(bands, 1, kMax);
        for (int x = 0; x + 1 < bands_; ++x) {
            const C lp = C::make(T::LowPass, fs, hz[x], 0.7071), hp = C::make(T::HighPass, fs, hz[x], 0.7071), ap = C::make(T::AllPass, fs, hz[x], 0.7071);
            lp_[x][0].c = lp_[x][1].c = lp;
            hp_[x][0].c = hp_[x][1].c = hp;
            for (int b = 0; b < x; ++b) ap_[x][b].c = ap;   // the bands under crossover x that need its phase
        }
    }
    void clear() noexcept {
        for (auto& a : lp_) for (auto& b : a) b.clear();
        for (auto& a : hp_) for (auto& b : a) b.clear();
        for (auto& a : ap_) for (auto& b : a) b.clear();
    }
    [[nodiscard]] int bands() const noexcept { return bands_; }
    // out[0..bands-1]: lowest band first.
    void split(float x, float* out) noexcept {
        float rest = x;
        for (int k = 0; k + 1 < bands_; ++k) {
            out[k] = lp_[k][1].tick(lp_[k][0].tick(rest));
            rest = hp_[k][1].tick(hp_[k][0].tick(rest));
            for (int b = 0; b < k; ++b) out[b] = ap_[k][b].tick(out[b]);
        }
        out[bands_ - 1] = rest;
    }

private:
    int bands_ = 1;
    std::array<std::array<hc::fx::Biquad, 2>, kMax> lp_{}, hp_{};
    std::array<std::array<hc::fx::Biquad, kMax>, kMax> ap_{};
};

// Gentle saturation curves (all pass through 0 with slope 1).
inline float softClip(float x) noexcept { return x / std::sqrt(1.0f + x * x); }                  // odd, smooth
inline float tubeish(float x, float even) noexcept {                                              // odd + a little even
    const float y = x + even * x * x;
    return y / std::cbrt(1.0f + std::fabs(y) * y * y);
}

// A sample delay of a fixed length (allocated in prepare).
struct Delay2 {
    hc::fx::DelayLine l, r;
    void prepare(std::uint32_t n) { l.prepare(n + 1); r.prepare(n + 1); }
    void clear() noexcept { l.clear(); r.clear(); }
};

// Note length (beats) -> ms at a tempo.
inline double beatsMs(double beats, double bpm) noexcept { return beats * 60000.0 / std::max(bpm, 20.0); }

} // namespace hem::rack::arsenal
