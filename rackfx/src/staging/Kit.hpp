#pragma once
// =============================================================================
//  Staging kit — plumbing shared by the staging plug-ins (the 47 new HEM engines).
//  Only building blocks live here (filters, meters, a pitch detector, oversampling,
//  a Hilbert pair). Every plug-in's sound engine is its own code in its own file.
//  Audio-thread rule: nothing here allocates after prepare().
// =============================================================================
#include "hybrid/fx/Dsp.hpp"
#include "hybrid/fx/Fx.hpp"
#include "hybrid/fx/Fft.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

namespace hem::staging::kit {

inline constexpr float kPi  = std::numbers::pi_v<float>;
inline constexpr float kTau = 2.0f * std::numbers::pi_v<float>;

[[nodiscard]] inline float dbToGain(float db) noexcept { return db <= -119.0f ? 0.0f : std::pow(10.0f, db * 0.05f); }
[[nodiscard]] inline float gainToDb(float g) noexcept { return 20.0f * std::log10(std::max(g, 1e-6f)); }
[[nodiscard]] inline bool  isFlat(float v, float neutral = 0.0f) noexcept { return std::fabs(v - neutral) < 1e-6f; }

// Safety net: a non-finite or runaway sample becomes silence (never reached in normal use).
[[nodiscard]] inline float sane(float x) noexcept { return std::isfinite(x) ? std::clamp(x, -64.0f, 64.0f) : 0.0f; }

// ---- peak meter in dBFS with a release (ballistics only, never touches audio) ----------------
struct PeakMeter {
    float env = 0.0f, rel = 0.0f;
    void prepare(double fs, double releaseMs = 300.0) noexcept { rel = hc::fx::onePoleCoef(fs, releaseMs); env = 0.0f; }
    void feed(const float* x, std::uint32_t n) noexcept {
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::fabs(x[i]));
        const float decayed = env * std::pow(rel, static_cast<float>(n));
        env = std::max(pk, decayed);
    }
    [[nodiscard]] float db() const noexcept { return gainToDb(env); }
};

