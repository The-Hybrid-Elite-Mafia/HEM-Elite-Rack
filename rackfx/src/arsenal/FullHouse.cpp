// FULL HOUSE — loudness without squash: leveler, gate, lift the quiet, hold the loud (see Arsenal.hpp).
//
// In order: the LEVELER (very slow: anything louder than its fader is brought toward it; LOUD lets go
// sooner and comes out louder, SOFT later and softer), the GATE (under its threshold a gentle downward
// expansion, 1 dB per dB, at most 30 dB), the LOW LEVEL (under its threshold the sound is lifted 1 dB
// for every 2 under, by at most GAIN), the HIGH LEVEL (over its threshold held down 4:1 with automatic
// make-up, then nothing ever goes past its GAIN: the most that comes out). Each section has ON.
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace fullhouse {
namespace {
using namespace arsenal;

const char* const kLevelN[] = {"LOUD", "SOFT"};
const ParamInfo kParams[kCount] = {
    {"gateon", "Gate On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"gate", "Gate", "dB", -96.0f, 0.0f, -60.0f},
    {"lowon", "Low Level On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"lowthreshold", "Low Threshold", "dB", -96.0f, 0.0f, -40.0f},
    {"lowgain", "Low Gain", "dB", 0.0f, 48.0f, 6.0f},
    {"leveleron", "Leveler On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"leveler", "Leveler", "dB", -48.0f, 0.0f, -12.0f},
    {"levelermode", "Leveler Release", "", 0, 1, 0, Taper::Choice, kLevelN},
    {"highon", "High Level On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"highthreshold", "High Threshold", "dB", -48.0f, 0.0f, -6.0f},
    {"highgain", "High Gain", "dB", -48.0f, 0.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"ENERGY", "dB", -96.0f, 0.0f}, {"LOW GAIN", "dB", 0.0f, 48.0f}, {"HIGH", "dB", -48.0f, 0.0f},
                                     {"OUTPUT", "dBFS", -60.0f, 3.0f}};

// rounds into the ceiling (linear) above 90 % of it; never past it
inline float roundInto(float y, float c) noexcept {
    const float k = 0.9f * c, a = std::fabs(y);
    if (a <= k) return y;
    const float t = (a - k) / (c - k);
    return std::copysign(k + (c - k) * t / (1.0f + t), y);
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == Gate) return p(GateOn) >= 0.5f;
        if (i == LowThreshold || i == LowGain) return p(LowOn) >= 0.5f;
        if (i == Leveler || i == LevelerMode) return p(LevelerOn) >= 0.5f;
        if (i == HighThreshold || i == HighGain) return p(HighOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        rms_ = 0.0;
        lev_ = gate_ = lift_ = hold_ = 0.0f;
        env_ = 0.0f;
        mk_.prepare(fs_, 30, hc::fx::dbToGain(makeupDb()));
        ceil_.prepare(fs_, 30, ceiling());
        pk_.clear();
        for (int m = 0; m < 3; ++m) publishMeter(m, m == 0 ? -100.0f : 0.0f);
        publishMeter(3, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const bool gateOn = p(GateOn) >= 0.5f, lowOn = p(LowOn) >= 0.5f, levOn = p(LevelerOn) >= 0.5f, highOn = p(HighOn) >= 0.5f;
        const float rmsK = 1.0f - coefMs(fs_, 50.0);
        const float levAtt = coefMs(fs_, 1500.0), levRel = coefMs(fs_, p(LevelerMode) < 0.5f ? 2500.0 : 6000.0);
        const float envAtt = coefMs(fs_, 1.0), envRel = coefMs(fs_, 100.0);
        const float gAtt = coefMs(fs_, 5.0), gRel = coefMs(fs_, 150.0), lAtt = coefMs(fs_, 80.0), lRel = coefMs(fs_, 200.0);
        const float hAtt = coefMs(fs_, 1.0), hRel = coefMs(fs_, 120.0);
        float energy = -100.0f, liftMost = 0.0f, holdMost = 0.0f, pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            // the leveler: slow, toward its fader, only ever down
            rms_ += (0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r) - rms_) * rmsK;
            const float avg = powDb(2.0 * rms_);
            const float levT = levOn ? std::min(0.0f, p(Leveler) - avg) : 0.0f;
            lev_ = levT + (levT < lev_ ? levAtt : levRel) * (lev_ - levT);
            const float gl = hc::fx::dbToGain(lev_);
            l *= gl;
            r *= gl;
            // what the rest hears: the levelled sound
            const float x = std::max(std::fabs(l), std::fabs(r));
            env_ = x + (x > env_ ? envAtt : envRel) * (env_ - x);
            const float lv = dbOf(env_);
            energy = lv;
            const float gT = gateOn ? std::max(-std::max(p(Gate) - lv, 0.0f), -30.0f) : 0.0f;
            gate_ = gT + (gT > gate_ ? gAtt : gRel) * (gate_ - gT);
            const float lT = lowOn && env_ > 1e-7f ? std::min(p(LowGain), std::max(0.0f, p(LowThreshold) - lv) * 0.5f) : 0.0f;
            lift_ = lT + (lT > lift_ ? lAtt : lRel) * (lift_ - lT);
            const float hT = highOn ? downDb(lv + lift_ + gate_, p(HighThreshold), 4.0f, 6.0f) : 0.0f;
            hold_ = follow(hold_, hT, hAtt, hRel);
            liftMost = std::max(liftMost, lift_);
            holdMost = std::min(holdMost, hold_);
            const float g = hc::fx::dbToGain(gate_ + lift_ + hold_) * mk_.next(hc::fx::dbToGain(makeupDb()));
            l *= g;
            r *= g;
            if (highOn) {   // HIGH GAIN: the most that comes out
                const float c = ceil_.next(ceiling());
                l = std::clamp(roundInto(l, c), -c, c);
                r = std::clamp(roundInto(r, c), -c, c);
            }
            L[i] = l;
            R[i] = r;
            pk = std::max({pk, std::fabs(l), std::fabs(r)});
        }
        publishMeter(0, std::max(energy, -100.0f));
        publishMeter(1, liftMost);
        publishMeter(2, holdMost);
        publishMeter(3, pk_.push(dbOf(pk), n, fs_));
        publishReduction(std::min(holdMost + std::min(lev_, 0.0f), 0.0f));
    }

private:
    float makeupDb() const noexcept { return p(HighOn) >= 0.5f ? -downDb(0.0f, p(HighThreshold), 4.0f, 6.0f) + p(HighGain) : 0.0f; }
    float ceiling() const noexcept { return hc::fx::dbToGain(p(HighGain)); }

    double fs_ = 48000.0, rms_ = 0.0;
    float lev_ = 0, gate_ = 0, lift_ = 0, hold_ = 0, env_ = 0;
    hc::fx::Smoothed mk_, ceil_;
    maxim::PeakHold pk_;
};

} // namespace
} // namespace fullhouse

std::unique_ptr<hc::fx::Effect> makeFullHouse() { return std::make_unique<fullhouse::Impl>(); }

} // namespace hem::rack
