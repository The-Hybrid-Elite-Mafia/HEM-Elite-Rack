// LOW RIDER, UNDERTOW, DEEP POCKETS, LOW PROFILE — the low-end makers (see ToneShop.hpp).
//
// Two engines. The octave engine (LOW RIDER, UNDERTOW) follows the band's waveform with a flip-flop that
// changes state at every other rising zero crossing (with a little hysteresis), so it runs at half the
// pitch (and a second one at a quarter); the square it makes is scaled by the band's level and rounded
// by a low-pass into a sub. The harmonics engine (DEEP POCKETS, LOW PROFILE) splits the bass off at
// FREQUENCY, normalises it to a steady size and feeds it through Chebyshev polynomials: for a pure
// tone T_k gives exactly the k-th harmonic, so the harmonic series and how fast it falls are exact; the
// level is then put back (with the upward squeeze of RATIO) and the result high-passed.
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace {
using namespace tone;

const char* const kPushN[] = {"A", "B"};

// gain for a level knob whose bottom is off
inline float levelOrOff(float db, float floor) noexcept { return db <= floor + 0.01f ? 0.0f : hc::fx::dbToGain(db); }
// above unity, the level saturates softly instead of passing full scale
inline float saturating(float x, float g) noexcept { return g <= 1.0f ? g * x : std::tanh(g * x); }

// A low-pass of 2 x 2nd-order Butterworth (24 dB / oct), one channel.
struct Lp4 {
    hc::fx::Biquad a, b;
    void set(double fs, double hz) noexcept { a.c = b.c = C::make(T::LowPass, fs, hz, 0.7071); }
    void clear() noexcept { a.clear(); b.clear(); }
    float tick(float x) noexcept { return b.tick(a.tick(x)); }
};

// Half the pitch of what goes in (and half again): see the file header.
struct Divider {
    float env = 0.0f;
    bool high = false;
    float q1 = 1.0f, q2 = 1.0f;
    void clear() noexcept { env = 0.0f; high = false; q1 = q2 = 1.0f; }
    // envRel: the level follower's release coefficient
    void tick(float x, float envRel) noexcept {
        const float a = std::fabs(x);
        env = a > env ? a : a + envRel * (env - a);
        const float h = 0.1f * env;
        if (!high && x > h) {   // a rising crossing
            high = true;
            const float before = q1;
            q1 = -q1;
            if (before < 0.0f) q2 = -q2;   // q1 went up: q2 flips at half its rate
        } else if (high && x < -h) {
            high = false;
        }
    }
};

// ---- LOW RIDER ---------------------------------------------------------------------------------
namespace lr {
using namespace lowrider;
const ParamInfo kParams[kCount] = {
    {"range", "Range", "Hz", 20.0f, 120.0f, 80.0f},
    {"lowair", "Low Air", "dB", -60.0f, 30.0f, 3.0f},
    {"lo", "Lo", "dB", -60.0f, 30.0f, 3.0f},
    {"direct", "Direct", "dB", -60.0f, 0.0f, 0.0f},
    {"output", "Output", "dB", -60.0f, 0.0f, 0.0f},
    {"trim", "Trim", "dB", -12.0f, 12.0f, 0.0f},
    {"align", "Align", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
const hc::fx::MeterInfo kMeters[] = {{"OUTPUT", "dBFS", -50.0f, 0.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lat_ = static_cast<std::uint32_t>(std::ceil(kLatencyS * fs));
        for (auto& ch : dly_) for (auto& d : ch) d.prepare(lat_ + 2);
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            lp_[c].clear();
            sub_[c].clear();
            div_[c].clear();
            for (auto& d : dly_[c]) d.clear();
        }
        range_ = -1.0f;
        design();
        g_[0].prepare(fs_, 20, levelOrOff(p(LowAir), -60.0f));
        g_[1].prepare(fs_, 20, levelOrOff(p(Lo), -60.0f));
        g_[2].prepare(fs_, 20, levelOrOff(p(Direct), -60.0f));
        g_[3].prepare(fs_, 20, outGain());
        publishMeter(0, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float w[4] = {levelOrOff(p(LowAir), -60.0f), levelOrOff(p(Lo), -60.0f), levelOrOff(p(Direct), -60.0f), outGain()};
        // ALIGN: the low paths wait less, by their own delay, so they land with the untouched sound
        const bool align = p(Align) >= 0.5f;
        const std::uint32_t dLo = align ? lat_ - std::min(lat_, gdLo_) : lat_, dAir = align ? lat_ - std::min(lat_, gdAir_) : lat_;
        const float envRel = coefMs(fs_, 30.0);
        float* io[2] = {L, R};
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float air = g_[0].next(w[0]), lo = g_[1].next(w[1]), dir = g_[2].next(w[2]), out = g_[3].next(w[3]);
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                const float band = lp_[c].tick(x);
                div_[c].tick(band, envRel);
                const float s = sub_[c].tick(div_[c].q1 * div_[c].env);
                const float y = dly_[c][0].process(dir * x, lat_) + dly_[c][1].process(saturating(band, lo), dLo) + dly_[c][2].process(saturating(s, air), dAir);
                io[c][i] = y * out;
                pk = std::max(pk, std::fabs(io[c][i]));
            }
        }
        publishMeter(0, std::max(dbOf(pk), -100.0f));
    }

private:
    float outGain() const noexcept { return levelOrOff(p(Output), -60.0f) * hc::fx::dbToGain(p(Trim)); }
    void design() noexcept {
        if (p(Range) == range_) return;
        range_ = p(Range);
        for (int c = 0; c < 2; ++c) {
            lp_[c].set(fs_, range_);
            sub_[c].set(fs_, range_ * 0.5);
        }
        // each 24 dB low-pass delays the lowest notes by about 0.45 / its frequency
        gdLo_ = static_cast<std::uint32_t>(0.45 / range_ * fs_);
        gdAir_ = static_cast<std::uint32_t>((0.45 / range_ + 0.45 / (range_ * 0.5)) * fs_);
    }

    double fs_ = 48000.0;
    std::uint32_t lat_ = 0, gdLo_ = 0, gdAir_ = 0;
    float range_ = -1.0f;
    Lp4 lp_[2], sub_[2];
    Divider div_[2];
    hc::fx::DelayLine dly_[2][3];
    hc::fx::Smoothed g_[4];
};
} // namespace lr

// ---- UNDERTOW ----------------------------------------------------------------------------------
namespace ut {
using namespace undertow;
const char* const kMonoN[] = {"STEREO", "MONO"};
const ParamInfo kParams[kCount] = {
    {"rangemin", "Range Min", "Hz", 20.0f, 120.0f, 20.0f},
    {"rangemax", "Range Max", "Hz", 40.0f, 240.0f, 150.0f},
    {"sub1on", "Sub -1", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"sub1", "Sub -1 Level", "dB", -60.0f, 12.0f, 0.0f},
    {"sub2on", "Sub -2", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"sub2", "Sub -2 Level", "dB", -60.0f, 12.0f, 0.0f},
    {"drive", "Drive", "%", 0.0f, 100.0f, 0.0f},
    {"dynamics", "Dynamics", "%", 0.0f, 100.0f, 50.0f},
    {"monosub", "Mono Sub", "", 0, 1, 1, Taper::Choice, kMonoN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
    {"output", "Output", "dB", -18.0f, 0.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"SUB", "dBFS", -60.0f, 0.0f}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == Sub1) return p(Sub1On) >= 0.5f;
        if (i == Sub2) return p(Sub2On) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            for (auto& h : hp_[c]) h.clear();
            lp_[c].clear();
            r1_[c].clear();
            r2_[c].clear();
            div_[c].clear();
            slow_[c] = 0.0f;
        }
        key_ = {-1.0f, -1.0f};
        design();
        s1_.prepare(fs_, 20, subGain(Sub1On, Sub1));
        s2_.prepare(fs_, 20, subGain(Sub2On, Sub2));
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const bool mono = p(MonoSub) >= 0.5f;
        const float w1 = subGain(Sub1On, Sub1), w2 = subGain(Sub2On, Sub2), wMix = p(Mix) * 0.01f, wOut = hc::fx::dbToGain(p(Output));
        const float drive = 1.0f + 0.09f * p(Drive), dyn = p(Dynamics) * 0.01f;
        const float envRel = coefMs(fs_, 30.0), slowK = 1.0f - coefMs(fs_, 500.0);
        float subPk = 0.0f, outPk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g1 = s1_.next(w1), g2 = s2_.next(w2), m = mix_.next(wMix), o = out_.next(wOut);
            const float dryG = std::min(1.0f, 2.0f * (1.0f - m)), wetG = std::min(1.0f, 2.0f * m);
            float subs[2];
            const int chans = mono ? 1 : 2;
            for (int c = 0; c < chans; ++c) {
                float x = mono ? 0.5f * (L[i] + R[i]) : (c == 0 ? L[i] : R[i]);
                for (auto& h : hp_[c]) x = h.tick(x);
                x = lp_[c].tick(x);
                Divider& d = div_[c];
                d.tick(x, envRel);
                // DYNAMICS: between the steady (slow) level and every move of the band
                slow_[c] += (d.env - slow_[c]) * slowK;
                const float lev = std::pow(std::max(d.env, 1e-9f), dyn) * std::pow(std::max(slow_[c], 1e-9f), 1.0f - dyn);
                float s = g1 * r1_[c].tick(d.q1 * lev) + g2 * r2_[c].tick(d.q2 * lev);
                if (drive > 1.0f) s = std::tanh(drive * s) / drive * (1.0f + 0.3f * (drive - 1.0f) / 9.0f);
                subs[c] = s;
            }
            if (mono) subs[1] = subs[0];
            const float yl = (dryG * L[i] + wetG * subs[0]) * o, yr = (dryG * R[i] + wetG * subs[1]) * o;
            subPk = std::max({subPk, std::fabs(subs[0]), std::fabs(subs[1])});
            L[i] = yl;
            R[i] = yr;
            outPk = std::max({outPk, std::fabs(yl), std::fabs(yr)});
        }
        publishMeter(0, std::max(dbOf(subPk), -100.0f));
        publishMeter(1, std::max(dbOf(outPk), -100.0f));
    }

