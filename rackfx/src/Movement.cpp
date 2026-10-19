// Modulation effects: CHORUS, FLANGER, PHASER, TREMOLO, ROTARY, AUTO FILTER (see Movement.hpp).
#include "hem/rack/Movement.hpp"

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
constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kPi = std::numbers::pi;
const char* const kOnOff[] = {"OFF", "ON"};
inline float soft(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }

// A phase accumulator 0..1.
struct Lfo {
    double ph = 0;
    double tick(double hz, double fs) noexcept { ph += hz / fs; ph -= std::floor(ph); return ph; }
};
inline float sine01(double ph) noexcept { return static_cast<float>(std::sin(kTwoPi * ph)); }

// ---- CHORUS ---------------------------------------------------------------------------------------
namespace ch {
using namespace chorus;
const char* const kVoicesN[] = {"1", "2", "3"};
const ParamInfo kParams[kCount] = {
    {"rate", "Rate", "Hz", 0.05f, 5.0f, 0.6f, Taper::Log},
    {"depth", "Depth", "%", 0.0f, 100.0f, 40.0f},
    {"delay", "Delay", "ms", 5.0f, 30.0f, 12.0f},
    {"voices", "Voices", "", 0, 2, 1, Taper::Choice, kVoicesN},
    {"width", "Width", "%", 0.0f, 100.0f, 100.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : d_) d.prepare(static_cast<std::uint32_t>(fs * 0.045) + 4);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : d_) d.clear();
        lfo_.ph = 0;
        delay_.prepare(fs_, 100, static_cast<float>(p(Delay) * 0.001 * fs_));
        depth_.prepare(fs_, 50, p(Depth) * 0.01f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int voices = std::clamp(static_cast<int>(std::lround(p(Voices))), 0, 2) + 1;
        const float width = p(Width) * 0.01f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const double ph = lfo_.tick(p(Rate), fs_);
            const float base = delay_.next(static_cast<float>(p(Delay) * 0.001 * fs_));
            const float depth = depth_.next(p(Depth) * 0.01f) * static_cast<float>(0.008 * fs_) * 0.5f;   // up to ±4 ms
            const float mx = mix_.next(p(Mix) * 0.01f);
            d_[0].write(L[i]);
            d_[1].write(R[i]);
            float wl = 0, wr = 0;
            for (int v = 0; v < voices; ++v) {
                const double off = static_cast<double>(v) / voices;
                // right side: a quarter cycle later (times WIDTH) — that's what spreads it
                const float ml = sine01(ph + off), mr = sine01(ph + off + 0.25 * width);
                wl += d_[0].read(base + depth * ml);
                wr += d_[1].read(base + depth * mr);
            }
            wl /= static_cast<float>(voices);
            wr /= static_cast<float>(voices);
            L[i] += mx * (wl - L[i]);
            R[i] += mx * (wr - R[i]);
        }
    }
private:
    double fs_ = 48000;
    hc::fx::DelayLine d_[2];
    Lfo lfo_;
    hc::fx::Smoothed delay_, depth_, mix_;
};
} // namespace ch

// ---- FLANGER --------------------------------------------------------------------------------------
namespace fl {
using namespace flanger;
const ParamInfo kParams[kCount] = {
    {"rate", "Rate", "Hz", 0.02f, 5.0f, 0.25f, Taper::Log},
    {"depth", "Depth", "%", 0.0f, 100.0f, 60.0f},
    {"manual", "Manual", "ms", 0.1f, 10.0f, 1.5f, Taper::Log},
    {"feedback", "Feedback", "%", -95.0f, 95.0f, 50.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : d_) d.prepare(static_cast<std::uint32_t>(fs * 0.025) + 4);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : d_) d.clear();
        lfo_.ph = 0;
        fbs_[0] = fbs_[1] = 0.0f;
        man_.prepare(fs_, 50, static_cast<float>(p(Manual) * 0.001 * fs_));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float depth = p(Depth) * 0.01f, fb = p(Feedback) * 0.01f;
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const double ph = lfo_.tick(p(Rate), fs_);
            const float man = man_.next(static_cast<float>(p(Manual) * 0.001 * fs_));
            const float mx = mix_.next(p(Mix) * 0.01f);
            for (int c = 0; c < 2; ++c) {
                // the sweep goes from MANUAL up to MANUAL x (1 + 6 x DEPTH): an exponential sweep, like a tape flange
                const float m = 0.5f * (1.0f + sine01(ph + (c == 0 ? 0.0 : 0.25)));
                const float delay = std::max(1.0f, man * std::pow(1.0f + 6.0f * depth, m));
                d_[c].write(io[c][i] + fb * fbs_[c]);
                const float w = d_[c].read(delay);
                fbs_[c] = soft(w);   // keeps high feedback from running away
                // MIX 50 % = equal parts: the deepest notches
                io[c][i] += mx * (w - io[c][i]);
            }
        }
    }