// ---- topology-preserving state-variable filter (stable under fast modulation) ----------------
struct Svf {
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set(double fs, double hz, double q) noexcept {
        hz = std::clamp(hz, 10.0, fs * 0.45);
        g  = static_cast<float>(std::tan(std::numbers::pi * hz / fs));
        k  = static_cast<float>(1.0 / std::max(q, 0.05));
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void clear() noexcept { ic1 = ic2 = 0; }
    // returns low, band, high through references
    void tick(float x, float& lp, float& bp, float& hp) noexcept {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    [[nodiscard]] float low(float x) noexcept { float l, b, h; tick(x, l, b, h); return l; }
    [[nodiscard]] float band(float x) noexcept { float l, b, h; tick(x, l, b, h); return b; }
    [[nodiscard]] float high(float x) noexcept { float l, b, h; tick(x, l, b, h); return h; }
};

// ---- pitch detector: McLeod normalized square difference (NSDF), correlation by FFT ---------
// Runs on a mono feed; analyses every `hop` samples over a window of 2 * maxPeriod.
class PitchDetector {
public:
    void prepare(double fs, float minHz = 50.0f, float maxHz = 1200.0f, std::uint32_t hop = 256) {
        fs_ = fs;
        maxLag_ = static_cast<std::uint32_t>(std::ceil(fs / minHz));
        minLag_ = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(std::floor(fs / maxHz)));
        win_ = 2 * maxLag_;
        cmp_ = win_ - maxLag_;
        buf_.assign(win_ * 2, 0.0f);
        nsdf_.assign(maxLag_ + 2, 0.0f);
        pre_.assign(win_ + 1, 0.0);
        int n = 1; while (n < static_cast<int>(2 * win_)) n <<= 1;
        fft_.prepare(n);
        a_.assign(static_cast<std::size_t>(n), {});
        b_.assign(static_cast<std::size_t>(n), {});
        hop_ = std::max<std::uint32_t>(32, hop);
        pos_ = 0; count_ = 0; hz_ = 0; conf_ = 0;
    }
    void setHop(std::uint32_t hop) noexcept { hop_ = std::max<std::uint32_t>(32, hop); }
    void setRange(float loHz, float hiHz) noexcept { lo_ = loHz; hi_ = hiHz; }
    void reset() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = count_ = 0; hz_ = conf_ = 0; }
    void push(float x) noexcept {
        buf_[pos_] = x; buf_[pos_ + win_] = x;           // double-written ring: contiguous window read
        pos_ = (pos_ + 1) % win_;
        if (++count_ >= hop_) { count_ = 0; analyse(); }
    }
    [[nodiscard]] float hz() const noexcept { return hz_; }          // 0 = no clear pitch
    [[nodiscard]] float confidence() const noexcept { return conf_; } // 0..1

private:
    void analyse() noexcept {
        const float* x = buf_.data() + pos_;   // oldest .. newest, win_ long
        pre_[0] = 0.0;
        for (std::uint32_t i = 0; i < win_; ++i) pre_[i + 1] = pre_[i] + static_cast<double>(x[i]) * x[i];
        if (pre_[win_] < 1e-7 * win_) { hz_ = 0; conf_ = 0; return; }
        const std::size_t n = a_.size();
        for (std::size_t i = 0; i < n; ++i) { a_[i] = {}; b_[i] = {}; }
        for (std::uint32_t i = 0; i < cmp_; ++i) a_[i] = {x[i], 0.0f};
        for (std::uint32_t i = 0; i < win_; ++i) b_[i] = {x[i], 0.0f};
        fft_.forward(a_.data()); fft_.forward(b_.data());
        for (std::size_t i = 0; i < n; ++i) b_[i] = std::conj(a_[i]) * b_[i];
        fft_.inverse(b_.data());                         // b_[tau] = sum_i x[i] x[i + tau], i < cmp_
        const double e0 = pre_[cmp_];
        for (std::uint32_t tau = 0; tau <= maxLag_; ++tau) {
            const double m = e0 + (pre_[tau + cmp_] - pre_[tau]);
            nsdf_[tau] = m > 1e-12 ? static_cast<float>(2.0 * b_[tau].real() / m) : 0.0f;
        }
        // key maxima between positive zero crossings; take the first within 0.9 of the highest
        const std::uint32_t lagLo = std::max(minLag_, static_cast<std::uint32_t>(fs_ / std::max(hi_, 1.0f)));
        const std::uint32_t lagHi = std::min(maxLag_ - 1, static_cast<std::uint32_t>(fs_ / std::max(lo_, 1.0f)));
        float best = 0.0f; std::uint32_t bestTau = 0;
        std::array<std::uint32_t, 64> cand{}; std::array<float, 64> candV{}; int nc = 0;
        std::uint32_t tau = 1;
        while (tau < maxLag_ && nsdf_[tau] > 0.0f) ++tau;          // leave the zero-lag lobe
        while (tau < maxLag_ && nc < 64) {
            while (tau < maxLag_ && nsdf_[tau] <= 0.0f) ++tau;
            std::uint32_t pk = tau; float pv = -1.0f;
            while (tau < maxLag_ && nsdf_[tau] > 0.0f) { if (nsdf_[tau] > pv) { pv = nsdf_[tau]; pk = tau; } ++tau; }
            if (pv > 0.0f && pk >= lagLo && pk <= lagHi) {
                cand[static_cast<std::size_t>(nc)] = pk; candV[static_cast<std::size_t>(nc)] = pv; ++nc; best = std::max(best, pv);
            }
        }
        for (int i = 0; i < nc; ++i)
            if (candV[static_cast<std::size_t>(i)] >= 0.9f * best) { bestTau = cand[static_cast<std::size_t>(i)]; break; }
        if (bestTau < 1 || best < 0.5f) { hz_ = 0; conf_ = std::max(0.0f, best); return; }
        const float a = nsdf_[bestTau - 1], b = nsdf_[bestTau], c = nsdf_[bestTau + 1];
        const float den = a - 2.0f * b + c;
        const float shift = std::fabs(den) > 1e-9f ? std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        hz_ = static_cast<float>(fs_ / (static_cast<double>(bestTau) + shift));
        conf_ = b;
    }
    double fs_ = 48000;
    std::uint32_t minLag_ = 40, maxLag_ = 960, win_ = 1920, cmp_ = 960, hop_ = 256, pos_ = 0, count_ = 0;
    float lo_ = 1.0f, hi_ = 100000.0f;
    std::vector<float> buf_, nsdf_;
    std::vector<double> pre_;
    std::vector<std::complex<float>> a_, b_;
    hc::fx::Fft fft_;
    float hz_ = 0, conf_ = 0;
};

