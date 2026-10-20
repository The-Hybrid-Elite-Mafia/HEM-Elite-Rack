// GOLD CHAIN — one-knob tone: warmth, density and sheen that grow together (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"
#include "../arsenal/Vintage.hpp"

namespace hem::rack {
namespace goldchain {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -18.0f, 18.0f, 0.0f},
    {"tone", "Tone", "%", 0.0f, 100.0f, 0.0f},
    {"link", "Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"analog", "Hum", "", 0, 1, 1, Taper::Toggle, kOnOff},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        drive_.prepare();
        hum_.prepare(fs);
        dcK_ = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * 8.0 / fs));
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        tone_ = -1.0f;
        env_ = gr_ = 0.0f;
        dcL_ = dcR_ = 0.0f;
        drive_.clear();
        drive_.mix = p(Tone) > 0.0f ? 1.0f : 0.0f;
        hum_.clear();
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, outGain());
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    static constexpr std::uint32_t kChunk = 64;
    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        design();
        const float t = p(Tone) * 0.01f;
        // glue: slow, gentle, more of it as TONE goes up; half of what it takes comes back
        const float thr = -18.0f, ratio = 1.0f + 1.5f * t, makeup = -0.5f * downDb(-6.0f, thr, ratio, 10.0f);
        const float eAtt = coefMs(fs_, 30.0), eRel = coefMs(fs_, 300.0);
        const float att = coefMs(fs_, 20.0), rel = coefMs(fs_, 250.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(hc::fx::dbToGain(p(Input)));
            float l = L[i] * gi, r = R[i] * gi;
            for (auto& b : eq_[0]) l = b.tick(l);
            for (auto& b : eq_[1]) r = b.tick(r);
            const float x = std::max(std::fabs(l), std::fabs(r));
            env_ = envStep(env_, x, eAtt, eRel);
            gr_ = follow(gr_, downDb(dbOf(env_), thr, ratio, 10.0f), att, rel);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_ + makeup);
            L[i] = l * g;
            R[i] = r * g;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        const float drive = 1.0f + 2.5f * t;
        drive_.run(L, R, n, t > 0.0f, static_cast<float>(1.0 / (fs_ * 0.02)),
                   [t, drive](float s) { return s + t * (tubeish(s * drive, 0.15f) / drive - s); });
        const int analog = p(Analog) >= 0.5f ? 3 : 0;   // a whisper of hiss under the music
        for (std::uint32_t i = 0; i < n; ++i) {
            float yl = L[i], yr = R[i];
            dcL_ += (yl - dcL_) * dcK_;
            dcR_ += (yr - dcR_) * dcK_;
            yl -= dcL_;
            yr -= dcR_;
            const float o = out_.next(outGain());
            yl *= o;
            yr *= o;
            hum_.tick(analog, 1.0f, yl, yr);
            L[i] = yl;
            R[i] = yr;
        }
        publishReduction(most);
    }
    float outGain() const noexcept {
        const float link = p(Link) >= 0.5f ? -p(Input) : 0.0f;
        return hc::fx::dbToGain(p(Output) + link);
    }
    void design() noexcept {
        if (p(Tone) == tone_) return;
        tone_ = p(Tone);
        const float t = tone_ * 0.01f;
        const C low = C::make(T::LowShelf, fs_, 120.0, 0.7071, 2.5 * t);
        const C box = C::make(T::Bell, fs_, 400.0, 0.8, -1.0 * t);
        const C sheen = C::make(T::HighShelf, fs_, std::min(9000.0, fs_ * 0.4), 0.7071, 2.0 * t);
        for (auto& ch : eq_) { ch[0].c = low; ch[1].c = box; ch[2].c = sheen; }
    }

    double fs_ = 48000.0;
    std::array<std::array<hc::fx::Biquad, 3>, 2> eq_{};
    float tone_ = -1.0f, env_ = 0, gr_ = 0, dcL_ = 0, dcR_ = 0, dcK_ = 0;
    Drive4 drive_;
    arsenal::Hum hum_;
    hc::fx::Smoothed in_, out_;
};

} // namespace
} // namespace goldchain

std::unique_ptr<hc::fx::Effect> makeGoldChain() { return std::make_unique<goldchain::Impl>(); }

} // namespace hem::rack
