#pragma once
// Building blocks for the EQ family (private to rackfx).
//
//  * Shape and magDb(): the band shapes as analog curves (magnitude only, no warping near the top).
//    The linear-phase EQs build their filter straight from these.
//  * biquadFor(): the same shapes as one or two biquads, for the minimum-phase EQs, and iirDb() for
//    their exact curve (the display shows what the audio runs).
//  * LinPhase<kParts, kDesign>: a linear-phase filter engine. The curve is sampled on kDesign points,
//    turned into a symmetrical impulse (zero phase, delayed by half its length), windowed and run by
//    uniformly partitioned FFT convolution (kParts parts of 512 samples, left and right packed into
//    one complex FFT). When the curve changes, the new filter goes into the second bank and one block
//    crossfades. Latency is fixed: 512 + (taps - 1) / 2.
//  * IirBands<kMax>: up to kMax biquads per side with their settings eased (no zipper noise).
#include "../arsenal/Common.hpp"
#include "hybrid/fx/Fft.hpp"

#include <complex>
#include <vector>

namespace hem::rack::eqkit {
using namespace arsenal;

enum class Shape : std::uint8_t {
    Bell,        // analog bell (Q = width)
    LowShelf,    // analog shelves and cuts, 12 dB / oct (Q: 0.71 = no bump)
    HighShelf,
    LowCut,
    HighCut,
    VLowCut,     // "precision": variable slope (Q = how steep), no bump
    VLowShelf,
    VHighShelf,
    VHighCut,
    PropBell,    // bell whose width narrows as the gain grows
    Off,
};

// Steepness of the variable-slope shapes from Q (0.26 .. 6.5).
inline double vOrder(double q) noexcept { return 0.5 + std::clamp(q, 0.1, 8.0); }        // cut: order N (6N dB / oct)
inline double vShelfK(double q) noexcept { return 2.2 * std::clamp(q, 0.1, 8.0); }      // shelf: sigmoid steepness
// The width a proportional-Q bell really uses: Q at 6 dB of gain, twice as narrow at 18 dB, wider when small.
inline double propQ(double q, double gainDb) noexcept { return q * (1.0 + std::fabs(gainDb) / 12.0) / 1.5; }

// The shape's response (dB) at hz: analog curves, with the corner kept under `top` Hz.
inline double magDb(Shape s, double f0, double gainDb, double q, double hz, double top = 21600.0) noexcept {
    f0 = std::clamp(f0, 5.0, top);
    q = std::max(q, 0.025);
    const double x = std::max(hz, 1e-3) / f0, x2 = x * x;
    switch (s) {
    case Shape::Bell:
    case Shape::PropBell: {
        if (gainDb == 0.0) return 0.0;
        const double qq = s == Shape::PropBell ? propQ(q, gainDb) : q;
        const double A = std::pow(10.0, gainDb / 40.0), r = (1.0 - x2) * (1.0 - x2);
        const double num = r + (x * A / qq) * (x * A / qq), den = r + (x / (A * qq)) * (x / (A * qq));
        return 10.0 * std::log10(num / den);
    }
    case Shape::LowShelf:
    case Shape::HighShelf: {
        if (gainDb == 0.0) return 0.0;
        const double A = std::pow(10.0, gainDb / 40.0), sA = std::sqrt(A), m = (sA * x / q) * (sA * x / q);
        const double a = (A - x2) * (A - x2) + m, b = (1.0 - A * x2) * (1.0 - A * x2) + m;
        return 10.0 * std::log10(A * A * (s == Shape::LowShelf ? a / b : b / a));
    }
    case Shape::LowCut: return 10.0 * std::log10(std::max(x2 * x2 / ((1.0 - x2) * (1.0 - x2) + x2 / (q * q)), 1e-30));
    case Shape::HighCut: return 10.0 * std::log10(std::max(1.0 / ((1.0 - x2) * (1.0 - x2) + x2 / (q * q)), 1e-30));
    case Shape::VLowCut: return -10.0 * std::log10(1.0 + std::pow(x, -2.0 * vOrder(q)));
    case Shape::VHighCut: return -10.0 * std::log10(1.0 + std::pow(x, 2.0 * vOrder(q)));
    case Shape::VLowShelf: return gainDb / (1.0 + std::pow(x, vShelfK(q)));
    case Shape::VHighShelf: return gainDb / (1.0 + std::pow(x, -vShelfK(q)));
    case Shape::Off: break;
    }
    return 0.0;
}

// The shape as biquads (the analog-type shapes only): returns how many (0..1).
inline int biquadFor(Shape s, double fs, double f0, double gainDb, double q, C* out) noexcept {
    f0 = std::min(f0, fs * 0.45);
    switch (s) {
    case Shape::Bell: out[0] = C::make(T::Bell, fs, f0, q, gainDb); return 1;
    case Shape::PropBell: out[0] = C::make(T::Bell, fs, f0, propQ(q, gainDb), gainDb); return 1;
    case Shape::LowShelf: out[0] = C::make(T::LowShelf, fs, f0, q, gainDb); return 1;
    case Shape::HighShelf: out[0] = C::make(T::HighShelf, fs, f0, q, gainDb); return 1;
    case Shape::LowCut: out[0] = C::make(T::HighPass, fs, f0, q); return 1;
    case Shape::HighCut: out[0] = C::make(T::LowPass, fs, f0, q); return 1;
    default: return 0;
    }
}

// The biquad's response (dB) at hz.
inline double iirDb(const C& c, double fs, double hz) noexcept { return c.magnitudeDb(fs, std::min(hz, fs * 0.4999)); }

// ---- the linear-phase engine ----------------------------------------------------------------
// What the engine asks its owner, on the audio thread, once a block.
struct LinSource {
    virtual ~LinSource() = default;
    // the settings changed since the last call? (then the owner keeps the new ones for magnitudeDb)
    virtual bool takeChange() noexcept = 0;
    // the curve (dB) at hz for the kept settings
    virtual double magnitudeDb(double hz) const noexcept = 0;
    // the window: 0 Blackman (standard), 1 Hann (tighter), 2 Blackman-Harris (least ripple)
    virtual int window() const noexcept = 0;
};

template <std::uint32_t kParts, std::uint32_t kDesign>
class LinPhase {
public:
    static constexpr std::uint32_t kBlock = 512, kFft = 2 * kBlock, kTaps = kBlock * kParts - 1;
    static constexpr std::uint32_t kCentre = (kTaps - 1) / 2, kLatency = kBlock + kCentre;
    static_assert(kDesign >= 2 * kTaps);
    static constexpr int kRedesignBlocks = 3;   // at most one rebuild every 3 blocks (32 ms) while a knob moves

