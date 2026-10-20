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

// ---- a pitch shifter: one read head gliding through a short window of the past; when it runs out
// of room it hands over (a short crossfade) to a new head placed where the sound lines up best with
// what the old head is playing (the best match of the last few ms), so the splices don't warble.
class PitchShift {
public:
    void prepare(double fs, double windowMs) {
        win_ = static_cast<float>(windowMs * 0.001 * fs);
        fade_ = std::max(16.0f, win_ * 0.25f);
        search_ = std::min(static_cast<int>(0.02 * fs), static_cast<int>(win_ * 0.5f));
        match_ = std::max(32, static_cast<int>(0.004 * fs));
        d_.prepare(static_cast<std::uint32_t>(win_ * 2.0f + static_cast<float>(search_ + match_) + 16.0f));
        clear();
    }
    void clear() noexcept {
        d_.clear();
        head_ = 1.0f + 0.5f * win_;
        old_ = head_;
        fadeLeft_ = 0;
    }
    // ratio = output pitch / input pitch (e.g. 2^(cents/1200)); 0.5 .. 2
    float tick(float x, float ratio) noexcept {
        d_.write(x);
        const float move = ratio - 1.0f;   // how much nearer the present each head gets per sample
        head_ -= move;
        if (fadeLeft_ > 0) old_ -= move;
        // out of room (the head would pass the present, or the far end of the window): splice
        const float lo = 2.0f + std::max(0.0f, move) * fade_, hi = win_ - std::max(0.0f, -move) * fade_;
        if (fadeLeft_ == 0 && (head_ < lo || head_ > hi)) splice(head_ < lo);
        float y = d_.read(head_);
        if (fadeLeft_ > 0) {
            const float t = static_cast<float>(fadeLeft_) / fade_;   // 1 -> 0: the old head fades out
            y = y * (1.0f - t) + d_.read(old_) * t;
            --fadeLeft_;
        }
        return y;
    }

private:
    // a new head near the other end of the window, where the sound matches the current head's best
    void splice(bool toFar) noexcept {
        old_ = head_;
        const float frac = head_ - std::floor(head_);
        const int base = toFar ? static_cast<int>(win_) - search_ - 2 : 3;
        int best = base;
        float bestScore = -1e30f;
        for (int c = base; c < base + search_; ++c) {
            float score = 0.0f;
            for (int j = 0; j < match_; j += 2) score += d_.read(old_ + static_cast<float>(j)) * d_.read(static_cast<float>(c) + frac + static_cast<float>(j));
            if (score > bestScore) { bestScore = score; best = c; }
        }
        head_ = static_cast<float>(best) + frac;
        fadeLeft_ = static_cast<int>(fade_);
    }

    hc::fx::DelayLine d_;
    float win_ = 1000.0f, fade_ = 250.0f, head_ = 500.0f, old_ = 500.0f;
    int search_ = 500, match_ = 192, fadeLeft_ = 0;
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
inline constexpr const char* kNoteN[] = {"1/16", "1/8 T", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "BAR"};
inline constexpr double kNoteBeats[] = {0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0};

} // namespace hem::rack::hideout
