#pragma once
// =============================================================================
//  The parts the HEM maximizers share (BIG SHOT, LAST WORD, HIGH STAKES, TOP DOG, SWEET SIXTEEN,
//  FAST MONEY, QUICK CASH). rackfx-private. Nothing here allocates after prepare().
//
//  Idr          word length (quantize), dither TYPE 1 / TYPE 2 and noise shaping MODERATE / NORMAL /
//               ULTRA: the last step before the output.
//  TruePeak4    the peak between the samples (4x, windowed-sinc), 4 samples late.
//  HoldMin, Box the look-ahead: the smallest gain wanted over the window, and a running average.
//  LinearSplit  linear-phase band split (up to 16 bands) by partitioned FFT convolution:
//               the bands add back up to the input exactly (late by kLatency).
//  Shared       the multiband limiting stage: one wanted reduction for the whole signal, shared out
//               over the bands by PRIORITY and SEPARATION, each band with its own release.
//  Catch        a short wideband look-ahead limiter that catches whatever the bands left over.
// =============================================================================
#include "../arsenal/Common.hpp"

#include "hybrid/fx/Fft.hpp"

#include <complex>
#include <vector>

namespace hem::rack::maxim {
using namespace arsenal;

// ---- names shared by the plates
inline const char* const kQuantN[] = {"24 BIT", "22 BIT", "20 BIT", "18 BIT", "16 BIT"};
inline constexpr int kQuantBits[] = {24, 22, 20, 18, 16};
inline const char* const kDitherN[] = {"TYPE 1", "TYPE 2", "NONE"};
inline const char* const kShapeN[] = {"MODERATE", "NORMAL", "ULTRA", "NONE"};
// release behaviour of a multiband limiter: how program-dependent each band's release is
enum Behave : int { Auto, AutoLows, AutoHighs, Hard, Manual };
inline const char* const kBehaveN[] = {"AUTO", "AUTO LOWS", "AUTO HIGHS", "HARD", "MANUAL"};

inline float gainOfDb(float db) noexcept { return std::pow(10.0f, db * 0.05f); }

// ---- IDR: quantize to `bits` (0 = leave the word length alone), dither, noise shaping.
//  Error feedback: the quantizer's error (dither included) is fed back through the shaping filter,
//  so the noise that comes out is 1 + n1 z^-1 + ... (the noise transfer) times the plain error:
//  MODERATE a gentle dip around 4 kHz, NORMAL a deep one, ULTRA deep at 3 kHz and 9 kHz (4th order).
//  TYPE 1: triangular dither of +-1 LSB (flat). TYPE 2: half that, tilted up to the highs (less
//  noise, a little low-level distortion). After 32 samples of digital silence nothing is added.
class Idr {
public:
    void prepare(double fs) {
        auto pair = [&](double hz, double r, double* c) {
            const double w = 2.0 * std::numbers::pi * hz / fs;
            c[0] = -2.0 * r * std::cos(w);
            c[1] = r * r;
        };
        double a[2], b[2];
        pair(4000.0, 0.65, a);
        nt_[0][0] = static_cast<float>(a[0]); nt_[0][1] = static_cast<float>(a[1]); ord_[0] = 2;
        pair(4000.0, 0.9, a);
        nt_[1][0] = static_cast<float>(a[0]); nt_[1][1] = static_cast<float>(a[1]); ord_[1] = 2;
        pair(3000.0, 0.95, a);
        pair(9000.0, 0.85, b);
        nt_[2][0] = static_cast<float>(a[0] + b[0]);
        nt_[2][1] = static_cast<float>(a[1] + a[0] * b[0] + b[1]);
        nt_[2][2] = static_cast<float>(a[0] * b[1] + a[1] * b[0]);
        nt_[2][3] = static_cast<float>(a[1] * b[1]);
        ord_[2] = 4;
        ord_[3] = 0;
        clear();
    }
    void clear() noexcept {
        for (auto& c : e_) for (float& v : c) v = 0.0f;
        prev_[0] = prev_[1] = 0.0f;
        quiet_[0] = quiet_[1] = 1 << 20;   // starts out silent
    }
    // bits 0 = off; dither 0 TYPE 1, 1 TYPE 2, 2 NONE; shape 0 MODERATE, 1 NORMAL, 2 ULTRA, 3 NONE
    void set(int bits, int dither, int shape, float ceiling) noexcept {
        if (bits != bits_ || shape != shape_) clear();
        bits_ = bits;
        dither_ = dither;
        shape_ = std::clamp(shape, 0, 3);
        if (bits_ > 0) {
            lsb_ = std::ldexp(1.0f, 1 - bits_);
            top_ = std::floor(ceiling / lsb_) * lsb_;
        }
    }
    [[nodiscard]] bool on() const noexcept { return bits_ > 0; }
    [[nodiscard]] float tick(int c, float x) noexcept {
        if (bits_ == 0) return x;
        {   // digital silence stays silence (both dither types)
            quiet_[c] = std::fabs(x) < 1e-10f ? std::min(quiet_[c] + 1, 1 << 20) : 0;
            if (quiet_[c] > 32) {
                for (float& v : e_[c]) v = 0.0f;
                return 0.0f;
            }
        }
        const int ord = ord_[shape_];
        float u = x;
        for (int i = 0; i < ord; ++i) u += nt_[shape_][i] * e_[c][i];
        float d = 0.0f;
        if (dither_ == 0) d = (rand01() + rand01() - 1.0f) * lsb_;
        else if (dither_ == 1) {
            const float r = rand01() - 0.5f;
            d = 0.5f * (r - prev_[c]) * lsb_;
            prev_[c] = r;
        }
        const float y = std::clamp(std::round((u + d) / lsb_) * lsb_, -top_, top_);
        const float err = std::clamp(y - u, -4.0f * lsb_, 4.0f * lsb_);
        for (int i = ord - 1; i > 0; --i) e_[c][i] = e_[c][i - 1];
        if (ord > 0) e_[c][0] = err;
        return y;
    }

private:
    float rand01() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) / 16777216.0f;
    }
    float nt_[4][4]{};
    int ord_[4]{};
    int bits_ = 0, dither_ = 0, shape_ = 3;
    float lsb_ = 1.0f / 8388608.0f, top_ = 1.0f;
    float e_[2][4]{};
    float prev_[2]{};
    int quiet_[2]{};
    std::uint32_t rng_ = 0x51A7E5u;
};

