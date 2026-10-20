// COIL — spring reverb (see Comeback.hpp).
//
// Each side runs the tank's two or three springs in parallel. A spring is a loop: a delay (its length),
// a chain of first-order all-passes (the dispersion that makes the "drip": high frequencies arrive at
// different times than low ones), a low-pass (the spring's bandwidth) and a gain (the decay). The right
// side's springs are a little longer than the left's, as in a real stereo tank with two transducers.
// In order: DRIVE and HEADROOM (a soft clipper), PRE-DELAY, the springs, the tank's low cut, TREBLE / BASS,
// MIX with the (undelayed) dry sound, OUTPUT, and WIDTH.
#include "hem/rack/Comeback.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace coil {
namespace {
using namespace hideout;

const char* const kTankN[] = {"DUO SHORT", "DUO LONG", "DUO GRIT", "TRIO CLASSIC", "TRIO DEEP", "TRIO STEEL", "TRIO SOFT"};
const char* const kLengthN[] = {"SHORT", "MEDIUM", "LONG"};
const char* const kWidthN[] = {"STEREO", "MONO"};
const ParamInfo kParams[kCount] = {
    {"drive", "Drive", "", 0.0f, 100.0f, 30.0f},
    {"predelay", "Pre-Delay", "ms", 0.0f, 600.0f, 0.0f},
    {"headroom", "Headroom", "", 0.0f, 100.0f, 30.0f},
    {"tank", "Tank", "", 0, 6, 3, Taper::Choice, kTankN},
    {"length", "Length", "", 0, 2, 1, Taper::Choice, kLengthN},
    {"treble", "Treble", "dB", -18.0f, 18.0f, 0.0f},
    {"bass", "Bass", "dB", -18.0f, 18.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 30.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"width", "Width", "", 0, 1, 0, Taper::Toggle, kWidthN},
};

struct TankSpec {
    int springs;
    float ms[3];        // loop lengths (left side)
    float t60;          // MEDIUM decay (s)
    float drip;         // all-pass coefficient: more = more dispersion
    float lowPassHz;    // the springs' bandwidth
    float lowCutHz;     // the transducer's low end
};
constexpr TankSpec kTanks[] = {
    {2, {33.0f, 41.0f, 0.0f}, 1.6f, 0.55f, 4500.0f, 120.0f},
    {2, {47.0f, 59.0f, 0.0f}, 2.6f, 0.62f, 4000.0f, 100.0f},
    {2, {37.0f, 44.0f, 0.0f}, 1.9f, 0.70f, 5500.0f, 300.0f},
    {3, {31.0f, 37.0f, 43.0f}, 2.2f, 0.60f, 4200.0f, 110.0f},
    {3, {41.0f, 53.0f, 61.0f}, 3.0f, 0.50f, 3200.0f, 80.0f},
    {3, {29.0f, 35.0f, 47.0f}, 2.4f, 0.72f, 6000.0f, 140.0f},
    {3, {39.0f, 45.0f, 57.0f}, 2.8f, 0.45f, 2800.0f, 90.0f},
};
constexpr float kLengthX[] = {0.5f, 1.0f, 1.8f};
constexpr int kStages = 16;
constexpr double kMaxPreMs = 600.0, kMaxLoopMs = 70.0;
constexpr float kBassHz = 200.0f, kTrebleHz = 3000.0f;

struct Spring {
    hc::fx::DelayLine loop;
    std::array<float, kStages> ap{};
    float lp = 0.0f, len = 1000.0f, g = 0.0f;
    void clear() noexcept {
        loop.clear();
        ap.fill(0.0f);
        lp = 0.0f;
    }
    float tick(float x, float drip, float lpA) noexcept {
        const float back = loop.read(len - 1.0f);
        lp = back + lpA * (lp - back);
        float v = x + g * lp;
        for (int k = 0; k < kStages; ++k) {
            const float y = -drip * v + ap[static_cast<std::size_t>(k)];
            ap[static_cast<std::size_t>(k)] = v + drip * y;
            v = y;
        }
        loop.write(v);
        return lp;   // what reaches the far end: the sound arrives after one trip down the spring
    }
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& side : springs_)
            for (auto& s : side) s.loop.prepare(static_cast<std::uint32_t>(kMaxLoopMs * 1.1 * 0.001 * fs) + 8);
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(kMaxPreMs * 0.001 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        for (auto& side : springs_)
            for (auto& s : side) s.clear();
        for (auto& d : pre_) d.clear();
        for (auto& ch : f_)
            for (auto& b : ch) b.clear();
        key_.fill(-1e9f);
        design();
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        drive_.prepare(fs_, 30, driveGain());
        room_.prepare(fs_, 30, headroom());
        preS_.prepare(fs_, 60, preSamples());
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const TankSpec& t = kTanks[tank_];
        const float lpA = static_cast<float>(std::exp(-2.0 * std::numbers::pi * t.lowPassHz / fs_));
        const float norm = 0.5f / std::sqrt(static_cast<float>(t.springs));
        const bool mono = p(Width) >= 0.5f;
        const float wMix = p(Mix) * 0.01f, wOut = hc::fx::dbToGain(p(Output)), wDrive = driveGain(), wRoom = headroom(), wPre = preSamples();
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dg = drive_.next(wDrive), h = room_.next(wRoom), pd = preS_.next(wPre);
            const float mx = mix_.next(wMix), og = out_.next(wOut);
            const float dry[2] = {L[i], R[i]};
            float wet[2];
            for (int c = 0; c < 2; ++c) {
                // DRIVE into the clipper; HEADROOM sets where it starts rounding (small signals: level unchanged)
                const float x = h * std::tanh(dg * dry[c] / h);
                pre_[c].write(x);
                const float in = pre_[c].read(pd) * norm;
                float sum = 0.0f;
                for (int k = 0; k < t.springs; ++k) sum += springs_[c][static_cast<std::size_t>(k)].tick(in, t.drip, lpA) * (k & 1 ? -1.0f : 1.0f);
                auto& f = f_[static_cast<std::size_t>(c)];
                wet[c] = f[2].tick(f[1].tick(f[0].tick(sum)));
            }
            if (mono) wet[1] = wet[0];
            L[i] = og * (dry[0] + mx * (wet[0] - dry[0]));
            R[i] = og * (dry[1] + mx * (wet[1] - dry[1]));
        }
    }

