// SUB, BASS TIGHTENER, VOCAL STRIP (see Specialty.hpp).
#include "hem/rack/Specialty.hpp"

#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace hem::rack {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;
const char* const kOnOff[] = {"OFF", "ON"};
inline float dbOf(float x) noexcept { return 20.0f * std::log10(std::max(x, 1e-9f)); }
inline float coefFor(double fs, double ms) noexcept { return static_cast<float>(std::exp(-1.0 / (fs * ms * 0.001))); }
const C kId{1, 0, 0, 0, 0};

// ---- SUB ------------------------------------------------------------------------------------------
namespace sb {
using namespace sub;
const char* const kModeN[] = {"OCTAVE", "HARMONICS"};
const ParamInfo kParams[kCount] = {
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"freq", "Frequency", "Hz", 40.0f, 150.0f, 100.0f, Taper::Log},
    {"amount", "Amount", "%", 0.0f, 100.0f, 0.0f},
    {"tone", "Tone", "%", 0.0f, 100.0f, 40.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; before_.prepare(fs); after_.prepare(fs); reset(); }
    void reset() noexcept override {
        before_.reset();
        after_.reset();
        for (auto& b : det_) b.clear();
        for (auto& b : post_) b.clear();
        key_ = -1.0f;
        flip_ = 1.0f;
        prev_ = 0.0f;
        env_ = 0.0f;
        amt_.prepare(fs_, 30, p(Amount) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        design();
        const bool octave = p(Mode) < 0.5f;
        const float envK = coefFor(fs_, 30.0);
        for (std::uint32_t i = 0; i < n; ++i) {
            // the bass, mono, below FREQ
            const float b = static_cast<float>(det_[1].tick(det_[0].tick(0.5f * (L[i] + R[i]))));
            const float a = std::fabs(b);
            env_ = a + envK * (env_ - a);
            float gen;
            if (octave) {   // flip at every upward zero crossing: a square at half the frequency, shaped by the bass's level
                if (prev_ < 0.0f && b >= 0.0f) flip_ = -flip_;
                gen = flip_ * env_ * 1.4f;
            } else {        // overtones: Chebyshev polynomials of the bass scaled to its level give its 2nd and 3rd harmonics
                const float peakEst = env_ * 1.5708f;   // the average of |sine| is 0.637 of its peak
                const float x = std::clamp(b / std::max(peakEst, 1e-6f), -1.2f, 1.2f);
                gen = peakEst * (0.7f * (2.0f * x * x - 1.0f) + 0.5f * (4.0f * x * x * x - 3.0f * x));
            }
            prev_ = b;
            // smooth the new part: OCTAVE into a round sub, HARMONICS keeps only the overtones
            const float sm = static_cast<float>(post_[1].tick(post_[0].tick(gen)));
            const float am = amt_.next(p(Amount) * 0.01f) * 2.0f;   // up to +6 dB of the bass's level
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + am * sm) * o;
            R[i] = (R[i] + am * sm) * o;
        }
        after_.push(L, R, n);
    }
private:
    void design() noexcept {
        const float key = p(Freq) * 1000.0f + p(Tone) + (p(Mode) < 0.5f ? 0.0f : 0.5f);
        if (key == key_) return;
        key_ = key;
        const double f = p(Freq), tone = p(Tone) * 0.01;
        det_[0].c = det_[1].c = C::make(T::LowPass, fs_, f, 0.7071);
        if (p(Mode) < 0.5f) {   // OCTAVE: a sub under FREQ / 2, TONE darker = rounder
            post_[0].c = C::make(T::LowPass, fs_, f * (0.7 - 0.4 * tone), 0.7071);
            post_[1].c = C::make(T::HighPass, fs_, 20.0, 0.7071);
        } else {                // HARMONICS: from FREQ up, TONE darker = less top
            post_[0].c = C::make(T::HighPass, fs_, f * 1.2, 0.7071);
            post_[1].c = C::make(T::LowPass, fs_, f * (8.0 - 5.0 * tone), 0.7071);
        }
    }
    double fs_ = 48000;
    SpectrumMeter before_, after_;
    hc::fx::Biquad det_[2], post_[2];
    float key_ = -1, flip_ = 1, prev_ = 0, env_ = 0;
    hc::fx::Smoothed amt_, out_;
};
} // namespace sb

