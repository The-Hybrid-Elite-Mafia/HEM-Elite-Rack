// TOMMY GUN — one-knob tempo pump (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace tommygun;

const char* const kRateN[] = {"1/16", "1/8", "1/4", "1/2", "1 BAR"};
const char* const kSourceN[] = {"HOST", "BPM"};
const ParamInfo kParams[kCount] = {
    {"amount", "Amount", "%", 0.0f, 100.0f, 0.0f},
    {"rate", "Rate", "", 0, 4, 2, Taper::Choice, kRateN},
    {"offset", "Offset", "deg", -180.0f, 180.0f, 0.0f},
    {"syncsource", "Tempo From", "", 0, 1, 0, Taper::Choice, kSourceN},
    {"bpm", "BPM", "", 60.0f, 200.0f, 120.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override { return i != Bpm || p(SyncSource) >= 0.5f; }

    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        beats_ = 0.0;
        g_ = 1.0f;
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int ri = std::clamp(static_cast<int>(std::lround(p(Rate))), 0, 4);
        const double bpm = p(SyncSource) >= 0.5f ? p(Bpm) : tempo();
        const double cycle = kRateBeats[ri];
        const double beatPerSample = bpm / 60.0 / fs_;
        const float amt = p(Amount) * 0.01f;
        const float depthDb = -24.0f * amt;                     // how deep it dips
        const float sharp = 3.0f + 6.0f * amt;                  // and how quickly it comes back up (harder with AMOUNT)
        const double off = p(Offset) / 360.0;
        const float smooth = coefMs(fs_, 1.5);                  // no clicks at the dip
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            double ph = beats_ / cycle + off;
            ph -= std::floor(ph);
            const float duck = static_cast<float>(std::exp(-ph * sharp));   // full dip on the beat, then back up
            const float targetDb = depthDb * duck;
            const float target = hc::fx::dbToGain(targetDb);
            g_ = target + smooth * (g_ - target);
            most = std::min(most, dbOf(g_));
            L[i] *= g_;
            R[i] *= g_;
            beats_ += beatPerSample;
            if (beats_ > 1e6) beats_ = std::fmod(beats_, 16.0);
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0, beats_ = 0.0;
    float g_ = 1.0f;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTommyGun() { return std::make_unique<Impl>(); }

} // namespace hem::rack
