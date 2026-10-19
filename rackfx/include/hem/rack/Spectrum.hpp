#pragma once
// =============================================================================
//  SpectrumMeter — the before / after spectrum on the faceplates.
//
//  The audio thread pushes samples (stereo, summed to mono); every quarter window a
//  Hann-windowed FFT is folded into kSpectrumBins log-spaced display bins (20 Hz .. 20 kHz,
//  the loudest FFT bin inside each), smoothed (rises fast, falls ~36 dB/s) and published
//  as relaxed atomics. Calibration: a full-scale sine reads 0 dB in its bin.
//
//  Threads: prepare() — message thread (allocates). push() — audio thread (no allocation,
//  no locks); it does nothing while setActive(false) (no faceplate open = no CPU spent).
//  level() / frames() — any thread.
// =============================================================================
#include "hybrid/fx/Fft.hpp"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

namespace hem::rack {

inline constexpr int   kSpectrumBins = 160;
inline constexpr float kSpectrumLowHz = 20.0f, kSpectrumHighHz = 20000.0f;
inline constexpr float kSpectrumFloorDb = -120.0f;

// Centre frequency of display bin i (log spaced).
[[nodiscard]] float spectrumBinHz(int i) noexcept;
// Display bin (fractional, may be outside 0..kSpectrumBins-1) for a frequency.
[[nodiscard]] float spectrumBinOf(float hz) noexcept;

class SpectrumMeter {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setActive(bool on) noexcept { active_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return active_.load(std::memory_order_relaxed); }

    // Audio thread.
    void push(const float* left, const float* right, std::uint32_t frames) noexcept;

    // Any thread.
    [[nodiscard]] float level(int bin) const noexcept { return out_[static_cast<std::size_t>(bin)].load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t frames() const noexcept { return count_.load(std::memory_order_relaxed); }
    // Converts a per-FFT-bin noise power from another analysis (window sum of squares `sumW2`)
    // to this display's dB scale, so a noise floor can be drawn on the same graph.
    [[nodiscard]] float noiseToDisplayDb(double powerPerBin, double sumW2) const noexcept;
    [[nodiscard]] int fftSize() const noexcept { return n_; }

private:
    void analyse() noexcept;

    double sampleRate_ = 48000.0;
    int n_ = 4096, hop_ = 1024, pos_ = 0, hopCount_ = 0;
    hc::fx::Fft fft_;
    std::vector<float> ring_, win_;
    std::vector<std::complex<float>> buf_;
    std::vector<int> binLo_, binHi_;       // FFT bin range per display bin
    std::vector<float> binFrac_;           // < 0: range; >= 0: interpolate between binLo_ and binLo_+1
    std::array<float, kSpectrumBins> smooth_{};
    std::array<std::atomic<float>, kSpectrumBins> out_{};
    std::atomic<std::uint32_t> count_{0};
    std::atomic<bool> active_{false};
    double norm_ = 1.0;                    // |X|^2 -> sine amplitude^2
    float fallPerFrame_ = 0.75f;
};

// Effects that draw a spectrum on their faceplate expose it through this interface.
class SpectrumSource {
public:
    virtual ~SpectrumSource() = default;
    [[nodiscard]] virtual SpectrumMeter& spectrumBefore() noexcept = 0;
    [[nodiscard]] virtual SpectrumMeter& spectrumAfter() noexcept = 0;
};

} // namespace hem::rack
