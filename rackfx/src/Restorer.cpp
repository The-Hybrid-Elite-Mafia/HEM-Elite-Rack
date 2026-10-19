#include "hem/rack/Restorer.hpp"

#include "hybrid/fx/Dsp.hpp"
#include "hybrid/fx/Fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace {

using namespace restorer;
using hc::fx::ParamInfo;
using hc::fx::Taper;

const char* const kOnOff[] = {"OFF", "ON"};
const ParamInfo kParams[kCount] = {
    {"reduction", "Noise Reduction", "/10", 0.0f, 10.0f, 5.0f},
    {"focus",     "Focus",           "%", -100.0f, 100.0f, 0.0f},
    {"air",       "Air Preserve",    "/10", 0.0f, 10.0f, 5.0f},
    {"low",       "Range Low",       "Hz", 20.0f, 20000.0f, 20.0f, Taper::Log},
    {"high",      "Range High",      "Hz", 20.0f, 20000.0f, 20000.0f, Taper::Log},
    {"adapt",     "Adapt",           "", 0.0f, 1.0f, 1.0f, Taper::Toggle, kOnOff},
    {"listen",    "Listen",          "", 0.0f, 1.0f, 0.0f, Taper::Toggle, kOnOff},
    {"output",    "Output",          "dB", -24.0f, 12.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"REMOVED", "dB", -30.0f, 0.0f}};

constexpr int kSlots = 8;
constexpr float kBias = 1.6f, kEps = 1e-14f;

class RestorerImpl final : public hc::fx::Effect, public SpectrumSource, public NoiseProfileSource {
public:
    RestorerImpl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return static_cast<std::uint32_t>(n_); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    float noiseFloorDb(int b) const noexcept override {
        return b >= 0 && b < kSpectrumBins ? noisePub_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed) : kSpectrumFloorDb;
    }
    float removedDb() const noexcept override { return meter(0); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        n_ = fs > 64000.0 ? 4096 : 2048;
        hop_ = n_ / 4;
        fft_.prepare(n_);
        const auto N = static_cast<std::size_t>(n_), B = N / 2 + 1;
        win_.resize(N);
        sumW2_ = 0.0;
        for (std::size_t i = 0; i < N; ++i) {
            win_[i] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / n_)));
            sumW2_ += static_cast<double>(win_[i]) * win_[i];
        }
        for (int c = 0; c < 2; ++c) { in_[c].assign(N, 0.0f); out_[c].assign(N, 0.0f); buf_[c].assign(N, {}); }
        for (auto* v : {&power_, &smooth_, &noise_, &gain_, &gainTmp_, &gammaPrev_, &curMin_, &logPrev_, &focusW_, &airMin_, &rangeW_})
            v->assign(B, 0.0f);
        slotMin_.assign(B * kSlots, 0.0f);
        framesPerSlot_ = std::max(1, static_cast<int>(std::lround(0.5 * fs / hop_)));
        // Which STFT bins feed each display bin of the noise curve.
        dispK_.assign(kSpectrumBins, 0.0f);
        for (int d = 0; d < kSpectrumBins; ++d) dispK_[static_cast<std::size_t>(d)] = static_cast<float>(spectrumBinHz(d) * n_ / fs);
        // FOCUS / AIR / RANGE weights are rebuilt only when their knobs move.
        lastFocus_ = lastAir_ = lastLow_ = lastHigh_ = lastReduction_ = -1e9f;
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }

    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) { std::fill(in_[c].begin(), in_[c].end(), 0.0f); std::fill(out_[c].begin(), out_[c].end(), 0.0f); }
        for (auto* v : {&power_, &smooth_, &noise_, &slotMin_, &curMin_, &gammaPrev_, &logPrev_}) std::fill(v->begin(), v->end(), 0.0f);
        std::fill(gain_.begin(), gain_.end(), 1.0f);
        pos_ = hopCount_ = slotFrame_ = slot_ = slotsFilled_ = 0;
        removedAvg_ = 0.0f;
        onset_ = 0.0f;
        outGain_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishMeter(0, 0.0f);
        publishReduction(0.0f);
        for (auto& v : noisePub_) v.store(kSpectrumFloorDb, std::memory_order_relaxed);
        before_.reset();
        after_.reset();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        const float oT = hc::fx::dbToGain(p(Output));
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto ps = static_cast<std::size_t>(pos_);
            in_[0][ps] = L[i];
            in_[1][ps] = R[i];
            const float g = outGain_.next(oT);
            L[i] = out_[0][ps] * g;
            R[i] = out_[1][ps] * g;
            out_[0][ps] = out_[1][ps] = 0.0f;
            if (++pos_ >= n_) pos_ = 0;
            if (++hopCount_ >= hop_) { hopCount_ = 0; frame(); }
        }
        after_.push(L, R, n);
    }

