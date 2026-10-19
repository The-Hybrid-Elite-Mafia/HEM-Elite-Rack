#pragma once
// Test signals and measurements shared by the rack tests.
#include "hybrid/fx/Fx.hpp"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace hem::test {

struct Stereo {
    std::vector<float> l, r;
    explicit Stereo(std::size_t n = 0) : l(n, 0.0f), r(n, 0.0f) {}
    [[nodiscard]] std::size_t size() const noexcept { return l.size(); }
};

inline Stereo sine(double hz, double seconds, float amp, double fs = 48000.0, bool antiPhase = false) {
    Stereo s(static_cast<std::size_t>(seconds * fs));
    for (std::size_t i = 0; i < s.size(); ++i) {
        const float v = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / fs));
        s.l[i] = v;
        s.r[i] = antiPhase ? -v : v;
    }
    return s;
}

inline Stereo noise(double seconds, float amp, unsigned seed = 7, double fs = 48000.0) {
    Stereo s(static_cast<std::size_t>(seconds * fs));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    for (std::size_t i = 0; i < s.size(); ++i) { s.l[i] = amp * d(rng); s.r[i] = amp * d(rng); }
    return s;
}

inline void add(Stereo& a, const Stereo& b) {
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) { a.l[i] += b.l[i]; a.r[i] += b.r[i]; }
}

// Runs an effect over a signal in blocks of `block`.
inline void run(hc::fx::Effect& e, Stereo& s, std::uint32_t block = 512) {
    for (std::size_t i = 0; i < s.size(); i += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, s.size() - i));
        e.process(s.l.data() + i, s.r.data() + i, n);
    }
}

// Amplitude of the component at hz in x[from .. from+len) (single-bin DFT with a Hann window).
inline double toneAmp(const std::vector<float>& x, double hz, std::size_t from, std::size_t len, double fs = 48000.0) {
    double re = 0, im = 0, wsum = 0;
    for (std::size_t i = 0; i < len && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(len));
        const double ph = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / fs;
        re += w * x[from + i] * std::cos(ph);
        im -= w * x[from + i] * std::sin(ph);
        wsum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / wsum;
}
inline double db(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

inline double rms(const std::vector<float>& x, std::size_t from, std::size_t len) {
    double s = 0;
    std::size_t n = 0;
    for (std::size_t i = from; i < from + len && i < x.size(); ++i, ++n) s += static_cast<double>(x[i]) * x[i];
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}

inline float peak(const Stereo& s) {
    float p = 0;
    for (std::size_t i = 0; i < s.size(); ++i) p = std::max({p, std::fabs(s.l[i]), std::fabs(s.r[i])});
    return p;
}
inline bool finite(const Stereo& s) {
    for (std::size_t i = 0; i < s.size(); ++i) if (!std::isfinite(s.l[i]) || !std::isfinite(s.r[i])) return false;
    return true;
}

inline int paramIndex(const hc::fx::Effect& e, const std::string& id) {
    const auto ps = e.params();
    for (std::size_t i = 0; i < ps.size(); ++i) if (id == ps[i].id) return static_cast<int>(i);
    return -1;
}

// Energy of x in a band (FFT-free: sum of single-bin DFT amplitudes over a grid of frequencies).
inline double bandLevelDb(const std::vector<float>& x, double loHz, double hiHz, std::size_t from, std::size_t len, double fs = 48000.0) {
    double s = 0;
    int n = 0;
    for (double f = loHz; f <= hiHz; f *= 1.06, ++n) { const double a = toneAmp(x, f, from, len, fs); s += a * a; }
    return n ? 10.0 * std::log10(std::max(s / n, 1e-24)) : -240.0;
}

} // namespace hem::test
