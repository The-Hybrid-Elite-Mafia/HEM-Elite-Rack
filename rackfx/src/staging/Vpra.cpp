// =============================================================================
//  02 V.P.R.A. — Vocal Presence & Resonance Architect. One screen for a vocal's tone:
//  fixed vocal-voiced tone knobs, one resonance band (static or dynamic), one de-resonator
//  that only cuts when its spot rings louder than the voice around it, saturation, stereo image.
//  No learning (that is TONECOMPASS's job, dropped from the roster) and no free EQ nodes.
//  Dead flat on load: low cut at 20 Hz = off, every tone knob 0 dB, resonance 0 dB, de-resonate OFF,
//  saturation mix 0 %, width 100 %, centre 0, output 0 dB.
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
    kLowCut, kBody, kMud, kPresence, kBright, kAir,
    kResMode, kResFreq, kResQ, kResAmt,
    kDeMode, kDeFreq, kDeQ, kDeAmt,
    kSatMode, kSatDrive, kSatMix,
    kFocus, kStereoMode, kWidth, kCenter,
    kProcMode, kOutput, kAnalyzer, kMapView, kBypass, kCount
};
const char* const kResModes[] = {"NARROW", "DYNAMIC", "BROAD"};
const char* const kDeModes[] = {"OFF", "INTELLIGENT", "AGGRESSIVE"};
const char* const kSatModes[] = {"CLEAN", "WARM", "RICH"};
const char* const kFocusN[] = {"VOCAL", "MUSIC", "CUSTOM"};
const char* const kStereoN[] = {"MONO", "NATURAL", "WIDE"};
const char* const kProcN[] = {"NATURAL", "MODERN", "AGGRESSIVE"};
const char* const kAnaN[] = {"LIVE", "AVG", "PEAK"};
const char* const kMapN[] = {"BODY", "PRESENCE", "AIR"};

