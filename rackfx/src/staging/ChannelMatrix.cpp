// =============================================================================
//  29 CHANNELMATRIX — a modular channel strip whose modules can be put in any order:
//    PRE        saturation (ODD / EVEN / DENSE / EXTREME), HP and LP (6 / 12 / 18 / 24 dB/oct with
//               resonance), LOW BUMP (+2 / +4 dB just above the HP corner)
//    EQ         LOW (shelf / bell), LOW-MID and HIGH-MID (WIDE / NARROW / PARAM Q), HIGH (shelf / bell)
//    DUAL DYN   two dynamic bands (COMP / EXPAND / LIMIT around their own frequency), each with
//               LISTEN, a gain-reduction readout and a static OUTPUT gain
//    COMPRESSOR VCA / FET / OPT / SOFT, threshold, ratio, attack, release, parallel MIX, OUTPUT, a
//               sidechain filter (LOW SHELF / BELL / HP) with LISTEN and an external key
//    GATE       GATE / EXPAND, threshold, FLOOR, CLOSE (hysteresis), attack, release, sidechain HP / LP
//               with LISTEN, LINK and an external key
//  Five ORDER slots pick the chain; a module named twice runs once (first slot wins) and a module
//  left out runs after the rest in the default order. Dynamics modules run STEREO (linked), DUAL
//  (each side on its own) or M/S (mid and side on their own); PRE runs STEREO or M/S.
//  MASTER: input and output faders, width, MONITOR (STEREO / MONO / L / R / M / S), safety LIMITER
//  (0 dBFS ceiling, on), polarity, peak meters with reset, VU (IN / OUT / GR) with trim.
//  MONITOR M plays the mid on both sides, which sounds the same as MONO; S plays the side.
//  Not built (cannot be): the INSERT slot that hosts other plugins, and a second knob set per side
//  for DUAL / M-S. LINK / COPY / FOCUS are faceplate editing features, not sound.
//  Dead flat on load: saturation 0, filters at 20 Hz / 20 kHz (= off), EQ gains 0, band
//  thresholds 0 dB in COMP mode, compressor MIX 0 %, GATE module off, faders 0 dB, width 100 %,
//  limiter ceiling 0 dBFS (normal audio never reaches it).
//  ASSUMED: dynamic bands 3:1 / 1:2 / 20:1, 5 ms / 80 ms; compressor types (VCA hard knee, FET
//  fast + gritty, OPT slow + two-stage release, SOFT 10 dB knee); sidechain shelf -12 dB, bell
//  +12 dB; WIDE Q 0.5, NARROW Q 3; GATE module off on load (its -60 dB floor would cut quiet tails).
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

