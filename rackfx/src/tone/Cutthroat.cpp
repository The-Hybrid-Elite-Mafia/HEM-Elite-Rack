// CUTTHROAT — high-pass / low-pass pair with a one-knob sweep, resonance and drive (see ToneShop.hpp).
//
// State-variable filters (trapezoidal), so sweeping never clicks; their response is the same
// bilinear 2nd-order section the curve draws.
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"
#include "hem/rack/Spectrum.hpp"

namespace hem::rack {
namespace cutthroat {
namespace {
using namespace tone;

const char* const kSlopeN[] = {"12 dB", "24 dB"};
const ParamInfo kParams[kCount] = {
    {"sweep", "Sweep", "%", -100.0f, 100.0f, 0.0f},
    {"lowcut", "Low Cut", "Hz", 10.0f, 2000.0f, 10.0f, Taper::Log},
    {"highcut", "High Cut", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"resonance", "Resonance", "%", 0.0f, 100.0f, 0.0f},
    {"slope", "Slope", "", 0, 1, 0, Taper::Choice, kSlopeN},
    {"drive", "Drive", "%", 0.0f, 100.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kOpenLow = 10.5, kOpenHigh = 19999.0;   // at these edges a filter is out of the way

// the Qs of the stages for a slope and resonance (the last stage carries the resonance)
void stageQs(const float* v, int& stages, double* q) noexcept {
    const double res = std::clamp(static_cast<double>(v[Resonance]), 0.0, 100.0) * 0.01;
    if (v[Slope] >= 0.5f) {
        stages = 2;
        q[0] = 0.5412;
        q[1] = 1.3066 + 8.0 * res;
    } else {
        stages = 1;
        q[0] = 0.7071 + 9.0 * res;
    }
}

int dots(const float* v, double* hz, int max) noexcept {
    double hp = 0, lp = 0, q = 0;
    edges(v, hp, lp, q);
    int n = 0;
    if (hp > kOpenLow && n < max) hz[n++] = hp;
    if (lp < kOpenHigh && n < max) hz[n++] = lp;
    return n;
}

// One trapezoidal state-variable filter stage (both outputs from the same state).
struct Svf {
    double ic1 = 0, ic2 = 0;
    void clear() noexcept { ic1 = ic2 = 0; }
    // returns low-pass (hp = false) or high-pass
    float tick(float x, double g, double k, bool hp) noexcept {
        const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return static_cast<float>(hp ? x - k * v1 - v2 : v2);
    }
};

class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : hp_) for (auto& s : ch) s.clear();
        for (auto& ch : lp_) for (auto& s : ch) s.clear();
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        double hp = 0, lp = 0, q = 0;
        edges(v, hp, lp, q);
        hpOct_.prepare(fs_, 20, static_cast<float>(std::log2(hp)));
        lpOct_.prepare(fs_, 20, static_cast<float>(std::log2(lp)));
        hpOn_ = hp > kOpenLow ? 1.0f : 0.0f;
        lpOn_ = lp < kOpenHigh ? 1.0f : 0.0f;
        drive_.prepare(fs_, 30, p(Drive) * 0.01f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        before_.reset();
        after_.reset();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        double hpHz = 0, lpHz = 0, unused = 0;
        edges(v, hpHz, lpHz, unused);
        int stages = 1;
        double q[2] = {0.7071, 0.7071};
        stageQs(v, stages, q);
        const bool hpWanted = hpHz > kOpenLow, lpWanted = lpHz < kOpenHigh;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const double top = fs_ * 0.45;
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const double fh = std::min(std::exp2(static_cast<double>(hpOct_.next(static_cast<float>(std::log2(hpHz))))), top);
            const double fl = std::min(std::exp2(static_cast<double>(lpOct_.next(static_cast<float>(std::log2(lpHz))))), top);
            const double gh = std::tan(std::numbers::pi * fh / fs_), gl = std::tan(std::numbers::pi * fl / fs_);
            hpOn_ = hpWanted ? std::min(1.0f, hpOn_ + step) : std::max(0.0f, hpOn_ - step);
            lpOn_ = lpWanted ? std::min(1.0f, lpOn_ + step) : std::max(0.0f, lpOn_ - step);
            const float d = drive_.next(p(Drive) * 0.01f), dg = 1.0f + 8.0f * d;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            for (int c = 0; c < 2; ++c) {
                const float dry = io[c][i];
                float x = dry + d * (softClip(dry * dg) / std::sqrt(dg) - dry);
                float y = x;
                for (int s = 0; s < stages; ++s) y = hp_[c][s].tick(y, gh, 1.0 / q[s], true);
                x = x + hpOn_ * (y - x);
                y = x;
                for (int s = 0; s < stages; ++s) y = lp_[c][s].tick(y, gl, 1.0 / q[s], false);
                x = x + lpOn_ * (y - x);
                io[c][i] = (dry + m * (x - dry)) * o;
            }
        }
        after_.push(L, R, n);
    }

private:
    double fs_ = 48000.0;
    std::array<std::array<Svf, 2>, 2> hp_{}, lp_{};
    hc::fx::Smoothed hpOct_, lpOct_, drive_, mix_, out_;
    float hpOn_ = 0, lpOn_ = 0;
    SpectrumMeter before_, after_;
};

} // namespace

void edges(const float* v, double& hpHz, double& lpHz, double& q) noexcept {
    const double s = std::clamp(static_cast<double>(v[Sweep]), -100.0, 100.0) * 0.01;
    hpHz = v[LowCut];
    lpHz = v[HighCut];
    if (s > 0.0) hpHz = std::max(hpHz, 20.0 * std::pow(8000.0 / 20.0, s));          // right: the lows thin out up to 8 kHz
    if (s < 0.0) lpHz = std::min(lpHz, 20000.0 * std::pow(100.0 / 20000.0, -s));   // left: the highs close down to 100 Hz
    if (s > 0.0 && hpHz <= kOpenLow) hpHz = kOpenLow + 0.1;
    q = 0.7071 + 9.0 * std::clamp(static_cast<double>(v[Resonance]), 0.0, 100.0) * 0.01;
}

double curveDb(const float* v, double fs, double hz) noexcept {
    double hp = 0, lp = 0, unused = 0;
    edges(v, hp, lp, unused);
    int stages = 1;
    double q[2] = {0.7071, 0.7071};
    stageQs(v, stages, q);
    const double top = fs * 0.45;
    double db = 0.0;
    for (int s = 0; s < stages; ++s) {
        if (hp > kOpenLow) db += C::make(T::HighPass, fs, std::min(hp, top), q[s]).magnitudeDb(fs, hz);
        if (lp < kOpenHigh) db += C::make(T::LowPass, fs, std::min(lp, top), q[s]).magnitudeDb(fs, hz);
    }
    return db;
}
const CurveInfo kCurve{&curveDb, &dots, 24.0f};

} // namespace cutthroat

std::unique_ptr<hc::fx::Effect> makeCutthroat() { return std::make_unique<cutthroat::Impl>(); }

} // namespace hem::rack
