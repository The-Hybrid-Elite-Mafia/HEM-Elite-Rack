// =============================================================================
//  33 REELAURA — a full tape machine with its own tape echo.
//    TAPE: input and output faders per side (INVERSE LINK: raising the input lowers the output by
//      the same amount), TAPE CHARACTER A / B / C, SPEED 7.5 / 15 IPS (head bump and top end move),
//      BIAS NOM / +3 / +5 (over-bias = darker and cleaner), TRACK A / A+B / B (A+B sums two heads a
//      hair apart), CALIBRATION (how hot the tape is hit), SATURATION, WOW and FLUTTER (depth and
//      rate), NOISE (on / off and level), VU in / out.
//    DELAY: SLAP (one repeat), FEEDBACK (repeats), PING PONG (repeats bounce L <-> R); INSERT (the
//      machine plus its echoes) or SEND - RETURN (echoes only, for an aux); left and right times
//      with SYNC and LINK, left and right levels, HP / LP; the repeats go back through the tape.
//  Different from TAPE SATURATOR (49: saturation only, no machine settings, no echo).
//  Dead flat on load (the dry signal, delayed by the fixed latency the wow and flutter need):
//  SATURATION 0 (the tape colour is only switched in above 0), WOW / FLUTTER 0, NOISE off, DELAY off,
//  every fader 0 dB.
//  ASSUMED: DELAY ON switch (the spec's levels are 0 dB; the card says echo off on load);
//  FEEDBACK / PING PONG repeat at 45 %; head bump 7.5 IPS +2.5 dB at 60 Hz, 15 IPS +2 dB at 110 Hz;
//  top end 7.5 IPS 12 kHz, 15 IPS 18 kHz; +3 / +5 bias lower the top to 0.8x / 0.6x and the drive to
//  0.8x / 0.65x; track B head 0.1 ms late; fixed latency 4 ms; 0 VU = -18 dBFS + CALIBRATION.
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
    kInL, kInR, kInvLink, kChar, kSpeed, kBias, kTrack, kCal, kClipReset, kVuSrc, kWowD, kWowR, kFlutD, kFlutR, kNoise, kNoiseLvl, kSat, kOutL, kOutR,
    kDelayOn, kType, kRouting, kTimeL, kSyncL, kLink, kTimeR, kSyncR, kLevelL, kLevelR, kDHp, kDLp, kBypass, kCount
};
const char* const kCharN[] = {"A", "B", "C"};
const char* const kSpeedN[] = {"7.5 IPS", "15 IPS"};
const char* const kBiasN[] = {"NOM", "+3", "+5"};
const char* const kTrackN[] = {"TRACK A", "TRACK A+B", "TRACK B"};
const char* const kVuN[] = {"INPUT", "OUTPUT"};
const char* const kTypeN[] = {"SLAP", "FEEDBACK", "PING PONG"};
const char* const kRouteN[] = {"INSERT", "SEND - RETURN"};
const ParamInfo kParams[kCount] = {
    {"input_l", "INPUT L", "dB", -18.0f, 18.0f, 0.0f},
    {"input_r", "INPUT R", "dB", -18.0f, 18.0f, 0.0f},
    {"inverse_link", "INVERSE LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"character", "TAPE CHARACTER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kCharN},
    {"speed", "SPEED", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kSpeedN},
    {"bias", "BIAS", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kBiasN},
    {"track", "TRACK SELECT", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kTrackN},
    {"calibration", "CALIBRATION", "dB", -6.0f, 6.0f, 0.0f},
    {"clip_reset", "CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"vu_source", "VU SOURCE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kVuN},
    {"wow_depth", "WOW DEPTH", "%", 0.0f, 100.0f, 0.0f},
    {"wow_rate", "WOW RATE", "Hz", 0.1f, 4.0f, 0.8f, Taper::Log},
    {"flutter_depth", "FLUTTER DEPTH", "%", 0.0f, 100.0f, 0.0f},
    {"flutter_rate", "FLUTTER RATE", "Hz", 4.0f, 20.0f, 9.0f, Taper::Log},
    {"noise", "NOISE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"noise_level", "NOISE LEVEL", "%", 0.0f, 100.0f, 0.0f},
    {"saturation", "SATURATION", "%", 0.0f, 100.0f, 0.0f},
    {"output_l", "OUTPUT L", "dB", -18.0f, 18.0f, 0.0f},
    {"output_r", "OUTPUT R", "dB", -18.0f, 18.0f, 0.0f},
    {"delay_on", "DELAY ON", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"delay_type", "DELAY TYPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kTypeN},
    {"delay_routing", "DELAY ROUTING", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kRouteN},
    {"left_time", "LEFT TIME", "ms", 1.0f, 2000.0f, 120.0f, Taper::Log},
    {"left_sync", "LEFT SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"time_link", "LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"right_time", "RIGHT TIME", "ms", 1.0f, 2000.0f, 180.0f, Taper::Log},
    {"right_sync", "RIGHT SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"left_level", "LEFT LEVEL", "dB", -60.0f, 6.0f, 0.0f},
    {"right_level", "RIGHT LEVEL", "dB", -60.0f, 6.0f, 0.0f},
    {"delay_hp", "HP FILTER", "Hz", 20.0f, 2000.0f, 20.0f, Taper::Log},
    {"delay_lp", "LP FILTER", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT VU", "VU", -20, 3, true}, {"OUTPUT VU", "VU", -20, 3, true}, {"CLIP", "", 0, 1}};

class ReelAura final : public hc::fx::Effect {
public:
    ReelAura() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_; }
    bool paramActive(int i) const noexcept override {
        if (i == kWowR) return p(kWowD) > 0.0f;
        if (i == kFlutR) return p(kFlutD) > 0.0f;
        if (i == kNoiseLvl) return p(kNoise) > 0.5f;
        if (i >= kChar && i <= kTrack) return p(kSat) > 0.0f;
        if (i == kTimeR || i == kSyncR) return p(kDelayOn) > 0.5f && p(kLink) < 0.5f;
        if (i > kDelayOn && i < kBypass) return p(kDelayOn) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lat_ = static_cast<std::uint32_t>(std::lround(fs * 0.004));
        for (auto& d : wf_) d.prepare(lat_ * 2 + 8);
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(fs * 2.1));
        for (auto* s : {&inL_, &inR_, &outL_, &outR_}) s->prepare(fs, 20.0, 1.0f);
        sat_.prepare(fs, 20.0, 0.0f); eng_.prepare(fs, 20.0, 0.0f); wow_.prepare(fs, 50.0, 0.0f); flut_.prepare(fs, 50.0, 0.0f);
        lvL_.prepare(fs, 20.0, 0.0f); lvR_.prepare(fs, 20.0, 0.0f); dOn_.prepare(fs, 20.0, 0.0f); tl_.prepare(fs, 80.0, 0.0f); tr_.prepare(fs, 80.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : wf_) d.clear();
        for (auto& d : echo_) d.clear();
        for (auto* f : {&emph_, &deemph_, &bump_, &top_, &topB_, &eHp_, &eLp_, &eTape_}) f->clear();
        dc_.fill(0); hist_.fill(0); wPh_ = fPh_ = 0; rng_ = 2468u; vuI_ = vuO_ = 0; clip_ = false; loaded_ = false; key_ = keyE_ = -1; eHpOn_ = eLpOn_ = false;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kClipReset) > 0.5f) clip_ = false;
        if (p(kBypass) > 0.5f) {   // bypass: the dry signal, still delayed by the reported latency so the host's timing holds
            float* b2[2] = {L, R};
            for (std::uint32_t i = 0; i < n; ++i) for (std::size_t c = 0; c < 2; ++c) { wf_[c].write(b2[c][i]); b2[c][i] = wf_[c].read(static_cast<float>(lat_)); }
            publishAll(); return;
        }
        const bool inv = p(kInvLink) > 0.5f;
        const float iLT = kit::dbToGain(p(kInL)), iRT = kit::dbToGain(p(kInR));
        const float oLT = kit::dbToGain(p(kOutL) - (inv ? p(kInL) : 0.0f)), oRT = kit::dbToGain(p(kOutR) - (inv ? p(kInR) : 0.0f));
        const float satT = p(kSat) * 0.01f, engT = p(kSat) > 0.0f ? 1.0f : 0.0f;
        const float wowT = p(kWowD) * 0.01f, flT = p(kFlutD) * 0.01f;
        const float lvLT = p(kLevelL) <= -59.9f ? 0.0f : kit::dbToGain(p(kLevelL)), lvRT = p(kLevelR) <= -59.9f ? 0.0f : kit::dbToGain(p(kLevelR));
        const float dOnT = p(kDelayOn) > 0.5f ? 1.0f : 0.0f;
        const float tLT = echoSamp(p(kTimeL), p(kSyncL) > 0.5f);
        const float tRT = p(kLink) > 0.5f ? tLT : echoSamp(p(kTimeR), p(kSyncR) > 0.5f);
        if (!loaded_) {
            inL_.cur = iLT; inR_.cur = iRT; outL_.cur = oLT; outR_.cur = oRT; sat_.cur = satT; eng_.cur = engT; wow_.cur = wowT; flut_.cur = flT;
            lvL_.cur = lvLT; lvR_.cur = lvRT; dOn_.cur = dOnT; tl_.cur = tLT; tr_.cur = tRT; loaded_ = true;
        }
        setTape();
        setEcho();
        const int ch = static_cast<int>(p(kChar)), bias = static_cast<int>(p(kBias)), track = static_cast<int>(p(kTrack));
        const float biasDrive = bias == 0 ? 1.0f : bias == 1 ? 0.8f : 0.65f;
        const float calG = kit::dbToGain(p(kCal));
        const float noiseA = p(kNoise) > 0.5f ? kit::dbToGain(-90.0f + 0.4f * p(kNoiseLvl)) : 0.0f;   // -90 .. -50 dBFS
        const float wInc = p(kWowR) / static_cast<float>(fs_), fInc = p(kFlutR) / static_cast<float>(fs_);
        const float fs = static_cast<float>(fs_);
        const float base = static_cast<float>(lat_);
        const int type = static_cast<int>(p(kType));
        const bool send = p(kRouting) > 0.5f;
        const float fb = type == 0 ? 0.0f : 0.45f;
        const float bOff = 0.0001f * fs;   // track B head, 0.1 ms late
        const float vuC = hc::fx::onePoleCoef(fs_, 300.0);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi[2] = {inL_.next(iLT), inR_.next(iRT)}, go[2] = {outL_.next(oLT), outR_.next(oRT)};
            const float sa = sat_.next(satT), en = eng_.next(engT), wd = wow_.next(wowT), fd = flut_.next(flT);
            wPh_ += wInc; wPh_ -= std::floor(wPh_); fPh_ += fInc; fPh_ -= std::floor(fPh_);
            // wow up to +-1.5 ms, flutter up to +-0.15 ms around the fixed 4 ms transport delay
            const float mod = wd * 0.0015f * fs * std::sin(kit::kTau * wPh_) + fd * 0.00015f * fs * std::sin(kit::kTau * fPh_);
            const float dl = lvL_.next(lvLT), dr = lvR_.next(lvRT), don = dOn_.next(dOnT), tl = tl_.next(tLT), tr = tr_.next(tRT);
            std::array<float, 2> y{};
            for (std::size_t c = 0; c < 2; ++c) {
                const float x = io[c][i] * gi[c];
                vuI_ = std::fabs(x) + vuC * (vuI_ - std::fabs(x));
                wf_[c].write(x);
                float v = wf_[c].read(std::max(0.0f, base + (c == 0 ? mod : mod * 0.97f)));   // written first, so `base` lands exactly base samples late
                if (en > 1e-6f) {   // the tape colour, switched in above SATURATION 0 and faded over 20 ms
                    const float vb = track == 0 ? v : wf_[c].read(std::max(0.0f, base + bOff + mod));
                    float t = track == 1 ? 0.5f * (v + vb) : track == 2 ? vb : v;
                    t = c == 0 ? emph_.l.tick(t) : emph_.r.tick(t);   // treble hits the tape first
                    const float k = (1.0f + 6.0f * sa) * biasDrive * calG;
                    const float u = t * k;
                    float s;
                    if (ch == 0) s = std::tanh(u);                                                // A: smooth, symmetric
                    else if (ch == 1) s = std::tanh(u + 0.12f * u * u);                          // B: richer even harmonics
                    else { s = std::tanh(u - 0.25f * (u - hist_[c])); hist_[c] = u; }            // C: denser, a touch of hysteresis
                    s /= k;
                    dc_[c] += 0.0005f * (s - dc_[c]);
                    s -= dc_[c];
                    s = c == 0 ? deemph_.l.tick(s) : deemph_.r.tick(s);
                    s = c == 0 ? bump_.l.tick(s) : bump_.r.tick(s);
                    s = c == 0 ? top_.l.tick(s) : top_.r.tick(s);
                    if (track != 0) s = c == 0 ? topB_.l.tick(s) : topB_.r.tick(s);
                    v += en * (s - v);
                }
                if (noiseA > 0.0f) { rng_ = rng_ * 1664525u + 1013904223u; v += noiseA * (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f); }
                y[c] = v;
            }
            // ---- tape echo ----
            if (don > 1e-6f || dOnT > 0.0f) {
                float el = echo_[0].read(std::max(0.0f, tl - 1.0f + mod)), er = echo_[1].read(std::max(0.0f, tr - 1.0f + mod));
                if (eHpOn_) { el = eHp_.l.tick(el); er = eHp_.r.tick(er); }
                if (eLpOn_) { el = eLp_.l.tick(el); er = eLp_.r.tick(er); }
                el = std::tanh(eTape_.l.tick(el) * 1.2f) / 1.2f; er = std::tanh(eTape_.r.tick(er) * 1.2f) / 1.2f;   // each repeat goes back through tape
                if (type == 2) { echo_[0].write(0.5f * (y[0] + y[1]) + fb * er); echo_[1].write(fb * el); }
                else { echo_[0].write(y[0] + fb * el); echo_[1].write(y[1] + fb * er); }
                const float wl = el * dl, wr = er * dr;
                if (send) { y[0] += don * (wl - y[0]); y[1] += don * (wr - y[1]); }   // SEND - RETURN: echoes only
                else { y[0] += don * wl; y[1] += don * wr; }                         // INSERT: the machine plus its echoes
            }
            for (std::size_t c = 0; c < 2; ++c) {
                const float o = y[c] * go[c];
                if (std::fabs(o) >= 1.0f) clip_ = true;
                vuO_ = std::fabs(o) + vuC * (vuO_ - std::fabs(o));
                io[c][i] = kit::sane(o);
            }
        }
        publishAll();
    }

private:
    [[nodiscard]] float echoSamp(float ms, bool sync) const noexcept {
        if (sync) { const float six = 15000.0f / std::max(tempo(), 20.0f); ms = std::max(six, std::round(ms / six) * six); }
        return std::clamp(static_cast<float>(fs_) * ms * 0.001f, 1.0f, static_cast<float>(fs_ * 2.0));
    }
    void setTape() noexcept {
        const int speed = static_cast<int>(p(kSpeed)), bias = static_cast<int>(p(kBias)), track = static_cast<int>(p(kTrack));
        const float key = static_cast<float>(speed * 9 + bias * 3 + track);
        if (key == key_) return;
        key_ = key;
        const double ny = fs_ * 0.45;
        const double emphHz = speed == 0 ? 3000.0 : 5000.0;
        emph_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, emphHz, 0.7071, 6.0));
        deemph_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, emphHz, 0.7071, -6.0));
        bump_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, speed == 0 ? 60.0 : 110.0, 1.0, (speed == 0 ? 2.5 : 2.0) + (track == 2 ? 0.5 : 0.0)));
        const double biasTop = bias == 0 ? 1.0 : bias == 1 ? 0.8 : 0.6;
        top_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(ny, (speed == 0 ? 12000.0 : 18000.0) * biasTop), 0.6));
        topB_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min(ny, 9000.0), 0.7071, -0.8));   // the second head is a little duller
    }
    void setEcho() noexcept {
        const float hp = p(kDHp), lp = p(kDLp);
        const float key = hp * 1e5f + lp;
        if (key == keyE_) return;
        keyE_ = key;
        if (hp > 20.01f) eHpOn_ = true;
        if (lp < 19999.0f) eLpOn_ = true;
        eHp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 0.7071));
        eLp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, fs_ * 0.45), 0.7071));
        eTape_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(7000.0, fs_ * 0.45), 0.6));
    }
    void publishAll() noexcept {
        const float cal = p(kCal);
        publishMeter(0, std::clamp(kit::gainToDb(vuI_ * 1.11f) + 18.0f - cal, -20.0f, 3.0f));
        publishMeter(1, std::clamp(kit::gainToDb(vuO_ * 1.11f) + 18.0f - cal, -20.0f, 3.0f));
        publishMeter(2, clip_ ? 1.0f : 0.0f);
    }

    double fs_ = 48000;
    std::uint32_t lat_ = 192;
    std::array<hc::fx::DelayLine, 2> wf_{}, echo_{};
    hc::fx::Biquad2 emph_, deemph_, bump_, top_, topB_, eHp_, eLp_, eTape_;
    hc::fx::Smoothed inL_, inR_, outL_, outR_, sat_, eng_, wow_, flut_, lvL_, lvR_, dOn_, tl_, tr_;
    std::array<float, 2> dc_{}, hist_{};
    float wPh_ = 0, fPh_ = 0, vuI_ = 0, vuO_ = 0, key_ = -1, keyE_ = -1;
    bool clip_ = false, loaded_ = false, eHpOn_ = false, eLpOn_ = false;
    std::uint32_t rng_ = 2468u;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeReelAura() { return std::make_unique<ReelAura>(); }

} // namespace hem::staging
