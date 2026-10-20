#pragma once
// The multiband dynamics engine of TERRITORY (4 bands, minimum phase) and STRAIGHT EDGE (5 bands, linear
// phase). rackfx-private; nothing allocates after prepare().
//
// STRAIGHT EDGE splits the sound into its bands (linear phase), moves each and adds them back up.
// TERRITORY listens to its bands through a Linkwitz-Riley split, but moves the sound itself with a chain
// of low shelves at the crossovers (each shelf = the step from one band's gain to the next, the top
// band's gain over all): no phase smear between bands, and Q sets how steep each step is (0.75 sharp,
// 0.1 a long gentle slope). SOLO listens to the split bands themselves.
//
// Each band: THRESHOLD, GAIN, RANGE (the most it moves: minus = down, plus = up; the threshold sits in
// the middle of the move — it starts half a RANGE under the threshold and is complete half a RANGE
// over it), ATTACK, RELEASE, SOLO, BYPASS. KNEE 0 (soft: a longer, rounder move) .. 100 (hard: a
// straight line). Release AUTO (short after short peaks, longer when it works hard) or MANUAL;
// BEHAVIOR EASE OUT (lets go slower as it nears no change) or SNAP BACK (quicker near no change).
// AUTO MAKEUP gives each band back half its RANGE. ADAPTIVE (-40 = off .. +12 dB): a band only works
// when it stands out over the band under it (its threshold rises to that band's level + ADAPTIVE).
#include "Common.hpp"

#include "../maxim/LimitKit.hpp"

