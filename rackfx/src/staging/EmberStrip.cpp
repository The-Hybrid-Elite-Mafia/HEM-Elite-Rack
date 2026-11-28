// =============================================================================
//  31 EMBERSTRIP — a warm tube channel: INPUT (polarity, HP OFF / 60 / 110 Hz, SENSITIVITY into
//  the tube, DRIVE), TONE (switchable bass shelf, sweepable mid, switchable high shelf), DYNAMICS
//  (one-knob EXPAND with GATE / EXPAND, one-knob COMPRESS that sets threshold and make-up
//  together, AGGRESSIVE for a harder, grittier squeeze), OUTPUT (fader, VU with CALIBRATION, clip).
//  Signal path: INPUT > EQ > DYNAMICS > OUTPUT.
//  Different from TUBE PREAMP (51: pick the tube, no dynamics) and CONSOLECORE (28: clean console).
//  Dead flat on load: SENSITIVITY 0 dB, DRIVE 0, all tone gains 0, EXPAND 0, COMPRESS 0, fader 0 dB.
//  ASSUMED: bass shelf 120 Hz, high shelf 10 kHz, mid Q 0.8; EXPAND 1..100 moves the threshold from
//  -70 to -20 dBFS (expander 1:2, gate 40 dB deep); COMPRESS 1..100 moves the threshold from -6 to
//  -36 dBFS at 3:1 (AGGRESSIVE 8:1, quicker, with grit) and adds back half the average cut;
//  0 VU = -18 dBFS + CALIBRATION.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kPolarity, kHp, kSens, kDrive, kBassOn, kBass, kMidF, kMidG, kHighOn, kHigh, kExpand, kExpMode, kComp, kAggr, kCal, kClipReset, kBypass, kOutput, kCount };
const char* const kHpN[] = {"OFF", "60 HZ", "110 HZ"};
const char* const kExpN[] = {"GATE", "EXPAND"};
const ParamInfo kParams[kCount] = {
    {"polarity", "POLARITY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"hp", "HP", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kHpN},
    {"sensitivity", "SENSITIVITY", "dB", -12.0f, 12.0f, 0.0f},
    {"drive", "DRIVE", "", 0.0f, 100.0f, 0.0f},
    {"bass_on", "BASS SHELF ON", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"bass", "BASS SHELF", "dB", -15.0f, 15.0f, 0.0f},
    {"mid_freq", "MID FREQ", "Hz", 100.0f, 5000.0f, 1000.0f, Taper::Log},
    {"mid_gain", "MID GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"high_on", "HIGH SHELF ON", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"high", "HIGH SHELF", "dB", -15.0f, 15.0f, 0.0f},
    {"expand", "EXPAND", "", 0.0f, 100.0f, 0.0f},
    {"expand_mode", "GATE / EXPAND", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kExpN},
    {"compress", "COMPRESS", "", 0.0f, 100.0f, 0.0f},
    {"aggressive", "AGGRESSIVE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"calibration", "CALIBRATION", "dB", -12.0f, 12.0f, 0.0f},
    {"clip_reset", "CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"output", "OUTPUT", "dB", -48.0f, 12.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"VU", "VU", -20, 6, true}, {"EXPANSION ACTIVITY", "dB", -40, 0}, {"COMPRESSION ACTIVITY", "dB", -30, 0}, {"CLIP", "", 0, 1}};

class EmberStrip final : public hc::fx::Effect {
public:
    EmberStrip() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kBass) return p(kBassOn) > 0.5f;
        if (i == kHigh) return p(kHighOn) > 0.5f;
        if (i == kExpMode) return p(kExpand) > 0.0f;
        if (i == kAggr) return p(kComp) > 0.0f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        sens_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f); drive_.prepare(fs, 20.0, 0.0f);
        bassG_.prepare(fs, 20.0, 1.0f); highG_.prepare(fs, 20.0, 1.0f);
        expA_.prepare(fs, 30.0, 0.0f); compA_.prepare(fs, 30.0, 0.0f);
        for (auto& g : gl_) g.prepare(fs, 32, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto* f : {&hp_, &bass_, &mid_, &high_}) f->clear();
        dc_.fill(0); eEnv_ = 0; eGr_ = 0; cEnv_ = -120; cGr_ = 0; avgGr_ = 0; gOpen_ = false;
        vu_ = 0; clip_ = false; loaded_ = false; hpKey_ = -1; eqKey_ = -1; hpOn_ = false; eqOn_.fill(false);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kClipReset) > 0.5f) clip_ = false;
        if (p(kBypass) > 0.5f) { publishAll(); return; }
        const float sT = kit::dbToGain(p(kSens)) * (p(kPolarity) > 0.5f ? -1.0f : 1.0f), oT = kit::dbToGain(p(kOutput)), dT = p(kDrive) * 0.01f;
        const float bT = p(kBassOn) > 0.5f ? 1.0f : 0.0f, hT = p(kHighOn) > 0.5f ? 1.0f : 0.0f;
        if (!loaded_) { sens_.cur = sT; out_.cur = oT; drive_.cur = dT; bassG_.cur = bT; highG_.cur = hT; expA_.cur = p(kExpand); compA_.cur = p(kComp); snap_ = true; loaded_ = true; }
        const bool gate = p(kExpMode) < 0.5f, aggr = p(kAggr) > 0.5f;
        const float eAtt = hc::fx::onePoleCoef(fs_, 1.0), eRel = hc::fx::onePoleCoef(fs_, 120.0);
        const float cAtt = hc::fx::onePoleCoef(fs_, aggr ? 1.0 : 10.0), cRel = hc::fx::onePoleCoef(fs_, aggr ? 60.0 : 250.0);
        const float ratio = aggr ? 8.0f : 3.0f;
        const float avgC = hc::fx::onePoleCoef(fs_, 1000.0), vuC = hc::fx::onePoleCoef(fs_, 300.0);
        float eM = 0, cM = 0;
        float* io[2] = {L, R};
        constexpr std::uint32_t kSub = 32;   // tone settings glide, a fresh curve every 32 samples
        for (std::uint32_t at = 0; at < n; at += kSub) {
        const std::uint32_t end = std::min(n, at + kSub);
        setFilters();
        snap_ = false;
        for (std::uint32_t i = at; i < end; ++i) {
            const float s = sens_.next(sT), d = drive_.next(dT), bg = bassG_.next(bT), hg = highG_.next(hT);
            std::array<float, 2> x{L[i] * s, R[i] * s};
            for (std::size_t c = 0; c < 2; ++c) {
                float v = x[c];
                if (hpOn_) v = c == 0 ? hp_.l.tick(v) : hp_.r.tick(v);
                if (d > 1e-6f) {   // the tube: an asymmetric curve (even + odd harmonics), blended in from clean
                    const float k = 1.0f + 7.0f * d;
                    const float u = v * k;
                    const float t = (std::tanh(u) + 0.18f * u * u / (1.0f + u * u)) / k;
                    v += d * (t - v);
                    dc_[c] += 0.0005f * (v - dc_[c]);
                    v -= dc_[c];
                }
                // tone: a switched band fades in / out over 20 ms (no click)
                if (eqOn_[0]) { const float y = c == 0 ? bass_.l.tick(v) : bass_.r.tick(v); v += bg * (y - v); }
                if (eqOn_[1]) v = c == 0 ? mid_.l.tick(v) : mid_.r.tick(v);
                if (eqOn_[2]) { const float y = c == 0 ? high_.l.tick(v) : high_.r.tick(v); v += hg * (y - v); }
                x[c] = v;
            }
            // ---- dynamics (linked) ----
            const float lvl = kit::gainToDb(std::max(std::fabs(x[0]), std::fabs(x[1])));
            const float ea = expA_.next(p(kExpand)), ca = compA_.next(p(kComp));
            float eWant = 0.0f;
            if (ea > 0.01f) {
                const float thr = -70.0f + 0.5f * ea;
                if (gate) { if (lvl > thr) gOpen_ = true; else if (lvl < thr - 4.0f) gOpen_ = false; eWant = gOpen_ ? 0.0f : 40.0f * std::min(1.0f, ea / 5.0f); }
                else eWant = std::min(40.0f, std::max(0.0f, thr - lvl));
            }
            eEnv_ = eWant + (eWant < eEnv_ ? eAtt : eRel) * (eEnv_ - eWant);
            cEnv_ = lvl + (lvl > cEnv_ ? cAtt : cRel) * (cEnv_ - lvl);
            float cg = 0.0f;
            if (ca > 0.01f) { const float thr = -6.0f - 0.3f * ca; cg = cEnv_ > thr ? (cEnv_ - thr) * (1.0f - 1.0f / ratio) : 0.0f; }
            avgGr_ = cg + avgC * (avgGr_ - cg);
            const float makeup = 0.5f * avgGr_;
            const float g = kit::dbToGain(-eEnv_ - cg + makeup);
            const float o = out_.next(oT);
            for (std::size_t c = 0; c < 2; ++c) {
                float v = x[c] * g;
                if (aggr && cg > 0.0f) { const float k = 1.0f + 0.03f * cg; v = std::tanh(v * k) / k; }   // AGGRESSIVE: grit grows with the squeeze
                v *= o;
                if (std::fabs(v) >= 1.0f) clip_ = true;
                io[c][i] = kit::sane(v);
            }
            eM = std::max(eM, eEnv_); cM = std::max(cM, cg);
            const float a = 0.5f * (std::fabs(L[i]) + std::fabs(R[i]));
            vu_ = a + vuC * (vu_ - a);
        }
        }
        eGr_ = eM; cGr_ = cM;
        publishAll();
    }

private:
    void setFilters() noexcept {
        const int hp = static_cast<int>(p(kHp));
        if (static_cast<float>(hp) != hpKey_) {
            hpKey_ = static_cast<float>(hp);
            if (hp > 0) hpOn_ = true;
            hp_.set(hp == 0 ? hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 10.0, 0.7071) : hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp == 1 ? 60.0 : 110.0, 0.7071));
        }
        auto glide = [this](std::size_t k, float t, bool logd) { auto& g = gl_[k]; const float v = logd ? std::log(t) : t; if (snap_) g.cur = v; const float o = g.step(v); return logd ? std::exp(o) : o; };
        const float bass = glide(0, p(kBass), false), midF = glide(1, p(kMidF), true), midG = glide(2, p(kMidG), false), high = glide(3, p(kHigh), false);
        const float key = bass * 3.0f + midF * 0.01f + midG * 7.0f + high * 11.0f;
        if (key == eqKey_) return;
        eqKey_ = key;
        if (std::fabs(bass) > 1e-4f) eqOn_[0] = true;
        if (std::fabs(midG) > 1e-4f) eqOn_[1] = true;
        if (std::fabs(high) > 1e-4f) eqOn_[2] = true;
        bass_.set(hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, 120.0, 0.7071, bass));
        mid_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, midF, 0.8, midG));
        high_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min(10000.0, fs_ * 0.4), 0.7071, high));
    }
    void publishAll() noexcept {
        publishMeter(0, std::clamp(kit::gainToDb(vu_ * 1.11f) + 18.0f - p(kCal), -20.0f, 6.0f));
        publishMeter(1, -eGr_); publishMeter(2, -cGr_); publishMeter(3, clip_ ? 1.0f : 0.0f);
        publishReduction(-(eGr_ + cGr_));
    }

    double fs_ = 48000;
    hc::fx::Biquad2 hp_, bass_, mid_, high_;
    hc::fx::Smoothed sens_, out_, drive_, bassG_, highG_, expA_, compA_;
    std::array<float, 2> dc_{};
    std::array<bool, 3> eqOn_{};
    float eEnv_ = 0, eGr_ = 0, cEnv_ = -120, cGr_ = 0, avgGr_ = 0, vu_ = 0, hpKey_ = -1, eqKey_ = -1;
    std::array<kit::BlockGlide, 4> gl_{};
    bool snap_ = true;
    bool gOpen_ = false, clip_ = false, loaded_ = false, hpOn_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeEmberStrip() { return std::make_unique<EmberStrip>(); }

} // namespace hem::staging