// ---- the peak between the samples: x[n-4] itself and three points between x[n-4] and x[n-3]
class TruePeak4 {
public:
    static constexpr std::uint32_t kDelay = 4;
    TruePeak4() {
        for (int j = 1; j < 4; ++j) {
            double sum = 0;
            for (int k = 0; k < 8; ++k) {
                const double t = k - 4.0 + j * 0.25;   // from tap x[n-k] to the point
                const double s = std::fabs(t) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
                const double w = 0.5 + 0.5 * std::cos(std::numbers::pi * t / 4.5);
                h_[j - 1][k] = static_cast<float>(s * w);
                sum += s * w;
            }
            for (int k = 0; k < 8; ++k) h_[j - 1][k] = static_cast<float>(h_[j - 1][k] / sum);
        }
    }
    void clear() noexcept { for (float& v : x_) v = 0.0f; }
    // push the newest sample; the largest |value| around the sample kDelay ago
    [[nodiscard]] float late() const noexcept { return x_[4]; }   // the plain sample kDelay ago
    [[nodiscard]] float tick(float in) noexcept {
        for (int k = 7; k > 0; --k) x_[k] = x_[k - 1];
        x_[0] = in;
        float m = std::fabs(x_[4]);
        for (int j = 0; j < 3; ++j) {
            float v = 0.0f;
            for (int k = 0; k < 8; ++k) v += h_[j][k] * x_[k];
            m = std::max(m, std::fabs(v));
        }
        return m;
    }

private:
    float h_[3][8]{};
    float x_[8]{};
};