private:
    double fs_ = 48000;
    hc::fx::DelayLine d_[2];
    float fbs_[2] = {0, 0};
    Lfo lfo_;
    hc::fx::Smoothed man_, mix_;
};
} // namespace fl

// ---- PHASER ---------------------------------------------------------------------------------------
namespace ph {
using namespace phaser;
const char* const kStagesN[] = {"4", "6", "8", "12"};
const ParamInfo kParams[kCount] = {
    {"rate", "Rate", "Hz", 0.02f, 5.0f, 0.4f, Taper::Log},
    {"depth", "Depth", "%", 0.0f, 100.0f, 70.0f},
    {"center", "Center", "Hz", 200.0f, 4000.0f, 900.0f, Taper::Log},
    {"stages", "Stages", "", 0, 3, 1, Taper::Choice, kStagesN},
    {"feedback", "Feedback", "%", -90.0f, 90.0f, 30.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& c : z_) c.fill(0.0f);
        fbs_[0] = fbs_[1] = 0.0f;
        lfo_.ph = 0;
        center_.prepare(fs_, 50, p(Center));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int stages = kStages[std::clamp(static_cast<int>(std::lround(p(Stages))), 0, 3)];
        const float depth = p(Depth) * 0.01f, fb = p(Feedback) * 0.01f;
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const double ph = lfo_.tick(p(Rate), fs_);
            const float center = center_.next(p(Center)), mx = mix_.next(p(Mix) * 0.01f);
            for (int c = 0; c < 2; ++c) {
                // the sweep: ±2 octaves x DEPTH around CENTER
                const float hz = std::clamp(center * std::pow(4.0f, depth * sine01(ph + (c == 0 ? 0.0 : 0.25))), 20.0f, static_cast<float>(fs_ * 0.45));
                const float t = static_cast<float>(std::tan(kPi * hz / fs_));
                const float a = (t - 1.0f) / (t + 1.0f);   // first-order all-pass with 90 degrees at hz
                float v = io[c][i] + fb * fbs_[c];
                auto& z = z_[static_cast<std::size_t>(c)];
                for (int k = 0; k < stages; ++k) {
                    const float y = a * v + z[static_cast<std::size_t>(k)];
                    z[static_cast<std::size_t>(k)] = v - a * y;
                    v = y;
                }
                fbs_[c] = soft(v);
                io[c][i] += mx * (v - io[c][i]);
            }
        }
    }
private:
    double fs_ = 48000;
    std::array<std::array<float, 12>, 2> z_{};
    float fbs_[2] = {0, 0};
    Lfo lfo_;
    hc::fx::Smoothed center_, mix_;
};
} // namespace ph