private:
    void rebuildWeights(float reduction, float focus, float air, float lowHz, float highHz) noexcept {
        const auto B = static_cast<std::size_t>(n_ / 2 + 1);
        const float r = reduction / 10.0f, a = air / 10.0f, f = focus / 100.0f;
        const float floorDb = -30.0f * r;
        const float lo = std::min(lowHz, highHz), hi = std::max(lowHz, highHz);
        const float edge = std::pow(2.0f, 1.0f / 3.0f);   // 1/3 octave soft edges
        for (std::size_t k = 0; k < B; ++k) {
            const float hz = std::max(1.0f, static_cast<float>(k * fs_ / n_));
            const float pos = std::clamp(spectrumBinOf(hz) / static_cast<float>(kSpectrumBins - 1), 0.0f, 1.0f);
            focusW_[k] = std::exp2(f * (pos - 0.5f) * 2.0f);   // FOCUS HI: work twice as hard at the top, half at the bottom
            // AIR PRESERVE: above ~6 kHz (ramped from 4 to 8 kHz) never take out as much.
            const float airRamp = std::clamp((std::log2(hz) - std::log2(4000.0f)), 0.0f, 1.0f);
            const float minDb = floorDb * (1.0f - 0.75f * a * airRamp);
            airMin_[k] = hc::fx::dbToGain(minDb);
            // RANGE: 1 inside, 0 outside, cosine edges one third of an octave wide.
            float w = 1.0f;
            if (hz < lo) w = hz <= lo / edge ? 0.0f : 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * std::log(hz * edge / lo) / std::log(edge));
            if (hz > hi) w = hz >= hi * edge ? 0.0f : 0.5f + 0.5f * std::cos(std::numbers::pi_v<float> * std::log(hz / hi) / std::log(edge));
            rangeW_[k] = w;
        }
    }

    void frame() noexcept {
        const int N = n_;
        const auto B = static_cast<std::size_t>(N / 2 + 1);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < N; ++i)
                buf_[c][static_cast<std::size_t>(i)] = {in_[c][static_cast<std::size_t>((pos_ + i) % N)] * win_[static_cast<std::size_t>(i)], 0.0f};
        fft_.forward(buf_[0].data());
        fft_.forward(buf_[1].data());

        const float reduction = p(Reduction), focus = p(Focus), air = p(Air), lowHz = p(LowHz), highHz = p(HighHz);
        if (reduction != lastReduction_ || focus != lastFocus_ || air != lastAir_ || lowHz != lastLow_ || highHz != lastHigh_) {
            rebuildWeights(reduction, focus, air, lowHz, highHz);
            lastReduction_ = reduction; lastFocus_ = focus; lastAir_ = air; lastLow_ = lowHz; lastHigh_ = highHz;
        }
        const bool adapt = p(Adapt) >= 0.5f, listen = p(Listen) >= 0.5f;
        const float r = reduction / 10.0f, a = air / 10.0f;

        // Power per bin, its short average, and how much the 1..8 kHz band jumped (attacks).
        float flux = 0.0f;
        int fluxBins = 0;
        const auto k1 = static_cast<std::size_t>(1000.0 * N / fs_), k8 = std::min(B - 1, static_cast<std::size_t>(8000.0 * N / fs_));
        for (std::size_t k = 0; k < B; ++k) {
            const float pw = 0.5f * (std::norm(buf_[0][k]) + std::norm(buf_[1][k]));
            power_[k] = pw;
            smooth_[k] = 0.6f * smooth_[k] + 0.4f * pw;
            if (smooth_[k] < 1e-30f) smooth_[k] = 0.0f;
            const float lg = std::log(pw + 1e-20f);
            if (k >= k1 && k <= k8) { flux += std::max(0.0f, lg - logPrev_[k]); ++fluxBins; }
            logPrev_[k] = lg;
        }
        const float fluxMean = fluxBins > 0 ? flux / static_cast<float>(fluxBins) : 0.0f;
        onset_ = std::max(onset_ * 0.7f, std::clamp((fluxMean - 0.6f) / 1.2f, 0.0f, 1.0f));

        if (adapt) learnNoise(B);

        float removed = 0.0f, total = 0.0f;
        if (r <= 0.0f) {
            std::fill(gainTmp_.begin(), gainTmp_.end(), 1.0f);
        } else {
            const float over = 1.0f + 1.5f * r;
            for (std::size_t k = 0; k < B; ++k) {
                const float nk = noise_[k] * over * focusW_[k];
                float g = 1.0f;
                if (nk > kEps) {
                    const float gamma = power_[k] / nk;                                   // a-posteriori SNR
                    const float xi = 0.96f * gain_[k] * gain_[k] * gammaPrev_[k] + 0.04f * std::max(gamma - 1.0f, 0.0f);   // decision-directed
                    g = xi / (1.0f + xi);
                    gammaPrev_[k] = std::min(gamma, 1e6f);
                } else {
                    gammaPrev_[k] = 1e6f;
                }
                // Floor (deeper with more reduction, kept higher in the air band and on attacks).
                float mn = airMin_[k];
                mn += (1.0f - mn) * a * 0.6f * onset_;
                g = std::max(g, mn);
                g = 1.0f - rangeW_[k] * (1.0f - g);
                gainTmp_[k] = g;
            }
        }
        for (std::size_t k = 0; k < B; ++k) {
            const float t = 0.25f * gainTmp_[k > 0 ? k - 1 : k] + 0.5f * gainTmp_[k] + 0.25f * gainTmp_[k + 1 < B ? k + 1 : k];
            gain_[k] = t < gain_[k] ? t : gain_[k] + 0.5f * (t - gain_[k]);   // down at once, back up over a few frames
            const float g = listen ? 1.0f - gain_[k] : gain_[k];
            removed += power_[k] * (1.0f - gain_[k] * gain_[k]);
            total += power_[k];
            buf_[0][k] *= g;
            buf_[1][k] *= g;
            if (k > 0 && k < static_cast<std::size_t>(N / 2)) {
                buf_[0][static_cast<std::size_t>(N) - k] = std::conj(buf_[0][k]);
                buf_[1][static_cast<std::size_t>(N) - k] = std::conj(buf_[1][k]);
            }
        }
        fft_.inverse(buf_[0].data());
        fft_.inverse(buf_[1].data());
        const float scale = 0.5f;   // inverse divides by N; 4x square-root Hann overlap sums to 2
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < N; ++i)
                out_[c][static_cast<std::size_t>((pos_ + i) % N)] += buf_[c][static_cast<std::size_t>(i)].real() * win_[static_cast<std::size_t>(i)] * scale;

        const float rem = total > kEps ? 10.0f * std::log10(std::max(1e-3f, 1.0f - removed / total)) : 0.0f;
        removedAvg_ = 0.8f * removedAvg_ + 0.2f * rem;
        publishMeter(0, removedAvg_);
        publishReduction(removedAvg_);
        publishNoise(B);
    }

    void learnNoise(std::size_t B) noexcept {
        for (std::size_t k = 0; k < B; ++k) {
            std::array<float, 13> v{};
            for (int j = -6; j <= 6; ++j)
                v[static_cast<std::size_t>(j + 6)] = smooth_[static_cast<std::size_t>(std::clamp(static_cast<int>(k) + j, 0, static_cast<int>(B) - 1))];
            std::nth_element(v.begin(), v.begin() + 6, v.end());
            const float med = v[6];
            curMin_[k] = slotFrame_ == 0 ? med : std::min(curMin_[k], med);
            float m = curMin_[k];
            for (int s = 0; s < std::min(slotsFilled_, kSlots); ++s) m = std::min(m, slotMin_[k * kSlots + static_cast<std::size_t>(s)]);
            noise_[k] = m * kBias;
        }
        if (++slotFrame_ >= framesPerSlot_) {
            for (std::size_t k = 0; k < B; ++k) slotMin_[k * kSlots + static_cast<std::size_t>(slot_)] = curMin_[k];
            slot_ = (slot_ + 1) % kSlots;
            slotsFilled_ = std::min(kSlots, slotsFilled_ + 1);
            slotFrame_ = 0;
        }
    }

    void publishNoise(std::size_t B) noexcept {
        for (int d = 0; d < kSpectrumBins; ++d) {
            const float kf = dispK_[static_cast<std::size_t>(d)];
            const auto k0 = std::min(B - 2, static_cast<std::size_t>(std::max(0.0f, kf)));
            const float t = std::clamp(kf - static_cast<float>(k0), 0.0f, 1.0f);
            const double nk = (noise_[k0] + t * (noise_[k0 + 1] - noise_[k0])) / kBias;
            noisePub_[static_cast<std::size_t>(d)].store(after_.noiseToDisplayDb(nk, sumW2_), std::memory_order_relaxed);
        }
    }

    double fs_ = 48000.0, sumW2_ = 1024.0;
    int n_ = 2048, hop_ = 512, pos_ = 0, hopCount_ = 0;
    hc::fx::Fft fft_;
    std::vector<float> win_, in_[2], out_[2];
    std::vector<std::complex<float>> buf_[2];
    std::vector<float> power_, smooth_, noise_, gain_, gainTmp_, gammaPrev_, curMin_, slotMin_, logPrev_;
    std::vector<float> focusW_, airMin_, rangeW_, dispK_;
    float lastReduction_ = 0, lastFocus_ = 0, lastAir_ = 0, lastLow_ = 0, lastHigh_ = 0;
    int framesPerSlot_ = 47, slotFrame_ = 0, slot_ = 0, slotsFilled_ = 0;
    float removedAvg_ = 0.0f, onset_ = 0.0f;
    hc::fx::Smoothed outGain_;
    std::array<std::atomic<float>, kSpectrumBins> noisePub_{};
    SpectrumMeter before_, after_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeRestorer() { return std::make_unique<RestorerImpl>(); }

} // namespace hem::rack
