// HIGH STAKES (linear phase) and FAST MONEY (low latency) — five-band maximizers, every setting
// on the plate (see Maximizers.hpp).
#include "hem/rack/Maximizers.hpp"

#include "FiveBand.hpp"

namespace hem::rack {
namespace multimax {
namespace {
using namespace maxim;

#define HEM_BAND(n, rel, relMax)                                                                          \
    {"gain" #n, "Band " #n " Gain", "dB", -12.0f, 12.0f, 0.0f},                                           \
    {"priority" #n, "Band " #n " Priority", "dB", -12.0f, 12.0f, 0.0f},                                   \
    {"link" #n, "Band " #n " Link", "", 0, 1, 0, Taper::Toggle, kOnOff},                                  \
    {"release" #n, "Band " #n " Release", "ms", 0.1f, relMax, rel, Taper::Log},                           \
    {"solo" #n, "Band " #n " Solo", "", 0, 1, 0, Taper::Toggle, kOnOff}

#define HEM_GLOBALS                                                                                       \
    {"threshold", "Threshold", "dB", -30.0f, 0.0f, 0.0f},                                                 \
    {"ceiling", "Ceiling", "dB", -30.0f, 0.0f, 0.0f},                                                     \
    {"link", "Link", "", 0, 1, 0, Taper::Toggle, kOnOff},                                                 \
    {"releasetype", "Release Type", "", 0, 4, 0, Taper::Choice, kBehaveN},                                \
    {"release", "Release", "x", 0.1f, 10.0f, 1.0f, Taper::Log},                                           \
    {"xover1", "Crossover 1", "Hz", 40.0f, 350.0f, 80.0f, Taper::Log},                                    \
    {"xover2", "Crossover 2", "Hz", 150.0f, 3000.0f, 320.0f, Taper::Log},                                 \
    {"xover3", "Crossover 3", "Hz", 1022.0f, 4757.0f, 1278.0f, Taper::Log},                               \
    {"xover4", "Crossover 4", "Hz", 4000.0f, 16000.0f, 5113.0f, Taper::Log},                              \
    {"separation", "Separation", "%", 0.0f, 100.0f, 100.0f},                                              \
    {"quantize", "Quantize", "", 0, 4, 0, Taper::Choice, kQuantN},                                        \
    {"dither", "Dither", "", 0, 2, 0, Taper::Choice, kDitherN},                                           \
    {"shaping", "Shaping", "", 0, 3, 1, Taper::Choice, kShapeN}

const ParamInfo kLinearParams[kCount] = {
    HEM_GLOBALS,
    HEM_BAND(1, 598.0f, 5000.0f), HEM_BAND(2, 503.0f, 5000.0f), HEM_BAND(3, 402.0f, 5000.0f), HEM_BAND(4, 298.0f, 5000.0f), HEM_BAND(5, 200.0f, 5000.0f),
};
const ParamInfo kFastParams[kCount] = {
    HEM_GLOBALS,
    HEM_BAND(1, 598.0f, 2500.0f), HEM_BAND(2, 503.0f, 2500.0f), HEM_BAND(3, 402.0f, 2500.0f), HEM_BAND(4, 298.0f, 2500.0f), HEM_BAND(5, 200.0f, 2500.0f),
};
#undef HEM_BAND
#undef HEM_GLOBALS

const hc::fx::MeterInfo kMeters[] = {{"BAND 1", "dB", -30.0f, 0.0f}, {"BAND 2", "dB", -30.0f, 0.0f}, {"BAND 3", "dB", -30.0f, 0.0f},
                                     {"BAND 4", "dB", -30.0f, 0.0f}, {"BAND 5", "dB", -30.0f, 0.0f}};

template <bool kLinear>
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kLinear ? kLinearParams : kFastParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return FiveBand<kLinear>::kLatency; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        eng_.prepare(fs, settings());
        reset();
    }
    void reset() noexcept override {
        eng_.reset(settings());
        publishReduction(0.0f);
        for (int b = 0; b < kBands; ++b) publishMeter(b, 0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float most = eng_.process(settings(), L, R, n);
        publishReduction(most);
        for (int b = 0; b < kBands; ++b) publishMeter(b, eng_.bandDb(b));
    }

private:
    FiveSettings settings() const noexcept {
        FiveSettings s;
        s.thresholdDb = p(Threshold);
        s.ceilingDb = p(Ceiling);
        s.link = p(Link) >= 0.5f;
        s.behave = std::clamp(static_cast<int>(std::lround(p(ReleaseType))), 0, 4);
        s.releaseMul = p(Release);
        for (int k = 0; k < 4; ++k) s.xo[k] = p(X1 + k);
        s.separation = p(Separation) * 0.01f;
        for (int b = 0; b < kBands; ++b) {
            s.gainDb[b] = p(band(b, Gain));
            s.prioDb[b] = p(band(b, Priority));
            s.bandLink[b] = p(band(b, BandLink)) >= 0.5f;
            s.releaseMs[b] = p(band(b, BandRelease));
            s.solo[b] = p(band(b, Solo)) >= 0.5f;
        }
        s.bits = kQuantBits[std::clamp(static_cast<int>(std::lround(p(Quantize))), 0, 4)];
        s.dither = static_cast<int>(std::lround(p(Dither)));
        s.shape = static_cast<int>(std::lround(p(Shaping)));
        return s;
    }

    double fs_ = 48000.0;
    FiveBand<kLinear> eng_;
};

} // namespace
} // namespace multimax

std::unique_ptr<hc::fx::Effect> makeHighStakes() { return std::make_unique<multimax::Impl<true>>(); }
std::unique_ptr<hc::fx::Effect> makeFastMoney() { return std::make_unique<multimax::Impl<false>>(); }

} // namespace hem::rack
