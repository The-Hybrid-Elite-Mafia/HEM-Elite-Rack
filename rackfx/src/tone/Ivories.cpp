// IVORIES — piano tone: one PIANO knob, a soft stereo slap, a doubler (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"
#include "../arsenal/Vintage.hpp"

namespace hem::rack {
namespace ivories {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -18.0f, 18.0f, 0.0f},
    {"piano", "Piano", "", -50.0f, 50.0f, 0.0f},
    {"delayon", "Delay", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"delaylevel", "Delay Level", "dB", -24.0f, 0.0f, -13.6f},
    {"doubleron", "Doubler", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"doublerlevel", "Doubler Level", "dB", -24.0f, 0.0f, -8.7f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"analog", "Analog", "", 0, 1, 1, Taper::Toggle, kOnOff},
};
constexpr float kTimeMs = 120.0f;   // the slap: left 120 ms, right 156 ms
constexpr double kMaxDelayS = 0.45;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        if (i == DelayLevel) return p(DelayOn) >= 0.5f;
        if (i == DoublerLevel) return p(DoublerOn) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const auto n = static_cast<std::uint32_t>(kMaxDelayS * fs) + 4;
        echo_[0].prepare(n);
        echo_[1].prepare(n);
        dbl_[0].prepare(static_cast<std::uint32_t>(0.03 * fs) + 4);
        dbl_[1].prepare(static_cast<std::uint32_t>(0.03 * fs) + 4);
        hum_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        for (auto& d : echo_) d.clear();
        for (auto& d : dbl_) d.clear();
        for (auto& f : damp_) f.clear();
        key_ = -1e9f;
        hum_.clear();
        env_ = gr_ = 0.0f;
        phase_[0] = 0.0;
        phase_[1] = 0.25;
        fb_[0] = fb_[1] = 0.0f;
        delay_.prepare(fs_, 30, delayGain());
        dblMix_.prepare(fs_, 30, doublerGain());
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float c = 0.6f * std::fabs(p(Piano)) / 50.0f;   // PIANO either way leans on the compressor more
        const float thr = -6.0f - 24.0f * c, ratio = 1.0f + 3.0f * c, makeup = -0.5f * downDb(-6.0f, thr, ratio, 8.0f);
        const float att = coefMs(fs_, 10.0), rel = coefMs(fs_, 200.0), envRel = coefMs(fs_, 15.0);
        const double rate[2] = {0.6 / fs_, 0.45 / fs_};
        const float baseMs[2] = {13.0f, 19.0f};
        const float t = static_cast<float>(kTimeMs * 0.001 * fs_), wIn = hc::fx::dbToGain(p(Input));
        const float wDelay = delayGain(), wDbl = doublerGain(), wOut = hc::fx::dbToGain(p(Output));
        const int analog = p(Analog) >= 0.5f ? 3 : 0;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn);
            float l = L[i] * gi, r = R[i] * gi;
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
            const float dm = dblMix_.next(wDbl);
            l += dm * dbl[0];
            r += dm * dbl[1];
            // the slap: left at TIME, right a little later, darker each time round
            const float e = delay_.next(wDelay);
            const float in[2] = {l, r}, tt[2] = {t, t * 1.3f};
            float echo[2];
            for (int k = 0; k < 2; ++k) {
                echo_[k].write(in[k] + 0.25f * fb_[k]);
                fb_[k] = damp_[k].tick(echo_[k].read(tt[k]));
                echo[k] = fb_[k];
            }
            l += e * echo[0];
            r += e * echo[1];
            const float o = out_.next(wOut);
            l *= o;
            r *= o;
            hum_.tick(analog, 1.0f, l, r);
            L[i] = l;
            R[i] = r;
        }
        publishReduction(most);
    }

private:
    float delayGain() const noexcept { return p(DelayOn) >= 0.5f ? hc::fx::dbToGain(p(DelayLevel)) : 0.0f; }
    float doublerGain() const noexcept { return p(DoublerOn) >= 0.5f ? hc::fx::dbToGain(p(DoublerLevel)) : 0.0f; }
    // PIANO: up = brighter, leaner, more present; down = darker and fuller
    void design() noexcept {
        if (p(Piano) == key_) return;
        key_ = p(Piano);
        const float t = key_ / 50.0f;
        const C low = C::make(T::LowShelf, fs_, 250.0, 0.7071, t > 0.0f ? -3.0 * t : 0.0);
        const C high = C::make(T::HighShelf, fs_, 2500.0, 0.7071, 5.0 * t);
        const C body = C::make(T::Bell, fs_, 180.0, 0.7, t < 0.0f ? -4.0 * t : 0.0);
        for (auto& ch : eq_) { ch[0].c = low; ch[1].c = high; ch[2].c = body; }
        damp_[0].c = damp_[1].c = C::make(T::LowPass, fs_, 4000.0, 0.7071);
    }

    double fs_ = 48000.0;
    std::array<std::array<hc::fx::Biquad, 3>, 2> eq_{};
    float key_ = -1e9f;
    hc::fx::DelayLine echo_[2], dbl_[2];
    hc::fx::Biquad damp_[2];
    float env_ = 0, gr_ = 0, fb_[2]{};
    double phase_[2]{};
    hc::fx::Smoothed delay_, dblMix_, in_, out_;
    arsenal::Hum hum_;
};

} // namespace
} // namespace ivories

std::unique_ptr<hc::fx::Effect> makeIvories() { return std::make_unique<ivories::Impl>(); }

} // namespace hem::rack
