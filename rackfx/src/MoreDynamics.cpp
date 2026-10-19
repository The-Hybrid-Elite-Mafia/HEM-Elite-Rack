// Wave 7 dynamics: VCA COMP, PUNCH COMP, FOCUS BUS, DIODE BUS, GATE, MULTIBAND (see MoreDynamics.hpp).
#include "hem/rack/MoreDynamics.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {

// Gain change for `over` dB above the threshold with a given slope (dB of change per dB over) and
// a quadratic knee of kneeDb around the threshold.
static float slopeKneeDb(float over, float slope, float kneeDb) noexcept {
    if (kneeDb <= 1e-3f) return over > 0.0f ? slope * over : 0.0f;
    if (over <= -kneeDb * 0.5f) return 0.0f;
    if (over >= kneeDb * 0.5f) return slope * over;
    const float k = over + kneeDb * 0.5f;
    return slope * k * k / (2.0f * kneeDb);
}

float kneeGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept {
    return slopeKneeDb(levelDb - thresholdDb, 1.0f / std::max(ratio, 1.0f) - 1.0f, kneeDb);
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;
using OS = hc::fx::Oversampler4;

inline float dbOf(float x) noexcept { return 20.0f * std::log10(std::max(x, 1e-9f)); }
inline float coefFor(double fs, double ms) noexcept { return static_cast<float>(std::exp(-1.0 / (fs * std::max(ms, 0.001) * 0.001))); }
// one step of a dB-domain gain smoother: attack when more reduction is wanted, release otherwise
inline float follow(float gr, float target, float att, float rel) noexcept {
    return target + (target < gr ? att : rel) * (gr - target);
}
const char* const kOnOff[] = {"OFF", "ON"};

// The "work" memory used by the program-dependent releases: charges while it compresses harder
// than 1 dB (2 s), drains when it rests (3 s). 0..1.
struct Work {
    float v = 0, charge = 0, drain = 0;
    void set(double fs) noexcept { charge = 1.0f - coefFor(fs, 2000); drain = 1.0f - coefFor(fs, 3000); }
    void tick(float gr) noexcept { v += gr < -1.0f ? (1.0f - v) * charge : -v * drain; }
};

// ---- VCA COMP -------------------------------------------------------------------------------------
namespace vc {
using namespace vca;
const char* const kScN[] = {"OFF", "60 Hz", "90 Hz", "150 Hz", "250 Hz"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -60.0f, 0.0f, -20.0f},
    {"ratio", "Ratio", ":1", 1.0f, 20.0f, 4.0f, Taper::Log},
    {"knee", "Knee", "dB", 0.0f, 24.0f, 6.0f},
    {"attack", "Attack", "ms", 0.1f, 100.0f, 10.0f, Taper::Log},
    {"release", "Release", "ms", 10.0f, 2000.0f, 150.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f},
    {"automakeup", "Auto Makeup", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"schpf", "Sidechain HPF", "", 0, 4, 0, Taper::Choice, kScN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        if (i == Release) return p(AutoRelease) < 0.5f;
        if (i == Makeup) return p(AutoMakeup) < 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; work_.set(fs); reset(); }
    void reset() noexcept override {
        sc_[0].clear();
        sc_[1].clear();
        scIdx_ = -1;
        gr_ = 0.0f;
        env_ = 0.0f;
        work_.v = 0.0f;
        makeup_.prepare(fs_, 30, makeupDb());
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int sc = std::clamp(static_cast<int>(std::lround(p(ScHpf))), 0, 4);
        if (sc != scIdx_) {
            scIdx_ = sc;
            if (sc > 0) sc_[0].c = sc_[1].c = C::make(T::HighPass, fs_, kScHz[sc], 0.7071);
        }
        const float thr = p(Threshold), ratio = p(Ratio), knee = p(Knee);
        const float att = coefFor(fs_, p(Attack));
        const bool autoRel = p(AutoRelease) >= 0.5f;
        const float relManual = coefFor(fs_, p(Release));
        const float envRel = coefFor(fs_, 5.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float dl = L[i], dr = R[i];
            if (sc > 0) { dl = static_cast<float>(sc_[0].tick(dl)); dr = static_cast<float>(sc_[1].tick(dr)); }
            const float x = std::max(std::fabs(dl), std::fabs(dr));
            env_ = x > env_ ? x : x + envRel * (env_ - x);   // peak detector: holds the tops of the waves
            const float target = kneeGainDb(dbOf(env_), thr, ratio, knee);
            // AUTO: lets go in 40 ms after a short hit, up to 640 ms after it has been working a while
            const float rel = autoRel ? coefFor(fs_, 40.0 + 600.0 * work_.v) : relManual;
            gr_ = follow(gr_, target, att, rel);
            work_.tick(gr_);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_ + makeup_.next(makeupDb()));
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (L[i] * g - L[i])) * o;
            R[i] = (R[i] + m * (R[i] * g - R[i])) * o;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }
private:
    // AUTO MAKEUP gives back half of what the curve takes off a full-scale signal.
    float makeupDb() const noexcept { return p(AutoMakeup) >= 0.5f ? -0.5f * kneeGainDb(0.0f, p(Threshold), p(Ratio), p(Knee)) : p(Makeup); }
    double fs_ = 48000;
    hc::fx::Biquad sc_[2];
    int scIdx_ = -1;
    float gr_ = 0, env_ = 0;
    Work work_;
    hc::fx::Smoothed makeup_, mix_, out_;
};
} // namespace vc

} // namespace

std::unique_ptr<hc::fx::Effect> makeVcaComp() { return std::make_unique<vc::Impl>(); }

} // namespace hem::rack