// ---- BASS TIGHTENER -------------------------------------------------------------------------------
namespace bt {
using namespace tightener;
const ParamInfo kParams[kCount] = {
    {"freq", "Frequency", "Hz", 60.0f, 250.0f, 120.0f, Taper::Log},
    {"tighten", "Tighten", "%", 0.0f, 100.0f, 0.0f},
    {"punch", "Punch", "%", 0.0f, 100.0f, 0.0f},
    {"mono", "Mono Lows", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"lowgain", "Low Gain", "dB", -6.0f, 6.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& ch : x_) for (auto& b : ch) b.clear();
        freq_ = -1.0f;
        gr_ = 0.0f;
        fast_ = slow_ = 0.0f;
        monoMix_ = p(Mono) >= 0.5f ? 1.0f : 0.0f;
        low_.prepare(fs_, 30, hc::fx::dbToGain(p(LowGain)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Freq) != freq_) {
            freq_ = p(Freq);
            const C lp = C::make(T::LowPass, fs_, freq_, 0.7071), hp = C::make(T::HighPass, fs_, freq_, 0.7071);
            for (auto& ch : x_) { ch[0].c = ch[1].c = lp; ch[2].c = ch[3].c = hp; }
        }
        const float tighten = p(Tighten) * 0.01f, punch = p(Punch) * 0.01f;
        // TIGHTEN: a fast compressor on the lows, threshold falling as you turn it up
        const float thr = -6.0f - 24.0f * tighten, ratio = 1.0f + 5.0f * tighten;
        const float att = coefFor(fs_, 3.0), rel = coefFor(fs_, 80.0);
        const float fA = coefFor(fs_, 1.0), sA = coefFor(fs_, 20.0), fR = coefFor(fs_, 40.0);
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const bool mono = p(Mono) >= 0.5f;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float lo[2], hi[2];
            const float in[2] = {L[i], R[i]};
            for (int c = 0; c < 2; ++c) {
                auto& x = x_[static_cast<std::size_t>(c)];
                lo[c] = static_cast<float>(x[1].tick(x[0].tick(in[c])));
                hi[c] = static_cast<float>(x[3].tick(x[2].tick(in[c])));
            }
            // LR4 low + high sum to an all-pass: flat in level
            const float lv = std::max(std::fabs(lo[0]), std::fabs(lo[1]));
            const float target = tighten > 0.0f ? kneeGainDb(dbOf(lv), thr, ratio, 6.0f) : 0.0f;
            gr_ = target + (target < gr_ ? att : rel) * (gr_ - target);
            // PUNCH: a fast follower running ahead of a slow one at every low-end attack
            fast_ = lv + (lv > fast_ ? fA : fR) * (fast_ - lv);
            slow_ = lv + (lv > slow_ ? sA : fR) * (slow_ - lv);
            const float onset = std::clamp(dbOf(fast_) - dbOf(slow_), 0.0f, 12.0f);
            const float g = hc::fx::dbToGain(gr_ + punch * onset) * low_.next(hc::fx::dbToGain(p(LowGain)));
            most = std::min(most, gr_);
            monoMix_ = mono ? std::min(1.0f, monoMix_ + step) : std::max(0.0f, monoMix_ - step);
            const float m = 0.5f * (lo[0] + lo[1]);
            const float l0 = lo[0] + monoMix_ * (m - lo[0]), l1 = lo[1] + monoMix_ * (m - lo[1]);
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (l0 * g + hi[0]) * o;
            R[i] = (l1 * g + hi[1]) * o;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }
private:
    double fs_ = 48000;
    std::array<std::array<hc::fx::Biquad, 4>, 2> x_{};
    float freq_ = -1, gr_ = 0, fast_ = 0, slow_ = 0, monoMix_ = 0;
    hc::fx::Smoothed low_, out_;
};
} // namespace bt

