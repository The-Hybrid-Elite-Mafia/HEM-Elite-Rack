// SLOW BURN — variable-mu tube limiter (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

#include <vector>

namespace hem::rack {

namespace slowburn {
// A wide soft knee, and a ratio that grows the further over it goes: 1.5:1 at first, up to 8:1.
float gainComputerDb(float x, float t) noexcept {
    constexpr float K = 12.0f;
    const float over = x - t;
    float oe;
    if (over <= -K * 0.5f) return 0.0f;
    if (over >= K * 0.5f) oe = over;
    else { const float k = over + K * 0.5f; oe = k * k / (2.0f * K); }
    const float ratio = std::min(1.5f + oe / 4.0f, 8.0f);
    return -oe * (1.0f - 1.0f / ratio);
}
} // namespace slowburn

namespace {
using namespace arsenal;
using namespace slowburn;

const char* const kTimeN[] = {"1", "2", "3", "4", "5", "6"};
const char* const kLinkN[] = {"LINKED", "DUAL", "MID / SIDE"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -20.0f, 10.0f, 0.0f},
    {"threshold", "Threshold", "dB", -40.0f, 0.0f, -14.0f},
    {"time", "Time", "", 0, 5, 1, Taper::Choice, kTimeN},
    {"link", "Link", "", 0, 2, 0, Taper::Choice, kLinkN},
    {"tube", "Tube", "%", 0.0f, 100.0f, 30.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};

inline float tubeCurve(float x, float g) noexcept { return tubeish(g * x, 0.18f) / g; }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : tubed_) v.assign(mb, 0.0f);
        dry_.prepare(OS::latency());
        clean_.prepare(OS::latency());
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        dry_.clear();
        clean_.clear();
        ms_[0] = ms_[1] = 0.0f;
        gr_[0] = gr_[1] = 0.0f;
        work_ = 0.0f;
        dcL_ = dcR_ = 0.0;
        tubeFade_ = p(Tube) > 0.0f ? 1.0f : 0.0f;
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int tc = std::clamp(static_cast<int>(std::lround(p(Time))), 0, 5);
        const int link = std::clamp(static_cast<int>(std::lround(p(Link))), 0, 2);
        const float thr = p(Threshold);
        const float attC = coefMs(fs_, kAttackMs[tc]);
        const float detC = coefMs(fs_, 5.0);   // the tube's smooth detector
        const float chargeK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 2.0)));
        const float drainK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 4.0)));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            const float l = L[i] * g, r = R[i] * g;
            float a = l, b = r;
            if (link == 2) { a = 0.5f * (l + r); b = 0.5f * (l - r); }
            float sq[2] = {a * a, b * b};
            if (link == 0) sq[0] = sq[1] = std::max(sq[0], sq[1]);
            float gain[2];
            for (int c = 0; c < 2; ++c) {
                ms_[c] = sq[c] + detC * (ms_[c] - sq[c]);
                const float lvl = 10.0f * std::log10(std::max(2.0f * ms_[c], 1e-12f));
                const float target = gainComputerDb(lvl, thr);
                const float relS = kReleaseS[tc] + (kReleaseLongS[tc] - kReleaseS[tc]) * work_;
                gr_[c] = follow(gr_[c], target, attC, coefMs(fs_, relS * 1000.0));
                gain[c] = hc::fx::dbToGain(gr_[c]);
                most = std::min(most, gr_[c]);
            }
            work_ += std::min(gr_[0], gr_[1]) < -3.0f ? (1.0f - work_) * chargeK : -work_ * drainK;
            a *= gain[0];
            b *= gain[1];
            if (link == 2) { wet_[0][i] = a + b; wet_[1][i] = a - b; }
            else { wet_[0][i] = a; wet_[1][i] = b; }
        }
        const float drive = p(Tube) * 0.01f, tg = 0.5f + 2.0f * drive;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = tubeCurve(x4[i], tg);
            os_[c].down(x4, tubed_[static_cast<std::size_t>(c)].data(), n);
        }
        const float step = static_cast<float>(1.0 / (fs_ * 0.010));
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            tubeFade_ = drive > 0.0f ? std::min(1.0f, tubeFade_ + step) : std::max(0.0f, tubeFade_ - step);
            float yl = clean_.l.process(wet_[0][i], OS::latency()), yr = clean_.r.process(wet_[1][i], OS::latency());
            dcL_ += (tubed_[0][i] - dcL_) * dcK;   // always tracked, so the tube fades in without a jump
            dcR_ += (tubed_[1][i] - dcR_) * dcK;
            if (tubeFade_ > 0.0f) {
                yl += tubeFade_ * ((tubed_[0][i] - static_cast<float>(dcL_)) - yl);
                yr += tubeFade_ * ((tubed_[1][i] - static_cast<float>(dcR_)) - yr);
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output))), m = mix_.next(p(Mix) * 0.01f);
            const float dl = dry_.l.process(L[i], OS::latency()), dr = dry_.r.process(R[i], OS::latency());
            L[i] = dl + m * (yl * o - dl);
            R[i] = dr + m * (yr * o - dr);
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, tubed_;
    Delay2 dry_, clean_;
    std::array<float, 2> ms_{}, gr_{};
    float work_ = 0.0f, tubeFade_ = 0.0f;
    double dcL_ = 0, dcR_ = 0;
    hc::fx::Smoothed in_, out_, mix_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeSlowBurn() { return std::make_unique<Impl>(); }

} // namespace hem::rack
