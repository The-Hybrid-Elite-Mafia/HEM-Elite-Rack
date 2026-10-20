#include "hem/rack/Tape.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace {
using namespace tape;
using hc::fx::ParamInfo;
using hc::fx::Taper;
using OS = hc::fx::Oversampler4;

const char* const kSpeedN[] = {"15 IPS", "30 IPS"};
const char* const kOnOff[] = {"OFF", "ON"};
const char* const kCalN[] = {"-18 dBFS", "-14 dBFS", "-10 dBFS", "-6 dBFS"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -12.0f, 18.0f, 0.0f},
    {"speed", "Speed", "", 0, 1, 0, Taper::Choice, kSpeedN},
    {"bump", "Head Bump", "%", 0.0f, 100.0f, 50.0f},
    {"wow", "Wow & Flutter", "%", 0.0f, 100.0f, 0.0f},
    {"hiss", "Hiss", "%", 0.0f, 100.0f, 0.0f},
    {"autotrim", "Auto Trim", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"vucal", "VU Zero", "", 0, 3, 2, Taper::Choice, kCalN},
};
const hc::fx::MeterInfo kMeters[] = {{"VU L", "dB", -40.0f, 6.0f}, {"VU R", "dB", -40.0f, 6.0f}};

// Tape's curve: soft and a little lopsided (even harmonics), tightening smoothly toward the top.
inline float tapeCurve(float x) noexcept {
    const float v = x + 0.06f * x * x;
    return v / std::cbrt(1.0f + std::fabs(v) * v * v);
}

class TapeImpl final : public hc::fx::Effect {
public:
    TapeImpl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return OS::latency() + kTransportDelay; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& d : transport_) d.prepare(kTransportDelay + kMaxWow + 2);
        reset();
    }

    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& d : transport_) d.clear();
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        for (auto& h : hissHp_) h = 0.0f;
        dcL_ = dcR_ = 0.0;
        ms_[0] = ms_[1] = 0.0;
        wowPhase_ = flutterPhase_ = 0.0;
        rng_ = 0x2545F491u;
        speed_ = -1;
        bump_ = p(Bump);
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, outTarget());
        wow_.prepare(fs_, 50, p(Wow) * 0.01f);
        hiss_.prepare(fs_, 50, p(Hiss) * 0.01f);
        design();
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (static_cast<int>(p(Speed)) != speed_ || std::fabs(p(Bump) - bump_) > 1e-4f) {
            bump_ += (p(Bump) - bump_) * std::min(1.0f, static_cast<float>(n) / static_cast<float>(fs_ * 0.03));
            if (std::fabs(p(Bump) - bump_) < 0.01f) bump_ = p(Bump);
            design();
        }
        // 1. input and pre-emphasis
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            L[i] = f_[0][PreEmph].tick(L[i] * g);
            R[i] = f_[1][PreEmph].tick(R[i] * g);
        }
        // 2. the tape itself, 4x oversampled
        float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(io[c], x4, n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = tapeCurve(x4[i]);
            os_[c].down(x4, io[c], n);
        }
        // 3. de-emphasis, head bump, the top end of the speed, DC out (the curve is lopsided)
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) {
                auto& f = f_[static_cast<std::size_t>(c)];
                float x = f[DeEmph].tick(io[c][i]);
                x = f[BumpUp].tick(x);
                x = f[BumpDip].tick(x);
                x = f[Top].tick(x);
                double& dc = c == 0 ? dcL_ : dcR_;
                dc += (x - dc) * dcK;
                io[c][i] = x - static_cast<float>(dc);
            }
        }
        // 4. the transport: wow (0.6 Hz) and flutter (6.5 Hz) move the read point; at 0 % it reads
        //    exactly kTransportDelay behind, so the delay never changes.
        const double wowInc = 2.0 * std::numbers::pi * 0.6 / fs_, flutInc = 2.0 * std::numbers::pi * 6.5 / fs_;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float w = wow_.next(p(Wow) * 0.01f);
            wowPhase_ += wowInc;
            flutterPhase_ += flutInc;
            if (wowPhase_ > 2.0 * std::numbers::pi) wowPhase_ -= 2.0 * std::numbers::pi;
            if (flutterPhase_ > 2.0 * std::numbers::pi) flutterPhase_ -= 2.0 * std::numbers::pi;
            const float mod = w * static_cast<float>(kMaxWow * 0.5 * (0.85 * std::sin(wowPhase_) + 0.15 * std::sin(flutterPhase_)));
            const float d = static_cast<float>(kTransportDelay) + mod;
            for (int c = 0; c < 2; ++c) {
                (void)transport_[c].process(io[c][i], 0);
                io[c][i] = w > 0.0f ? transport_[c].read(d) : transport_[c].read(static_cast<float>(kTransportDelay));
            }
        }
        // 5. hiss, trim, output, VU meters (300 ms)
        const float vuK = static_cast<float>(1.0 - std::exp(-static_cast<double>(1) / (fs_ * 0.3)));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float h = hiss_.next(p(Hiss) * 0.01f);
            const float o = out_.next(outTarget());
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i];
                if (h > 0.0f) {
                    rng_ = rng_ * 1664525u + 1013904223u;
                    const float white = static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;
                    const float bright = white - hissHp_[c];   // tilt toward the top: tape hiss is bright
                    hissHp_[c] = white;
                    x += h * 0.0032f * bright;                 // about -50 dBFS at 100 %
                }
                x *= o;
                io[c][i] = x;
                ms_[c] += (static_cast<double>(x) * x - ms_[c]) * vuK;
            }
        }
        for (int c = 0; c < 2; ++c) publishMeter(c, static_cast<float>(10.0 * std::log10(std::max(2.0 * ms_[c], 1e-10))));
    }

