// =============================================================================
//  36 MASTERCOUNCIL — a classic studio mastering chain in five modules, in this order:
//    INPUT     gain, TAPE EQ (FLAT / NAB->IEC / IEC->NAB at 7.5 or 15 IPS), polarity (L / R / BOTH),
//              SWAP, BALANCE, PHASE (STEREO, or one side turned 90 degrees against the other)
//    TONE      four bands (LOW, LOW-MID, HIGH-MID, HIGH), each with 5 stepped frequencies, GAIN, 5
//              shapes (SHELF / WIDE / MEDIUM / NARROW / TIGHT), ENABLE and SOLO
//    DYNAMICS  CHARACTER A (feedback, soft) / B (feedforward, firmer) / LIMIT, RELEASE steps 1..6
//              (6 = auto), RATIO, AMOUNT (how far the threshold comes down), MAKEUP, parallel MIX and
//              a sidechain filter (HP / BELL / LP with FREQ, GAIN, Q, LISTEN)
//    FILTER    high pass (20..300 Hz), PRESENCE bell, low pass (5..20 kHz)
//    OUTPUT    width, MONITOR (L / STEREO / R / M / MONO / S), L / R faders with LINK
//  TONE, DYNAMICS and FILTER run STEREO (one knob set on both sides, dynamics linked), DUAL (set A on
//  the left, set B on the right) or M/S (set A on the mid, set B on the side). LINK on = set B
//  follows set A. Meters: input, gain reduction and output per side plus correlation; METER TYPE
//  (PPM / VU / CORRELATION) only changes how the meters move.
//  Different from CHANNELMATRIX (29: reorderable channel modules) and PEAKCITADEL (34: a limiter).
//  Dead flat on load: every gain 0 dB, TAPE EQ FLAT, PHASE STEREO, dynamics MIX 0 %, filters at
//  20 Hz / 20 kHz (= off), presence 0 dB, width 100 %, faders 0 dB.
//  ASSUMED: the stepped frequency tables and shape Qs; tape EQ = shelves approximating the NAB / IEC
//  difference; dynamics attack A 10 ms / B 5 ms / LIMIT 0.5 ms, knees 6 / 2 / 0 dB; AMOUNT 0..100 =
//  threshold 0..-30 dBFS; release steps 0.1 / 0.25 / 0.5 / 1 / 2 s / auto; filters 12 dB/oct.
//  The module order is fixed (the panel has no order slots) and INPUT / OUTPUT have no mode / link
//  switch (they already have their own per-side controls).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

constexpr int kBandP = 5, kToneP = 4 * kBandP, kDynP = 11, kFiltP = 4;
enum TB { tFreq, tGain, tShape, tEnable, tSolo };
enum DY { dChar, dRel, dRatio, dAmount, dMakeup, dMix, dScType, dScListen, dScFreq, dScGain, dScQ };
enum FI { fHp, fPresF, fPresG, fLp };
enum P {
    kInOn, kInGain, kTapeEq, kSpeed, kPolarity, kSwap, kBalance, kPhase,
    kToneOn, kToneMode, kToneLink, kToneA, kToneB = kToneA + kToneP,
    kDynOn = kToneB + kToneP, kDynMode, kDynLink, kDynA, kDynB = kDynA + kDynP,
    kFiltOn = kDynB + kDynP, kFiltMode, kFiltLink, kFiltA, kFiltB = kFiltA + kFiltP,
    kOutOn = kFiltB + kFiltP, kWidth, kMonitor, kOutL, kOutR, kOutLink, kMeterType, kBypass, kCount
};
const char* const kTapeN[] = {"FLAT", "NAB->IEC", "IEC->NAB"};
const char* const kSpeedN[] = {"7.5 IPS", "15 IPS"};
const char* const kPolN[] = {"OFF", "L", "R", "BOTH"};
const char* const kPhaseN[] = {"STEREO", "L+90", "R+90"};
const char* const kModeN[] = {"STEREO", "DUAL", "M/S"};
const char* const kShapeN[] = {"SHELF", "WIDE", "MEDIUM", "NARROW", "TIGHT"};
const char* const kLowF[] = {"30 HZ", "50 HZ", "80 HZ", "120 HZ", "200 HZ"};
const char* const kLmF[] = {"250 HZ", "400 HZ", "700 HZ", "1.2 KHZ", "2 KHZ"};
const char* const kHmF[] = {"1.5 KHZ", "2.5 KHZ", "3.5 KHZ", "5 KHZ", "7 KHZ"};
const char* const kHiF[] = {"5 KHZ", "8 KHZ", "10 KHZ", "12.5 KHZ", "16 KHZ"};
constexpr std::array<std::array<float, 5>, 4> kFreqs{{{30, 50, 80, 120, 200}, {250, 400, 700, 1200, 2000}, {1500, 2500, 3500, 5000, 7000}, {5000, 8000, 10000, 12500, 16000}}};
constexpr std::array<float, 5> kShapeQ{0.7071f, 0.5f, 1.0f, 2.0f, 4.0f};
const char* const kCharN[] = {"A", "B", "LIMIT"};
const char* const kRelN[] = {"1", "2", "3", "4", "5", "6"};
const char* const kScN[] = {"HP", "BELL", "LP"};
const char* const kMonN[] = {"L", "STEREO", "R", "M", "MONO", "S"};
const char* const kMeterN[] = {"PPM", "VU", "CORRELATION"};