namespace hem::rack::arsenal {

template <int kB>
struct DynSet {
    float thr[kB]{}, gain[kB]{}, range[kB]{}, attMs[kB]{}, relMs[kB]{};
    bool solo[kB]{}, bypass[kB]{};
    float xo[kB - 1]{};
    float q = 0.6f;              // the minimum-phase split's crossover Q
    bool autoRel = true, opto = false, autoMakeup = false, dither = false;
    float knee = 50.0f;          // 0 soft .. 100 hard
    float adaptive = -40.0f;     // <= -40: off
    float outDb = 0.0f;
};

// The band's move (dB) for a level.
inline float rangeMoveDb(float lv, float thr, float range, float knee) noexcept {
    const float R = std::fabs(range);
    if (R < 1e-3f) return 0.0f;
    const float soft = 1.0f - std::clamp(knee, 0.0f, 100.0f) * 0.01f;
    const float span = R * (1.0f + 2.0f * soft);   // a softer knee spreads the move wider
    const float t = std::clamp((lv - thr) / span + 0.5f, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return range * (t + soft * (smooth - t));
}

template <int kB, bool kLinear>
class MultiDyn {
public:
    static constexpr std::uint32_t kLatency = kLinear ? maxim::LinearSplit::kLatency : 0u;
    static constexpr int kRedesignSamples = 1536;

    void prepare(double fs, const DynSet<kB>& s) {
        fs_ = fs;
        if constexpr (kLinear) split_.prepare(fs, kB - 1);
        mem_.set(fs, 1.0, 3.0);
        idr_.prepare(fs);
        envRel_ = coefMs(fs, 4.0);
        reset(s);
    }
    void reset(const DynSet<kB>& s) noexcept {
        for (int k = 0; k < kB - 1; ++k) xo_[k] = s.xo[k];
        q_ = s.q;
        if constexpr (kLinear) {
            split_.clear();
            split_.designNow(xo_, maxim::LinearSplit::kTaps);
        } else {
            designIir();
            for (auto& x : xo2_) x.clear();
            for (auto& c : shelf_) for (auto& f : c) f.clear();
            for (int k = 0; k < kB - 1; ++k) stepDb_[k] = 1e9f;
            tick_ = 0;
        }
        since_ = kRedesignSamples;
        for (int b = 0; b < kB; ++b) { env_[b] = 0.0f; g_[b] = 0.0f; shown_[b] = 0.0f; }
        mem_.v = 0.0f;
        idr_.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(s.outDb));
        for (int b = 0; b < kB; ++b) band_[b].prepare(fs_, 30, 1.0f);
        peak_.clear();
        outDb_ = -100.0f;
    }

    void process(const DynSet<kB>& s, float* L, float* R, std::uint32_t n) noexcept {
        crossovers(s, n);
        idr_.set(s.dither ? 24 : 0, 0, 3, 1.0f);
        bool anySolo = false;
        float att[kB], rel[kB];
        for (int b = 0; b < kB; ++b) {
            anySolo = anySolo || s.solo[b];
            att[b] = coefMs(fs_, s.attMs[b]);
            rel[b] = s.relMs[b];
        }
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float bl[kB], br[kB];
            split(L[i], R[i], bl, br);
            float lvDb[kB];
            for (int b = 0; b < kB; ++b) {
                const float x = std::max(std::fabs(bl[b]), std::fabs(br[b]));
                env_[b] = x > env_[b] ? x : x + envRel_ * (env_[b] - x);
                lvDb[b] = dbOf(env_[b]);
            }
            bool working = false;
            float yl = 0.0f, yr = 0.0f, G[kB];
            for (int b = 0; b < kB; ++b) {
                float thr = s.thr[b];
                if (b > 0 && s.adaptive > -39.9f) thr = std::max(thr, lvDb[b - 1] + s.adaptive);
                const float target = s.bypass[b] ? 0.0f : rangeMoveDb(lvDb[b], thr, s.range[b], s.knee);
                // the release: AUTO stretches it after long hard work; EASE OUT slows near no change, SNAP BACK speeds up there
                const float depth = std::clamp(std::fabs(g_[b]) / 10.0f, 0.0f, 1.0f);
                double ms = rel[b] * (s.autoRel ? 0.35 + 1.3 * mem_.v : 1.0);
                ms *= s.opto ? 1.0 + 3.0 * (1.0 - depth) : 0.25 + 0.75 * depth;
                const bool away = std::fabs(target) > std::fabs(g_[b]);
                g_[b] = target + (away ? att[b] : coefMs(fs_, ms)) * (g_[b] - target);
                working = working || g_[b] < -3.0f;
                shown_[b] = std::fabs(g_[b]) > std::fabs(shown_[b]) ? g_[b] : shown_[b];
                const float makeup = s.autoMakeup ? -0.5f * s.range[b] : 0.0f;
                const float gb = band_[b].next(s.bypass[b] ? 1.0f : hc::fx::dbToGain(s.gain[b] + makeup)) * hc::fx::dbToGain(g_[b]);
                G[b] = gb;
                if (kLinear || anySolo) {
                    if (!anySolo || s.solo[b]) { yl += bl[b] * gb; yr += br[b] * gb; }
                }
            }
            if constexpr (!kLinear) {
                if (!anySolo) {   // the shelf chain, its steps refreshed every 16 samples
                    if ((tick_++ & 15u) == 0) restep(G);
                    float a = L[i], c = R[i];
                    for (int k = 0; k < kB - 1; ++k) { a = shelf_[0][k].tick(a); c = shelf_[1][k].tick(c); }
                    yl = a * G[kB - 1];
                    yr = c * G[kB - 1];
                } else {
                    ++tick_;
                }
            }
            mem_.tick(working);
            const float o = out_.next(hc::fx::dbToGain(s.outDb));
            L[i] = idr_.tick(0, yl * o);
            R[i] = idr_.tick(1, yr * o);
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        for (int b = 0; b < kB; ++b) {
            bandDb_[b] = shown_[b];
            shown_[b] = 0.0f;
            if (std::fabs(g_[b]) < 1e-6f) g_[b] = 0.0f;
        }
        lastPeakDb_ = dbOf(pk);
        outDb_ = peak_.push(lastPeakDb_, n, fs_);
    }
    [[nodiscard]] float bandDb(int b) const noexcept { return bandDb_[b]; }
    [[nodiscard]] float outPeakDb() const noexcept { return outDb_; }       // held
    [[nodiscard]] float blockPeakDb() const noexcept { return lastPeakDb_; }
    void clearPeak() noexcept { peak_.clear(); outDb_ = -100.0f; }

private:
    void split(float l, float r, float* bl, float* br) noexcept {
        if constexpr (kLinear) split_.tick(l, r, bl, br);
        else {
            xo2_[0].split(l, bl);
            xo2_[1].split(r, br);
        }
    }
    void designIir() noexcept {
        float lo = 10.0f;
        for (int k = 0; k < kB - 1; ++k) { hz_[k] = std::max(xo_[k], lo * 1.05f); lo = hz_[k]; }
        for (auto& x : xo2_) x.design(fs_, hz_, kB);
        for (int k = 0; k < kB - 1; ++k) stepDb_[k] = 1e9f;   // the shelves get redone at the next refresh
    }
    // the shelves for these band gains (linear): shelf k steps from band k's gain to band k+1's
    void restep(const float* G) noexcept {
        for (int k = 0; k < kB - 1; ++k) {
            const float step = dbOf(G[k]) - dbOf(G[k + 1]);
            if (std::fabs(step - stepDb_[k]) < 0.01f) continue;
            stepDb_[k] = step;
            const C c = C::make(T::LowShelf, fs_, hz_[k], std::clamp(q_, 0.1f, 2.0f), step);
            shelf_[0][k].c = shelf_[1][k].c = c;
        }
    }
    void crossovers(const DynSet<kB>& s, std::uint32_t n) noexcept {
        since_ += static_cast<int>(n);
        bool same = s.q == q_;
        for (int k = 0; k < kB - 1; ++k) same = same && s.xo[k] == xo_[k];
        if (same) return;
        if constexpr (kLinear) {
            if (since_ < kRedesignSamples || split_.fading()) return;
            for (int k = 0; k < kB - 1; ++k) xo_[k] = s.xo[k];
            split_.fadeTo(xo_, maxim::LinearSplit::kTaps);
        } else {
            for (int k = 0; k < kB - 1; ++k) xo_[k] = s.xo[k];
            q_ = s.q;
            designIir();
        }
        since_ = 0;
    }

    double fs_ = 48000.0;
    maxim::LinearSplit split_;
    Crossover<kB> xo2_[2];
    hc::fx::Biquad shelf_[2][kB];
    float xo_[kB]{}, hz_[kB]{}, stepDb_[kB]{}, q_ = 0.6f;
    std::uint32_t tick_ = 0;
    int since_ = 0;
    float env_[kB]{}, g_[kB]{}, shown_[kB]{}, bandDb_[kB]{}, envRel_ = 0.0f;
    Memory mem_;
    maxim::Idr idr_;
    maxim::PeakHold peak_;
    float outDb_ = -100.0f, lastPeakDb_ = -100.0f;
    hc::fx::Smoothed out_, band_[kB];
};

} // namespace hem::rack::arsenal