// ---- the look-ahead window: the smallest of the last n values, and a running average
struct HoldMin {
    std::vector<float> v;
    std::uint32_t pos = 0;
    void prepare(std::uint32_t n) { v.assign(n, 1.0f); pos = 0; }
    void clear() noexcept { std::fill(v.begin(), v.end(), 1.0f); pos = 0; }
    [[nodiscard]] float tick(float x) noexcept {
        v[pos] = x;
        pos = (pos + 1) % static_cast<std::uint32_t>(v.size());
        float m = x;
        for (float a : v) m = std::min(m, a);
        return m;
    }
};
struct Box {
    std::vector<float> v;
    std::uint32_t pos = 0;
    double sum = 0;
    float fill = 1.0f;
    void prepare(std::uint32_t n, float f = 1.0f) { fill = f; v.assign(n, f); clear(); }
    void clear() noexcept {
        std::fill(v.begin(), v.end(), fill);
        pos = 0;
        sum = static_cast<double>(fill) * static_cast<double>(v.size());
    }
    [[nodiscard]] float tick(float x) noexcept {
        sum += static_cast<double>(x) - v[pos];
        v[pos] = x;
        if (++pos == v.size()) {   // re-add once per lap so rounding never drifts
            pos = 0;
            double s = 0;
            for (float a : v) s += a;
            sum = s;
        }
        return static_cast<float>(sum / static_cast<double>(v.size()));
    }
};

// one-pole step for a time constant of `seconds` (cheap; accurate for anything longer than a few samples)
inline float stepK(double fs, double seconds) noexcept {
    const double x = 1.0 / std::max(fs * seconds, 1.0);
    return static_cast<float>(x / (1.0 + 0.5 * x));
}

// how program-dependent band b (of n) releases under a behaviour (0 = its own release time)
inline float adaptivity(int behave, int b, int n) noexcept {
    const float t = n > 1 ? static_cast<float>(b) / static_cast<float>(n - 1) : 0.5f;
    switch (behave) {
    case AutoLows: return 1.0f - 0.8f * t;
    case AutoHighs: return 0.2f + 0.8f * t;
    case Manual: return 0.0f;
    default: return 1.0f;
    }
}
// the release actually used (s): the band's own time, moved toward a program-dependent one —
// short while the limiting is only now and then (peaks get let go quickly), long once it has been
// working hard for a while (no pumping). HARD stays short.
inline float releaseS(float baseS, float adapt, float memory, bool hard) noexcept {
    const float programmed = baseS * (hard ? 0.15f + 0.6f * memory : 0.3f + 1.7f * memory);
    return baseS + adapt * (programmed - baseS);
}

// ---- linear-phase band split by partitioned FFT convolution.
//  Each crossover is a windowed-sinc low-pass (Blackman, `len` taps, all centred on the same tap);
//  band 0 = LP0, band k = LPk - LPk-1, the top band = the input (delayed) - the last LP, so the bands
//  always add back up to the input exactly. Left and right ride in one complex FFT.
class LinearSplit {
public:
    static constexpr std::uint32_t kBlock = 512, kParts = 8, kTaps = kBlock * kParts - 1, kCentre = (kTaps - 1) / 2;
    static constexpr std::uint32_t kFft = 2 * kBlock;
    static constexpr std::uint32_t kLatency = kBlock + kCentre;   // 2559 samples (53 ms at 48 kHz)
    static constexpr int kMaxX = 15;
    using Cx = std::complex<float>;

    // xovers: how many crossovers; extraSets: stored kernel sets to switch between (SWEET SIXTEEN)
    void prepare(double fs, int xovers, int extraSets = 0) {
        fs_ = fs;
        nx_ = std::clamp(xovers, 1, kMaxX);
        fft_.prepare(static_cast<int>(kFft));
        const std::size_t bank = static_cast<std::size_t>(nx_) * kParts * kFft;
        bank_[0].assign(bank, {});
        bank_[1].assign(bank, {});
        sets_.assign(static_cast<std::size_t>(extraSets), std::vector<Cx>(bank));
        fdl_.assign(static_cast<std::size_t>(kParts) * kFft, {});
        in_.assign(kFft, {});
        acc_.assign(kFft, {});
        acc2_.assign(kFft, {});
        tap_.assign(kTaps, 0.0f);
        lp_.assign(static_cast<std::size_t>(nx_) * kBlock, {});
        dly_[0].prepare(kLatency);
        dly_[1].prepare(kLatency);
        clear();
    }
    void clear() noexcept {
        std::fill(fdl_.begin(), fdl_.end(), Cx{});
        std::fill(in_.begin(), in_.end(), Cx{});
        std::fill(lp_.begin(), lp_.end(), Cx{});
        dly_[0].clear();
        dly_[1].clear();
        pos_ = 0;
        slot_ = 0;
        pending_ = -1;
    }
    [[nodiscard]] int bands() const noexcept { return nx_ + 1; }