private:
    float driveGain() const noexcept { return hc::fx::dbToGain(0.24f * p(Drive) - 6.0f); }   // -6 .. +18 dB into the tank
    float headroom() const noexcept { return hc::fx::dbToGain(6.0f - 0.24f * p(Headroom)); }  // the clipper's ceiling +6 .. -18 dBFS
    float preSamples() const noexcept { return static_cast<float>(p(PreDelay) * 0.001 * fs_); }
    void design() noexcept {
        const std::array<float, 4> now{p(Tank), p(Length), p(Treble), p(Bass)};
        if (now == key_) return;
        const int oldTank = tank_;
        key_ = now;
        tank_ = std::clamp(static_cast<int>(std::lround(p(Tank))), 0, 6);
        const TankSpec& t = kTanks[tank_];
        const float t60 = t.t60 * kLengthX[std::clamp(static_cast<int>(std::lround(p(Length))), 0, 2)];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 3; ++k) {
                auto& s = springs_[c][static_cast<std::size_t>(k)];
                if (tank_ != oldTank) s.clear();
                const double ms = (k < t.springs ? t.ms[k] : 30.0) * (c == 0 ? 1.0 : 1.07);
                s.len = static_cast<float>(ms * 0.001 * fs_);
                s.g = static_cast<float>(std::pow(10.0, -3.0 * ms * 0.001 / t60));
            }
        for (auto& ch : f_) {
            ch[0].c = C::make(T::HighPass, fs_, t.lowCutHz, 0.7071);
            ch[1].c = C::make(T::LowShelf, fs_, kBassHz, 0.7071, p(Bass));
            ch[2].c = C::make(T::HighShelf, fs_, kTrebleHz, 0.7071, p(Treble));
        }
    }

    double fs_ = 48000.0;
    std::array<std::array<Spring, 3>, 2> springs_;
    hc::fx::DelayLine pre_[2];
    std::array<std::array<hc::fx::Biquad, 3>, 2> f_{};
    std::array<float, 4> key_{};
    int tank_ = 3;
    hc::fx::Smoothed mix_, out_, drive_, room_, preS_;
};

} // namespace
} // namespace coil

std::unique_ptr<hc::fx::Effect> makeCoil() { return std::make_unique<coil::Impl>(); }

} // namespace hem::rack
