// THE FIXER — three dynamic cuts: mud, harshness, fizz (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace fixer {
namespace {
using namespace hideout;

#define HEM_BAND(n, name)                                                                         \
    {"amount" #n, name " Amount", "", 0.0f, 100.0f, 0.0f},                                        \
    {"freq" #n, name " Freq", "", 0.0f, 100.0f, 50.0f},                                           \
    {"listen" #n, name " Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},                            \
    {"bypass" #n, name " Bypass", "", 0, 1, 0, Taper::Toggle, kOnOff}
const ParamInfo kParams[kCount] = {
    HEM_BAND(1, "Mud"), HEM_BAND(2, "Harsh"), HEM_BAND(3, "Fizz"),
    {"input", "Input", "dB", -18.0f, 18.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {{"MUD", "dB", -12.0f, 0.0f}, {"HARSH", "dB", -12.0f, 0.0f}, {"FIZZ", "dB", -12.0f, 0.0f}};
constexpr double kQ[kBands] = {1.2, 1.6, 1.0};
constexpr double kAttMs[kBands] = {10.0, 3.0, 1.0}, kRelMs[kBands] = {150.0, 90.0, 60.0};   // the lows move slower

// FREQ 0..100 -> the band's centre, across its range on a log scale
float centreHz(int b, float freq) noexcept { return kLowHz[b] * std::pow(kHighHz[b] / kLowHz[b], std::clamp(freq, 0.0f, 100.0f) * 0.01f); }

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
        for (auto& b : cut_) for (auto& f : b) f.clear();
        key_.fill(-1.0f);
        for (int b = 0; b < kBands; ++b) { env_[b] = 0.0f; gr_[b] = 0.0f; publishMeter(b, 0.0f); }
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    // AMOUNT: the threshold comes down from -10 to -60 dBFS and the most it takes off grows to 18 dB
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float envRel = coefMs(fs_, 5.0);
        float att[kBands], rel[kBands], thr[kBands], most[kBands]{}, range[kBands];
        bool on[kBands], lis[kBands];
        bool anyListen = false;
        for (int b = 0; b < kBands; ++b) {
            att[b] = coefMs(fs_, kAttMs[b]);
            rel[b] = coefMs(fs_, kRelMs[b]);
            const float a = p(band(b, Amount));
            thr[b] = -10.0f - 0.5f * a;
            range[b] = 0.18f * a;
            on[b] = a > 0.0f && p(band(b, Bypass)) < 0.5f;
            lis[b] = p(band(b, Listen)) >= 0.5f;
            anyListen = anyListen || lis[b];
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(hc::fx::dbToGain(p(Input)));
            float l = L[i] * gi, r = R[i] * gi;
            float hl = 0.0f, hr = 0.0f;
            for (int b = 0; b < kBands; ++b) {
                // the band on its own (the detector and the cut use the same band-pass)
                const float bl = cut_[b][0].tick(l), br = cut_[b][1].tick(r);
                const float x = std::max(std::fabs(bl), std::fabs(br));
                env_[b] = x > env_[b] ? x : x + envRel * (env_[b] - x);
                const float want = on[b] ? std::max(downDb(dbOf(env_[b]), thr[b], 4.0f, 6.0f), -range[b]) : 0.0f;
                gr_[b] = follow(gr_[b], want, att[b], rel[b]);
                most[b] = std::min(most[b], gr_[b]);
                const float g = hc::fx::dbToGain(gr_[b]);
                if (lis[b]) {   // LISTEN: the band alone, as processed
                    hl += bl * g;
                    hr += br * g;
                }
                l += (g - 1.0f) * bl;   // a bell cut of gr dB: x + (G - 1) * band
                r += (g - 1.0f) * br;
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (anyListen ? hl : l) * o;
            R[i] = (anyListen ? hr : r) * o;
        }
        float worst = 0.0f;
        for (int b = 0; b < kBands; ++b) {
            if (gr_[b] > -1e-6f) gr_[b] = 0.0f;
            publishMeter(b, most[b]);
            worst = std::min(worst, most[b]);
        }
        publishReduction(worst);
    }

private:
    void design() noexcept {
        const std::array<float, kBands> k{p(band(0, Freq)), p(band(1, Freq)), p(band(2, Freq))};
        if (k == key_) return;
        key_ = k;
        for (int b = 0; b < kBands; ++b) {
            const C c = C::make(T::BandPass, fs_, std::min<double>(centreHz(b, k[static_cast<std::size_t>(b)]), fs_ * 0.45), kQ[b]);
            for (auto& f : cut_[b]) f.c = c;
        }
    }

    double fs_ = 48000.0;
    hc::fx::Biquad cut_[kBands][2];
    std::array<float, kBands> key_{};
    float env_[kBands]{}, gr_[kBands]{};
    hc::fx::Smoothed in_, out_;
};

} // namespace
} // namespace fixer

std::unique_ptr<hc::fx::Effect> makeFixer() { return std::make_unique<fixer::Impl>(); }

} // namespace hem::rack
