// WHEELMAN — automatic level rider for a vocal or a bass (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace wheelman;

const char* const kSourceN[] = {"VOCAL", "BASS"};
const char* const kSpeedN[] = {"SLOW", "FAST"};
const ParamInfo kParams[kCount] = {
    {"source", "Source", "", 0, 1, 0, Taper::Choice, kSourceN},
    {"target", "Target", "dB", -40.0f, 0.0f, -18.0f},
    {"sensitivity", "Sensitivity", "dB", -70.0f, -20.0f, -50.0f},
    {"maxboost", "Max Boost", "dB", 0.0f, 18.0f, 6.0f},
    {"maxcut", "Max Cut", "dB", 0.0f, 18.0f, 6.0f},
    {"speed", "Speed", "", 0, 1, 0, Taper::Choice, kSpeedN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"RIDE", "dB", -18.0f, 18.0f, true}, {"LEVEL", "dB", -70.0f, 0.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& b : det_) b.clear();
        designed_ = -1;
        ms_ = 0.0;
        ride_ = 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int src = p(Source) >= 0.5f ? 1 : 0;
        if (src != designed_) {   // what it listens to: the voice's range, or the bass's
            designed_ = src;
            const C a = src ? C::make(T::HighPass, fs_, 30.0, 0.7071) : C::make(T::HighPass, fs_, 150.0, 0.7071);
            const C b = src ? C::make(T::LowPass, fs_, 300.0, 0.7071) : C::make(T::LowPass, fs_, 8000.0, 0.7071);
            det_[0].c = det_[2].c = a;
            det_[1].c = det_[3].c = b;
        }
        const float rmsK = 1.0f - coefMs(fs_, src ? 80.0 : 30.0);
        const float rideC = coefMs(fs_, p(Speed) >= 0.5f ? 60.0 : 300.0);
        const float target = p(Target), sens = p(Sensitivity), up = p(MaxBoost), down = p(MaxCut);
        float lvShown = -100.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = det_[1].tick(det_[0].tick(L[i])), dr = det_[3].tick(det_[2].tick(R[i]));
            const double sq = 0.5 * (static_cast<double>(dl) * dl + static_cast<double>(dr) * dr);
            ms_ += (sq - ms_) * rmsK;
            const float lv = powDb(2.0 * ms_);
            lvShown = lv;
            // under SENSITIVITY (a gap, breath, room): hold the wheel still
            if (lv > sens) {
                const float want = std::clamp(target - lv, -down, up);
                ride_ = want + rideC * (ride_ - want);
            }
            ride_ = std::clamp(ride_, -down, up);   // turning a range down takes effect at once
            const float g = hc::fx::dbToGain(ride_) * out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
        }
        publishMeter(0, ride_);
        publishMeter(1, std::max(lvShown, -100.0f));
        publishReduction(std::min(ride_, 0.0f));
    }

private:
    double fs_ = 48000.0;
    hc::fx::Biquad det_[4];
    int designed_ = -1;
    double ms_ = 0;
    float ride_ = 0;
    hc::fx::Smoothed out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeWheelman() { return std::make_unique<Impl>(); }

} // namespace hem::rack
