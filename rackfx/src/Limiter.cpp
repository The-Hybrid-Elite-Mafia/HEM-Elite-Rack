#include "hem/rack/Limiter.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace {
using namespace limiter;
using hc::fx::ParamInfo;
using hc::fx::Taper;
using OS = hc::fx::Oversampler4;

const char* const kOnOff[] = {"OFF", "ON"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", 0.0f, 24.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -6.0f, 0.0f, -1.0f},
    {"release", "Release", "ms", 10.0f, 1000.0f, 120.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"truepeak", "True Peak", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"link", "Stereo Link", "%", 0.0f, 100.0f, 100.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"LOUDNESS", "LUFS", -40.0f, 0.0f}, {"TRUE PEAK", "dBTP", -40.0f, 3.0f}};
constexpr std::uint32_t kWin = kLookahead + 1;

class LimiterImpl final : public hc::fx::Effect {
public:
    LimiterImpl() : tp_{OS(1.0), OS(1.0)}, outTp_{OS(1.0), OS(1.0)} { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override { return i != Release || p(AutoRelease) < 0.5f; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        up_.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& d : audio_) d.prepare(kLatency + 1);
        for (auto& d : plain_) d.prepare(kDetectDelay + 1);
        for (auto& h : hold_) h.assign(kWin, 1.0f);
        for (auto& b : box_) b.assign(kWin, 1.0f);
        peak_[0].assign(mb, 0.0f);
        peak_[1].assign(mb, 0.0f);
        // The loudness meter's K-weighting: exactly the ITU-R BS.1770 filters (the same design The Hybrid
        // Core's export loudness uses), so the readout agrees with what streaming services measure.
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(std::numbers::pi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            hc::fx::BiquadCoefs c;
            c.b0 = (Vh + Vb * K / Q + K * K) / a0; c.b1 = 2.0 * (K * K - Vh) / a0; c.b2 = (Vh - Vb * K / Q + K * K) / a0;
            c.a1 = 2.0 * (K * K - 1.0) / a0; c.a2 = (1.0 - K / Q + K * K) / a0;
            kShelf_.set(c);
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(std::numbers::pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
            hc::fx::BiquadCoefs c;
            c.b0 = 1.0; c.b1 = -2.0; c.b2 = 1.0; c.a1 = 2.0 * (K * K - 1.0) / a0; c.a2 = (1.0 - K / Q + K * K) / a0;
            kHp_.set(c);
        }
        reset();
    }

    void reset() noexcept override {
        for (auto& o : tp_) o.clear();
        for (auto& o : outTp_) o.clear();
        for (auto& d : audio_) d.clear();
        for (auto& d : plain_) d.clear();
        for (auto& h : hold_) std::fill(h.begin(), h.end(), 1.0f);
        for (auto& b : box_) std::fill(b.begin(), b.end(), 1.0f);
        boxSum_[0] = boxSum_[1] = static_cast<double>(kWin);
        rel_[0] = rel_[1] = 1.0f;
        pos_ = 0;
        memory_ = 0.0f;
        kShelf_.clear();
        kHp_.clear();
        ms_ = 0.0;
        metered_ = 0;
        tpHold_ = -100.0f;
        tpAge_ = 0.0;
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        ceil_.prepare(fs_, 30, hc::fx::dbToGain(p(Ceiling)));
        publishReduction(0.0f);
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        float* io[2] = {L, R};
        // 1. push, and the detector: true peaks (4x) or plain sample peaks, both about 12 samples late
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            L[i] *= g;
            R[i] *= g;
        }
        const bool truePeak = p(TruePeak) >= 0.5f;
        for (int c = 0; c < 2; ++c) {
            auto& pk = peak_[static_cast<std::size_t>(c)];
            tp_[c].up(io[c], up_.data(), n);   // always run, so switching TRUE PEAK never jumps
            for (std::uint32_t i = 0; i < n; ++i) {
                const float* q = &up_[i * OS::kFactor];
                const float t = std::max({std::fabs(q[0]), std::fabs(q[1]), std::fabs(q[2]), std::fabs(q[3])});
                const float s = std::fabs(plain_[c].process(io[c][i], kDetectDelay));
                pk[i] = truePeak ? t : s;
            }
        }
        // 2. the gain: required gain -> held over the look-ahead -> release -> averaged over the look-ahead
        const float link = p(Link) * 0.01f;
        const bool autoRel = p(AutoRelease) >= 0.5f;
        const float manualK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * p(Release) * 0.001)));
        const float chargeK = static_cast<float>(1.0 - std::exp(-1.0 / fs_));
        const float drainK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 2.0)));
        float most = 1.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float c = ceil_.next(hc::fx::dbToGain(p(Ceiling)));
            const float both = std::max(peak_[0][i], peak_[1][i]);
            float relK = manualK;
            if (autoRel) relK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * (0.03 + 0.4 * memory_))));
            float gOut[2];
            for (int ch = 0; ch < 2; ++ch) {
                const float pc = link * both + (1.0f - link) * peak_[static_cast<std::size_t>(ch)][i];
                const float need = pc > c ? c / pc : 1.0f;
                auto& h = hold_[static_cast<std::size_t>(ch)];
                h[pos_] = need;
                float held = 1.0f;
                for (float v : h) held = std::min(held, v);
                float& rl = rel_[static_cast<std::size_t>(ch)];
                rl = held < rl ? held : rl + (held - rl) * relK;
                auto& b = box_[static_cast<std::size_t>(ch)];
                boxSum_[ch] += static_cast<double>(rl) - b[pos_];
                b[pos_] = rl;
                gOut[ch] = static_cast<float>(boxSum_[ch] / static_cast<double>(kWin));
            }
            pos_ = (pos_ + 1) % kWin;
            const float gmin = std::min(gOut[0], gOut[1]);
            memory_ += gmin < 0.79f ? (1.0f - memory_) * chargeK : -memory_ * drainK;   // working harder than 2 dB
            most = std::min(most, gmin);
            for (int ch = 0; ch < 2; ++ch) {
                float y = audio_[ch].process(io[ch][i], kLatency) * std::min(gOut[ch], 1.0f);
                y = std::clamp(y, -c, c);   // the safety net: no sample ever passes the ceiling
                io[ch][i] = y;
            }
        }
        if (pos_ == 0) {   // re-add the window once per cycle so rounding never drifts
            for (int ch = 0; ch < 2; ++ch) {
                double s = 0;
                for (float v : box_[static_cast<std::size_t>(ch)]) s += v;
                boxSum_[ch] = s;
            }
        }
        publishReduction(20.0f * std::log10(std::max(most, 1e-6f)));
        meters(L, R, n);
    }

