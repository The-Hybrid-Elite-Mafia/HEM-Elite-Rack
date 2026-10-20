// KNOCKOUT — transient designer (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace knockout;

const char* const kAttShapeN[] = {"SHARP", "PUNCHY", "ROUND"};
const char* const kSusShapeN[] = {"EVEN", "CURVED", "SOFT"};
const char* const kGuardN[] = {"OFF", "CLIP", "LIMIT"};
const ParamInfo kParams[kCount] = {
    {"attack", "Attack", "%", -100.0f, 100.0f, 0.0f},
    {"attacksens", "Attack Sensitivity", "%", 0.0f, 100.0f, 50.0f},
    {"attacktime", "Attack Length", "ms", 0.5f, 500.0f, 20.0f, Taper::Log},
    {"attackshape", "Attack Shape", "", 0, 2, 1, Taper::Choice, kAttShapeN},
    {"sustain", "Sustain", "%", -100.0f, 100.0f, 0.0f},
    {"sustainsens", "Sustain Sensitivity", "%", 0.0f, 100.0f, 50.0f},
    {"sustaintime", "Sustain Length", "ms", 30.0f, 1000.0f, 200.0f, Taper::Log},
    {"sustainshape", "Sustain Shape", "", 0, 2, 0, Taper::Choice, kSusShapeN},
    {"guard", "Guard", "", 0, 2, 0, Taper::Choice, kGuardN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -24.0f, 24.0f, 0.0f},
};

constexpr float kCeil = 0.9886f;   // -0.1 dBFS

// One band's shaper state.
struct Shaper {
    float eF = 0, eS = 0, sF = 0, sS = 0, g = 0;
    void clear() noexcept { *this = {}; }
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        if (i == AttackSens || i == AttackTime || i == AttackShape) return p(Attack) != 0.0f;
        if (i == SustainSens || i == SustainTime || i == SustainShape) return p(Sustain) != 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        wide_.clear();
        lim_ = 1.0f;
        limEnv_ = 0.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float atk = p(Attack) * 0.01f, sus = p(Sustain) * 0.01f;
        const float aThr = 12.0f * (1.0f - p(AttackSens) * 0.01f), sThr = 12.0f * (1.0f - p(SustainSens) * 0.01f);
        const float aT = p(AttackTime), sT = p(SustainTime);
        const int aShape = std::clamp(static_cast<int>(std::lround(p(AttackShape))), 0, 2);
        const int sShape = std::clamp(static_cast<int>(std::lround(p(SustainShape))), 0, 2);
        // envelopes: a fast and a lagging one for the hits, a short and a long one for the tails
        const float fA = coefMs(fs_, 0.05), fR = coefMs(fs_, aT), sA = coefMs(fs_, aT), sR = coefMs(fs_, aT);
        const float tA = coefMs(fs_, 1.0), tShort = coefMs(fs_, 20.0), tLong = coefMs(fs_, sT);
        // shapes: how the resulting gain moves (SHARP = as detected; PUNCHY / ROUND = smoothed)
        const float gAtt = coefMs(fs_, aShape == 2 ? 2.0 : 0.02), gRel = coefMs(fs_, aShape == 0 ? 0.02 : aShape == 1 ? aT * 0.3 : aT * 0.6);
        const int guard = std::clamp(static_cast<int>(std::lround(p(Guard))), 0, 2);
        const float limRel = coefMs(fs_, 60.0);
        float worst = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gW = hc::fx::dbToGain(shape(wide_, std::max(std::fabs(L[i]), std::fabs(R[i])), atk, sus, aThr, sThr,
                                                    fA, fR, sA, sR, tA, tShort, tLong, gAtt, gRel, sShape));
            worst = std::min(worst, dbOf(gW));
            const float dl = L[i], dr = R[i], pl = L[i] * gW, pr = R[i] * gW;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            float yl = (dl + m * (pl - dl)) * o, yr = (dr + m * (pr - dr)) * o;
            if (guard == 1) { yl = clipSoft(yl); yr = clipSoft(yr); }
            else if (guard == 2) {   // a fast limiter at -0.1 dBFS, then a final clamp
                const float y = std::max(std::fabs(yl), std::fabs(yr));
                limEnv_ = y > limEnv_ ? y : y + limRel * (limEnv_ - y);
                const float need = limEnv_ > kCeil ? kCeil / limEnv_ : 1.0f;
                lim_ = need < lim_ ? need : need + limRel * (lim_ - need);
                yl = std::clamp(yl * lim_, -kCeil, kCeil);
                yr = std::clamp(yr * lim_, -kCeil, kCeil);
            }
            L[i] = yl;
            R[i] = yr;
        }
        publishReduction(worst);
    }

private:
    // Gain change (dB) for one detector: + on hits / tails when ATTACK / SUSTAIN are +, - when -.
    static float shape(Shaper& s, float x, float atk, float sus, float aThr, float sThr, float fA, float fR, float sA, float sR,
                       float tA, float tShort, float tLong, float gAtt, float gRel, int sShape) noexcept {
        s.eF = x + (x > s.eF ? fA : fR) * (s.eF - x);
        s.eS = x + (x > s.eS ? sA : sR) * (s.eS - x);
        s.sF = x + (x > s.sF ? tA : tShort) * (s.sF - x);
        s.sS = x + (x > s.sS ? tA : tLong) * (s.sS - x);
        const float rise = std::clamp(dbOf(s.eF) - dbOf(s.eS) - aThr, 0.0f, 18.0f);   // how much a hit stands out
        float tail = std::clamp(dbOf(s.sS) - dbOf(s.sF) - sThr, 0.0f, 18.0f);          // how far into a decay
        if (sShape == 1) tail = std::sqrt(tail * 18.0f);
        float target = atk * rise + sus * tail;
        if (s.eF < 1e-6f) target = 0.0f;   // silence: no gain moves
        target = std::clamp(target, -24.0f, 18.0f);
        const bool away = std::fabs(target) > std::fabs(s.g);
        s.g = target + (away ? gAtt : (sShape == 2 ? std::max(gRel, 0.999f) : gRel)) * (s.g - target);
        return s.g;
    }
    static float clipSoft(float y) noexcept {
        constexpr float k = 0.9f * kCeil;
        const float a = std::fabs(y);
        if (a <= k) return y;
        const float t = (a - k) / (kCeil - k);
        return std::copysign(k + (kCeil - k) * t / (1.0f + t), y);
    }

    double fs_ = 48000.0;
    Shaper wide_;
    float lim_ = 1.0f, limEnv_ = 0.0f;
    hc::fx::Smoothed mix_, out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeKnockout() { return std::make_unique<Impl>(); }

} // namespace hem::rack
