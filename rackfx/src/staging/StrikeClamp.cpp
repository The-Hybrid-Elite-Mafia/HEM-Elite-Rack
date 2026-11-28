// =============================================================================
//  20 STRIKECLAMP — fast character compression (FET style): INPUT drives the signal into a fixed
//  threshold, ATTACK 1..7 and RELEASE 1..7 where 7 is the fastest (about 800 us -> 20 us attack,
//  1.1 s -> 50 ms release), RATIO 4 / 8 / 12 / 20 / MAX (all buttons: a near-limiting ratio with a
//  harder knee and extra grit). MODEL A is the cleaner circuit, MODEL B the grittier one (more
//  harmonics as it works). Different from BUSFOUNDRY (21, stepped VCA bus) and VCA COMPRESSOR (52).
//  Dead flat on load (verified spec): COMPRESSION OFF, INPUT / OUTPUT 0 dB, MIX 0 %, trim 0 dB, noise OFF.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kInput, kOutput, kAttack, kRatio, kRelease, kModel, kComp, kAutoMakeup, kMix, kTrim, kNoise, kMeterSel, kClipReset, kBypass, kCount };
const char* const kRatioN[] = {"4", "8", "12", "20", "MAX"};
const char* const kModelN[] = {"MODE A", "MODE B"};
const char* const kNoiseN[] = {"OFF", "50 HZ", "60 HZ"};
const char* const kMeterN[] = {"IN", "GR", "OUT"};
const ParamInfo kParams[kCount] = {
    {"input", "INPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"attack", "ATTACK", "", 1.0f, 7.0f, 1.0f},
    {"ratio", "RATIO", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kRatioN},
    {"release", "RELEASE", "", 1.0f, 7.0f, 1.0f},
    {"model", "MODEL", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModelN},
    {"compression", "COMPRESSION", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"auto_makeup", "AUTO MAKEUP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"trim", "POST-MIX TRIM", "dB", -18.0f, 18.0f, 0.0f},
    {"analog_noise", "ANALOG NOISE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kNoiseN},
    {"meter", "METER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kMeterN},
    {"clip_reset", "CLIP / OVER RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"VU", "dB", -20, 6, true}, {"GAIN REDUCTION", "dB", -30, 0}, {"CLIP / OVER", "", 0, 1}, {"OUTPUT", "dBFS", -60, 0}};

constexpr float kThr = -20.0f;   // the fixed threshold the INPUT drives into (ASSUMED, dBFS)

class StrikeClamp final : public hc::fx::Effect {
public:
    StrikeClamp() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override { return (i != kAttack && i != kRatio && i != kRelease && i != kModel && i != kAutoMakeup) || p(kComp) > 0.5f; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto* s : {&in_, &out_, &trim_}) s->prepare(fs, 20.0, 1.0f);
        mix_.prepare(fs, 20.0, 0.0f); compMix_.prepare(fs, 20.0, 0.0f);
        outM_.prepare(fs); reset();
    }
    void reset() noexcept override { env_ = 0; grDb_ = 0; vuIn_ = vuOut_ = 0; clip_ = false; humPh_ = 0; rng_ = 4242u; dc_[0] = dc_[1] = 0; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kClipReset) > 0.5f) clip_ = false;
        if (p(kBypass) > 0.5f) { publishAll(); return; }
        // knob 1..7 -> times (log spaced between the ends)
        const float a = (p(kAttack) - 1.0f) / 6.0f, r = (p(kRelease) - 1.0f) / 6.0f;
        const float attMs = 0.8f * std::pow(0.02f / 0.8f, a), relMs = 1100.0f * std::pow(50.0f / 1100.0f, r);
        const float ca = hc::fx::onePoleCoef(fs_, attMs), cr = hc::fx::onePoleCoef(fs_, relMs);
        static constexpr std::array<float, 5> kRatioV{4.0f, 8.0f, 12.0f, 20.0f, 40.0f};
        const int ri = static_cast<int>(p(kRatio));
        const float ratio = kRatioV[static_cast<std::size_t>(ri)];
        const float knee = ri == 4 ? 0.0f : 4.0f;                 // MAX: no knee
        const bool modelB = p(kModel) > 0.5f;
        const float mixT = p(kMix) * 0.01f, cT = p(kComp) > 0.5f ? 1.0f : 0.0f;
        const float inT = kit::dbToGain(p(kInput)), outT = kit::dbToGain(p(kOutput)), trimT = kit::dbToGain(p(kTrim));
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        float worst = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = L[i], dr = R[i];
            vuIn_ = 0.5f * (std::fabs(dl) + std::fabs(dr)) + 0.9995f * (vuIn_ - 0.5f * (std::fabs(dl) + std::fabs(dr)));
            float yl = dl, yr = dr;
            if (wet) {
                const float ig = in_.next(inT), og = out_.next(outT), cm = compMix_.next(cT);
                float xl = dl * ig, xr = dr * ig;
                float gDb = 0.0f;
                if (cm > 1e-6f) {
                    const float x = std::max(std::fabs(xl), std::fabs(xr));
                    env_ = x + (x > env_ ? ca : cr) * (env_ - x);
                    const float over = kit::gainToDb(env_) - kThr;
                    if (over > knee * 0.5f) gDb = -(over) * (1.0f - 1.0f / ratio);
                    else if (over > -knee * 0.5f && knee > 0.0f) gDb = -(1.0f - 1.0f / ratio) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                    gDb *= cm;
                    if (p(kAutoMakeup) > 0.5f) gDb += cm * std::max(0.0f, p(kInput) * (1.0f - 1.0f / ratio) * 0.5f);
                }
                worst = std::min(worst, gDb);
                const float g = kit::dbToGain(gDb);
                xl *= g; xr *= g;
                // FET character: harmonics that grow with gain reduction (B: more, and odd + even; MAX: extra)
                const float work = std::min(1.0f, -std::min(gDb, 0.0f) / 20.0f) * cm;
                const float k2 = (modelB ? 0.08f : 0.02f) * work * (ri == 4 ? 2.0f : 1.0f), k3 = (modelB ? 0.05f : 0.015f) * work;
                xl = xl + k2 * xl * xl - k3 * xl * xl * xl;   // x^2: even (2nd) harmonic; x^3: odd (3rd)
                xr = xr + k2 * xr * xr - k3 * xr * xr * xr;
                dc_[0] += 0.0005f * (xl - dc_[0]); dc_[1] += 0.0005f * (xr - dc_[1]);
                if (k2 > 0.0f) { xl -= dc_[0]; xr -= dc_[1]; }
                xl *= og; xr *= og;
                const float mx = mix_.next(mixT);
                yl = dl + mx * (xl - dl); yr = dr + mx * (xr - dr);
            }
            const float t = trim_.next(trimT);
            yl *= t; yr *= t;
            if (static_cast<int>(p(kNoise)) > 0) {
                const float hz = static_cast<int>(p(kNoise)) == 1 ? 50.0f : 60.0f;
                humPh_ += static_cast<double>(hz) / fs_; humPh_ -= std::floor(humPh_);
                rng_ = rng_ * 1664525u + 1013904223u;
                const float hiss = (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f) * 6e-5f;
                const float hum = 1e-4f * std::sin(kit::kTau * static_cast<float>(humPh_));
                yl += hum + hiss; yr += hum - hiss;
            }
            if (std::fabs(yl) >= 1.0f || std::fabs(yr) >= 1.0f) clip_ = true;
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
            vuOut_ = 0.5f * (std::fabs(yl) + std::fabs(yr)) + 0.9995f * (vuOut_ - 0.5f * (std::fabs(yl) + std::fabs(yr)));
        }
        grDb_ = worst;
        outM_.feed(L, n);
        publishAll();
    }

private:
    void publishAll() noexcept {
        const int sel = static_cast<int>(p(kMeterSel));
        const float vu = sel == 1 ? grDb_ : kit::gainToDb((sel == 0 ? vuIn_ : vuOut_) * 1.11f) + 18.0f;
        publishMeter(0, std::clamp(vu, -20.0f, 6.0f)); publishMeter(1, grDb_); publishMeter(2, clip_ ? 1.0f : 0.0f); publishMeter(3, outM_.db());
        publishReduction(grDb_);
    }
    double fs_ = 48000;
    hc::fx::Smoothed in_, out_, trim_, mix_, compMix_;
    float env_ = 0, grDb_ = 0, vuIn_ = 0, vuOut_ = 0;
    std::array<float, 2> dc_{};
    bool clip_ = false;
    double humPh_ = 0;
    std::uint32_t rng_ = 4242u;
    kit::PeakMeter outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeStrikeClamp() { return std::make_unique<StrikeClamp>(); }

} // namespace hem::staging