#define HEM_TB(id, nm, fn, shape)                                                         \
    {id "_freq", nm " FREQ", "", 0.0f, 4.0f, 2.0f, Taper::Choice, fn},                   \
    {id "_gain", nm " GAIN", "dB", -12.0f, 12.0f, 0.0f},                                  \
    {id "_shape", nm " SHAPE", "", 0.0f, 4.0f, shape, Taper::Choice, kShapeN},            \
    {id "_enable", nm " ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},                    \
    {id "_solo", nm " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}
#define HEM_TONE(s, sn)                                                                   \
    HEM_TB("tone_" s "_low", "TONE " sn " LOW", kLowF, 0.0f), HEM_TB("tone_" s "_lowmid", "TONE " sn " LOW-MID", kLmF, 2.0f), \
    HEM_TB("tone_" s "_highmid", "TONE " sn " HIGH-MID", kHmF, 2.0f), HEM_TB("tone_" s "_high", "TONE " sn " HIGH", kHiF, 0.0f)
#define HEM_DYN(s, sn)                                                                    \
    {"dyn_" s "_character", "DYNAMICS " sn " CHARACTER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kCharN}, \
    {"dyn_" s "_release", "DYNAMICS " sn " RELEASE", "", 0.0f, 5.0f, 2.0f, Taper::Choice, kRelN},     \
    {"dyn_" s "_ratio", "DYNAMICS " sn " RATIO", ":1", 1.0f, 10.0f, 1.0f, Taper::Log},                  \
    {"dyn_" s "_amount", "DYNAMICS " sn " AMOUNT", "", 0.0f, 100.0f, 0.0f},                             \
    {"dyn_" s "_makeup", "DYNAMICS " sn " MAKEUP", "dB", 0.0f, 12.0f, 0.0f},                            \
    {"dyn_" s "_mix", "DYNAMICS " sn " MIX", "%", 0.0f, 100.0f, 0.0f},                                  \
    {"dyn_" s "_sc_type", "DYNAMICS " sn " SIDECHAIN", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kScN},     \
    {"dyn_" s "_sc_listen", "DYNAMICS " sn " SIDECHAIN LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},   \
    {"dyn_" s "_sc_freq", "DYNAMICS " sn " SIDECHAIN FREQ", "Hz", 20.0f, 20000.0f, 20.0f, Taper::Log},  \
    {"dyn_" s "_sc_gain", "DYNAMICS " sn " SIDECHAIN GAIN", "dB", -12.0f, 12.0f, 0.0f},                 \
    {"dyn_" s "_sc_q", "DYNAMICS " sn " SIDECHAIN Q", "", 0.3f, 4.0f, 0.7f, Taper::Log}
#define HEM_FILT(s, sn)                                                                   \
    {"filter_" s "_hp", "FILTER " sn " HIGH PASS", "Hz", 20.0f, 300.0f, 20.0f, Taper::Log},        \
    {"filter_" s "_pres_freq", "FILTER " sn " PRESENCE FREQ", "Hz", 1000.0f, 10000.0f, 3000.0f, Taper::Log}, \
    {"filter_" s "_pres_gain", "FILTER " sn " PRESENCE GAIN", "dB", -10.0f, 10.0f, 0.0f},          \
    {"filter_" s "_lp", "FILTER " sn " LOW PASS", "Hz", 5000.0f, 20000.0f, 20000.0f, Taper::Log}

