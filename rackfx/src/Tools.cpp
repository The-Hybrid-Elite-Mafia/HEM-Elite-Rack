// DE-ESSER, STEREO WIDTH, CLIPPER, EXCITER, TRANSIENT (see Tools.hpp).
#include "hem/rack/Tools.hpp"

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
using OS = hc::fx::Oversampler4;
using T = hc::fx::BiquadCoefs::Type;
using C = hc::fx::BiquadCoefs;
const char* const kOnOff[] = {"OFF", "ON"};

inline float dbOf(float lin) noexcept { return 20.0f * std::log10(std::max(lin, 1e-9f)); }
inline float soft(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }

// ---- DE-ESSER -------------------------------------------------------------------------------------
namespace des {
using namespace deesser;
const char* const kModeN[] = {"SPLIT", "WIDE"};
const ParamInfo kParams[kCount] = {
    {"freq", "Frequency", "Hz", 3000.0f, 12000.0f, 6500.0f, Taper::Log},
    {"threshold", "Threshold", "dB", -50.0f, 0.0f, -24.0f},
    {"range", "Range", "dB", 0.0f, 20.0f, 8.0f},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"listen", "Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& b : lp_) b.clear();
        for (auto& b : det_) b.clear();
        freq_ = -1.0f;
        env_ = 0.0f;
        gr_ = 0.0f;
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Freq) != freq_) {
            freq_ = p(Freq);
            const C c = C::make(T::LowPass, fs_, freq_, 0.7071), h = C::make(T::HighPass, fs_, freq_, 0.7071);
            for (auto& b : lp_) b.c = c;
            for (auto& b : det_) b.c = h;
        }
        const float thr = p(Threshold), range = p(Range);
        const bool wide = p(Mode) >= 0.5f, listen = p(Listen) >= 0.5f;
        const float att = static_cast<float>(std::exp(-1.0 / (fs_ * 0.0005))), rel = static_cast<float>(std::exp(-1.0 / (fs_ * 0.060)));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            // the ess band = the input minus its low-passed part, so band + rest is exactly the input
            // and turning the band down never leaves a phase notch or bump around FREQ
            const float hl = L[i] - lp_[0].tick(L[i]), hr = R[i] - lp_[1].tick(R[i]);
            // what the detector hears (and LISTEN plays): a steep 24 dB / oct high-pass, so bass never triggers it
            const float dl = det_[1].tick(det_[0].tick(L[i])), dr = det_[3].tick(det_[2].tick(R[i]));
            const float lv = std::max(std::fabs(dl), std::fabs(dr));
            env_ = lv + (lv > env_ ? att : rel) * (env_ - lv);
            const float over = dbOf(env_) - thr;
            const float target = over > 0.0f ? -std::min(range, over * 0.75f) : 0.0f;   // 4:1 above the threshold, at most RANGE
            gr_ = target + (target < gr_ ? att : rel) * (gr_ - target);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_);
            if (listen) { L[i] = dl; R[i] = dr; continue; }
            if (wide) { L[i] *= g; R[i] *= g; }
            else { L[i] += hl * (g - 1.0f); R[i] += hr * (g - 1.0f); }   // only the ess band goes down
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }
private:
    double fs_ = 48000;
    hc::fx::Biquad lp_[2], det_[4];
    float freq_ = -1, env_ = 0, gr_ = 0;
};
} // namespace des