private:
    // Output loudness (K-weighted, 3 s) and true peak (held 1 s, then falling 20 dB / s).
    void meters(const float* L, const float* R, std::uint32_t n) noexcept {
        double s = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = kHp_.l.tick(kShelf_.l.tick(L[i])), r = kHp_.r.tick(kShelf_.r.tick(R[i]));
            s += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        // The first 3 s: the plain average so far (so it reads right from the start); then a 3 s window.
        metered_ += n;
        const double k = std::max(1.0 - std::exp(-static_cast<double>(n) / (fs_ * 3.0)), static_cast<double>(n) / static_cast<double>(metered_));
        ms_ += (s / std::max(n, 1u) - ms_) * k;
        publishMeter(0, static_cast<float>(ms_ > 1e-12 ? -0.691 + 10.0 * std::log10(ms_) : -100.0));
        float tp = 0.0f;
        const float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            outTp_[c].up(io[c], up_.data(), n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) tp = std::max(tp, std::fabs(up_[i]));
        }
        const float tpDb = 20.0f * std::log10(std::max(tp, 1e-6f));
        tpAge_ += static_cast<double>(n) / fs_;
        if (tpDb >= tpHold_) { tpHold_ = tpDb; tpAge_ = 0.0; }
        else if (tpAge_ > 1.0) tpHold_ = std::max(tpDb, tpHold_ - 20.0f * static_cast<float>(n / fs_));
        publishMeter(1, tpHold_);
    }

    double fs_ = 48000.0;
    OS tp_[2], outTp_[2];
    std::vector<float> up_;
    std::array<std::vector<float>, 2> peak_, hold_, box_;
    hc::fx::DelayLine audio_[2], plain_[2];
    std::array<double, 2> boxSum_{};
    std::array<float, 2> rel_{};
    std::uint32_t pos_ = 0;
    float memory_ = 0.0f, tpHold_ = -100.0f;
    double tpAge_ = 0.0, ms_ = 0.0;
    std::uint64_t metered_ = 0;
    hc::fx::Biquad2 kShelf_, kHp_;
    hc::fx::Smoothed in_, ceil_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeLimiter() { return std::make_unique<LimiterImpl>(); }

} // namespace hem::rack
