// The classic-style EQs (see ClassicEqs.hpp) on one shared engine.
#include "hem/rack/ClassicEqs.hpp"

#include "hem/rack/Plugins.hpp"
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

constexpr int kMaxSections = 12;
const C kIdentity{1, 0, 0, 0, 0};

// A choice parameter's frequency, also between two entries while it glides (log-interpolated).
// offHz stands in for an OFF entry (0 in the table) during the glide.
double pick(const double* table, int n, float idx, double offHz = 0) noexcept {
    const float x = std::clamp(idx, 0.0f, static_cast<float>(n - 1));
    const int i = std::min(static_cast<int>(x), n - 2 < 0 ? 0 : n - 2);
    const double t = n > 1 ? static_cast<double>(x) - i : 0.0;
    const double a = table[i] > 0 ? table[i] : offHz, b = n > 1 ? (table[i + 1] > 0 ? table[i + 1] : offHz) : a;
    return std::exp(std::log(a) + (std::log(b) - std::log(a)) * t);
}
bool isOff(float idx) noexcept { return idx < 1e-4f; }   // fully at an OFF entry 0
C firstOrder(bool highPass, double fs, double hz) noexcept {
    const double K = std::tan(std::numbers::pi * std::clamp(hz, 5.0, fs * 0.49) / fs);
    if (highPass) return {1.0 / (1.0 + K), -1.0 / (1.0 + K), 0, (K - 1.0) / (K + 1.0), 0};
    return {K / (1.0 + K), K / (1.0 + K), 0, (K - 1.0) / (K + 1.0), 0};
}

enum class Color : std::uint8_t { None, Tube, Iron };
using DesignFn = int (*)(const float* v, double fs, C* out) noexcept;   // always the same count per EQ

struct Spec {
    std::span<const ParamInfo> params;
    DesignFn design;
    int drive = -1, output = -1;   // parameter indexes (-1 = none)
    Color color = Color::None;
};

inline float tubeCurve(float v) noexcept {   // lopsided: mostly 2nd harmonic at normal levels, more 3rd when slammed
    const float y = v + 0.25f * v * v;
    return y / std::cbrt(1.0f + std::fabs(y) * y * y);
}
inline float ironCurve(float v) noexcept {   // symmetric, round: mostly 3rd harmonic
    return v / std::sqrt(1.0f + 0.5f * v * v);
}

double curveOf(const Spec& s, const float* v, double fs, double hz) noexcept {
    C c[kMaxSections];
    const int n = s.design(v, fs, c);
    double db = 0;
    for (int i = 0; i < n; ++i) db += c[i].magnitudeDb(fs, hz);
    return db;
}

class EqEngine final : public hc::fx::Effect, public SpectrumSource {
public:
    explicit EqEngine(const Spec& s) : s_(s) { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return s_.params; }
    std::uint32_t latency() const noexcept override { return s_.drive >= 0 ? OS::latency() : 0; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        if (s_.drive >= 0) {
            for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
            for (auto& v : sat_) v.assign(mb, 0.0f);
            for (auto& d : delay_) d.prepare(OS::latency() + 1);
        }
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }

    void reset() noexcept override {
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        for (auto& o : os_) o.clear();
        for (auto& d : delay_) d.clear();
        for (std::size_t i = 0; i < s_.params.size(); ++i) cur_[i] = p(static_cast<int>(i));
        driveFade_ = s_.drive >= 0 && p(s_.drive) > 0.0f ? 1.0f : 0.0f;
        dc_[0] = dc_[1] = 0.0;
        out_.prepare(fs_, 30, s_.output >= 0 ? hc::fx::dbToGain(p(s_.output)) : 1.0f);
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
            for (int c = 0; c < 2; ++c) {
                float* x = (c == 0 ? L : R) + done;
                auto& f = f_[static_cast<std::size_t>(c)];
                for (int k = 0; k < count_; ++k) {
                    auto& b = f[static_cast<std::size_t>(k)];
                    for (std::uint32_t i = 0; i < m; ++i) x[i] = static_cast<float>(b.tick(x[i]));
                }
            }
            clock_ = (clock_ + m) % kStep;
            done += m;
        }
        if (s_.drive >= 0) drive(L, R, n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = out_.next(s_.output >= 0 ? hc::fx::dbToGain(p(s_.output)) : 1.0f);
            L[i] *= g;
            R[i] *= g;
        }
        after_.push(L, R, n);
    }

