// BIG SHOT — one-band maximizer (see Maximizers.hpp).
#include "hem/rack/Maximizers.hpp"

#include "LimitKit.hpp"

namespace hem::rack {
namespace bigshot {
namespace {
using namespace maxim;

const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -30.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -30.0f, 0.0f, -0.3f},
    {"autorelease", "Auto Release", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"quantize", "Quantize", "", 0, 4, 0, Taper::Choice, kQuantN},
    {"dither", "Dither", "", 0, 2, 0, Taper::Choice, kDitherN},
    {"shaping", "Shaping", "", 0, 3, 1, Taper::Choice, kShapeN},
};
const hc::fx::MeterInfo kMeters[] = {{"MOST OFF", "dB", -30.0f, 0.0f}, {"INPUT", "dBFS", -60.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        one_.prepare(fs);
        idr_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        one_.clear();
        idr_.clear();
        drive_.prepare(fs_, 30, hc::fx::dbToGain(-p(Threshold)));
        ceil_.prepare(fs_, 30, hc::fx::dbToGain(p(Ceiling)));
        most_.clear();
        inPk_.clear();
        publishReduction(0.0f);
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float cT = hc::fx::dbToGain(p(Ceiling));
        idr_.set(kQuantBits[std::clamp(static_cast<int>(std::lround(p(Quantize))), 0, 4)], static_cast<int>(std::lround(p(Dither))),
                 static_cast<int>(std::lround(p(Shaping))), cT);
        OneBand::Set s;
        s.adaptive = p(AutoRelease) >= 0.5f;
        s.relS = 0.1f;      // AUTO RELEASE off: a steady 100 ms
        s.loS = 0.02f;
        s.hiS = 0.4f;
        float least = 1.0f, in = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float d = drive_.next(hc::fx::dbToGain(-p(Threshold)));
            s.ceiling = ceil_.next(cT);
            in = std::max({in, std::fabs(L[i]), std::fabs(R[i])});
            float l = L[i] * d, r = R[i] * d;
            least = std::min(least, one_.tick(s, l, r));
            L[i] = idr_.tick(0, l);
            R[i] = idr_.tick(1, r);
        }
        const float offDb = dbOf(least);
        publishReduction(offDb);
        publishMeter(0, -most_.push(-offDb, n, fs_));
        publishMeter(1, inPk_.push(dbOf(in), n, fs_));
    }

private:
    double fs_ = 48000.0;
    OneBand one_;
    Idr idr_;
    hc::fx::Smoothed drive_, ceil_;
    PeakHold most_, inPk_;
};

} // namespace
} // namespace bigshot

std::unique_ptr<hc::fx::Effect> makeBigShot() { return std::make_unique<bigshot::Impl>(); }

} // namespace hem::rack
