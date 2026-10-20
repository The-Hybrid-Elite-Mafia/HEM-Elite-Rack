// Colour plug-ins: TUBE SAT, PREAMP, CASSETTE, VINYL, BIT CRUSHER, TRANSFORMER (see ColorFx.hpp).
#include "hem/rack/ColorFx.hpp"

#include "hem/rack/Spectrum.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;
using OS = hc::fx::Oversampler4;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
const char* const kOnOff[] = {"OFF", "ON"};

inline float soft(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }
inline float ironCurve(float v) noexcept { return v / std::sqrt(1.0f + 0.5f * v * v); }

// A 4x oversampled non-linear stage for both channels: run(L, R, n, f) replaces each sample x
// (at 4x) by f(channel, x). The result is delayed by OS::latency().
struct OsStage {
    OS os[2];
    std::vector<float> up;
    void prepare(std::uint32_t maxBlock) { up.assign(static_cast<std::size_t>(std::min(std::max<std::uint32_t>(maxBlock, 1), hc::fx::Effect::kMaxBlock)) * OS::kFactor, 0.0f); }
    void clear() noexcept { for (auto& o : os) o.clear(); }
    template <class F> void run(float* L, float* R, std::uint32_t n, F&& f) noexcept {
        float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            os[c].up(io[c], up.data(), n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) up[i] = f(c, up[i]);
            os[c].down(up.data(), io[c], n);
        }
    }
};
// A slow DC blocker (5 Hz) per channel, for the lopsided curves.
struct DcBlock {
    double s[2] = {0, 0}, k = 0;
    void set(double fs) noexcept { k = 1.0 - std::exp(-kTwoPi * 5.0 / fs); }
    float tick(int c, float x) noexcept { s[c] += (x - s[c]) * k; return x - static_cast<float>(s[c]); }
};
// Deterministic white noise (-1..1).
struct Rng {
    std::uint32_t s = 12345;
    float next() noexcept { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 8388608.0f - 1.0f; }
};