const ParamInfo kParams[kCount] = {
    {"input_enable", "INPUT ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"input_gain", "INPUT GAIN", "dB", -18.0f, 18.0f, 0.0f},
    {"tape_eq", "TAPE EQ", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kTapeN},
    {"speed", "SPEED", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kSpeedN},
    {"polarity", "L/R POLARITY", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kPolN},
    {"swap", "SWAP L<->R", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"balance", "BALANCE", "dB", -6.0f, 6.0f, 0.0f},
    {"phase", "PHASE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kPhaseN},
    {"tone_enable", "TONE ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"tone_mode", "TONE STEREO / DUAL / M-S", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"tone_link", "TONE LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    HEM_TONE("a", "A"), HEM_TONE("b", "B"),
    {"dyn_enable", "DYNAMICS ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"dyn_mode", "DYNAMICS STEREO / DUAL / M-S", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"dyn_link", "DYNAMICS LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    HEM_DYN("a", "A"), HEM_DYN("b", "B"),
    {"filter_enable", "FILTER ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"filter_mode", "FILTER STEREO / DUAL / M-S", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"filter_link", "FILTER LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    HEM_FILT("a", "A"), HEM_FILT("b", "B"),
    {"output_enable", "OUTPUT ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"width", "WIDTH", "%", 0.0f, 200.0f, 100.0f},
    {"monitor", "MONITOR", "", 0.0f, 5.0f, 1.0f, Taper::Choice, kMonN},
    {"output_l", "OUTPUT L", "dB", -18.0f, 12.0f, 0.0f},
    {"output_r", "OUTPUT R", "dB", -18.0f, 12.0f, 0.0f},
    {"output_link", "OUTPUT LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"meter_type", "METER TYPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kMeterN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_TB
#undef HEM_TONE
#undef HEM_DYN
#undef HEM_FILT
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT L", "dBFS", -60, 0}, {"INPUT R", "dBFS", -60, 0}, {"GR L", "dB", -30, 0}, {"GR R", "dB", -30, 0},
    {"OUTPUT L", "dBFS", -60, 0}, {"OUTPUT R", "dBFS", -60, 0}, {"CORRELATION", "", -1, 1}};

constexpr std::uint32_t kSub = 8;

// 90-degree pair: two chains of all-passes in z^-2 (polyphase IIR), the first one sample later.
// Each stage: out = c * (in + out[t-2]) - in[t-2], c = a^2. 90 degrees apart within about 1 degree from
// 20 Hz to 20 kHz (measured: correlation under 0.02 from 50 Hz to 15 kHz at 48 kHz). Written here because
// the shared Kit pair has the stage sign flipped and the delay on the other chain (it is 90 degrees only near fs/4).
class Quadrature {
public:
    void clear() noexcept { for (auto& s : sa_) s = {}; for (auto& s : sb_) s = {}; d_ = 0; }
    void tick(float x, float& re, float& im) noexcept {
        static constexpr std::array<float, 4> ka{0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
        static constexpr std::array<float, 4> kb{0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};
        float a = x, b = x;
        for (std::size_t i = 0; i < 4; ++i) a = ap(sa_[i], ka[i] * ka[i], a);
        for (std::size_t i = 0; i < 4; ++i) b = ap(sb_[i], kb[i] * kb[i], b);
        re = d_; d_ = a; im = b;   // the one-sample delay sits on the first chain
    }
private:
    struct St { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float ap(St& s, float c, float x) noexcept {
        const float y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    std::array<St, 4> sa_{}, sb_{};
    float d_ = 0;
};

class MasterCouncil final : public hc::fx::Effect {
public:
    MasterCouncil() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i >= kToneB && i < kDynOn) return p(kToneOn) > 0.5f && static_cast<int>(p(kToneMode)) != 0 && p(kToneLink) < 0.5f;
        if (i >= kDynB && i < kFiltOn) return p(kDynOn) > 0.5f && static_cast<int>(p(kDynMode)) != 0 && p(kDynLink) < 0.5f;
        if (i >= kFiltB && i < kOutOn) return p(kFiltOn) > 0.5f && static_cast<int>(p(kFiltMode)) != 0 && p(kFiltLink) < 0.5f;
        if (i == kSpeed) return static_cast<int>(p(kTapeEq)) != 0;
        if (i == kOutR) return p(kOutLink) < 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto* s : {&inG_, &balL_, &balR_, &wid_, &oL_, &oR_}) s->prepare(fs, 20.0, 1.0f);
        phX_.prepare(fs, 20.0, 0.0f); tapeX_.prepare(fs, 20.0, 0.0f);
        for (auto& c : ch_) {
            for (auto& g : c.gl) g.prepare(fs, kSub, 30.0, 0.0f);
            c.mix.prepare(fs, 20.0, 0.0f); c.mk.prepare(fs, 20.0, 1.0f); c.thr.prepare(fs, 30.0, 0.0f); c.rat.prepare(fs, 30.0, 1.0f);
        }
        reset();
    }
    void reset() noexcept override {
        for (auto& c : ch_) {
            for (auto& f : c.tone) f.clear();
            for (auto& f : c.solo) f.clear();
            c.hp.clear(); c.lp.clear(); c.pres.clear(); c.sc.clear(); c.tapeLo.clear(); c.tapeHi.clear(); c.hil.clear();
            c.env = -120; c.gr = 0; c.fb = 0; c.sus = 0; c.toneOn.fill(false); c.toneFade.fill(0); c.hpOn = c.lpOn = c.presOn = c.scOn = false; c.filtFade.fill(0);
            c.key = c.keyF = c.keyS = -1;
        }
        tapeKey_ = -1; loaded_ = false; mtr_ = {}; corr_ = {};
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const bool inOn = p(kInOn) > 0.5f;
        const float gT = inOn ? kit::dbToGain(p(kInGain)) : 1.0f;
        const float bal = inOn ? p(kBalance) : 0.0f;
        const float bLT = kit::dbToGain(-std::max(0.0f, bal)), bRT = kit::dbToGain(std::min(0.0f, bal));
        const int phase = inOn ? static_cast<int>(p(kPhase)) : 0;
        const float phT = phase != 0 ? 1.0f : 0.0f;
        const int tape = inOn ? static_cast<int>(p(kTapeEq)) : 0;
        const float tapeT = tape != 0 ? 1.0f : 0.0f;
        const bool outOn = p(kOutOn) > 0.5f;
        const float wT = outOn ? p(kWidth) * 0.01f : 1.0f;
        const bool ol = p(kOutLink) > 0.5f;
        const float oLT = outOn ? kit::dbToGain(p(kOutL)) : 1.0f, oRT = outOn ? kit::dbToGain(ol ? p(kOutL) : p(kOutR)) : 1.0f;
        if (!loaded_) {
            inG_.cur = gT; balL_.cur = bLT; balR_.cur = bRT; phX_.cur = phT; tapeX_.cur = tapeT; wid_.cur = wT; oL_.cur = oLT; oR_.cur = oRT;
            snap_ = true;
        }
        setTape(tape);
        const int pol = inOn ? static_cast<int>(p(kPolarity)) : 0;
        const bool swap = inOn && p(kSwap) > 0.5f;
        // ---- INPUT ----
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = inG_.next(gT);
            float l = L[i] * g, r = R[i] * g;
            mtr_.in[0] = std::max(mtr_.in[0], std::fabs(l)); mtr_.in[1] = std::max(mtr_.in[1], std::fabs(r));
            const float tx = tapeX_.next(tapeT);
            if (tx > 1e-6f) {
                const float tl = ch_[0].tapeHi.tick(ch_[0].tapeLo.tick(l)), tr = ch_[1].tapeHi.tick(ch_[1].tapeLo.tick(r));
                l += tx * (tl - l); r += tx * (tr - r);
            }
            if (pol == 1 || pol == 3) l = -l;
            if (pol == 2 || pol == 3) r = -r;
            if (swap) std::swap(l, r);
            l *= balL_.next(bLT); r *= balR_.next(bRT);
            const float px = phX_.next(phT);
            if (px > 1e-6f) {   // PHASE: one side turned 90 degrees against the other (both run through matched all-passes)
                float lr, li, rr, ri;
                ch_[0].hil.tick(l, lr, li); ch_[1].hil.tick(r, rr, ri);
                const float pl = phase == 2 ? lr : li, pr = phase == 2 ? ri : rr;
                l += px * (pl - l); r += px * (pr - r);
            }
            L[i] = l; R[i] = r;
        }
        // ---- TONE / DYNAMICS / FILTER ----
        if (p(kToneOn) > 0.5f) runModule(0, L, R, n);
        if (p(kDynOn) > 0.5f) runModule(1, L, R, n); else { ch_[0].gr = ch_[1].gr = 0; }
        if (p(kFiltOn) > 0.5f) runModule(2, L, R, n);
        // ---- OUTPUT ----
        const int mon = outOn ? static_cast<int>(p(kMonitor)) : 1;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            const float w = wid_.next(wT);
            if (!(kit::isFlat(w, 1.0f) && kit::isFlat(wT, 1.0f))) { const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w; l = m + s; r = m - s; }
            l *= oL_.next(oLT); r *= oR_.next(oRT);
            switch (mon) {
                case 0: r = l; break;
                case 2: l = r; break;
                case 3: case 4: { const float m = 0.5f * (l + r); l = r = m; break; }
                case 5: { const float s = 0.5f * (l - r); l = s; r = s; break; }
                default: break;
            }
            mtr_.out[0] = std::max(mtr_.out[0], std::fabs(l)); mtr_.out[1] = std::max(mtr_.out[1], std::fabs(r));
            corr_.lr += static_cast<double>(l) * r; corr_.ll += static_cast<double>(l) * l; corr_.rr += static_cast<double>(r) * r;
            L[i] = kit::sane(l); R[i] = kit::sane(r);
        }
        snap_ = false; loaded_ = true;
        publishAll(n);
    }

private:
    struct Chan {
        std::array<hc::fx::Biquad, 4> tone{}, solo{};
        hc::fx::Biquad hp, lp, pres, sc, tapeLo, tapeHi;
        Quadrature hil;
        std::array<kit::BlockGlide, 8> gl{};   // 4 tone gains, HP, LP, presence freq, presence gain
        hc::fx::Smoothed mix, mk, thr, rat;
        std::array<bool, 4> toneOn{};
        std::array<float, 4> toneFade{};
        std::array<float, 3> filtFade{};
        bool hpOn = false, lpOn = false, presOn = false, scOn = false;
        float env = -120, gr = 0, fb = 0, sus = 0, key = -1, keyF = -1, keyS = -1;
    };
    // which knob set a side uses for module m (0 tone, 1 dynamics, 2 filter)
    [[nodiscard]] int setBase(int m, std::size_t c) const noexcept {
        static constexpr std::array<int, 3> kModeP{kToneMode, kDynMode, kFiltMode}, kLinkP{kToneLink, kDynLink, kFiltLink};
        static constexpr std::array<int, 3> kA{kToneA, kDynA, kFiltA}, kB{kToneB, kDynB, kFiltB};
        const auto mi = static_cast<std::size_t>(m);
        const bool useB = c == 1 && static_cast<int>(p(kModeP[mi])) != 0 && p(kLinkP[mi]) < 0.5f;
        return useB ? kB[mi] : kA[mi];
    }
    static void toMs(float* a, float* b, std::uint32_t n) noexcept { for (std::uint32_t i = 0; i < n; ++i) { const float m = 0.5f * (a[i] + b[i]), s = 0.5f * (a[i] - b[i]); a[i] = m; b[i] = s; } }
    static void fromMs(float* a, float* b, std::uint32_t n) noexcept { for (std::uint32_t i = 0; i < n; ++i) { const float l = a[i] + b[i], r = a[i] - b[i]; a[i] = l; b[i] = r; } }
    float glide(Chan& h, std::size_t k, float target, bool logd) noexcept {
        auto& g = h.gl[k];
        const float t = logd ? std::log(target) : target;
        if (snap_) g.cur = t;
        const float v = g.step(t);
        return logd ? std::exp(v) : v;
    }

    void runModule(int m, float* L, float* R, std::uint32_t n) noexcept {
        static constexpr std::array<int, 3> kModeP{kToneMode, kDynMode, kFiltMode};
        const int mode = static_cast<int>(p(kModeP[static_cast<std::size_t>(m)]));
        if (mode == 2) toMs(L, R, n);
        if (m == 0) tone(L, R, n);
        else if (m == 1) dyn(L, R, n, mode == 0);
        else filter(L, R, n);
        if (mode == 2) fromMs(L, R, n);
    }

    void tone(float* L, float* R, std::uint32_t n) noexcept {
        float* io[2] = {L, R};
        const float inc = static_cast<float>(1.0 / (0.02 * fs_));
        for (std::size_t c = 0; c < 2; ++c) {
            Chan& h = ch_[c];
            const int b0 = setBase(0, c);
            bool anySolo = false;
            for (int b = 0; b < 4; ++b) anySolo = anySolo || (p(b0 + b * kBandP + tSolo) > 0.5f && p(b0 + b * kBandP + tEnable) > 0.5f);
            for (std::uint32_t at = 0; at < n; at += kSub) {
                const std::uint32_t end = std::min(n, at + kSub);
                // coefficients for this stretch
                std::array<float, 4> g{};
                float key = static_cast<float>(b0);
                for (int b = 0; b < 4; ++b) {
                    const int q = b0 + b * kBandP;
                    g[static_cast<std::size_t>(b)] = glide(h, static_cast<std::size_t>(b), p(q + tEnable) > 0.5f ? p(q + tGain) : 0.0f, false);
                    key += (g[static_cast<std::size_t>(b)] * 13.0f + p(q + tFreq) * 3.0f + p(q + tShape) * 7.0f) * static_cast<float>(b + 1);
                }
                if (key != h.key) {
                    h.key = key;
                    for (std::size_t b = 0; b < 4; ++b) {
                        const int q = b0 + static_cast<int>(b) * kBandP;
                        const double hz = std::min<double>(kFreqs[b][static_cast<std::size_t>(p(q + tFreq))], fs_ * 0.45);
                        const int sh = static_cast<int>(p(q + tShape));
                        if (std::fabs(g[b]) > 1e-4f) h.toneOn[b] = true;
                        if (sh == 0) h.tone[b].c = hc::fx::BiquadCoefs::make(b < 2 ? BT::LowShelf : BT::HighShelf, fs_, hz, 0.7071, g[b]);
                        else h.tone[b].c = hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, kShapeQ[static_cast<std::size_t>(sh)], g[b]);
                        h.solo[b].c = hc::fx::BiquadCoefs::make(BT::BandPass, fs_, hz, sh == 0 ? 0.7071 : kShapeQ[static_cast<std::size_t>(sh)]);
                    }
                }
                for (std::uint32_t i = at; i < end; ++i) {
                    float x = io[c][i];
                    const float in = x;
                    for (std::size_t b = 0; b < 4; ++b) {
                        if (!h.toneOn[b]) continue;
                        if (h.toneFade[b] < 1.0f) h.toneFade[b] = std::min(1.0f, h.toneFade[b] + inc);   // a band that starts running fades in
                        x += h.toneFade[b] * (h.tone[b].tick(x) - x);
                    }
                    if (anySolo) {   // SOLO: hear only the soloed bands' region of the module input
                        float s = 0;
                        for (std::size_t b = 0; b < 4; ++b) {
                            const int q = b0 + static_cast<int>(b) * kBandP;
                            const float bp = h.solo[b].tick(in);
                            if (p(q + tSolo) > 0.5f && p(q + tEnable) > 0.5f) s += bp;
                        }
                        x = s;
                    }
                    io[c][i] = x;
                }
            }
        }
    }

    void dyn(float* L, float* R, std::uint32_t n, bool linked) noexcept {
        float* io[2] = {L, R};
        std::array<int, 2> base{setBase(1, 0), setBase(1, 1)};
        std::array<float, 2> att{}, rel{}, knee{}, ratioT{}, thrT{}, mkT{}, mixT{};
        std::array<int, 2> chr{}, relI{};
        std::array<bool, 2> lis{};
        for (std::size_t c = 0; c < 2; ++c) {
            const int b = base[c];
            Chan& h = ch_[c];
            chr[c] = static_cast<int>(p(b + dChar)); relI[c] = static_cast<int>(p(b + dRel));
            att[c] = hc::fx::onePoleCoef(fs_, chr[c] == 0 ? 10.0 : chr[c] == 1 ? 5.0 : 0.5);
            static constexpr std::array<float, 5> kRelS{0.1f, 0.25f, 0.5f, 1.0f, 2.0f};
            rel[c] = relI[c] < 5 ? hc::fx::onePoleCoef(fs_, kRelS[static_cast<std::size_t>(relI[c])] * 1000.0) : 0.0f;
            knee[c] = chr[c] == 0 ? 6.0f : chr[c] == 1 ? 2.0f : 0.0f;
            ratioT[c] = chr[c] == 2 ? 20.0f : p(b + dRatio);
            thrT[c] = -0.3f * p(b + dAmount);
            mkT[c] = kit::dbToGain(p(b + dMakeup));
            mixT[c] = p(b + dMix) * 0.01f;
            lis[c] = p(b + dScListen) > 0.5f;
            // sidechain filter
            const int st = static_cast<int>(p(b + dScType));
            const float f = p(b + dScFreq), gdb = p(b + dScGain), q = p(b + dScQ);
            const float key = f + gdb * 1e5f + q * 3e5f + static_cast<float>(st) * 1e6f;
            if (key != h.keyS) {
                h.keyS = key;
                if ((st == 0 && f > 20.01f) || (st == 1 && std::fabs(gdb) > 1e-4f) || (st == 2 && f < 19999.0f)) h.scOn = true;
                const double hz = std::min<double>(f, fs_ * 0.45);
                h.sc.c = st == 0 ? hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hz, q) : st == 1 ? hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, q, gdb) : hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hz, q);
            }
            if (snap_) { h.mix.cur = mixT[c]; h.mk.cur = mkT[c]; h.thr.cur = thrT[c]; h.rat.cur = ratioT[c]; }
        }
        const float invFs = static_cast<float>(1.0 / fs_);
        float grM0 = 0, grM1 = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            std::array<float, 2> det{}, lv{};
            for (std::size_t c = 0; c < 2; ++c) {
                Chan& h = ch_[c];
                float d = chr[c] == 0 ? h.fb : io[c][i];   // A: the detector hears its own output (feedback)
                if (h.scOn) d = h.sc.tick(d);
                det[c] = d;
                lv[c] = kit::gainToDb(std::fabs(d));
            }
            if (linked) lv[0] = lv[1] = std::max(lv[0], lv[1]);
            for (std::size_t c = 0; c < 2; ++c) {
                Chan& h = ch_[c];
                const float t = h.thr.next(thrT[c]), r = h.rat.next(ratioT[c]);
                float rc = rel[c];
                if (relI[c] == 5) rc = std::exp(-invFs / (0.1f + 1.4f * h.sus));   // 6: auto, longer when it has been working
                h.env = lv[c] + (lv[c] > h.env ? att[c] : rc) * (h.env - lv[c]);
                const float over = h.env - t;
                float gr;
                if (knee[c] > 0 && std::fabs(over) < knee[c] * 0.5f) { const float x = over + knee[c] * 0.5f; gr = (1.0f - 1.0f / r) * x * x / (2.0f * knee[c]); }
                else gr = over > 0 ? over * (1.0f - 1.0f / r) : 0.0f;
                h.sus = std::clamp(h.sus + invFs * (gr > 1.0f ? 0.5f : -0.25f), 0.0f, 1.0f);
                h.gr = gr;
                const float x = io[c][i];
                const float y = x * kit::dbToGain(-gr) * h.mk.next(mkT[c]);
                h.fb = y;
                const float mx = h.mix.next(mixT[c]);
                io[c][i] = lis[c] ? det[c] : x + mx * (y - x);
                (c == 0 ? grM0 : grM1) = std::max(c == 0 ? grM0 : grM1, gr * (mixT[c] > 0.0f ? 1.0f : 0.0f));
            }
        }
        mtr_.gr[0] = std::max(mtr_.gr[0], grM0); mtr_.gr[1] = std::max(mtr_.gr[1], grM1);
    }

    void filter(float* L, float* R, std::uint32_t n) noexcept {
        float* io[2] = {L, R};
        const float inc = static_cast<float>(1.0 / (0.02 * fs_));
        for (std::size_t c = 0; c < 2; ++c) {
            Chan& h = ch_[c];
            const int b = setBase(2, c);
            for (std::uint32_t at = 0; at < n; at += kSub) {
                const std::uint32_t end = std::min(n, at + kSub);
                const float hp = glide(h, 4, p(b + fHp), true), lp = glide(h, 5, p(b + fLp), true), pf = glide(h, 6, p(b + fPresF), true), pg = glide(h, 7, p(b + fPresG), false);
                const float key = hp * 1e5f + lp + pf * 0.37f + pg * 977.0f;
                if (key != h.keyF) {
                    h.keyF = key;
                    if (hp > 20.01f) h.hpOn = true;
                    if (lp < 19999.0f) h.lpOn = true;
                    if (std::fabs(pg) > 1e-4f) h.presOn = true;
                    h.hp.c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 0.7071);
                    h.lp.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, fs_ * 0.45), 0.7071);
                    h.pres.c = hc::fx::BiquadCoefs::make(BT::Bell, fs_, std::min<double>(pf, fs_ * 0.45), 1.2, pg);
                }
                for (std::uint32_t i = at; i < end; ++i) {
                    float x = io[c][i];
                    const std::array<bool, 3> on{h.hpOn, h.presOn, h.lpOn};
                    for (std::size_t k = 0; k < 3; ++k) {
                        if (!on[k]) continue;
                        if (h.filtFade[k] < 1.0f) h.filtFade[k] = std::min(1.0f, h.filtFade[k] + inc);
                        const float y = k == 0 ? h.hp.tick(x) : k == 1 ? h.pres.tick(x) : h.lp.tick(x);
                        x += h.filtFade[k] * (y - x);
                    }
                    io[c][i] = x;
                }
            }
        }
    }

    void setTape(int tape) noexcept {
        const int speed = static_cast<int>(p(kSpeed));
        const float key = static_cast<float>(tape * 3 + speed);
        if (key == tapeKey_) return;
        tapeKey_ = key;
        const float sgn = tape == 2 ? -1.0f : 1.0f;   // IEC->NAB = the reverse of NAB->IEC
        const double hiHz = speed == 1 ? 4500.0 : 2300.0, hiDb = speed == 1 ? 1.5 : -1.5;
        for (auto& c : ch_) {
            c.tapeLo.c = hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, 50.0, 0.7071, -3.0 * sgn);
            c.tapeHi.c = hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min(hiHz, fs_ * 0.4), 0.7071, hiDb * sgn);
        }
    }

    void publishAll(std::uint32_t n) noexcept {
        const int type = static_cast<int>(p(kMeterType));
        // PPM: quick rise, slow fall (about 20 dB in 1.7 s); VU: averaged; CORRELATION: same as VU for the level bars
        const float fall = type == 0 ? std::pow(10.0f, -20.0f / 20.0f * static_cast<float>(n) / (1.7f * static_cast<float>(fs_))) : std::pow(hc::fx::onePoleCoef(fs_, 300.0), static_cast<float>(n));
        for (std::size_t c = 0; c < 2; ++c) {
            disp_.in[c] = type == 0 ? std::max(mtr_.in[c], disp_.in[c] * fall) : mtr_.in[c] * 0.707f + fall * (disp_.in[c] - mtr_.in[c] * 0.707f);
            disp_.out[c] = type == 0 ? std::max(mtr_.out[c], disp_.out[c] * fall) : mtr_.out[c] * 0.707f + fall * (disp_.out[c] - mtr_.out[c] * 0.707f);
            publishMeter(static_cast<int>(c), kit::gainToDb(disp_.in[c]));
            publishMeter(2 + static_cast<int>(c), -mtr_.gr[c]);
            publishMeter(4 + static_cast<int>(c), kit::gainToDb(disp_.out[c]));
        }
        const double den = std::sqrt(corr_.ll * corr_.rr);
        corrV_ = den > 1e-12 ? static_cast<float>(corr_.lr / den) : corrV_;
        publishMeter(6, corrV_);
        publishReduction(-std::max(mtr_.gr[0], mtr_.gr[1]));
        mtr_ = {}; corr_ = {};
    }

    struct Mtr { std::array<float, 2> in{}, out{}, gr{}; };
    struct Corr { double lr = 0, ll = 0, rr = 0; };
    double fs_ = 48000;
    std::array<Chan, 2> ch_{};
    hc::fx::Smoothed inG_, balL_, balR_, wid_, oL_, oR_, phX_, tapeX_;
    Mtr mtr_{}, disp_{};
    Corr corr_{};
    float tapeKey_ = -1, corrV_ = 1;
    bool loaded_ = false, snap_ = true;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeMasterCouncil() { return std::make_unique<MasterCouncil>(); }

} // namespace hem::staging
