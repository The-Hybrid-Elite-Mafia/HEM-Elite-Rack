// IVORIES — piano tone: tone, body, compression, a soft stereo slap, a doubler, width (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace ivories {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"tone", "Tone", "", -100.0f, 100.0f, 0.0f},
    {"body", "Body", "%", 0.0f, 100.0f, 0.0f},
    {"compress", "Compress", "%", 0.0f, 100.0f, 0.0f},
    {"delay", "Delay", "%", 0.0f, 100.0f, 0.0f},
    {"time", "Time", "ms", 40.0f, 300.0f, 120.0f, Taper::Log},
    {"doubler", "Doubler", "%", 0.0f, 100.0f, 0.0f},
    {"width", "Width", "%", 0.0f, 200.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kMaxDelayS = 0.45;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override { return i != Time || p(Delay) > 0.0f; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const auto n = static_cast<std::uint32_t>(kMaxDelayS * fs) + 4;
        echo_[0].prepare(n);
        echo_[1].prepare(n);
        dbl_[0].prepare(static_cast<std::uint32_t>(0.03 * fs) + 4);
        dbl_[1].prepare(static_cast<std::uint32_t>(0.03 * fs) + 4);
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        for (auto& d : echo_) d.clear();
        for (auto& d : dbl_) d.clear();
        for (auto& f : damp_) f.clear();
        key_ = {-1e9f, -1e9f};
        env_ = gr_ = 0.0f;
        phase_[0] = 0.0;
        phase_[1] = 0.25;
        fb_[0] = fb_[1] = 0.0f;
        delay_.prepare(fs_, 30, p(Delay) * 0.01f);
        time_.prepare(fs_, 80, timeSamples());
        dblMix_.prepare(fs_, 30, p(Doubler) * 0.01f);
        width_.prepare(fs_, 30, p(Width) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float c = p(Compress) * 0.01f;
        const float thr = -6.0f - 24.0f * c, ratio = 1.0f + 3.0f * c, makeup = -0.5f * downDb(-6.0f, thr, ratio, 8.0f);
        const float att = coefMs(fs_, 10.0), rel = coefMs(fs_, 200.0), envRel = coefMs(fs_, 15.0);
        const double rate[2] = {0.6 / fs_, 0.45 / fs_};
        const float baseMs[2] = {13.0f, 19.0f};
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            for (auto& b : eq_[0]) l = b.tick(l);
            for (auto& b : eq_[1]) r = b.tick(r);
            // compress
            if (c > 0.0f) {
                const float x = std::max(std::fabs(l), std::fabs(r));
                env_ = x > env_ ? x : x + envRel * (env_ - x);
                gr_ = follow(gr_, downDb(dbOf(env_), thr, ratio, 8.0f), att, rel);
                most = std::min(most, gr_);
                const float g = hc::fx::dbToGain(gr_ + makeup);
                l *= g;
                r *= g;
            } else {
                gr_ = 0.0f;
            }
            // doubler: two slightly late, slowly drifting copies, one each side
            const float mono = 0.5f * (l + r);
            float dbl[2];
            for (int k = 0; k < 2; ++k) {
                dbl_[k].write(mono);
                phase_[k] += rate[k];
                if (phase_[k] >= 1.0) phase_[k] -= 1.0;
                const float ms = baseMs[k] + 0.35f * static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_[k]));
                dbl[k] = dbl_[k].read(ms * 0.001f * static_cast<float>(fs_));
            }
            const float dm = dblMix_.next(p(Doubler) * 0.01f) * 0.6f;
            l += dm * dbl[0];
            r += dm * dbl[1];
            // the slap: left at TIME, right a little later, darker each time round
            const float t = time_.next(timeSamples());
            const float e = delay_.next(p(Delay) * 0.01f) * 0.5f;
            const float in[2] = {l, r}, tt[2] = {t, t * 1.3f};
            float echo[2];
            for (int k = 0; k < 2; ++k) {
                echo_[k].write(in[k] + 0.25f * fb_[k]);
                fb_[k] = damp_[k].tick(echo_[k].read(tt[k]));
                echo[k] = fb_[k];
            }
            l += e * echo[0];
            r += e * echo[1];
            // width
            const float w = width_.next(p(Width) * 0.01f);
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (m + s) * o;
            R[i] = (m - s) * o;
        }
        publishReduction(most);
    }

private:
    float timeSamples() const noexcept { return std::min(p(Time) * 0.001f * static_cast<float>(fs_), static_cast<float>(kMaxDelayS * fs_ / 1.3) - 2.0f); }
    void design() noexcept {
        const std::array<float, 2> k{p(Tone), p(Body)};
        if (k == key_) return;
        key_ = k;
        const float t = p(Tone) * 0.01f, b = p(Body) * 0.01f;
        const C low = C::make(T::LowShelf, fs_, 250.0, 0.7071, -4.0 * t);
        const C high = C::make(T::HighShelf, fs_, 2500.0, 0.7071, 5.0 * t);
        const C body = C::make(T::Bell, fs_, 180.0, 0.7, 5.0 * b);
        for (auto& ch : eq_) { ch[0].c = low; ch[1].c = high; ch[2].c = body; }
        damp_[0].c = damp_[1].c = C::make(T::LowPass, fs_, 4000.0, 0.7071);
    }

    double fs_ = 48000.0;
    std::array<std::array<hc::fx::Biquad, 3>, 2> eq_{};
    std::array<float, 2> key_{};
    hc::fx::DelayLine echo_[2], dbl_[2];
    hc::fx::Biquad damp_[2];
    float env_ = 0, gr_ = 0, fb_[2]{};
    double phase_[2]{};
    hc::fx::Smoothed delay_, time_, dblMix_, width_, out_;
};

} // namespace
} // namespace ivories

std::unique_ptr<hc::fx::Effect> makeIvories() { return std::make_unique<ivories::Impl>(); }

} // namespace hem::rack