// ---- TUBE SAT -------------------------------------------------------------------------------------
namespace ts {
using namespace tubesat;
const ParamInfo kParams[kCount] = {
    {"drive", "Drive", "%", 0.0f, 100.0f, 40.0f},
    {"bias", "Bias", "%", 0.0f, 100.0f, 50.0f},
    {"tone", "Tone", "%", -100.0f, 100.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    void prepare(double fs, std::uint32_t mb) override {
        fs_ = fs;
        before_.prepare(fs);
        after_.prepare(fs);
        st_.prepare(mb);
        for (auto& d : dry_) d.prepare(OS::latency() + 1);
        dc_.set(fs);
        reset();
    }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        st_.clear();
        for (auto& d : dry_) d.clear();
        for (auto& ch : tone_) for (auto& b : ch) b.clear();
        dc_.s[0] = dc_.s[1] = 0;
        toneSet_ = -1000.0f;
        drive_.prepare(fs_, 30, p(Drive) * 0.01f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        work(L, R, n);
        after_.push(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        float* io[2] = {L, R};
        float dry[2][kChunk];
        for (std::uint32_t done = 0; done < n; done += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - done);
            for (int c = 0; c < 2; ++c) std::copy_n(io[c] + done, m, dry[c]);
            // DRIVE 0..100 % = 1x..16x (+24 dB) into the curve, and back down after it
            const float d = drive_.next(p(Drive) * 0.01f);
            for (std::uint32_t k = 1; k < m; ++k) (void)drive_.next(p(Drive) * 0.01f);
            const float g = std::pow(16.0f, d), b = 0.6f * p(Bias) * 0.01f;
            st_.run(L + done, R + done, m, [&](int, float x) {
                const float v = g * x;
                return soft(v + b * v * v) / g;   // BIAS tips the curve: even harmonics
            });
            setTone();
            for (std::uint32_t i = 0; i < m; ++i) {
                const float mx = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
                for (int c = 0; c < 2; ++c) {
                    auto& t = tone_[static_cast<std::size_t>(c)];
                    const float wet = static_cast<float>(t[1].tick(t[0].tick(dc_.tick(c, io[c][done + i]))));
                    const float dr = dry_[c].process(dry[c][i], OS::latency());
                    io[c][done + i] = (dr + mx * (wet - dr)) * o;
                }
            }
        }
    }
private:
    static constexpr std::uint32_t kChunk = 256;
    void setTone() noexcept {
        const float t = p(Tone);
        if (t == toneSet_) return;
        toneSet_ = t;
        const C lo = C::make(T::LowShelf, fs_, 700, 0.5, -0.04 * t), hi = C::make(T::HighShelf, fs_, 700, 0.5, 0.04 * t);   // ±4 dB tilt
        for (auto& ch : tone_) { ch[0].c = lo; ch[1].c = hi; }
    }
    SpectrumMeter before_, after_;
    double fs_ = 48000;
    OsStage st_;
    hc::fx::DelayLine dry_[2];
    std::array<std::array<hc::fx::Biquad, 2>, 2> tone_{};
    DcBlock dc_;
    float toneSet_ = -1000;
    hc::fx::Smoothed drive_, mix_, out_;
};
} // namespace ts

// ---- PREAMP ---------------------------------------------------------------------------------------
namespace pa {
using namespace preamp;
const char* const kCharN[] = {"CLEAN", "IRON", "TUBE"};
const char* const kPhaseN[] = {"NORMAL", "INVERT"};
const ParamInfo kParams[kCount] = {
    {"gain", "Gain", "dB", 0.0f, 30.0f, 0.0f},
    {"character", "Character", "", 0, 2, 1, Taper::Choice, kCharN},
    {"lowcut", "Low Cut 80 Hz", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"phase", "Phase", "", 0, 1, 0, Taper::Toggle, kPhaseN},
    {"output", "Output", "dB", -30.0f, 6.0f, 0.0f},
};
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    void prepare(double fs, std::uint32_t mb) override {
        fs_ = fs;
        before_.prepare(fs);
        after_.prepare(fs);
        st_.prepare(mb);
        dc_.set(fs);
        const C hp = C::make(T::HighPass, fs, 80, 0.7071);
        for (auto& b : cut_) b.c = hp;
        reset();
    }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        st_.clear();
        for (auto& b : cut_) b.clear();
        dc_.s[0] = dc_.s[1] = 0;
        gain_.prepare(fs_, 30, hc::fx::dbToGain(p(Gain)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        cutMix_ = p(LowCut) >= 0.5f ? 1.0f : 0.0f;
        sign_ = p(Phase) >= 0.5f ? -1.0f : 1.0f;
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        work(L, R, n);
        after_.push(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        const bool cut = p(LowCut) >= 0.5f;
        const float wantSign = p(Phase) >= 0.5f ? -1.0f : 1.0f;
        const float step = static_cast<float>(1.0 / (fs_ * 0.01));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = gain_.next(hc::fx::dbToGain(p(Gain)));
            cutMix_ = cut ? std::min(1.0f, cutMix_ + step) : std::max(0.0f, cutMix_ - step);
            sign_ += std::clamp(wantSign - sign_, -2.0f * step, 2.0f * step);   // a 10 ms flip, no click
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                const float hp = static_cast<float>(cut_[c].tick(x));
                io[c][i] = (x + cutMix_ * (hp - x)) * g * sign_;
            }
        }
        const int ch = std::clamp(static_cast<int>(std::lround(p(Character))), 0, 2);
        // the stage itself: CLEAN only rounds off what would clip; IRON round and odd; TUBE lopsided and even
        st_.run(L, R, n, [ch](int, float x) {
            if (ch == 0) return soft(x * 0.5f) * 2.0f;
            if (ch == 1) return ironCurve(x * 1.4f) / 1.4f;
            const float v = 1.6f * x;
            return soft(v + 0.3f * v * v) / 1.6f;
        });
        for (std::uint32_t i = 0; i < n; ++i) {
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = dc_.tick(0, L[i]) * o;
            R[i] = dc_.tick(1, R[i]) * o;
        }
    }
private:
    SpectrumMeter before_, after_;
    double fs_ = 48000;
    OsStage st_;
    hc::fx::Biquad cut_[2];
    DcBlock dc_;
    float cutMix_ = 0, sign_ = 1;
    hc::fx::Smoothed gain_, out_;
};
} // namespace pa