    void prepare(double fs, LinSource* src) {
        fs_ = fs;
        src_ = src;
        fftD_.prepare(static_cast<int>(kDesign));
        fftB_.prepare(static_cast<int>(kFft));
        design_.resize(kDesign);
        for (int w = 0; w < 3; ++w) {
            win_[w].resize(kTaps);
            for (std::uint32_t t = 0; t < kTaps; ++t) {
                const double a = 2.0 * std::numbers::pi * t / (kTaps - 1);
                double v = 0;
                if (w == 0) v = 0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2 * a);
                else if (w == 1) v = 0.5 - 0.5 * std::cos(a);
                else v = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2 * a) - 0.01168 * std::cos(3 * a);
                win_[w][t] = static_cast<float>(v);
            }
        }
        for (auto& b : bank_) b.assign(static_cast<std::size_t>(kParts) * kFft, {});
        fdl_.assign(static_cast<std::size_t>(kParts) * kFft, {});
        acc_.assign(kFft, {});
        acc2_.assign(kFft, {});
        in_.assign(kFft, {});
        outL_.assign(kBlock, 0.0f);
        outR_.assign(kBlock, 0.0f);
        reset();
    }
    void reset() noexcept {
        std::fill(fdl_.begin(), fdl_.end(), Cx{});
        std::fill(in_.begin(), in_.end(), Cx{});
        std::fill(outL_.begin(), outL_.end(), 0.0f);
        std::fill(outR_.begin(), outR_.end(), 0.0f);
        pos_ = slot_ = 0;
        active_ = 0;
        sinceDesign_ = kRedesignBlocks;
        pending_ = false;
        src_->takeChange();
        build(bank_[0]);
    }
    // L / R in place, delayed by kLatency
    void process(float* L, float* R, std::uint32_t n) noexcept {
        for (std::uint32_t i = 0; i < n; ++i) {
            const float yl = outL_[pos_], yr = outR_[pos_];
            in_[kBlock + pos_] = {L[i], R[i]};
            L[i] = yl;
            R[i] = yr;
            if (++pos_ == kBlock) { pos_ = 0; block(); }
        }
    }