    // the low-passes for these crossovers (rising), `len` taps long, into the live bank (no fade: for prepare / reset)
    void designNow(const float* hz, std::uint32_t len) noexcept {
        build(bank_[active_], hz, len);
    }
    // into a stored set (prepare only)
    void designSet(int set, const float* hz, std::uint32_t len) noexcept { build(sets_[static_cast<std::size_t>(set)], hz, len); }
    // fade to new crossovers over the next block (no allocation; at most once per block)
    void fadeTo(const float* hz, std::uint32_t len) noexcept {
        build(bank_[1 - active_], hz, len);
        pending_ = 1;
    }
    // fade to a stored set
    void fadeToSet(int set) noexcept {
        const auto& s = sets_[static_cast<std::size_t>(set)];
        std::copy(s.begin(), s.end(), bank_[1 - active_].begin());
        pending_ = 1;
    }
    void useSet(int set) noexcept {
        const auto& s = sets_[static_cast<std::size_t>(set)];
        std::copy(s.begin(), s.end(), bank_[active_].begin());
    }
    [[nodiscard]] bool fading() const noexcept { return pending_ >= 0; }

    // one stereo sample in; the bands (kLatency late) out: bl[0..bands-1], br[..]
    void tick(float l, float r, float* bl, float* br) noexcept {
        const float dl = dly_[0].process(l, kLatency), dr = dly_[1].process(r, kLatency);
        float prevL = 0.0f, prevR = 0.0f;
        for (int k = 0; k < nx_; ++k) {
            const Cx v = lp_[static_cast<std::size_t>(k) * kBlock + pos_];
            bl[k] = v.real() - prevL;
            br[k] = v.imag() - prevR;
            prevL = v.real();
            prevR = v.imag();
        }
        bl[nx_] = dl - prevL;
        br[nx_] = dr - prevR;
        in_[kBlock + pos_] = {l, r};
        if (++pos_ == kBlock) { pos_ = 0; block(); }
    }

private:
    void build(std::vector<Cx>& bank, const float* hz, std::uint32_t len) noexcept {
        len = std::clamp<std::uint32_t>(len | 1u, 63u, kTaps);
        const int half = static_cast<int>(len / 2);
        float lo = 10.0f;
        for (int k = 0; k < nx_; ++k) {
            // rising, at least 5 % apart, under 0.45 fs
            const float f = std::clamp(std::max(hz[k], lo * 1.05f), 10.0f, static_cast<float>(fs_ * 0.45));
            lo = f;
            const double wc = 2.0 * std::numbers::pi * f / fs_;
            double sum = 0;
            for (std::uint32_t t = 0; t < kTaps; ++t) {
                const int u = static_cast<int>(t) - static_cast<int>(kCentre);
                if (std::abs(u) > half) { tap_[t] = 0.0f; continue; }
                const double s = u == 0 ? wc / std::numbers::pi : std::sin(wc * u) / (std::numbers::pi * u);
                const double a = 2.0 * std::numbers::pi * (u + half) / static_cast<double>(len - 1);
                const double w = 0.42 - 0.5 * std::cos(a) + 0.08 * std::cos(2.0 * a);
                tap_[t] = static_cast<float>(s * w);
                sum += s * w;
            }
            const float norm = static_cast<float>(1.0 / sum);
            for (std::uint32_t part = 0; part < kParts; ++part) {
                Cx* h = &bank[(static_cast<std::size_t>(k) * kParts + part) * kFft];
                for (std::uint32_t j = 0; j < kFft; ++j) h[j] = {};
                for (std::uint32_t j = 0; j < kBlock; ++j) {
                    const std::uint32_t t = part * kBlock + j;
                    if (t < kTaps) h[j] = {tap_[t] * norm, 0.0f};
                }
                fft_.forward(h);
            }
        }
    }
    void convolve(const std::vector<Cx>& bank, int k, std::vector<Cx>& acc) const noexcept {
        std::fill(acc.begin(), acc.end(), Cx{});
        for (std::uint32_t part = 0; part < kParts; ++part) {
            const Cx* x = &fdl_[static_cast<std::size_t>((slot_ + kParts - part) % kParts) * kFft];
            const Cx* h = &bank[(static_cast<std::size_t>(k) * kParts + part) * kFft];
            for (std::uint32_t j = 0; j < kFft; ++j) acc[j] += x[j] * h[j];
        }
        fft_.inverse(acc.data());
    }
    void block() noexcept {
        Cx* slot = &fdl_[static_cast<std::size_t>(slot_) * kFft];
        std::copy(in_.begin(), in_.end(), slot);
        fft_.forward(slot);
        std::copy(in_.begin() + kBlock, in_.end(), in_.begin());
        const bool fade = pending_ >= 0;
        for (int k = 0; k < nx_; ++k) {
            Cx* out = &lp_[static_cast<std::size_t>(k) * kBlock];
            convolve(bank_[active_], k, acc_);
            if (fade) {
                convolve(bank_[1 - active_], k, acc2_);
                for (std::uint32_t j = 0; j < kBlock; ++j) {
                    const float t = (static_cast<float>(j) + 0.5f) / static_cast<float>(kBlock);
                    out[j] = acc_[kBlock + j] + t * (acc2_[kBlock + j] - acc_[kBlock + j]);
                }
            } else {
                for (std::uint32_t j = 0; j < kBlock; ++j) out[j] = acc_[kBlock + j];
            }
        }
        if (fade) { active_ = 1 - active_; pending_ = -1; }
        slot_ = (slot_ + 1) % kParts;
    }