// ---- a wobbling transport (CASSETTE, VINYL) -------------------------------------------------------
struct Transport {
    hc::fx::DelayLine d[2];
    std::uint32_t base = 96;
    void prepare(std::uint32_t baseDelay) { base = baseDelay; for (auto& x : d) x.prepare(2 * baseDelay + 4); }
    void clear() noexcept { for (auto& x : d) x.clear(); }
    // mod: samples of displacement (|mod| < base). Exactly `base` behind when mod == 0.
    void tick(float& l, float& r, float mod) noexcept {
        (void)d[0].process(l, 0);
        (void)d[1].process(r, 0);
        if (mod == 0.0f) { l = d[0].read(static_cast<float>(base)); r = d[1].read(static_cast<float>(base)); return; }
        const float at = static_cast<float>(base) + mod;
        l = d[0].read(at);
        r = d[1].read(at);
    }
};

// ---- CASSETTE -------------------------------------------------------------------------------------
namespace cs {
using namespace cassette;
const char* const kTypeN[] = {"TYPE I", "TYPE II", "TYPE IV"};
const ParamInfo kParams[kCount] = {
    {"drive", "Drive", "%", 0.0f, 100.0f, 35.0f},
    {"type", "Tape Type", "", 0, 2, 1, Taper::Choice, kTypeN},
    {"wow", "Wow", "%", 0.0f, 100.0f, 20.0f},
    {"flutter", "Flutter", "%", 0.0f, 100.0f, 20.0f},
    {"hiss", "Hiss", "%", 0.0f, 100.0f, 0.0f},
    {"age", "Age", "%", 0.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr std::uint32_t kBase = 96;   // room for ±0.3 % wow at 0.5 Hz (about ±46 samples)
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency() + kBase; }
    void prepare(double fs, std::uint32_t mb) override {
        fs_ = fs;
        before_.prepare(fs);
        after_.prepare(fs);
        st_.prepare(mb);
        tr_.prepare(kBase);
        dc_.set(fs);
        reset();
    }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        st_.clear();
        tr_.clear();
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        dc_.s[0] = dc_.s[1] = 0;
        wowPh_ = 0.0;
        flPh_ = 0.0;
        hissLp_[0] = hissLp_[1] = 0.0f;
        rng_.s = 12345;
        filterKey_ = -1.0f;
        wow_.prepare(fs_, 50, p(Wow) * 0.01f);
        fl_.prepare(fs_, 50, p(Flutter) * 0.01f);
        hiss_.prepare(fs_, 50, p(Hiss) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        work(L, R, n);
        after_.push(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        const int type = std::clamp(static_cast<int>(std::lround(p(Type))), 0, 2);
        // the tape: TYPE I saturates earliest, metal latest; DRIVE pushes it
        const float head[3] = {1.6f, 1.25f, 1.0f};
        const float g = head[type] * std::pow(8.0f, p(Drive) * 0.01f);
        st_.run(L, R, n, [g](int, float x) { const float v = g * x; return soft(v + 0.08f * v * v) / g; });
        designFilters(type);
        const double wowInc = kTwoPi * 0.5 / fs_, flInc = kTwoPi * 6.0 / fs_;
        const float wowDepth = static_cast<float>(0.003 * fs_ / (kTwoPi * 0.5)), flDepth = static_cast<float>(0.0015 * fs_ / (kTwoPi * 6.0));
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = dc_.tick(0, L[i]), r = dc_.tick(1, R[i]);
            // the head and the tape lose the top end (TYPE, AGE)
            l = static_cast<float>(f_[0][1].tick(f_[0][0].tick(l)));
            r = static_cast<float>(f_[1][1].tick(f_[1][0].tick(r)));
            const float w = wow_.next(p(Wow) * 0.01f), fl = fl_.next(p(Flutter) * 0.01f);
            wowPh_ += wowInc; if (wowPh_ > kTwoPi) wowPh_ -= kTwoPi;
            flPh_ += flInc; if (flPh_ > kTwoPi) flPh_ -= kTwoPi;
            const float mod = w * wowDepth * static_cast<float>(std::sin(wowPh_)) + fl * flDepth * static_cast<float>(std::sin(flPh_) + 0.3 * std::sin(2.7 * flPh_));
            tr_.tick(l, r, mod);
            const float h = hiss_.next(p(Hiss) * 0.01f);
            if (h > 0.0f) {   // up to about -40 dBFS of hiss, brightened
                const float level = 0.01f * h * h;
                for (int c = 0; c < 2; ++c) {
                    const float wn = rng_.next();
                    const float bright = wn - 0.6f * hissLp_[c];
                    hissLp_[c] = wn;
                    (c == 0 ? l : r) += level * bright;
                }
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = l * o;
            R[i] = r * o;
        }
    }
private:
    void designFilters(int type) noexcept {
        const float key = static_cast<float>(type) * 1000.0f + p(Age);
        if (key == filterKey_) return;
        filterKey_ = key;
        const double age = p(Age) * 0.01;
        const double top = kTopHz[type] * std::pow(0.45, age);   // AGE: down to under half the bandwidth
        const C a = C::make(T::LowPass, fs_, std::min(top, fs_ * 0.45), 0.6);
        const C b = C::make(T::HighShelf, fs_, 3000, 0.5, -6.0 * age);
        for (auto& ch : f_) { ch[0].c = a; ch[1].c = b; }
    }
    SpectrumMeter before_, after_;
    double fs_ = 48000, wowPh_ = 0, flPh_ = 0;
    OsStage st_;
    Transport tr_;
    std::array<std::array<hc::fx::Biquad, 2>, 2> f_{};
    DcBlock dc_;
    Rng rng_;
    float hissLp_[2] = {0, 0}, filterKey_ = -1;
    hc::fx::Smoothed wow_, fl_, hiss_, out_;
};
} // namespace cs

// ---- VINYL ----------------------------------------------------------------------------------------
namespace vn {
using namespace vinyl;
const ParamInfo kParams[kCount] = {
    {"crackle", "Crackle", "%", 0.0f, 100.0f, 0.0f},
    {"noise", "Noise", "%", 0.0f, 100.0f, 0.0f},
    {"wear", "Wear", "%", 0.0f, 100.0f, 30.0f},
    {"warp", "Warp", "%", 0.0f, 100.0f, 20.0f},
    {"monolows", "Mono Lows", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr std::uint32_t kBase = 96;
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kBase; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        before_.prepare(fs);
        after_.prepare(fs);
        tr_.prepare(kBase);
        const C lp = C::make(T::LowPass, fs, 150, 0.7071), rum = C::make(T::LowPass, fs, 40, 0.7071);
        for (auto& b : mono_) b.c = lp;
        for (auto& b : rumble_) b.c = rum;
        reset();
    }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        tr_.clear();
        for (auto& b : mono_) b.clear();
        for (auto& b : rumble_) b.clear();
        for (auto& ch : wear_) for (auto& b : ch) b.clear();
        ph_ = 0.0;
        rng_.s = 777;
        click_ = 0.0f;
        clickDecay_ = 0.0f;
        clickSide_ = 0;
        wearKey_ = -1.0f;
        monoMix_ = p(MonoLows) >= 0.5f ? 1.0f : 0.0f;
        warp_.prepare(fs_, 50, p(Warp) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        work(L, R, n);
        after_.push(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        designWear();
        const double inc = kTwoPi * (33.333 / 60.0) / fs_;   // a 33 rpm record turns 0.56 times a second
        const float depth = static_cast<float>(0.004 * fs_ / (kTwoPi * 33.333 / 60.0));
        const float crackle = p(Crackle) * 0.01f, noise = p(Noise) * 0.01f, wear = p(Wear) * 0.01f;
        const bool mono = p(MonoLows) >= 0.5f;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const float clicksPerSecond = 40.0f * crackle * crackle;
        const float chance = static_cast<float>(clicksPerSecond / fs_);
        const float decay = static_cast<float>(std::exp(-1.0 / (fs_ * 0.0004)));
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            // MONO LOWS: below ~150 Hz both sides carry the same (a cutting lathe does that)
            monoMix_ = mono ? std::min(1.0f, monoMix_ + step) : std::max(0.0f, monoMix_ - step);
            {
                const float side = 0.5f * (l - r);
                const float lowSide = static_cast<float>(mono_[0].tick(side));
                const float s2 = side - monoMix_ * lowSide;
                const float mid = 0.5f * (l + r);
                l = mid + s2;
                r = mid - s2;
            }
            // WEAR: duller, and a touch of grit on the loud parts
            for (int c = 0; c < 2; ++c) {
                float& x = c == 0 ? l : r;
                auto& f = wear_[static_cast<std::size_t>(c)];
                x = static_cast<float>(f[1].tick(f[0].tick(x)));
                if (wear > 0.0f) { const float k = 1.0f + 2.0f * wear; x = soft(k * x) / k; }
            }
            // WARP
            const float w = warp_.next(p(Warp) * 0.01f);
            ph_ += inc; if (ph_ > kTwoPi) ph_ -= kTwoPi;
            tr_.tick(l, r, w * depth * static_cast<float>(std::sin(ph_)));
            // NOISE: surface hiss and rumble
            if (noise > 0.0f) {
                const float rum = static_cast<float>(rumble_[0].tick(rng_.next())) * 0.03f * noise;
                const float h0 = rng_.next() * 0.0015f * noise, h1 = rng_.next() * 0.0015f * noise;
                l += rum + h0;
                r += rum + h1;
            }
            // CRACKLE: short random clicks of random size, mostly on one side
            if (crackle > 0.0f) {
                const float u = 0.5f * (rng_.next() + 1.0f);
                if (u < chance) {
                    const float size = 0.5f * (rng_.next() + 1.0f);
                    click_ = (rng_.next() > 0 ? 1.0f : -1.0f) * (0.02f + 0.25f * size * size * size) * crackle;
                    clickSide_ = rng_.next() > 0 ? 0 : 1;
                    clickDecay_ = 1.0f;
                }
                if (clickDecay_ > 1e-4f) {
                    const float v = click_ * clickDecay_ * (rng_.next() * 0.5f + 0.5f);
                    l += clickSide_ == 0 ? v : 0.4f * v;
                    r += clickSide_ == 1 ? v : 0.4f * v;
                    clickDecay_ *= decay;
                }
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = l * o;
            R[i] = r * o;
        }
    }
private:
    void designWear() noexcept {
        const float wear = p(Wear);
        if (wear == wearKey_) return;
        wearKey_ = wear;
        const double w = wear * 0.01;
        const C id{1, 0, 0, 0, 0};   // WEAR 0: untouched
        const C a = w <= 0.0 ? id : C::make(T::LowPass, fs_, std::min(20000.0 * std::pow(0.3, w), fs_ * 0.45), 0.7071);
        const C b = w <= 0.0 ? id : C::make(T::HighPass, fs_, 10.0 + 40.0 * w, 0.7071);
        for (auto& ch : wear_) { ch[0].c = a; ch[1].c = b; }
    }
    SpectrumMeter before_, after_;
    double fs_ = 48000, ph_ = 0;
    Transport tr_;
    hc::fx::Biquad mono_[1], rumble_[1];
    std::array<std::array<hc::fx::Biquad, 2>, 2> wear_{};
    Rng rng_;
    float click_ = 0, clickDecay_ = 0, wearKey_ = -1, monoMix_ = 0;
    int clickSide_ = 0;
    hc::fx::Smoothed warp_, out_;
};
} // namespace vn

// ---- BIT CRUSHER ----------------------------------------------------------------------------------
namespace bc {
using namespace crusher;
const ParamInfo kParams[kCount] = {
    {"bits", "Bits", "", 1.0f, 24.0f, 8.0f},
    {"downsample", "Downsample", "x", 1.0f, 50.0f, 4.0f, Taper::Log},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; before_.prepare(fs); after_.prepare(fs); reset(); }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        phase_ = 0.0;   // the first sample is taken at once
        held_[0] = held_[1] = 0.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        work(L, R, n);
        after_.push(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        // BITS: 2^(bits-1) steps each side of zero (fractional bits blend smoothly between depths)
        const float steps = std::pow(2.0f, p(Bits) - 1.0f);
        const double hold = std::max(1.0f, p(Downsample));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const bool take = phase_ <= 0.0;   // a new sample every DOWNSAMPLE samples (fractional allowed)
            if (take) phase_ += hold;
            phase_ -= 1.0;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                if (take) held_[c] = std::round(std::clamp(x, -1.0f, 1.0f) * steps) / steps;
                io[c][i] = (x + m * (held_[c] - x)) * o;
            }
        }
    }
private:
    SpectrumMeter before_, after_;
    double fs_ = 48000, phase_ = 0.0;
    float held_[2] = {0, 0};
    hc::fx::Smoothed mix_, out_;
};
} // namespace bc

