#include "hem/rack/Spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hem::rack {

float spectrumBinHz(int i) noexcept {
    const float t = static_cast<float>(i) / static_cast<float>(kSpectrumBins - 1);
    return kSpectrumLowHz * std::pow(kSpectrumHighHz / kSpectrumLowHz, t);
}
float spectrumBinOf(float hz) noexcept {
    return std::log(std::max(hz, 1.0f) / kSpectrumLowHz) / std::log(kSpectrumHighHz / kSpectrumLowHz) * static_cast<float>(kSpectrumBins - 1);
}

void SpectrumMeter::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    n_ = sampleRate > 64000.0 ? 8192 : 4096;   // ~11.7 Hz per bin at 44.1 / 48 kHz
    hop_ = n_ / 4;
    fft_.prepare(n_);
    ring_.assign(static_cast<std::size_t>(n_), 0.0f);
    win_.resize(static_cast<std::size_t>(n_));
    double sum = 0.0;
    for (int i = 0; i < n_; ++i) {
        win_[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / n_));
        sum += win_[static_cast<std::size_t>(i)];
    }
    norm_ = 4.0 / (sum * sum);   // a sine of amplitude A peaks at |X| = A * sum / 2
    buf_.assign(static_cast<std::size_t>(n_), {});
    binLo_.assign(kSpectrumBins, 0);
    binHi_.assign(kSpectrumBins, 0);
    binFrac_.assign(kSpectrumBins, -1.0f);
    const double perBin = sampleRate / n_;
    const float edge = std::pow(kSpectrumHighHz / kSpectrumLowHz, 0.5f / static_cast<float>(kSpectrumBins - 1));
    for (int b = 0; b < kSpectrumBins; ++b) {
        const float hz = spectrumBinHz(b);
        const double lo = hz / edge / perBin, hi = hz * edge / perBin;
        const auto ub = static_cast<std::size_t>(b);
        if (hi - lo < 1.0) {   // narrower than an FFT bin: interpolate at the centre
            const double c = hz / perBin;
            binLo_[ub] = std::clamp(static_cast<int>(std::floor(c)), 1, n_ / 2 - 1);
            binFrac_[ub] = static_cast<float>(c - std::floor(c));
        } else {
            binLo_[ub] = std::clamp(static_cast<int>(std::ceil(lo)), 1, n_ / 2);
            binHi_[ub] = std::clamp(static_cast<int>(std::floor(hi)), binLo_[ub], n_ / 2);
        }
    }
    const double framesPerSecond = sampleRate / hop_;
    fallPerFrame_ = static_cast<float>(36.0 / framesPerSecond);
    reset();
}

void SpectrumMeter::reset() noexcept {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    smooth_.fill(kSpectrumFloorDb);
    for (auto& o : out_) o.store(kSpectrumFloorDb, std::memory_order_relaxed);
    pos_ = hopCount_ = 0;
}

void SpectrumMeter::push(const float* L, const float* R, std::uint32_t n) noexcept {
    if (!active_.load(std::memory_order_relaxed) || ring_.empty()) return;
    for (std::uint32_t i = 0; i < n; ++i) {
        ring_[static_cast<std::size_t>(pos_)] = 0.5f * (L[i] + R[i]);
        if (++pos_ >= n_) pos_ = 0;
        if (++hopCount_ >= hop_) { hopCount_ = 0; analyse(); }
    }
}

void SpectrumMeter::analyse() noexcept {
    for (int i = 0; i < n_; ++i)
        buf_[static_cast<std::size_t>(i)] = {ring_[static_cast<std::size_t>((pos_ + i) % n_)] * win_[static_cast<std::size_t>(i)], 0.0f};
    fft_.forward(buf_.data());
    auto powerAt = [&](int k) { return static_cast<double>(std::norm(buf_[static_cast<std::size_t>(k)])) * norm_; };
    for (int b = 0; b < kSpectrumBins; ++b) {
        const auto ub = static_cast<std::size_t>(b);
        double p = 0.0;
        if (binFrac_[ub] >= 0.0f) {
            const int k = binLo_[ub];
            p = powerAt(k) + static_cast<double>(binFrac_[ub]) * (powerAt(k + 1) - powerAt(k));
        } else {
            for (int k = binLo_[ub]; k <= binHi_[ub]; ++k) p = std::max(p, powerAt(k));
        }
        const float db = p > 1e-14 ? std::max(kSpectrumFloorDb, static_cast<float>(10.0 * std::log10(p))) : kSpectrumFloorDb;
        float& s = smooth_[ub];
        s = db > s ? s + 0.6f * (db - s) : std::max(db, s - fallPerFrame_);
        out_[ub].store(s, std::memory_order_relaxed);
    }
    count_.fetch_add(1, std::memory_order_relaxed);
}

float SpectrumMeter::noiseToDisplayDb(double powerPerBin, double sumW2) const noexcept {
    // white noise of variance s2 gives powerPerBin = s2 * sumW2 in the other analysis; here it reads
    // s2 * (sum of this Hann window squared) * norm_ = s2 * (3n/8) * norm_
    const double s2 = powerPerBin / std::max(sumW2, 1e-12);
    const double p = s2 * (3.0 * n_ / 8.0) * norm_;
    return p > 1e-14 ? std::max(kSpectrumFloorDb, static_cast<float>(10.0 * std::log10(p))) : kSpectrumFloorDb;
}

} // namespace hem::rack
