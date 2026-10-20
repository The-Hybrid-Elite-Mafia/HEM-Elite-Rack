// HIGH ROLLER — five-band enhancer: lift and width per band (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace highroller {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"lift1", "Sub Lift", "%", 0.0f, 100.0f, 0.0f},
    {"lift2", "Low Lift", "%", 0.0f, 100.0f, 0.0f},
    {"lift3", "Mid Lift", "%", 0.0f, 100.0f, 0.0f},
    {"lift4", "High Lift", "%", 0.0f, 100.0f, 0.0f},
    {"lift5", "Air Lift", "%", 0.0f, 100.0f, 0.0f},
    {"width1", "Sub Width", "%", 0.0f, 200.0f, 100.0f},
    {"width2", "Low Width", "%", 0.0f, 200.0f, 100.0f},
    {"width3", "Mid Width", "%", 0.0f, 200.0f, 100.0f},
    {"width4", "High Width", "%", 0.0f, 200.0f, 100.0f},
    {"width5", "Air Width", "%", 0.0f, 200.0f, 100.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
// release per band: slower in the lows so the lift doesn't wobble
constexpr double kRelMs[kBands] = {160.0, 120.0, 90.0, 70.0, 60.0};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& x : xo_) x.design(fs, kSplitHz, kBands);
        reset();
    }
    void reset() noexcept override {
        for (auto& x : xo_) x.clear();
        for (int b = 0; b < kBands; ++b) {
            env_[b] = 0.0f;
            gr_[b] = 0.0f;
            lift_[b].prepare(fs_, 30, p(Lift1 + b) * 0.01f);
            width_[b].prepare(fs_, 30, p(Width1 + b) * 0.01f);
        }
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        float att[kBands], rel[kBands];
        for (int b = 0; b < kBands; ++b) { att[b] = coefMs(fs_, 2.0); rel[b] = coefMs(fs_, kRelMs[b]); }
        const float envRel = coefMs(fs_, 5.0);
        for (std::uint32_t i = 0; i < n; ++i) {
            float bl[kBands], br[kBands];
            xo_[0].split(L[i], bl);
            xo_[1].split(R[i], br);
            float yl = 0.0f, yr = 0.0f, dl = 0.0f, dr = 0.0f;
            for (int b = 0; b < kBands; ++b) {
                dl += bl[b];
                dr += br[b];
                // LIFT: the band squeezed hard (6:1 from -40 dB) and brought back up, added in
                const float x = std::max(std::fabs(bl[b]), std::fabs(br[b]));
                env_[b] = x > env_[b] ? x : x + envRel * (env_[b] - x);
                gr_[b] = follow(gr_[b], downDb(dbOf(env_[b]), -40.0f, 6.0f, 6.0f), att[b], rel[b]);
                const float a = lift_[b].next(p(Lift1 + b) * 0.01f) * 0.5f * hc::fx::dbToGain(gr_[b] + 12.0f);
                float l = bl[b] * (1.0f + a), r = br[b] * (1.0f + a);
                // WIDTH of this band
                const float w = width_[b].next(p(Width1 + b) * 0.01f);
                const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
                yl += m + s;
                yr += m - s;
            }
            const float mx = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (dl + mx * (yl - dl)) * o;
            R[i] = (dr + mx * (yr - dr)) * o;
        }
        for (int b = 0; b < kBands; ++b) if (gr_[b] > -1e-6f && gr_[b] < 0.0f) gr_[b] = 0.0f;
    }

private:
    double fs_ = 48000.0;
    Crossover<kBands> xo_[2];
    float env_[kBands]{}, gr_[kBands]{};
    hc::fx::Smoothed lift_[kBands], width_[kBands], mix_, out_;
};

} // namespace
} // namespace highroller

std::unique_ptr<hc::fx::Effect> makeHighRoller() { return std::make_unique<highroller::Impl>(); }

} // namespace hem::rack
