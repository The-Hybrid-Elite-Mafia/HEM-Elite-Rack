#include "hem/rack/TubeEq.hpp"

#include "hem/rack/Spectrum.hpp"
#include "arsenal/Vintage.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace tubeeq {

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
const char* const kLowN[] = {"20 Hz", "30 Hz", "60 Hz", "100 Hz"};
const char* const kHighN[] = {"3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz"};
const char* const kAttenN[] = {"5 kHz", "10 kHz", "20 kHz"};
const char* const kInOutN[] = {"OUT", "IN"};
const ParamInfo kParams[kCount] = {
    {"lowfreq", "Low Frequency", "", 0, 3, 2, Taper::Choice, kLowN},
    {"lowboost", "Low Boost", "/10", 0.0f, 10.0f, 0.0f},
    {"lowatten", "Low Atten", "/10", 0.0f, 10.0f, 0.0f},
    {"highfreq", "High Frequency", "", 0, 6, 4, Taper::Choice, kHighN},
    {"highboost", "High Boost", "/10", 0.0f, 10.0f, 0.0f},
    {"bandwidth", "Bandwidth", "/10", 1.0f, 10.0f, 5.0f},
    {"highatten", "High Atten", "/10", 0.0f, 10.0f, 0.0f},
    {"attensel", "Atten Select", "", 0, 2, 1, Taper::Choice, kAttenN},
    {"drive", "Tube Drive", "%", 0.0f, 100.0f, 20.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"eqin", "EQ", "", 0, 1, 1, Taper::Toggle, kInOutN},
    {"analog", "Hum", "", 0, 2, 0, Taper::Choice, arsenal::kMainsN},
};
eq::Section fromRbj(const hc::fx::BiquadCoefs& c) { return {c.b0, c.b1, c.b2, c.a1, c.a2}; }
double sectionDb(const eq::Section& c, double fs, double hz) {
    const double w = 2.0 * std::numbers::pi * hz / fs;
    const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
    const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
    const double dr = 1 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
    return 10.0 * std::log10(std::max(nr * nr + ni * ni, 1e-30) / std::max(dr * dr + di * di, 1e-30));
}
} // namespace

Settings settingsFrom(const float* v) noexcept {
    Settings s;
    s.lowHz = kLowHz[std::clamp(static_cast<int>(std::lround(v[LowFreq])), 0, 3)];
    s.lowBoost = v[LowBoost];
    s.lowAtten = v[LowAtten];
    s.highHz = kHighHz[std::clamp(static_cast<int>(std::lround(v[HighFreq])), 0, 6)];
    s.highBoost = v[HighBoost];
    s.bandwidth = v[Bandwidth];
    s.highAtten = v[HighAtten];
    s.attenHz = kAttenHz[std::clamp(static_cast<int>(std::lround(v[AttenSel])), 0, 2)];
    if (v[EqIn] < 0.5f) s.lowBoost = s.lowAtten = s.highBoost = s.highAtten = 0.0;   // EQ out: flat
    return s;
}

Design design(const Settings& s, double fs) noexcept {
    using T = hc::fx::BiquadCoefs::Type;
    using C = hc::fx::BiquadCoefs;
    Design d;
    d.lowBoost = fromRbj(C::make(T::LowShelf, fs, s.lowHz, 0.9, 1.35 * s.lowBoost));            // up to +13.5 dB
    d.lowAtten = fromRbj(C::make(T::LowShelf, fs, s.lowHz * 3.0, 0.5, -0.8 * s.lowAtten));       // up to -8 dB, an octave and a half up
    const double q = 2.5 * std::pow(0.35 / 2.5, (std::clamp(s.bandwidth, 1.0, 10.0) - 1.0) / 9.0);  // sharp 2.5 .. broad 0.35
    d.highBoost = fromRbj(C::make(T::Bell, fs, std::min(s.highHz, fs * 0.45), q, 1.8 * s.highBoost)); // up to +18 dB
    d.highAtten = fromRbj(C::make(T::HighShelf, fs, std::min(s.attenHz, fs * 0.45), 0.6, -2.0 * s.highAtten));  // up to -20 dB
    return d;
}

double curveDb(const Settings& s, double fs, double hz) noexcept {
    const Design d = design(s, fs);
    return sectionDb(d.lowBoost, fs, hz) + sectionDb(d.lowAtten, fs, hz) + sectionDb(d.highBoost, fs, hz) + sectionDb(d.highAtten, fs, hz);
}

} // namespace tubeeq

namespace {
using namespace tubeeq;
using OS = hc::fx::Oversampler4;

// The tube: lopsided soft curve (mostly 2nd harmonic), clean at low levels.
inline float tube(float x, float g) noexcept {
    const float v = g * x;
    const float y = v + 0.25f * v * v;
    return y / std::cbrt(1.0f + std::fabs(y) * y * y) / g;
}

class TubeEqImpl final : public hc::fx::Effect, public SpectrumSource {
public:
    TubeEqImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        for (auto& d : delay_) d.prepare(OS::latency() + 1);
        before_.prepare(fs);
        after_.prepare(fs);
        hum_.prepare(fs);
        reset();
    }

