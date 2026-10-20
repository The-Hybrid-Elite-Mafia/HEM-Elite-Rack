// DOUBLE CROSS — mono to stereo by frequency-swept panning that always folds back to mono (see ToneShop.hpp).
//
// The middle is cut into 2048-sample frames (every 512 samples, square-root Hann windows on both sides),
// each frequency is weighted by how far it leans (panAt: a real, zero-phase gain), and the frames are
// added back. That gives D; then left = M + D + S and right = M - D - S, with M and S delayed to line up,
// so left + right is exactly twice the delayed middle.
#include "hem/rack/ToneShop.hpp"

#include "hybrid/fx/Fft.hpp"

#include "ToneCommon.hpp"

#include <complex>
#include <vector>

namespace hem::rack {
namespace doublecross {
namespace {
using namespace tone;

const char* const kKindN[] = {"SPREAD", "SPREAD 10", "SPLIT"};
const char* const kIoN[] = {"L / R", "M / S"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"kind", "Kind", "", 0, 2, 0, Taper::Choice, kKindN},
    {"input", "Input", "dB", -24.0f, 0.0f, 0.0f},
    {"width", "Width", "", 0.0f, 1.0f, 1.0f},
    {"rotation", "Rotation", "deg", -45.0f, 45.0f, 0.0f},
    {"polarityl", "Polarity L", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"polarityr", "Polarity R", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"swap", "Swap", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"spread", "Spread", "", 0.0f, 1.2f, 1.0f},
    {"freq", "Freq", "Hz", 32.0f, 16000.0f, 200.0f, Taper::Log},
    {"lfspread", "LF Spread", "", 0.0f, 2.0f, 0.0f},
    {"sweeps", "Sweeps", "", 2.0f, 22.0f, 8.0f},
    {"fcenter", "F Center", "Hz", 100.0f, 12000.0f, 2600.0f, Taper::Log},
    {"fdensity", "F Density", "", -1.0f, 1.0f, 0.0f},
    {"tweak", "Tweak", "", -1.0f, 1.0f, 0.0f},
    {"shape", "Shape", "", 0.0f, 1.25f, 0.15f},
    {"metermode", "Meter", "", 0, 1, 0, Taper::Choice, kIoN},
    {"reset", "Reset", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"PEAK L / M", "dBFS", -60.0f, 6.0f}, {"PEAK R / S", "dBFS", -60.0f, 6.0f}};
using Cx = std::complex<float>;

double xOf(double hz) noexcept { return std::clamp(std::log2(std::max(hz, 1.0) / 20.0) / std::log2(1000.0), 0.0, 1.0); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        const int k = kind();
        if (i == LfSpread) return k != SplitMode;
        if (i == Shape) return k == SplitMode;
        if (i == FCenter || i == FDensity) return sweeps() > 2;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fft_.prepare(static_cast<int>(kFrame));
        win_.resize(kFrame);
        for (std::uint32_t n = 0; n < kFrame; ++n) win_[n] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / kFrame)));
        in_.assign(kFrame, 0.0f);
        acc_.assign(2 * kFrame, 0.0f);
        dM_.assign(kFrame, 0.0f);
        dS_.assign(kFrame, 0.0f);
        buf_.assign(kFrame, {});
        gain_.assign(kFrame / 2 + 1, 0.0f);
        reset();
    }
    void reset() noexcept override {
        std::fill(in_.begin(), in_.end(), 0.0f);
        std::fill(acc_.begin(), acc_.end(), 0.0f);
        std::fill(dM_.begin(), dM_.end(), 0.0f);
        std::fill(dS_.begin(), dS_.end(), 0.0f);
        t_ = 0;
        key_.fill(-99.0f);
        design();
        held_[0] = held_[1] = 0.0f;
        push_ = p(Reset);
        in_g_.prepare(fs_, 20, hc::fx::dbToGain(p(Input)));
        width_.prepare(fs_, 20, p(Width));
        rot_.prepare(fs_, 20, p(Rotation));
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        if (p(Reset) != push_) {   // RESET: the held peaks start again
            push_ = p(Reset);
            held_[0] = held_[1] = 0.0f;
        }
        const float pl = p(PolarityL) >= 0.5f ? -1.0f : 1.0f, pr = p(PolarityR) >= 0.5f ? -1.0f : 1.0f;
        const bool swap = p(Swap) >= 0.5f, ms = p(MeterMode) >= 0.5f;
        const float wIn = hc::fx::dbToGain(p(Input)), wW = p(Width), wRot = p(Rotation);
        for (std::uint32_t i = 0; i < n; ++i) {
            // the input: gain, polarity, swap, width, rotation
            const float g = in_g_.next(wIn);
            float l = L[i] * g * pl, r = R[i] * g * pr;
            if (swap) std::swap(l, r);
            const float w = width_.next(wW);
            const float m0 = 0.5f * (l + r), s0 = 0.5f * (l - r) * w;
            l = m0 + s0;
            r = m0 - s0;
            const float a = rot_.next(wRot) * static_cast<float>(std::numbers::pi / 180.0), c = std::cos(a), sn = std::sin(a);
            const float rl = c * l - sn * r, rr = sn * l + c * r;
            const float m = 0.5f * (rl + rr), s = 0.5f * (rl - rr);
            // the frame machinery: M in, D out kLatency later
            const auto at = static_cast<std::uint32_t>(t_ % kFrame), old = static_cast<std::uint32_t>((t_ + 1) % kFrame);   // `old`: the sample kLatency ago
            in_[at] = m;
            dM_[at] = m;
            dS_[at] = s;
            const float md = dM_[old], sd = dS_[old];
            if ((t_ + 1) % kHop == 0) frame();
            const auto outAt = static_cast<std::uint32_t>((t_ + 1) % (2 * kFrame));   // the sample kLatency ago (t - (kFrame - 1))
            const float d = acc_[outAt];
            acc_[outAt] = 0.0f;
            ++t_;
            const float yl = md + d + sd, yr = md - d - sd;
            L[i] = yl;
            R[i] = yr;
            held_[0] = std::max(held_[0], std::fabs(ms ? 0.5f * (yl + yr) : yl));
            held_[1] = std::max(held_[1], std::fabs(ms ? 0.5f * (yl - yr) : yr));
        }
        publishMeter(0, std::max(dbOf(held_[0]), -100.0f));
        publishMeter(1, std::max(dbOf(held_[1]), -100.0f));
    }

