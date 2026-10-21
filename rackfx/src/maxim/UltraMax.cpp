// TOP DOG (linear phase) and QUICK CASH (low latency) — the five-band maximizer run from a PROFILE
// (see Maximizers.hpp). The nine profiles are ours.
#include "hem/rack/Maximizers.hpp"

#include "FiveBand.hpp"

namespace hem::rack {
namespace ultramax {
namespace {
using namespace maxim;

const char* const kProfileN[kProfiles] = {"STANDARD", "OLD SCHOOL", "HALF SPLIT", "SINGLE AUTO", "SINGLE FIXED",
                                          "VELVET", "STRIPPED", "QUIET LIFT", "HEAVY"};
struct ProfileDef {
    float xo[4];
    float separation;   // 0..1
    float gain[5], prio[5], releaseMs[5];
    int behave;
};
// What each profile does:
//  STANDARD      even all-rounder: bands by their own energy, adaptive release.
//  OLD SCHOOL    lower crossovers, slower release that adapts most in the lows, a touch of low priority:
//                thick and soft, like an analog chain.
//  HALF SPLIT    the bands only half on their own: in between one band and five.
//  SINGLE AUTO   every band gives the same (works as one band), adaptive release.
//  SINGLE FIXED  as SINGLE AUTO with fixed releases.
//  VELVET        lows and low mids keep more, highs give more, slow release: smooth and warm.
//  STRIPPED      lows give more, presence keeps more, short releases adaptive in the highs: lean.
//  QUIET LIFT    long, adaptive releases: brings quiet parts up with little pumping.
//  HEAVY         fast, hard release and full separation: the loudest, with the most movement.
const ProfileDef kProfile[kProfiles] = {
    {{80, 320, 1278, 5113}, 1.0f, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {598, 503, 402, 298, 200}, Auto},
    {{100, 400, 1600, 6000}, 0.6f, {0, 0, 0, 0, 0}, {2, 1, 0, 0, 0}, {950, 800, 650, 480, 320}, AutoLows},
    {{80, 320, 1278, 5113}, 0.5f, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {598, 503, 402, 298, 200}, Auto},
    {{80, 320, 1278, 5113}, 0.0f, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {300, 300, 300, 300, 300}, Auto},
    {{80, 320, 1278, 5113}, 0.0f, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {300, 300, 300, 300, 300}, Manual},
    {{80, 320, 1278, 5113}, 0.8f, {0, 0, 0, 0, 0}, {2, 1, 0, -1, -2}, {840, 700, 560, 420, 280}, AutoLows},
    {{80, 320, 1278, 5113}, 1.0f, {0, 0, 0, 0, 0}, {-1, 0, 1, 1, 0}, {300, 250, 200, 150, 100}, AutoHighs},
    {{80, 320, 1278, 5113}, 1.0f, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {1200, 1000, 800, 600, 400}, Auto},
    {{80, 320, 1278, 5113}, 1.0f, {0, 0, 0, 0, 0}, {1, 0, 0, 0, 1}, {360, 300, 240, 180, 120}, Hard},
};

const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -30.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -30.0f, 0.0f, 0.0f},
    {"link", "Link", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"release", "Release", "x", 0.1f, 10.0f, 1.0f, Taper::Log},
    {"profile", "Profile", "", 0, kProfiles - 1, 0, Taper::Choice, kProfileN},
    {"quantize", "Bit Depth", "", 0, 4, 0, Taper::Choice, kQuantN},
    {"dither", "Dither", "", 0, 2, 0, Taper::Choice, kDitherN},
    {"shaping", "Noise Shape", "", 0, 3, 1, Taper::Choice, kShapeN},
};
const hc::fx::MeterInfo kMeters[] = {{"BAND 1", "dB", -30.0f, 0.0f}, {"BAND 2", "dB", -30.0f, 0.0f}, {"BAND 3", "dB", -30.0f, 0.0f},
                                     {"BAND 4", "dB", -30.0f, 0.0f}, {"BAND 5", "dB", -30.0f, 0.0f},
                                     {"INPUT PEAK", "dBFS", -60.0f, 3.0f}, {"OUTPUT PEAK", "dBFS", -60.0f, 3.0f}};

template <bool kLinear>
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return FiveBand<kLinear>::kLatency; }

    void prepare(double fs, std::uint32_t) override {
        eng_.prepare(fs, settings());
        reset();
    }
    void reset() noexcept override {
        eng_.reset(settings());
        publishReduction(0.0f);
        for (int b = 0; b < 5; ++b) publishMeter(b, 0.0f);
        publishMeter(5, -100.0f);
        publishMeter(6, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        publishReduction(eng_.process(settings(), L, R, n));
        for (int b = 0; b < 5; ++b) publishMeter(b, eng_.bandDb(b));
        publishMeter(5, eng_.inPeakDb());
        publishMeter(6, eng_.outPeakDb());
    }

private:
    FiveSettings settings() const noexcept {
        const ProfileDef& pr = kProfile[std::clamp(static_cast<int>(std::lround(p(Profile))), 0, kProfiles - 1)];
        FiveSettings s;
        s.thresholdDb = p(Threshold);
        s.ceilingDb = p(Ceiling);
        s.link = p(Link) >= 0.5f;
        s.behave = pr.behave;
        s.releaseMul = p(Release);
        for (int k = 0; k < 4; ++k) s.xo[k] = pr.xo[k];
        s.separation = pr.separation;
        for (int b = 0; b < 5; ++b) {
            s.gainDb[b] = pr.gain[b];
            s.prioDb[b] = pr.prio[b];
            s.releaseMs[b] = pr.releaseMs[b];
        }
        s.bits = kQuantBits[std::clamp(static_cast<int>(std::lround(p(Quantize))), 0, 4)];
        s.dither = static_cast<int>(std::lround(p(Dither)));
        s.shape = static_cast<int>(std::lround(p(Shaping)));
        return s;
    }

    FiveBand<kLinear> eng_;
};

} // namespace
} // namespace ultramax

std::unique_ptr<hc::fx::Effect> makeTopDog() { return std::make_unique<ultramax::Impl<true>>(); }
std::unique_ptr<hc::fx::Effect> makeQuickCash() { return std::make_unique<ultramax::Impl<false>>(); }

} // namespace hem::rack