    void reset() noexcept override {
        hum_.clear();
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        for (auto& o : os_) o.clear();
        for (auto& d : delay_) d.clear();
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        cur_ = settingsFrom(v);
        driveFade_ = p(Drive) > 0.0f ? 1.0f : 0.0f;
        dcL_ = dcR_ = 0.0;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        clock_ = 0;
        apply();
        before_.reset();
        after_.reset();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        for (std::uint32_t done = 0; done < n;) {
            if (clock_ == 0) glide();
            const std::uint32_t m = std::min(n - done, kStep - clock_);
            for (std::uint32_t i = 0; i < m; ++i) {
                float* io[2] = {L + done + i, R + done + i};
                for (int c = 0; c < 2; ++c) {
                    auto& f = f_[static_cast<std::size_t>(c)];
                    *io[c] = f[3].tick(f[2].tick(f[1].tick(f[0].tick(*io[c]))));
                }
            }
            clock_ = (clock_ + m) % kStep;
            done += m;
        }
        drive(L, R, n);
        const int mains = std::clamp(static_cast<int>(std::lround(p(Analog))), 0, 2);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
            hum_.tick(mains, 1.0f, L[i], R[i]);
        }
        after_.push(L, R, n);
    }

private:
    static constexpr std::uint32_t kStep = 32;

    void glide() noexcept {
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        Settings t = settingsFrom(v);
        const double k = 1.0 - std::exp(-static_cast<double>(kStep) / (fs_ * 0.03));
        auto lin = [k](double& c, double x) { c += (x - c) * k; if (std::fabs(x - c) < 1e-6) c = x; };
        auto lg = [k](double& c, double x) {
            const double a = std::log(c), b = std::log(x);
            c = std::exp(a + (b - a) * k);
            if (std::fabs(b - a) < 1e-6) c = x;
        };
        lg(cur_.lowHz, t.lowHz);
        lin(cur_.lowBoost, t.lowBoost);
        lin(cur_.lowAtten, t.lowAtten);
        lg(cur_.highHz, t.highHz);
        lin(cur_.highBoost, t.highBoost);
        lin(cur_.bandwidth, t.bandwidth);
        lin(cur_.highAtten, t.highAtten);
        lg(cur_.attenHz, t.attenHz);
        apply();
    }
    void apply() noexcept {
        const Design d = design(cur_, fs_);
        const eq::Section s[4] = {d.lowBoost, d.lowAtten, d.highBoost, d.highAtten};
        for (auto& ch : f_)
            for (int i = 0; i < 4; ++i) {
                hc::fx::BiquadCoefs c;
                c.b0 = s[i].b0; c.b1 = s[i].b1; c.b2 = s[i].b2; c.a1 = s[i].a1; c.a2 = s[i].a2;
                ch[static_cast<std::size_t>(i)].c = c;
            }
    }
    void drive(float* L, float* R, std::uint32_t n) noexcept {
        const float drive = p(Drive) * 0.01f;
        for (std::uint32_t i = 0; i < n; ++i) {
            sat_[0][i] = L[i];
            sat_[1][i] = R[i];
            L[i] = delay_[0].process(L[i], OS::latency());
            R[i] = delay_[1].process(R[i], OS::latency());
        }
        const float g = 0.6f + 2.4f * drive;
        for (int c = 0; c < 2; ++c) {   // always run, so DRIVE can come up without a click
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(sat_[static_cast<std::size_t>(c)].data(), x4, n);
            if (drive > 0.0f || driveFade_ > 0.0f)
                for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = tube(x4[i], g);
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        if (drive <= 0.0f && driveFade_ <= 0.0f) return;
        const float step = static_cast<float>(1.0 / (fs_ * 0.010));
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            driveFade_ = drive > 0.0f ? std::min(1.0f, driveFade_ + step) : std::max(0.0f, driveFade_ - step);
            dcL_ += (sat_[0][i] - dcL_) * dcK;
            dcR_ += (sat_[1][i] - dcR_) * dcK;
            const float sl = sat_[0][i] - static_cast<float>(dcL_), sr = sat_[1][i] - static_cast<float>(dcR_);
            L[i] += driveFade_ * (sl - L[i]);
            R[i] += driveFade_ * (sr - R[i]);
        }
    }

    double fs_ = 48000.0;
    Settings cur_;
    std::array<std::array<hc::fx::Biquad, 4>, 2> f_{};
    std::uint32_t clock_ = 0;
    float driveFade_ = 0.0f;
    double dcL_ = 0, dcR_ = 0;
    hc::fx::Smoothed out_;
    OS os_[2];
    hc::fx::DelayLine delay_[2];
    std::array<std::vector<float>, 2> up_, sat_;
    SpectrumMeter before_, after_;
    arsenal::Hum hum_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTubeEq() { return std::make_unique<TubeEqImpl>(); }

} // namespace hem::rack