// ---- TREMOLO --------------------------------------------------------------------------------------
namespace tr {
using namespace tremolo;
const char* const kShapeN[] = {"SINE", "TRIANGLE", "SQUARE"};
const ParamInfo kParams[kCount] = {
    {"rate", "Rate", "Hz", 0.1f, 20.0f, 4.0f, Taper::Log},
    {"depth", "Depth", "%", 0.0f, 100.0f, 50.0f},
    {"shape", "Shape", "", 0, 2, 0, Taper::Choice, kShapeN},
    {"pan", "Auto Pan", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        lfo_.ph = 0;
        sq_ = 0.0f;
        depth_.prepare(fs_, 30, p(Depth) * 0.01f);
        pan_ = p(Pan) >= 0.5f ? 1.0f : 0.0f;
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int shape = std::clamp(static_cast<int>(std::lround(p(Shape))), 0, 2);
        const bool pan = p(Pan) >= 0.5f;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const float sqK = static_cast<float>(std::exp(-1.0 / (fs_ * 0.002)));   // square edges: 2 ms
        for (std::uint32_t i = 0; i < n; ++i) {
            const double ph = lfo_.tick(p(Rate), fs_);
            float m;   // -1..1
            if (shape == 0) m = sine01(ph);
            else if (shape == 1) m = static_cast<float>(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            else { const float t = ph < 0.5 ? 1.0f : -1.0f; sq_ = t + sqK * (sq_ - t); m = sq_; }
            const float d = depth_.next(p(Depth) * 0.01f);
            pan_ = pan ? std::min(1.0f, pan_ + step) : std::max(0.0f, pan_ - step);
            const float gl = 1.0f - d * 0.5f * (1.0f + m);                // left (and both, as tremolo)
            const float gp = 1.0f - d * 0.5f * (1.0f - m);                // opposite phase
            const float gr = gl + pan_ * (gp - gl);
            L[i] *= gl;
            R[i] *= gr;
        }
    }
private:
    double fs_ = 48000;
    Lfo lfo_;
    float sq_ = 0, pan_ = 0;
    hc::fx::Smoothed depth_;
};
} // namespace tr

// ---- ROTARY ---------------------------------------------------------------------------------------
namespace ro {
using namespace rotary;
const char* const kSpeedN[] = {"SLOW", "FAST"};
const ParamInfo kParams[kCount] = {
    {"speed", "Speed", "", 0, 1, 0, Taper::Choice, kSpeedN},
    {"drive", "Drive", "%", 0.0f, 100.0f, 20.0f},
    {"balance", "Balance", "%", -100.0f, 100.0f, 0.0f},
    {"spread", "Spread", "%", 0.0f, 100.0f, 80.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
// Rotor speeds (Hz) and how long each takes to change speed (s): the light horn is quick, the drum slow.
constexpr double kHornSlow = 0.8, kHornFast = 6.7, kHornInertia = 0.6;
constexpr double kDrumSlow = 0.65, kDrumFast = 5.7, kDrumInertia = 2.5;
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : horn_) d.prepare(static_cast<std::uint32_t>(fs * 0.004) + 4);
        for (auto& d : drum_) d.prepare(static_cast<std::uint32_t>(fs * 0.004) + 4);
        lp_.c = C::make(T::LowPass, fs, 800, 0.7071);
        lp2_.c = lp_.c;
        reset();
    }
    void reset() noexcept override {
        for (auto& d : horn_) d.clear();
        for (auto& d : drum_) d.clear();
        lp_.clear();
        lp2_.clear();
        hornPh_ = 0.0;
        drumPh_ = 0.25;
        const bool fast = p(Speed) >= 0.5f;
        hornHz_ = fast ? kHornFast : kHornSlow;
        drumHz_ = fast ? kDrumFast : kDrumSlow;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const bool fast = p(Speed) >= 0.5f;
        const double hk = 1.0 - std::exp(-1.0 / (fs_ * kHornInertia / 3.0)), dk = 1.0 - std::exp(-1.0 / (fs_ * kDrumInertia / 3.0));
        const float drive = 1.0f + 4.0f * p(Drive) * 0.01f;
        const float bal = p(Balance) * 0.01f, spread = p(Spread) * 0.01f;
        const float hornG = bal < 0 ? 1.0f + bal : 1.0f, drumG = bal > 0 ? 1.0f - bal : 1.0f;
        const float depthH = static_cast<float>(0.00025 * fs_), depthD = static_cast<float>(0.0001 * fs_);   // doppler: ±0.25 ms horn, ±0.1 ms drum
        for (std::uint32_t i = 0; i < n; ++i) {
            hornHz_ += ((fast ? kHornFast : kHornSlow) - hornHz_) * hk;
            drumHz_ += ((fast ? kDrumFast : kDrumSlow) - drumHz_) * dk;
            hornPh_ += hornHz_ / fs_; hornPh_ -= std::floor(hornPh_);
            drumPh_ += drumHz_ / fs_; drumPh_ -= std::floor(drumPh_);
            // the cabinet's tube amp, then the crossover
            const float in = soft(drive * 0.5f * (L[i] + R[i])) / drive;
            const float lo = static_cast<float>(lp2_.tick(lp_.tick(in))), hi = in - lo;
            horn_[0].write(hi);
            drum_[0].write(lo);
            float out[2];
            for (int m = 0; m < 2; ++m) {   // two mics, SPREAD apart around the cabinet
                const double off = m == 0 ? 0.0 : 0.25 + 0.25 * spread;
                const float sh = sine01(hornPh_ + off), sd = sine01(drumPh_ + off);
                const float h = horn_[0].read(depthH * (1.2f + sh)) * (1.0f - 0.35f * (0.5f + 0.5f * sh));   // pitch wobble and level swing
                const float d = drum_[0].read(depthD * (1.2f + sd)) * (1.0f - 0.2f * (0.5f + 0.5f * sd));
                out[m] = hornG * h + drumG * d;
            }
            const float mx = mix_.next(p(Mix) * 0.01f);
            L[i] += mx * (out[0] - L[i]);
            R[i] += mx * (out[1] - R[i]);
        }
    }
private:
    double fs_ = 48000, hornPh_ = 0, drumPh_ = 0, hornHz_ = kHornSlow, drumHz_ = kDrumSlow;
    hc::fx::DelayLine horn_[1], drum_[1];
    hc::fx::Biquad lp_, lp2_;
    hc::fx::Smoothed mix_;
};
} // namespace ro

