// =============================================================================
//  35 IMPACTCROWN — a one-screen "make it hit harder" tool. INPUT gain with a sensitivity readout,
//  then four enhancers, each with its own ENABLE:
//    LOW      lifts and thickens the low end at the chosen NOTE (C1..C3) and adds its harmonics so
//             it reads on small speakers
//    BODY     fills the low mids (around 220 Hz) with a little even-harmonic warmth
//    ENHANCE  a presence exciter (harmonics above 2 kHz), with FOCUS (pulls the centre forward),
//             DYNAMIC PUNCH (lifts each hit's attack) and STEREO IMAGE (widens the sides above 300 Hz)
//    HIGH     air: a high shelf from 10 kHz plus a touch of top harmonics
//  FINAL STAGE: DRIVE GAIN into CLIP (a 0 dBFS clipper whose soft knee widens with the drive) or
//  LIMIT (a zero-latency peak limiter at 0 dBFS), then OUTPUT ATTENUATION; GR / clip and output
//  peak meters (with reset).
//  Different from PEAKCITADEL (34, a pure limiter) and AIREMBER (39, an exciter only).
//  Dead flat on load: every amount 0, DRIVE 0 dB (the clipper is a plain 0 dBFS ceiling, so normal
//  audio is untouched), OUTPUT 0 dB.
//  ASSUMED: LOW up to +12 dB at the note (Q 1.5) plus harmonics; BODY up to +6 dB; ENHANCE harmonics
//  up to 25 %; FOCUS +-4 dB mid / side at 2.5 kHz; PUNCH up to +6 dB on attacks; IMAGE up to +6 dB of
//  side above 300 Hz; HIGH up to +8 dB; clip knee = the drive, at most 6 dB; LIMIT release 80 ms;
//  sensitivity LOW / MED / HIGH = input below -24 / below -12 / above -12 dBFS RMS.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kIn, kLowOn, kLow, kNote, kBodyOn, kBody, kEnhOn, kEnh, kFocus, kPunch, kImage, kHighOn, kHigh, kDrive, kClipLim, kOutAtt, kPeakReset, kBypass, kCount };
const char* const kNoteN[] = {"C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1",
                              "C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2", "C3"};
