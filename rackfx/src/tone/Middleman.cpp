// THE MIDDLEMAN — centre / sides balance, per range, with punch steering (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace middleman {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"center", "Center", "dB", -12.0f, 12.0f, 0.0f},
    {"sides", "Sides", "dB", -12.0f, 12.0f, 0.0f},
    {"lowbalance", "Low Balance", "", -100.0f, 100.0f, 0.0f},
    {"highbalance", "High Balance", "", -100.0f, 100.0f, 0.0f},
    {"punch", "Punch", "", -100.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"CENTER", "dB", -60.0f, 0.0f}, {"SIDES", "dB", -60.0f, 0.0f}};
constexpr float kSplit[2] = {kLowHz, kHighHz};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& x : xo_) x.design(fs, kSplit, 3);
        reset();
    }
    void reset() noexcept override {
        for (auto& x : xo_) x.clear();
        fast_ = slow_ = 0.0f;
        mLevel_ = sLevel_ = 0.0;
        center_.prepare(fs_, 30, p(Center));
        sides_.prepare(fs_, 30, p(Sides));
        lowB_.prepare(fs_, 30, p(LowBalance) * 0.01f);
        highB_.prepare(fs_, 30, p(HighBalance) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float fAtt = coefMs(fs_, 0.5), fRel = coefMs(fs_, 40.0), sAtt = coefMs(fs_, 20.0);
        const float punch = p(Punch) * 0.01f;
        double mSum = 0.0, sSum = 0.0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            float mb[3], sb[3];
            xo_[0].split(m, mb);
            xo_[1].split(s, sb);
            // punch: while a hit rises, the middle (PUNCH +) or the sides (PUNCH -) come up and the other goes down
            const float x = std::fabs(m);
            fast_ = envStep(fast_, x, fAtt, fRel);
            slow_ = envStep(slow_, x, sAtt, fRel);
            const float hit = std::clamp((dbOf(fast_) - dbOf(slow_)) / 12.0f, 0.0f, 1.0f);
            const float steer = 6.0f * punch * hit;
            const float c = center_.next(p(Center)), sd = sides_.next(p(Sides));
            const float lb = 6.0f * lowB_.next(p(LowBalance) * 0.01f), hb = 6.0f * highB_.next(p(HighBalance) * 0.01f);
            const float bal[3] = {lb, 0.0f, hb};
            float mo = 0.0f, so = 0.0f;
            for (int b = 0; b < 3; ++b) {
                mo += mb[b] * hc::fx::dbToGain(c + bal[b] + steer);
                so += sb[b] * hc::fx::dbToGain(sd - bal[b] - steer);
            }
            mSum += static_cast<double>(mo) * mo;
            sSum += static_cast<double>(so) * so;
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (mo + so) * o;
            R[i] = (mo - so) * o;
        }
        const double k = 1.0 / std::max<std::uint32_t>(n, 1);
        mLevel_ += (mSum * k - mLevel_) * 0.3;
        sLevel_ += (sSum * k - sLevel_) * 0.3;
        publishMeter(0, std::max(-100.0f, powDb(2.0 * mLevel_)));
        publishMeter(1, std::max(-100.0f, powDb(2.0 * sLevel_)));
    }

private:
    double fs_ = 48000.0;
    Crossover<3> xo_[2];
    float fast_ = 0, slow_ = 0;
    double mLevel_ = 0, sLevel_ = 0;
    hc::fx::Smoothed center_, sides_, lowB_, highB_, out_;
};

} // namespace
} // namespace middleman

std::unique_ptr<hc::fx::Effect> makeMiddleman() { return std::make_unique<middleman::Impl>(); }

} // namespace hem::rack
