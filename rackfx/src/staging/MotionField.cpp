// =============================================================================
//  26 MOTIONFIELD — a chorus -> delay -> reverb chain with a MORPH slider that sweeps any of eight
//  controls at once between their own RANGE MIN and RANGE MAX (or the other way, DIRECTION INVERT).
//  Routing: INPUT > CHORUS > [TO DELAY] > DELAY > [TO REVERB] > REVERB > OUTPUT. Each stage passes
//  its input straight on and adds its own effect: the chorus blends in its moving voices
//  (INTENSITY), the delay adds echoes of what TO DELAY sends it, the reverb adds a tail of what
//  TO REVERB sends it. AUTO lets the input level push the morph slider (SENSITIVITY, ATTACK,
//  RELEASE); GLOBAL REVERSE flips every assigned direction.
//  Morph-able controls: INTENSITY, CHORUS WIDTH, TO DELAY, FEEDBACK, MOD DEPTH, TO REVERB, DECAY,
//  DAMP. An assigned control follows the slider; an unassigned one follows its own knob.
//  Different from STEREO DELAY (48) and REVERB (47): one chained, morphing motion effect.
//  Dead flat on load: INTENSITY, TO DELAY, TO REVERB 0, no assignments, WET / DRY 0 / 100.
//  MANUAL / ASSIGN and RESET PARAM are editor actions (they choose and clear assignments on the
//  faceplate); they do not change the sound by themselves.
//  ASSUMED: chorus TYPE 1 = one voice (12 ms, 0.8 Hz), TYPE 2 = two voices (16 / 23 ms, 0.33 /
//  0.71 Hz); INTENSITY 100 = an even voice / dry blend; AUTO maps -60..0 dBFS onto the slider.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P {
    kCEnable, kCIn, kCType, kIntensity, kCWidth, kToDelay,
    kDEnable, kDIn, kPingPong, kDSync, kDTime, kFeedback, kDHp, kDLp, kModDepth, kModRate, kLrOffset, kModOffset, kToReverb,
    kREnable, kRIn, kSpace, kDecay, kRHp, kRLp, kRPredelay, kDamp, kRWidth,
    kMorphMode, kGlobalReverse, kResetParam, kMorph, kAuto, kSens, kAttack, kRelease,
    kM0,                          // 8 targets x {assign, min, max, direction}
    kMix = kM0 + 32, kOutput, kBypass, kCount
};
enum T { tIntensity, tWidth, tToDelay, tFeedback, tModDepth, tToReverb, tDecay, tDamp, tCount };
constexpr std::array<int, tCount> kTargetKnob{kIntensity, kCWidth, kToDelay, kFeedback, kModDepth, kToReverb, kDecay, kDamp};

