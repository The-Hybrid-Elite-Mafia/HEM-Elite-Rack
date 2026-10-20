// CUTTHROAT (high-pass + low-pass, left and right sets) and TUNNEL VISION (one-knob filter) (see ToneShop.hpp).
//
// Both glide: the frequencies move toward the knob (log steps, about 20 ms) and the filters are
// redesigned every 2 samples while they move (bigger steps ring near the top of the range). CUTTHROAT's filters are 3rd-order Butterworth (a 1st-
// order section and a 2nd-order one with Q 1); switching one on or off crossfades it in 20 ms.
#include "hem/rack/ToneShop.hpp"

#include "hem/rack/Spectrum.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace {
using namespace tone;

// 1st-order sections as biquads (bilinear transform)
C onePole(bool high, double fs, double hz) noexcept {
    const double k = std::tan(std::numbers::pi * std::clamp(hz, 5.0, fs * 0.49) / fs), n = 1.0 / (k + 1.0);
    C c;
    c.b0 = high ? n : k * n;
    c.b1 = high ? -n : k * n;
    c.b2 = 0.0;
    c.a1 = (k - 1.0) * n;
    c.a2 = 0.0;
    return c;
}
// an 18 dB / oct section pair
void third(bool high, double fs, double hz, C& a, C& b) noexcept {
    a = onePole(high, fs, hz);
    b = C::make(high ? T::HighPass : T::LowPass, fs, std::clamp(hz, 5.0, fs * 0.49), 1.0);
}

// ---- CUTTHROAT ---------------------------------------------------------------------------------
namespace ct {
using namespace cutthroat;
#define HEM_SIDE(s, S)                                                                                  \
    {s "hpon", S "High-Pass", "", 0, 1, 1, Taper::Toggle, kOnOff},                                      \
    {s "hpfreq", S "High-Pass Freq", "Hz", 16.0f, 21357.0f, 16.0f, Taper::Log},                        \
    {s "lpon", S "Low-Pass", "", 0, 1, 1, Taper::Toggle, kOnOff},                                       \
    {s "lpfreq", S "Low-Pass Freq", "Hz", 16.0f, 21357.0f, 16000.0f, Taper::Log}
const ParamInfo kParams[kCount] = {
    HEM_SIDE("l", "L "), HEM_SIDE("r", "R "),
    {"link", "Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
};
#undef HEM_SIDE

// side 1 runs the left set while linked
int setOf(const float* v, int side) noexcept { return side == 1 && v[Link] < 0.5f ? 1 : 0; }

double sideDb(const float* v, int side, double fs, double hz) noexcept {
    const int o = setOf(v, side) * kSide;
    double db = 0.0;
    C a, b;
    if (v[o + HpOn] >= 0.5f) {
        third(true, fs, v[o + HpFreq], a, b);
        db += a.magnitudeDb(fs, hz) + b.magnitudeDb(fs, hz);
    }
    if (v[o + LpOn] >= 0.5f) {
        third(false, fs, v[o + LpFreq], a, b);
        db += a.magnitudeDb(fs, hz) + b.magnitudeDb(fs, hz);
    }
    return db;
}
int dots(const float* v, double* hz, int max) noexcept {
    int n = 0;
    if (v[HpOn] >= 0.5f && n < max) hz[n++] = v[HpFreq];
    if (v[LpOn] >= 0.5f && n < max) hz[n++] = v[LpFreq];
    return n;
}

class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    bool paramActive(int i) const noexcept override {
        if (i >= kSide && i < 2 * kSide && p(Link) >= 0.5f) return false;
        if (i == HpFreq || i == RHpFreq) return p(i - 1) >= 0.5f;
        if (i == LpFreq || i == RLpFreq) return p(i - 1) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        ease_ = 1.0f - std::exp(-2.0f / static_cast<float>(fs * 0.02));
        fade_ = static_cast<float>(1.0 / (fs * 0.02));
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (int s = 0; s < 2; ++s)
            for (int f = 0; f < 2; ++f) {
                for (auto& q : bq_[s][f]) q.clear();
                hz_[s][f] = target(s, f);
                mix_[s][f] = on(s, f) ? 1.0f : 0.0f;
                design(s, f);
            }
        tick_ = 0;
        before_.reset();
        after_.reset();
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        float* io[2] = {L, R};
        float want[2][2];
        bool onNow[2][2];
        for (int s = 0; s < 2; ++s)
            for (int f = 0; f < 2; ++f) { want[s][f] = target(s, f); onNow[s][f] = on(s, f); }
        for (std::uint32_t i = 0; i < n; ++i) {
            if (++tick_ >= 2) {
                tick_ = 0;
                for (int s = 0; s < 2; ++s)
                    for (int f = 0; f < 2; ++f) {
                        float& h = hz_[s][f];
                        if (h == want[s][f]) continue;
                        h = std::exp(std::log(h) + ease_ * (std::log(want[s][f]) - std::log(h)));
                        if (std::fabs(h - want[s][f]) < 1e-4f * want[s][f]) h = want[s][f];
                        design(s, f);
                    }
            }
            for (int s = 0; s < 2; ++s) {
                float x = io[s][i];
                for (int f = 0; f < 2; ++f) {
                    float& m = mix_[s][f];
                    m = onNow[s][f] ? std::min(1.0f, m + fade_) : std::max(0.0f, m - fade_);
                    const float y = bq_[s][f][1].tick(bq_[s][f][0].tick(x));
                    x += m * (y - x);
                }
                io[s][i] = x;
            }
        }
        after_.push(L, R, n);
    }

private:
    int base(int s) const noexcept { return s == 1 && p(Link) < 0.5f ? kSide : 0; }
    float target(int s, int f) const noexcept { return p(base(s) + (f == 0 ? HpFreq : LpFreq)); }
    bool on(int s, int f) const noexcept { return p(base(s) + (f == 0 ? HpOn : LpOn)) >= 0.5f; }
    void design(int s, int f) noexcept {
        C a, b;
        third(f == 0, fs_, hz_[s][f], a, b);
        bq_[s][f][0].c = a;
        bq_[s][f][1].c = b;
    }

    double fs_ = 48000.0;
    float ease_ = 0.5f, fade_ = 0.001f;
    hc::fx::Biquad bq_[2][2][2];   // side, filter (HP / LP), section
    float hz_[2][2]{}, mix_[2][2]{};
    int tick_ = 0;
    SpectrumMeter before_, after_;
};
} // namespace ct

