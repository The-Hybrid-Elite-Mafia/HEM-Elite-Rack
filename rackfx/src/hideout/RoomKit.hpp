#pragma once
// Building blocks for the room reverbs (BIG HOUSE, SPEAKEASY; private to rackfx).
//
//  * Tail: an 8-line feedback delay network whose decay is set per band: below LOW Hz it decays at T60 x LOW RATIO,
//    above HIGH Hz at T60 x HIGH RATIO, between them at T60. Input diffusion (four allpasses) sets how fast the echo
//    density builds. A slow, slight wobble of two line lengths keeps it from ringing metallic.
//  * Early: up to 24 early reflections per side, placed by a pattern (seeded, so a VARIATION gives a slightly
//    different room), each with its own level, and a high-frequency absorption.
#include "SpaceKit.hpp"

#include <array>
#include <cstdint>

namespace hem::rack::hideout {

class Tail {
public:
    static constexpr int kN = 8;
    void prepare(double fs, double maxSize) {
        fs_ = fs;
        maxSize_ = maxSize;
        for (int i = 0; i < kN; ++i) line_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(kBaseMs[i] * maxSize * 1.05 * fs * 0.001) + 16);
        for (int i = 0; i < 4; ++i) ap_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(kApMs[i] * 0.001 * fs) + 4);
        set(1.0, 2.0, 1.0, 1.0, 250.0, 6000.0, 0.8);
        clear();
    }
    void clear() noexcept {
        for (auto& d : line_) d.clear();
        for (auto& d : ap_) d.clear();
        lpLo_.fill(0.0f);
        lpHi_.fill(0.0f);
        wob_ = 0.0;
    }
    // size (relative line lengths), T60 (s), low / high decay ratios, their corners (Hz), density 0..1 (input diffusion)
    void set(double size, double t60, double lowRatio, double highRatio, double lowHz, double highHz, double density) noexcept {
        size = std::clamp(size, 0.05, maxSize_);
        for (int i = 0; i < kN; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const double n = std::max(8.0, kBaseMs[i] * size * fs_ * 0.001);
            len_[k] = static_cast<float>(n);
            auto g = [&](double t) { return static_cast<float>(std::pow(10.0, -3.0 * n / (std::max(t, 0.02) * fs_))); };
            gMid_[k] = g(t60);
            gLo_[k] = g(t60 * lowRatio);
            gHi_[k] = g(t60 * highRatio);
        }
        aLo_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::clamp(lowHz, 10.0, fs_ * 0.45) / fs_));
        aHi_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::clamp(highHz, 100.0, fs_ * 0.45) / fs_));
        diff_ = static_cast<float>(0.75 * std::clamp(density, 0.0, 1.0));
    }
    void tick(float inL, float inR, float& outL, float& outR) noexcept {
        // input diffusion: four allpasses in series on the mono-ish input, left and right kept apart by the taps
        float x = 0.5f * (inL + inR), sideIn = 0.5f * (inL - inR);
        for (int i = 0; i < 4; ++i) {
            auto& d = ap_[static_cast<std::size_t>(i)];
            const float z = d.read(kApMs[i] * 0.001f * static_cast<float>(fs_) - 1.0f);
            const float w = x + diff_ * z;
            d.write(w);
            x = z - diff_ * w;
        }
        wob_ += 0.37 / fs_;
        if (wob_ >= 1.0) wob_ -= 1.0;
        const float wob = static_cast<float>(std::sin(2.0 * std::numbers::pi * wob_)) * 0.0011f * static_cast<float>(fs_) * 0.001f * 6.0f;
        float v[kN];
        float sum = 0.0f;
        for (int i = 0; i < kN; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const float extra = (i == 2 ? wob : i == 5 ? -wob : 0.0f);
            float o = line_[k].read(len_[k] - 1.0f + extra);
            // three-band decay: lows (one-pole low-pass), highs (input minus a one-pole low-pass), the middle in between
            lpLo_[k] = o + aLo_ * (lpLo_[k] - o);
            lpHi_[k] = o + aHi_ * (lpHi_[k] - o);
            const float lo = lpLo_[k], hi = o - lpHi_[k], mid = o - lo - hi;
            v[i] = gLo_[k] * lo + gMid_[k] * mid + gHi_[k] * hi;
            sum += v[i];
        }
        const float h = sum * (2.0f / kN);   // Householder: each line gets the others back, energy kept
        for (int i = 0; i < kN; ++i) {
            const float feed = (i & 1) ? x - sideIn : x + sideIn;
            line_[static_cast<std::size_t>(i)].write(v[i] - h + feed * 0.35f);
        }
        outL = 0.5f * (v[0] - v[2] + v[4] - v[6]);
        outR = 0.5f * (v[1] - v[3] + v[5] - v[7]);
    }

