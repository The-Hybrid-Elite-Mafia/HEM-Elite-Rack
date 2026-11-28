// =============================================================================
//  51 TUBE PREAMP — a tube amp stage you pick and drive, with an amp-style tone section.
//  GAIN (-20..+40 dB) sets how hot the tube is hit; DRIVE (0..10) how far into the tube's curve it
//  goes (0 = clean). TUBE STAGE: 12AX7 (high-gain preamp tube: strong even + odd, asymmetric),
//  12AU7 (lower gain: soft and mostly low-order), 6L6 (power tube: rounder, odd-rich, it sags under
//  load). CHARACTER: CLEAN / WARM (a little low-mid weight, softer top) / VINTAGE (more weight, a
//  rolled-off top). Tone: BASS, MID, TREBLE (+-12 dB), PRESENCE (0..10, upper-mid lift), HPF and LPF.
//  OUTPUT (-12..+24 dB). VU DRIVE shows the level going into the tube.
//  Different from EMBERSTRIP (31: a full channel with expander and compressor).
//  Dead flat on load: GAIN 0 dB, DRIVE 0, CHARACTER CLEAN, tone 0 dB, PRESENCE 0, HPF 20 Hz / LPF 20 kHz
//  (= off), OUTPUT 0 dB.
//  ASSUMED: the panel's BASS 30..400 Hz / MID 250 Hz..8 kHz / TREBLE 4..16 kHz are the regions the knobs
//  work in; they are gain knobs acting at 120 Hz (shelf) / 1 kHz (Q 0.7) / 8 kHz (shelf). PRESENCE 10 =
//  +6 dB at 4 kHz. Tube curves and the 6L6 sag (up to 1.5 dB) are original HEM shapes.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kGain, kBass, kMid, kTreble, kOut, kTube, kDrive, kChar, kPresence, kHpf, kLpf, kBypass, kCount };
const char* const kTubeN[] = {"12AX7", "12AU7", "6L6"};
const char* const kCharN[] = {"CLEAN", "WARM", "VINTAGE"};
const ParamInfo kParams[kCount] = {
    {"gain", "GAIN", "dB", -20.0f, 40.0f, 0.0f},
    {"bass", "BASS", "dB", -12.0f, 12.0f, 0.0f},
    {"mid", "MID", "dB", -12.0f, 12.0f, 0.0f},
    {"treble", "TREBLE", "dB", -12.0f, 12.0f, 0.0f},
    {"output", "OUTPUT", "dB", -12.0f, 24.0f, 0.0f},
    {"tube_stage", "TUBE STAGE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kTubeN},
    {"drive", "DRIVE", "", 0.0f, 10.0f, 0.0f},
    {"character", "CHARACTER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kCharN},
    {"presence", "PRESENCE", "", 0.0f, 10.0f, 0.0f},
    {"hpf", "HPF", "Hz", 20.0f, 160.0f, 20.0f, Taper::Log},
    {"lpf", "LPF", "Hz", 5000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 6}, {"OUT", "dB", -60, 6}, {"VU DRIVE", "VU", -20, 3, true}};

constexpr std::uint32_t kSub = 8;

class TubePreamp final : public hc::fx::Effect {
public:
    TubePreamp() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        gain_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f); drive_.prepare(fs, 20.0, 0.0f);
        for (auto& g : gl_) g.prepare(fs, kSub, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto* f : {&bass_, &mid_, &treb_, &pres_, &hp_, &lp_, &c1_, &c2_}) f->clear();
        dc_.fill(0); sag_ = 0; on_.fill(false); fade_.fill(0); key_ = cKey_ = -1; snap_ = true; loaded_ = false; vu_ = 0; pkI_ = pkO_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float gT = kit::dbToGain(p(kGain)), oT = kit::dbToGain(p(kOut)), dT = p(kDrive) * 0.1f;
        if (!loaded_) { gain_.cur = gT; out_.cur = oT; drive_.cur = dT; loaded_ = true; }
        const int tube = static_cast<int>(p(kTube)), ch = static_cast<int>(p(kChar));
        setCharacter(ch);
        const float inc = static_cast<float>(1.0 / (0.02 * fs_)), vuC = hc::fx::onePoleCoef(fs_, 300.0), sagC = hc::fx::onePoleCoef(fs_, 80.0);
        float pkI = 0, pkO = 0;
        float* io[2] = {L, R};
        for (std::uint32_t at = 0; at < n; at += kSub) {
            setTone();
            snap_ = false;
            const std::uint32_t end = std::min(n, at + kSub);
            for (std::uint32_t i = at; i < end; ++i) {
                const float g = gain_.next(gT), d = drive_.next(dT), o = out_.next(oT);
                for (std::size_t k = 0; k < on_.size(); ++k) if (on_[k] && fade_[k] < 1.0f) fade_[k] = std::min(1.0f, fade_[k] + inc);
                float lvl = 0;
                for (std::size_t c = 0; c < 2; ++c) {
                    const float x0 = io[c][i];
                    pkI = std::max(pkI, std::fabs(x0));
                    float x = x0 * g;
                    lvl = std::max(lvl, std::fabs(x));
                    // filters first (the input coupling), then the tube
                    if (on_[4]) x += fade_[4] * ((c == 0 ? hp_.l.tick(x) : hp_.r.tick(x)) - x);
                    if (d > 1e-6f) {
                        const float k = 1.0f + 15.0f * d;
                        const float u = x * k;
                        float t;
                        if (tube == 0) t = std::tanh(u + 0.25f) - std::tanh(0.25f);                                   // 12AX7: asymmetric
                        else if (tube == 1) t = (u + 0.08f * u * u / (1.0f + std::fabs(u))) / std::sqrt(1.0f + u * u * 0.25f);   // 12AU7: soft, low order
                        else { const float sg = kit::dbToGain(-1.5f * std::min(1.0f, sag_)); t = std::tanh(u * sg) / sg; }   // 6L6: round, sags
                        const float sm = tube == 0 ? 1.0f / (1.0f - std::tanh(0.25f) * std::tanh(0.25f)) : 1.0f;   // small-signal gain of the curve
                        x += d * (t / (k * sm) - x);
                        dc_[c] += 0.0005f * (x - dc_[c]);
                        x -= dc_[c];
                    }
                    if (on_[0]) x += fade_[0] * ((c == 0 ? bass_.l.tick(x) : bass_.r.tick(x)) - x);
                    if (on_[1]) x += fade_[1] * ((c == 0 ? mid_.l.tick(x) : mid_.r.tick(x)) - x);
                    if (on_[2]) x += fade_[2] * ((c == 0 ? treb_.l.tick(x) : treb_.r.tick(x)) - x);
                    if (on_[3]) x += fade_[3] * ((c == 0 ? pres_.l.tick(x) : pres_.r.tick(x)) - x);
                    if (on_[5]) x += fade_[5] * ((c == 0 ? lp_.l.tick(x) : lp_.r.tick(x)) - x);
                    if (ch > 0) { x = c == 0 ? c1_.l.tick(x) : c1_.r.tick(x); x = c == 0 ? c2_.l.tick(x) : c2_.r.tick(x); }
                    x *= o;
                    pkO = std::max(pkO, std::fabs(x));
                    io[c][i] = kit::sane(x);
                }
                sag_ = lvl * (1.0f + 15.0f * d) * 0.3f + sagC * (sag_ - lvl * (1.0f + 15.0f * d) * 0.3f);
                const float a = lvl * 0.707f;
                vu_ = a + vuC * (vu_ - a);
            }
        }
        pkI_ = pkI; pkO_ = pkO;
        publishMeter(0, kit::gainToDb(pkI_)); publishMeter(1, kit::gainToDb(pkO_)); publishMeter(2, std::clamp(kit::gainToDb(vu_ * 1.11f) + 18.0f, -20.0f, 3.0f));
    }

private:
    float glide(std::size_t k, float t, bool logd) noexcept {
        auto& g = gl_[k];
        const float v = logd ? std::log(t) : t;
        if (snap_) g.cur = v;
        const float o = g.step(v);
        return logd ? std::exp(o) : o;
    }
    void setTone() noexcept {
        const float b = glide(0, p(kBass), false), m = glide(1, p(kMid), false), t = glide(2, p(kTreble), false), pr = glide(3, p(kPresence) * 0.6f, false);
        const float hp = glide(4, p(kHpf), true), lp = glide(5, p(kLpf), true);
        const float key = b * 3.0f + m * 7.0f + t * 11.0f + pr * 13.0f + hp * 0.1f + lp * 0.001f;
        if (key == key_) return;
        key_ = key;
        if (std::fabs(b) > 1e-4f) on_[0] = true;
        if (std::fabs(m) > 1e-4f) on_[1] = true;
        if (std::fabs(t) > 1e-4f) on_[2] = true;
        if (pr > 1e-4f) on_[3] = true;
        if (hp > 20.01f) on_[4] = true;
        if (lp < 19999.0f) on_[5] = true;
        const double ny = fs_ * 0.45;
        bass_.set(hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, 120.0, 0.7071, b));
        mid_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 1000.0, 0.7, m));
        treb_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min(8000.0, ny), 0.7071, t));
        pres_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, std::min(4000.0, ny), 1.0, pr));
        hp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 0.7071));
        lp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, ny), 0.7071));
    }
    void setCharacter(int ch) noexcept {
        if (static_cast<float>(ch) == cKey_) return;
        cKey_ = static_cast<float>(ch);
        const double ny = fs_ * 0.45;
        if (ch == 1) { c1_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 250.0, 0.8, 1.5)); c2_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min(8000.0, ny), 0.7071, -1.5)); }
        else if (ch == 2) { c1_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 150.0, 0.8, 2.0)); c2_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(10000.0, ny), 0.7071)); }
    }
    double fs_ = 48000;
    hc::fx::Biquad2 bass_, mid_, treb_, pres_, hp_, lp_, c1_, c2_;
    hc::fx::Smoothed gain_, out_, drive_;
    std::array<kit::BlockGlide, 6> gl_{};
    std::array<bool, 6> on_{};
    std::array<float, 6> fade_{};
    std::array<float, 2> dc_{};
    float sag_ = 0, key_ = -1, cKey_ = -1, vu_ = 0, pkI_ = 0, pkO_ = 0;
    bool snap_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeTubePreamp() { return std::make_unique<TubePreamp>(); }

} // namespace hem::staging