    double fs_ = 48000.0;
    int nx_ = 1, active_ = 0, pending_ = -1;
    hc::fx::Fft fft_;
    std::vector<Cx> bank_[2], fdl_, in_, acc_, acc2_, lp_;
    std::vector<std::vector<Cx>> sets_;
    std::vector<float> tap_;
    hc::fx::DelayLine dly_[2];
    std::uint32_t pos_ = 0, slot_ = 0;
};

// ---- the multiband limiting stage.
//  The whole signal's peak (the bands added up) decides how much has to come off; that reduction is
//  shared out over the bands: with SEPARATION 0 every band gives the same; at 100 the bands with the
//  most energy give the most. PRIORITY tilts it (a band with more priority gives up less, the others
//  more; on the energy the bands carry it averages to the same total). Each band then lets go at
//  its own release, and every gain change is smoothed over the look-ahead (no clicks).
template <int kMaxB>
class Shared {
public:
    struct Set {
        int bands = 1;
        float ceiling = 1.0f;          // linear
        float prioW[kMaxB]{};          // per band: dbToGain(-priority)
        float separation = 1.0f;       // 0..1
        float releaseS[kMaxB]{};       // per band (s), behaviour applied by the caller's adapt[]
        float adapt[kMaxB]{};
        bool hard = false;
    };
    void prepare(double fs, std::uint32_t look) {
        fs_ = fs;
        look_ = look;
        hold_.prepare(look + 1);
        for (int b = 0; b < kMaxB; ++b) {
            box_[b].prepare(look + 1, 0.0f);
            dl_[b].prepare(look);
            dr_[b].prepare(look);
        }
        eK_ = 1.0f - coefMs(fs, 30.0);
        mem_.set(fs, 1.0, 2.0);
        clear();
    }
    void clear() noexcept {
        hold_.clear();
        for (int b = 0; b < kMaxB; ++b) {
            box_[b].clear();
            dl_[b].clear();
            dr_[b].clear();
            g_[b] = 0.0f;
            e_[b] = 0.0f;
            shown_[b] = 0.0f;
        }
        mem_.v = 0.0f;
    }
    // band samples in (already driven); the limited band samples (look-ahead late) back out.
    // Returns the reduction the whole signal wanted (dB, <= 0).
    float tick(const Set& s, float* bl, float* br) noexcept {
        const int nb = s.bands;
        float sl = 0, sr = 0;
        for (int b = 0; b < nb; ++b) { sl += bl[b]; sr += br[b]; }
        const float peak = std::max(std::fabs(sl), std::fabs(sr));
        const float held = hold_.tick(peak > s.ceiling ? s.ceiling / peak : 1.0f);
        const float rDb = 20.0f * std::log10(std::max(held, 1e-6f));
        mem_.tick(rDb < -1.0f);
        float sumE = 0, sumEW = 0, w[kMaxB];
        for (int b = 0; b < nb; ++b) {
            const float e = std::max(bl[b] * bl[b], br[b] * br[b]);
            e_[b] += (e - e_[b]) * eK_;
            sumE += e_[b];
        }
        const float meanA = nb > 0 ? std::sqrt(std::max(sumE / static_cast<float>(nb), 1e-20f)) : 1.0f;
        for (int b = 0; b < nb; ++b) {
            const float own = std::sqrt(std::max(e_[b], 1e-20f)) / meanA;
            w[b] = s.prioW[b] * ((1.0f - s.separation) + s.separation * own);
            sumEW += e_[b] * w[b];
        }
        const float norm = sumEW > 1e-20f ? sumE / sumEW : 1.0f;
        for (int b = 0; b < nb; ++b) {
            const float want = std::max(rDb * w[b] * norm, -40.0f);
            const float rel = releaseS(s.releaseS[b], s.adapt[b], mem_.v, s.hard);
            g_[b] = want < g_[b] ? want : g_[b] + (want - g_[b]) * stepK(fs_, rel);
            const float gDb = box_[b].tick(g_[b]);
            shown_[b] = std::min(shown_[b], gDb);
            bl[b] = dl_[b].process(bl[b], look_) * gainOfDb(gDb);
            br[b] = dr_[b].process(br[b], look_) * gainOfDb(gDb);
        }
        return rDb;
    }
    // the most each band came down since the last call (dB), then start over
    [[nodiscard]] float takeBandDb(int b) noexcept {
        const float v = shown_[b];
        shown_[b] = 0.0f;
        return v;
    }

private:
    double fs_ = 48000.0;
    std::uint32_t look_ = 48;
    HoldMin hold_;
    Box box_[kMaxB];
    hc::fx::DelayLine dl_[kMaxB], dr_[kMaxB];
    float g_[kMaxB]{}, e_[kMaxB]{}, shown_[kMaxB]{};
    float eK_ = 0.0f;
    Memory mem_;
};

