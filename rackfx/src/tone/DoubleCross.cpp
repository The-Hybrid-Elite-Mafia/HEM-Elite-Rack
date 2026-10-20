// DOUBLE CROSS — mono to stereo, mono-safe: bands lean left and right in turn (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace doublecross {
namespace {
using namespace tone;

const char* const kStyleN[] = {"TIGHT", "MEDIUM", "WIDE"};
const char* const kMonoN[] = {"OFF", "100 Hz", "200 Hz", "300 Hz"};
const ParamInfo kParams[kCount] = {
    {"spread", "Spread", "%", 0.0f, 100.0f, 50.0f},
    {"style", "Style", "", 0, 2, 1, Taper::Choice, kStyleN},
    {"lowmono", "Low Stays Mono", "", 0, 3, 1, Taper::Choice, kMonoN},
    {"center", "Center", "dB", -12.0f, 6.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr float kTight[] = {300.0f, 1200.0f, 4800.0f};
constexpr float kMedium[] = {200.0f, 500.0f, 1200.0f, 3000.0f, 7000.0f};
constexpr float kWide[] = {150.0f, 300.0f, 600.0f, 1200.0f, 2400.0f, 4800.0f, 9600.0f};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        style_ = mono_ = -1;
        design();
        for (auto& x : xo_) x.clear();
        for (auto& b : hp_) b.clear();
        spread_.prepare(fs_, 30, p(Spread) * 0.009f);
        center_.prepare(fs_, 30, hc::fx::dbToGain(p(Center)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int nb = xo_[0].bands();
        const bool lowMono = mono_ > 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            float mb[8], sb[8];
            xo_[0].split(m, mb);
            xo_[1].split(s, sb);
            float mo = 0.0f, so = 0.0f, made = 0.0f;
            for (int b = 0; b < nb; ++b) {
                mo += mb[b];
                so += sb[b];
                made += (b % 2 == 0 ? mb[b] : -mb[b]);   // left, right, left, ...
            }
            if (lowMono) made = hp_[1].tick(hp_[0].tick(made));
            const float sp = spread_.next(p(Spread) * 0.009f), c = center_.next(hc::fx::dbToGain(p(Center)));
            const float side = so + sp * made, mid = c * mo;
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (mid + side) * o;
            R[i] = (mid - side) * o;
        }
    }

private:
    void design() noexcept {
        const int st = std::clamp(static_cast<int>(std::lround(p(Style))), 0, 2);
        const int mo = std::clamp(static_cast<int>(std::lround(p(LowMono))), 0, 3);
        if (st != style_) {
            style_ = st;
            const float* hz = st == 0 ? kTight : st == 1 ? kMedium : kWide;
            for (auto& x : xo_) x.design(fs_, hz, kBandsFor[st]);
        }
        if (mo != mono_) {
            mono_ = mo;
            if (mo > 0) {
                hp_[0].c = C::make(T::HighPass, fs_, kMonoHz[mo], 0.5412);
                hp_[1].c = C::make(T::HighPass, fs_, kMonoHz[mo], 1.3066);
            }
        }
    }

    double fs_ = 48000.0;
    Crossover<8> xo_[2];
    hc::fx::Biquad hp_[2];
    int style_ = -1, mono_ = -1;
    hc::fx::Smoothed spread_, center_, out_;
};

} // namespace
} // namespace doublecross

std::unique_ptr<hc::fx::Effect> makeDoubleCross() { return std::make_unique<doublecross::Impl>(); }

} // namespace hem::rack
