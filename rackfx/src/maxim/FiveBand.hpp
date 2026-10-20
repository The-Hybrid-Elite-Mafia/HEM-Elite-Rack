#pragma once
// The five-band maximizer engine: HIGH STAKES / FAST MONEY (every setting on the plate) and
// TOP DOG / QUICK CASH (the settings come from a profile). kLinear: linear-phase band split
// (LinearSplit), or the low-latency one (Linkwitz-Riley tree, the bands add up to an all-pass).
#include "LimitKit.hpp"

namespace hem::rack::maxim {

struct FiveSettings {
    float thresholdDb = 0, ceilingDb = 0;
    bool link = false;              // the ceiling comes down with the threshold
    int behave = Auto;
    float releaseMul = 1.0f;
    float xo[4] = {80, 320, 1278, 5113};
    float separation = 1.0f;        // 0..1
    float gainDb[5]{}, prioDb[5]{};
    bool bandLink[5]{};             // GAIN also raises PRIORITY
    float releaseMs[5] = {598, 503, 402, 298, 200};
    bool solo[5]{};
    int bits = 24, dither = 0, shape = 1;
};

template <bool kLinear>
class FiveBand {
public:
    static constexpr int kB = 5;
    static constexpr std::uint32_t kLook = 72;
    static constexpr std::uint32_t kLatency = (kLinear ? LinearSplit::kLatency : 0u) + kLook + Catch::kLook;
    static constexpr int kRedesignSamples = 1536;   // at most one new split every 32 ms while a crossover moves

    void prepare(double fs, const FiveSettings& s) {
        fs_ = fs;
        if constexpr (kLinear) split_.prepare(fs, 4);
        shared_.prepare(fs, kLook);
        catch_.prepare(fs);
        idr_.prepare(fs);
        reset(s);
    }
    void reset(const FiveSettings& s) noexcept {
        for (int k = 0; k < 4; ++k) xo_[k] = s.xo[k];
        if constexpr (kLinear) {
            split_.clear();
            split_.designNow(xo_, LinearSplit::kTaps);
        } else {
            float hz[4];
            rising(xo_, hz);
            for (auto& x : iir_) { x.design(fs_, hz, kB); x.clear(); }
        }
        since_ = kRedesignSamples;
        shared_.clear();
        catch_.clear();
        idr_.clear();
        drive_.prepare(fs_, 30, gainOfDb(-s.thresholdDb));
        ceil_.prepare(fs_, 30, gainOfDb(ceilingDb(s)));
        for (int b = 0; b < kB; ++b) band_[b].prepare(fs_, 30, gainOfDb(s.gainDb[b]));
        for (float& v : bandDb_) v = 0.0f;
    }

    // returns the most the whole signal wanted to come down in this block (dB)
    float process(const FiveSettings& s, float* L, float* R, std::uint32_t n) noexcept {
        crossovers(s, n);
        const float cT = gainOfDb(ceilingDb(s));
        idr_.set(s.bits, s.dither, s.shape, cT);
        typename Shared<kB>::Set set;
        set.bands = kB;
        set.separation = std::clamp(s.separation, 0.0f, 1.0f);
        set.hard = s.behave == Hard;
        bool anySolo = false;
        for (int b = 0; b < kB; ++b) {
            set.prioW[b] = gainOfDb(-(s.prioDb[b] + (s.bandLink[b] ? s.gainDb[b] : 0.0f)));
            set.releaseS[b] = std::max(s.releaseMs[b] * s.releaseMul, 0.01f) * 0.001f;
            set.adapt[b] = adaptivity(s.behave, b, kB);
            anySolo = anySolo || s.solo[b];
        }
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float bl[kB], br[kB];
            if constexpr (kLinear) split_.tick(L[i], R[i], bl, br);
            else {
                iir_[0].split(L[i], bl);
                iir_[1].split(R[i], br);
            }
            const float d = drive_.next(gainOfDb(-s.thresholdDb));
            set.ceiling = ceil_.next(cT);
            for (int b = 0; b < kB; ++b) {
                const float g = band_[b].next(gainOfDb(s.gainDb[b])) * d;
                bl[b] *= g;
                br[b] *= g;
            }
            most = std::min(most, shared_.tick(set, bl, br));
            float yl = 0, yr = 0;
            for (int b = 0; b < kB; ++b)
                if (!anySolo || s.solo[b]) { yl += bl[b]; yr += br[b]; }
            catch_.tick(yl, yr, set.ceiling);
            yl = std::clamp(yl, -set.ceiling, set.ceiling);
            yr = std::clamp(yr, -set.ceiling, set.ceiling);
            L[i] = idr_.tick(0, yl);
            R[i] = idr_.tick(1, yr);
        }
        for (int b = 0; b < kB; ++b) bandDb_[b] = shared_.takeBandDb(b);
        return most;
    }
    [[nodiscard]] float bandDb(int b) const noexcept { return bandDb_[b]; }

private:
    // crossovers kept rising, at least 5 % apart (LinearSplit does the same)
    static void rising(const float* in, float* out) noexcept {
        float lo = 10.0f;
        for (int k = 0; k < 4; ++k) { out[k] = std::max(in[k], lo * 1.05f); lo = out[k]; }
    }
    static float ceilingDb(const FiveSettings& s) noexcept { return s.ceilingDb + (s.link ? s.thresholdDb : 0.0f); }
    void crossovers(const FiveSettings& s, std::uint32_t n) noexcept {
        since_ += static_cast<int>(n);
        bool same = true;
        for (int k = 0; k < 4; ++k) same = same && s.xo[k] == xo_[k];
        if (same) return;
        if constexpr (kLinear) {
            if (since_ < kRedesignSamples || split_.fading()) return;
            for (int k = 0; k < 4; ++k) xo_[k] = s.xo[k];
            split_.fadeTo(xo_, LinearSplit::kTaps);
        } else {
            for (int k = 0; k < 4; ++k) xo_[k] = s.xo[k];
            float hz[4];
            rising(xo_, hz);
            for (auto& x : iir_) x.design(fs_, hz, kB);
        }
        since_ = 0;
    }

    double fs_ = 48000.0;
    LinearSplit split_;
    Crossover<kB> iir_[2];
    Shared<kB> shared_;
    Catch catch_;
    Idr idr_;
    float xo_[4]{};
    int since_ = 0;
    hc::fx::Smoothed drive_, ceil_, band_[kB];
    float bandDb_[kB]{};
};

} // namespace hem::rack::maxim
