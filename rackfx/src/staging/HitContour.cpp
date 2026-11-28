// =============================================================================
//  37 HITCONTOUR — detailed transient shaping for drums. Each hit is spotted when its quick level
//  jumps over its slow level (SENSITIVITY: 0 = only big hits, 100 = small ones too).
//    ATTACK   AMOUNT (-100 .. +100: cut or boost the front of the hit), SENSITIVITY, SHAPE (NEEDLE =
//             a sharp spike fading fast, NAIL = held flat then cut off, BLUNT = rounded), DURATION
//             (0.5 .. 500 ms, how long the front lasts)
//    SUSTAIN  AMOUNT (-100 .. +100: shorten or lengthen the body), SENSITIVITY, SHAPE (LINEAR = even
//             over the duration, NONLINEAR = growing toward the end, SOFT LINEAR = rounded), DURATION
//             (30 .. 1000 ms after the attack)
//  MIX, OUTPUT, GUARD (OFF / CLIP = soft clip at 0 dBFS / LIMIT = zero-latency limiter at 0 dBFS).
//  Different from TRANSIENT SHAPER (50: two knobs) and IMPACTCROWN (35: a whole loudness chain).
//  Dead flat on load: AMOUNTS 0, MIX 0 %, OUTPUT 0 dB, GUARD OFF.
//  ASSUMED: AMOUNT +-100 = +-12 dB; hit = quick level over slow level by 18 dB at SENSITIVITY 0
//  down to 3 dB at 100; quick level 0.5 / 20 ms, slow level 20 / 200 ms; CLIP knee from -1 dBFS.
//  The waveform / gain-change graph needs the Phase 2 UI; the gain change is published as a meter.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kAAmt, kASens, kAShape, kADur, kSAmt, kSSens, kSShape, kSDur, kMix, kOut, kGuard, kBypass, kCount };
const char* const kAShapeN[] = {"NEEDLE", "NAIL", "BLUNT"};
const char* const kSShapeN[] = {"LINEAR", "NONLINEAR", "SOFT LINEAR"};
const char* const kGuardN[] = {"OFF", "CLIP", "LIMIT"};
const ParamInfo kParams[kCount] = {
    {"attack_amount", "ATTACK AMOUNT", "", -100.0f, 100.0f, 0.0f},
    {"attack_sensitivity", "ATTACK SENSITIVITY", "", 0.0f, 100.0f, 0.0f},
    {"attack_shape", "ATTACK SHAPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kAShapeN},
    {"attack_duration", "ATTACK DURATION", "ms", 0.5f, 500.0f, 20.0f, Taper::Log},
    {"sustain_amount", "SUSTAIN AMOUNT", "", -100.0f, 100.0f, 0.0f},
    {"sustain_sensitivity", "SUSTAIN SENSITIVITY", "", 0.0f, 100.0f, 0.0f},
    {"sustain_shape", "SUSTAIN SHAPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kSShapeN},
    {"sustain_duration", "SUSTAIN DURATION", "ms", 30.0f, 1000.0f, 200.0f, Taper::Log},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"guard", "GUARD", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kGuardN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"GAIN CHANGE", "dB", -24, 24}, {"GUARD REDUCTION", "dB", -24, 0}, {"OUTPUT", "dBFS", -60, 0}};

class HitContour final : public hc::fx::Effect {
public:
    HitContour() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        mix_.prepare(fs, 20.0, 0.0f); out_.prepare(fs, 20.0, 1.0f); aAmt_.prepare(fs, 20.0, 0.0f); sAmt_.prepare(fs, 20.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        fast_ = slow_ = 0; tA_ = tS_ = 1e9f; armA_ = armS_ = true; gDb_ = 0; limG_ = 1; gcM_ = 0; grM_ = 0; loaded_ = false; outM_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float mixT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOut));
        const float aT = p(kAAmt) * 0.12f, sT = p(kSAmt) * 0.12f;   // +-100 -> +-12 dB
        if (!loaded_) { mix_.cur = mixT; out_.cur = oT; aAmt_.cur = aT; sAmt_.cur = sT; loaded_ = true; }
        const float fs = static_cast<float>(fs_);
        const float fA = hc::fx::onePoleCoef(fs_, 0.5), fR = hc::fx::onePoleCoef(fs_, 20.0), sA = hc::fx::onePoleCoef(fs_, 20.0), sR = hc::fx::onePoleCoef(fs_, 200.0);
        const float gS = hc::fx::onePoleCoef(fs_, 0.3);
        const float thrA = 18.0f - 0.15f * p(kASens), thrS = 18.0f - 0.15f * p(kSSens);
        const int aShape = static_cast<int>(p(kAShape)), sShape = static_cast<int>(p(kSShape));
        const float dA = p(kADur) * 0.001f * fs, dS = p(kSDur) * 0.001f * fs;
        const int guard = static_cast<int>(p(kGuard));
        const float limRel = hc::fx::onePoleCoef(fs_, 50.0);
        gcM_ = 0; grM_ = 0;
        float outPk = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            const float lvl = std::max(std::fabs(xl), std::fabs(xr));
            fast_ = lvl + (lvl > fast_ ? fA : fR) * (fast_ - lvl);
            slow_ = lvl + (lvl > slow_ ? sA : sR) * (slow_ - lvl);
            const float rise = kit::gainToDb(fast_ + 1e-9f) - kit::gainToDb(slow_ + 1e-9f);
            if (fast_ > 1e-4f) {
                if (armA_ && rise > thrA) { tA_ = 0; armA_ = false; }
                if (armS_ && rise > thrS) { tS_ = 0; armS_ = false; }
            }
            if (rise < thrA - 3.0f) armA_ = true;
            if (rise < thrS - 3.0f) armS_ = true;
            // ---- attack curve ----
            float ca = 0;
            if (tA_ < dA) {
                const float u = tA_ / dA;
                if (aShape == 0) ca = std::exp(-5.0f * u);                              // NEEDLE
                else if (aShape == 1) ca = u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) / 0.3f;   // NAIL
                else ca = std::sin(kit::kPi * u);                                      // BLUNT
            }
            // ---- sustain curve: starts where the attack ends ----
            float cs = 0;
            const float ts = tS_ - dA;
            if (ts >= 0 && ts < dS) {
                const float u = ts / dS, edge = std::min({1.0f, ts / (0.005f * fs + 1.0f), (dS - ts) / (0.02f * fs + 1.0f)});
                if (sShape == 0) cs = edge;                         // LINEAR
                else if (sShape == 1) cs = std::sqrt(u) * edge;     // NONLINEAR
                else cs = std::sin(kit::kPi * u);                    // SOFT LINEAR
            }
            tA_ += 1.0f; tS_ += 1.0f;
            const float want = std::clamp(aAmt_.next(aT) * ca + sAmt_.next(sT) * cs, -24.0f, 24.0f);
            gDb_ = want + gS * (gDb_ - want);
            const float g = kit::dbToGain(gDb_);
            const float mx = mix_.next(mixT), o = out_.next(oT);
            float yl = (xl + mx * (xl * g - xl)) * o, yr = (xr + mx * (xr * g - xr)) * o;
            if (guard == 1) {
                auto clip = [this](float v) {
                    const float a = std::fabs(v), k = 0.891f;   // knee from -1 dBFS
                    if (a <= k) return v;
                    const float y = k + (1.0f - k) * std::tanh((a - k) / (1.0f - k));
                    grM_ = std::max(grM_, kit::gainToDb(a) - kit::gainToDb(y));
                    return std::copysign(y, v);
                };
                yl = clip(yl); yr = clip(yr);
            } else if (guard == 2) {
                const float pk = std::max(std::fabs(yl), std::fabs(yr));
                const float need = pk > 1.0f ? 1.0f / pk : 1.0f;
                limG_ = std::min(need, 1.0f + limRel * (limG_ - 1.0f));
                yl *= limG_; yr *= limG_;
                grM_ = std::max(grM_, -kit::gainToDb(limG_));
            }
            if (std::fabs(gDb_ * mx) > std::fabs(gcM_)) gcM_ = gDb_ * mx;
            outPk = std::max(outPk, std::max(std::fabs(yl), std::fabs(yr)));
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        outM_ = outPk;
        publishMeter(0, gcM_); publishMeter(1, -grM_); publishMeter(2, kit::gainToDb(outM_));
        publishReduction(-grM_);
    }

private:
    double fs_ = 48000;
    hc::fx::Smoothed mix_, out_, aAmt_, sAmt_;
    float fast_ = 0, slow_ = 0, tA_ = 1e9f, tS_ = 1e9f, gDb_ = 0, limG_ = 1, gcM_ = 0, grM_ = 0, outM_ = 0;
    bool armA_ = true, armS_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeHitContour() { return std::make_unique<HitContour>(); }

} // namespace hem::staging