// ---- TUNNEL VISION ------------------------------------------------------------------------------
namespace tv {
using namespace tunnelvision;
const char* const kResN[] = {"OFF", "LOW", "MID", "HIGH"};
const ParamInfo kParams[kCount] = {
    {"filter", "Filter", "/10", 0.0f, 10.0f, 10.0f},
    {"resonance", "Resonance", "", 0, 3, 0, Taper::Choice, kResN},
};
constexpr float kResQ[4] = {1.3066f, 2.0f, 4.0f, 8.0f};   // the second section's Q (OFF: 4th-order Butterworth)
constexpr float kBottomHz = 120.0f, kTopHz = 20000.0f;

float cutoff(float knob) noexcept { return kBottomHz * std::pow(kTopHz / kBottomHz, std::clamp(knob, 0.0f, 10.0f) / 10.0f); }
// 10 = open: the filter fades out over the last tenth of the knob
float wetOf(float knob) noexcept { return std::clamp((10.0f - knob) / 0.1f, 0.0f, 1.0f); }

int dots(const float* v, double* hz, int max) noexcept {
    if (max < 1 || v[Filter] >= 9.999f) return 0;
    hz[0] = cutoff(v[Filter]);
    return 1;
}

class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        ease_ = 1.0f - std::exp(-2.0f / static_cast<float>(fs * 0.02));
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : bq_) for (auto& q : c) q.clear();
        hz_ = cutoff(p(Filter));
        res_ = resIndex();
        design();
        wet_.prepare(fs_, 20, wetOf(p(Filter)));
        tick_ = 0;
        before_.reset();
        after_.reset();
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        const float want = cutoff(p(Filter)), wWet = wetOf(p(Filter));
        if (resIndex() != res_) {
            res_ = resIndex();
            design();
        }
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            if (++tick_ >= 2) {
                tick_ = 0;
                if (hz_ != want) {
                    hz_ = std::exp(std::log(hz_) + ease_ * (std::log(want) - std::log(hz_)));
                    if (std::fabs(hz_ - want) < 1e-4f * want) hz_ = want;
                    design();
                }
            }
            const float w = wet_.next(wWet);
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                const float y = bq_[c][1].tick(bq_[c][0].tick(x));
                io[c][i] = x + w * (y - x);
            }
        }
        after_.push(L, R, n);
    }

private:
    int resIndex() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Resonance))), 0, 3); }
    void design() noexcept {
        const C a = C::make(T::LowPass, fs_, hz_, 0.5412), b = C::make(T::LowPass, fs_, hz_, kResQ[res_]);
        for (auto& c : bq_) { c[0].c = a; c[1].c = b; }
    }

    double fs_ = 48000.0;
    float ease_ = 0.5f, hz_ = 20000.0f;
    int res_ = 0, tick_ = 0;
    hc::fx::Biquad bq_[2][2];
    hc::fx::Smoothed wet_;
    SpectrumMeter before_, after_;
};
} // namespace tv

} // namespace

namespace cutthroat {
double curveDb(const float* v, double fs, double hz) noexcept { return ct::sideDb(v, 0, fs, hz); }
double curveRightDb(const float* v, double fs, double hz) noexcept { return ct::sideDb(v, 1, fs, hz); }
const CurveInfo kCurve{&curveDb, &ct::dots, 30.0f, &curveRightDb};
} // namespace cutthroat
namespace tunnelvision {
double curveDb(const float* v, double fs, double hz) noexcept {
    if (v[Filter] >= 9.999f) return 0.0;
    const double f = tv::cutoff(v[Filter]), w = tv::wetOf(v[Filter]);
    const int r = std::clamp(static_cast<int>(std::lround(v[Resonance])), 0, 3);
    const C a = C::make(T::LowPass, fs, f, 0.5412), b = C::make(T::LowPass, fs, f, tv::kResQ[r]);
    if (w >= 1.0) return a.magnitudeDb(fs, hz) + b.magnitudeDb(fs, hz);
    return 20.0 * std::log10(std::max(1.0 - w + w * std::pow(10.0, (a.magnitudeDb(fs, hz) + b.magnitudeDb(fs, hz)) / 20.0), 1e-9));
}
const CurveInfo kCurve{&curveDb, &tv::dots, 30.0f};
} // namespace tunnelvision

std::unique_ptr<hc::fx::Effect> makeCutthroat() { return std::make_unique<ct::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeTunnelVision() { return std::make_unique<tv::Impl>(); }

} // namespace hem::rack
