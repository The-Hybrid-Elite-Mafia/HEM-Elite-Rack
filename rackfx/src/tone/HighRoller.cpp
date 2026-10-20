// HIGH ROLLER — five-band parallel enhancer: a processed copy per band mixed with the untouched sound (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace highroller {
namespace {
using namespace tone;

#define HEM_BAND(n, name)                                                                          \
    {"gain" #n, name " Gain", "dB", 0.0f, 48.0f, 0.0f},                                            \
    {"width" #n, name " Width", "x", 0.0f, 3.0f, 1.0f},                                            \
    {"solo" #n, name " Solo", "", 0, 1, 0, Taper::Toggle, kOnOff},                                 \
    {"bypass" #n, name " Bypass", "", 0, 1, 0, Taper::Toggle, kOnOff}
const ParamInfo kParams[kCount] = {
    HEM_BAND(1, "Low"), HEM_BAND(2, "Low Mid"), HEM_BAND(3, "Mid"), HEM_BAND(4, "High Mid"), HEM_BAND(5, "High"),
    {"cross1", "Crossover 1", "Hz", 20.0f, 18000.0f, 90.0f, Taper::Log},
    {"cross2", "Crossover 2", "Hz", 20.0f, 18000.0f, 500.0f, Taper::Log},
    {"cross3", "Crossover 3", "Hz", 20.0f, 18000.0f, 2594.0f, Taper::Log},
    {"cross4", "Crossover 4", "Hz", 20.0f, 18000.0f, 9310.0f, Taper::Log},
    {"input", "Input", "dB", -18.0f, 18.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"direct", "Direct", "dB", -45.0f, 0.0f, 0.0f},
    {"punch", "Punch", "", 0.0f, 5.0f, 3.0f},
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {{"LOW", "dB", 0.0f, 48.0f}, {"LOW MID", "dB", 0.0f, 48.0f}, {"MID", "dB", 0.0f, 48.0f},
                                     {"HIGH MID", "dB", 0.0f, 48.0f}, {"HIGH", "dB", 0.0f, 48.0f}};
constexpr double kRelMs[kBands] = {160.0, 120.0, 90.0, 70.0, 60.0};   // slower in the lows: no wobble
constexpr float kSqueezeDb = -40.0f, kSqueezeRatio = 10.0f;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        cross_.fill(-1.0f);
        design();
        for (auto& x : xo_) x.clear();
        for (int b = 0; b < kBands; ++b) {
            env_[b] = gr_[b] = 0.0f;
            proc_[b].prepare(fs_, 30, procGain(b));
            width_[b].prepare(fs_, 30, p(band(b, Width)));
            heard_[b].prepare(fs_, 30, heard(b));
            publishMeter(b, 0.0f);
        }
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        dir_.prepare(fs_, 30, directGain());
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float att = coefMs(fs_, 0.3 * std::pow(4.0, static_cast<double>(p(Punch)))), envRel = coefMs(fs_, 5.0);
        float rel[kBands], wProc[kBands], wWidth[kBands], wHeard[kBands], lift[kBands]{};
        for (int b = 0; b < kBands; ++b) {
            rel[b] = coefMs(fs_, kRelMs[b]);
            wProc[b] = procGain(b);
            wWidth[b] = p(band(b, Width));
            wHeard[b] = heard(b);
        }
        const float wIn = hc::fx::dbToGain(p(Input)), wDir = directGain(), wOut = hc::fx::dbToGain(p(Output));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn), d = dir_.next(wDir);
            float bl[kBands], br[kBands];
            xo_[0].split(L[i] * gi, bl);
            xo_[1].split(R[i] * gi, br);
            float yl = 0.0f, yr = 0.0f;
            for (int b = 0; b < kBands; ++b) {
                // the processed copy: squeezed hard (10:1 from -40 dB), then rounded
                const float x = std::max(std::fabs(bl[b]), std::fabs(br[b]));
                env_[b] = x > env_[b] ? x + att * (env_[b] - x) : x + envRel * (env_[b] - x);
                gr_[b] = follow(gr_[b], downDb(dbOf(env_[b]), kSqueezeDb, kSqueezeRatio, 6.0f), att, rel[b]);
                const float sq = hc::fx::dbToGain(gr_[b]), pg = proc_[b].next(wProc[b]);
                const float pl = 0.25f * std::tanh(4.0f * bl[b] * sq) * pg, pr = 0.25f * std::tanh(4.0f * br[b] * sq) * pg;
                lift[b] = std::max(lift[b], x > 1e-6f ? dbOf((std::fabs(pl) + std::fabs(pr)) * 0.5f + x) - dbOf(x) : 0.0f);
                float l = d * bl[b] + pl, r = d * br[b] + pr;
                const float w = width_[b].next(wWidth[b]), h = heard_[b].next(wHeard[b]);
                const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
                yl += (m + s) * h;
                yr += (m - s) * h;
            }
            const float o = out_.next(wOut);
            L[i] = yl * o;
            R[i] = yr * o;
        }
        for (int b = 0; b < kBands; ++b) publishMeter(b, lift[b]);
    }

private:
    // GAIN 0..48 dB puts the processed copy at -24..+24 dB: at 0 it is far under the music
    float procGain(int b) const noexcept { return p(band(b, Bypass)) >= 0.5f ? 0.0f : hc::fx::dbToGain(p(band(b, Gain)) - 24.0f); }
    float directGain() const noexcept { return p(Direct) <= -44.9f ? 0.0f : hc::fx::dbToGain(p(Direct)); }
    // with any band soloed, only the soloed ones are heard
    float heard(int b) const noexcept {
        bool any = false;
        for (int k = 0; k < kBands; ++k) any = any || p(band(k, Solo)) >= 0.5f;
        return !any || p(band(b, Solo)) >= 0.5f ? 1.0f : 0.0f;
    }
    // the crossovers, each kept between its neighbours
    void design() noexcept {
        std::array<float, kBands - 1> c{p(Cross1), p(Cross2), p(Cross3), p(Cross4)};
        for (int k = 1; k < kBands - 1; ++k) c[static_cast<std::size_t>(k)] = std::max(c[static_cast<std::size_t>(k)], c[static_cast<std::size_t>(k - 1)] * 1.01f);
        if (c == cross_) return;
        cross_ = c;
        for (auto& x : xo_) x.design(fs_, cross_.data(), kBands);
    }

    double fs_ = 48000.0;
    Crossover<kBands> xo_[2];
    std::array<float, kBands - 1> cross_{};
    float env_[kBands]{}, gr_[kBands]{};
    hc::fx::Smoothed proc_[kBands], width_[kBands], heard_[kBands], in_, dir_, out_;
};

} // namespace
} // namespace highroller

std::unique_ptr<hc::fx::Effect> makeHighRoller() { return std::make_unique<highroller::Impl>(); }

} // namespace hem::rack