// ---- a short wideband look-ahead limiter: whatever still goes past the ceiling comes down here
class Catch {
public:
    static constexpr std::uint32_t kLook = 32;
    void prepare(double fs) {
        hold_.prepare(kLook + 1);
        box_.prepare(kLook + 1);
        d_.prepare(kLook);
        relK_ = stepK(fs, 0.015);
        clear();
    }
    void clear() noexcept { hold_.clear(); box_.clear(); d_.clear(); g_ = 1.0f; }
    // returns the gain it applied (for meters)
    float tick(float& l, float& r, float ceiling) noexcept {
        const float peak = std::max(std::fabs(l), std::fabs(r));
        const float held = hold_.tick(peak > ceiling ? ceiling / peak : 1.0f);
        g_ = held < g_ ? held : g_ + (held - g_) * relK_;
        const float g = box_.tick(g_);
        l = d_.l.process(l, kLook) * g;
        r = d_.r.process(r, kLook) * g;
        return g;
    }

private:
    HoldMin hold_;
    Box box_;
    Delay2 d_;
    float g_ = 1.0f, relK_ = 0.0f;
};

// ---- one-band look-ahead limiter (BIG SHOT, LAST WORD). Peaks are found 4 samples late (so the peaks
//  between the samples can be found too); the audio waits the look-ahead on top of that.
//  domain 0 DIGITAL: sample peaks; 1 ANALOG: the peaks between the samples, rounded softly into the
//  ceiling at the end; 2 TRUE PEAK: the peaks between the samples.
class OneBand {
public:
    static constexpr std::uint32_t kLook = 72, kLatency = kLook + TruePeak4::kDelay;
    struct Set {
        float ceiling = 1.0f;
        float relS = 0.1f;              // the release (s) when not adaptive
        bool adaptive = false;          // release from loS (single peaks) to hiS (working hard)
        float loS = 0.02f, hiS = 0.4f;
        int domain = 0;
    };
    void prepare(double fs) {
        fs_ = fs;
        hold_.prepare(kLook + 1);
        box_.prepare(kLook + 1);
        d_.prepare(kLatency);
        mem_.set(fs, 1.0, 2.0);
        clear();
    }
    void clear() noexcept {
        hold_.clear();
        box_.clear();
        d_.clear();
        tpL_.clear();
        tpR_.clear();
        g_ = 1.0f;
        mem_.v = 0.0f;
    }
    // l, r: the (driven) input; replaced by the limited output (kLatency late). Returns the gain used.
    float tick(const Set& s, float& l, float& r) noexcept {
        const float pl = tpL_.tick(l), pr = tpR_.tick(r);
        const float peak = s.domain == 0 ? std::max(std::fabs(tpL_.late()), std::fabs(tpR_.late())) : std::max(pl, pr);
        const float held = hold_.tick(peak > s.ceiling ? s.ceiling / peak : 1.0f);
        mem_.tick(held < 0.89f);
        const float rel = s.adaptive ? s.loS + (s.hiS - s.loS) * mem_.v : s.relS;
        g_ = held < g_ ? held : g_ + (held - g_) * stepK(fs_, rel);
        const float g = box_.tick(g_);
        l = d_.l.process(l, kLatency) * g;
        r = d_.r.process(r, kLatency) * g;
        if (s.domain == 1) {
            l = roundInto(l, s.ceiling);
            r = roundInto(r, s.ceiling);
        }
        l = std::clamp(l, -s.ceiling, s.ceiling);
        r = std::clamp(r, -s.ceiling, s.ceiling);
        return g;
    }

private:
    static float roundInto(float y, float c) noexcept {
        const float k = 0.89f * c, a = std::fabs(y);
        if (a <= k) return y;
        const float t = (a - k) / (c - k);
        return std::copysign(k + (c - k) * t / (1.0f + t), y);
    }
    double fs_ = 48000.0;
    HoldMin hold_;
    Box box_;
    Delay2 d_;
    TruePeak4 tpL_, tpR_;
    float g_ = 1.0f;
    Memory mem_;
};

