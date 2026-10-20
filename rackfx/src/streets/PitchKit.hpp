#pragma once
// Pitch detection shared by the tuner and the pitch corrector (private to rackfx).
//
// YIN (de Cheveigné & Kawahara): the cumulative-mean-normalized difference function, the first dip
// under a threshold, refined with a parabola. The input is low-passed and taken at half the rate
// first, which halves the work and is plenty for voices and instruments.
#include "../arsenal/Common.hpp"

#include <vector>

namespace hem::rack::streets {
using namespace arsenal;

class Yin {
public:
    // minHz: lowest pitch it can find; window / hop in samples at the half rate
    void prepare(double fs, double minHz, std::uint32_t window, std::uint32_t hop) {
        fsHalf_ = fs * 0.5;
        maxLag_ = static_cast<std::uint32_t>(std::ceil(fsHalf_ / minHz)) + 2;
        window_ = window;
        hop_ = hop;
        n_ = window_ + maxLag_ + 2;
        buf_.assign(2 * static_cast<std::size_t>(n_), 0.0f);   // every sample twice: the last n_ are always in one piece
        d_.assign(maxLag_ + 1, 0.0f);
        lp_[0].c = lp_[1].c = C::make(T::LowPass, fs, std::min(fsHalf_ * 0.4, 5000.0), 0.7071);
        clear();
    }
    void clear() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        lp_[0].clear();
        lp_[1].clear();
        odd_ = false;
        w_ = 0;
        sinceHop_ = 0;
        hz_ = 0.0f;
        conf_ = 0.0f;
        level_ = 0.0f;
    }
    // one input sample; true when a new estimate is ready
    bool push(float x) noexcept {
        const float y = lp_[1].tick(lp_[0].tick(x));
        odd_ = !odd_;
        if (odd_) return false;
        buf_[w_] = buf_[w_ + n_] = y;
        w_ = (w_ + 1) % n_;
        if (++sinceHop_ < hop_) return false;
        sinceHop_ = 0;
        analyse();
        return true;
    }
    [[nodiscard]] float hz() const noexcept { return hz_; }           // 0 = no pitch
    [[nodiscard]] float confidence() const noexcept { return conf_; } // 0..1
    [[nodiscard]] float levelDb() const noexcept { return level_; }   // of the analysed window

private:
    void analyse() noexcept {
        const float* x = buf_.data() + w_ + (n_ - window_ - maxLag_);   // oldest first
        double e = 0.0;
        for (std::uint32_t j = 0; j < window_; ++j) e += static_cast<double>(x[j]) * x[j];
        level_ = powDb(2.0 * e / window_);
        if (level_ < -60.0f) { hz_ = 0.0f; conf_ = 0.0f; return; }
        d_[0] = 1.0f;
        double run = 0.0;
        for (std::uint32_t tau = 1; tau <= maxLag_ - 2; ++tau) {
            double s = 0.0;
            for (std::uint32_t j = 0; j < window_; ++j) {
                const double df = static_cast<double>(x[j]) - x[j + tau];
                s += df * df;
            }
            run += s;
            d_[tau] = static_cast<float>(run > 0.0 ? s * tau / run : 1.0);
        }
        // the first dip under the threshold, then down to its bottom
        constexpr float kThreshold = 0.15f;
        std::uint32_t best = 0;
        for (std::uint32_t tau = 2; tau < maxLag_ - 2; ++tau) {
            if (d_[tau] < kThreshold) {
                while (tau + 1 < maxLag_ - 2 && d_[tau + 1] < d_[tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best == 0) { hz_ = 0.0f; conf_ = 0.0f; return; }
        const float a = d_[best - 1], b = d_[best], c = d_[best + 1];
        const float den = a - 2.0f * b + c;
        const float shift = std::fabs(den) > 1e-9f ? std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        hz_ = static_cast<float>(fsHalf_ / (static_cast<double>(best) + shift));
        conf_ = std::clamp(1.0f - b, 0.0f, 1.0f);
    }

    double fsHalf_ = 24000.0;
    std::uint32_t maxLag_ = 600, window_ = 800, hop_ = 256, sinceHop_ = 0, n_ = 1402, w_ = 0;
    std::vector<float> buf_, d_;
    hc::fx::Biquad lp_[2];
    bool odd_ = false;
    float hz_ = 0.0f, conf_ = 0.0f, level_ = -120.0f;
};

// Hz -> MIDI note number (fractional), for a reference A.
inline double midiOf(double hz, double refA = 440.0) noexcept { return 69.0 + 12.0 * std::log2(std::max(hz, 1e-3) / refA); }

} // namespace hem::rack::streets
