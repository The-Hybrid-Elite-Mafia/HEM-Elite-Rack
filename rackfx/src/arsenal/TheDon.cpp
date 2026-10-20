// THE DON — old-school broadcast bus compressor, feedback detector (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace thedon;

const char* const kRatioN[] = {"1.5", "2", "3", "4", "6", "LIMIT"};
const char* const kAttackN[] = {"FAST", "MEDIUM", "SLOW"};
const char* const kDecayN[] = {"0.1 s", "0.25 s", "0.5 s", "1 s", "2 s", "AUTO"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -40.0f, 0.0f, -14.0f},
    {"ratio", "Ratio", "", 0, 5, 1, Taper::Choice, kRatioN},
    {"attack", "Attack", "", 0, 2, 1, Taper::Choice, kAttackN},
    {"decay", "Decay", "", 0, 5, 2, Taper::Choice, kDecayN},
    {"warmth", "Warmth", "%", 0.0f, 100.0f, 30.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        mem_.set(fs, 2.0, 4.0);
        dcK_ = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * 8.0 / fs));
        reset();
    }
    void reset() noexcept override {
        env_ = 0.0f;
        gr_ = 0.0f;
        lastL_ = lastR_ = 0.0f;
        dcL_ = dcR_ = 0.0f;
        mem_.v = 0.0f;
        warm_.prepare(fs_, 30, p(Warmth) * 0.01f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int ri = std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 5);
        const int ai = std::clamp(static_cast<int>(std::lround(p(Attack))), 0, 2);
        const int di = std::clamp(static_cast<int>(std::lround(p(Decay))), 0, 5);
        const float thr = p(Threshold), ratio = kRatios[ri];
        const float att = coefMs(fs_, kAttackMs[ai]);
        const float envRel = coefMs(fs_, 8.0);   // an averaging detector, not a peak one: the old way
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            // the detector listens to what came OUT (feedback): it can never fully "catch" a peak, so
            // it squeezes softly and breathes with the music
            const float x = std::max(std::fabs(lastL_), std::fabs(lastR_));
            env_ = x > env_ ? x + att * (env_ - x) : x + envRel * (env_ - x);
            // seen from the output, a slope s over the threshold acts on the input as s / (1 - s): so to
            // land on the ratio the switch says, the loop works with the slope that gives it
            const float eff = 1.0f / ratio - 1.0f, loopSlope = std::max(eff / (1.0f + eff), -30.0f);
            const float over = dbOf(env_) - thr, knee = ri == 5 ? 3.0f : 8.0f;
            float ov = over;
            if (over <= -knee * 0.5f) ov = 0.0f;
            else if (over < knee * 0.5f) { const float k = over + knee * 0.5f; ov = k * k / (2.0f * knee); }
            const float target = std::max(loopSlope * ov, -40.0f);
            const double relMs = di < 5 ? kDecayMs[di] : 300.0 + 2700.0 * mem_.v;
            gr_ = follow(gr_, target, att, coefMs(fs_, relMs));
            mem_.tick(gr_ < -2.0f);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_);
            float yl = L[i] * g, yr = R[i] * g;
            lastL_ = yl;
            lastR_ = yr;
            // the output stage: even harmonics that grow with level (bounded), DC taken out
            const float w = warm_.next(p(Warmth) * 0.01f) * 0.25f;
            yl += w * yl * yl / (1.0f + std::fabs(yl));
            yr += w * yr * yr / (1.0f + std::fabs(yr));
            dcL_ += (yl - dcL_) * dcK_;
            dcR_ += (yr - dcR_) * dcK_;
            yl -= dcL_;
            yr -= dcR_;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (yl - L[i])) * o;
            R[i] = (R[i] + m * (yr - R[i])) * o;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    float env_ = 0, gr_ = 0, lastL_ = 0, lastR_ = 0, dcL_ = 0, dcR_ = 0, dcK_ = 0;
    Memory mem_;
    hc::fx::Smoothed warm_, mix_, out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTheDon() { return std::make_unique<Impl>(); }

} // namespace hem::rack
