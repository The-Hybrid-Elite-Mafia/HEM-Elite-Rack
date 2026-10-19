#include "hem/rack/Dynamics.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>

namespace hem::rack {
namespace opto {

// The cell starts working when the sidechain reaches 0 dBFS (PEAK REDUCTION raises the sidechain
// up to 40 dB, so the more you turn it, the lower the signal it starts on).
float gainComputerDb(float x, bool limit) noexcept {
    const float K = limit ? 4.0f : 10.0f, ratio = limit ? 12.0f : 3.0f;
    const float over = x;
    if (over <= -K * 0.5f) return 0.0f;
    const float slope = 1.0f / ratio - 1.0f;
    if (over >= K * 0.5f) return slope * over;
    const float k = over + K * 0.5f;
    return slope * k * k / (2.0f * K);
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
const char* const kModeN[] = {"COMPRESS", "LIMIT"};
const ParamInfo kParams[kCount] = {
    {"peakreduction", "Peak Reduction", "%", 0.0f, 100.0f, 30.0f},
    {"gain", "Gain", "dB", 0.0f, 40.0f, 0.0f},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"emphasis", "Emphasis", "%", 0.0f, 100.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
} // namespace
} // namespace opto

namespace {
using namespace opto;

class OptoImpl final : public hc::fx::Effect {
public:
    OptoImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        attC_ = static_cast<float>(std::exp(-1.0 / (fs_ * 0.010)));
        fastC_ = static_cast<float>(std::exp(-1.0 / (fs_ * 0.060)));
        chargeK_ = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 3.0)));
        drainK_ = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 8.0)));
        reset();
    }

    void reset() noexcept override {
        sc_[0].clear();
        sc_[1].clear();
        emph_ = -1.0f;
        fast_ = slow_ = 0.0f;
        memory_ = 0.0f;
        gain_.prepare(fs_, 30, hc::fx::dbToGain(p(Gain)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Emphasis) != emph_) {   // the detector's ear: a high shelf, up to +10 dB at 2 kHz
            emph_ = p(Emphasis);
            const auto c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighShelf, fs_, 2000.0, 0.7071, emph_ * 0.1);
            sc_[0].c = c;
            sc_[1].c = c;
        }
        const bool limit = p(Mode) >= 0.5f;
        const float scDb = p(PeakReduction) * 0.4f;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = sc_[0].tick(L[i]), dr = sc_[1].tick(R[i]);
            const float level = std::max(std::fabs(dl), std::fabs(dr));
            const float target = gainComputerDb(20.0f * std::log10(std::max(level, 1e-9f)) + scDb, limit);
            // the light cell: ~10 ms attack; half lets go in ~60 ms, half slowly (0.5 s .. 5 s with memory)
            const float slowC = static_cast<float>(std::exp(-1.0 / (fs_ * (0.5 + 4.5 * memory_))));
            fast_ = target + (target < fast_ ? attC_ : fastC_) * (fast_ - target);
            slow_ = target + (target < slow_ ? attC_ : slowC) * (slow_ - target);
            const float gr = 0.5f * (fast_ + slow_);
            memory_ += target < -3.0f ? (1.0f - memory_) * chargeK_ : -memory_ * drainK_;
            most = std::min(most, gr);
            const float g = hc::fx::dbToGain(gr) * gain_.next(hc::fx::dbToGain(p(Gain)));
            const float m = mix_.next(p(Mix) * 0.01f);
            L[i] += m * (L[i] * g - L[i]);
            R[i] += m * (R[i] * g - R[i]);
        }
        if (fast_ > -1e-6f) fast_ = 0.0f;
        if (slow_ > -1e-6f) slow_ = 0.0f;
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    hc::fx::Biquad sc_[2];
    float emph_ = -1.0f, fast_ = 0.0f, slow_ = 0.0f, memory_ = 0.0f;
    float attC_ = 0, fastC_ = 0, chargeK_ = 0, drainK_ = 0;
    hc::fx::Smoothed gain_, mix_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeOpto() { return std::make_unique<OptoImpl>(); }

} // namespace hem::rack
