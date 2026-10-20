// STRAPPED — guitar mix desk: mode voicing, re-amp, six coloured faders (see Hideout.hpp).
//
// Order: SENSITIVITY, the MODE's voicing EQ, RE-AMP (oversampled amp curve, crossfaded so the latency never
// changes), COMPRESS, BASS and TREBLE, then the PITCH spread, DELAY and REVERB added in, BALANCE, OUTPUT.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"
#include "../tone/ToneCommon.hpp"

namespace hem::rack {
namespace strapped {
namespace {
using namespace hideout;

const char* const kModeN[] = {"CLEAN", "CRUNCH", "HEAVY", "BYPASS"};
const char* const kBassN[] = {"OFF", "80 HZ", "160 HZ", "320 HZ"};
const char* const kTrebleN[] = {"OFF", "3 KHZ", "6 KHZ", "12 KHZ"};
const char* const kCompN[] = {"OFF", "LIGHT", "MEDIUM", "HEAVY"};
const char* const kVerbN[] = {"OFF", "SMALL", "MEDIUM", "LARGE"};
const char* const kDelayN[] = {"OFF", "1/8", "1/8 D", "1/4"};
const char* const kPitchN[] = {"OFF", "NARROW", "WIDE", "WIDEST"};
#define HEM_FADER(id, name) {id, name, "", -10.0f, 10.0f, 0.0f}
#define HEM_COLOR(id, name, list) {id, name, "", 0, 3, 1, Taper::Choice, list}
const ParamInfo kParams[kCount] = {
    {"mode", "Mode", "", 0, 3, 0, Taper::Choice, kModeN},
    {"reamp", "Re-Amp", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"sensitivity", "Sensitivity", "dB", -10.0f, 10.0f, 0.0f},
    {"balance", "Balance", "dB", -6.0f, 6.0f, 0.0f},
    {"output", "Output", "dB", -10.0f, 10.0f, 0.0f},
    HEM_FADER("bass", "Bass"), HEM_FADER("treble", "Treble"), HEM_FADER("compress", "Compress"),
    HEM_FADER("reverb", "Reverb"), HEM_FADER("delay", "Delay"), HEM_FADER("pitch", "Pitch"),
    HEM_COLOR("basscolor", "Bass Color", kBassN), HEM_COLOR("treblecolor", "Treble Color", kTrebleN), HEM_COLOR("compresscolor", "Compress Color", kCompN),
    HEM_COLOR("reverbcolor", "Reverb Color", kVerbN), HEM_COLOR("delaycolor", "Delay Color", kDelayN), HEM_COLOR("pitchcolor", "Pitch Color", kPitchN),
};
#undef HEM_FADER
#undef HEM_COLOR
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "dBFS", -26.0f, 0.0f}, {"OUTPUT", "dBFS", -26.0f, 0.0f}};
constexpr float kBassHz[] = {0, 80, 160, 320}, kTrebleHz[] = {0, 3000, 6000, 12000};
constexpr double kDelayBeats[] = {0, 0.5, 0.75, 1.0};
constexpr double kMaxDelayS = 2.1;
// the modes' fixed voicing: low cut (Hz), a mid bell (Hz, dB), presence (dB at 4 kHz), and the amp's drive
struct Voice { float lowCut, midHz, midDb, presence, drive; };
constexpr Voice kVoice[4] = {{70, 800, -1.0f, 1.5f, 1.3f}, {90, 900, 2.0f, 1.0f, 3.0f}, {110, 750, -3.0f, 2.5f, 7.0f}, {0, 1000, 0.0f, 0.0f, 1.0f}};
// the compressor colours: threshold at fader 0 (dBFS), ratio, attack / release (ms)
struct Comp { float thr, ratio, att, rel; };
constexpr Comp kComp[4] = {{0, 1, 1, 1}, {-18, 2, 20, 200}, {-22, 4, 8, 120}, {-28, 8, 2, 80}};
constexpr float kVerbSize[4] = {0, 0.35f, 0.7f, 1.0f}, kVerbT60[4] = {0, 0.8f, 1.8f, 3.5f};
constexpr float kPitchCents[4] = {0, 6, 12, 20}, kPitchMs[4] = {0, 9, 16, 24};

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }
// a fader -10..+10 as a wet level: -30 dB at -10, 0 dB at +10
inline float wetOf(float f) noexcept { return hc::fx::dbToGain(-15.0f + 1.5f * f); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i >= Fader0 && i < Color0) return pick(p(Color0 + i - Fader0), 3) > 0;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        amp_.prepare();
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(kMaxDelayS * fs) + 8);
        for (auto& v : spread_) v.prepare(fs, 30.0);
        for (auto& d : spreadLate_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        room_.prepare(fs, 1.0);
        reset();
    }
    void reset() noexcept override {
        amp_.clear();
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        for (auto& d : echo_) d.clear();
        for (auto& v : spread_) v.clear();
        for (auto& d : spreadLate_) d.clear();
        room_.clear();
        key_.fill(-1e9f);
        design();
        env_ = gr_ = 0.0f;
        fb_[0] = fb_[1] = 0.0f;
        for (int f = 0; f < kFaders; ++f) wet_[f].prepare(fs_, 30, faderWet(f));
        time_.prepare(fs_, 150, delaySamples());
        in_.prepare(fs_, 20, hc::fx::dbToGain(p(Sensitivity)));
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        bal_.prepare(fs_, 20, p(Balance));
        pkIn_ = pkOut_ = 0.0f;
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int mode = pick(p(Mode), 3);
        const bool reamp = p(ReAmp) >= 0.5f && mode < 3;
        const float wIn = hc::fx::dbToGain(p(Sensitivity));
        float pin = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(wIn);
            L[i] *= g;
            R[i] *= g;
            pin = std::max({pin, std::fabs(L[i]), std::fabs(R[i])});
            // the mode's voicing
            L[i] = eq_[0][2].tick(eq_[0][1].tick(eq_[0][0].tick(L[i])));
            R[i] = eq_[1][2].tick(eq_[1][1].tick(eq_[1][0].tick(R[i])));
        }
        // RE-AMP: the mode's amp curve (always running, so switching it never moves the timing)
        const float drive = kVoice[mode].drive;
        amp_.run(L, R, n, reamp, static_cast<float>(1.0 / (fs_ * 0.02)), [drive](float x) { return std::tanh(drive * x + 0.1f * drive * x * x) / std::tanh(drive); });
        const int cc = pick(p(Color0 + CompressF), 3), rc = pick(p(Color0 + ReverbF), 3), dc = pick(p(Color0 + DelayF), 3), pc = pick(p(Color0 + PitchF), 3);
        const Comp& cp = kComp[cc];
        const float thr = cp.thr - 1.2f * p(Fader0 + CompressF);   // the fader pushes it harder
        const float att = coefMs(fs_, cp.att), rel = coefMs(fs_, cp.rel), envRel = coefMs(fs_, 10.0);
        const float makeup = cc > 0 ? std::min(0.5f * -downDb(-6.0f, thr, cp.ratio, 6.0f), 18.0f) : 0.0f;
        float wW[kFaders];
        for (int f = 0; f < kFaders; ++f) wW[f] = faderWet(f);
        const float tWant = delaySamples(), wOut = hc::fx::dbToGain(p(Output)), wBal = p(Balance);
        const float cents = kPitchCents[pc], ms = kPitchMs[pc] * 0.001f * static_cast<float>(fs_);
        float most = 0.0f, pout = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            if (cc > 0) {
                const float x = std::max(std::fabs(l), std::fabs(r));
                env_ = x > env_ ? x : x + envRel * (env_ - x);
                gr_ = follow(gr_, downDb(dbOf(env_), thr, cp.ratio, 6.0f), att, rel);
                most = std::min(most, gr_);
                const float gg = hc::fx::dbToGain(gr_ + makeup);
                l *= gg;
                r *= gg;
            }
            l = eq_[0][4].tick(eq_[0][3].tick(l));   // BASS, TREBLE
            r = eq_[1][4].tick(eq_[1][3].tick(r));
            const float mono = 0.5f * (l + r);
            float yl = l, yr = r;
            // PITCH: two detuned, late copies, one each side
            if (pc > 0) {
                spreadLate_[0].write(mono);
                spreadLate_[1].write(mono);
                const float a = spread_[0].tick(spreadLate_[0].read(ms), std::exp2(cents / 1200.0f));
                const float b = spread_[1].tick(spreadLate_[1].read(ms * 1.37f), std::exp2(-cents / 1200.0f));
                const float w = wet_[PitchF].next(wW[PitchF]);
                yl += w * a;
                yr += w * b;
            } else {
                wet_[PitchF].next(0.0f);
            }
            // DELAY: a note of the tempo, a little feedback, bouncing
            const float t = time_.next(tWant);
            const float el = echo_[0].read(t), er = echo_[1].read(t);
            echo_[0].write(mono + 0.3f * fb_[1]);
            echo_[1].write(0.3f * fb_[0]);
            fb_[0] = el;
            fb_[1] = er;
            const float dw = dc > 0 ? wet_[DelayF].next(wW[DelayF]) : wet_[DelayF].next(0.0f);
            yl += dw * el;
            yr += dw * er;
            // REVERB
            float rl = 0.0f, rr = 0.0f;
            room_.tick(l, r, rl, rr);
            const float rw = rc > 0 ? wet_[ReverbF].next(wW[ReverbF]) : wet_[ReverbF].next(0.0f);
            yl += rw * rl;
            yr += rw * rr;
            // BALANCE: one side down by what the other would go up
            const float b = bal_.next(wBal), o = out_.next(wOut);
            yl *= hc::fx::dbToGain(std::min(0.0f, -b)) * o;
            yr *= hc::fx::dbToGain(std::min(0.0f, b)) * o;
            L[i] = yl;
            R[i] = yr;
            pout = std::max({pout, std::fabs(yl), std::fabs(yr)});
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        pkIn_ = std::max(pin, pkIn_ * 0.9f);
        pkOut_ = std::max(pout, pkOut_ * 0.9f);
        publishMeter(0, std::max(dbOf(pkIn_), -100.0f));
        publishMeter(1, std::max(dbOf(pkOut_), -100.0f));
        publishReduction(most);
    }