private:
    int kind() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Kind))), 0, 2); }
    int sweeps() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Sweeps))), 2, kind() == SpreadMode ? 22 : 10); }
    // the per-frequency lean, when a setting that shapes it changed
    void design() noexcept {
        const std::array<float, 9> now{p(Kind), p(Spread), p(Freq), p(LfSpread), p(Sweeps), p(FCenter), p(FDensity), p(Tweak), p(Shape)};
        if (now == key_) return;
        key_ = now;
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        for (std::uint32_t k = 0; k <= kFrame / 2; ++k) gain_[k] = static_cast<float>(panAt(v, fs_ * k / kFrame));
    }
    // one frame: window, FFT, weight, back, window, add in
    void frame() noexcept {
        const auto end = static_cast<std::uint32_t>(t_ % kFrame);   // the newest sample
        for (std::uint32_t n = 0; n < kFrame; ++n) buf_[n] = {in_[(end + 1 + n) % kFrame] * win_[n], 0.0f};
        fft_.forward(buf_.data());
        for (std::uint32_t k = 0; k <= kFrame / 2; ++k) {
            buf_[k] *= gain_[k];
            if (k > 0 && k < kFrame / 2) buf_[kFrame - k] *= gain_[k];
        }
        fft_.inverse(buf_.data());
        // frame sample n is input time t - (kFrame - 1) + n; it comes out at that time + kLatency
        const auto first = static_cast<std::uint32_t>((t_ + 1) % (2 * kFrame));   // = (t - kLatency + 2 kFrame) mod 2 kFrame, as read below
        for (std::uint32_t n = 0; n < kFrame; ++n) acc_[(first + n) % (2 * kFrame)] += 0.5f * buf_[n].real() * win_[n];
    }

    double fs_ = 48000.0;
    hc::fx::Fft fft_;
    std::vector<float> win_, in_, acc_, dM_, dS_, gain_;
    std::vector<Cx> buf_;
    std::uint64_t t_ = 0;
    std::array<float, 9> key_{};
    float held_[2]{}, push_ = 0.0f;
    hc::fx::Smoothed in_g_, width_, rot_;
};

} // namespace

double panAt(const float* v, double hz) noexcept {
    const int kind = std::clamp(static_cast<int>(std::lround(v[Kind])), 0, 2);
    const int n = std::clamp(static_cast<int>(std::lround(v[Sweeps])), 2, kind == SpreadMode ? 22 : 10);
    double x = xOf(hz);
    if (n > 2) {   // FDENSITY crowds the sweeps around FCENTER (plus) or away from it (minus)
        const double xc = std::clamp(xOf(v[FCenter]), 0.02, 0.98), d = v[FDensity];
        const double e = d >= 0.0 ? 1.0 / (1.0 + 3.0 * d) : 1.0 + 3.0 * -d;
        const double u = x - xc, span = u >= 0.0 ? 1.0 - xc : xc, un = u / span;
        x = xc + std::copysign(std::pow(std::fabs(un), e), un) * span;
    }
    double tweak = v[Tweak];
    if (n == 2) tweak = std::fabs(tweak) > 0.5 ? 1.0 : 0.0;   // with two sweeps TWEAK only swaps the sides
    const double th = std::numbers::pi * (n * x + tweak);
    const double base = -std::cos(th);
    double lean = base;
    if (kind == SplitMode) {
        const double k = std::clamp(static_cast<double>(v[Shape]), 0.0, 1.25);
        const double tri = 2.0 / std::numbers::pi * std::asin(std::clamp(base, -1.0, 1.0));
        if (k <= 0.15) lean = tri + (base - tri) * k / 0.15;
        else {
            const double a = std::min(k, 1.0), g = 0.01 + 20.0 * ((a - 0.15) / 0.85) * ((a - 0.15) / 0.85);
            lean = std::tanh(g * base) / std::tanh(g);
            if (k > 1.0) lean *= 1.0 + 0.8 * (k - 1.0);   // past square: beyond full left / right
        }
    }
    // under FREQ, LF SPREAD (SPLIT: 0) takes over, across an octave
    const double s = v[Spread], lf = kind == SplitMode ? 0.0 : v[LfSpread] * s;
    const double w = std::clamp(std::log2(std::max(hz, 1.0) / v[Freq]) + 0.5, 0.0, 1.0);
    return (lf + (s - lf) * w) * lean;
}

} // namespace doublecross

std::unique_ptr<hc::fx::Effect> makeDoubleCross() { return std::make_unique<doublecross::Impl>(); }

} // namespace hem::rack
