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

// ---- STEREO WIDTH ---------------------------------------------------------------------------------
namespace wid {
using namespace width;
const char* const kMonoN[] = {"OFF", "80 Hz", "120 Hz", "160 Hz", "250 Hz"};
const char* const kIoN[] = {"L / R", "M / S"};
const char* const kOnOffW[] = {"OFF", "ON"};
const ParamInfo kParams[kCount] = {
    {"width", "Width", "%", 0.0f, 200.0f, 100.0f},
    {"monobass", "Mono Bass", "", 0, 4, 0, Taper::Choice, kMonoN},
    {"balance", "Balance", "%", -100.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"rotation", "Aim", "deg", -45.0f, 45.0f, 0.0f},
    {"asymmetry", "Center Shift", "%", -100.0f, 100.0f, 0.0f},
    {"shuffle", "Low Widen", "%", 0.0f, 100.0f, 0.0f},
    {"shufflefreq", "Widen Below", "Hz", 350.0f, 1400.0f, 600.0f, Taper::Log},
    {"basstrim", "Low Trim", "dB", -12.0f, 12.0f, 0.0f},
    {"inputmode", "Input", "", 0, 1, 0, Taper::Choice, kIoN},
    {"polarityl", "Polarity L", "", 0, 1, 0, Taper::Toggle, kOnOffW},
    {"polarityr", "Polarity R", "", 0, 1, 0, Taper::Toggle, kOnOffW},
    {"swap", "Swap", "", 0, 1, 0, Taper::Toggle, kOnOffW},
    {"metermode", "Meter", "", 0, 1, 0, Taper::Choice, kIoN},
};
const hc::fx::MeterInfo kMeters[] = {{"CORRELATION", "corr", -1.0f, 1.0f, true}, {"WIDTH", "dB", -40.0f, 6.0f},
                                     {"LEFT / MID", "dBFS", -60.0f, 3.0f}, {"RIGHT / SIDE", "dBFS", -60.0f, 3.0f}};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& b : hp_) b.clear();
        shuf_.clear();
        shuffle_ = shufHz_ = -1.0f;
        rot_.prepare(fs_, 30, p(Rotation));
        asym_.prepare(fs_, 30, p(Asymmetry) * 0.01f);
        mono_ = -1;
        monoMix_ = p(MonoBass) >= 0.5f ? 1.0f : 0.0f;
        w_.prepare(fs_, 30, p(Width) * 0.01f);
        bal_.prepare(fs_, 30, p(Balance) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        lr_ = ll_ = rr_ = mm_ = ss_ = 0.0;
        trim_.clear();
        trimKey_ = {-1.0f, -100.0f};
        pk_[0].clear();
        pk_[1].clear();
        publishMeter(0, 1.0f);
        publishMeter(1, -40.0f);
        publishMeter(2, -100.0f);
        publishMeter(3, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int mono = std::clamp(static_cast<int>(p(MonoBass)), 0, 4);
        if (mono != mono_ && mono > 0) {   // 24 dB / oct on the sides: two Butterworth sections
            const C c = C::make(T::HighPass, fs_, kMonoHz[mono], 0.7071);
            hp_[0].c = c;
            hp_[1].c = c;
        }
        if (mono > 0) mono_ = mono;
        if (p(Shuffle) != shuffle_ || p(ShuffleFreq) != shufHz_) {   // SHUFFLE: the sides under SHUFFLE FREQ up, as much as 6 dB
            shuffle_ = p(Shuffle);
            shufHz_ = p(ShuffleFreq);
            shuf_.c = C::make(T::LowShelf, fs_, shufHz_, 0.7071, 6.0 * shuffle_ * 0.01);
        }
        if (p(BassTrim) != trimKey_[1] || p(ShuffleFreq) != trimKey_[0]) {   // BASS TRIM: the middle under SHUFFLE FREQ
            trimKey_ = {p(ShuffleFreq), p(BassTrim)};
            trim_.c = C::make(T::LowShelf, fs_, trimKey_[0], 0.7071, trimKey_[1]);
        }
        const bool msIn = p(InputMode) >= 0.5f, swap = p(Swap) >= 0.5f, msMeter = p(MeterMode) >= 0.5f;
        const float pl = p(PolarityL) >= 0.5f ? -1.0f : 1.0f, pr = p(PolarityR) >= 0.5f ? -1.0f : 1.0f;
        float peak[2] = {0.0f, 0.0f};
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const double mk = 1.0 - std::exp(-static_cast<double>(1) / (fs_ * 0.3));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float w = w_.next(p(Width) * 0.01f), b = bal_.next(p(Balance) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            monoMix_ = mono > 0 ? std::min(1.0f, monoMix_ + step) : std::max(0.0f, monoMix_ - step);
            // the input: polarity per side, SWAP, and M / S input (left carries the middle, right the sides)
            float il = L[i] * pl, ir = R[i] * pr;
            if (swap) std::swap(il, ir);
            if (msIn) {
                const float mid = il, side = ir;
                il = mid + side;
                ir = mid - side;
            }
            const float m = trim_.tick(0.5f * (il + ir));
            float s = 0.5f * (il - ir);
            const float shp = hp_[1].tick(hp_[0].tick(s));   // the sides without their low end
            s += monoMix_ * (shp - s);
            s = shuf_.tick(s) * w;
            s += 0.5f * asym_.next(p(Asymmetry) * 0.01f) * m;   // ASYMMETRY: some of the middle leans to one side
            float l = (m + s) * o, r = (m - s) * o;
            const float th = rot_.next(p(Rotation)) * static_cast<float>(std::numbers::pi / 180.0);
            if (th != 0.0f) {   // ROTATION: the whole picture turns (plus = toward the right)
                const float c = std::cos(th), sn = std::sin(th), l0 = l;
                l = l0 * c - r * sn;
                r = r * c + l0 * sn;
            }
            if (b > 0.0f) l *= 1.0f - b; else r *= 1.0f + b;   // balance: turn the other side down
            L[i] = l;
            R[i] = r;
            peak[0] = std::max(peak[0], std::fabs(msMeter ? 0.5f * (l + r) : l));
            peak[1] = std::max(peak[1], std::fabs(msMeter ? 0.5f * (l - r) : r));
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
        publishMeter(2, pk_[0].push(20.0f * std::log10(std::max(peak[0], 1e-9f)), n, fs_));
        publishMeter(3, pk_[1].push(20.0f * std::log10(std::max(peak[1], 1e-9f)), n, fs_));
    }
private:
    double fs_ = 48000;
    hc::fx::Biquad hp_[2], shuf_, trim_;
    float shuffle_ = -1.0f, shufHz_ = -1.0f;
    std::array<float, 2> trimKey_{-1.0f, -100.0f};
    struct Hold {   // the highest peak, held 1.5 s, then falling 20 dB/s
        float db = -100.0f;
        double age = 0;
        void clear() noexcept { db = -100.0f; age = 0; }
        float push(float v, std::uint32_t n, double fs) noexcept {
            age += n / fs;
            if (v >= db) { db = v; age = 0; }
            else if (age > 1.5) db = std::max(v, db - 20.0f * static_cast<float>(n / fs));
            return std::max(db, -100.0f);
        }
    } pk_[2];
    hc::fx::Smoothed rot_, asym_;
    int mono_ = -1;
    float monoMix_ = 0;
    hc::fx::Smoothed w_, bal_, out_;
    double lr_ = 0, ll_ = 0, rr_ = 0, mm_ = 0, ss_ = 0;
};
} // namespace wid

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

} // namespace

std::unique_ptr<hc::fx::Effect> makeStereoWidth() { return std::make_unique<wid::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeExciter() { return std::make_unique<exc::Impl>(); }

} // namespace hem::rack