const char* const kTypeN[] = {"1", "2"};
const char* const kSpaceN[] = {"HUGE", "PLATE"};
const char* const kModeN[] = {"MANUAL", "ASSIGN"};
const char* const kDirN[] = {"NORMAL", "INVERT"};
#define HEM_MT(id, nm)                                                              \
    {id "_assign", nm " ASSIGN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},              \
    {id "_min", nm " RANGE MIN", "%", 0.0f, 100.0f, 0.0f},                          \
    {id "_max", nm " RANGE MAX", "%", 0.0f, 100.0f, 100.0f},                        \
    {id "_dir", nm " DIRECTION", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kDirN}
const ParamInfo kParams[kCount] = {
    {"chorus_enable", "CHORUS ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"chorus_input", "CHORUS INPUT GAIN", "dB", -24.0f, 12.0f, 0.0f},
    {"chorus_type", "CHORUS TYPE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kTypeN},
    {"intensity", "INTENSITY", "%", 0.0f, 100.0f, 0.0f},
    {"chorus_width", "CHORUS WIDTH", "%", 0.0f, 100.0f, 0.0f},
    {"to_delay", "TO DELAY", "%", 0.0f, 100.0f, 0.0f},
    {"delay_enable", "DELAY ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"delay_input", "DELAY INPUT GAIN", "dB", -24.0f, 12.0f, 0.0f},
    {"ping_pong", "PING PONG", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"delay_sync", "DELAY SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"delay_time", "DELAY TIME", "ms", 1.0f, 2000.0f, 375.0f, Taper::Log},
    {"feedback", "FEEDBACK", "%", 0.0f, 95.0f, 0.0f},
    {"delay_hp", "DELAY HP", "Hz", 20.0f, 2000.0f, 20.0f, Taper::Log},
    {"delay_lp", "DELAY LP", "Hz", 500.0f, 20000.0f, 20000.0f, Taper::Log},
    {"mod_depth", "MOD DEPTH", "%", 0.0f, 100.0f, 0.0f},
    {"mod_rate", "MOD RATE", "Hz", 0.05f, 10.0f, 0.5f, Taper::Log},
    {"lr_offset", "L/R OFFSET", "ms", 0.0f, 100.0f, 0.0f},
    {"mod_offset", "MOD OFFSET", "deg", 0.0f, 180.0f, 0.0f},
    {"to_reverb", "TO REVERB", "%", 0.0f, 100.0f, 0.0f},
    {"reverb_enable", "REVERB ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"reverb_input", "REVERB INPUT GAIN", "dB", -24.0f, 12.0f, 0.0f},
    {"space", "SPACE TYPE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kSpaceN},
    {"decay", "DECAY", "s", 0.2f, 20.0f, 3.0f, Taper::Log},
    {"reverb_hp", "REVERB HP", "Hz", 20.0f, 2000.0f, 20.0f, Taper::Log},
    {"reverb_lp", "REVERB LP", "Hz", 500.0f, 20000.0f, 20000.0f, Taper::Log},
    {"reverb_predelay", "PREDELAY", "ms", 0.0f, 250.0f, 0.0f},
    {"damp", "DAMP", "%", 0.0f, 100.0f, 0.0f},
    {"reverb_width", "REVERB WIDTH", "%", 0.0f, 100.0f, 0.0f},
    {"morph_mode", "MORPH MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN},
    {"global_reverse", "GLOBAL REVERSE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"reset_param", "RESET PARAM", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"morph", "A - B", "%", 0.0f, 100.0f, 0.0f},
    {"auto", "AUTO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"sensitivity", "SENSITIVITY", "%", 0.0f, 100.0f, 0.0f},
    {"attack", "ATTACK", "ms", 1.0f, 500.0f, 20.0f, Taper::Log},
    {"release", "RELEASE", "ms", 10.0f, 2000.0f, 300.0f, Taper::Log},
    HEM_MT("m_intensity", "INTENSITY"), HEM_MT("m_width", "CHORUS WIDTH"), HEM_MT("m_to_delay", "TO DELAY"), HEM_MT("m_feedback", "FEEDBACK"),
    HEM_MT("m_mod_depth", "MOD DEPTH"), HEM_MT("m_to_reverb", "TO REVERB"), HEM_MT("m_decay", "DECAY"), HEM_MT("m_damp", "DAMP"),
    {"mix", "WET / DRY", "%", 0.0f, 100.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_MT
const hc::fx::MeterInfo kMeters[] = {{"OUTPUT", "dBFS", -60, 0}, {"PEAK", "", 0, 1}, {"MORPH POSITION", "%", 0, 100}};

class MotionField final : public hc::fx::Effect {
public:
    MotionField() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i >= kCIn && i <= kToDelay) return p(kCEnable) > 0.5f;
        if (i >= kDIn && i <= kToReverb) return p(kDEnable) > 0.5f;
        if (i >= kRIn && i <= kRWidth) return p(kREnable) > 0.5f;
        if (i == kSens || i == kAttack || i == kRelease) return p(kAuto) > 0.5f;
        if (i >= kM0 && i < kMix && (i - kM0) % 4 != 0) return p(kM0 + ((i - kM0) / 4) * 4) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : cho_) d.prepare(static_cast<std::uint32_t>(fs * 0.05));
        for (auto& d : dly_) d.prepare(static_cast<std::uint32_t>(fs * 2.2));
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(fs * 0.26));
        const auto big = static_cast<std::uint32_t>(fs * 0.2);
        for (auto& r : ring_) { r.ap.prepare(big); r.dl.prepare(big); }
        for (auto& a : diff_) a.prepare(static_cast<std::uint32_t>(fs * 0.02));
        for (auto* s : {&cIn_, &dIn_, &rIn_, &out_}) s->prepare(fs, 20.0, 1.0f);
        for (auto& s : sm_) s.prepare(fs, 30.0, 0.0f);
        mix_.prepare(fs, 20.0, 0.0f);
        dt_.prepare(fs, 80.0, delaySamp(p(kDTime))); off_.prepare(fs, 80.0, 0.0f); preS_.prepare(fs, 80.0, 0.0f);
        outM_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : cho_) d.clear();
        for (auto& d : dly_) d.clear();
        for (auto& d : pre_) d.clear();
        for (auto& r : ring_) { r.ap.clear(); r.dl.clear(); r.lp = 0; }
        for (auto& a : diff_) a.clear();
        for (auto* f : {&dHp_, &dLp_, &rHp_, &rLp_}) f->clear();
        chPh_.fill(0); dPh_ = 0; rPh_ = 0; env_ = 0; pos_ = 0; peak_ = 0; fKey_ = rKey_ = -1; primed_ = dLpOn_ = rLpOn_ = loaded_ = false;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        // ---- morph position: the slider, pushed by the input level when AUTO is on ----
        if (p(kAuto) > 0.5f) {
            const float a = hc::fx::onePoleCoef(fs_, p(kAttack)), r = hc::fx::onePoleCoef(fs_, p(kRelease));
            for (std::uint32_t i = 0; i < n; ++i) {
                const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
                env_ = x + (x > env_ ? a : r) * (env_ - x);
            }
        } else env_ = 0;
        const float envN = std::clamp((kit::gainToDb(env_) + 60.0f) / 60.0f, 0.0f, 1.0f);
        pos_ = std::clamp(p(kMorph) * 0.01f + (p(kAuto) > 0.5f ? p(kSens) * 0.01f * envN : 0.0f), 0.0f, 1.0f);
        std::array<float, tCount> v{};
        for (int t = 0; t < tCount; ++t) v[static_cast<std::size_t>(t)] = target(t);

        const float mixT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOutput));
        if (!loaded_) { mix_.cur = mixT; out_.cur = oT; loaded_ = true; }   // a loaded session starts where it was saved
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        if (!wet) {
            if (!(kit::isFlat(oT, 1.0f) && kit::isFlat(out_.cur, 1.0f))) for (std::uint32_t i = 0; i < n; ++i) { const float o = out_.next(oT); L[i] *= o; R[i] *= o; }
            meter(L, R, n); return;
        }
        const float fs = static_cast<float>(fs_);
        const bool cOn = p(kCEnable) > 0.5f, dOn = p(kDEnable) > 0.5f, rOn = p(kREnable) > 0.5f;
        const float cInT = kit::dbToGain(p(kCIn)), dInT = kit::dbToGain(p(kDIn)), rInT = kit::dbToGain(p(kRIn));
        // chorus
        const bool two = p(kCType) > 0.5f;
        static constexpr std::array<float, 2> kBase2{16.0f, 23.0f}, kRate2{0.33f, 0.71f};
        // delay
        setFilters();
        const float dT = delaySamp(p(kDTime)), offT = p(kLrOffset) * 0.001f * fs;
        const bool pp = p(kPingPong) > 0.5f;
        const float modRate = p(kModRate), modPhR = p(kModOffset) / 360.0f;
        // reverb
        const bool huge = p(kSpace) < 0.5f;
        static constexpr std::array<float, 4> kApH{31, 37, 41, 43}, kDlH{97, 113, 131, 149}, kApP{8, 11, 13, 17}, kDlP{29, 37, 43, 53};
        std::array<float, 4> apL{}, dlL{}, g{};
        const float rt = v[tDecay];
        for (std::size_t k = 0; k < 4; ++k) {
            apL[k] = (huge ? kApH[k] : kApP[k]) * 0.001f * fs;
            dlL[k] = (huge ? kDlH[k] : kDlP[k]) * 0.001f * fs;
            g[k] = std::min(0.98f, std::pow(10.0f, -3.0f * (apL[k] + dlL[k]) / (fs * rt)));
        }
        const float damp = (huge ? 0.1f : 0.0f) + 0.75f * v[tDamp] * 0.01f;
        const float rw = rOn ? p(kRWidth) * 0.01f : 0.0f;
        const float preT = p(kRPredelay) * 0.001f * fs;
        if (!primed_) {   // start at the set values, no glide from the defaults
            dt_.cur = dT; off_.cur = offT; preS_.cur = preT;
            const std::array<float, 6> init{v[tIntensity] * 0.01f, v[tWidth] * 0.01f, v[tToDelay] * 0.01f, v[tFeedback] * 0.01f, v[tModDepth] * 0.01f, v[tToReverb] * 0.01f};
            for (std::size_t k = 0; k < 6; ++k) sm_[k].cur = init[k];
            primed_ = true;
        }
        const bool dLp = dLpOn_, rLp = rLpOn_;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            const float inten = sm_[0].next(v[tIntensity] * 0.01f), cw = sm_[1].next(v[tWidth] * 0.01f), toD = sm_[2].next(v[tToDelay] * 0.01f);
            const float fb = sm_[3].next(v[tFeedback] * 0.01f), md = sm_[4].next(v[tModDepth] * 0.01f), toR = sm_[5].next(v[tToReverb] * 0.01f);
            // ---- CHORUS ----
            float sl = xl, sr = xr;
            if (cOn) {
                const float ci = cIn_.next(cInT);
                sl *= ci; sr *= ci;
                cho_[0].write(sl); cho_[1].write(sr);
                float vl = 0, vr = 0;
                const int voices = two ? 2 : 1;
                for (int k = 0; k < voices; ++k) {
                    const auto kk = static_cast<std::size_t>(k);
                    const float base = (two ? kBase2[kk] : 12.0f) * 0.001f * fs, depth = (two ? 4.0f : 3.0f) * 0.001f * fs * 0.5f;
                    chPh_[kk] += static_cast<double>(two ? kRate2[kk] : 0.8f) / fs_; chPh_[kk] -= std::floor(chPh_[kk]);
                    const float ph = static_cast<float>(chPh_[kk]);
                    vl += cho_[0].read(base + depth * std::sin(kit::kTau * ph));
                    vr += cho_[1].read(base + depth * std::sin(kit::kTau * (ph + 0.5f * cw)));   // WIDTH: right voice up to half a cycle apart
                }
                const float vs = 1.0f / static_cast<float>(voices);
                sl += 0.5f * inten * (vl * vs - sl); sr += 0.5f * inten * (vr * vs - sr);
            }
            // ---- DELAY ----
            if (dOn) {
                const float di = dIn_.next(dInT);
                sl *= di; sr *= di;
                dPh_ += static_cast<double>(modRate) / fs_; dPh_ -= std::floor(dPh_);
                const float dev = md * 0.003f * fs;
                const float t = dt_.next(dT), o = off_.next(offT);
                float el = dly_[0].read(std::max(0.0f, t - 1.0f + dev * std::sin(kit::kTau * static_cast<float>(dPh_))));
                float er = dly_[1].read(std::max(0.0f, t + o - 1.0f + dev * std::sin(kit::kTau * (static_cast<float>(dPh_) + modPhR))));
                el = dHp_.l.tick(el); er = dHp_.r.tick(er);
                if (dLp) { el = dLp_.l.tick(el); er = dLp_.r.tick(er); }
                if (pp) { dly_[0].write(toD * 0.5f * (sl + sr) + fb * er); dly_[1].write(fb * el); }
                else { dly_[0].write(toD * sl + fb * el); dly_[1].write(toD * sr + fb * er); }
                sl += el; sr += er;
            }
            // ---- REVERB ----
            if (rOn) {
                const float ri = rIn_.next(rInT);
                sl *= ri; sr *= ri;
                const float pd = preS_.next(preT);
                pre_[0].write(toR * sl); pre_[1].write(toR * sr);
                float il = pre_[0].read(pd), ir = pre_[1].read(pd);
                if (!huge) { il = diff_[1].tick(diff_[0].tick(il, 0.7f, 0.0047f * fs), 0.7f, 0.0031f * fs); ir = diff_[3].tick(diff_[2].tick(ir, 0.7f, 0.0053f * fs), 0.7f, 0.0037f * fs); }
                rPh_ += 0.3 / fs_; rPh_ -= std::floor(rPh_);
                const float wob = huge ? 0.0008f * fs * std::sin(kit::kTau * static_cast<float>(rPh_)) : 0.0f;
                // a ring of four allpass + delay stages; left enters stage 0, right enters stage 2
                float x = ring_[3].lastOut;
                std::array<float, 4> outs{};
                for (std::size_t k = 0; k < 4; ++k) {
                    if (k == 0) x += il;
                    if (k == 2) x += ir;
                    outs[k] = ring_[k].tick(x, apL[k] + (k % 2 ? -wob : wob), dlL[k], damp, g[k]);
                    x = outs[k];
                }
                float tl = 0.6f * (outs[0] - outs[2]), tr = 0.6f * (outs[1] - outs[3]);
                tl = rHp_.l.tick(tl); tr = rHp_.r.tick(tr);
                if (rLp) { tl = rLp_.l.tick(tl); tr = rLp_.r.tick(tr); }
                const float m = 0.5f * (tl + tr), s = 0.5f * (tl - tr) * rw;   // WIDTH 0 = mono tail
                sl += m + s; sr += m - s;
            }
            const float mx = mix_.next(mixT), o = out_.next(oT);
            L[i] = kit::sane((xl + mx * (sl - xl)) * o);
            R[i] = kit::sane((xr + mx * (sr - xr)) * o);
        }
        meter(L, R, n);
    }

private:
    struct Stage {
        hc::fx::DelayLine ap, dl;
        float lp = 0, lastOut = 0;
        float tick(float x, float apLen, float dlLen, float damp, float g) noexcept {
            const float v = ap.read(std::max(0.0f, apLen - 1.0f));
            const float w = x + 0.6f * v;
            ap.write(w);
            const float a = v - 0.6f * w;
            const float y = dl.read(std::max(0.0f, dlLen - 1.0f));
            dl.write(a);
            lp = y + damp * (lp - y);
            lastOut = lp * g;
            return lastOut;
        }
    };
    struct Ap {
        hc::fx::DelayLine d;
        void prepare(std::uint32_t m) { d.prepare(m + 4); }
        void clear() noexcept { d.clear(); }
        float tick(float x, float g, float len) noexcept { const float v = d.read(std::max(0.0f, len - 1.0f)); const float w = x + g * v; d.write(w); return v - g * w; }
    };

    // effective value of a morph target: its knob, or (assigned) a point between RANGE MIN and MAX
    [[nodiscard]] float target(int t) const noexcept {
        const int knob = kTargetKnob[static_cast<std::size_t>(t)];
        const auto& info = kParams[knob];
        const int b = kM0 + 4 * t;
        if (p(b) < 0.5f) return p(knob);
        const bool inv = (p(b + 3) > 0.5f) != (p(kGlobalReverse) > 0.5f);
        const float f = inv ? 1.0f - pos_ : pos_;
        const float u = (p(b + 1) + f * (p(b + 2) - p(b + 1))) * 0.01f;
        if (info.taper == Taper::Log) return info.min * std::pow(info.max / info.min, u);
        return info.min + u * (info.max - info.min);
    }
    [[nodiscard]] float delaySamp(float ms) const noexcept {
        if (p(kDSync) > 0.5f) { const float six = 15000.0f / std::max(tempo(), 20.0f); ms = std::max(six, std::round(ms / six) * six); }
        return std::clamp(static_cast<float>(fs_) * ms * 0.001f, 1.0f, static_cast<float>(fs_ * 2.0));
    }
    void setFilters() noexcept {
        const double ny = fs_ * 0.45;
        const float k = p(kDHp) + p(kDLp) * 3.0f;
        if (k != fKey_) {
            fKey_ = k;
            if (p(kDLp) < 19999.0f) dLpOn_ = true;   // 20 kHz = off; once moved, keep running
            dHp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kDHp), 0.7071));
            dLp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(p(kDLp), ny), 0.7071));
        }
        const float k2 = p(kRHp) + p(kRLp) * 3.0f;
        if (k2 != rKey_) {
            rKey_ = k2;
            if (p(kRLp) < 19999.0f) rLpOn_ = true;
            rHp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kRHp), 0.7071));
            rLp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(p(kRLp), ny), 0.7071));
        }
    }
    void meter(const float* L, const float* R, std::uint32_t n) noexcept {
        outM_.feed(L, n); outM_.feed(R, n);
        peak_ = std::max(0.0f, peak_ - static_cast<float>(n));
        for (std::uint32_t i = 0; i < n; ++i) if (std::fabs(L[i]) >= 1.0f || std::fabs(R[i]) >= 1.0f) peak_ = static_cast<float>(fs_);
        publishMeter(0, outM_.db()); publishMeter(1, peak_ > 0 ? 1.0f : 0.0f); publishMeter(2, pos_ * 100.0f);
    }

    double fs_ = 48000;
    std::array<hc::fx::DelayLine, 2> cho_{}, dly_{}, pre_{};
    std::array<Stage, 4> ring_{};
    std::array<Ap, 4> diff_{};
    hc::fx::Biquad2 dHp_, dLp_, rHp_, rLp_;
    hc::fx::Smoothed cIn_, dIn_, rIn_, out_, mix_, dt_, off_, preS_;
    std::array<hc::fx::Smoothed, 6> sm_{};
    std::array<double, 2> chPh_{};
    double dPh_ = 0, rPh_ = 0;
    float env_ = 0, pos_ = 0, peak_ = 0, fKey_ = -1, rKey_ = -1;
    bool primed_ = false, dLpOn_ = false, rLpOn_ = false, loaded_ = false;
    kit::PeakMeter outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeMotionField() { return std::make_unique<MotionField>(); }

} // namespace hem::staging
