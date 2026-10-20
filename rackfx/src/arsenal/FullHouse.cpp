// FULL HOUSE — loudness without squash: gate, lift the quiet, ride the average, hold the loud (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace fullhouse;

const char* const kSpeedN[] = {"SOFT", "LOUD"};
const ParamInfo kParams[kCount] = {
    {"gate", "Gate", "dB", -90.0f, -30.0f, -90.0f},
    {"lowthreshold", "Low Threshold", "dB", -60.0f, -10.0f, -40.0f},
    {"lowlift", "Max Lift", "dB", 0.0f, 24.0f, 0.0f},
    {"leveler", "Leveler", "%", 0.0f, 100.0f, 0.0f},
    {"levelspeed", "Leveler Speed", "", 0, 1, 0, Taper::Choice, kSpeedN},
    {"highlevel", "High Level", "%", 0.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"LIFT", "dB", 0.0f, 24.0f}, {"LEVELER", "dB", -12.0f, 12.0f, true}, {"HOLD", "dB", -24.0f, 0.0f}};
constexpr float kLevelTarget = -18.0f;   // the leveler aims the average here (dBFS RMS)
constexpr float kGateDepth = -30.0f;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == LowThreshold) return p(LowLift) > 0.0f;
        if (i == LevelSpeed) return p(Leveler) > 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        ms_ = msSlow_ = 0.0;
        peakEnv_ = 0.0f;
        gate_ = lift_ = level_ = hold_ = 0.0f;
        open_ = false;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
        publishMeter(0, 0.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float rmsK = 1.0f - coefMs(fs_, 10.0);
        const float slowK = 1.0f - coefMs(fs_, p(LevelSpeed) >= 0.5f ? 400.0 : 2000.0);
        const float gThr = p(Gate);
        const bool gateOn = gThr > -89.5f;
        const float lowThr = p(LowThreshold), maxLift = p(LowLift), lev = p(Leveler) * 0.01f, hi = p(HighLevel) * 0.01f;
        const float hiThr = -6.0f - 24.0f * hi, hiRatio = 1.0f + 4.0f * hi;
        const float hiMakeup = hi > 0.0f ? -0.5f * downDb(0.0f, hiThr, hiRatio, 6.0f) : 0.0f;
        const float gAtt = coefMs(fs_, 1.0), gRel = coefMs(fs_, 200.0);
        const float upAtt = coefMs(fs_, 50.0), upRel = coefMs(fs_, 250.0);
        const float hAtt = coefMs(fs_, 5.0), hRel = coefMs(fs_, 150.0), envRel = coefMs(fs_, 5.0);
        const float levC = coefMs(fs_, p(LevelSpeed) >= 0.5f ? 300.0 : 1000.0);
        float maxLiftSeen = 0.0f, minHold = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const double sq = std::max(static_cast<double>(L[i]) * L[i], static_cast<double>(R[i]) * R[i]);
            ms_ += (sq - ms_) * rmsK;
            msSlow_ += (sq - msSlow_) * slowK;
            const float lv = powDb(2.0 * ms_);   // as a sine's peak would read
            // gate: quiet noise stays down (4 dB hysteresis); nothing below it is lifted
            if (!gateOn || lv >= gThr) open_ = true;
            else if (lv < gThr - 4.0f) open_ = false;
            const float gT = open_ ? 0.0f : kGateDepth;
            gate_ = gT + (gT < gate_ ? gAtt : gRel) * (gate_ - gT);
            // low level: lift the quiet parts, half of how far under LOW THRESHOLD they are, at most MAX LIFT
            float liftT = 0.0f;
            if (open_ && maxLift > 0.0f && lv < lowThr && lv > -80.0f) liftT = std::min(maxLift, 0.5f * (lowThr - lv));
            lift_ = liftT + (liftT > lift_ ? upAtt : upRel) * (lift_ - liftT);
            // leveler: rides the slow average toward -18 dBFS, up to 12 dB either way
            const float slowLv = powDb(2.0 * msSlow_);
            float levT = 0.0f;
            if (lev > 0.0f && open_ && slowLv > -60.0f) levT = std::clamp((kLevelTarget - slowLv) * lev, -12.0f, 12.0f);
            level_ = levT + levC * (level_ - levT);   // and moves there smoothly
            // high level: hold the loud parts (after lift and leveler)
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i])) * hc::fx::dbToGain(lift_ + level_);
            peakEnv_ = x > peakEnv_ ? x : x + envRel * (peakEnv_ - x);
            const float hT = hi > 0.0f ? downDb(dbOf(peakEnv_), hiThr, hiRatio, 6.0f) : 0.0f;
            hold_ = follow(hold_, hT, hAtt, hRel);
            maxLiftSeen = std::max(maxLiftSeen, lift_);
            minHold = std::min(minHold, hold_);
            const float g = hc::fx::dbToGain(gate_ + lift_ + level_ + hold_ + hiMakeup) * out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
        }
        publishReduction(std::min(minHold, gate_));
        publishMeter(0, maxLiftSeen);
        publishMeter(1, level_);
        publishMeter(2, minHold);
    }

private:
    double fs_ = 48000.0;
    double ms_ = 0, msSlow_ = 0;
    float peakEnv_ = 0, gate_ = 0, lift_ = 0, level_ = 0, hold_ = 0;
    bool open_ = false;
    hc::fx::Smoothed out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeFullHouse() { return std::make_unique<Impl>(); }

} // namespace hem::rack