// ---- VOCAL STRIP ----------------------------------------------------------------------------------
namespace vs {
using namespace vocal;
const char* const kHpfN[] = {"OFF", "60 Hz", "80 Hz", "100 Hz", "120 Hz"};
const ParamInfo kParams[kCount] = {
    {"hpf", "High-Pass", "", 0, 4, 0, Taper::Choice, kHpfN},
    {"body", "Body (200 Hz)", "dB", -6.0f, 6.0f, 0.0f},
    {"compress", "Compress", "%", 0.0f, 100.0f, 0.0f},
    {"deess", "De-Ess", "%", 0.0f, 100.0f, 0.0f},
    {"presence", "Presence (4 kHz)", "dB", -6.0f, 6.0f, 0.0f},
    {"air", "Air (12 kHz)", "dB", 0.0f, 8.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        for (auto& b : ess_) b.clear();
        key_ = -1e9f;
        gr_ = esGr_ = env_ = esEnv_ = 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float comp = p(Compress) * 0.01f, de = p(DeEss) * 0.01f;
        // COMPRESS: one knob from nothing to a firm 6:1 at -30 dB, with makeup for half of it
        const float thr = -6.0f - 24.0f * comp, ratio = 1.0f + 5.0f * comp, makeup = 0.5f * 24.0f * comp * (1.0f - 1.0f / ratio);
        const float att = coefFor(fs_, 5.0), rel = coefFor(fs_, 120.0), envRel = coefFor(fs_, 10.0);
        const float esAtt = coefFor(fs_, 0.5), esRel = coefFor(fs_, 60.0);
        float most = 0.0f;
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            // 1. high-pass and body (EQ before the compressor)
            for (int c = 0; c < 2; ++c) {
                auto& e = eq_[static_cast<std::size_t>(c)];
                io[c][i] = static_cast<float>(e[1].tick(e[0].tick(io[c][i])));
            }
            // 2. compressor
            if (comp > 0.0f) {
                const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
                env_ = x > env_ ? x : x + envRel * (env_ - x);
                const float t = kneeGainDb(dbOf(env_), thr, ratio, 6.0f);
                gr_ = t + (t < gr_ ? att : rel) * (gr_ - t);
                most = std::min(most, gr_);
                const float g = hc::fx::dbToGain(gr_ + makeup);
                L[i] *= g;
                R[i] *= g;
            }
            // 3. de-esser: the 5 kHz+ band (input minus its low-passed part) goes down when it's loud
            if (de > 0.0f) {
                const float hl = L[i] - static_cast<float>(ess_[0].tick(L[i])), hr = R[i] - static_cast<float>(ess_[1].tick(R[i]));
                const float x = std::max(std::fabs(hl), std::fabs(hr));
                esEnv_ = x > esEnv_ ? x : x + esRel * (esEnv_ - x);
                const float t = -std::min(12.0f * de, std::max(0.0f, (dbOf(esEnv_) - (-20.0f - 20.0f * de)) * 0.75f));
                esGr_ = t + (t < esGr_ ? esAtt : esRel) * (esGr_ - t);
                const float g = hc::fx::dbToGain(esGr_) - 1.0f;
                L[i] += hl * g;
                R[i] += hr * g;
            }
            // 4. presence and air (after the compressor)
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            for (int c = 0; c < 2; ++c) {
                auto& e = eq_[static_cast<std::size_t>(c)];
                io[c][i] = static_cast<float>(e[3].tick(e[2].tick(io[c][i]))) * o;
            }
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(std::min(most, esGr_));
    }
private:
    void design() noexcept {
        const float key = p(Hpf) * 1e6f + p(Body) * 1e4f + p(Presence) * 100.0f + p(Air);
        if (key == key_) return;
        key_ = key;
        const int h = std::clamp(static_cast<int>(std::lround(p(Hpf))), 0, 4);
        const C hp = h > 0 ? C::make(T::HighPass, fs_, kHpfHz[h], 0.7071) : kId;
        const C body = C::make(T::Bell, fs_, 200, 0.8, p(Body));
        const C pres = C::make(T::Bell, fs_, 4000, 0.9, p(Presence));
        const C air = C::make(T::HighShelf, fs_, std::min(12000.0, fs_ * 0.4), 0.6, p(Air));
        for (auto& e : eq_) { e[0].c = hp; e[1].c = body; e[2].c = pres; e[3].c = air; }
        ess_[0].c = ess_[1].c = C::make(T::LowPass, fs_, 5000, 0.7071);
    }
    double fs_ = 48000;
    std::array<std::array<hc::fx::Biquad, 4>, 2> eq_{};
    hc::fx::Biquad ess_[2];
    float key_ = -1e9f, gr_ = 0, esGr_ = 0, env_ = 0, esEnv_ = 0;
    hc::fx::Smoothed out_;
};
} // namespace vs

} // namespace

std::unique_ptr<hc::fx::Effect> makeSub() { return std::make_unique<sb::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeBassTightener() { return std::make_unique<bt::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeVocalStrip() { return std::make_unique<vs::Impl>(); }

} // namespace hem::rack