const char* const kClN[] = {"CLIP", "LIMIT"};
const ParamInfo kParams[kCount] = {
    {"input_gain", "INPUT GAIN", "dB", -24.0f, 24.0f, 0.0f},
    {"low_enable", "LOW ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"low", "LOW", "", 0.0f, 100.0f, 0.0f},
    {"note", "NOTE", "", 0.0f, 24.0f, 7.0f, Taper::Choice, kNoteN},
    {"body_enable", "BODY ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"body", "BODY", "", 0.0f, 100.0f, 0.0f},
    {"enhance_enable", "ENHANCE ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"enhance", "ENHANCE", "", 0.0f, 100.0f, 0.0f},
    {"focus", "FOCUS", "", 0.0f, 100.0f, 0.0f},
    {"dynamic_punch", "DYNAMIC PUNCH", "", 0.0f, 100.0f, 0.0f},
    {"stereo_image", "STEREO IMAGE", "", 0.0f, 100.0f, 0.0f},
    {"high_enable", "HIGH ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"high", "HIGH", "", 0.0f, 100.0f, 0.0f},
    {"drive_gain", "DRIVE GAIN", "dB", 0.0f, 24.0f, 0.0f},
    {"clip_limit", "CLIP / LIMIT", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kClN},
    {"output_attenuation", "OUTPUT ATTENUATION", "dB", -24.0f, 0.0f, 0.0f},
    {"peak_reset", "OUTPUT PEAK RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "dBFS", -60, 0}, {"SENSITIVITY", "", 0, 2}, {"GAIN REDUCTION / CLIPPING", "dB", -24, 0}, {"OUTPUT PEAK", "dBFS", -60, 6}};

class ImpactCrown final : public hc::fx::Effect {
public:
    ImpactCrown() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kLow || i == kNote) return p(kLowOn) > 0.5f;
        if (i == kBody) return p(kBodyOn) > 0.5f;
        if (i >= kEnh && i <= kImage) return p(kEnhOn) > 0.5f;
        if (i == kHigh) return p(kHighOn) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        in_.prepare(fs, 20.0, 1.0f); drv_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f);
        for (auto& s : amt_) s.prepare(fs, 20.0, 0.0f);
        const double ny = fs * 0.45;
        auto set = [&](hc::fx::Biquad2& f, hc::fx::BiquadCoefs c) { f.set(c); };
        set(bodyBp_, hc::fx::BiquadCoefs::make(BT::BandPass, fs, 220.0, 0.7));
        set(bodyLp_, hc::fx::BiquadCoefs::make(BT::LowPass, fs, 400.0, 0.7071));
        set(enhHp_, hc::fx::BiquadCoefs::make(BT::HighPass, fs, 2000.0, 0.7071));
        set(enhHp2_, hc::fx::BiquadCoefs::make(BT::HighPass, fs, 3000.0, 0.7071));
        set(focBp_, hc::fx::BiquadCoefs::make(BT::BandPass, fs, std::min(2500.0, ny), 0.8));
        set(imgHp_, hc::fx::BiquadCoefs::make(BT::HighPass, fs, 300.0, 0.7071));
        set(highHp_, hc::fx::BiquadCoefs::make(BT::HighPass, fs, std::min(10000.0, ny), 0.7071));
        set(airHp_, hc::fx::BiquadCoefs::make(BT::HighPass, fs, std::min(12000.0, ny), 0.7071));
        noteKey_ = -1;
        reset();
    }
    void reset() noexcept override {
        for (auto* f : {&lowBp_, &lowLp_, &lowHp_, &bodyBp_, &bodyLp_, &enhHp_, &enhHp2_, &focBp_, &imgHp_, &highHp_, &airHp_}) f->clear();
        dc_.fill(0); fast_ = slow_ = 0; limG_ = 1; grM_ = 0; pk_ = 0; inRms_ = 0; loaded_ = false;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kPeakReset) > 0.5f) pk_ = 0;
        if (p(kBypass) > 0.5f) { publishAll(); return; }
        const int note = static_cast<int>(p(kNote));
        if (static_cast<float>(note) != noteKey_) {
            noteKey_ = static_cast<float>(note);
            const double hz = 32.7032 * std::pow(2.0, note / 12.0);
            lowBp_.set(hc::fx::BiquadCoefs::make(BT::BandPass, fs_, hz, 1.5));
            lowLp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hz * 1.5, 0.7071));
            lowHp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hz * 1.6, 0.7071));
        }
        const float inT = kit::dbToGain(p(kIn)), dT = kit::dbToGain(p(kDrive)), oT = kit::dbToGain(p(kOutAtt));
        // amounts are 0 when their section is off, so a switch fades over 20 ms
        const std::array<float, 8> aT{
            p(kLowOn) > 0.5f ? p(kLow) * 0.01f : 0.0f, p(kBodyOn) > 0.5f ? p(kBody) * 0.01f : 0.0f,
            p(kEnhOn) > 0.5f ? p(kEnh) * 0.01f : 0.0f, p(kEnhOn) > 0.5f ? p(kFocus) * 0.01f : 0.0f,
            p(kEnhOn) > 0.5f ? p(kPunch) * 0.01f : 0.0f, p(kEnhOn) > 0.5f ? p(kImage) * 0.01f : 0.0f,
            p(kHighOn) > 0.5f ? p(kHigh) * 0.01f : 0.0f, 0.0f};
        if (!loaded_) { in_.cur = inT; drv_.cur = dT; out_.cur = oT; for (std::size_t k = 0; k < 8; ++k) amt_[k].cur = aT[k]; loaded_ = true; }
        const bool limit = p(kClipLim) > 0.5f;
        const float knee = std::min(p(kDrive), 6.0f);
        const float kneeLin = kit::dbToGain(-knee);
        const float fA = hc::fx::onePoleCoef(fs_, 1.0), fR = hc::fx::onePoleCoef(fs_, 40.0), sA = hc::fx::onePoleCoef(fs_, 25.0), sR = hc::fx::onePoleCoef(fs_, 300.0);
        const float limRel = hc::fx::onePoleCoef(fs_, 80.0), rmsC = hc::fx::onePoleCoef(fs_, 300.0);
        grM_ = 0;
        float inPk = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(inT);
            float l = L[i] * g, r = R[i] * g;
            inPk = std::max(inPk, std::max(std::fabs(l), std::fabs(r)));
            const float e2 = 0.5f * (l * l + r * r);
            inRms_ = e2 + rmsC * (inRms_ - e2);
            std::array<float, 8> a{};
            for (std::size_t k = 0; k < 7; ++k) a[k] = amt_[k].next(aT[k]);
            std::array<float, 2> x{l, r};
            for (std::size_t c = 0; c < 2; ++c) {
                float v = x[c];
                if (a[0] > 1e-6f) {   // LOW: a lift at the note plus its harmonics
                    const float bp = c == 0 ? lowBp_.l.tick(v) : lowBp_.r.tick(v);
                    const float lo = c == 0 ? lowLp_.l.tick(v) : lowLp_.r.tick(v);
                    float h = std::tanh(lo * 4.0f) * 0.25f - lo;   // the harmonics the curve adds
                    h = c == 0 ? lowHp_.l.tick(h) : lowHp_.r.tick(h);
                    v += (kit::dbToGain(12.0f * a[0]) - 1.0f) * bp + 0.3f * a[0] * h;
                }
                if (a[1] > 1e-6f) {   // BODY: low mids up to +6 dB with a little even warmth
                    const float bp = c == 0 ? bodyBp_.l.tick(v) : bodyBp_.r.tick(v);
                    const float lo = c == 0 ? bodyLp_.l.tick(v) : bodyLp_.r.tick(v);
                    v += (kit::dbToGain(6.0f * a[1]) - 1.0f) * bp + 0.15f * a[1] * lo * lo;
                    dc_[c] += 0.0005f * (v - dc_[c]);
                    v -= dc_[c] * a[1];
                }
                if (a[2] > 1e-6f) {   // ENHANCE: harmonics generated from the presence band, added back above 3 kHz
                    const float hp = c == 0 ? enhHp_.l.tick(v) : enhHp_.r.tick(v);
                    float h = std::tanh(hp * 3.0f + 0.2f * hp * hp) / 3.0f - hp * 0.95f;
                    h = c == 0 ? enhHp2_.l.tick(h) : enhHp2_.r.tick(h);
                    v += 0.25f * a[2] * h * 4.0f;
                }
                if (a[6] > 1e-6f) {   // HIGH: air shelf plus a touch of top harmonics
                    const float hp = c == 0 ? highHp_.l.tick(v) : highHp_.r.tick(v);
                    const float air = c == 0 ? airHp_.l.tick(std::tanh(hp * 4.0f) * 0.25f) : airHp_.r.tick(std::tanh(hp * 4.0f) * 0.25f);
                    v += (kit::dbToGain(8.0f * a[6]) - 1.0f) * hp + 0.1f * a[6] * air;
                }
                x[c] = v;
            }
            // FOCUS and STEREO IMAGE work on mid / side
            if (a[3] > 1e-6f || a[5] > 1e-6f) {
                float m = 0.5f * (x[0] + x[1]), s = 0.5f * (x[0] - x[1]);
                if (a[3] > 1e-6f) { const float fm = focBp_.l.tick(m), fsd = focBp_.r.tick(s); m += (kit::dbToGain(4.0f * a[3]) - 1.0f) * fm; s += (kit::dbToGain(-4.0f * a[3]) - 1.0f) * fsd; }
                if (a[5] > 1e-6f) { const float sh = imgHp_.l.tick(s); s += (kit::dbToGain(6.0f * a[5]) - 1.0f) * sh; }
                x[0] = m + s; x[1] = m - s;
            }
            // DYNAMIC PUNCH: lift the attack of each hit (fast level over slow level)
            const float lvl = std::max(std::fabs(x[0]), std::fabs(x[1]));
            fast_ = lvl + (lvl > fast_ ? fA : fR) * (fast_ - lvl);
            slow_ = lvl + (lvl > slow_ ? sA : sR) * (slow_ - lvl);
            if (a[4] > 1e-6f) {
                const float over = std::clamp(kit::gainToDb(fast_ + 1e-9f) - kit::gainToDb(slow_ + 1e-9f), 0.0f, 12.0f);
                const float k = kit::dbToGain(a[4] * std::min(6.0f, over));
                x[0] *= k; x[1] *= k;
            }
            // ---- FINAL STAGE ----
            const float d = drv_.next(dT);
            float yl = x[0] * d, yr = x[1] * d;
            if (limit) {
                const float pk = std::max(std::fabs(yl), std::fabs(yr));
                const float need = pk > 1.0f ? 1.0f / pk : 1.0f;
                limG_ = std::min(need, 1.0f + limRel * (limG_ - 1.0f));
                yl *= limG_; yr *= limG_;
                grM_ = std::max(grM_, -kit::gainToDb(limG_));
            } else {
                limG_ = 1.0f;
                auto clip = [kneeLin, this](float v) {
                    const float av = std::fabs(v);
                    if (av <= kneeLin) return v;
                    float y;
                    if (kneeLin >= 0.9999f) y = 1.0f;   // no drive: a plain 0 dBFS ceiling
                    else y = kneeLin + (1.0f - kneeLin) * std::tanh((av - kneeLin) / (1.0f - kneeLin));
                    grM_ = std::max(grM_, kit::gainToDb(av) - kit::gainToDb(y));
                    return std::copysign(y, v);
                };
                yl = clip(yl); yr = clip(yr);
            }
            const float o = out_.next(oT);
            yl *= o; yr *= o;
            pk_ = std::max(pk_, std::max(std::fabs(yl), std::fabs(yr)));
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        inPk_ = inPk;
        publishAll();
    }

private:
    void publishAll() noexcept {
        const float rms = 10.0f * std::log10(std::max(inRms_, 1e-12f));
        publishMeter(0, kit::gainToDb(inPk_)); publishMeter(1, rms < -24.0f ? 0.0f : rms < -12.0f ? 1.0f : 2.0f);
        publishMeter(2, -grM_); publishMeter(3, kit::gainToDb(pk_));
        publishReduction(-grM_);
    }
    double fs_ = 48000;
    hc::fx::Biquad2 lowBp_, lowLp_, lowHp_, bodyBp_, bodyLp_, enhHp_, enhHp2_, focBp_, imgHp_, highHp_, airHp_;
    hc::fx::Smoothed in_, drv_, out_;
    std::array<hc::fx::Smoothed, 8> amt_{};
    std::array<float, 2> dc_{};
    float fast_ = 0, slow_ = 0, limG_ = 1, grM_ = 0, pk_ = 0, inPk_ = 0, inRms_ = 0, noteKey_ = -1;
    bool loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeImpactCrown() { return std::make_unique<ImpactCrown>(); }

} // namespace hem::staging
