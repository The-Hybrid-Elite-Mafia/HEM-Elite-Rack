// =============================================================================
//  52 VCA COMPRESSOR — the clean, accurate everyday compressor: smooth THRESHOLD, RATIO (1:1 .. 20:1),
//  ATTACK (0.1 .. 30 ms), RELEASE (10 .. 500 ms) and KNEE (HARD .. SOFT, 0 .. 12 dB wide) with no steps,
//  MAKEUP, MIX. MODE: CLEAN (peak-sensing, exact), PUNCH (lets the first moment of each hit through,
//  then recovers quicker), VINTAGE (averaging detector and a faint odd-harmonic edge under heavy
//  squeeze). SIDECHAIN: INT, HPF (detector ignores the lows below 80 Hz), EXT (an external key).
//  Different from BUSFOUNDRY (21) and MIXBOND (32): no stepped controls, no bus tricks.
//  Dead flat on load: THRESHOLD 0 dB, RATIO 1:1, MAKEUP 0 dB, MIX 0 %.
//  ASSUMED: PUNCH = attack x2 and release x0.7; VINTAGE detector averages over 20 ms; 0 VU = -18 dBFS.
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kThr, kRatio, kAtt, kRel, kKnee, kMakeup, kMix, kMode, kSc, kBypass, kCount };
const char* const kModeN[] = {"CLEAN", "PUNCH", "VINTAGE"};
const char* const kScN[] = {"INT", "HPF", "EXT"};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -60.0f, 0.0f, 0.0f},
    {"ratio", "RATIO", ":1", 1.0f, 20.0f, 1.0f, Taper::Log},
    {"attack", "ATTACK", "ms", 0.1f, 30.0f, 10.0f, Taper::Log},
    {"release", "RELEASE", "ms", 10.0f, 500.0f, 120.0f, Taper::Log},
    {"knee", "KNEE", "", 0.0f, 100.0f, 50.0f},
    {"makeup", "MAKEUP", "dB", -12.0f, 24.0f, 0.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"mode", "MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"sidechain", "SIDECHAIN", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kScN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 6}, {"OUT", "dB", -60, 6}, {"VU INPUT", "VU", -20, 3, true}, {"VU OUTPUT", "VU", -20, 3, true}, {"GAIN REDUCTION", "dB", -20, 0}};

class VcaComp final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    VcaComp() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        hp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs, 80.0, 0.7071));
        mix_.prepare(fs, 20.0, 0.0f); mk_.prepare(fs, 20.0, 1.0f); thr_.prepare(fs, 30.0, 0.0f); rat_.prepare(fs, 30.0, 1.0f); knee_.prepare(fs, 30.0, 6.0f);
        reset();
    }
    void reset() noexcept override { hp_.clear(); rms_ = 0; gr_ = 0; grM_ = 0; inPk_ = outPk_ = vuI_ = vuO_ = 0; loaded_ = false; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        const float mixT = p(kMix) * 0.01f, mkT = kit::dbToGain(p(kMakeup)), kneeT = p(kKnee) * 0.12f;
        if (!loaded_) { mix_.cur = mixT; mk_.cur = mkT; thr_.cur = p(kThr); rat_.cur = p(kRatio); knee_.cur = kneeT; loaded_ = true; }
        const int mode = static_cast<int>(p(kMode)), sc = static_cast<int>(p(kSc));
        const float att = hc::fx::onePoleCoef(fs_, p(kAtt) * (mode == 1 ? 2.0f : 1.0f)), rel = hc::fx::onePoleCoef(fs_, p(kRel) * (mode == 1 ? 0.7f : 1.0f));
        const float rmsC = hc::fx::onePoleCoef(fs_, 20.0), vuC = hc::fx::onePoleCoef(fs_, 300.0);
        const bool ext = sc == 2 && scL_ != nullptr;
        grM_ = 0;
        float inPk = 0, outPk = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            float dl = ext ? scL_[i] : xl, dr = ext ? scR_[i] : xr;
            const float hl = hp_.l.tick(dl), hr = hp_.r.tick(dr);
            if (sc == 1) { dl = hl; dr = hr; }
            float lvl;
            if (mode == 2) { const float e = 0.5f * (dl * dl + dr * dr); rms_ = e + rmsC * (rms_ - e); lvl = 10.0f * std::log10(std::max(2.0f * rms_, 1e-12f)); }
            else lvl = kit::gainToDb(std::max(std::fabs(dl), std::fabs(dr)));
            const float t = thr_.next(p(kThr)), r = rat_.next(p(kRatio)), k = knee_.next(kneeT);
            const float over = lvl - t;
            float want;
            if (k > 0.01f && std::fabs(over) < k * 0.5f) { const float x = over + k * 0.5f; want = (1.0f - 1.0f / r) * x * x / (2.0f * k); }
            else want = over > 0 ? over * (1.0f - 1.0f / r) : 0.0f;
            gr_ = want + (want > gr_ ? att : rel) * (gr_ - want);   // smoothing in the gain domain (no detector ripple)
            grM_ = std::max(grM_, gr_);
            const float g = kit::dbToGain(-gr_) * mk_.next(mkT);
            float cl = xl * g, cr = xr * g;
            if (mode == 2 && gr_ > 6.0f) { const float d = 1.0f + 0.01f * (gr_ - 6.0f); cl = std::tanh(cl * d) / d; cr = std::tanh(cr * d) / d; }
            const float mx = mix_.next(mixT);
            const float yl = xl + mx * (cl - xl), yr = xr + mx * (cr - xr);
            inPk = std::max(inPk, std::max(std::fabs(xl), std::fabs(xr))); outPk = std::max(outPk, std::max(std::fabs(yl), std::fabs(yr)));
            const float ai = 0.5f * (std::fabs(xl) + std::fabs(xr)), ao = 0.5f * (std::fabs(yl) + std::fabs(yr));
            vuI_ = ai + vuC * (vuI_ - ai); vuO_ = ao + vuC * (vuO_ - ao);
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        inPk_ = inPk; outPk_ = outPk; scL_ = scR_ = nullptr;
        publishMeter(0, kit::gainToDb(inPk_)); publishMeter(1, kit::gainToDb(outPk_));
        publishMeter(2, std::clamp(kit::gainToDb(vuI_ * 1.11f) + 18.0f, -20.0f, 3.0f)); publishMeter(3, std::clamp(kit::gainToDb(vuO_ * 1.11f) + 18.0f, -20.0f, 3.0f));
        const float gr = mixT > 0.0f ? -grM_ : 0.0f;
        publishMeter(4, gr); publishReduction(gr);
    }

private:
    double fs_ = 48000;
    hc::fx::Biquad2 hp_;
    hc::fx::Smoothed mix_, mk_, thr_, rat_, knee_;
    float rms_ = 0, gr_ = 0, grM_ = 0, inPk_ = 0, outPk_ = 0, vuI_ = 0, vuO_ = 0;
    bool loaded_ = false;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeVcaComp() { return std::make_unique<VcaComp>(); }

} // namespace hem::staging