// ---- STEREO WIDTH ---------------------------------------------------------------------------------
namespace wid {
using namespace width;
const char* const kMonoN[] = {"OFF", "80 Hz", "120 Hz", "160 Hz", "250 Hz"};
const ParamInfo kParams[kCount] = {
    {"width", "Width", "%", 0.0f, 200.0f, 100.0f},
    {"monobass", "Mono Bass", "", 0, 4, 0, Taper::Choice, kMonoN},
    {"balance", "Balance", "%", -100.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"CORRELATION", "corr", -1.0f, 1.0f, true}, {"WIDTH", "dB", -40.0f, 6.0f}};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& b : hp_) b.clear();
        mono_ = -1;
        monoMix_ = p(MonoBass) >= 0.5f ? 1.0f : 0.0f;
        w_.prepare(fs_, 30, p(Width) * 0.01f);
        bal_.prepare(fs_, 30, p(Balance) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        lr_ = ll_ = rr_ = mm_ = ss_ = 0.0;
        publishMeter(0, 1.0f);
        publishMeter(1, -40.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int mono = std::clamp(static_cast<int>(p(MonoBass)), 0, 4);
        if (mono != mono_ && mono > 0) {   // 24 dB / oct on the sides: two Butterworth sections
            const C c = C::make(T::HighPass, fs_, kMonoHz[mono], 0.7071);
            hp_[0].c = c;
            hp_[1].c = c;
        }
        if (mono > 0) mono_ = mono;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const double mk = 1.0 - std::exp(-static_cast<double>(1) / (fs_ * 0.3));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float w = w_.next(p(Width) * 0.01f), b = bal_.next(p(Balance) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            monoMix_ = mono > 0 ? std::min(1.0f, monoMix_ + step) : std::max(0.0f, monoMix_ - step);
            const float m = 0.5f * (L[i] + R[i]);
            float s = 0.5f * (L[i] - R[i]);
            const float shp = hp_[1].tick(hp_[0].tick(s));   // the sides without their low end
            s += monoMix_ * (shp - s);
            s *= w;
            float l = (m + s) * o, r = (m - s) * o;
            if (b > 0.0f) l *= 1.0f - b; else r *= 1.0f + b;   // balance: turn the other side down
            L[i] = l;
            R[i] = r;
            lr_ += (static_cast<double>(l) * r - lr_) * mk;
            ll_ += (static_cast<double>(l) * l - ll_) * mk;
            rr_ += (static_cast<double>(r) * r - rr_) * mk;
            const double mo = 0.5 * (l + r), so = 0.5 * (l - r);
            mm_ += (mo * mo - mm_) * mk;
            ss_ += (so * so - ss_) * mk;
        }
        const double den = std::sqrt(ll_ * rr_);
        publishMeter(0, den > 1e-12 ? static_cast<float>(std::clamp(lr_ / den, -1.0, 1.0)) : 1.0f);
        publishMeter(1, static_cast<float>(10.0 * std::log10(std::max(ss_, 1e-12) / std::max(mm_, 1e-12))));
    }
private:
    double fs_ = 48000;
    hc::fx::Biquad hp_[2];
    int mono_ = -1;
    float monoMix_ = 0;
    hc::fx::Smoothed w_, bal_, out_;
    double lr_ = 0, ll_ = 0, rr_ = 0, mm_ = 0, ss_ = 0;
};
} // namespace wid

// ---- CLIPPER --------------------------------------------------------------------------------------
namespace clp {
using namespace clipper;
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", 0.0f, 24.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -12.0f, 0.0f, -0.3f},
    {"shape", "Shape", "%", 0.0f, 100.0f, 30.0f},
    {"output", "Output", "dB", -12.0f, 6.0f, 0.0f},
};
// Clips |x| at 1: hard (k = 0) or with a soft knee that starts at 1 - k below it.
inline float clipShape(float x, float k) noexcept {
    const float a = std::fabs(x);
    if (k <= 1e-4f || a <= 1.0f - k) return std::clamp(x, -1.0f, 1.0f);
    const float t = std::min((a - (1.0f - k)) / (2.0f * k), 1.0f);   // 0..1 across the knee
    const float y = (1.0f - k) + k * (2.0f * t - t * t) ;            // quadratic ease into 1
    return std::copysign(std::min(y, 1.0f), x);
}
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        up_.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        ceil_.prepare(fs_, 30, hc::fx::dbToGain(p(Ceiling)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        cl_ = ceil_.cur;
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        float* io[2] = {L, R};
        float peakIn = 0.0f, peakOut = 0.0f;
        const float k = p(Shape) * 0.005f;   // knee 0 .. 0.5 (-6 dB) below the ceiling
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            L[i] *= g;
            R[i] *= g;
        }
        for (int c = 0; c < 2; ++c) {
            os_[c].up(io[c], up_.data(), n);
            ceil_.cur = cl_;   // both channels step the ceiling glide identically
            for (std::uint32_t i = 0; i < n; ++i) {
                const float ce = ceil_.next(hc::fx::dbToGain(p(Ceiling)));
                for (int q = 0; q < OS::kFactor; ++q) {
                    float& v = up_[i * OS::kFactor + static_cast<std::uint32_t>(q)];
                    const float y = clipShape(v / ce, k) * ce;
                    peakIn = std::max(peakIn, std::fabs(v));
                    peakOut = std::max(peakOut, std::fabs(y));
                    v = y;
                }
            }
            os_[c].down(up_.data(), io[c], n);
            // the down-sampling filter can ring a little past the ceiling on hard clipping: catch it
            ceil_.cur = cl_;
            for (std::uint32_t i = 0; i < n; ++i) {
                const float ce = ceil_.next(hc::fx::dbToGain(p(Ceiling)));
                io[c][i] = std::clamp(io[c][i], -ce, ce);
            }
        }
        cl_ = ceil_.cur;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= o;
            R[i] *= o;
        }
        publishReduction(peakIn > 0.0f ? std::min(0.0f, dbOf(peakOut) - dbOf(peakIn)) : 0.0f);   // how far the peaks are shaved
    }
private:
    double fs_ = 48000;
    OS os_[2];
    std::vector<float> up_;
    hc::fx::Smoothed in_, ceil_, out_;
    float cl_ = 1.0f;
};
} // namespace clp

