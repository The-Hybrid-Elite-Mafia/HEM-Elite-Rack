// THE DON — old-school broadcast bus compressor, feedback detector (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace thedon;

const char* const kThresholdN[] = {"-24 dB", "-22 dB", "-20 dB", "-18 dB", "-16 dB", "-14 dB", "-12 dB", "-10 dB", "-8 dB", "-6 dB", "-4 dB",
                                   "-2 dB", "0 dB", "+2 dB", "+4 dB", "+6 dB", "+8 dB", "+10 dB", "+12 dB", "+14 dB", "+16 dB"};
const char* const kRatioN[] = {"1:1", "2:1", "3:1", "5:1", "LIMIT"};
const char* const kDecayN[] = {"1", "2", "4", "8", "16", "32"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "", 0, 20, 20, Taper::Choice, kThresholdN},
    {"ratio", "Ratio", "", 0, 4, 2, Taper::Choice, kRatioN},
    {"decay", "Decay", "", 0, 5, 2, Taper::Choice, kDecayN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"meter", "Meter", "", 0, 2, 1, Taper::Choice, kMeterN},
    {"analog", "Analog", "", 0, 2, 0, Taper::Choice, kMainsN},
    {"vucal", "VU Calibrate", "dB", 8.0f, 24.0f, 18.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};
constexpr float kAttackMs = 2.0f;     // fixed, as on the hardware
constexpr float kWarmth = 0.3f;       // the output stage's even harmonics

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        dcK_ = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * 8.0 / fs));
        reset();
    }
    void reset() noexcept override {
        env_ = 0.0f;
        gr_ = 0.0f;
        lastL_ = lastR_ = 0.0f;
        dcL_ = dcR_ = 0.0f;
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int ri = std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 4);
        const int di = std::clamp(static_cast<int>(std::lround(p(Decay))), 0, 5);
        const float cal = p(VuCal);
        // THRESHOLD is in dB around 0 VU: with VU CALIBRATE 18, +16 = -2 dBFS
        const float thr = -24.0f + 2.0f * std::clamp(std::round(p(Threshold)), 0.0f, 20.0f) - cal;
        const float ratio = kRatios[ri];
        const float att = coefMs(fs_, kAttackMs);
        const float rel = coefMs(fs_, kDecayMs[di]);
        const float envRel = coefMs(fs_, 8.0);   // an averaging detector, not a peak one: the old way
        const int mains = static_cast<int>(std::lround(p(Analog)));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            vuIn_.tick(L[i], R[i]);
            // the detector listens to what came OUT (feedback): it can never fully "catch" a peak, so
            // it squeezes softly and breathes with the music
            const float x = std::max(std::fabs(lastL_), std::fabs(lastR_));
            env_ = x > env_ ? x + att * (env_ - x) : x + envRel * (env_ - x);
            // seen from the output, a slope s over the threshold acts on the input as s / (1 - s): so to
            // land on the ratio the switch says, the loop works with the slope that gives it
            const float eff = 1.0f / ratio - 1.0f, loopSlope = std::max(eff / (1.0f + eff), -30.0f);
            const float over = dbOf(env_) - thr, knee = ri == 4 ? 3.0f : 8.0f;
            float ov = over;
            if (over <= -knee * 0.5f) ov = 0.0f;
            else if (over < knee * 0.5f) { const float k = over + knee * 0.5f; ov = k * k / (2.0f * knee); }
            const float target = std::max(loopSlope * ov, -40.0f);
            gr_ = follow(gr_, target, att, rel);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_);
            float yl = L[i] * g, yr = R[i] * g;
            lastL_ = yl;
            lastR_ = yr;
            // the output stage: even harmonics that grow with level (bounded), DC taken out
            yl += kWarmth * 0.25f * yl * yl / (1.0f + std::fabs(yl));
            yr += kWarmth * 0.25f * yr * yr / (1.0f + std::fabs(yr));
            dcL_ += (yl - dcL_) * dcK_;
            dcR_ += (yr - dcR_) * dcK_;
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            yl = (yl - dcL_) * o;
            yr = (yr - dcR_) * o;
            hum_.tick(mains, 1.0f, yl, yr);
            L[i] = yl;
            R[i] = yr;
            vuOut_.tick(yl, yr);
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
        publishMeter(0, vuIn_.vu(cal));
        publishMeter(1, vuOut_.vu(cal));
    }

private:
    double fs_ = 48000.0;
    float env_ = 0, gr_ = 0, lastL_ = 0, lastR_ = 0, dcL_ = 0, dcR_ = 0, dcK_ = 0;
    Hum hum_;
    Vu vuIn_, vuOut_;
    hc::fx::Smoothed out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTheDon() { return std::make_unique<Impl>(); }

} // namespace hem::rack
