// TERRITORY (4 bands, minimum-phase split) and STRAIGHT EDGE (5 bands, linear phase) — multiband
// dynamics (see Arsenal.hpp; the engine is MultiDyn.hpp).
#include "hem/rack/Arsenal.hpp"

#include "MultiDyn.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;

const char* const kRelN[] = {"AUTO", "MANUAL"};
const char* const kBehaviorN[] = {"EASE OUT", "SNAP BACK"};
const char* const kMakeupN[] = {"MANUAL", "AUTO"};
const char* const kPushN[] = {"A", "B"};   // any change = a push

#define HEM_BAND(n, thr, att, rel)                                                                       \
    {"threshold" #n, "Band " #n " Threshold", "dB", -80.0f, 0.0f, thr},                                  \
    {"gain" #n, "Band " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                                          \
    {"range" #n, "Band " #n " Range", "dB", -24.0f, 18.0f, -6.0f},                                       \
    {"attack" #n, "Band " #n " Attack", "ms", 0.5f, 500.0f, att, Taper::Log},                            \
    {"release" #n, "Band " #n " Release", "ms", 5.0f, 5000.0f, rel, Taper::Log},                         \
    {"solo" #n, "Band " #n " Solo", "", 0, 1, 0, Taper::Toggle, kOnOff},                                 \
    {"bypass" #n, "Band " #n " Bypass", "", 0, 1, 0, Taper::Toggle, kOnOff}
#define HEM_MASTER                                                                                       \
    {"mthreshold", "Master Threshold", "dB", -40.0f, 40.0f, 0.0f},                                       \
    {"mgain", "Master Gain", "dB", -18.0f, 18.0f, 0.0f},                                                 \
    {"mrange", "Master Range", "dB", -24.0f, 24.0f, 0.0f},                                               \
    {"mattack", "Master Attack", "x", 0.1f, 10.0f, 1.0f, Taper::Log},                                    \
    {"mrelease", "Master Release", "x", 0.1f, 10.0f, 1.0f, Taper::Log}

namespace c4 {
using namespace territory;
const ParamInfo kParams[kCount] = {
    HEM_BAND(1, -30.0f, 10.0f, 150.0f), HEM_BAND(2, -30.0f, 8.0f, 120.0f), HEM_BAND(3, -30.0f, 5.0f, 90.0f), HEM_BAND(4, -30.0f, 3.0f, 70.0f),
    HEM_MASTER,
    {"x1", "Crossover 1", "Hz", 40.0f, 350.0f, 100.0f, Taper::Log},
    {"x2", "Crossover 2", "Hz", 150.0f, 3000.0f, 600.0f, Taper::Log},
    {"x3", "Crossover 3", "Hz", 1000.0f, 16000.0f, 5000.0f, Taper::Log},
    {"q", "Crossover Q", "", 0.1f, 0.75f, 0.6f},
    {"releasemode", "Release Mode", "", 0, 1, 0, Taper::Choice, kRelN},
    {"behavior", "Let-Go", "", 0, 1, 0, Taper::Choice, kBehaviorN},
    {"knee", "Knee", "", 0.0f, 100.0f, 50.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
} // namespace c4

namespace lin {
using namespace straightedge;
const ParamInfo kParams[kCount] = {
    HEM_BAND(1, -30.0f, 10.0f, 150.0f), HEM_BAND(2, -30.0f, 8.0f, 120.0f), HEM_BAND(3, -30.0f, 5.0f, 90.0f), HEM_BAND(4, -30.0f, 3.0f, 70.0f),
    HEM_BAND(5, -30.0f, 2.0f, 50.0f),
    HEM_MASTER,
    {"x1", "Crossover 1", "Hz", 40.0f, 350.0f, 92.0f, Taper::Log},
    {"x2", "Crossover 2", "Hz", 150.0f, 3000.0f, 545.0f, Taper::Log},
    {"x3", "Crossover 3", "Hz", 1024.0f, 4750.0f, 4000.0f, Taper::Log},
    {"x4", "Crossover 4", "Hz", 4000.0f, 16000.0f, 11071.0f, Taper::Log},
    {"makeup", "Makeup", "", 0, 1, 0, Taper::Choice, kMakeupN},
    {"adaptive", "Adaptive", "dB", -40.0f, 12.0f, -40.0f},
    {"releasemode", "Release Mode", "", 0, 1, 0, Taper::Choice, kRelN},
    {"behavior", "Let-Go", "", 0, 1, 1, Taper::Choice, kBehaviorN},
    {"knee", "Knee", "", 0.0f, 100.0f, 50.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"dither", "Dither", "", 0, 1, 1, Taper::Toggle, kOnOff},
};
} // namespace lin
#undef HEM_BAND
#undef HEM_MASTER

const hc::fx::MeterInfo kMeters4[] = {{"BAND 1", "dB", -24.0f, 18.0f, true}, {"BAND 2", "dB", -24.0f, 18.0f, true}, {"BAND 3", "dB", -24.0f, 18.0f, true},
                                      {"BAND 4", "dB", -24.0f, 18.0f, true}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};
const hc::fx::MeterInfo kMeters5[] = {{"BAND 1", "dB", -24.0f, 18.0f, true}, {"BAND 2", "dB", -24.0f, 18.0f, true}, {"BAND 3", "dB", -24.0f, 18.0f, true},
                                      {"BAND 4", "dB", -24.0f, 18.0f, true}, {"BAND 5", "dB", -24.0f, 18.0f, true}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};

// kLinear: STRAIGHT EDGE (5 bands); otherwise TERRITORY (4 bands)
template <bool kLinear>
class Impl final : public hc::fx::Effect {
    static constexpr int kB = kLinear ? 5 : 4;
    static constexpr int kPer = 7;
    static constexpr int kM = kB * kPer;   // the master block
    enum : int { kThr, kGain, kRange, kAtt, kRel, kSolo, kByp };

public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override {
        if constexpr (kLinear) return lin::kParams;
        else return c4::kParams;
    }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override {
        if constexpr (kLinear) return kMeters5;
        else return kMeters4;
    }
    std::uint32_t latency() const noexcept override { return MultiDyn<kB, kLinear>::kLatency; }

    void prepare(double fs, std::uint32_t) override {
        eng_.prepare(fs, settings());
        reset();
    }
    void reset() noexcept override {
        eng_.reset(settings());
        trimDb_ = 0.0f;
        if constexpr (kLinear) pushSeen_ = p(straightedge::Trim);
        publishReduction(0.0f);
        for (int b = 0; b <= kB; ++b) publishMeter(b, b < kB ? 0.0f : -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if constexpr (kLinear) {
            if (p(straightedge::Trim) != pushSeen_) {   // TRIM pushed: bring the loudest peak since the last push to 0 dBFS
                pushSeen_ = p(straightedge::Trim);
                if (eng_.outPeakDb() > -99.0f) trimDb_ = std::clamp(trimDb_ - eng_.outPeakDb(), -60.0f, 24.0f);
                eng_.clearPeak();
            }
        }
        eng_.process(settings(), L, R, n);
        float most = 0.0f;
        for (int b = 0; b < kB; ++b) {
            publishMeter(b, eng_.bandDb(b));
            most = std::min(most, eng_.bandDb(b));
        }
        publishMeter(kB, eng_.outPeakDb());
        publishReduction(most);
    }

private:
    DynSet<kB> settings() const noexcept {
        DynSet<kB> s;
        for (int b = 0; b < kB; ++b) {
            const int o = b * kPer;
            s.thr[b] = p(o + kThr) + p(kM + 0);
            s.gain[b] = p(o + kGain) + p(kM + 1);
            s.range[b] = std::clamp(p(o + kRange) + p(kM + 2), -24.0f, 18.0f);
            s.attMs[b] = p(o + kAtt) * p(kM + 3);
            s.relMs[b] = p(o + kRel) * p(kM + 4);
            s.solo[b] = p(o + kSolo) >= 0.5f;
            s.bypass[b] = p(o + kByp) >= 0.5f;
        }
        if constexpr (kLinear) {
            using namespace straightedge;
            for (int k = 0; k < 4; ++k) s.xo[k] = p(X1 + k);
            s.autoMakeup = p(Makeup) >= 0.5f;
            s.adaptive = p(Adaptive);
            s.autoRel = p(ReleaseMode) < 0.5f;
            s.opto = p(Behavior) < 0.5f;
            s.knee = p(Knee);
            s.outDb = p(Output) + trimDb_;
            s.dither = p(Dither) >= 0.5f;
        } else {
            using namespace territory;
            for (int k = 0; k < 3; ++k) s.xo[k] = p(X1 + k);
            s.q = p(Q);
            s.autoRel = p(ReleaseMode) < 0.5f;
            s.opto = p(Behavior) < 0.5f;
            s.knee = p(Knee);
            s.outDb = p(Output);
        }
        return s;
    }

    MultiDyn<kB, kLinear> eng_;
    float trimDb_ = 0.0f, pushSeen_ = 0.0f;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTerritory() { return std::make_unique<Impl<false>>(); }
std::unique_ptr<hc::fx::Effect> makeStraightEdge() { return std::make_unique<Impl<true>>(); }

} // namespace hem::rack