// ---- EXCITER --------------------------------------------------------------------------------------
namespace exc {
using namespace exciter;
const char* const kModeN[] = {"TUBE", "TAPE"};
const ParamInfo kParams[kCount] = {
    {"freq", "Frequency", "Hz", 1000.0f, 10000.0f, 3000.0f, Taper::Log},
    {"drive", "Drive", "%", 0.0f, 100.0f, 50.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 25.0f},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
};
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        up_.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& b : band_) b.assign(mb, 0.0f);
        before_.prepare(fs);
        after_.prepare(fs);
        for (auto& d : dry_) d.prepare(OS::latency() + 1);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& d : dry_) d.clear();
        for (auto& f : pre_) f.clear();
        for (auto& f : post_) f.clear();
        freq_ = -1.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        before_.reset();
        after_.reset();
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Freq) != freq_) {
            freq_ = p(Freq);
            const C pre = C::make(T::HighPass, fs_, freq_, 0.7071), post = C::make(T::HighPass, fs_, freq_ * 1.4, 0.7071);
            for (auto& f : pre_) f.c = pre;
            for (auto& f : post_) f.c = post;
        }
        const float g = 1.0f + 9.0f * p(Drive) * 0.01f;
        const bool tube = p(Mode) < 0.5f;
        float* io[2] = {L, R};
        before_.push(L, R, n);
        for (int c = 0; c < 2; ++c) {
            auto& b = band_[static_cast<std::size_t>(c)];
            for (std::uint32_t i = 0; i < n; ++i) b[i] = pre_[c].tick(io[c][i]);
            os_[c].up(b.data(), up_.data(), n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) {
                const float v = g * up_[i];
                up_[i] = (tube ? soft(v + 0.5f * v * v) : soft(v)) / g;   // TUBE adds even harmonics, TAPE odd
            }
            os_[c].down(up_.data(), b.data(), n);
            for (std::uint32_t i = 0; i < n; ++i) b[i] = post_[c].tick(b[i]);   // only the new top end, nothing below
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = mix_.next(p(Mix) * 0.01f);
            for (int c = 0; c < 2; ++c) io[c][i] = dry_[c].process(io[c][i], OS::latency()) + m * band_[static_cast<std::size_t>(c)][i];
        }
        after_.push(L, R, n);
    }
private:
    double fs_ = 48000;
    OS os_[2];
    std::vector<float> up_;
    std::array<std::vector<float>, 2> band_;
    hc::fx::DelayLine dry_[2];
    hc::fx::Biquad pre_[2], post_[2];
    float freq_ = -1;
    hc::fx::Smoothed mix_;
    SpectrumMeter before_, after_;
};
} // namespace exc

// ---- TRANSIENT ------------------------------------------------------------------------------------
namespace trn {
using namespace transient;
const hc::fx::MeterInfo kMeters[] = {{"GAIN", "dB", -24.0f, 24.0f, true}};
const ParamInfo kParams[kCount] = {
    {"attack", "Attack", "%", -100.0f, 100.0f, 0.0f},
    {"sustain", "Sustain", "%", -100.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
struct Env {
    float a = 0, r = 0, v = 0;
    void set(double fs, double attMs, double relMs) { a = static_cast<float>(std::exp(-1.0 / (fs * attMs * 0.001))); r = static_cast<float>(std::exp(-1.0 / (fs * relMs * 0.001))); }
    float tick(float x) noexcept { v = x + (x > v ? a : r) * (v - x); return v; }
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fastA_.set(fs, 0.5, 30);
        slowA_.set(fs, 15, 30);
        longS_.set(fs, 0.5, 400);
        shortS_.set(fs, 0.5, 40);
        reset();
    }
    void reset() noexcept override {
        fastA_.v = slowA_.v = longS_.v = shortS_.v = 0.0f;
        gain_ = 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishMeter(0, 0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float at = p(Attack) * 0.01f, su = p(Sustain) * 0.01f;
        const float smooth = static_cast<float>(std::exp(-1.0 / (fs_ * 0.001)));
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            // the hit: a fast follower runs ahead of a slow one at every attack
            const float onset = std::max(0.0f, dbOf(fastA_.tick(x)) - dbOf(slowA_.tick(x)));
            // the tail: a long-release follower stays up while a short one falls
            const float tail = std::max(0.0f, dbOf(longS_.tick(x)) - dbOf(shortS_.tick(x)));
            const float target = std::clamp(at * onset + su * std::min(tail, 24.0f), -24.0f, 24.0f);
            gain_ = target + smooth * (gain_ - target);
            const float g = hc::fx::dbToGain(gain_) * out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
            if (std::fabs(gain_) > std::fabs(most)) most = gain_;
        }
        publishMeter(0, most);   // the biggest move in this block, up or down
    }
private:
    double fs_ = 48000;
    Env fastA_, slowA_, longS_, shortS_;
    float gain_ = 0;
    hc::fx::Smoothed out_;
};
} // namespace trn

} // namespace

std::unique_ptr<hc::fx::Effect> makeDeEsser() { return std::make_unique<des::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeStereoWidth() { return std::make_unique<wid::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeClipper() { return std::make_unique<clp::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeExciter() { return std::make_unique<exc::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeTransient() { return std::make_unique<trn::Impl>(); }

} // namespace hem::rack
