// =============================================================================
//  28 CONSOLECORE — a large-format console channel: INPUT (line and mic gain, analog THD, -20 dB
//  pad, polarity, 18 dB/oct HP and 12 dB/oct LP, FILTER SPLIT), DYNAMICS (compressor with
//  threshold / ratio / release / fast attack, gate or expander with threshold / range / release /
//  fast attack, POST EQ, external sidechain), a FOUR-BAND EQ (LF and HF shelf or bell, LMF and HMF
//  with Q, two curve characters, EQ SOLO, EQ TO SC) and the master section (fader, width with
//  PROTECT LOWS, stereo reverse, VU).
//  Signal order (default): FILTER > DYNAMICS > EQ > OUTPUT. POST EQ puts the dynamics after the EQ.
//  FILTER SPLIT ON keeps the filters at the input; OFF lets them travel with the EQ.
//  EQ TO SC takes the EQ out of the audio and puts it in the compressor's ears instead.
//  Different from CHANNELMATRIX (29, modular / reorderable) and EMBERSTRIP (31, tube channel).
//  Dead flat on load: gains 0 dB, THD 0 %, filters at 20 Hz / 20 kHz (= off), ratio 1:1, gate
//  range 0 dB, EQ gains 0, width 100 %, fader 0 dB.
//  ASSUMED: EQ CHARACTER A = constant-Q bells and plain shelves, B = Q narrows as the gain rises and
//  the shelves overshoot a little; FAMILY CLASSIC = +-15 dB and console frequency ranges, EXTENDED =
//  the full knob ranges (+-20 dB); ROUTING = where the compressor listens (PRE = the raw input,
//  INS = its own input, POST = its own output, feedback style); auto attack 30 ms, fast 3 ms; gate
//  attack 1.5 ms, fast 0.1 ms; expander 1:2; MW = width 200 %; 0 VU = -18 dBFS.
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

