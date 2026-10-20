// STRAIGHT SHOOTER — linear-phase EQ (see ToneShop.hpp).
//
// The curve (the same sections the faceplate draws) is sampled on a fine frequency grid, turned
// into a symmetrical impulse (zero phase, then delayed by half its length) and run by uniformly
// partitioned FFT convolution: 16 parts of 512 samples, left and right packed into one complex FFT.
// When a setting changes, the new filter is built into the second bank and one block crossfades.
#include "hem/rack/ToneShop.hpp"

#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Fft.hpp"

#include "../arsenal/Common.hpp"

#include <complex>
#include <vector>

namespace hem::rack {
namespace straight {
namespace {
using namespace arsenal;

const char* const kSlopeN[] = {"OFF", "12 dB", "24 dB", "48 dB"};
const ParamInfo kParams[kCount] = {
    {"lowcutslope", "Low Cut Slope", "", 0, 3, 0, Taper::Choice, kSlopeN},
    {"lowcut", "Low Cut", "Hz", 10.0f, 1000.0f, 30.0f, Taper::Log},
    {"lowfreq", "Low Freq", "Hz", 20.0f, 1000.0f, 100.0f, Taper::Log},
    {"lowgain", "Low Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"mid1freq", "Mid 1 Freq", "Hz", 20.0f, 20000.0f, 250.0f, Taper::Log},
    {"mid1gain", "Mid 1 Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"mid1q", "Mid 1 Q", "", 0.3f, 10.0f, 1.0f, Taper::Log},
    {"mid2freq", "Mid 2 Freq", "Hz", 20.0f, 20000.0f, 1500.0f, Taper::Log},
    {"mid2gain", "Mid 2 Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"mid2q", "Mid 2 Q", "", 0.3f, 10.0f, 1.0f, Taper::Log},
    {"mid3freq", "Mid 3 Freq", "Hz", 20.0f, 20000.0f, 5000.0f, Taper::Log},
    {"mid3gain", "Mid 3 Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"mid3q", "Mid 3 Q", "", 0.3f, 10.0f, 1.0f, Taper::Log},
    {"highfreq", "High Freq", "Hz", 1000.0f, 20000.0f, 10000.0f, Taper::Log},
    {"highgain", "High Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"highcut", "High Cut", "Hz", 1000.0f, 22000.0f, 18000.0f, Taper::Log},
    {"highcutslope", "High Cut Slope", "", 0, 3, 0, Taper::Choice, kSlopeN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

// Butterworth section Qs for 2nd, 4th and 8th order.
constexpr double kQ12[] = {0.70710678};
constexpr double kQ24[] = {0.54119610, 1.30656296};
constexpr double kQ48[] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};

// The sections of the curve; returns how many.
int sections(const float* v, double fs, C* out) noexcept {
    int n = 0;
    const double top = fs * 0.45;
    auto cut = [&](int slope, T t, double hz) {
        const double* q = slope == 1 ? kQ12 : slope == 2 ? kQ24 : kQ48;
        const int k = slope == 1 ? 1 : slope == 2 ? 2 : 4;
        for (int i = 0; i < k; ++i) out[n++] = C::make(t, fs, std::min(hz, top), q[i]);
    };
    const int ls = std::clamp(static_cast<int>(std::lround(v[LowCutSlope])), 0, 3);
    const int hs = std::clamp(static_cast<int>(std::lround(v[HighCutSlope])), 0, 3);
    if (ls > 0) cut(ls, T::HighPass, v[LowCut]);
    if (v[LowGain] != 0.0f) out[n++] = C::make(T::LowShelf, fs, std::min<double>(v[LowFreq], top), 0.7071, v[LowGain]);
    for (int m = 0; m < 3; ++m) {
        const int f = Mid1Freq + m * 3;
        if (v[f + 1] != 0.0f) out[n++] = C::make(T::Bell, fs, std::min<double>(v[f], top), v[f + 2], v[f + 1]);
    }
    if (v[HighGain] != 0.0f) out[n++] = C::make(T::HighShelf, fs, std::min<double>(v[HighFreq], top), 0.7071, v[HighGain]);
    if (hs > 0 && v[HighCut] < top) cut(hs, T::LowPass, v[HighCut]);
    return n;
}

int dots(const float* v, double* hz, int max) noexcept {
    int n = 0;
    auto put = [&](double f) { if (n < max) hz[n++] = f; };
    if (v[LowCutSlope] >= 0.5f) put(v[LowCut]);
    put(v[LowFreq]);
    put(v[Mid1Freq]);
    put(v[Mid2Freq]);
    put(v[Mid3Freq]);
    put(v[HighFreq]);
    if (v[HighCutSlope] >= 0.5f) put(v[HighCut]);
    return n;
}

constexpr std::uint32_t kDesign = 16384;    // the grid the curve is sampled on
constexpr std::uint32_t kFft = 2 * kBlock;  // one part, zero-padded
constexpr std::uint32_t kCentre = (kTaps - 1) / 2;
constexpr int kRedesignBlocks = 3;          // at most one rebuild every 3 blocks (32 ms) while a knob moves
using Cx = std::complex<float>;

class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    bool paramActive(int i) const noexcept override {
        if (i == LowCut) return p(LowCutSlope) >= 0.5f;
        if (i == HighCut) return p(HighCutSlope) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fftD_.prepare(static_cast<int>(kDesign));
        fftB_.prepare(static_cast<int>(kFft));
        const std::uint32_t bins = kDesign / 2 + 1;
        c1_.resize(bins); s1_.resize(bins); c2_.resize(bins); s2_.resize(bins);
        for (std::uint32_t k = 0; k < bins; ++k) {
            const double w = 2.0 * std::numbers::pi * k / kDesign;
            c1_[k] = std::cos(w); s1_[k] = -std::sin(w); c2_[k] = std::cos(2 * w); s2_[k] = -std::sin(2 * w);
        }
        mag_.resize(bins);
        design_.resize(kDesign);
        win_.resize(kTaps);
        for (std::uint32_t t = 0; t < kTaps; ++t) {
            const double a = 2.0 * std::numbers::pi * t / (kTaps - 1);
            win_[t] = static_cast<float>(0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2 * a));
        }
        for (auto& b : bank_) b.assign(static_cast<std::size_t>(kParts) * kFft, {});
        fdl_.assign(static_cast<std::size_t>(kParts) * kFft, {});
        frame_.assign(kFft, {});
        acc_.assign(kFft, {});
        acc2_.assign(kFft, {});
        in_.assign(kFft, {});
        outL_.assign(kBlock, 0.0f);
        outR_.assign(kBlock, 0.0f);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        std::fill(fdl_.begin(), fdl_.end(), Cx{});
        std::fill(in_.begin(), in_.end(), Cx{});
        std::fill(outL_.begin(), outL_.end(), 0.0f);
        std::fill(outR_.begin(), outR_.end(), 0.0f);
        pos_ = 0;
        slot_ = 0;
        active_ = 0;
        sinceDesign_ = kRedesignBlocks;
        snapshot(designed_);
        build(bank_[0], designed_.data());
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        before_.reset();
        after_.reset();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float yl = outL_[pos_], yr = outR_[pos_];
            in_[kBlock + pos_] = {L[i], R[i]};
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = yl * o;
            R[i] = yr * o;
            if (++pos_ == kBlock) { pos_ = 0; block(); }
        }
        after_.push(L, R, n);
    }

private:
    // the settings the filter is built from (OUTPUT is a plain gain after it)
    void snapshot(std::array<float, kCount>& v) const noexcept {
        for (int i = 0; i < kCount; ++i) v[static_cast<std::size_t>(i)] = p(i);
        v[Output] = 0.0f;
    }

    // The filter for these settings into one bank (no allocation).
    void build(std::vector<Cx>& bank, const float* v) noexcept {
        C sec[16];
        const int ns = sections(v, fs_, sec);
        const std::uint32_t bins = kDesign / 2 + 1;
        for (std::uint32_t k = 0; k < bins; ++k) {
            double m = 1.0;
            for (int s = 0; s < ns; ++s) {
                const C& c = sec[s];
                const double nr = c.b0 + c.b1 * c1_[k] + c.b2 * c2_[k], ni = c.b1 * s1_[k] + c.b2 * s2_[k];
                const double dr = 1 + c.a1 * c1_[k] + c.a2 * c2_[k], di = c.a1 * s1_[k] + c.a2 * s2_[k];
                m *= std::sqrt((nr * nr + ni * ni) / std::max(dr * dr + di * di, 1e-300));
            }
            mag_[k] = m;
        }
        for (std::uint32_t k = 0; k < bins; ++k) design_[k] = {static_cast<float>(mag_[k]), 0.0f};
        for (std::uint32_t k = bins; k < kDesign; ++k) design_[k] = design_[kDesign - k];
        fftD_.inverse(design_.data());
        // the symmetrical impulse, windowed, cut into parts
        for (std::uint32_t part = 0; part < kParts; ++part) {
            Cx* h = &bank[static_cast<std::size_t>(part) * kFft];
            for (std::uint32_t j = 0; j < kFft; ++j) h[j] = {};
            for (std::uint32_t j = 0; j < kBlock; ++j) {
                const std::uint32_t t = part * kBlock + j;
                if (t >= kTaps) continue;
                const std::uint32_t src = (t + kDesign - kCentre) % kDesign;
                h[j] = {design_[src].real() * win_[t], 0.0f};
            }
            fftB_.forward(h);
        }
    }

    void convolve(const std::vector<Cx>& bank, std::vector<Cx>& acc) const noexcept {
        std::fill(acc.begin(), acc.end(), Cx{});
        for (std::uint32_t part = 0; part < kParts; ++part) {
            const Cx* x = &fdl_[static_cast<std::size_t>((slot_ + kParts - part) % kParts) * kFft];
            const Cx* h = &bank[static_cast<std::size_t>(part) * kFft];
            for (std::uint32_t k = 0; k < kFft; ++k) acc[k] += x[k] * h[k];
        }
        fftB_.inverse(acc.data());
    }

    void block() noexcept {
        // the newest input frame (last block + this one) into the delay line, in the frequency domain
        Cx* slot = &fdl_[static_cast<std::size_t>(slot_) * kFft];
        std::copy(in_.begin(), in_.end(), slot);
        fftB_.forward(slot);
        std::copy(in_.begin() + kBlock, in_.end(), in_.begin());
        // a setting changed? build the other bank and crossfade over this block
        std::array<float, kCount> now{};
        snapshot(now);
        ++sinceDesign_;
        const bool change = now != designed_ && sinceDesign_ >= kRedesignBlocks;
        convolve(bank_[active_], acc_);
        if (change) {
            designed_ = now;
            sinceDesign_ = 0;
            build(bank_[1 - active_], now.data());
            convolve(bank_[1 - active_], acc2_);
            for (std::uint32_t j = 0; j < kBlock; ++j) {
                const float t = (static_cast<float>(j) + 0.5f) / static_cast<float>(kBlock);
                const Cx a = acc_[kBlock + j], b = acc2_[kBlock + j];
                outL_[j] = a.real() + t * (b.real() - a.real());
                outR_[j] = a.imag() + t * (b.imag() - a.imag());
            }
            active_ = 1 - active_;
        } else {
            for (std::uint32_t j = 0; j < kBlock; ++j) {
                outL_[j] = acc_[kBlock + j].real();
                outR_[j] = acc_[kBlock + j].imag();
            }
        }
        slot_ = (slot_ + 1) % kParts;
    }

    double fs_ = 48000.0;
    hc::fx::Fft fftD_, fftB_;
    std::vector<double> c1_, s1_, c2_, s2_, mag_;
    std::vector<Cx> design_, bank_[2], fdl_, frame_, acc_, acc2_, in_;
    std::vector<float> win_, outL_, outR_;
    std::uint32_t pos_ = 0, slot_ = 0;
    int active_ = 0, sinceDesign_ = 0;
    std::array<float, kCount> designed_{};
    hc::fx::Smoothed out_;
    SpectrumMeter before_, after_;
};

} // namespace

double curveDb(const float* v, double fs, double hz) noexcept {
    C sec[16];
    const int n = sections(v, fs, sec);
    double db = 0.0;
    for (int i = 0; i < n; ++i) db += sec[i].magnitudeDb(fs, hz);
    return db;
}
const CurveInfo kCurve{&curveDb, &dots, 18.0f};

} // namespace straight

std::unique_ptr<hc::fx::Effect> makeStraightShooter() { return std::make_unique<straight::Impl>(); }

} // namespace hem::rack