private:
    float subGain(int on, int level) const noexcept { return p(on) >= 0.5f ? levelOrOff(p(level), -60.0f) : 0.0f; }
    void design() noexcept {
        const std::array<float, 2> now{p(RangeMin), p(RangeMax)};
        if (now == key_) return;
        key_ = now;
        constexpr double kQ48[] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};   // 8th-order Butterworth
        const double top = std::max(static_cast<double>(now[1]), now[0] * 1.2);
        for (int c = 0; c < 2; ++c) {
            for (int k = 0; k < 4; ++k) hp_[c][k].c = C::make(T::HighPass, fs_, now[0], kQ48[k]);
            lp_[c].set(fs_, top);
            r1_[c].set(fs_, top * 0.5);
            r2_[c].set(fs_, top * 0.25);
        }
    }

    double fs_ = 48000.0;
    std::array<float, 2> key_{};
    hc::fx::Biquad hp_[2][4];
    Lp4 lp_[2], r1_[2], r2_[2];
    Divider div_[2];
    float slow_[2]{};
    hc::fx::Smoothed s1_, s2_, mix_, out_;
};
} // namespace ut

// ---- the harmonics engine (DEEP POCKETS, LOW PROFILE) -------------------------------------------
struct HarmSettings {
    float hz = 90.0f, decayDb = -15.0f, ratio = 1.0f, responseMs = 20.0f;
    int highPass = 1;   // 0: 6 dB @ 16 Hz, 1: 12 dB at hz, 2: 24 dB at hz
    bool operator==(const HarmSettings&) const = default;
};
constexpr int kOrders = 6;   // harmonics 2 .. 7