private:
    static constexpr std::uint32_t kStep = 32;

    // Every parameter except switches glides to where it is going (30 ms); choices glide in index.
    void glide() noexcept {
        const double k = 1.0 - std::exp(-static_cast<double>(kStep) / (fs_ * 0.03));
        bool moved = false;
        for (std::size_t i = 0; i < s_.params.size(); ++i) {
            const float t = p(static_cast<int>(i));
            float& c = cur_[i];
            if (c == t) continue;
            moved = true;
            if (s_.params[i].taper == Taper::Toggle || std::fabs(t - c) < 1e-4f) c = t;
            else c += static_cast<float>((t - c) * k);
        }
        if (moved) apply();
    }
    void apply() noexcept {
        C c[kMaxSections];
        count_ = s_.design(cur_.data(), fs_, c);
        for (auto& ch : f_)
            for (int i = 0; i < count_; ++i) ch[static_cast<std::size_t>(i)].c = c[i];
    }
    void drive(float* L, float* R, std::uint32_t n) noexcept {
        const float drive = p(s_.drive) * 0.01f;
        for (std::uint32_t i = 0; i < n; ++i) {
            sat_[0][i] = L[i];
            sat_[1][i] = R[i];
            L[i] = delay_[0].process(L[i], OS::latency());
            R[i] = delay_[1].process(R[i], OS::latency());
        }
        const float g = 0.6f + 2.4f * drive;
        const bool tube = s_.color == Color::Tube;
        for (int c = 0; c < 2; ++c) {   // always run, so DRIVE can come up without a click
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(sat_[static_cast<std::size_t>(c)].data(), x4, n);
            if (drive > 0.0f || driveFade_ > 0.0f)
                for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = (tube ? tubeCurve(g * x4[i]) : ironCurve(g * x4[i])) / g;
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        if (drive <= 0.0f && driveFade_ <= 0.0f) return;
        const float step = static_cast<float>(1.0 / (fs_ * 0.010));
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            driveFade_ = drive > 0.0f ? std::min(1.0f, driveFade_ + step) : std::max(0.0f, driveFade_ - step);
            float* io[2] = {L + i, R + i};
            for (int c = 0; c < 2; ++c) {
                dc_[c] += (sat_[static_cast<std::size_t>(c)][i] - dc_[c]) * dcK;
                const float sv = sat_[static_cast<std::size_t>(c)][i] - static_cast<float>(dc_[c]);
                *io[c] += driveFade_ * (sv - *io[c]);
            }
        }
    }

    const Spec& s_;
    double fs_ = 48000.0;
    std::array<float, hc::fx::Effect::kMaxParams> cur_{};
    std::array<std::array<hc::fx::Biquad, kMaxSections>, 2> f_{};
    int count_ = 0;
    std::uint32_t clock_ = 0;
    float driveFade_ = 0.0f;
    double dc_[2] = {0, 0};
    hc::fx::Smoothed out_;
    OS os_[2];
    hc::fx::DelayLine delay_[2];
    std::array<std::vector<float>, 2> up_, sat_;
    SpectrumMeter before_, after_;
};

const char* const kOnOff[] = {"OFF", "ON"};
int idx(float v, int n) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, n - 1); }