// ---- TRANSFORMER ----------------------------------------------------------------------------------
namespace tf {
using namespace transformer;
const char* const kTypeN[] = {"NICKEL", "STEEL"};
const ParamInfo kParams[kCount] = {
    {"drive", "Drive", "%", 0.0f, 100.0f, 40.0f},
    {"weight", "Weight", "dB", 0.0f, 6.0f, 1.5f},
    {"type", "Type", "", 0, 1, 1, Taper::Choice, kTypeN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    void prepare(double fs, std::uint32_t mb) override {
        fs_ = fs;
        before_.prepare(fs);
        after_.prepare(fs);
        st_.prepare(mb);
        for (auto& d : dry_) d.prepare(OS::latency() + 1);
        lowK_ = static_cast<float>(1.0 - std::exp(-kTwoPi * 120.0 / (fs * OS::kFactor)));   // the core's low end, at 4x
        reset();
    }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        st_.clear();
        for (auto& d : dry_) d.clear();
        for (auto& b : weight_) b.clear();
        low_[0] = low_[1] = 0.0f;
        weightSet_ = -1.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        work(L, R, n);
        after_.push(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        float* io[2] = {L, R};
        float dry[2][kChunk];
        for (std::uint32_t done = 0; done < n; done += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - done);
            for (int c = 0; c < 2; ++c) std::copy_n(io[c] + done, m, dry[c]);
            const bool steel = p(Type) >= 0.5f;
            const float g = (steel ? 1.3f : 1.0f) * std::pow(3.0f, p(Drive) * 0.01f);
            // the core saturates on the low end: split off below ~120 Hz, saturate that harder
            st_.run(L + done, R + done, m, [&](int c, float x) {
                low_[c] += (x - low_[c]) * lowK_;
                const float hi = x - low_[c];
                const float gl = 1.5f * g, gh = 0.3f * g;   // the lows drive the core much harder
                return ironCurve(gl * low_[c]) / gl + ironCurve(gh * hi) / gh;
            });
            setWeight();
            for (std::uint32_t i = 0; i < m; ++i) {
                const float mx = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
                for (int c = 0; c < 2; ++c) {
                    const float wet = static_cast<float>(weight_[c].tick(io[c][done + i]));
                    const float dr = dry_[c].process(dry[c][i], OS::latency());
                    io[c][done + i] = (dr + mx * (wet - dr)) * o;
                }
            }
        }
    }
private:
    static constexpr std::uint32_t kChunk = 256;
    void setWeight() noexcept {
        const float w = p(Weight);
        if (w == weightSet_) return;
        weightSet_ = w;
        const C c = C::make(T::Bell, fs_, 60, 0.8, w);
        for (auto& b : weight_) b.c = c;
    }
    SpectrumMeter before_, after_;
    double fs_ = 48000;
    OsStage st_;
    hc::fx::DelayLine dry_[2];
    hc::fx::Biquad weight_[2];
    float low_[2] = {0, 0}, lowK_ = 0, weightSet_ = -1;
    hc::fx::Smoothed mix_, out_;
};
} // namespace tf

} // namespace

std::unique_ptr<hc::fx::Effect> makeTubeSat() { return std::make_unique<ts::Impl>(); }
std::unique_ptr<hc::fx::Effect> makePreamp() { return std::make_unique<pa::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeCassette() { return std::make_unique<cs::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeVinyl() { return std::make_unique<vn::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeBitCrusher() { return std::make_unique<bc::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeTransformer() { return std::make_unique<tf::Impl>(); }

} // namespace hem::rack