const ParamInfo kParams[kCount] = {
    {"low_cut", "LOW CUT", "Hz", 20.0f, 300.0f, 20.0f, Taper::Log},
    {"body", "BODY", "dB", -12.0f, 12.0f, 0.0f},
    {"mud", "MUD CONTROL", "dB", -12.0f, 12.0f, 0.0f},
    {"presence", "PRESENCE", "dB", -12.0f, 12.0f, 0.0f},
    {"brightness", "BRIGHTNESS", "dB", -12.0f, 12.0f, 0.0f},
    {"air", "AIR", "dB", -12.0f, 12.0f, 0.0f},
    {"res_mode", "RESONANCE MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kResModes},
    {"res_freq", "RESONANCE FREQ", "Hz", 100.0f, 10000.0f, 1000.0f, Taper::Log},
    {"res_q", "RESONANCE Q", "", 0.5f, 10.0f, 2.0f, Taper::Log},
    {"res_amount", "RESONANCE AMOUNT", "dB", -12.0f, 12.0f, 0.0f},
    {"deres_mode", "DE-RESONATE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kDeModes},
    {"deres_freq", "DE-RESONATE FREQ", "Hz", 100.0f, 10000.0f, 3000.0f, Taper::Log},
    {"deres_q", "DE-RESONATE Q", "", 0.5f, 10.0f, 4.0f, Taper::Log},
    {"deres_amount", "DE-RESONATE AMOUNT", "%", 0.0f, 100.0f, 0.0f},
    {"sat_mode", "SATURATION", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kSatModes},
    {"sat_drive", "DRIVE", "", 0.0f, 100.0f, 0.0f},
    {"sat_mix", "SATURATION MIX", "%", 0.0f, 100.0f, 0.0f},
    {"eq_focus", "EQ FOCUS", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kFocusN},
    {"stereo_mode", "STEREO IMAGE", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kStereoN},
    {"width", "WIDTH", "%", 0.0f, 200.0f, 100.0f},
    {"center", "CENTER", "", -100.0f, 100.0f, 0.0f},
    {"proc_mode", "PROCESSING MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kProcN},
    {"output", "OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"analyzer", "ANALYZER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kAnaN},
    {"map_view", "RESONANCE MAP", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kMapN},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};

const hc::fx::MeterInfo kMeters[] = {
    {"IN L", "dB", -60, 0}, {"IN R", "dB", -60, 0}, {"OUT L", "dB", -60, 0}, {"OUT R", "dB", -60, 0},
    {"BODY", "dB", -60, 0}, {"PRESENCE", "dB", -60, 0}, {"AIR", "dB", -60, 0},
    {"RESONANCE MOVE", "dB", -12, 12, true}, {"DE-RESONATE", "dB", -18, 0}, {"OUTPUT", "dB", -48, 0},
};

// voicing of the fixed tone knobs (ASSUMED centres; card: body 200 Hz shelf, mud 350 Hz, presence 3 kHz,
// brightness 6 kHz shelf, air 12 kHz shelf)
constexpr float kBodyHz = 200.0f, kMudHz = 350.0f, kPresHz = 3000.0f, kBrightHz = 6000.0f, kAirHz = 12000.0f;

class Vpra final : public hc::fx::Effect {
public:
    Vpra() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kDeFreq || i == kDeQ || i == kDeAmt) return p(kDeMode) > 0.5f;
        if (i == kSatMode || i == kSatDrive) return p(kSatMix) > 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto* m : {&inL_, &inR_, &outL_, &outR_, &outAll_}) m->prepare(fs);
        const std::array<int, 5> tk{kBody, kMud, kPresence, kBright, kAir};
        for (std::size_t i = 0; i < 5; ++i) toneG_[i].prepare(fs, 32, 40.0, p(tk[i]));   // 40 ms: smaller coefficient steps
        resG_.prepare(fs, 32, 20.0, 0.0f);
        outG_.prepare(fs, 20.0, kit::dbToGain(p(kOutput)));
        reset();
    }
    void reset() noexcept override {
        for (auto& f : tone_) f.clear();
        toneOn_.fill(false);
        lowCut_.clear(); res_.clear(); de_.clear();
        resDet_.clear(); deDet_.clear(); refDetLo_.clear(); refDetHi_.clear();
        resEnv_ = {}; resRef_ = {}; deEnv_ = {}; deRef_ = {};
        bodyE_ = presE_ = airE_ = 0; bodyF_.clear(); presF_.clear(); airF_.clear();
        resDyn_ = deGr_ = 0; keys_.fill(1e9f); width_.prepare(fs_, 30.0, p(kWidth) * 0.01f);
        satDc_ = 0;
        bodyF_.set(fs_, 200.0, 1.0); presF_.set(fs_, 3000.0, 1.0); airF_.set(fs_, 10000.0, 0.7071);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inL_.feed(L, n); inR_.feed(R, n);
        if (p(kBypass) > 0.5f) { finish(L, R, n); return; }
        constexpr std::uint32_t kSub = 32;
        for (std::uint32_t at = 0; at < n; at += kSub) block(L + at, R + at, std::min(kSub, n - at));
        finish(L, R, n);
    }

private:
    // timing of the dynamic parts, set by PROCESSING MODE (ASSUMED: natural 20/200, modern 5/80, aggressive 1/40 ms)
    void times(float& att, float& rel) const noexcept {
        switch (static_cast<int>(p(kProcMode))) {
        case 1: att = 5; rel = 80; break;
        case 2: att = 1; rel = 40; break;
        default: att = 20; rel = 200; break;
        }
    }
    // the reference band the de-resonator and the dynamic resonance band compare against (EQ FOCUS)
    void focusBand(float& lo, float& hi) const noexcept {
        switch (static_cast<int>(p(kFocus))) {
        case 1: lo = 40; hi = 16000; break;                                         // whole music range
        case 2: lo = p(kResFreq) * 0.5f; hi = std::min(p(kResFreq) * 2.0f, 18000.0f); break;   // around your own spot
        default: lo = 200; hi = 8000; break;                                        // the voice
        }
    }

    bool setIf(int k, float v) noexcept { if (keys_[static_cast<std::size_t>(k)] == v) return false; keys_[static_cast<std::size_t>(k)] = v; return true; }

    void block(float* l, float* r, std::uint32_t m) noexcept {
        // ---- low cut (20 Hz = off) -----------------------------------------------------------
        if (p(kLowCut) > 20.5f) {
            if (setIf(0, p(kLowCut))) lowCut_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kLowCut), 0.7071));
            lowCut_.run(l, r, m);
        }
        // ---- tone knobs (a flat knob is skipped) --------------------------------------------
        const std::array<float, 5> g{toneG_[0].step(p(kBody)), toneG_[1].step(p(kMud)), toneG_[2].step(p(kPresence)), toneG_[3].step(p(kBright)), toneG_[4].step(p(kAir))};
        for (std::size_t i = 0; i < 5; ++i) {
            if (kit::isFlat(g[i]) && !toneOn_[i]) continue;   // never used yet: skip; once used it keeps running (exact at 0 dB)
            toneOn_[i] = true;
            if (setIf(1 + static_cast<int>(i), g[i])) {
                BT t = BT::Bell; float hz = 0, q = 0.7f;
                switch (i) {
                case 0: t = BT::LowShelf; hz = kBodyHz; q = 0.7f; break;
                case 1: t = BT::Bell; hz = kMudHz; q = 1.2f; break;
                case 2: t = BT::Bell; hz = kPresHz; q = 0.9f; break;
                case 3: t = BT::HighShelf; hz = kBrightHz; q = 0.7f; break;
                default: t = BT::HighShelf; hz = kAirHz; q = 0.6f; break;
                }
                tone_[i].set(hc::fx::BiquadCoefs::make(t, fs_, hz, q, g[i]));
            }
            tone_[i].run(l, r, m);
        }

        float att = 20, rel = 200; times(att, rel);
        float flo = 200, fhi = 8000; focusBand(flo, fhi);
        refDetLo_.set(fs_, flo, 0.7071); refDetHi_.set(fs_, fhi, 0.7071);

        // ---- detectors: resonance spot, de-resonate spot and the reference band -----------
        const int resMode = static_cast<int>(p(kResMode));
        const int deMode = static_cast<int>(p(kDeMode));
        resDet_.set(fs_, p(kResFreq), p(kResQ));
        deDet_.set(fs_, p(kDeFreq), p(kDeQ));
        resEnv_.set(fs_, att, rel); resRef_.set(fs_, att * 4, rel * 2);
        deEnv_.set(fs_, att, rel); deRef_.set(fs_, att * 4, rel * 2);
        for (std::uint32_t i = 0; i < m; ++i) {
            const float x = 0.5f * (l[i] + r[i]);
            const float ref = refDetHi_.low(refDetLo_.high(x));
            (void)resEnv_.tick(std::fabs(resDet_.band(x)));
            (void)deEnv_.tick(std::fabs(deDet_.band(x)));
            (void)resRef_.tick(std::fabs(ref));
            (void)deRef_.tick(std::fabs(ref));
            // band energies for the RESONANCE MAP and analyzer (ballistics chosen by ANALYZER)
            bodyE_ = meterStep(bodyE_, std::fabs(bodyF_.band(x)));
            presE_ = meterStep(presE_, std::fabs(presF_.band(x)));
            airE_ = meterStep(airE_, std::fabs(airF_.high(x)));
        }

        // ---- resonance band ----------------------------------------------------------------
        const float amt = p(kResAmt);
        float resGain = 0.0f;
        if (!kit::isFlat(amt)) {
            if (resMode == 1) {   // DYNAMIC: moves by AMOUNT only while the spot pokes out of the reference band
                const float poke = kit::gainToDb(resEnv_.env) - kit::gainToDb(resRef_.env) + 6.0f;   // ASSUMED 6 dB allowance
                const float k = std::clamp(poke / 6.0f, 0.0f, 1.0f);
                resGain = amt * k;
            } else {
                resGain = amt;
            }
        }
        resGain = resG_.step(resGain);   // glides: no click when the amount jumps
        resDyn_ = resGain;
        if (!kit::isFlat(resGain)) {
            const float q = resMode == 0 ? p(kResQ) * 2.0f : resMode == 2 ? p(kResQ) * 0.5f : p(kResQ);
            if (setIf(6, resGain) | setIf(7, q) | setIf(8, p(kResFreq))) res_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, p(kResFreq), q, resGain));
            res_.run(l, r, m);
        }

        // ---- de-resonator: cuts only when its spot rings over the voice around it ----------
        float cut = 0.0f;
        if (deMode > 0 && p(kDeAmt) > 0.0f) {
            const float maxCut = deMode == 2 ? 18.0f : 12.0f;            // ASSUMED limits
            const float allowance = deMode == 2 ? 0.0f : 3.0f;           // aggressive reacts sooner
            const float poke = kit::gainToDb(deEnv_.env) - kit::gainToDb(deRef_.env) - allowance;
            cut = -std::clamp(poke * (deMode == 2 ? 1.0f : 0.6f), 0.0f, maxCut) * p(kDeAmt) * 0.01f;
        }
        deGr_ = cut;
        if (cut < -0.01f) {
            if (setIf(9, cut) | setIf(10, p(kDeFreq)) | setIf(11, p(kDeQ))) de_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, p(kDeFreq), p(kDeQ), cut));
            de_.run(l, r, m);
        } else if (keys_[9] != 1e9f) { de_.clear(); keys_[9] = 1e9f; }

        // ---- saturation (parallel, mix 0 % = untouched) ------------------------------------
        const float mix = p(kSatMix) * 0.01f;
        if (mix > 0.0f) {
            const float drive = 1.0f + p(kSatDrive) * 0.19f;   // up to about +26 dB into the curve
            const int mode = static_cast<int>(p(kSatMode));
            for (std::uint32_t i = 0; i < m; ++i) {
                for (float* s : {l + i, r + i}) {
                    const float x = *s * drive;
                    float y = 0;
                    if (mode == 0) y = std::tanh(x);                                         // CLEAN: odd, symmetric
                    else if (mode == 1) y = std::tanh(x + 0.25f * x * x) - std::tanh(0.0f); // WARM: adds even harmonics
                    else y = 0.7f * std::tanh(x) + 0.3f * std::tanh(3.0f * x);             // RICH: denser upper harmonics
                    y /= drive > 1.0f ? std::tanh(drive) : 1.0f;                            // level-matched at full scale
                    *s = *s + mix * (y - *s);
                }
            }
            if (static_cast<int>(p(kSatMode)) == 1) {   // remove the DC the even curve makes
                for (std::uint32_t i = 0; i < m; ++i) {
                    satDc_ += 0.0005f * (0.5f * (l[i] + r[i]) - satDc_);
                    l[i] -= satDc_ * mix; r[i] -= satDc_ * mix;
                }
            }
        }

        // ---- stereo image -------------------------------------------------------------------
        const int sm = static_cast<int>(p(kStereoMode));
        const float wTarget = sm == 0 ? 0.0f : sm == 2 ? std::min(2.0f, p(kWidth) * 0.015f) : p(kWidth) * 0.01f;   // WIDE = 1.5x
        const float c = p(kCenter) * 0.01f;
        if (!(kit::isFlat(wTarget, 1.0f) && kit::isFlat(width_.cur, 1.0f) && kit::isFlat(c))) {
            for (std::uint32_t i = 0; i < m; ++i) {
                const float w = width_.next(wTarget);
                float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
                // CENTER moves the middle of the image left / right (constant-power)
                const float a = (c + 1.0f) * 0.25f * kit::kPi;
                const float ml = mid * std::cos(a) * 1.41421356f, mr = mid * std::sin(a) * 1.41421356f;
                l[i] = ml + side; r[i] = mr - side;
            }
        }

        // ---- output -------------------------------------------------------------------------
        const float ogT = kit::dbToGain(p(kOutput));
        for (std::uint32_t i = 0; i < m; ++i) { const float og = outG_.next(ogT); l[i] = kit::sane(l[i] * og); r[i] = kit::sane(r[i] * og); }
    }

    float meterStep(float e, float x) noexcept {
        const int mode = static_cast<int>(p(kAnalyzer));
        const float a = mode == 0 ? 0.99f : mode == 1 ? 0.9995f : 0.99995f;   // LIVE fast, AVG slow, PEAK holds
        if (mode == 2) return x > e ? x : e * a;
        return x + a * (e - x);
    }

    void finish(float* L, float* R, std::uint32_t n) noexcept {
        outL_.feed(L, n); outR_.feed(R, n); outAll_.feed(L, n);
        publishMeter(0, inL_.db()); publishMeter(1, inR_.db());
        publishMeter(2, outL_.db()); publishMeter(3, outR_.db());
        publishMeter(4, kit::gainToDb(bodyE_)); publishMeter(5, kit::gainToDb(presE_)); publishMeter(6, kit::gainToDb(airE_));
        publishMeter(7, resDyn_); publishMeter(8, deGr_); publishMeter(9, outAll_.db());
        publishReduction(deGr_);
    }

    double fs_ = 48000;
    hc::fx::Biquad2 lowCut_, res_, de_;
    std::array<hc::fx::Biquad2, 5> tone_{};
    kit::Svf resDet_, deDet_, refDetLo_, refDetHi_;
    kit::Svf bodyF_, presF_, airF_;
    hc::fx::Envelope resEnv_, resRef_, deEnv_, deRef_;
    hc::fx::Smoothed width_, outG_;
    std::array<kit::BlockGlide, 5> toneG_{};
    std::array<bool, 5> toneOn_{};
    kit::BlockGlide resG_;
    float bodyE_ = 0, presE_ = 0, airE_ = 0, resDyn_ = 0, deGr_ = 0, satDc_ = 0;
    std::array<float, 12> keys_{};
    kit::PeakMeter inL_, inR_, outL_, outR_, outAll_;

};
} // namespace

std::unique_ptr<hc::fx::Effect> makeVpra() { return std::make_unique<Vpra>(); }

} // namespace hem::staging