// ---- IRON PRE -------------------------------------------------------------------------------------
namespace bp {
using namespace iron;
const char* const kHpfN[] = {"OFF", "50 Hz", "80 Hz", "160 Hz", "300 Hz"};
const char* const kLowN[] = {"35 Hz", "60 Hz", "110 Hz", "220 Hz"};
const char* const kMidN[] = {"360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz"};
const ParamInfo kParams[kCount] = {
    {"hpf", "High-Pass", "", 0, 4, 0, Taper::Choice, kHpfN},
    {"lowfreq", "Low Frequency", "", 0, 3, 1, Taper::Choice, kLowN},
    {"lowgain", "Low Gain", "dB", -16.0f, 16.0f, 0.0f},
    {"midfreq", "Mid Frequency", "", 0, 5, 2, Taper::Choice, kMidN},
    {"midgain", "Mid Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"highgain", "High Gain", "dB", -16.0f, 16.0f, 0.0f},
    {"drive", "Preamp Drive", "%", 0.0f, 100.0f, 20.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
int design(const float* v, double fs, C* o) noexcept {
    if (isOff(v[Hpf])) { o[0] = kIdentity; o[1] = kIdentity; }
    else {   // 18 dB / oct Butterworth: one first-order and one Q = 1 section
        const double hz = pick(kHpfHz, 5, v[Hpf], 12.0);
        o[0] = firstOrder(true, fs, hz);
        o[1] = C::make(T::HighPass, fs, hz, 1.0);
    }
    o[2] = C::make(T::LowShelf, fs, pick(kLowHz, 4, v[LowFreq]), 0.6, v[LowGain]);
    // the mid gets narrower as it is pushed harder, like the inductor original
    o[3] = C::make(T::Bell, fs, pick(kMidHz, 6, v[MidFreq]), 0.7 + 0.04 * std::fabs(v[MidGain]), v[MidGain]);
    o[4] = C::make(T::HighShelf, fs, std::min(kHighHz, fs * 0.45), 0.5, v[HighGain]);
    return 5;
}
int dots(const float* v, double* hz, int max) noexcept {
    const double d[4] = {isOff(v[Hpf]) ? 0.0 : pick(kHpfHz, 5, v[Hpf], 12.0), pick(kLowHz, 4, v[LowFreq]), pick(kMidHz, 6, v[MidFreq]), kHighHz};
    int n = 0;
    for (double x : d) if (x > 0 && n < max) hz[n++] = x;
    return n;
}
const Spec kSpec{kParams, &design, Drive, Output, Color::Iron};
double curve(const float* v, double fs, double hz) noexcept { return curveOf(kSpec, v, fs, hz); }
} // namespace bp

// ---- PUNCH EQ ----------------------------------------------------------------------------------
namespace am {
using namespace punchq;
const char* const kLowN[] = {"50 Hz", "100 Hz", "200 Hz", "300 Hz", "400 Hz"};
const char* const kMidN[] = {"400 Hz", "800 Hz", "1.5 kHz", "3 kHz", "5 kHz"};
const char* const kHighN[] = {"5 kHz", "7.5 kHz", "10 kHz", "12.5 kHz", "15 kHz"};
const char* const kShapeN[] = {"PEAK", "SHELF"};
const ParamInfo kParams[kCount] = {
    {"lowfreq", "Low Frequency", "", 0, 4, 1, Taper::Choice, kLowN},
    {"lowgain", "Low Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"lowshelf", "Low Shape", "", 0, 1, 0, Taper::Toggle, kShapeN},
    {"midfreq", "Mid Frequency", "", 0, 4, 2, Taper::Choice, kMidN},
    {"midgain", "Mid Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"highfreq", "High Frequency", "", 0, 4, 2, Taper::Choice, kHighN},
    {"highgain", "High Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"highshelf", "High Shape", "", 0, 1, 0, Taper::Toggle, kShapeN},
    {"filter", "Filter", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"drive", "Drive", "%", 0.0f, 100.0f, 15.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
// proportional Q: a gentle 2 dB move is broad, a 12 dB move is narrow
double propQ(float gain) noexcept { return 0.45 + 0.11 * std::fabs(gain); }
int design(const float* v, double fs, C* o) noexcept {
    const double lo = pick(kLowHz, 5, v[LowFreq]), mid = pick(kMidHz, 5, v[MidFreq]), hi = std::min(pick(kHighHz, 5, v[HighFreq]), fs * 0.45);
    o[0] = v[LowShelf] >= 0.5f ? C::make(T::LowShelf, fs, lo, 0.7, v[LowGain]) : C::make(T::Bell, fs, lo, propQ(v[LowGain]), v[LowGain]);
    o[1] = C::make(T::Bell, fs, mid, propQ(v[MidGain]), v[MidGain]);
    o[2] = v[HighShelf] >= 0.5f ? C::make(T::HighShelf, fs, hi, 0.7, v[HighGain]) : C::make(T::Bell, fs, hi, propQ(v[HighGain]), v[HighGain]);
    const bool f = v[Filter] >= 0.5f;   // 50 Hz – 15 kHz band-pass, 12 dB / oct each side
    o[3] = f ? C::make(T::HighPass, fs, 50.0, 0.7071) : kIdentity;
    o[4] = f ? C::make(T::LowPass, fs, std::min(15000.0, fs * 0.45), 0.7071) : kIdentity;
    return 5;
}
int dots(const float* v, double* hz, int max) noexcept {
    const double d[3] = {pick(kLowHz, 5, v[LowFreq]), pick(kMidHz, 5, v[MidFreq]), pick(kHighHz, 5, v[HighFreq])};
    int n = 0;
    for (double x : d) if (n < max) hz[n++] = x;
    return n;
}
const Spec kSpec{kParams, &design, Drive, Output, Color::Iron};
double curve(const float* v, double fs, double hz) noexcept { return curveOf(kSpec, v, fs, hz); }
} // namespace am

// ---- MID EQ ---------------------------------------------------------------------------------------
namespace me {
using namespace mideq;
const char* const kLowN[] = {"200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz"};
const char* const kDipN[] = {"200 Hz", "300 Hz", "500 Hz", "700 Hz", "1 kHz", "1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz", "7 kHz"};
const char* const kHighN[] = {"1.5 kHz", "2 kHz", "3 kHz", "4 kHz", "5 kHz"};
const ParamInfo kParams[kCount] = {
    {"lowfreq", "Low Peak Frequency", "", 0, 4, 2, Taper::Choice, kLowN},
    {"lowpeak", "Low Peak", "/10", 0.0f, 10.0f, 0.0f},
    {"dipfreq", "Dip Frequency", "", 0, 10, 4, Taper::Choice, kDipN},
    {"dip", "Dip", "/10", 0.0f, 10.0f, 0.0f},
    {"highfreq", "High Peak Frequency", "", 0, 4, 2, Taper::Choice, kHighN},
    {"highpeak", "High Peak", "/10", 0.0f, 10.0f, 0.0f},
    {"drive", "Tube Drive", "%", 0.0f, 100.0f, 20.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
int design(const float* v, double fs, C* o) noexcept {
    o[0] = C::make(T::Bell, fs, pick(kLowHz, 5, v[LowFreq]), 0.9, 1.0 * v[LowPeak]);     // up to +10 dB
    o[1] = C::make(T::Bell, fs, pick(kDipHz, 11, v[DipFreq]), 1.3, -1.0 * v[Dip]);       // up to -10 dB
    o[2] = C::make(T::Bell, fs, pick(kHighHz, 5, v[HighFreq]), 0.9, 1.0 * v[HighPeak]);  // up to +10 dB
    return 3;
}
int dots(const float* v, double* hz, int max) noexcept {
    const double d[3] = {pick(kLowHz, 5, v[LowFreq]), pick(kDipHz, 11, v[DipFreq]), pick(kHighHz, 5, v[HighFreq])};
    int n = 0;
    for (double x : d) if (n < max) hz[n++] = x;
    return n;
}
const Spec kSpec{kParams, &design, Drive, Output, Color::Tube};
double curve(const float* v, double fs, double hz) noexcept { return curveOf(kSpec, v, fs, hz); }
} // namespace me

// ---- AIR EQ ---------------------------------------------------------------------------------------
namespace ae {
using namespace air;
const char* const kAirN[] = {"2.5 kHz", "5 kHz", "10 kHz", "20 kHz"};
const ParamInfo kParams[kCount] = {
    {"sub", "Sub (10 Hz)", "dB", -10.0f, 10.0f, 0.0f},
    {"b40", "40 Hz", "dB", -10.0f, 10.0f, 0.0f},
    {"b160", "160 Hz", "dB", -10.0f, 10.0f, 0.0f},
    {"b650", "650 Hz", "dB", -10.0f, 10.0f, 0.0f},
    {"b2k5", "2.5 kHz", "dB", -10.0f, 10.0f, 0.0f},
    {"airfreq", "Air Frequency", "", 0, 3, 2, Taper::Choice, kAirN},
    {"airgain", "Air", "dB", 0.0f, 10.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
int design(const float* v, double fs, C* o) noexcept {
    // broad, overlapping bands (Q 0.6): they add up smoothly, like the stepped mastering originals
    for (int b = 0; b < 5; ++b) o[b] = C::make(T::Bell, fs, kBandHz[b], b == 0 ? 0.5 : 0.6, v[b]);
    o[5] = C::make(T::HighShelf, fs, std::min(pick(kAirHz, 4, v[AirFreq]), fs * 0.42), 0.5, v[AirGain]);
    return 6;
}
int dots(const float* v, double* hz, int max) noexcept {
    int n = 0;
    for (int b = 0; b < 5 && n < max; ++b) hz[n++] = kBandHz[b];
    if (n < max) hz[n++] = pick(kAirHz, 4, v[AirFreq]);
    return n;
}
const Spec kSpec{kParams, &design, -1, Output, Color::None};
double curve(const float* v, double fs, double hz) noexcept { return curveOf(kSpec, v, fs, hz); }
} // namespace ae

// ---- GRAPHIC EQ -----------------------------------------------------------------------------------
namespace ge {
using namespace graphic;
const ParamInfo kParams[kCount] = {
    {"b31", "31 Hz", "dB", -12.0f, 12.0f, 0.0f},   {"b62", "62 Hz", "dB", -12.0f, 12.0f, 0.0f},
    {"b125", "125 Hz", "dB", -12.0f, 12.0f, 0.0f}, {"b250", "250 Hz", "dB", -12.0f, 12.0f, 0.0f},
    {"b500", "500 Hz", "dB", -12.0f, 12.0f, 0.0f}, {"b1k", "1 kHz", "dB", -12.0f, 12.0f, 0.0f},
    {"b2k", "2 kHz", "dB", -12.0f, 12.0f, 0.0f},   {"b4k", "4 kHz", "dB", -12.0f, 12.0f, 0.0f},
    {"b8k", "8 kHz", "dB", -12.0f, 12.0f, 0.0f},   {"b16k", "16 kHz", "dB", -12.0f, 12.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
int design(const float* v, double fs, C* o) noexcept {
    for (int b = 0; b < kBands; ++b) o[b] = C::make(T::Bell, fs, std::min(kHz[b], fs * 0.4), 1.4, v[b]);   // constant Q, one octave apart
    return kBands;
}
int dots(const float*, double* hz, int max) noexcept {
    int n = 0;
    for (int b = 0; b < kBands && n < max; ++b) hz[n++] = kHz[b];
    return n;
}
const Spec kSpec{kParams, &design, -1, Output, Color::None};
double curve(const float* v, double fs, double hz) noexcept { return curveOf(kSpec, v, fs, hz); }
} // namespace ge

// ---- TILT -----------------------------------------------------------------------------------------
namespace tl {
using namespace tilt;
const char* const kPivotN[] = {"300 Hz", "650 Hz", "1.2 kHz", "2.5 kHz"};
const char* const kLowN[] = {"OFF", "20 Hz", "30 Hz", "40 Hz", "60 Hz", "80 Hz"};
const char* const kHighN[] = {"OFF", "20 kHz", "16 kHz", "12 kHz", "8 kHz"};
const ParamInfo kParams[kCount] = {
    {"tilt", "Tilt", "dB", -6.0f, 6.0f, 0.0f},
    {"pivot", "Pivot", "", 0, 3, 1, Taper::Choice, kPivotN},
    {"lowcut", "Low Cut", "", 0, 5, 0, Taper::Choice, kLowN},
    {"highcut", "High Cut", "", 0, 4, 0, Taper::Choice, kHighN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
int design(const float* v, double fs, C* o) noexcept {
    // + = brighter: the lows go down by half the amount and the highs up by half, around PIVOT
    const double pv = pick(kPivotHz, 4, v[Pivot]);
    o[0] = C::make(T::LowShelf, fs, pv, 0.5, -0.5 * v[Tilt]);
    o[1] = C::make(T::HighShelf, fs, pv, 0.5, 0.5 * v[Tilt]);
    o[2] = isOff(v[LowCut]) ? kIdentity : C::make(T::HighPass, fs, pick(kLowCutHz, 6, v[LowCut], 8.0), 0.7071);
    o[3] = isOff(v[HighCut]) ? kIdentity : C::make(T::LowPass, fs, std::min(pick(kHighCutHz, 5, v[HighCut], fs * 0.49), fs * 0.45), 0.7071);
    return 4;
}
int dots(const float* v, double* hz, int max) noexcept {
    int n = 0;
    if (n < max) hz[n++] = pick(kPivotHz, 4, v[Pivot]);
    if (!isOff(v[LowCut]) && n < max) hz[n++] = pick(kLowCutHz, 6, v[LowCut], 8.0);
    if (!isOff(v[HighCut]) && n < max) hz[n++] = pick(kHighCutHz, 5, v[HighCut], 24000.0);
    return n;
}
const Spec kSpec{kParams, &design, -1, Output, Color::None};
double curve(const float* v, double fs, double hz) noexcept { return curveOf(kSpec, v, fs, hz); }
} // namespace tl

} // namespace

std::unique_ptr<hc::fx::Effect> makeIronPre() { return std::make_unique<EqEngine>(bp::kSpec); }
std::unique_ptr<hc::fx::Effect> makePunchEq() { return std::make_unique<EqEngine>(am::kSpec); }
std::unique_ptr<hc::fx::Effect> makeMidEq() { return std::make_unique<EqEngine>(me::kSpec); }
std::unique_ptr<hc::fx::Effect> makeAirEq() { return std::make_unique<EqEngine>(ae::kSpec); }
std::unique_ptr<hc::fx::Effect> makeGraphicEq() { return std::make_unique<EqEngine>(ge::kSpec); }
std::unique_ptr<hc::fx::Effect> makeTilt() { return std::make_unique<EqEngine>(tl::kSpec); }

const CurveInfo kIronCurve{&bp::curve, &bp::dots, 18.0f};
const CurveInfo kPunchEqCurve{&am::curve, &am::dots, 15.0f};
const CurveInfo kMidEqCurve{&me::curve, &me::dots, 12.0f};
const CurveInfo kAirCurve{&ae::curve, &ae::dots, 12.0f};
const CurveInfo kGraphicCurve{&ge::curve, &ge::dots, 15.0f};
const CurveInfo kTiltCurve{&tl::curve, &tl::dots, 12.0f};

} // namespace hem::rack