class HarmEngine {
public:
    void prepare(double fs) noexcept { fs_ = fs; }
    void clear() noexcept {
        for (auto& x : xo_) x.clear();
        for (auto& h : hp_) for (auto& b : h) b.clear();
        env_[0] = env_[1] = 0.0f;
        cur_ = {};
        cur_.hz = -1.0f;
    }
    void set(const HarmSettings& s) noexcept {
        if (s == cur_) return;
        const bool split = s.hz != cur_.hz || s.highPass != cur_.highPass;
        cur_ = s;
        if (split) {
            const float f[1] = {s.hz};
            for (auto& x : xo_) x.design(fs_, f, 2);
            for (auto& h : hp_) {
                if (s.highPass == 0) {
                    h[0].c = onePoleHp(16.0);
                    h[1].c = C{};
                } else {
                    h[0].c = C::make(T::HighPass, fs_, s.hz, 0.7071);
                    h[1].c = s.highPass == 2 ? h[0].c : C{};
                }
            }
        }
        double sum = 0.0;
        for (int k = 0; k < kOrders; ++k) {
            w_[k] = static_cast<float>(std::pow(10.0, s.decayDb * k / 20.0));
            sum += w_[k];
        }
        for (float& w : w_) w = static_cast<float>(w / sum);
        att_ = coefMs(fs_, s.responseMs);
        rel_ = coefMs(fs_, s.responseMs * 4.0f);
    }
    // one channel: returns the highs, the bass and the harmonics of x
    void tick(int c, float x, float& high, float& low, float& harm) noexcept {
        float b[2];
        xo_[c].split(x, b);
        low = b[0];
        high = b[1];
        const float a = std::fabs(low);
        env_[c] = a + (a > env_[c] ? att_ : rel_) * (env_[c] - a);
        const float e = std::max(env_[c], 1e-6f);
        const float u = std::clamp(low / e, -1.0f, 1.0f);
        // T_2 .. T_7 by the recurrence T_{k+1} = 2 u T_k - T_{k-1}
        float tPrev = u, t = 2.0f * u * u - 1.0f, y = 0.0f;
        for (int k = 0; k < kOrders; ++k) {
            y += w_[k] * t;
            const float next = 2.0f * u * t - tPrev;
            tPrev = t;
            t = next;
        }
        // RATIO: the quiet bass gets more (an upward squeeze toward -12 dBFS, at most +24 dB)
        const float up = cur_.ratio > 1.0f ? std::clamp((1.0f - 1.0f / cur_.ratio) * (-12.0f - dbOf(e)), 0.0f, 24.0f) : 0.0f;
        y *= env_[c] * hc::fx::dbToGain(up);
        harm = hp_[c][1].tick(hp_[c][0].tick(y));
    }

private:
    C onePoleHp(double hz) const noexcept {   // 6 dB / oct high-pass as a biquad
        const double k = std::exp(-2.0 * std::numbers::pi * hz / fs_);
        C c;
        c.b0 = (1.0 + k) * 0.5;
        c.b1 = -(1.0 + k) * 0.5;
        c.b2 = 0.0;
        c.a1 = -k;
        c.a2 = 0.0;
        return c;
    }
    double fs_ = 48000.0;
    HarmSettings cur_{};
    Crossover<2> xo_[2];
    hc::fx::Biquad hp_[2][2];
    float w_[kOrders]{}, env_[2]{}, att_ = 0.0f, rel_ = 0.0f;
};