private:
    using Cx = std::complex<float>;

    // the filter for the source's kept settings into one bank (no allocation)
    void build(std::vector<Cx>& bank) noexcept {
        const std::uint32_t bins = kDesign / 2 + 1;
        const double top = fs_ * 0.5;
        for (std::uint32_t k = 0; k < bins; ++k) {
            const double hz = std::max(top * k / (bins - 1), 0.5);
            design_[k] = {static_cast<float>(std::pow(10.0, std::clamp(src_->magnitudeDb(hz), -200.0, 60.0) / 20.0)), 0.0f};
        }
        for (std::uint32_t k = bins; k < kDesign; ++k) design_[k] = design_[kDesign - k];
        fftD_.inverse(design_.data());
        const std::vector<float>& win = win_[std::clamp(src_->window(), 0, 2)];
        for (std::uint32_t part = 0; part < kParts; ++part) {
            Cx* h = &bank[static_cast<std::size_t>(part) * kFft];
            for (std::uint32_t j = 0; j < kFft; ++j) h[j] = {};
            for (std::uint32_t j = 0; j < kBlock; ++j) {
                const std::uint32_t t = part * kBlock + j;
                if (t >= kTaps) continue;
                const std::uint32_t src = (t + kDesign - kCentre) % kDesign;
                h[j] = {design_[src].real() * win[t], 0.0f};
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
        Cx* slot = &fdl_[static_cast<std::size_t>(slot_) * kFft];
        std::copy(in_.begin(), in_.end(), slot);
        fftB_.forward(slot);
        std::copy(in_.begin() + kBlock, in_.end(), in_.begin());
        ++sinceDesign_;
        pending_ = src_->takeChange() || pending_;
        convolve(bank_[active_], acc_);
        if (pending_ && sinceDesign_ >= kRedesignBlocks) {
            pending_ = false;
            sinceDesign_ = 0;
            build(bank_[1 - active_]);
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
    LinSource* src_ = nullptr;
    hc::fx::Fft fftD_, fftB_;
    std::vector<Cx> design_, bank_[2], fdl_, acc_, acc2_, in_;
    std::vector<float> win_[3], outL_, outR_;
    std::uint32_t pos_ = 0, slot_ = 0;
    int active_ = 0, sinceDesign_ = 0;
    bool pending_ = false;
};

// ---- minimum-phase bands ----------------------------------------------------------------------
// One band's settings (plain values).
struct BandSet {
    Shape shape = Shape::Off;
    float hz = 1000.0f, gainDb = 0.0f, q = 1.0f;
    bool operator==(const BandSet&) const = default;
};

// Up to kMax biquads per side. Each band's frequency, gain and Q are eased toward their targets
// (about 20 ms) and the coefficients refreshed every 4 samples while they move; a change of shape
// or switching a band crossfades it in or out over 20 ms instead of jumping.
template <int kMax>
class IirBands {
public:
    void prepare(double fs) noexcept {
        fs_ = fs;
        ease_ = 1.0f - std::exp(-4.0f / static_cast<float>(fs * 0.020));
        fadeStep_ = static_cast<float>(1.0 / (fs * 0.020));
        clear();
    }
    void clear() noexcept {
        for (int b = 0; b < kMax; ++b) {
            for (auto& s : bq_[b]) s.clear();
            cur_[b] = want_[b];
            mix_[b] = want_[b].shape == Shape::Off ? 0.0f : 1.0f;
            design(b);
        }
        tick_ = 0;
    }
    void set(int b, const BandSet& s) noexcept { want_[b] = s; }
    // the coefficients the band runs now (for tests)
    const C& coefs(int b) const noexcept { return c_[b]; }

    // one side (0 / 1) of one sample through every band
    float run(int side, float x) noexcept {
        for (int b = 0; b < used_; ++b) {
            if (mix_[b] <= 0.0f && cur_[b].shape == Shape::Off) continue;
            const float y = bq_[b][side].tick(x);
            x = x + mix_[b] * (y - x);
        }
        return x;
    }
    // call once per sample, before run() for both sides
    void step() noexcept {
        if (++tick_ < 4) return;
        tick_ = 0;
        for (int b = 0; b < used_; ++b) {
            BandSet& c = cur_[b];
            const BandSet& w = want_[b];
            if (w.shape != c.shape) {   // fade out, switch, fade in
                mix_[b] = std::max(0.0f, mix_[b] - 4.0f * fadeStep_);
                if (mix_[b] <= 0.0f) {
                    c = w;
                    bq_[b][0].clear();
                    bq_[b][1].clear();
                    design(b);
                }
                continue;
            }
            if (c.shape != Shape::Off && mix_[b] < 1.0f) mix_[b] = std::min(1.0f, mix_[b] + 4.0f * fadeStep_);
            if (c == w) continue;
            auto toward = [&](float& v, float t, bool logScale) {
                if (logScale) v = std::exp(std::log(v) + ease_ * (std::log(t) - std::log(v)));
                else v += ease_ * (t - v);
                if (std::fabs(v - t) < (logScale ? 1e-4f * t : 1e-4f)) v = t;
            };
            toward(c.hz, w.hz, true);
            toward(c.gainDb, w.gainDb, false);
            toward(c.q, w.q, true);
            design(b);
        }
    }
    void bands(int n) noexcept { used_ = std::clamp(n, 0, kMax); }

private:
    void design(int b) noexcept {
        C c[1];
        if (biquadFor(cur_[b].shape, fs_, cur_[b].hz, cur_[b].gainDb, cur_[b].q, c) == 1) c_[b] = c[0];
        else c_[b] = C{};
        bq_[b][0].c = bq_[b][1].c = c_[b];
    }

    double fs_ = 48000.0;
    float ease_ = 0.5f, fadeStep_ = 0.001f;
    BandSet want_[kMax]{}, cur_[kMax]{};
    C c_[kMax]{};
    hc::fx::Biquad bq_[kMax][2]{};
    float mix_[kMax]{};
    int used_ = kMax, tick_ = 0;
};

} // namespace hem::rack::eqkit
