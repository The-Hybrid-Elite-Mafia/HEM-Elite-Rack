#pragma once
// Building blocks shared by the hideout plug-ins (space and modulation; private to rackfx).
#include "../arsenal/Common.hpp"

#include <array>

namespace hem::rack::hideout {
using namespace arsenal;

// ---- an 8-line feedback delay network reverb (Householder mixing, damped, decay set as T60) ------
class Fdn8 {
public:
    static constexpr int kN = 8;
    // maxSize: the largest SIZE that will be asked for (line lengths scale with it)
    void prepare(double fs, double maxSize) {
        fs_ = fs;
        for (int i = 0; i < kN; ++i) d_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(kBaseMs[i] * maxSize * fs * 0.001) + 8);
        maxSize_ = maxSize;
        set(1.0, 1.5, 6000.0);
        clear();
    }
    void clear() noexcept {
        for (auto& d : d_) d.clear();
        lp_.fill(0.0f);
    }
    // size (relative line length), decay T60 in seconds, damping corner (Hz)
    void set(double size, double t60, double dampHz) noexcept {
        size = std::clamp(size, 0.05, maxSize_);
        for (int i = 0; i < kN; ++i) {
            const double n = std::max(8.0, kBaseMs[i] * size * fs_ * 0.001);
            len_[static_cast<std::size_t>(i)] = static_cast<float>(n);
            g_[static_cast<std::size_t>(i)] = static_cast<float>(std::pow(10.0, -3.0 * n / (std::max(t60, 0.05) * fs_)));
        }
        damp_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::min(dampHz, fs_ * 0.45) / fs_));
    }
    void tick(float inL, float inR, float& outL, float& outR) noexcept {
        float v[kN];
        float sum = 0.0f;
        for (int i = 0; i < kN; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const float o = d_[k].read(len_[k] - 1.0f);
            lp_[k] = o + damp_ * (lp_[k] - o);
            v[i] = lp_[k] * g_[k];
            sum += v[i];
        }
        outL = 0.0f;
        outR = 0.0f;
        for (int i = 0; i < kN; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const float o = d_[k].read(len_[k] - 1.0f);
            (i % 2 == 0 ? outL : outR) += (i % 4 < 2 ? o : -o);
            const float fb = v[i] - 0.25f * sum;
            d_[k].write(fb + 0.35f * (i % 2 == 0 ? inL : inR) * (i % 3 == 0 ? -1.0f : 1.0f));
        }
        outL *= 0.5f;
        outR *= 0.5f;
    }

private:
    static constexpr double kBaseMs[kN] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1};
    double fs_ = 48000.0, maxSize_ = 1.0;
    std::array<hc::fx::DelayLine, kN> d_{};
    std::array<float, kN> len_{}, g_{}, lp_{};
    float damp_ = 0.0f;
};

// ---- a pitch shifter for small shifts: two read heads sweeping a short window, crossfaded -------
class PitchShift {
public:
    void prepare(double fs, double windowMs) {
        win_ = static_cast<float>(windowMs * 0.001 * fs);
        d_.prepare(static_cast<std::uint32_t>(win_) + 8);
        clear();
    }
    void clear() noexcept {
        d_.clear();
        phase_ = 0.0f;
    }
    // ratio = output pitch / input pitch (e.g. 2^(cents/1200))
    float tick(float x, float ratio) noexcept {
        d_.write(x);
        phase_ += (1.0f - ratio) / win_;
        phase_ -= std::floor(phase_);
        float p2 = phase_ + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        const float a = std::sin(static_cast<float>(std::numbers::pi) * phase_), b = std::sin(static_cast<float>(std::numbers::pi) * p2);
        return a * a * d_.read(1.0f + phase_ * win_) + b * b * d_.read(1.0f + p2 * win_);
    }
    [[nodiscard]] float latencyGuess() const noexcept { return win_ * 0.5f; }

private:
    hc::fx::DelayLine d_;
    float win_ = 1000.0f, phase_ = 0.0f;
};

// ---- LFO shapes (phase 0..1 -> -1..+1) ----------------------------------------------------------
enum class Wave : int { Sine, Triangle, Square, SawUp, SawDown };
inline float lfo(double phase, Wave w) noexcept {
    const double p = phase - std::floor(phase);
    switch (w) {
    case Wave::Sine: return static_cast<float>(std::sin(2.0 * std::numbers::pi * p));
    case Wave::Triangle: return static_cast<float>(p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4);
    case Wave::Square: {   // softened edges (no clicks)
        const double s = std::sin(2.0 * std::numbers::pi * p);
        return static_cast<float>(std::tanh(6.0 * s) / std::tanh(6.0));
    }
    case Wave::SawUp: return static_cast<float>(2 * p - 1);
    case Wave::SawDown: return static_cast<float>(1 - 2 * p);
    }
    return 0.0f;
}

// Note lengths in beats used by the synced plug-ins.
inline constexpr const char* kNoteN[] = {"1/16", "1/8 T", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "1 BAR"};
inline constexpr double kNoteBeats[] = {0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0};

} // namespace hem::rack::hideout