// ---- DEEP POCKETS ------------------------------------------------------------------------------
namespace dp {
using namespace deeppockets;
const char* const kHpN[] = {"6 dB 16 Hz", "12 dB", "24 dB"};
const char* const kListenN[] = {"ALL", "HARMONICS", "ORIGINAL"};
const ParamInfo kParams[kCount] = {
    {"frequency", "Frequency", "Hz", 32.0f, 256.0f, 90.0f, Taper::Log},
    {"input", "Input", "dB", -60.0f, 0.0f, 0.0f},
    {"original", "Original Bass", "dB", -60.0f, 12.0f, 0.0f},
    {"harmonics", "Harmonics", "dB", -60.0f, 12.0f, 0.0f},
    {"highpass", "High-Pass", "", 0, 2, 1, Taper::Choice, kHpN},
    {"decay", "Decay", "dB", -24.0f, -9.0f, -15.0f},
    {"ratio", "Ratio", ":1", 1.0f, 4.0f, 1.0f},
    {"response", "Response", "ms", 1.0f, 200.0f, 20.0f, Taper::Log},
    {"monitor", "Monitor", "", 0, 2, 0, Taper::Choice, kListenN},
    {"reset", "Reset", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"PEAK", "dBFS", -40.0f, 6.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        h_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        h_.clear();
        h_.set(settings());
        in_.prepare(fs_, 20, levelOrOff(p(Input), -60.0f));
        orig_.prepare(fs_, 20, levelOrOff(p(Original), -60.0f));
        harm_.prepare(fs_, 20, levelOrOff(p(Harmonics), -60.0f));
        held_ = 0.0f;
        push_ = p(Reset);
        publishMeter(0, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        h_.set(settings());
        if (p(Reset) != push_) {
            push_ = p(Reset);
            held_ = 0.0f;
        }
        const int mon = std::clamp(static_cast<int>(std::lround(p(Listen))), 0, 2);
        const float wIn = levelOrOff(p(Input), -60.0f), wO = levelOrOff(p(Original), -60.0f), wH = levelOrOff(p(Harmonics), -60.0f);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn), go = orig_.next(wO), gh = harm_.next(wH);
            for (int c = 0; c < 2; ++c) {
                float hi, lo, hm;
                h_.tick(c, io[c][i] * gi, hi, lo, hm);
                const float y = mon == All ? hi + go * lo + gh * hm : mon == HarmonicsOnly ? gh * hm : lo;
                io[c][i] = y;
                held_ = std::max(held_, std::fabs(y));
            }
        }
        publishMeter(0, std::max(dbOf(held_), -100.0f));
    }

private:
    HarmSettings settings() const noexcept {
        return {p(Frequency), p(Decay), p(Ratio), p(Response), std::clamp(static_cast<int>(std::lround(p(HighPass))), 0, 2)};
    }
    double fs_ = 48000.0;
    HarmEngine h_;
    hc::fx::Smoothed in_, orig_, harm_;
    float held_ = 0.0f, push_ = 0.0f;
};
} // namespace dp