// ---- peak meter with a hold (dBFS), for the plates
struct PeakHold {
    float hold = -100.0f;
    double age = 0;
    void clear() noexcept { hold = -100.0f; age = 0; }
    float push(float peakDb, std::uint32_t n, double fs) noexcept {
        age += static_cast<double>(n) / fs;
        if (peakDb >= hold) { hold = peakDb; age = 0; }
        else if (age > 1.5) hold = std::max(peakDb, hold - 20.0f * static_cast<float>(n / fs));
        return std::max(hold, -100.0f);
    }
};

// the response (dB) of a biquad at hz
inline double biquadDb(const C& c, double fs, double hz) noexcept {
    const double w = 2.0 * std::numbers::pi * hz / fs;
    const double c1 = std::cos(w), s1 = -std::sin(w), c2 = std::cos(2 * w), s2 = -std::sin(2 * w);
    const double nr = c.b0 + c.b1 * c1 + c.b2 * c2, ni = c.b1 * s1 + c.b2 * s2;
    const double dr = 1 + c.a1 * c1 + c.a2 * c2, di = c.a1 * s1 + c.a2 * s2;
    return 10.0 * std::log10(std::max((nr * nr + ni * ni) / std::max(dr * dr + di * di, 1e-300), 1e-30));
}

} // namespace hem::rack::maxim
