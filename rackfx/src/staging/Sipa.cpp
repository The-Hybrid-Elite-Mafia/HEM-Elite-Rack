// =============================================================================
//  10 S.I.P.A. — Stereo Image & Phase Architect: widen, narrow, rotate phase, fix polarity,
//  make the lows mono, add air / harmonics / depth to the sides, check mono.
//  The only stereo-only tool: it never changes the middle's tone (only the sides' filters,
//  air and harmonics act, and only on the sides).
//  Dead flat on load: width 100 %, preset NORMAL, every amount 0, phase 0°, no inverts / swap,
//  mono bass off, side filters off, balance 0, trim 0 dB, mode NORMAL.
//  PAN LAW on the panel works as a balance in dB (left / right level), labelled as drawn.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P {
    kWidth, kPreset, kCenterFocus, kDepth, kPanLaw, kRotate, kInvL, kInvR, kSwap, kMode,
    kMonoOn, kMonoHz, kMsBal, kHarmWidth, kAir, kSideFilt, kSideHp, kSideLp, kDynWidth, kTrim, kBypass, kCount
};
const char* const kPresetN[] = {"NARROW", "NORMAL", "WIDE", "MAX"};
const char* const kModeN[] = {"NORMAL", "MID / SIDE", "SIDE ONLY", "MONO"};
const ParamInfo kParams[kCount] = {
    {"width", "WIDTH", "%", 0.0f, 200.0f, 100.0f},
    {"width_preset", "WIDTH PRESET", "", 0.0f, 3.0f, 1.0f, Taper::Choice, kPresetN},
    {"center_focus", "CENTER FOCUS", "", 0.0f, 100.0f, 0.0f},
    {"depth", "DEPTH", "", 0.0f, 100.0f, 0.0f},
    {"pan_law", "PAN LAW", "dB", -24.0f, 24.0f, 0.0f},
    {"phase_rotate", "PHASE ROTATE", "deg", -180.0f, 180.0f, 0.0f},
    {"invert_l", "INVERT L", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"invert_r", "INVERT R", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"swap", "SWAP L/R", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"stereo_mode", "STEREO MODE", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kModeN},
    {"mono_bass", "MONO BASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"mono_bass_freq", "MONO BASS FREQ", "Hz", 20.0f, 500.0f, 120.0f, Taper::Log},
    {"ms_balance", "MID / SIDE BALANCE", "", -100.0f, 100.0f, 0.0f},
    {"harmonic_width", "HARMONIC WIDTH", "", 0.0f, 100.0f, 0.0f},
    {"air_enhance", "AIR ENHANCE", "", 0.0f, 100.0f, 0.0f},
    {"filter_side", "FILTER SIDE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"hpf_side", "HPF SIDE", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"lpf_side", "LPF SIDE", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"dynamic_width", "DYNAMIC WIDTH", "", 0.0f, 100.0f, 0.0f},
    {"output_trim", "OUTPUT TRIM", "dB", -6.0f, 6.0f, 0.0f},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"CORRELATION", "corr", -1, 1, true}, {"WIDTH", "%", 0, 200}, {"PHASE", "deg", -180, 180, true},
    {"L", "dB", -60, 6}, {"R", "dB", -60, 6}, {"MONO CHECK", "dB", -24, 6, true},
};

class Sipa final : public hc::fx::Effect {
public:
    Sipa() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kMonoHz) return p(kMonoOn) > 0.5f;
        if (i == kSideHp || i == kSideLp) return p(kSideFilt) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        depthL_.prepare(static_cast<std::uint32_t>(fs * 0.03));
        width_.prepare(fs, 20.0, effWidth()); mid_.prepare(fs, 20.0, 1.0f); side_.prepare(fs, 20.0, 1.0f);
        balL_.prepare(fs, 20.0, 1.0f); balR_.prepare(fs, 20.0, 1.0f); trim_.prepare(fs, 20.0, 1.0f);
        cosR_.prepare(fs, 20.0, 1.0f); sinR_.prepare(fs, 20.0, 0.0f); rotMix_.prepare(fs, 20.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        monoLp_.clear(); sideHp_.clear(); sideLp_.clear(); airHs_.clear(); harmHp_.clear(); hil_.clear(); hilRef_.clear();
        depthL_.clear(); keys_.fill(1e9f); dynEnvM_ = dynEnvS_ = 0; corrXY_ = corrXX_ = corrYY_ = 0; monoPk_ = stPk_ = 0;
        lPk_ = rPk_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { meters(L, R, n); return; }
        // ---- polarity, swap, phase rotation (input side) ----------------------------------
        const bool invL = p(kInvL) > 0.5f, invR = p(kInvR) > 0.5f, swap = p(kSwap) > 0.5f;
        if (invL || invR || swap)
            for (std::uint32_t i = 0; i < n; ++i) {
                float l = invL ? -L[i] : L[i], r = invR ? -R[i] : R[i];
                if (swap) std::swap(l, r);
                L[i] = l; R[i] = r;
            }
        // PHASE ROTATE turns the right channel's phase against the left (all frequencies, via a Hilbert pair)
        // The phase filters fade in over 20 ms when it leaves 0° and fade out when it comes back (no click),
        // and are skipped entirely once faded out (dead flat).
        const float rot = p(kRotate) * kit::kPi / 180.0f;
        const float rotMixT = kit::isFlat(rot) ? 0.0f : 1.0f;
        if (rotMixT > 0.0f || rotMix_.cur > 1e-6f) {
            for (std::uint32_t i = 0; i < n; ++i) {
                float re, im, lre, lim;
                hil_.tick(R[i], re, im);
                hilRef_.tick(L[i], lre, lim);                              // the left through the same reference path
                const float c = cosR_.next(std::cos(rot)), s = sinR_.next(std::sin(rot));
                const float k = rotMix_.next(rotMixT);
                R[i] += k * (re * c + im * s - R[i]);
                L[i] += k * (lre - L[i]);
            }
            if (rotMix_.cur < 1e-6f) { rotMix_.cur = 0.0f; hil_.clear(); hilRef_.clear(); }
        }
        // ---- mid / side work ------------------------------------------------------------------
        const float wT = effWidth();
        const float cf = p(kCenterFocus) * 0.01f, bal = p(kMsBal) * 0.01f;
        const float midT = kit::dbToGain(cf * 6.0f) * (bal > 0.0f ? 1.0f - bal * 0.5f : 1.0f);   // CENTER FOCUS up to +6 dB on the middle
        const float sideT = bal < 0.0f ? 1.0f + bal * 0.5f : 1.0f + bal;                        // BALANCE: -100 half sides .. +100 double sides
        const bool monoOn = p(kMonoOn) > 0.5f, sideF = p(kSideFilt) > 0.5f;
        const float air = p(kAir) * 0.01f, harm = p(kHarmWidth) * 0.01f, depth = p(kDepth) * 0.01f, dyn = p(kDynWidth) * 0.01f;
        if (monoOn && setKey(0, p(kMonoHz))) monoLp_.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, p(kMonoHz), 0.7071);
        if (sideF) {
            if (setKey(1, p(kSideHp))) sideHp_.c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kSideHp), 0.7071);
            if (setKey(2, p(kSideLp))) sideLp_.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, p(kSideLp), 0.7071);
        }
        if (air > 0.0f && setKey(3, air)) airHs_.c = hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, 10000.0, 0.7, air * 6.0);
        if (harm > 0.0f && setKey(4, 1.0f)) harmHp_.c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 2000.0, 0.7071);
        const float dc = hc::fx::onePoleCoef(fs_, 150.0);
        const bool flatMs = kit::isFlat(wT, 1.0f) && kit::isFlat(width_.cur, 1.0f) && kit::isFlat(midT, 1.0f) && kit::isFlat(mid_.cur, 1.0f) &&
                            kit::isFlat(sideT, 1.0f) && kit::isFlat(side_.cur, 1.0f) && !monoOn && !sideF && air <= 0.0f && harm <= 0.0f && depth <= 0.0f && dyn <= 0.0f;
        if (!flatMs) {
            for (std::uint32_t i = 0; i < n; ++i) {
                float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
                if (monoOn) s -= static_cast<float>(monoLp_.tick(s));                 // nothing below the freq has width
                if (sideF) s = static_cast<float>(sideLp_.tick(static_cast<float>(sideHp_.tick(s))));
                if (air > 0.0f) s = static_cast<float>(airHs_.tick(s));
                if (harm > 0.0f) {                                                   // HARMONIC WIDTH: soft upper harmonics of the sides
                    const float h = static_cast<float>(harmHp_.tick(s));
                    s += harm * 0.5f * (std::tanh(3.0f * h) - h);
                }
                if (depth > 0.0f) {                                                  // DEPTH: a 17 ms echo of the middle into the sides
                    depthL_.write(m);
                    s += depth * 0.35f * depthL_.read(static_cast<float>(fs_ * 0.017));
                }
                float w = width_.next(wT);
                if (dyn > 0.0f) {                                                    // DYNAMIC WIDTH: reins in sides that outgrow the middle
                    dynEnvM_ = std::fabs(m) + dc * (dynEnvM_ - std::fabs(m));
                    dynEnvS_ = std::fabs(s * w) + dc * (dynEnvS_ - std::fabs(s * w));
                    const float over = dynEnvS_ / std::max(dynEnvM_, 1e-5f);
                    if (over > 1.0f) w /= 1.0f + dyn * (over - 1.0f);
                }
                m *= mid_.next(midT);
                s *= w * side_.next(sideT);
                L[i] = m + s; R[i] = m - s;
            }
        }
        // ---- stereo mode, balance ("PAN LAW") and trim ----------------------------------------
        const int mode = static_cast<int>(p(kMode));
        const float pl = p(kPanLaw);
        const float blT = pl > 0.0f ? kit::dbToGain(-pl) : 1.0f, brT = pl < 0.0f ? kit::dbToGain(pl) : 1.0f;
        const float tT = kit::dbToGain(p(kTrim));
        const bool flatOut = mode == 0 && kit::isFlat(blT, 1.0f) && kit::isFlat(brT, 1.0f) && kit::isFlat(tT, 1.0f) &&
                             kit::isFlat(balL_.cur, 1.0f) && kit::isFlat(balR_.cur, 1.0f) && kit::isFlat(trim_.cur, 1.0f);
        if (!flatOut)
            for (std::uint32_t i = 0; i < n; ++i) {
                float l = L[i], r = R[i];
                const float m = 0.5f * (l + r), s = 0.5f * (l - r);
                switch (mode) {
                case 1: l = m; r = s; break;          // MID / SIDE: mid on the left, side on the right (for checking)
                case 2: l = s; r = -s; break;         // SIDE ONLY
                case 3: l = r = m; break;             // MONO
                default: break;
                }
                const float t = trim_.next(tT);
                L[i] = kit::sane(l * balL_.next(blT) * t); R[i] = kit::sane(r * balR_.next(brT) * t);
            }
        meters(L, R, n);
    }

