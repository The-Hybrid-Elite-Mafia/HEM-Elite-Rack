// CONSIGLIERE — a two-knob leveler, light-cell or solid-state (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace consigliere;

const char* const kModelN[] = {"OPTO", "SOLID"};
const char* const kModeN[] = {"COMPRESS", "LIMIT"};
const ParamInfo kParams[kCount] = {
    {"peakreduction", "Peak Reduction", "%", 0.0f, 100.0f, 30.0f},
    {"gain", "Gain", "dB", 0.0f, 40.0f, 0.0f},
    {"model", "Model", "", 0, 1, 0, Taper::Choice, kModelN},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"emphasis", "Emphasis", "%", 0.0f, 100.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};

// The gain computer: the cell starts working when the (boosted) sidechain reaches 0 dBFS; PEAK
// REDUCTION boosts the sidechain up to 40 dB, so more turns = it starts on lower signals.
float computerDb(float x, bool limit, bool solid) noexcept {
    const float K = limit ? 4.0f : (solid ? 6.0f : 10.0f);
    const float ratio = limit ? (solid ? 20.0f : 12.0f) : 3.0f;
    return downDb(x, 0.0f, ratio, K);
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        mem_.set(fs, 3.0, 8.0);
        reset();
    }
    void reset() noexcept override {
        sc_[0].clear();
        sc_[1].clear();
        emph_ = -1.0f;
        fast_ = slow_ = 0.0f;
        mem_.v = 0.0f;
        solidMix_ = p(Model) >= 0.5f ? 1.0f : 0.0f;
        gain_.prepare(fs_, 30, hc::fx::dbToGain(p(Gain)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Emphasis) != emph_) {   // the detector's ear: up to +12 dB above ~1.5 kHz
            emph_ = p(Emphasis);
            const C c = C::make(T::HighShelf, fs_, 1500.0, 0.7071, emph_ * 0.12);
            sc_[0].c = sc_[1].c = c;
        }
        const bool limit = p(Mode) >= 0.5f, solid = p(Model) >= 0.5f;
        const float scDb = p(PeakReduction) * 0.4f;
        // OPTO: ~10 ms attack; half lets go in ~60 ms, half slowly (0.5 .. 5 s with memory).
        // SOLID: ~1.5 ms attack; ~40 ms then 0.3 .. 1.5 s.
        const float att = coefMs(fs_, solid ? 1.5 : 10.0);
        const float fastRel = coefMs(fs_, solid ? 40.0 : 60.0);
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = sc_[0].tick(L[i]), dr = sc_[1].tick(R[i]);
            const float level = std::max(std::fabs(dl), std::fabs(dr));
            const float target = computerDb(dbOf(level) + scDb, limit, solid);
            const double slowS = solid ? 0.3 + 1.2 * mem_.v : 0.5 + 4.5 * mem_.v;
            const float slowRel = coefMs(fs_, slowS * 1000.0);
            fast_ = target + (target < fast_ ? att : fastRel) * (fast_ - target);
            slow_ = target + (target < slow_ ? att : slowRel) * (slow_ - target);
            const float gr = 0.5f * (fast_ + slow_);
            mem_.tick(target < -3.0f);
            most = std::min(most, gr);
            const float g = hc::fx::dbToGain(gr) * gain_.next(hc::fx::dbToGain(p(Gain)));
            const float m = mix_.next(p(Mix) * 0.01f);
            solidMix_ = solid ? std::min(1.0f, solidMix_ + step) : std::max(0.0f, solidMix_ - step);
            float yl = L[i] * g, yr = R[i] * g;
            if (solidMix_ > 0.0f) {   // the solid-state amp's touch of grit: a little 3rd harmonic as it works
                const float d = solidMix_ * std::min(-gr, 12.0f) / 12.0f * 0.08f;
                yl *= 1.0f - d * yl * yl / (1.0f + yl * yl);   // bounded: never flips or runs away
                yr *= 1.0f - d * yr * yr / (1.0f + yr * yr);
            }
            L[i] += m * (yl - L[i]);
            R[i] += m * (yr - R[i]);
        }
        if (fast_ > -1e-6f) fast_ = 0.0f;
        if (slow_ > -1e-6f) slow_ = 0.0f;
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    hc::fx::Biquad sc_[2];
    float emph_ = -1.0f, fast_ = 0.0f, slow_ = 0.0f, solidMix_ = 0.0f;
    Memory mem_;
    hc::fx::Smoothed gain_, mix_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeConsigliere() { return std::make_unique<Impl>(); }

} // namespace hem::rack
