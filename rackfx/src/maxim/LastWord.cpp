// LAST WORD — one-band maximizer with input faders, release and ceiling domain (see Maximizers.hpp).
#include "hem/rack/Maximizers.hpp"

#include "LimitKit.hpp"

namespace hem::rack {
namespace lastword {
namespace {
using namespace maxim;

const char* const kDomainN[] = {"SAMPLE", "SMOOTHED", "TRUE PEAK"};
const char* const kQuantN6[] = {"NONE", "8 BIT", "12 BIT", "16 BIT", "20 BIT", "24 BIT"};
constexpr int kBits6[] = {0, 8, 12, 16, 20, 24};
const ParamInfo kParams[kCount] = {
    {"inputl", "Input Left", "dB", -60.0f, 0.0f, 0.0f},
    {"inputr", "Input Right", "dB", -60.0f, 0.0f, 0.0f},
    {"inputlink", "Input Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"threshold", "Threshold", "dB", -30.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -30.0f, 0.0f, 0.0f},
    {"release", "Release", "ms", 0.01f, 1000.0f, 100.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"domain", "Peak Mode", "", 0, 2, 0, Taper::Choice, kDomainN},
    {"quantize", "Bit Depth", "", 0, 5, 0, Taper::Choice, kQuantN6},
    {"dither", "Dither", "", 0, 2, 0, Taper::Choice, kDitherN},
    {"shaping", "Noise Shape", "", 0, 3, 3, Taper::Choice, kShapeN},
};
const hc::fx::MeterInfo kMeters[] = {{"IN LEFT", "dBFS", -60.0f, 3.0f}, {"IN RIGHT", "dBFS", -60.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i == InputR) return p(InputLink) < 0.5f;
        if (i == Dither || i == Shaping) return p(Quantize) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        one_.prepare(fs);
        idr_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        one_.clear();
        idr_.clear();
        inL_.prepare(fs_, 30, hc::fx::dbToGain(p(InputL)));
        inR_.prepare(fs_, 30, hc::fx::dbToGain(rightDb()));
        drive_.prepare(fs_, 30, hc::fx::dbToGain(-p(Threshold)));
        ceil_.prepare(fs_, 30, hc::fx::dbToGain(p(Ceiling)));
        pkL_.clear();
        pkR_.clear();
        publishReduction(0.0f);
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float cT = hc::fx::dbToGain(p(Ceiling));
        idr_.set(kBits6[std::clamp(static_cast<int>(std::lround(p(Quantize))), 0, 5)], static_cast<int>(std::lround(p(Dither))),
                 static_cast<int>(std::lround(p(Shaping))), cT);
        OneBand::Set s;
        s.relS = p(Release) * 0.001f;
        s.adaptive = p(AutoRelease) >= 0.5f;
        s.loS = s.relS * 0.2f;   // AUTO: a single peak lets go 5x quicker; working hard, at RELEASE
        s.hiS = s.relS;
        s.domain = std::clamp(static_cast<int>(std::lround(p(Domain))), 0, 2);
        const float gl = hc::fx::dbToGain(p(InputL)), gr = hc::fx::dbToGain(rightDb());
        float least = 1.0f, pl = 0.0f, pr = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i] * inL_.next(gl), r = R[i] * inR_.next(gr);
            pl = std::max(pl, std::fabs(l));
            pr = std::max(pr, std::fabs(r));
            const float d = drive_.next(hc::fx::dbToGain(-p(Threshold)));
            s.ceiling = ceil_.next(cT);
            l *= d;
            r *= d;
            least = std::min(least, one_.tick(s, l, r));
            L[i] = idr_.tick(0, l);
            R[i] = idr_.tick(1, r);
        }
        publishReduction(dbOf(least));
        publishMeter(0, pkL_.push(dbOf(pl), n, fs_));
        publishMeter(1, pkR_.push(dbOf(pr), n, fs_));
    }

private:
    float rightDb() const noexcept { return p(InputLink) >= 0.5f ? p(InputL) : p(InputR); }

    double fs_ = 48000.0;
    OneBand one_;
    Idr idr_;
    hc::fx::Smoothed inL_, inR_, drive_, ceil_;
    PeakHold pkL_, pkR_;
};

} // namespace
} // namespace lastword

std::unique_ptr<hc::fx::Effect> makeLastWord() { return std::make_unique<lastword::Impl>(); }

} // namespace hem::rack