private:
    float faderWet(int f) const noexcept {
        if (f < ReverbF) return 0.0f;
        return pick(p(Color0 + f), 3) > 0 ? wetOf(p(Fader0 + f)) : 0.0f;
    }
    float delaySamples() const noexcept {
        const int dc = std::max(1, pick(p(Color0 + DelayF), 3));
        return static_cast<float>(std::min(kDelayBeats[dc] * 60.0 / tempo(), kMaxDelayS - 0.01) * fs_);
    }
    void design() noexcept {
        const std::array<float, 8> now{p(Mode), p(Fader0 + BassF), p(Color0 + BassF), p(Fader0 + TrebleF), p(Color0 + TrebleF),
                                       p(Color0 + ReverbF), 0.0f, 0.0f};
        if (now == key_) return;
        key_ = now;
        const Voice& v = kVoice[pick(p(Mode), 3)];
        const double top = fs_ * 0.45;
        const C low = v.lowCut > 0 ? C::make(T::HighPass, fs_, v.lowCut, 0.7071) : C{};
        const C mid = C::make(T::Bell, fs_, v.midHz, 0.8, v.midDb);
        const C pres = C::make(T::HighShelf, fs_, std::min(4000.0, top), 0.7071, v.presence);
        const int bc = pick(p(Color0 + BassF), 3), tc = pick(p(Color0 + TrebleF), 3);
        const C bass = bc > 0 ? C::make(T::LowShelf, fs_, kBassHz[bc], 0.7071, 1.2 * p(Fader0 + BassF)) : C{};
        const C treb = tc > 0 ? C::make(T::HighShelf, fs_, std::min<double>(kTrebleHz[tc], top), 0.7071, 1.2 * p(Fader0 + TrebleF)) : C{};
        for (auto& ch : eq_) {
            ch[0].c = low;
            ch[1].c = mid;
            ch[2].c = pres;
            ch[3].c = bass;
            ch[4].c = treb;
        }
        const int rc = std::max(1, pick(p(Color0 + ReverbF), 3));
        room_.set(kVerbSize[rc], kVerbT60[rc], 7000.0);
    }

    double fs_ = 48000.0;
    tone::Drive4 amp_;
    hc::fx::Biquad eq_[2][5];
    hc::fx::DelayLine echo_[2], spreadLate_[2];
    PitchShift spread_[2];
    Fdn8 room_;
    std::array<float, 8> key_{};
    float env_ = 0.0f, gr_ = 0.0f, fb_[2]{}, pkIn_ = 0.0f, pkOut_ = 0.0f;
    hc::fx::Smoothed wet_[kFaders], time_, in_, out_, bal_;
};

} // namespace
} // namespace strapped

std::unique_ptr<hc::fx::Effect> makeStrapped() { return std::make_unique<strapped::Impl>(); }

} // namespace hem::rack