private:
    static constexpr std::uint32_t kMaxWow = 40;   // ±20 samples of drift at 100 % (about 0.4 ms)
    enum F : int { PreEmph, DeEmph, BumpUp, BumpDip, Top, kF };

    float outTarget() const noexcept {
        const float trim = p(AutoTrim) >= 0.5f ? -p(Input) : 0.0f;
        return hc::fx::dbToGain(p(Output) + trim);
    }
    void design() noexcept {
        using T = hc::fx::BiquadCoefs::Type;
        using C = hc::fx::BiquadCoefs;
        speed_ = static_cast<int>(p(Speed));
        const bool slow = speed_ == 0;
        const double b = bump_ * 0.01;
        const C pre = C::make(T::HighShelf, fs_, 4000.0, 0.7071, 5.0), de = C::make(T::HighShelf, fs_, 4000.0, 0.7071, -5.0);
        const C up = C::make(T::Bell, fs_, slow ? 55.0 : 95.0, 1.1, 3.5 * b);
        const C dip = C::make(T::Bell, fs_, slow ? 120.0 : 210.0, 1.4, -1.5 * b);
        const C top = C::make(T::LowPass, fs_, std::min(slow ? 16000.0 : 22000.0, fs_ * 0.45), slow ? 0.6 : 0.7071);
        for (auto& f : f_) {
            f[PreEmph].c = pre;
            f[DeEmph].c = de;
            f[BumpUp].c = up;
            f[BumpDip].c = dip;
            f[Top].c = top;
        }
    }

    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_;
    hc::fx::DelayLine transport_[2];
    std::array<std::array<hc::fx::Biquad, kF>, 2> f_{};
    hc::fx::Smoothed in_, out_, wow_, hiss_;
    double dcL_ = 0, dcR_ = 0, wowPhase_ = 0, flutterPhase_ = 0;
    std::array<double, 2> ms_{};
    std::array<float, 2> hissHp_{};
    std::uint32_t rng_ = 1;
    int speed_ = -1;
    float bump_ = 50.0f;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTape() { return std::make_unique<TapeImpl>(); }

} // namespace hem::rack
