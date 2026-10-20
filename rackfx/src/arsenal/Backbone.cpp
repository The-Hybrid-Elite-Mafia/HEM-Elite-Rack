// BACKBONE — two-fader loudness: lifts the quiet parts and holds the loud ones (see Arsenal.hpp).
//
// LOW LEVEL 0..48 dB: how much the quiet parts come up. Under its threshold (LOW LEVEL - 60 dBFS: higher
// = it starts on louder parts) the sound is lifted 1 dB for every 2 dB it sits under, by at most LOW
// LEVEL. HIGH LEVEL 0..-48 dB: the threshold the loud parts are held down from (3:1), with automatic
// make-up so a full-scale peak stays at full scale. OUTPUT 0..-48 dB.
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace backbone {
namespace {
using namespace arsenal;

const ParamInfo kParams[kCount] = {
    {"lowlevel", "Low Level", "dB", 0.0f, 48.0f, 0.0f},
    {"highlevel", "High Level", "dB", -48.0f, 0.0f, 0.0f},
    {"output", "Output", "dB", -48.0f, 0.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"CUT", "dB", -48.0f, 0.0f}, {"BOOST", "dB", 0.0f, 48.0f}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        env_ = 0.0f;
        boost_ = cut_ = 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        mk_.prepare(fs_, 30, hc::fx::dbToGain(makeupDb()));
        pk_.clear();
        publishMeter(0, 0.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float low = p(LowLevel), high = p(HighLevel);
        const float lowThr = low - 60.0f;
        const float envAtt = coefMs(fs_, 1.0), envRel = coefMs(fs_, 120.0);
        const float cAtt = coefMs(fs_, 1.0), cRel = coefMs(fs_, 150.0), bAtt = coefMs(fs_, 50.0), bRel = coefMs(fs_, 150.0);
        float mostCut = 0.0f, mostBoost = 0.0f, pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            env_ = x + (x > env_ ? envAtt : envRel) * (env_ - x);
            const float lv = dbOf(env_);
            const float bT = env_ < 1e-7f ? 0.0f : std::min(low, std::max(0.0f, lowThr - lv) * 0.5f);
            boost_ = bT + (bT < boost_ ? bAtt : bRel) * (boost_ - bT);   // lifts come in gently, cuts go first
            const float cT = downDb(lv + boost_, high, 3.0f, 6.0f);
            cut_ = follow(cut_, cT, cAtt, cRel);
            mostCut = std::min(mostCut, cut_);
            mostBoost = std::max(mostBoost, boost_);
            const float g = hc::fx::dbToGain(boost_ + cut_) * mk_.next(hc::fx::dbToGain(makeupDb())) * out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        publishMeter(0, mostCut);
        publishMeter(1, mostBoost);
        publishMeter(2, pk_.push(dbOf(pk), n, fs_));
        publishReduction(mostCut);
    }

private:
    float makeupDb() const noexcept { return -downDb(0.0f, p(HighLevel), 3.0f, 6.0f); }

    double fs_ = 48000.0;
    float env_ = 0.0f, boost_ = 0.0f, cut_ = 0.0f;
    hc::fx::Smoothed out_, mk_;
    maxim::PeakHold pk_;
};

} // namespace
} // namespace backbone

std::unique_ptr<hc::fx::Effect> makeBackbone() { return std::make_unique<backbone::Impl>(); }

} // namespace hem::rack