private:
    static constexpr double kBaseMs[kN] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.1};
    static constexpr float kApMs[4] = {4.77f, 3.59f, 12.73f, 9.31f};
    double fs_ = 48000.0, maxSize_ = 1.0, wob_ = 0.0;
    std::array<hc::fx::DelayLine, kN> line_;
    std::array<hc::fx::DelayLine, 4> ap_;
    std::array<float, kN> len_{}, gLo_{}, gMid_{}, gHi_{}, lpLo_{}, lpHi_{};
    float aLo_ = 0.9f, aHi_ = 0.5f, diff_ = 0.6f;
};

// Early reflections: a seeded pattern of taps (time 0..1 of the span, level falling with time), scaled to `spanMs`.
class Early {
public:
    static constexpr int kTaps = 24;
    void prepare(double fs, double maxSpanMs) {
        fs_ = fs;
        for (auto& d : line_) d.prepare(static_cast<std::uint32_t>(maxSpanMs * 0.001 * fs) + 8);
        clear();
    }
    void clear() noexcept {
        for (auto& d : line_) d.clear();
        lp_[0] = lp_[1] = 0.0f;
    }
    // spanMs: when the last reflection arrives; seed: the variation; spread: 1 = taps evenly spread, larger = bunched
    // early (a higher "dimension" gives more, denser reflections later); absorbHz: high-frequency absorption corner;
    // startMs: when the first reflection can arrive (the taps fill startMs..spanMs)
    void set(double spanMs, int seed, double spread, int taps, double absorbHz, double startMs = 0.0) noexcept {
        startMs = std::clamp(startMs, 0.0, spanMs);
        n_ = std::clamp(taps, 1, kTaps);
        std::uint32_t r = 0x9E3779B9u * static_cast<std::uint32_t>(seed + 1);
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < n_; ++k) {
                r = r * 1664525u + 1013904223u;
                const double jitter = (static_cast<double>(r >> 8) / 16777216.0 - 0.5) * 0.8;
                const double pos = std::pow((k + 1.0 + jitter) / (n_ + 0.5), spread);
                at_[s][static_cast<std::size_t>(k)] = static_cast<float>(std::max(1.0, (startMs + pos * (spanMs - startMs)) * 0.001 * fs_));
                r = r * 1664525u + 1013904223u;
                const float sign = (r >> 31) ? -1.0f : 1.0f;
                gain_[s][static_cast<std::size_t>(k)] = sign * static_cast<float>(0.9 / std::sqrt(n_) * (1.0 - 0.6 * pos));
            }
        a_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::clamp(absorbHz, 200.0, fs_ * 0.45) / fs_));
    }
    void tick(float inL, float inR, float& outL, float& outR) noexcept {
        line_[0].write(inL);
        line_[1].write(inR);
        float o[2] = {0.0f, 0.0f};
        for (int s = 0; s < 2; ++s) {
            for (int k = 0; k < n_; ++k) o[s] += gain_[s][static_cast<std::size_t>(k)] * line_[s].read(at_[s][static_cast<std::size_t>(k)]);
            lp_[s] = o[s] + a_ * (lp_[s] - o[s]);
        }
        outL = lp_[0];
        outR = lp_[1];
    }

private:
    double fs_ = 48000.0;
    std::array<hc::fx::DelayLine, 2> line_;
    float at_[2][kTaps]{}, gain_[2][kTaps]{}, lp_[2]{}, a_ = 0.0f;
    int n_ = 12;
};

} // namespace hem::rack::hideout