// ---- 2x halfband up/down sampler (31-tap linear-phase FIR), cascade for 4x / 8x ------------
class Halfband2x {
public:
    static constexpr int kTaps = 31;
    Halfband2x() {
        const int m = kTaps - 1;
        double sum = 0;
        for (int i = 0; i < kTaps; ++i) {
            const double t = i - m / 2.0;
            const double sinc = t == 0 ? 0.5 : std::sin(std::numbers::pi * 0.5 * t) / (std::numbers::pi * t);
            const double w = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * i / m) + 0.08 * std::cos(4 * std::numbers::pi * i / m);
            h_[static_cast<std::size_t>(i)] = static_cast<float>(sinc * w);
            sum += sinc * w;
        }
        for (auto& v : h_) v = static_cast<float>(v / sum);
    }
    void clear() noexcept { up_.fill(0.0f); dn_.fill(0.0f); upPos_ = dnPos_ = 0; }
    // one base sample -> two oversampled
    void up(float x, float& y0, float& y1) noexcept {
        y0 = fir(up_, upPos_, 2.0f * x);
        y1 = fir(up_, upPos_, 0.0f);
    }
    // two oversampled -> one base sample
    [[nodiscard]] float down(float x0, float x1) noexcept {
        const float a = fir(dn_, dnPos_, x0);
        (void)a;
        return fir(dn_, dnPos_, x1);
    }
    [[nodiscard]] static constexpr std::uint32_t latencyBase() noexcept { return (kTaps - 1) / 2; }   // round trip, base samples

private:
    float fir(std::array<float, kTaps>& z, int& pos, float x) noexcept {
        z[static_cast<std::size_t>(pos)] = x;
        float acc = 0.0f;
        int j = pos;
        for (int i = 0; i < kTaps; ++i) { acc += h_[static_cast<std::size_t>(i)] * z[static_cast<std::size_t>(j)]; j = j == 0 ? kTaps - 1 : j - 1; }
        pos = (pos + 1) % kTaps;
        return acc;
    }
    std::array<float, kTaps> h_{}, up_{}, dn_{};
    int upPos_ = 0, dnPos_ = 0;
};

// ---- Hilbert pair: two allpass chains whose outputs are 90° apart (20 Hz .. 20 kHz) ----------
class HilbertPair {
public:
    void clear() noexcept { for (auto& s : sa_) s = {}; for (auto& s : sb_) s = {}; delayA_ = 0; }
    void tick(float x, float& re, float& im) noexcept {
        static constexpr std::array<float, 4> ka{0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
        static constexpr std::array<float, 4> kb{0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};
        float a = x, b = x;
        for (std::size_t i = 0; i < 4; ++i) a = ap(sa_[i], ka[i] * ka[i], a);
        for (std::size_t i = 0; i < 4; ++i) b = ap(sb_[i], kb[i] * kb[i], b);
        re = delayA_; delayA_ = a; im = b;   // the one-sample delay sits on the first chain (measured: 90 deg +-1 from 50 Hz to 15 kHz)
    }

private:
    struct St { float x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    static float ap(St& s, float c, float x) noexcept {   // second-order allpass in z^-2 (polyphase IIR Hilbert)
        const float y = c * (x + s.y2) - s.x2;   // out = a^2 (in + out[t-2]) - in[t-2]
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
    std::array<St, 4> sa_{}, sb_{};
    float delayA_ = 0;
};

// ---- a value that glides to its target, stepped once per block (for filter gains) --------------
struct BlockGlide {
    float cur = 0, coef = 0;
    void prepare(double fs, std::uint32_t block, double ms, float v) noexcept { coef = std::exp(-static_cast<float>(block) / static_cast<float>(fs * ms * 0.001)); cur = v; }
    float step(float target) noexcept {
        cur = target + coef * (cur - target);
        if (std::fabs(cur - target) < 1e-3f) cur = target;
        return cur;
    }
};

// ---- small helpers ---------------------------------------------------------------------------
[[nodiscard]] inline float softClip(float x) noexcept { return std::tanh(x); }
[[nodiscard]] inline float hardClip(float x, float ceil = 1.0f) noexcept { return std::clamp(x, -ceil, ceil); }
[[nodiscard]] inline float midiToHz(float note) noexcept { return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f); }
[[nodiscard]] inline float hzToMidi(float hz) noexcept { return 69.0f + 12.0f * std::log2(std::max(hz, 1e-3f) / 440.0f); }

} // namespace hem::staging::kit