enum Mod { mPre, mEq, mDyn, mComp, mGate, mCount };
enum P {
    kSlot1, kSlot2, kSlot3, kSlot4, kSlot5,
    kPreOn, kPreMode, kSatAmt, kSatType, kHp, kLp, kSlope, kRes, kBump,
    kEqOn, kLoType, kLoF, kLoG, kLoQ, kLmType, kLmF, kLmG, kLmQ, kHmType, kHmF, kHmG, kHmQ, kHiType, kHiF, kHiG, kHiQ,
    kDynOn, kDynMode, kB1Mode, kB1F, kB1Thr, kB1Listen, kB1Out, kB2Mode, kB2F, kB2Thr, kB2Listen, kB2Out,
    kCompOn, kCompMode, kCType, kCThr, kCRatio, kCAtt, kCRel, kCMix, kCOut, kCScType, kCScF, kCScQ, kCListen, kCExt,
    kGateOn, kGateMode, kGType, kGThr, kGFloor, kGClose, kGAtt, kGRel, kGScHp, kGScLp, kGListen, kGLink, kGExt,
    kIn, kOut, kWidth, kMonitor, kLimiter, kPolarity, kPeakReset, kVuSrc, kVuTrim, kBypass, kCount
};
const char* const kModN[] = {"PRE", "EQ", "DUAL DYNAMICS", "COMPRESSOR", "GATE"};
const char* const kDynModeN[] = {"STEREO", "DUAL", "M/S"};
const char* const kPreModeN[] = {"STEREO", "M/S"};
const char* const kSatN[] = {"ODD", "EVEN", "DENSE", "EXTREME"};
const char* const kSlopeN[] = {"6 dB/OCT", "12 dB/OCT", "18 dB/OCT", "24 dB/OCT"};
const char* const kBumpN[] = {"OFF", "+2 dB", "+4 dB"};
const char* const kShN[] = {"SHELF", "BELL"};
const char* const kMidN[] = {"WIDE", "NARROW", "PARAM"};
const char* const kBandN[] = {"COMP", "EXPAND", "LIMIT"};
const char* const kCTypeN[] = {"VCA", "FET", "OPT", "SOFT"};
const char* const kScN[] = {"LOW SHELF", "BELL", "HP"};
const char* const kGTypeN[] = {"GATE", "EXPAND"};
const char* const kMonN[] = {"STEREO", "MONO", "L", "R", "M", "S"};
const char* const kVuN[] = {"IN", "OUT", "GR"};
const ParamInfo kParams[kCount] = {
    {"slot1", "ORDER 1", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kModN},
    {"slot2", "ORDER 2", "", 0.0f, 4.0f, 1.0f, Taper::Choice, kModN},
    {"slot3", "ORDER 3", "", 0.0f, 4.0f, 2.0f, Taper::Choice, kModN},
    {"slot4", "ORDER 4", "", 0.0f, 4.0f, 3.0f, Taper::Choice, kModN},
    {"slot5", "ORDER 5", "", 0.0f, 4.0f, 4.0f, Taper::Choice, kModN},
    {"pre_enable", "PRE ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"pre_mode", "PRE STEREO / M-S", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kPreModeN},
    {"sat_amount", "SATURATION AMOUNT", "%", 0.0f, 100.0f, 0.0f},
    {"sat_type", "SATURATION TYPE", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kSatN},
    {"hp_freq", "HP FREQ", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"lp_freq", "LP FREQ", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"slope", "SLOPE", "", 0.0f, 3.0f, 1.0f, Taper::Choice, kSlopeN},
    {"resonance", "RESONANCE", "", 0.5f, 4.0f, 0.7f, Taper::Log},
    {"low_bump", "LOW BUMP", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kBumpN},
    {"eq_enable", "EQ ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"low_type", "LOW SHELF / BELL", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kShN},
    {"low_freq", "LOW FREQ", "Hz", 20.0f, 500.0f, 100.0f, Taper::Log},
    {"low_gain", "LOW GAIN", "dB", -18.0f, 18.0f, 0.0f},
    {"low_q", "LOW Q", "", 0.3f, 4.0f, 0.7f, Taper::Log},
    {"lowmid_type", "LOW-MID TYPE", "", 0.0f, 2.0f, 2.0f, Taper::Choice, kMidN},
    {"lowmid_freq", "LOW-MID FREQ", "Hz", 100.0f, 2000.0f, 400.0f, Taper::Log},
    {"lowmid_gain", "LOW-MID GAIN", "dB", -18.0f, 18.0f, 0.0f},
    {"lowmid_q", "LOW-MID Q", "", 0.3f, 8.0f, 1.0f, Taper::Log},
    {"highmid_type", "HIGH-MID TYPE", "", 0.0f, 2.0f, 2.0f, Taper::Choice, kMidN},
    {"highmid_freq", "HIGH-MID FREQ", "Hz", 800.0f, 8000.0f, 2500.0f, Taper::Log},
    {"highmid_gain", "HIGH-MID GAIN", "dB", -18.0f, 18.0f, 0.0f},
    {"highmid_q", "HIGH-MID Q", "", 0.3f, 8.0f, 1.0f, Taper::Log},
    {"high_type", "HIGH SHELF / BELL", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kShN},
    {"high_freq", "HIGH FREQ", "Hz", 2000.0f, 20000.0f, 10000.0f, Taper::Log},
    {"high_gain", "HIGH GAIN", "dB", -18.0f, 18.0f, 0.0f},
    {"high_q", "HIGH Q", "", 0.3f, 4.0f, 0.7f, Taper::Log},
    {"dyn_enable", "DUAL DYNAMICS ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"dyn_mode", "DUAL DYNAMICS STEREO / DUAL / M-S", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kDynModeN},
    {"band1_mode", "BAND 1 MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kBandN},
    {"band1_freq", "BAND 1 FREQ", "Hz", 20.0f, 20000.0f, 200.0f, Taper::Log},
    {"band1_thresh", "BAND 1 THRESH", "dB", -60.0f, 0.0f, 0.0f},
    {"band1_listen", "BAND 1 LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"band1_output", "BAND 1 OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"band2_mode", "BAND 2 MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kBandN},
    {"band2_freq", "BAND 2 FREQ", "Hz", 20.0f, 20000.0f, 4000.0f, Taper::Log},
    {"band2_thresh", "BAND 2 THRESH", "dB", -60.0f, 0.0f, 0.0f},
    {"band2_listen", "BAND 2 LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"band2_output", "BAND 2 OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"comp_enable", "COMPRESSOR ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"comp_mode", "COMPRESSOR STEREO / DUAL / M-S", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kDynModeN},
    {"comp_type", "COMPRESSOR TYPE", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kCTypeN},
    {"comp_threshold", "COMP THRESHOLD", "dB", -60.0f, 0.0f, 0.0f},
    {"comp_ratio", "COMP RATIO", ":1", 1.0f, 20.0f, 1.0f, Taper::Log},
    {"comp_attack", "COMP ATTACK", "ms", 0.05f, 100.0f, 10.0f, Taper::Log},
    {"comp_release", "COMP RELEASE", "ms", 10.0f, 2000.0f, 150.0f, Taper::Log},
    {"comp_mix", "COMP MIX", "%", 0.0f, 100.0f, 0.0f},
    {"comp_output", "COMP OUTPUT", "dB", -12.0f, 24.0f, 0.0f},
    {"comp_sc_type", "COMP SIDECHAIN FILTER", "", 0.0f, 2.0f, 2.0f, Taper::Choice, kScN},
    {"comp_sc_freq", "COMP SIDECHAIN FREQ", "Hz", 20.0f, 10000.0f, 20.0f, Taper::Log},
    {"comp_sc_q", "COMP SIDECHAIN Q", "", 0.3f, 4.0f, 0.7f, Taper::Log},
    {"comp_sc_listen", "COMP SIDECHAIN LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"comp_ext_sc", "COMP EXTERNAL SIDECHAIN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"gate_enable", "GATE ENABLE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"gate_mode", "GATE STEREO / DUAL / M-S", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kDynModeN},
    {"gate_type", "GATE / EXPAND", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kGTypeN},
    {"gate_threshold", "GATE THRESHOLD", "dB", -80.0f, 0.0f, -60.0f},
    {"gate_floor", "GATE FLOOR", "dB", -80.0f, 0.0f, -60.0f},
    {"gate_close", "GATE CLOSE", "dB", -80.0f, 0.0f, -60.0f},
    {"gate_attack", "GATE ATTACK", "ms", 0.01f, 50.0f, 1.0f, Taper::Log},
    {"gate_release", "GATE RELEASE", "ms", 5.0f, 2000.0f, 100.0f, Taper::Log},
    {"gate_sc_hp", "GATE SIDECHAIN HP", "Hz", 20.0f, 2000.0f, 20.0f, Taper::Log},
    {"gate_sc_lp", "GATE SIDECHAIN LP", "Hz", 500.0f, 20000.0f, 20000.0f, Taper::Log},
    {"gate_sc_listen", "GATE SIDECHAIN LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"gate_link", "GATE SIDECHAIN LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"gate_ext_sc", "GATE EXTERNAL SIDECHAIN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"input", "INPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"width", "WIDTH", "%", 0.0f, 200.0f, 100.0f},
    {"monitor", "MONITOR", "", 0.0f, 5.0f, 0.0f, Taper::Choice, kMonN},
    {"limiter", "LIMITER", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"polarity", "POLARITY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"peak_reset", "PEAK RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"vu_source", "VU SOURCE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kVuN},
    {"vu_trim", "VU SOURCE TRIM", "dB", -12.0f, 12.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"VU", "VU", -30, 3, true}, {"PEAK L", "dBFS", -60, 0}, {"PEAK R", "dBFS", -60, 0},
    {"BAND 1 GR", "dB", -30, 0}, {"BAND 2 GR", "dB", -30, 0}, {"COMP GR", "dB", -30, 0}, {"GATE GR", "dB", -80, 0}, {"LIMITER GR", "dB", -30, 0}};

struct OnePole {   // 6 dB/oct low- or high-pass
    float a = 0, z = 0;
    void set(double fs, double hz) noexcept { a = static_cast<float>(std::exp(-kit::kTau * hz / fs)); }
    void clear() noexcept { z = 0; }
    float lp(float x) noexcept { z = x + a * (z - x); return z; }
    float hp(float x) noexcept { return x - lp(x); }
};

class ChannelMatrix final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    ChannelMatrix() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i > kPreOn && i < kEqOn) return p(kPreOn) > 0.5f;
        if (i > kEqOn && i < kDynOn) {
            if (i == kLmQ) return p(kEqOn) > 0.5f && static_cast<int>(p(kLmType)) == 2;
            if (i == kHmQ) return p(kEqOn) > 0.5f && static_cast<int>(p(kHmType)) == 2;
            return p(kEqOn) > 0.5f;
        }
        if (i > kDynOn && i < kCompOn) return p(kDynOn) > 0.5f;
        if (i > kCompOn && i < kGateOn) return p(kCompOn) > 0.5f && (i != kCScQ || static_cast<int>(p(kCScType)) != 2);
        if (i > kGateOn && i < kIn) return p(kGateOn) > 0.5f;
        if (i == kRes) return static_cast<int>(p(kSlope)) > 0;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto* s : {&in_, &out_, &wid_, &satS_, &cMix_, &cOut_}) s->prepare(fs, 20.0, 1.0f);
        satS_.prepare(fs, 20.0, 0.0f); cMix_.prepare(fs, 20.0, 0.0f);
        for (auto& s : bOut_) s.prepare(fs, 20.0, 0.0f);
        cThr_.prepare(fs, 30.0, 0.0f); cRat_.prepare(fs, 30.0, 1.0f);
        for (auto& g : gl_) g.prepare(fs, kSub, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : ch_) {
            for (auto& f : c.hp) f.clear();
            for (auto& f : c.lp) f.clear();
            c.hp1.clear(); c.lp1.clear(); c.bump.clear();
            for (auto& f : c.eq) f.clear();
            for (auto& f : c.bandDet) f.clear();
            for (auto& f : c.bandEq) f.clear();
            c.cSc.clear(); c.gHp.clear(); c.gLp.clear();
            c.dc = 0; c.bandEnv.fill(-120); c.bandGr.fill(0); c.cEnv = -120; c.cGr = 0; c.cSlow = 0; c.gGr = 0; c.gOpen = false;
        }
        keyPre_ = keyEq_ = keyC_ = keyG_ = -1; preHpOn_ = preLpOn_ = false; eqOn_.fill(false); gHpOn_ = gLpOn_ = false; cScOn_ = false;
        limG_ = 1; pk_.fill(0); vu_ = 0; loaded_ = false; bandActive_.fill(false); fade_.fill(0.0f);
        bandGrM_.fill(0); cGrM_ = gGrM_ = limGrM_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; publishAll(); return; }
        if (p(kPeakReset) > 0.5f) pk_.fill(0);
        const float inT = kit::dbToGain(p(kIn)) * (p(kPolarity) > 0.5f ? -1.0f : 1.0f), outT = kit::dbToGain(p(kOut)), wT = p(kWidth) * 0.01f;
        if (!loaded_) {
            in_.cur = inT; out_.cur = outT; wid_.cur = wT; satS_.cur = p(kSatAmt) * 0.01f; cMix_.cur = p(kCMix) * 0.01f; cOut_.cur = kit::dbToGain(p(kCOut));
            bOut_[0].cur = p(kB1Out); bOut_[1].cur = p(kB2Out); cThr_.cur = p(kCThr); cRat_.cur = p(kCRatio); snap_ = true; loaded_ = true;
        }
        listen_ = false;
        bandGrM_.fill(0); cGrM_ = gGrM_ = limGrM_ = 0;
        float vuIn = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(inT);
            vuIn = std::max(vuIn, 0.5f * (std::fabs(L[i]) + std::fabs(R[i])));
            L[i] *= g; R[i] *= g;
        }
        // ---- the chain, in slot order ----
        std::array<bool, mCount> done{};
        std::array<int, mCount> order{};
        int k = 0;
        for (int s = 0; s < 5; ++s) { const int m = static_cast<int>(p(kSlot1 + s)); if (!done[static_cast<std::size_t>(m)]) { done[static_cast<std::size_t>(m)] = true; order[static_cast<std::size_t>(k++)] = m; } }
        for (int m = 0; m < mCount; ++m) if (!done[static_cast<std::size_t>(m)]) order[static_cast<std::size_t>(k++)] = m;
        // a module that is switched off still glides its settings, so switching it on never jumps
        if (p(kPreOn) < 0.5f) preCoefs();
        if (p(kEqOn) < 0.5f) eqCoefs();
        for (int m : order) runModule(m, L, R, n);
        snap_ = false;
        // ---- master ----
        const int mon = static_cast<int>(p(kMonitor));
        const bool lim = p(kLimiter) > 0.5f;
        const float limRel = hc::fx::onePoleCoef(fs_, 50.0);
        float vuOut = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = listen_ ? lisL_[i] : L[i], r = listen_ ? lisR_[i] : R[i];
            const float w = wid_.next(wT);
            if (!(kit::isFlat(w, 1.0f) && kit::isFlat(wT, 1.0f))) { const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w; l = m + s; r = m - s; }
            const float o = out_.next(outT);
            l *= o; r *= o;
            switch (mon) {
                case 1: { const float m = 0.5f * (l + r); l = r = m; break; }
                case 2: r = l; break;
                case 3: l = r; break;
                case 4: { const float m = 0.5f * (l + r); l = r = m; break; }
                case 5: { const float s = 0.5f * (l - r); l = s; r = s; break; }
                default: break;
            }
            if (lim) {   // safety limiter: 0 dBFS ceiling, instant grab, 50 ms recovery; never above the ceiling
                const float pkv = std::max(std::fabs(l), std::fabs(r));
                const float need = pkv > 1.0f ? 1.0f / pkv : 1.0f;
                limG_ = std::min(need, 1.0f + limRel * (limG_ - 1.0f));
                l *= limG_; r *= limG_;
                limGrM_ = std::max(limGrM_, -kit::gainToDb(limG_));
            } else limG_ = 1.0f;
            pk_[0] = std::max(pk_[0], std::fabs(l)); pk_[1] = std::max(pk_[1], std::fabs(r));
            vuOut = std::max(vuOut, 0.5f * (std::fabs(l) + std::fabs(r)));
            L[i] = kit::sane(l); R[i] = kit::sane(r);
        }
        const int vs = static_cast<int>(p(kVuSrc));
        const float src = vs == 0 ? vuIn : vuOut;
        const float c = std::pow(hc::fx::onePoleCoef(fs_, 300.0), static_cast<float>(n));
        vu_ = src * 0.707f + c * (vu_ - src * 0.707f);
        scL_ = scR_ = nullptr;
        publishAll();
    }

private:
    struct Chan {
        std::array<hc::fx::Biquad, 2> hp{}, lp{};
        OnePole hp1, lp1;
        hc::fx::Biquad bump;
        std::array<hc::fx::Biquad, 4> eq{};
        std::array<hc::fx::Biquad, 2> bandDet{}, bandEq{};
        hc::fx::Biquad cSc, gHp, gLp;
        float dc = 0;
        std::array<float, 2> bandEnv{}, bandGr{};
        float cEnv = -120, cGr = 0, cSlow = 0, gGr = 0;
        bool gOpen = false;
    };
    static void toMs(float* a, float* b, std::uint32_t n) noexcept { for (std::uint32_t i = 0; i < n; ++i) { const float m = 0.5f * (a[i] + b[i]), s = 0.5f * (a[i] - b[i]); a[i] = m; b[i] = s; } }
    static void fromMs(float* a, float* b, std::uint32_t n) noexcept { for (std::uint32_t i = 0; i < n; ++i) { const float l = a[i] + b[i], r = a[i] - b[i]; a[i] = l; b[i] = r; } }

    void runModule(int m, float* L, float* R, std::uint32_t n) noexcept {
        switch (m) {
            case mPre: if (p(kPreOn) > 0.5f) { const bool ms = p(kPreMode) > 0.5f; if (ms) toMs(L, R, n); pre(L, R, n); if (ms) fromMs(L, R, n); } break;
            case mEq: if (p(kEqOn) > 0.5f) eq(L, R, n); break;
            case mDyn: if (p(kDynOn) > 0.5f) { const int md = static_cast<int>(p(kDynMode)); if (md == 2) toMs(L, R, n); dualDyn(L, R, n, md == 0); if (md == 2) fromMs(L, R, n); } break;
            case mComp: if (p(kCompOn) > 0.5f) { const int md = static_cast<int>(p(kCompMode)); if (md == 2) toMs(L, R, n); comp(L, R, n, md == 0); if (md == 2) fromMs(L, R, n); } break;
            case mGate: if (p(kGateOn) > 0.5f) { const int md = static_cast<int>(p(kGateMode)); if (md == 2) toMs(L, R, n); gate(L, R, n, md == 0 && p(kGLink) > 0.5f); if (md == 2) fromMs(L, R, n); } break;
            default: break;
        }
    }

    // glide a setting toward its knob (frequencies and Q in log steps); the first block after loading starts at the knob
    float glide(std::size_t k, float target, bool logd) noexcept {
        auto& g = gl_[k];
        const float t = logd ? std::log(target) : target;
        if (snap_) g.cur = t;
        const float v = g.step(t);
        return logd ? std::exp(v) : v;
    }
    static constexpr std::uint32_t kDynSub = 16;   // dynamic-band curve update
    static constexpr std::uint32_t kSub = 32;   // filter and EQ settings glide, a fresh curve every 32 samples
    void stepFade(std::size_t k, bool on) noexcept { if (on && fade_[k] < 1.0f) fade_[k] = std::min(1.0f, fade_[k] + static_cast<float>(1.0 / (0.02 * fs_))); }

    // ---- PRE ----
    void preCoefs() noexcept {
        const float hp = glide(0, p(kHp), true), lp = glide(1, p(kLp), true), res = glide(2, p(kRes), true);
        const int slope = static_cast<int>(p(kSlope)), bump = static_cast<int>(p(kBump));
        const float key = hp * 1e5f + lp + res * 7.0f + static_cast<float>(slope) * 3.0f + static_cast<float>(bump) * 11.0f;
        if (key == keyPre_) return;
        keyPre_ = key;
        if (hp > 20.01f || bump > 0) preHpOn_ = true;   // once used, keep running (no click on return)
        if (lp < 19999.0f) preLpOn_ = true;
        const double ny = fs_ * 0.45;
        const float r = res / 0.7071f;   // 0.7 = plain Butterworth, higher = a resonant peak at the corner
        for (auto& c : ch_) {
            c.hp1.set(fs_, hp); c.lp1.set(fs_, std::min<double>(lp, ny));
            const double qa = slope == 3 ? 0.5412 * r : 0.7071 * r, qb = 1.3066 * r;
            c.hp[0].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, slope == 2 ? 1.0 * r : qa);
            c.hp[1].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, qb);
            c.lp[0].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, ny), slope == 2 ? 1.0 * r : qa);
            c.lp[1].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, ny), qb);
            c.bump.c = hc::fx::BiquadCoefs::make(BT::Bell, fs_, std::max(50.0, hp * 1.4), 1.2, bump == 1 ? 2.0 : bump == 2 ? 4.0 : 0.0);
        }
    }
    void pre(float* L, float* R, std::uint32_t n) noexcept {
        const int type = static_cast<int>(p(kSatType)), slope = static_cast<int>(p(kSlope));
        const float satT = p(kSatAmt) * 0.01f;
        float* io[2] = {L, R};
        for (std::uint32_t at = 0; at < n; at += kSub) {
            preCoefs();
            const std::uint32_t end = std::min(n, at + kSub);
            for (std::uint32_t i = at; i < end; ++i) {
                stepFade(0, preHpOn_); stepFade(1, preLpOn_);
                const float a = satS_.next(satT);
                for (std::size_t c = 0; c < 2; ++c) {
                    float x = io[c][i];
                    Chan& h = ch_[c];
                    if (a > 1e-6f) {   // amount blends from clean to fully driven, so 0 % is exactly clean
                        const float kd = 1.0f + 9.0f * a;
                        float y;
                        switch (type) {
                            case 1: y = x + 0.25f * kd * x * x; break;                                                       // EVEN: 2nd harmonic (DC removed below)
                            case 2: y = (std::tanh(kd * (x + 0.15f)) - std::tanh(kd * 0.15f)) / kd; break;                   // DENSE: odd + even, asymmetric
                            case 3: y = std::clamp(std::tanh(kd * 1.8f * x) * 1.3f, -1.0f, 1.0f) / std::max(1.0f, kd * 0.5f); break;   // EXTREME
                            default: y = std::tanh(kd * x) / kd; break;                                                      // ODD
                        }
                        x += a * (y - x);
                        h.dc += 0.0005f * (x - h.dc);
                        if (type == 1 || type == 2) x -= h.dc;
                    }
                    if (preHpOn_) {   // a filter that starts running fades in over 20 ms (its memory starts empty)
                        float y;
                        if (slope == 0) y = h.hp1.hp(x);
                        else if (slope == 1) y = h.hp[0].tick(x);
                        else if (slope == 2) y = h.hp[0].tick(h.hp1.hp(x));
                        else y = h.hp[1].tick(h.hp[0].tick(x));
                        y = h.bump.tick(y);
                        x += fade_[0] * (y - x);
                    }
                    if (preLpOn_) {
                        float y;
                        if (slope == 0) y = h.lp1.lp(x);
                        else if (slope == 1) y = h.lp[0].tick(x);
                        else if (slope == 2) y = h.lp[0].tick(h.lp1.lp(x));
                        else y = h.lp[1].tick(h.lp[0].tick(x));
                        x += fade_[1] * (y - x);
                    }
                    io[c][i] = x;
                }
            }
        }
    }

    // ---- EQ ----
    void eqCoefs() noexcept {
        const std::array<int, 4> type{static_cast<int>(p(kLoType)), static_cast<int>(p(kLmType)), static_cast<int>(p(kHmType)), static_cast<int>(p(kHiType))};
        const std::array<float, 4> f{glide(3, p(kLoF), true), glide(4, p(kLmF), true), glide(5, p(kHmF), true), glide(6, p(kHiF), true)};
        const std::array<float, 4> g{glide(7, p(kLoG), false), glide(8, p(kLmG), false), glide(9, p(kHmG), false), glide(10, p(kHiG), false)};
        const std::array<float, 4> q{glide(11, p(kLoQ), true), glide(12, p(kLmQ), true), glide(13, p(kHmQ), true), glide(14, p(kHiQ), true)};
        float key = 0;
        for (std::size_t b = 0; b < 4; ++b) key += (f[b] * 0.01f + g[b] * 7.0f + q[b] * 3.0f + static_cast<float>(type[b]) * 5.0f) * static_cast<float>(b + 1) * 1.37f;
        if (key == keyEq_) return;
        keyEq_ = key;
        const double ny = fs_ * 0.45;
        for (std::size_t b = 0; b < 4; ++b) {
            if (std::fabs(g[b]) > 1e-4f) eqOn_[b] = true;
            hc::fx::BiquadCoefs c;
            const double hz = std::min<double>(f[b], ny);
            if (b == 0) c = type[0] == 0 ? hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, hz, q[0], g[0]) : hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, q[0], g[0]);
            else if (b == 3) c = type[3] == 0 ? hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, hz, q[3], g[3]) : hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, q[3], g[3]);
            else c = hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, type[b] == 0 ? 0.5 : type[b] == 1 ? 3.0 : q[b], g[b]);
            for (auto& h : ch_) h.eq[b].c = c;
        }
    }
    void eq(float* L, float* R, std::uint32_t n) noexcept {
        for (std::uint32_t at = 0; at < n; at += kSub) {
            eqCoefs();
            const std::uint32_t m = std::min(kSub, n - at);
            for (std::size_t b = 0; b < 4; ++b) {
                if (!eqOn_[b]) continue;
                float& f = fade_[2 + b];
                if (f >= 1.0f) { ch_[0].eq[b].run(L + at, m); ch_[1].eq[b].run(R + at, m); continue; }
                for (std::uint32_t i = at; i < at + m; ++i) {   // a band that starts running fades in over 20 ms
                    stepFade(2 + b, true);
                    L[i] += f * (ch_[0].eq[b].tick(L[i]) - L[i]); R[i] += f * (ch_[1].eq[b].tick(R[i]) - R[i]);
                }
            }
        }
    }

    // ---- DUAL DYNAMICS: two dynamic bells ----
    void dualDyn(float* L, float* R, std::uint32_t n, bool linked) noexcept {
        const std::array<int, 2> mode{static_cast<int>(p(kB1Mode)), static_cast<int>(p(kB2Mode))};
        const std::array<float, 2> f{p(kB1F), p(kB2F)}, thr{p(kB1Thr), p(kB2Thr)}, outT{p(kB1Out), p(kB2Out)};
        const std::array<bool, 2> lis{p(kB1Listen) > 0.5f, p(kB2Listen) > 0.5f};
        const float att = hc::fx::onePoleCoef(fs_, 5.0), rel = hc::fx::onePoleCoef(fs_, 80.0);
        const double ny = fs_ * 0.45;
        for (std::size_t b = 0; b < 2; ++b) for (auto& h : ch_) h.bandDet[b].c = hc::fx::BiquadCoefs::make(BT::BandPass, fs_, std::min<double>(f[b], ny), 1.4);
        float* io[2] = {L, R};
        for (std::uint32_t s0 = 0; s0 < n; s0 += kDynSub) {
            const std::uint32_t e = std::min(n, s0 + kDynSub);
            std::array<float, 2> ob{};
            for (std::size_t b = 0; b < 2; ++b) for (std::uint32_t i = s0; i < e; ++i) ob[b] = bOut_[b].next(outT[b]);
            for (std::size_t b = 0; b < 2; ++b) {
                // detector per side (or linked): band level -> gain change
                std::array<float, 2> grB{};
                for (std::uint32_t i = s0; i < e; ++i) {
                    std::array<float, 2> lv{};
                    for (std::size_t c = 0; c < 2; ++c) {
                        const float d = ch_[c].bandDet[b].tick(io[c][i]);
                        if (lis[b] && !listen_) { (c == 0 ? lisL_ : lisR_)[i] = d; }
                        lv[c] = kit::gainToDb(std::fabs(d) * 1.414f);
                    }
                    if (linked) lv[0] = lv[1] = std::max(lv[0], lv[1]);
                    for (std::size_t c = 0; c < 2; ++c) {
                        float& env = ch_[c].bandEnv[b];
                        env = lv[c] + (lv[c] > env ? att : rel) * (env - lv[c]);
                        float gr = 0;
                        const float over = env - thr[b];
                        if (mode[b] == 0) gr = over > 0 ? over * (1.0f - 1.0f / 3.0f) : 0.0f;
                        else if (mode[b] == 2) gr = over > 0 ? over * (1.0f - 1.0f / 20.0f) : 0.0f;
                        else gr = over < 0 ? std::min(20.0f, -over) : 0.0f;   // EXPAND 1:2 below the threshold, at most 20 dB
                        grB[c] = std::min(gr, 30.0f);
                    }
                }
                for (std::size_t c = 0; c < 2; ++c) {
                    ch_[c].bandGr[b] = grB[c];
                    ch_[c].bandEq[b].c = hc::fx::BiquadCoefs::make(BT::Bell, fs_, std::min<double>(f[b], ny), 1.4, ob[b] - grB[c]);
                    bandGrM_[b] = std::max(bandGrM_[b], grB[c]);
                }
            }
            for (std::size_t b = 0; b < 2; ++b) {
                if (lis[b] && !listen_) { listenB_ = true; }
                if (std::fabs(ob[b]) < 1e-5f && ch_[0].bandGr[b] < 1e-5f && ch_[1].bandGr[b] < 1e-5f && !bandActive_[b]) continue;
                bandActive_[b] = true;   // once it has moved, the bell keeps running (a 0 dB bell passes exactly)
                for (std::uint32_t i = s0; i < e; ++i) for (std::size_t c = 0; c < 2; ++c) io[c][i] = ch_[c].bandEq[b].tick(io[c][i]);
            }
        }
        if (listenB_) { listen_ = true; listenB_ = false; }
    }

    // ---- COMPRESSOR ----
    void comp(float* L, float* R, std::uint32_t n, bool linked) noexcept {
        const int type = static_cast<int>(p(kCType)), sc = static_cast<int>(p(kCScType));
        const float scF = p(kCScF), scQ = p(kCScQ);
        const float key = scF + scQ * 1e5f + static_cast<float>(sc) * 3e5f;
        if (key != keyC_) {
            keyC_ = key;
            if (sc != 2 || scF > 20.01f) cScOn_ = true;
            const double ny = fs_ * 0.45, hz = std::min<double>(scF, ny);
            const auto c = sc == 0 ? hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, hz, scQ, -12.0) : sc == 1 ? hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, scQ, 12.0) : hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hz, scQ);
            for (auto& h : ch_) h.cSc.c = c;
        }
        const bool lis = p(kCListen) > 0.5f, ext = p(kCExt) > 0.5f && scL_ != nullptr;
        double attMs = p(kCAtt), relMs = p(kCRel);
        if (type == 1) { attMs *= 0.3; relMs *= 0.6; }   // FET: quicker
        if (type == 2) attMs *= 3.0;                       // OPT: slower grab
        const float att = hc::fx::onePoleCoef(fs_, attMs), rel = hc::fx::onePoleCoef(fs_, relMs), slowRel = hc::fx::onePoleCoef(fs_, relMs * 6.0);
        const float knee = type == 3 ? 10.0f : 0.0f;
        const float mixT = p(kCMix) * 0.01f, outT = kit::dbToGain(p(kCOut));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float t = cThr_.next(p(kCThr)), r = cRat_.next(p(kCRatio));
            std::array<float, 2> d{ext ? scL_[i] : L[i], ext ? scR_[i] : R[i]}, lv{};
            for (std::size_t c = 0; c < 2; ++c) {
                if (cScOn_) d[c] = ch_[c].cSc.tick(d[c]);
                if (lis && !listen_) (c == 0 ? lisL_ : lisR_)[i] = d[c];
                lv[c] = kit::gainToDb(std::fabs(d[c]));
            }
            if (linked) lv[0] = lv[1] = std::max(lv[0], lv[1]);
            const float mx = cMix_.next(mixT), og = cOut_.next(outT);
            for (std::size_t c = 0; c < 2; ++c) {
                Chan& h = ch_[c];
                h.cEnv = lv[c] + (lv[c] > h.cEnv ? att : rel) * (h.cEnv - lv[c]);
                const float over = h.cEnv - t;
                float gr;
                if (knee > 0 && std::fabs(over) < knee * 0.5f) { const float x = over + knee * 0.5f; gr = (1.0f - 1.0f / r) * x * x / (2.0f * knee); }
                else gr = over > 0 ? over * (1.0f - 1.0f / r) : 0.0f;
                if (type == 2) {   // OPT: two-stage release, the second stage slow
                    h.cSlow = gr > h.cSlow ? gr : gr + slowRel * (h.cSlow - gr);
                    gr = std::max(gr, 0.4f * h.cSlow);
                }
                h.cGr = gr;
                cGrM_ = std::max(cGrM_, gr);
                const float x = io[c][i];
                float y = x * kit::dbToGain(-gr);
                if (type == 1 && gr > 0.0f) y = std::tanh(y * (1.0f + gr * 0.02f)) / (1.0f + gr * 0.02f);   // FET grit grows with the gain reduction
                io[c][i] = (x + mx * (y - x)) * og;
            }
        }
        if (lis && !listen_) listen_ = true;
    }

    // ---- GATE ----
    void gate(float* L, float* R, std::uint32_t n, bool linked) noexcept {
        const float hp = p(kGScHp), lp = p(kGScLp);
        const float key = hp * 1e5f + lp;
        if (key != keyG_) {
            keyG_ = key;
            if (hp > 20.01f) gHpOn_ = true;
            if (lp < 19999.0f) gLpOn_ = true;
            for (auto& h : ch_) { h.gHp.c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 0.7071); h.gLp.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, fs_ * 0.45), 0.7071); }
        }
        const bool expand = p(kGType) > 0.5f, lis = p(kGListen) > 0.5f, ext = p(kGExt) > 0.5f && scL_ != nullptr;
        const float thr = p(kGThr), floorDb = -p(kGFloor), closeT = std::min(p(kGClose), thr);
        const float att = hc::fx::onePoleCoef(fs_, p(kGAtt)), rel = hc::fx::onePoleCoef(fs_, p(kGRel));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            std::array<float, 2> d{ext ? scL_[i] : L[i], ext ? scR_[i] : R[i]}, lv{};
            for (std::size_t c = 0; c < 2; ++c) {
                if (gHpOn_) d[c] = ch_[c].gHp.tick(d[c]);
                if (gLpOn_) d[c] = ch_[c].gLp.tick(d[c]);
                if (lis && !listen_) (c == 0 ? lisL_ : lisR_)[i] = d[c];
                lv[c] = kit::gainToDb(std::fabs(d[c]));
            }
            if (linked) lv[0] = lv[1] = std::max(lv[0], lv[1]);
            for (std::size_t c = 0; c < 2; ++c) {
                Chan& h = ch_[c];
                float want;
                if (!expand) {
                    if (lv[c] > thr) h.gOpen = true; else if (lv[c] < closeT) h.gOpen = false;
                    want = h.gOpen ? 0.0f : floorDb;
                } else want = std::min(floorDb, std::max(0.0f, thr - lv[c]));   // 1:2 below the threshold, down to the floor
                h.gGr = want + (want < h.gGr ? att : rel) * (h.gGr - want);
                gGrM_ = std::max(gGrM_, h.gGr);
                io[c][i] *= kit::dbToGain(-h.gGr);
            }
        }
        if (lis && !listen_) listen_ = true;
    }

    void publishAll() noexcept {
        const int vs = static_cast<int>(p(kVuSrc));
        const float trim = p(kVuTrim);
        const float vu = vs == 2 ? -(cGrM_ + gGrM_) : kit::gainToDb(vu_) + 18.0f + trim;
        publishMeter(0, std::clamp(vu, -30.0f, 3.0f));
        publishMeter(1, kit::gainToDb(pk_[0])); publishMeter(2, kit::gainToDb(pk_[1]));
        publishMeter(3, -bandGrM_[0]); publishMeter(4, -bandGrM_[1]); publishMeter(5, -cGrM_); publishMeter(6, -gGrM_); publishMeter(7, -limGrM_);
        publishReduction(-(cGrM_ + gGrM_ + limGrM_));
    }

    double fs_ = 48000;
    std::array<Chan, 2> ch_{};
    hc::fx::Smoothed in_, out_, wid_, satS_, cMix_, cOut_, cThr_, cRat_;
    std::array<hc::fx::Smoothed, 2> bOut_{};
    std::array<float, kMaxBlock> lisL_{}, lisR_{};
    std::array<bool, 4> eqOn_{};
    std::array<kit::BlockGlide, 15> gl_{};
    std::array<float, 6> fade_{};
    bool snap_ = true;
    std::array<bool, 2> bandActive_{};
    std::array<float, 2> bandGrM_{}, pk_{};
    float keyPre_ = -1, keyEq_ = -1, keyC_ = -1, keyG_ = -1, limG_ = 1, vu_ = 0, cGrM_ = 0, gGrM_ = 0, limGrM_ = 0;
    bool preHpOn_ = false, preLpOn_ = false, gHpOn_ = false, gLpOn_ = false, cScOn_ = false, listen_ = false, listenB_ = false, loaded_ = false;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeChannelMatrix() { return std::make_unique<ChannelMatrix>(); }

} // namespace hem::staging