private:
    [[nodiscard]] float effWidth() const noexcept {
        static constexpr std::array<float, 4> kMul{0.5f, 1.0f, 1.5f, 2.0f};
        return std::min(2.0f, p(kWidth) * 0.01f * kMul[static_cast<std::size_t>(p(kPreset))]);
    }
    bool setKey(int k, float v) noexcept { if (keys_[static_cast<std::size_t>(k)] == v) return false; keys_[static_cast<std::size_t>(k)] = v; return true; }

    void meters(const float* L, const float* R, std::uint32_t n) noexcept {
        float lp = 0, rp = 0, mp = 0, sp = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            corrXY_ = 0.999f * corrXY_ + L[i] * R[i]; corrXX_ = 0.999f * corrXX_ + L[i] * L[i]; corrYY_ = 0.999f * corrYY_ + R[i] * R[i];
            lp = std::max(lp, std::fabs(L[i])); rp = std::max(rp, std::fabs(R[i]));
            mp = std::max(mp, std::fabs(0.5f * (L[i] + R[i]))); sp = std::max(sp, 0.5f * (std::fabs(L[i]) + std::fabs(R[i])));
        }
        const float corr = corrXY_ / std::sqrt(std::max(corrXX_ * corrYY_, 1e-20f));
        lPk_ = std::max(lp, lPk_ * 0.97f); rPk_ = std::max(rp, rPk_ * 0.97f);
        monoPk_ = std::max(mp, monoPk_ * 0.97f); stPk_ = std::max(sp, stPk_ * 0.97f);
        publishMeter(0, std::clamp(corr, -1.0f, 1.0f));
        publishMeter(1, effWidth() * 100.0f);
        publishMeter(2, p(kRotate));
        publishMeter(3, kit::gainToDb(lPk_)); publishMeter(4, kit::gainToDb(rPk_));
        publishMeter(5, kit::gainToDb(monoPk_) - kit::gainToDb(stPk_));   // how much quieter it gets in mono
    }

    double fs_ = 48000;
    hc::fx::Biquad monoLp_, sideHp_, sideLp_, airHs_, harmHp_;
    kit::HilbertPair hil_, hilRef_;
    hc::fx::DelayLine depthL_;
    hc::fx::Smoothed width_, mid_, side_, balL_, balR_, trim_, cosR_, sinR_, rotMix_;
    std::array<float, 5> keys_{};
    float dynEnvM_ = 0, dynEnvS_ = 0, corrXY_ = 0, corrXX_ = 0, corrYY_ = 0, monoPk_ = 0, stPk_ = 0, lPk_ = 0, rPk_ = 0;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeSipa() { return std::make_unique<Sipa>(); }

} // namespace hem::staging