enum P {
    kLine, kMic, kThd, kPad, kPolarity, kHp, kLp, kSplit,
    kCThr, kCRatio, kCFast, kCRel, kGMode, kGThr, kGRange, kGFast, kGRel, kDynOn, kPostEq, kExtSc, kRouting,
    kLfF, kLfG, kLfBell, kLmfF, kLmfG, kLmfQ, kHmfF, kHmfG, kHmfQ, kHfF, kHfG, kHfBell, kChar, kSolo, kEqOn, kEqSc,
    kOut, kWidth, kReverse, kMaxW, kProtect, kVuSrc, kClipReset, kFamily, kBypass, kCount
};
const char* const kShapeN[] = {"SHELF", "BELL"};
const char* const kGModeN[] = {"GATE", "EXPAND"};
const char* const kScN[] = {"INT", "EXT"};
const char* const kRouteN[] = {"PRE", "INS", "POST"};
const char* const kCharN[] = {"A", "B"};
const char* const kVuN[] = {"IN", "OUT"};
const char* const kFamN[] = {"CLASSIC", "EXTENDED"};
const ParamInfo kParams[kCount] = {
    {"line_gain", "LINE GAIN", "dB", -20.0f, 20.0f, 0.0f},
    {"mic_gain", "MIC GAIN", "dB", 0.0f, 60.0f, 0.0f},
    {"analog_thd", "ANALOG THD", "%", 0.0f, 100.0f, 0.0f},
    {"pad", "PAD -20 dB", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"polarity", "POLARITY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"hp_freq", "HP FREQ", "Hz", 20.0f, 350.0f, 20.0f, Taper::Log},
    {"lp_freq", "LP FREQ", "Hz", 3000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"filter_split", "FILTER SPLIT", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"comp_threshold", "COMP THRESHOLD", "dB", -30.0f, 10.0f, 0.0f},
    {"comp_ratio", "COMP RATIO", ":1", 1.0f, 20.0f, 1.0f, Taper::Log},
    {"comp_fast", "COMP FAST ATTACK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"comp_release", "COMP RELEASE", "s", 0.1f, 4.0f, 0.3f, Taper::Log},
    {"gate_mode", "GATE / EXPAND", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kGModeN},
    {"gate_threshold", "GATE THRESHOLD", "dB", -80.0f, 0.0f, -60.0f},
    {"gate_range", "GATE RANGE", "dB", 0.0f, 60.0f, 0.0f},
    {"gate_fast", "GATE FAST ATTACK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"gate_release", "GATE RELEASE", "s", 0.05f, 4.0f, 0.25f, Taper::Log},
    {"dyn_enable", "DYNAMICS ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"post_eq", "POST EQ", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"ext_sc", "EXT SC", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kScN},
    {"routing", "ROUTING", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kRouteN},
    {"lf_freq", "LF FREQ", "Hz", 20.0f, 600.0f, 100.0f, Taper::Log},
    {"lf_gain", "LF GAIN", "dB", -20.0f, 20.0f, 0.0f},
    {"lf_shape", "LF SHELF / BELL", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kShapeN},
    {"lmf_freq", "LMF FREQ", "Hz", 150.0f, 3000.0f, 600.0f, Taper::Log},
    {"lmf_gain", "LMF GAIN", "dB", -20.0f, 20.0f, 0.0f},
    {"lmf_q", "LMF Q", "", 0.3f, 3.0f, 1.0f, Taper::Log},
    {"hmf_freq", "HMF FREQ", "Hz", 500.0f, 8000.0f, 2500.0f, Taper::Log},
    {"hmf_gain", "HMF GAIN", "dB", -20.0f, 20.0f, 0.0f},
    {"hmf_q", "HMF Q", "", 0.3f, 3.0f, 1.0f, Taper::Log},
    {"hf_freq", "HF FREQ", "Hz", 1200.0f, 20000.0f, 8000.0f, Taper::Log},
    {"hf_gain", "HF GAIN", "dB", -20.0f, 20.0f, 0.0f},
    {"hf_shape", "HF SHELF / BELL", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kShapeN},
    {"eq_character", "EQ CHARACTER", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kCharN},
    {"eq_solo", "EQ SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"eq_enable", "EQ ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"eq_to_sc", "EQ TO SC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"output", "OUTPUT", "dB", -40.0f, 12.0f, 0.0f},
    {"width", "WIDTH", "%", 0.0f, 200.0f, 100.0f},
    {"reverse", "REVERSE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"max_width", "MAX WIDTH (MW)", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"protect_lows", "PROTECT LOWS", "Hz", 20.0f, 500.0f, 20.0f, Taper::Log},
    {"vu_source", "VU SOURCE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kVuN},
    {"clip_reset", "CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"family", "FAMILY MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kFamN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"VU", "VU", -30, 3, true}, {"COMP GR", "dB", -30, 0}, {"GATE GR", "dB", -60, 0}, {"CLIP", "", 0, 1}};

constexpr std::uint32_t kGlideSub = 8;

struct OnePoleHp {
    float a = 0, x1 = 0, y1 = 0;
    void set(double fs, double hz) noexcept { a = static_cast<float>(std::exp(-kit::kTau * hz / fs)); }
    void clear() noexcept { x1 = y1 = 0; }
    float tick(float x) noexcept { const float y = a * (y1 + x - x1); x1 = x; y1 = y; return y; }
};

class ConsoleCore final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    ConsoleCore() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i >= kCThr && i <= kRouting && i != kDynOn) return p(kDynOn) > 0.5f;
        if (i >= kLfF && i <= kEqSc && i != kEqOn) return p(kEqOn) > 0.5f;
        if (i == kWidth) return p(kMaxW) < 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto* s : {&gain_, &out_, &wid_}) s->prepare(fs, 20.0, 1.0f);
        for (auto& g : gl_) g.prepare(fs, kGlideSub, 30.0, 0.0f);
        thd_.prepare(fs, 20.0, 0.0f); thr_.prepare(fs, 30.0, 0.0f); ratio_.prepare(fs, 30.0, 1.0f); grange_.prepare(fs, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto* f : {&hp2_, &lp_, &lf_, &lmf_, &hmf_, &hf_, &dLf_, &dLmf_, &dHmf_, &dHf_, &prot_}) f->clear();
        for (auto& h : hp1_) h.clear();
        dc_.fill(0); fbOut_.fill(0);
        cEnv_ = -120; gEnv_ = 0; gOpen_ = false; cGr_ = gGr_ = 0; vu_ = 0; clip_ = false; loaded_ = false;
        hpOn_ = lpOn_ = false; bandOn_.fill(false); fade_.fill(0.0f); protOn_ = false; keyF_ = keyE_ = keyP_ = -1;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kClipReset) > 0.5f) clip_ = false;
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; publishAll(); return; }
        const float gT = kit::dbToGain(p(kLine) + p(kMic) - (p(kPad) > 0.5f ? 20.0f : 0.0f)) * (p(kPolarity) > 0.5f ? -1.0f : 1.0f);
        const float oT = kit::dbToGain(p(kOut));
        const float wT = p(kMaxW) > 0.5f ? 2.0f : p(kWidth) * 0.01f;
        const float thdT = p(kThd) * 0.01f * (1.0f + p(kMic) / 20.0f);   // the mic stage colours harder
        if (!loaded_) { gain_.cur = gT; out_.cur = oT; wid_.cur = wT; thd_.cur = thdT; thr_.cur = p(kCThr); ratio_.cur = p(kCRatio); grange_.cur = p(kGRange); snap_ = true; loaded_ = true; }
        const bool dynOn = p(kDynOn) > 0.5f, eqOn = p(kEqOn) > 0.5f, solo = p(kSolo) > 0.5f;
        const bool split = p(kSplit) > 0.5f, postEq = p(kPostEq) > 0.5f, eqSc = p(kEqSc) > 0.5f && eqOn;
        const bool useExt = p(kExtSc) > 0.5f && scL_ != nullptr;
        const int route = static_cast<int>(p(kRouting));
        const bool dynLive = dynOn && !solo;
        const bool fast = p(kCFast) > 0.5f;
        const float cAtt = hc::fx::onePoleCoef(fs_, fast ? 3.0 : 30.0), cRel = hc::fx::onePoleCoef(fs_, p(kCRel) * 1000.0);
        const float gAtt = hc::fx::onePoleCoef(fs_, p(kGFast) > 0.5f ? 0.1 : 1.5), gRel = hc::fx::onePoleCoef(fs_, p(kGRel) * 1000.0);
        const bool expand = p(kGMode) > 0.5f;
        const float gThr = p(kGThr);
        const bool vuOut = p(kVuSrc) > 0.5f;
        const float vuC = hc::fx::onePoleCoef(fs_, 300.0);
        float cGrMax = 0, gGrMax = 0;
        constexpr std::uint32_t kSub = kGlideSub;   // filter and EQ settings glide, a fresh curve every 8 samples while they move (no zipper, no click)
        for (std::uint32_t at = 0; at < n; at += kSub) {
        const std::uint32_t end = std::min(n, at + kSub);
        setFilters();
        setEq();
        setProtect();
        snap_ = false;
        for (std::uint32_t i = at; i < end; ++i) {
            stepFades();
            const float rawL = L[i], rawR = R[i];
            // ---- INPUT stage ----
            const float g = gain_.next(gT);
            float xl = rawL * g, xr = rawR * g;
            const float th = thd_.next(thdT);
            if (th > 1e-6f) {   // console transformer / op-amp colour: a 2nd and a 3rd harmonic, DC removed
                auto col = [th](float v) { return v + th * (0.06f * v * v - 0.03f * v * v * v); };
                xl = col(xl); xr = col(xr);
                dc_[0] += 0.0005f * (xl - dc_[0]); dc_[1] += 0.0005f * (xr - dc_[1]);
                xl -= dc_[0]; xr -= dc_[1];
            }
            // ---- FILTER > DYNAMICS > EQ (or EQ > DYNAMICS with POST EQ) ----
            if (split) filters(xl, xr);
            float detL = route == 0 ? rawL : xl, detR = route == 0 ? rawR : xr;
            auto eqBlock = [&](float& a, float& b) {
                if (!split) filters(a, b);
                if (eqOn && !eqSc) eqAudio(a, b);
            };
            auto dyn = [&](float& a, float& b) {
                if (!dynLive) return;
                if (route == 1) { detL = a; detR = b; }
                if (route == 2) { detL = fbOut_[0]; detR = fbOut_[1]; }
                if (useExt) { detL = scL_[i]; detR = scR_[i]; }
                if (eqSc) eqDetector(detL, detR);
                const float lvl = kit::gainToDb(std::max(std::fabs(detL), std::fabs(detR)));
                // compressor (hard knee, linked)
                cEnv_ = lvl + (lvl > cEnv_ ? cAtt : cRel) * (cEnv_ - lvl);
                const float t = thr_.next(p(kCThr)), r = ratio_.next(p(kCRatio));
                const float cg = cEnv_ > t ? (cEnv_ - t) * (1.0f - 1.0f / r) : 0.0f;
                // gate / expander (range 0 = never acts)
                const float rg = grange_.next(p(kGRange));
                float want = 0.0f;
                if (rg > 0.0f) {
                    if (!expand) {
                        if (lvl > gThr) gOpen_ = true;
                        else if (lvl < gThr - 3.0f) gOpen_ = false;   // 3 dB hysteresis
                        want = gOpen_ ? 0.0f : rg;
                    } else want = std::min(rg, std::max(0.0f, gThr - lvl));   // 1:2 below the threshold
                }
                gEnv_ = want + (want < gEnv_ ? gAtt : gRel) * (gEnv_ - want);
                const float k = kit::dbToGain(-cg - gEnv_);
                a *= k; b *= k;
                fbOut_[0] = a; fbOut_[1] = b;
                cGrMax = std::max(cGrMax, cg); gGrMax = std::max(gGrMax, gEnv_);
            };
            if (!postEq) { dyn(xl, xr); eqBlock(xl, xr); }
            else { eqBlock(xl, xr); dyn(xl, xr); }
            // ---- MASTER: width (lows below PROTECT LOWS stay as they are), reverse, fader ----
            const float w = wid_.next(wT);
            if (!(kit::isFlat(w, 1.0f) && kit::isFlat(wT, 1.0f))) {
                const float m = 0.5f * (xl + xr), s = 0.5f * (xl - xr);
                const float sLow = protOn_ ? fade_[6] * prot_.l.tick(s) : 0.0f;
                const float s2 = sLow + w * (s - sLow);
                xl = m + s2; xr = m - s2;
            } else if (protOn_) (void)prot_.l.tick(0.5f * (xl - xr));
            if (p(kReverse) > 0.5f) std::swap(xl, xr);
            const float o = out_.next(oT);
            xl *= o; xr *= o;
            if (std::fabs(xl) >= 1.0f || std::fabs(xr) >= 1.0f) clip_ = true;
            const float a = vuOut ? 0.5f * (std::fabs(xl) + std::fabs(xr)) : 0.5f * (std::fabs(rawL) + std::fabs(rawR));
            vu_ = a + vuC * (vu_ - a);
            L[i] = kit::sane(xl); R[i] = kit::sane(xr);
        }
        }
        cGr_ = dynLive ? -cGrMax : 0.0f; gGr_ = dynLive ? -gGrMax : 0.0f;
        scL_ = scR_ = nullptr;
        publishAll();
    }

private:
    // a stage that starts running fades in over 20 ms (its filter memory starts empty), then stays in
    void stepFades() noexcept {
        const std::array<bool, 7> on{hpOn_, lpOn_, bandOn_[0], bandOn_[1], bandOn_[2], bandOn_[3], protOn_};
        const float inc = static_cast<float>(1.0 / (0.02 * fs_));
        for (std::size_t k = 0; k < 7; ++k) if (on[k] && fade_[k] < 1.0f) fade_[k] = std::min(1.0f, fade_[k] + inc);
    }
    static void blend(float& a, float y, float f) noexcept { a = f >= 1.0f ? y : a + f * (y - a); }
    void filters(float& a, float& b) noexcept {
        if (hpOn_) { blend(a, hp1_[0].tick(hp2_.l.tick(a)), fade_[0]); blend(b, hp1_[1].tick(hp2_.r.tick(b)), fade_[0]); }
        if (lpOn_) { blend(a, lp_.l.tick(a), fade_[1]); blend(b, lp_.r.tick(b), fade_[1]); }
    }
    void eqAudio(float& a, float& b) noexcept {
        if (bandOn_[0]) { blend(a, lf_.l.tick(a), fade_[2]); blend(b, lf_.r.tick(b), fade_[2]); }
        if (bandOn_[1]) { blend(a, lmf_.l.tick(a), fade_[3]); blend(b, lmf_.r.tick(b), fade_[3]); }
        if (bandOn_[2]) { blend(a, hmf_.l.tick(a), fade_[4]); blend(b, hmf_.r.tick(b), fade_[4]); }
        if (bandOn_[3]) { blend(a, hf_.l.tick(a), fade_[5]); blend(b, hf_.r.tick(b), fade_[5]); }
    }
    void eqDetector(float& a, float& b) noexcept {   // same curves, own filter memory
        if (bandOn_[0]) { a = dLf_.l.tick(a); b = dLf_.r.tick(b); }
        if (bandOn_[1]) { a = dLmf_.l.tick(a); b = dLmf_.r.tick(b); }
        if (bandOn_[2]) { a = dHmf_.l.tick(a); b = dHmf_.r.tick(b); }
        if (bandOn_[3]) { a = dHf_.l.tick(a); b = dHf_.r.tick(b); }
    }
    [[nodiscard]] float fam(float v, float lo, float hi) const noexcept { return p(kFamily) > 0.5f ? v : std::clamp(v, lo, hi); }
    // glide a setting toward its knob (frequencies and Q in log steps); the first block after loading starts at the knob
    float glide(std::size_t k, float target, bool logd) noexcept {
        auto& g = gl_[k];
        const float t = logd ? std::log(target) : target;
        if (snap_) g.cur = t;
        const float v = g.step(t);
        return logd ? std::exp(v) : v;
    }
    void setFilters() noexcept {
        const float hp = glide(0, fam(p(kHp), 20.0f, 350.0f), true), lp = glide(1, fam(p(kLp), 3000.0f, 20000.0f), true);
        const float key = hp * 100000.0f + lp;
        if (key == keyF_) return;
        keyF_ = key;
        if (hp > 20.01f) hpOn_ = true;   // 20 Hz / 20 kHz = off; once moved, they keep running (no click on return)
        if (lp < 19999.0f) lpOn_ = true;
        hp2_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 1.0));   // 2nd order Q 1 + 1st order = 18 dB/oct Butterworth
        for (auto& h : hp1_) h.set(fs_, hp);
        lp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, fs_ * 0.45), 0.7071));
    }
    void setEq() noexcept {
        const bool b = p(kChar) > 0.5f, ext = p(kFamily) > 0.5f;
        const float gl = ext ? 20.0f : 15.0f;
        const std::array<float, 4> g{glide(2, std::clamp(p(kLfG), -gl, gl), false), glide(3, std::clamp(p(kLmfG), -gl, gl), false),
                                     glide(4, std::clamp(p(kHmfG), -gl, gl), false), glide(5, std::clamp(p(kHfG), -gl, gl), false)};
        const std::array<float, 4> f{glide(6, fam(p(kLfF), 30.0f, 450.0f), true), glide(7, fam(p(kLmfF), 200.0f, 2500.0f), true),
                                     glide(8, fam(p(kHmfF), 600.0f, 7000.0f), true), glide(9, fam(p(kHfF), 1500.0f, 16000.0f), true)};
        const float qLm = glide(10, p(kLmfQ), true), qHm = glide(11, p(kHmfQ), true);
        float key = static_cast<float>(b) * 7.0f + p(kLfBell) * 11.0f + p(kHfBell) * 13.0f + qLm * 17.0f + qHm * 19.0f + (ext ? 23.0f : 0.0f);
        for (std::size_t k = 0; k < 4; ++k) key += g[k] * (31.0f + static_cast<float>(k)) + f[k] * (0.001f * static_cast<float>(k + 1));
        if (key == keyE_) return;
        keyE_ = key;
        for (std::size_t k = 0; k < 4; ++k) if (std::fabs(g[k]) > 1e-4f) bandOn_[k] = true;
        auto bellQ = [b](float q, float gain) { return b ? q * (1.0f + std::fabs(gain) / 10.0f) : q; };   // B: Q narrows as the gain rises
        const double ny = fs_ * 0.45;
        auto shelfOrBell = [&](bool bell, bool low, float hz, float gain) {
            if (bell) return hc::fx::BiquadCoefs::make(BT::Bell, fs_, std::min<double>(hz, ny), bellQ(low ? 0.9f : 1.1f, gain), gain);
            return hc::fx::BiquadCoefs::make(low ? BT::LowShelf : BT::HighShelf, fs_, std::min<double>(hz, ny), b ? 1.0 : 0.7071, gain);   // B shelves overshoot a little
        };
        const auto cLf = shelfOrBell(p(kLfBell) > 0.5f, true, f[0], g[0]);
        const auto cLmf = hc::fx::BiquadCoefs::make(BT::Bell, fs_, f[1], bellQ(qLm, g[1]), g[1]);
        const auto cHmf = hc::fx::BiquadCoefs::make(BT::Bell, fs_, std::min<double>(f[2], ny), bellQ(qHm, g[2]), g[2]);
        const auto cHf = shelfOrBell(p(kHfBell) > 0.5f, false, f[3], g[3]);
        lf_.set(cLf); lmf_.set(cLmf); hmf_.set(cHmf); hf_.set(cHf);
        dLf_.set(cLf); dLmf_.set(cLmf); dHmf_.set(cHmf); dHf_.set(cHf);
    }
    void setProtect() noexcept {
        const float hz = glide(12, p(kProtect), true);
        if (hz == keyP_) return;
        keyP_ = hz;
        if (hz > 20.01f) protOn_ = true;
        prot_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hz, 0.7071));
    }
    void publishAll() noexcept {
        publishMeter(0, std::clamp(kit::gainToDb(vu_ * 1.11f) + 18.0f, -30.0f, 3.0f));
        publishMeter(1, cGr_); publishMeter(2, gGr_); publishMeter(3, clip_ ? 1.0f : 0.0f);
        publishReduction(cGr_ + gGr_);
    }

    double fs_ = 48000;
    hc::fx::Biquad2 hp2_, lp_, lf_, lmf_, hmf_, hf_, dLf_, dLmf_, dHmf_, dHf_, prot_;
    std::array<OnePoleHp, 2> hp1_{};
    hc::fx::Smoothed gain_, out_, wid_, thd_, thr_, ratio_, grange_;
    std::array<float, 2> dc_{}, fbOut_{};
    std::array<bool, 4> bandOn_{};
    std::array<float, 7> fade_{};
    float cEnv_ = -120, gEnv_ = 0, cGr_ = 0, gGr_ = 0, vu_ = 0, keyF_ = -1, keyE_ = -1, keyP_ = -1;
    std::array<kit::BlockGlide, 13> gl_{};
    bool snap_ = true;
    bool gOpen_ = false, clip_ = false, loaded_ = false, hpOn_ = false, lpOn_ = false, protOn_ = false;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeConsoleCore() { return std::make_unique<ConsoleCore>(); }

} // namespace hem::staging
