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

} // namespace

std::unique_ptr<hc::fx::Effect> makeTubeSat() { return std::make_unique<ts::Impl>(); }

} // namespace hem::rack
