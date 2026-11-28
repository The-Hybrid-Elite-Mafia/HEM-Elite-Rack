// =============================================================================
//  13 VOICEPRESS — vocal dynamics on three controls: GATE (downward expander for the noise
//  between lines), COMPRESSION (one fader: lowers the threshold and makes up half of what it
//  takes), OUTPUT CEILING (a peak limiter that never lets a sample past it).
//  Different from LEVELPILOT (09, a slow rider): this grabs peaks fast.
//  SIGNAL PATH: COMPRESS (3:1, 5 ms / 80 ms, soft knee) or LIMIT (10:1, 0.5 ms / 50 ms).
//  COMP + CEILING LINK: the ceiling follows the compression down, so a heavier setting does
//  not simply sound louder (a fair level comparison).
//  Dead flat on load: GATE off (-80 dB), COMPRESSION 0 dB, CEILING 0 dBFS (only overs are touched).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kGate, kComp, kCeiling, kLink, kPath, kResetIn, kResetOut, kBypass, kCount };
const char* const kPathN[] = {"COMPRESS", "LIMIT"};
const ParamInfo kParams[kCount] = {
    {"gate", "GATE", "dB", -80.0f, 0.0f, -80.0f},
    {"compression", "COMPRESSION", "dB", 0.0f, 40.0f, 0.0f},
    {"ceiling", "OUTPUT CEILING", "dBFS", -20.0f, 0.0f, 0.0f},
    {"link", "COMP + CEILING LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"signal_path", "SIGNAL PATH", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kPathN},
    {"reset_input", "RESET INPUT PEAK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"reset_output", "RESET OUTPUT PEAK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT LEVEL", "dBFS", -60, 0}, {"GATE REDUCTION", "dB", -40, 0}, {"COMP REDUCTION", "dB", -40, 0},
    {"TOTAL REDUCTION", "dB", -40, 0}, {"OUTPUT PEAK L", "dBFS", -60, 0}, {"OUTPUT PEAK R", "dBFS", -60, 0}, {"INPUT PEAK", "dBFS", -60, 0},
};

class VoicePress final : public hc::fx::Effect {
public:
    VoicePress() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; inM_.prepare(fs); reset(); }
    void reset() noexcept override { compS_ = p(kComp); gEnv_ = cEnv_ = 0; gateG_ = 1; compDb_ = 0; limG_ = 1; inPk_ = oPkL_ = oPkR_ = 0; gDb_ = cDb_ = 0; mk_ = 0; ceilS_ = 1; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inM_.feed(L, n);
        if (p(kResetIn) > 0.5f) inPk_ = 0;
        if (p(kResetOut) > 0.5f) oPkL_ = oPkR_ = 0;
        if (p(kBypass) > 0.5f) { meters(L, R, n); return; }
        const bool limit = p(kPath) > 0.5f;
        const float ratio = limit ? 10.0f : 3.0f;
        const float ca = hc::fx::onePoleCoef(fs_, limit ? 0.5 : 5.0), cr = hc::fx::onePoleCoef(fs_, limit ? 50.0 : 80.0);
        const float ga = hc::fx::onePoleCoef(fs_, 1.0), gr = hc::fx::onePoleCoef(fs_, 100.0), gs = hc::fx::onePoleCoef(fs_, 5.0);
        const float lr = hc::fx::onePoleCoef(fs_, 60.0), smooth = hc::fx::onePoleCoef(fs_, 20.0);
        const float compT = p(kComp);                                    // COMPRESSION lowers the threshold from 0 dBFS (glided)
        const float ceilDbT = p(kCeiling) - (p(kLink) > 0.5f ? 0.25f * compT : 0.0f);
        const bool gateOn = p(kGate) > -79.5f;
        const float gThr = p(kGate);
        float gWorst = 0, cWorst = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            compS_ = compT + smooth * (compS_ - compT);
            const float comp = compS_, thr = -comp;
            const float makeupT = comp * (1.0f - 1.0f / ratio) * 0.5f;   // half of the full-scale reduction made up
            const float ceilT = kit::dbToGain(ceilDbT);
            // gate: below its threshold, down 2 dB per dB (up to 40 dB)
            float gTarget = 1.0f;
            if (gateOn) {
                gEnv_ = x + (x > gEnv_ ? ga : gr) * (gEnv_ - x);
                const float lvl = kit::gainToDb(gEnv_);
                if (lvl < gThr) gTarget = kit::dbToGain(std::max(-40.0f, -2.0f * (gThr - lvl)));
            }
            gateG_ = gTarget + gs * (gateG_ - gTarget);
            // compressor, soft knee 6 dB, on the gated signal's level
            const float xs = x * gateG_;
            cEnv_ = xs + (xs > cEnv_ ? ca : cr) * (cEnv_ - xs);
            const float lvl = kit::gainToDb(cEnv_);
            const float over = lvl - thr;
            float red = 0.0f;
            if (comp > 0.0f) {
                if (over > 3.0f) red = -(over) * (1.0f - 1.0f / ratio);
                else if (over > -3.0f) red = -(1.0f - 1.0f / ratio) * (over + 3.0f) * (over + 3.0f) / 12.0f;
            }
            mk_ = makeupT + smooth * (mk_ - makeupT);
            float g = gateG_ * kit::dbToGain(red + mk_);
            float l = L[i] * g, r = R[i] * g;
            // ceiling: instant gain down on any sample past it, 60 ms recovery
            ceilS_ = ceilT + smooth * (ceilS_ - ceilT);
            const float pk = std::max(std::fabs(l), std::fabs(r));
            const float need = pk > ceilS_ ? ceilS_ / pk : 1.0f;
            limG_ = std::min(need, 1.0f + lr * (limG_ - 1.0f));
            l *= limG_; r *= limG_;
            L[i] = kit::sane(l); R[i] = kit::sane(r);
            gWorst = std::min(gWorst, kit::gainToDb(gateG_));
            cWorst = std::min(cWorst, red + kit::gainToDb(limG_));
        }
        gDb_ = gWorst; cDb_ = cWorst;
        meters(L, R, n);
    }

private:
    void meters(const float* L, const float* R, std::uint32_t n) noexcept {
        for (std::uint32_t i = 0; i < n; ++i) { oPkL_ = std::max(oPkL_, std::fabs(L[i])); oPkR_ = std::max(oPkR_, std::fabs(R[i])); }
        inPk_ = std::max(inPk_, inM_.env);
        publishMeter(0, inM_.db()); publishMeter(1, gDb_); publishMeter(2, cDb_); publishMeter(3, gDb_ + cDb_);
        publishMeter(4, kit::gainToDb(oPkL_)); publishMeter(5, kit::gainToDb(oPkR_)); publishMeter(6, kit::gainToDb(inPk_));
        publishReduction(gDb_ + cDb_);
    }
    double fs_ = 48000;
    float gEnv_ = 0, cEnv_ = 0, gateG_ = 1, compDb_ = 0, compS_ = 0, limG_ = 1, inPk_ = 0, oPkL_ = 0, oPkR_ = 0, gDb_ = 0, cDb_ = 0, mk_ = 0, ceilS_ = 1;
    kit::PeakMeter inM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeVoicePress() { return std::make_unique<VoicePress>(); }

} // namespace hem::staging
