#include "hem/rack/BusGlue.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hem::rack {
namespace glue {

float gainComputerDb(float x, float t, float ratio) noexcept {
    const float slope = 1.0f / ratio - 1.0f;   // dB of change per dB over the threshold (< 0)
    const float over = x - t;
    if (over <= -kKneeDb * 0.5f) return 0.0f;
    if (over >= kKneeDb * 0.5f) return slope * over;
    const float k = over + kKneeDb * 0.5f;      // inside the knee: a smooth quadratic join
    return slope * k * k / (2.0f * kKneeDb);
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
const char* const kRatioN[] = {"2:1", "4:1", "10:1"};
const char* const kAttackN[] = {"0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"};
const char* const kReleaseN[] = {"0.1 s", "0.3 s", "0.6 s", "1.2 s", "AUTO"};
const char* const kScN[] = {"OFF", "60 Hz", "90 Hz", "150 Hz"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -40.0f, 0.0f, -12.0f},
    {"ratio", "Ratio", "", 0, 2, 1, Taper::Choice, kRatioN},
    {"attack", "Attack", "", 0, 5, 3, Taper::Choice, kAttackN},
    {"release", "Release", "", 0, 4, 4, Taper::Choice, kReleaseN},
    {"makeup", "Makeup", "dB", 0.0f, 20.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"schpf", "Sidechain HPF", "", 0, 3, 0, Taper::Choice, kScN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
} // namespace
} // namespace glue

namespace {
using namespace glue;

class BusGlueImpl final : public hc::fx::Effect {
public:
    BusGlueImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }

    void reset() noexcept override {
        sc_[0].clear();
        sc_[1].clear();
        scIndex_ = -1;
        grDb_ = 0.0f;
        work_ = 0.0f;
        makeup_.prepare(fs_, 30, hc::fx::dbToGain(p(Makeup)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold);
        const float ratio = kRatios[std::clamp(static_cast<int>(p(Ratio)), 0, 2)];
        const float attS = kAttackMs[std::clamp(static_cast<int>(p(Attack)), 0, 5)] * 0.001f;
        const int relIdx = std::clamp(static_cast<int>(p(Release)), 0, 4);
        const int sc = std::clamp(static_cast<int>(p(ScHpf)), 0, 3);
        if (sc != scIndex_) {   // the detector's high-pass (never in the audio path)
            scIndex_ = sc;
            if (sc > 0) {
                const auto c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, fs_, kScHpfHz[sc], 0.7071);
                sc_[0].c = c;
                sc_[1].c = c;
            }
        }
        const float attC = static_cast<float>(std::exp(-1.0 / (fs_ * attS)));
        // AUTO release: "work" charges (2 s) while it is pulling more than 2 dB and drains (3 s) when it is not;
        // the release runs from 0.1 s (short hits) to 1.2 s (after sustained work).
        const float chargeK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 2.0)));
        const float drainK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 3.0)));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float dl = L[i], dr = R[i];
            if (sc > 0) { dl = sc_[0].tick(dl); dr = sc_[1].tick(dr); }
            const float level = std::max(std::fabs(dl), std::fabs(dr));   // stereo-linked peak
            const float levelDb = 20.0f * std::log10(std::max(level, 1e-9f));
            const float target = gainComputerDb(levelDb, thr, ratio);
            float relS;
            if (relIdx < 4) relS = kReleaseS[relIdx];
            else relS = 0.1f + 1.1f * work_;
            const float c = target < grDb_ ? attC : static_cast<float>(std::exp(-1.0 / (fs_ * relS)));
            grDb_ = target + c * (grDb_ - target);
            work_ += target < -2.0f ? (1.0f - work_) * chargeK : -work_ * drainK;
            most = std::min(most, grDb_);
            const float g = hc::fx::dbToGain(grDb_) * makeup_.next(hc::fx::dbToGain(p(Makeup)));
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (L[i] * g - L[i])) * o;
            R[i] = (R[i] + m * (R[i] * g - R[i])) * o;
        }
        if (grDb_ > -1e-6f) grDb_ = 0.0f;
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    hc::fx::Biquad sc_[2];
    int scIndex_ = -1;
    float grDb_ = 0.0f, work_ = 0.0f;
    hc::fx::Smoothed makeup_, mix_, out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeBusGlue() { return std::make_unique<BusGlueImpl>(); }

} // namespace hem::rack
