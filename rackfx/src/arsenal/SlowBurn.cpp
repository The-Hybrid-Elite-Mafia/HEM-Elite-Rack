// SLOW BURN and LONE WOLF — variable-mu tube limiters (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

#include <vector>

namespace hem::rack {

// A wide soft knee, and a ratio that grows the further over it goes: 1.5:1 at first, up to 8:1.
float varimu::gainComputerDb(float x, float t) noexcept {
    constexpr float K = 12.0f;
    const float over = x - t;
    float oe;
    if (over <= -K * 0.5f) return 0.0f;
    if (over >= K * 0.5f) oe = over;
    else { const float k = over + K * 0.5f; oe = k * k / (2.0f * K); }
    const float ratio = std::min(1.5f + oe / 4.0f, 8.0f);
    return -oe * (1.0f - 1.0f / ratio);
}

namespace varimu {
namespace {
using namespace arsenal;

const char* const kTimeN[] = {"1", "2", "3", "4", "5", "6"};
const char* const kLinkN[] = {"SEPARATE", "LINKED", "MID / SIDE"};
const ParamInfo kStereoParams[slowburn::kCount] = {
    {"input", "Input", "dB", -20.0f, 0.0f, -14.0f},
    {"threshold", "Threshold", "/10", 0.0f, 10.0f, 0.0f},
    {"time", "Time Constant", "", 0, 5, 0, Taper::Choice, kTimeN},
    {"link", "Link", "", 0, 2, 1, Taper::Choice, kLinkN},
    {"mains", "Mains", "", 0, 2, 0, Taper::Choice, kMainsN},
    {"compress", "Compress", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const ParamInfo kMonoParams[lonewolf::kCount] = {
    {"input", "Input", "dB", -20.0f, 0.0f, -14.0f},
    {"threshold", "Threshold", "/10", 0.0f, 10.0f, 0.0f},
    {"time", "Time Constant", "", 0, 5, 0, Taper::Choice, kTimeN},
    {"mains", "Mains", "", 0, 2, 0, Taper::Choice, kMainsN},
    {"compress", "Compress", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

inline float tubeCurve(float x, float g) noexcept { return tubeish(g * x, 0.18f) / g; }
constexpr float kTubeDrive = 1.1f;   // the tubes, always in the path (on or off)

// kStereo: SLOW BURN (with LINK); otherwise LONE WOLF: one channel's circuit, the same on both sides
template <bool kStereo>
class Impl final : public hc::fx::Effect {
    static constexpr int kInput = 0, kThreshold = 1, kTime = 2, kLink = kStereo ? 3 : -1, kMains = kStereo ? 4 : 3,
                         kCompress = kStereo ? 5 : 4, kOutput = kStereo ? 6 : 5;

public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override {
        if constexpr (kStereo) return kStereoParams;
        else return kMonoParams;
    }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : tubed_) v.assign(mb, 0.0f);
        hum_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        ms_[0] = ms_[1] = 0.0f;
        gr_[0] = gr_[1] = 0.0f;
        work_ = 0.0f;
        dcL_ = dcR_ = 0.0;
        hum_.clear();
        in_.prepare(fs_, 30, inGain());
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(kOutput)));
        on_.prepare(fs_, 20, p(kCompress) >= 0.5f ? 1.0f : 0.0f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int tc = std::clamp(static_cast<int>(std::lround(p(kTime))), 0, 5);
        const int link = kStereo ? std::clamp(static_cast<int>(std::lround(p(kLink))), 0, 2) : 1;
        const float thr = -4.0f * p(kThreshold);   // the dial: 0 = nothing .. 10 = from -40 dBFS
        const float attC = coefMs(fs_, kAttackMs[tc]);
        const float detC = coefMs(fs_, 5.0);   // the tube's smooth detector
        const float chargeK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 2.0)));
        const float drainK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 4.0)));
        const float onT = p(kCompress) >= 0.5f ? 1.0f : 0.0f;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(inGain());
            const float l = L[i] * g, r = R[i] * g;
            float a = l, b = r;
            if (link == 2) { a = 0.5f * (l + r); b = 0.5f * (l - r); }
            float sq[2] = {a * a, b * b};
            if (link == 1) sq[0] = sq[1] = std::max(sq[0], sq[1]);
            const float on = on_.next(onT);
            float gain[2];
            for (int c = 0; c < 2; ++c) {
                ms_[c] = sq[c] + detC * (ms_[c] - sq[c]);
                const float lvl = 10.0f * std::log10(std::max(2.0f * ms_[c], 1e-12f));
                const float target = gainComputerDb(lvl, thr);
                const float relS = kReleaseS[tc] + (kReleaseLongS[tc] - kReleaseS[tc]) * work_;
                gr_[c] = follow(gr_[c], target, attC, coefMs(fs_, relS * 1000.0));
                gain[c] = hc::fx::dbToGain(gr_[c] * on);
                most = std::min(most, gr_[c] * on);
            }
            work_ += std::min(gr_[0], gr_[1]) < -3.0f ? (1.0f - work_) * chargeK : -work_ * drainK;
            a *= gain[0];
            b *= gain[1];
            if (link == 2) { wet_[0][i] = a + b; wet_[1][i] = a - b; }
            else { wet_[0][i] = a; wet_[1][i] = b; }
        }
        // the tubes, 4x oversampled (always in: COMPRESS OFF leaves them, like the hardware)
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = tubeCurve(x4[i], kTubeDrive);
            os_[c].down(x4, tubed_[static_cast<std::size_t>(c)].data(), n);
        }
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        const int mains = static_cast<int>(std::lround(p(kMains)));
        for (std::uint32_t i = 0; i < n; ++i) {
            dcL_ += (tubed_[0][i] - dcL_) * dcK;
            dcR_ += (tubed_[1][i] - dcR_) * dcK;
            const float o = out_.next(hc::fx::dbToGain(p(kOutput)));
            float yl = (tubed_[0][i] - static_cast<float>(dcL_)) * o, yr = (tubed_[1][i] - static_cast<float>(dcR_)) * o;
            hum_.tick(mains, 1.0f, yl, yr);
            L[i] = yl;
            R[i] = yr;
        }
        publishReduction(most);
    }

private:
    float inGain() const noexcept { return hc::fx::dbToGain(p(kInput) + 14.0f); }   // -14 on the dial = unity

    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, tubed_;
    std::array<float, 2> ms_{}, gr_{};
    float work_ = 0.0f;
    double dcL_ = 0, dcR_ = 0;
    Hum hum_;
    hc::fx::Smoothed in_, out_, on_;
};

} // namespace
} // namespace varimu

std::unique_ptr<hc::fx::Effect> makeSlowBurn() { return std::make_unique<varimu::Impl<true>>(); }
std::unique_ptr<hc::fx::Effect> makeLoneWolf() { return std::make_unique<varimu::Impl<false>>(); }

} // namespace hem::rack