// ---- AUTO FILTER ----------------------------------------------------------------------------------
namespace af {
using namespace autofilter;
const char* const kTypeN[] = {"LOW-PASS", "BAND-PASS", "HIGH-PASS"};
const ParamInfo kParams[kCount] = {
    {"type", "Type", "", 0, 2, 0, Taper::Choice, kTypeN},
    {"cutoff", "Cutoff", "Hz", 40.0f, 16000.0f, 800.0f, Taper::Log},
    {"resonance", "Resonance", "", 0.5f, 10.0f, 2.0f, Taper::Log},
    {"envelope", "Envelope", "%", -100.0f, 100.0f, 50.0f},
    {"rate", "LFO Rate", "Hz", 0.05f, 10.0f, 0.5f, Taper::Log},
    {"depth", "LFO Depth", "oct", 0.0f, 4.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
// Topology-preserving state-variable filter (stays stable while the cutoff moves every sample).
struct Svf {
    float ic1 = 0, ic2 = 0;
    void clear() noexcept { ic1 = ic2 = 0; }
    void tick(float x, float g, float k, float& lp, float& bp, float& hp) noexcept {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        f_[0].clear();
        f_[1].clear();
        env_ = 0.0f;
        lfo_.ph = 0;
        cut_.prepare(fs_, 30, std::log2(p(Cutoff)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int type = std::clamp(static_cast<int>(std::lround(p(Type))), 0, 2);
        const float k = 1.0f / p(Resonance), envAmt = p(Envelope) * 0.01f * 5.0f, depth = p(Depth);   // ENVELOPE: up to ±5 octaves
        const float att = static_cast<float>(std::exp(-1.0 / (fs_ * 0.005))), rel = static_cast<float>(std::exp(-1.0 / (fs_ * 0.15)));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            env_ = x + (x > env_ ? att : rel) * (env_ - x);
            const double ph = lfo_.tick(p(Rate), fs_);
            // the envelope in octaves: 0 at -40 dBFS and below, 1 at 0 dBFS
            const float envOct = std::clamp((20.0f * std::log10(std::max(env_, 1e-6f)) + 40.0f) / 40.0f, 0.0f, 1.0f);
            const float oct = cut_.next(std::log2(p(Cutoff))) + envAmt * envOct + depth * sine01(ph);
            const float hz = std::clamp(std::exp2(oct), 20.0f, static_cast<float>(fs_ * 0.45));
            const float g = static_cast<float>(std::tan(kPi * hz / fs_));
            const float mx = mix_.next(p(Mix) * 0.01f);
            for (int c = 0; c < 2; ++c) {
                float lp, bp, hp;
                f_[c].tick(io[c][i], g, k, lp, bp, hp);
                const float w = type == 0 ? lp : type == 1 ? bp * k : hp;   // band-pass normalised to 0 dB at the peak
                io[c][i] += mx * (w - io[c][i]);
            }
        }
    }
private:
    double fs_ = 48000;
    Svf f_[2];
    float env_ = 0;
    Lfo lfo_;
    hc::fx::Smoothed cut_, mix_;
};
} // namespace af

} // namespace

std::unique_ptr<hc::fx::Effect> makeChorus() { return std::make_unique<ch::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeFlanger() { return std::make_unique<fl::Impl>(); }
std::unique_ptr<hc::fx::Effect> makePhaser() { return std::make_unique<ph::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeTremolo() { return std::make_unique<tr::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeRotary() { return std::make_unique<ro::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeAutoFilter() { return std::make_unique<af::Impl>(); }

} // namespace hem::rack
