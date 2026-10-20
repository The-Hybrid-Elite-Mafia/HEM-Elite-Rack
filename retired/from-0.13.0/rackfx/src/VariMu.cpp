#include "hem/rack/Dynamics.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace varimu {

float gainComputerDb(float x, float t) noexcept {
    constexpr float K = 10.0f;   // a wide, soft knee
    const float over = x - t;
    float oe;
    if (over <= -K * 0.5f) return 0.0f;
    if (over >= K * 0.5f) oe = over;
    else { const float k = over + K * 0.5f; oe = k * k / (2.0f * K); }
    const float ratio = std::min(1.5f + oe / 5.0f, 6.0f);   // 1.5:1 at the knee, up to 6:1 far over
    return -oe * (1.0f - 1.0f / ratio);
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
const char* const kTimeN[] = {"1", "2", "3", "4", "5", "6"};
const char* const kModeN[] = {"STEREO", "MID / SIDE"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -12.0f, 12.0f, 0.0f},
    {"threshold", "Threshold", "dB", -40.0f, 0.0f, -16.0f},
    {"time", "Time Constant", "", 0, 5, 1, Taper::Choice, kTimeN},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"tube", "Tube", "%", 0.0f, 100.0f, 30.0f},
    {"makeup", "Makeup", "dB", 0.0f, 20.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
} // namespace
} // namespace varimu

namespace {
using namespace varimu;
using OS = hc::fx::Oversampler4;

inline float tubeCurve(float x, float g) noexcept {
    const float v = g * x, y = v + 0.18f * v * v;
    return y / std::cbrt(1.0f + std::fabs(y) * y * y) / g;
}

class VariMuImpl final : public hc::fx::Effect {
public:
    VariMuImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : clean_) v.assign(mb, 0.0f);
        for (auto& d : dryDelay_) d.prepare(OS::latency() + 1);
        for (auto& d : cleanDelay_) d.prepare(OS::latency() + 1);
        reset();
    }

    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& d : dryDelay_) d.clear();
        for (auto& d : cleanDelay_) d.clear();
        ms_[0] = ms_[1] = 0.0f;
        gr_[0] = gr_[1] = 0.0f;
        work_ = 0.0f;
        dcL_ = dcR_ = 0.0;
        tubeFade_ = p(Tube) > 0.0f ? 1.0f : 0.0f;
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        makeup_.prepare(fs_, 30, hc::fx::dbToGain(p(Makeup)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int tc = std::clamp(static_cast<int>(p(Time)), 0, 5);
        const bool ms = p(Mode) >= 0.5f;
        const float thr = p(Threshold);
        const float attC = static_cast<float>(std::exp(-1.0 / (fs_ * kAttackMs[tc] * 0.001)));
        const float detC = static_cast<float>(std::exp(-1.0 / (fs_ * 0.005)));   // the tube's smooth detector (5 ms)
        const float chargeK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 2.0)));
        const float drainK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 4.0)));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            const float l = L[i] * g, r = R[i] * g;
            float a = l, b = r;                       // the two channels the compressor works on
            if (ms) { a = 0.5f * (l + r); b = 0.5f * (l - r); }
            float sq[2] = {a * a, b * b};
            if (!ms) sq[0] = sq[1] = std::max(sq[0], sq[1]);   // stereo: linked
            float gain[2];
            for (int c = 0; c < 2; ++c) {
                ms_[c] = sq[c] + detC * (ms_[c] - sq[c]);
                const float lvl = 10.0f * std::log10(std::max(2.0f * ms_[c], 1e-12f));
                const float target = gainComputerDb(lvl, thr);
                float relS = kReleaseS[tc];
                if (kReleaseLongS[tc] > kReleaseS[tc]) relS += (kReleaseLongS[tc] - kReleaseS[tc]) * work_;
                const float cc = target < gr_[c] ? attC : static_cast<float>(std::exp(-1.0 / (fs_ * relS)));
                gr_[c] = target + cc * (gr_[c] - target);
                gain[c] = hc::fx::dbToGain(gr_[c]);
                most = std::min(most, gr_[c]);
            }
            work_ += std::min(gr_[0], gr_[1]) < -3.0f ? (1.0f - work_) * chargeK : -work_ * drainK;
            a *= gain[0];
            b *= gain[1];
            if (ms) { wet_[0][i] = a + b; wet_[1][i] = a - b; }
            else { wet_[0][i] = a; wet_[1][i] = b; }
        }
        // The tube (oversampled); at TUBE 0 the path is clean, delayed the same.
        const float drive = p(Tube) * 0.01f, tg = 0.5f + 2.0f * drive;
        for (std::uint32_t i = 0; i < n; ++i) {
            clean_[0][i] = cleanDelay_[0].process(wet_[0][i], OS::latency());
            clean_[1][i] = cleanDelay_[1].process(wet_[1][i], OS::latency());
        }
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            if (drive > 0.0f || tubeFade_ > 0.0f)
                for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = tubeCurve(x4[i], tg);
            os_[c].down(x4, wet_[static_cast<std::size_t>(c)].data(), n);
        }
        const float step = static_cast<float>(1.0 / (fs_ * 0.010));
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            tubeFade_ = drive > 0.0f ? std::min(1.0f, tubeFade_ + step) : std::max(0.0f, tubeFade_ - step);
            float yl = clean_[0][i], yr = clean_[1][i];
            dcL_ += (wet_[0][i] - dcL_) * dcK;   // always tracked, so the tube fades in without a jump
            dcR_ += (wet_[1][i] - dcR_) * dcK;
            if (tubeFade_ > 0.0f) {
                yl += tubeFade_ * ((wet_[0][i] - static_cast<float>(dcL_)) - yl);
                yr += tubeFade_ * ((wet_[1][i] - static_cast<float>(dcR_)) - yr);
            }
            const float mk = makeup_.next(hc::fx::dbToGain(p(Makeup))), m = mix_.next(p(Mix) * 0.01f);
            const float dl = dryDelay_[0].process(L[i], OS::latency()), dr = dryDelay_[1].process(R[i], OS::latency());
            L[i] = dl + m * (yl * mk - dl);
            R[i] = dr + m * (yr * mk - dr);
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, clean_;
    hc::fx::DelayLine dryDelay_[2], cleanDelay_[2];
    std::array<float, 2> ms_{}, gr_{};
    float work_ = 0.0f, tubeFade_ = 0.0f;
    double dcL_ = 0, dcR_ = 0;
    hc::fx::Smoothed in_, makeup_, mix_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeVariMu() { return std::make_unique<VariMuImpl>(); }

} // namespace hem::rack