// ---- LOW PROFILE -------------------------------------------------------------------------------
namespace lp {
using namespace lowprofile;
const ParamInfo kParams[kCount] = {
    {"frequency", "Frequency", "Hz", 32.0f, 256.0f, 90.0f, Taper::Log},
    {"originalin", "Original Bass", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"intensity", "Intensity", "dB", -24.0f, 24.0f, 0.0f},
    {"gain", "Gain", "dB", -60.0f, 0.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"OUTPUT", "dBFS", -60.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        h_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        h_.clear();
        h_.set(settings());
        orig_.prepare(fs_, 20, p(OriginalIn) >= 0.5f ? 1.0f : 0.0f);
        harm_.prepare(fs_, 20, hc::fx::dbToGain(p(Intensity)));
        out_.prepare(fs_, 20, levelOrOff(p(Gain), -60.0f));
        publishMeter(0, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        h_.set(settings());
        const float wO = p(OriginalIn) >= 0.5f ? 1.0f : 0.0f, wH = hc::fx::dbToGain(p(Intensity)), wOut = levelOrOff(p(Gain), -60.0f);
        float* io[2] = {L, R};
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float go = orig_.next(wO), gh = harm_.next(wH), o = out_.next(wOut);
            for (int c = 0; c < 2; ++c) {
                float hi, lo, hm;
                h_.tick(c, io[c][i], hi, lo, hm);
                io[c][i] = (hi + go * lo + gh * hm) * o;
                pk = std::max(pk, std::fabs(io[c][i]));
            }
        }
        publishMeter(0, std::max(dbOf(pk), -100.0f));
    }

private:
    HarmSettings settings() const noexcept { return {p(Frequency), -15.0f, 1.0f, 20.0f, 1}; }
    double fs_ = 48000.0;
    HarmEngine h_;
    hc::fx::Smoothed orig_, harm_, out_;
};
} // namespace lp

} // namespace

std::unique_ptr<hc::fx::Effect> makeLowRider() { return std::make_unique<lr::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeUndertow() { return std::make_unique<ut::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeDeepPockets() { return std::make_unique<dp::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeLowProfile() { return std::make_unique<lp::Impl>(); }

} // namespace hem::rack
