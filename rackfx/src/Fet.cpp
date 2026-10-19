#include "hem/rack/Dynamics.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hem::rack {
namespace fet {

float attackMs(float k) noexcept { return 0.8f * std::pow(0.02f / 0.8f, (std::clamp(k, 1.0f, 7.0f) - 1.0f) / 6.0f); }
float releaseMs(float k) noexcept { return 1100.0f * std::pow(50.0f / 1100.0f, (std::clamp(k, 1.0f, 7.0f) - 1.0f) / 6.0f); }

namespace {
struct RatioSpec { float ratio, threshold, knee; };
constexpr RatioSpec kSpecs[5] = {{4, -14, 6}, {8, -12, 4}, {12, -10, 3}, {20, -8, 2}, {20, -12, 2}};
} // namespace

float gainComputerDb(float x, RatioSel r) noexcept {
    const RatioSpec& s = kSpecs[static_cast<int>(r)];
    const float over = x - s.threshold, slope = 1.0f / s.ratio - 1.0f;
    float g;
    if (over <= -s.knee * 0.5f) g = 0.0f;
    else if (over >= s.knee * 0.5f) g = slope * over;
    else { const float k = over + s.knee * 0.5f; g = slope * k * k / (2.0f * s.knee); }
    return r == RatioSel::All ? 1.3f * g : g;   // ALL buttons in: it overshoots, pulling harder than 20:1
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
const char* const kRatioN[] = {"4:1", "8:1", "12:1", "20:1", "ALL"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", 0.0f, 40.0f, 10.0f},
    {"output", "Output", "dB", -30.0f, 12.0f, -6.0f},
    {"attack", "Attack", "", 1.0f, 7.0f, 4.0f},
    {"release", "Release", "", 1.0f, 7.0f, 4.0f},
    {"ratio", "Ratio", "", 0, 4, 0, Taper::Choice, kRatioN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
} // namespace
} // namespace fet

namespace {
using namespace fet;
using OS = hc::fx::Oversampler4;

class FetImpl final : public hc::fx::Effect {
public:
    FetImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        drive_.assign(mb, 0.0f);
        gain_.assign(mb, 0.0f);
        for (auto& d : dryDelay_) d.prepare(OS::latency() + 1);
        reset();
    }

    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& d : dryDelay_) d.clear();
        gr_ = 0.0f;
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const auto ratio = static_cast<RatioSel>(std::clamp(static_cast<int>(p(Ratio)), 0, 4));
        const float attC = static_cast<float>(std::exp(-1.0 / (fs_ * attackMs(p(Attack)) * 0.001)));
        const float relC = static_cast<float>(std::exp(-1.0 / (fs_ * releaseMs(p(Release)) * 0.001)));
        const bool all = ratio == RatioSel::All;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            const float l = L[i] * g, r = R[i] * g;
            const float level = std::max(std::fabs(l), std::fabs(r));
            const float target = gainComputerDb(20.0f * std::log10(std::max(level, 1e-9f)), ratio);
            gr_ = target + (target < gr_ ? attC : relC) * (gr_ - target);
            most = std::min(most, gr_);
            wet_[0][i] = l;                       // the driven input; the gain is applied at 4x below
            wet_[1][i] = r;
            gain_[i] = hc::fx::dbToGain(gr_);
            drive_[i] = 1.0f + 0.015f * -gr_;     // its colour grows with the gain reduction
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t i = 0; i < n; ++i) {
                const float d = drive_[i], g = gain_[i];
                for (int k = 0; k < OS::kFactor; ++k) {
                    float& v = x4[i * OS::kFactor + static_cast<std::uint32_t>(k)];
                    float y = v;
                    if (all) {   // ALL buttons in: the circuit is overdriven before it even turns down
                        const float a = 0.35f * y;
                        y = a / std::cbrt(1.0f + std::fabs(a) * a * a) / 0.35f;
                    }
                    const float x = d * (y * g);
                    v = x / std::cbrt(1.0f + std::fabs(x) * x * x) / d;
                }
            }
            os_[c].down(x4, wet_[static_cast<std::size_t>(c)].data(), n);
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float o = out_.next(hc::fx::dbToGain(p(Output))), m = mix_.next(p(Mix) * 0.01f);
            const float dl = dryDelay_[0].process(L[i], OS::latency()), dr = dryDelay_[1].process(R[i], OS::latency());
            L[i] = dl + m * (wet_[0][i] * o - dl);
            R[i] = dr + m * (wet_[1][i] * o - dr);
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_;
    std::vector<float> drive_, gain_;
    hc::fx::DelayLine dryDelay_[2];
    float gr_ = 0.0f;
    hc::fx::Smoothed in_, out_, mix_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeFet() { return std::make_unique<FetImpl>(); }

} // namespace hem::rack
