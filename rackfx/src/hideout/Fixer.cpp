// THE FIXER — three dynamic cuts: mud, harshness, fizz (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace fixer {
namespace {
using namespace hideout;

const char* const kSpeedN[] = {"FAST", "MEDIUM", "SLOW"};
const char* const kListenN[] = {"OFF", "MUD", "HARSH", "FIZZ"};
const ParamInfo kParams[kCount] = {
    {"freq1", "Mud Freq", "Hz", 80.0f, 1000.0f, 300.0f, Taper::Log},
    {"thr1", "Mud Threshold", "dB", -60.0f, 0.0f, -24.0f},
    {"range1", "Mud Range", "dB", 0.0f, 18.0f, 8.0f},
    {"freq2", "Harsh Freq", "Hz", 1000.0f, 6000.0f, 2800.0f, Taper::Log},
    {"thr2", "Harsh Threshold", "dB", -60.0f, 0.0f, -24.0f},
    {"range2", "Harsh Range", "dB", 0.0f, 18.0f, 8.0f},
    {"freq3", "Fizz Freq", "Hz", 4000.0f, 16000.0f, 7500.0f, Taper::Log},
    {"thr3", "Fizz Threshold", "dB", -60.0f, 0.0f, -24.0f},
    {"range3", "Fizz Range", "dB", 0.0f, 18.0f, 8.0f},
    {"speed", "Speed", "", 0, 2, 1, Taper::Choice, kSpeedN},
    {"listen", "Listen", "", 0, 3, 0, Taper::Choice, kListenN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"MUD", "dB", -18.0f, 0.0f}, {"HARSH", "dB", -18.0f, 0.0f}, {"FIZZ", "dB", -18.0f, 0.0f}};
constexpr double kQ[kBands] = {1.2, 1.6, 1.0};
constexpr double kAttMs[] = {1.0, 5.0, 20.0}, kRelMs[] = {40.0, 120.0, 300.0};

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
        for (auto& b : lis_) for (auto& f : b) f.clear();
        key_.fill(-1.0f);
        for (int b = 0; b < kBands; ++b) { env_[b] = 0.0f; gr_[b] = 0.0f; publishMeter(b, 0.0f); }
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int sp = std::clamp(static_cast<int>(std::lround(p(Speed))), 0, 2);
        const float att = coefMs(fs_, kAttMs[sp]), rel = coefMs(fs_, kRelMs[sp]), envRel = coefMs(fs_, 5.0);
        const int listen = std::clamp(static_cast<int>(std::lround(p(Listen))), 0, 3) - 1;
        float most[kBands] = {0, 0, 0};
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            float hl = 0.0f, hr = 0.0f;
            if (listen >= 0) {
                hl = lis_[listen][0].tick(l);
                hr = lis_[listen][1].tick(r);
            }
            for (int b = 0; b < kBands; ++b) {
                // the band on its own (detector and the cut use the same band-pass)
                const float bl = cut_[b][0].tick(l), br = cut_[b][1].tick(r);
                const float x = std::max(std::fabs(bl), std::fabs(br));
                env_[b] = x > env_[b] ? x : x + envRel * (env_[b] - x);
                const float want = std::max(downDb(dbOf(env_[b]), p(Thr1 + b * 3), 4.0f, 6.0f), -p(Range1 + b * 3));
                gr_[b] = follow(gr_[b], want, att, rel);
                most[b] = std::min(most[b], gr_[b]);
                const float g = hc::fx::dbToGain(gr_[b]) - 1.0f;   // a bell cut of gr dB: x + (G - 1) * band
                l += g * bl;
                r += g * br;
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (listen >= 0 ? hl : l) * o;
            R[i] = (listen >= 0 ? hr : r) * o;
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
        const std::array<float, kBands> k{p(Freq1), p(Freq2), p(Freq3)};
        if (k == key_) return;
        key_ = k;
        for (int b = 0; b < kBands; ++b) {
            const C c = C::make(T::BandPass, fs_, std::min<double>(k[static_cast<std::size_t>(b)], fs_ * 0.45), kQ[b]);
            for (auto& f : cut_[b]) f.c = c;
            for (auto& f : lis_[b]) f.c = c;
        }
    }

    double fs_ = 48000.0;
    hc::fx::Biquad cut_[kBands][2], lis_[kBands][2];
    std::array<float, kBands> key_{};
    float env_[kBands]{}, gr_[kBands]{};
    hc::fx::Smoothed out_;
};

} // namespace
} // namespace fixer

std::unique_ptr<hc::fx::Effect> makeFixer() { return std::make_unique<fixer::Impl>(); }

} // namespace hem::rack
