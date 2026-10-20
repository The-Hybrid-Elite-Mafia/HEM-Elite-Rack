#pragma once
// Pitch detection shared by the tuner and the pitch corrector (private to rackfx).
//
// YIN (de Cheveigné & Kawahara): the cumulative-mean-normalized difference function, the first dip
// under a threshold, refined with a parabola. The input is low-passed and taken at half the rate
// first, which halves the work and is plenty for voices and instruments.
#include "../arsenal/Common.hpp"

#include <vector>

namespace hem::rack::streets {
using namespace arsenal;

class Yin {
public:
    // minHz: lowest pitch it can find; window / hop in samples at the half rate
    void prepare(double fs, double minHz, std::uint32_t window, std::uint32_t hop) {
        fsHalf_ = fs * 0.5;
        maxLag_ = static_cast<std::uint32_t>(std::ceil(fsHalf_ / minHz)) + 2;
        window_ = window;
        hop_ = hop;
        n_ = window_ + maxLag_ + 2;
        buf_.assign(2 * static_cast<std::size_t>(n_), 0.0f);   // every sample twice: the last n_ are always in one piece
        d_.assign(maxLag_ + 1, 0.0f);
        lp_[0].c = lp_[1].c = C::make(T::LowPass, fs, std::min(fsHalf_ * 0.4, 5000.0), 0.7071);
        clear();
    }
    void clear() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        lp_[0].clear();
        lp_[1].clear();
        odd_ = false;
        w_ = 0;
        sinceHop_ = 0;
        hz_ = 0.0f;
        conf_ = 0.0f;
        level_ = 0.0f;
    }
    // one input sample; true when a new estimate is ready
    bool push(float x) noexcept {
        const float y = lp_[1].tick(lp_[0].tick(x));
        odd_ = !odd_;
        if (odd_) return false;
        buf_[w_] = buf_[w_ + n_] = y;
        w_ = (w_ + 1) % n_;
        if (++sinceHop_ < hop_) return false;
        sinceHop_ = 0;
        analyse();
        return true;
    }
    [[nodiscard]] float hz() const noexcept { return hz_; }           // 0 = no pitch
    [[nodiscard]] float confidence() const noexcept { return conf_; } // 0..1
    [[nodiscard]] float levelDb() const noexcept { return level_; }   // of the analysed window

private:
    void analyse() noexcept {
        const float* x = buf_.data() + w_ + (n_ - window_ - maxLag_);   // oldest first
        double e = 0.0;
        for (std::uint32_t j = 0; j < window_; ++j) e += static_cast<double>(x[j]) * x[j];
        level_ = powDb(2.0 * e / window_);
        if (level_ < -60.0f) { hz_ = 0.0f; conf_ = 0.0f; return; }
        d_[0] = 1.0f;
        double run = 0.0;
        for (std::uint32_t tau = 1; tau <= maxLag_ - 2; ++tau) {
            double s = 0.0;
            for (std::uint32_t j = 0; j < window_; ++j) {
                const double df = static_cast<double>(x[j]) - x[j + tau];
                s += df * df;
            }
            run += s;
            d_[tau] = static_cast<float>(run > 0.0 ? s * tau / run : 1.0);
        }
        // the first dip under the threshold, then down to its bottom
        constexpr float kThreshold = 0.15f;
        std::uint32_t best = 0;
        for (std::uint32_t tau = 2; tau < maxLag_ - 2; ++tau) {
            if (d_[tau] < kThreshold) {
                while (tau + 1 < maxLag_ - 2 && d_[tau + 1] < d_[tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best == 0) { hz_ = 0.0f; conf_ = 0.0f; return; }
        const float a = d_[best - 1], b = d_[best], c = d_[best + 1];
        const float den = a - 2.0f * b + c;
        const float shift = std::fabs(den) > 1e-9f ? std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        hz_ = static_cast<float>(fsHalf_ / (static_cast<double>(best) + shift));
        conf_ = std::clamp(1.0f - b, 0.0f, 1.0f);
    }

    double fsHalf_ = 24000.0;
    std::uint32_t maxLag_ = 600, window_ = 800, hop_ = 256, sinceHop_ = 0, n_ = 1402, w_ = 0;
    std::vector<float> buf_, d_;
    hc::fx::Biquad lp_[2];
    bool odd_ = false;
    float hz_ = 0.0f, conf_ = 0.0f, level_ = -120.0f;
};

// ---- PSOLA: pitch shifting that keeps a voice's formants (or moves them on purpose) ---------------------
//
// PsolaInput keeps the last few hundred milliseconds of the input and a pitch track (YIN, every 128 samples).
// Each PsolaVoice builds its output, `latency` samples late, from two-period grains cut around analysis marks
// one period apart (a period = the detected pitch's, or 5 ms where nothing is pitched), and laid down at its own
// spacing: closer = higher. Each grain can be read faster or slower (FORMANT) to move the formants. At a ratio
// of 1 and a formant of 1 the output is the input, exactly, `latency` late.
class PsolaInput {
public:
    static constexpr std::uint32_t kSize = 16384, kStep = 128;
    void prepare(double fs) {
        fs_ = fs;
        yin_.prepare(fs, 30.0, 512, kStep / 2);
        x_.assign(kSize, 0.0f);
        per_.assign(kSize / kStep, 0.0f);
        clear();
    }
    void clear() noexcept {
        yin_.clear();
        std::fill(x_.begin(), x_.end(), 0.0f);
        std::fill(per_.begin(), per_.end(), 0.0f);
        t_ = 0;
        filled_ = -1;
        last_ = 0.0f;
        hz_ = 0.0f;
    }
    // the range a pitch has to be in to count (outside it: unpitched)
    void range(float minHz, float maxHz, float sureness) noexcept {
        minHz_ = minHz;
        maxHz_ = maxHz;
        sure_ = sureness;
    }
    void push(float v) noexcept {
        x_[static_cast<std::size_t>(t_ & (kSize - 1))] = v;
        ++t_;
        if (!yin_.push(v)) return;
        const float hz = yin_.hz();
        const bool voiced = hz >= minHz_ && hz <= maxHz_ && yin_.confidence() >= sure_;
        hz_ = voiced ? hz : 0.0f;
        last_ = voiced ? static_cast<float>(fs_ / hz) : 0.0f;
        // the estimate belongs to the middle of the window it came from: hold it from the last one to there
        const std::int64_t centre = (t_ - kCentre) / kStep;
        for (std::int64_t k = std::max(filled_ + 1, centre - static_cast<std::int64_t>(per_.size()) + 1); k <= centre; ++k)
            per_[static_cast<std::size_t>(k & static_cast<std::int64_t>(per_.size() - 1))] = last_;
        filled_ = std::max(filled_, centre);
    }
    [[nodiscard]] std::int64_t now() const noexcept { return t_; }   // samples pushed so far
    [[nodiscard]] float hz() const noexcept { return hz_; }          // the latest estimate (0 = unpitched)
    // the period (samples) at input time `s` (0 = unpitched); newer than the last estimate: the last one
    [[nodiscard]] float period(std::int64_t s) const noexcept {
        const std::int64_t k = s / static_cast<std::int64_t>(kStep);
        if (k > filled_ || filled_ < 0) return last_;
        return per_[static_cast<std::size_t>(k & static_cast<std::int64_t>(per_.size() - 1))];
    }
    // the input at a (fractional) time
    [[nodiscard]] float at(double s) const noexcept {
        const auto i = static_cast<std::int64_t>(std::floor(s));
        const float f = static_cast<float>(s - static_cast<double>(i));
        const float a = x_[static_cast<std::size_t>(i & (kSize - 1))], b = x_[static_cast<std::size_t>((i + 1) & (kSize - 1))];
        return a + f * (b - a);
    }
    [[nodiscard]] double fs() const noexcept { return fs_; }

private:
    static constexpr std::int64_t kCentre = 900;   // about the middle of YIN's window, in samples
    double fs_ = 48000.0;
    Yin yin_;
    std::vector<float> x_, per_;
    std::int64_t t_ = 0, filled_ = -1;
    float last_ = 0.0f, hz_ = 0.0f, minHz_ = 40.0f, maxHz_ = 2000.0f, sure_ = 0.8f;
};

class PsolaVoice {
public:
    static constexpr std::uint32_t kOut = 8192;
    // latency: how far behind the input the output runs (room for two periods either side of a grain)
    void prepare(double fs, std::uint32_t latency) {
        latency_ = latency;
        unvoiced_ = 0.005 * fs;
        // a grain made now reaches maxL_ ahead and reads up to half a (30 Hz) period plus maxL_ further: all inside the latency
        maxL_ = 0.5 * (static_cast<double>(latency) - 0.5 * fs / 30.0 - 8.0);
        acc_.assign(kOut, 0.0f);
        nrm_.assign(kOut, 0.0f);
        clear();
    }
    void clear() noexcept {
        std::fill(acc_.begin(), acc_.end(), 0.0f);
        std::fill(nrm_.begin(), nrm_.end(), 0.0f);
        syn_ = -1.0;
        ana_ = 0.0;
    }
    // one output sample (call once per input sample, after PsolaInput::push). ratio: output pitch / input
    // pitch; formant: how much faster each grain is read (1 = formants stay, 2 = an octave up)
    float tick(const PsolaInput& in, float ratio, float formant) noexcept {
        const std::int64_t outT = in.now() - 1 - static_cast<std::int64_t>(latency_);
        if (syn_ < 0.0) {
            syn_ = static_cast<double>(outT) + maxL_;
            ana_ = syn_;
        }
        while (syn_ <= static_cast<double>(outT) + maxL_ + 1.0) grain(in, ratio, formant);
        const auto k = static_cast<std::size_t>(outT & (kOut - 1));
        // where grains pile up (a voice going up) their sum is brought back to one; where a squeezed grain leaves a
        // gap, it stays a gap (that is what a higher formant sounds like)
        const float y = acc_[k] / std::max(nrm_[k], 1.0f);
        acc_[k] = 0.0f;
        nrm_[k] = 0.0f;
        return y;
    }

private:
    void grain(const PsolaInput& in, float ratio, float formant) noexcept {
        float pa = in.period(static_cast<std::int64_t>(syn_));
        const bool voiced = pa > 0.0f;
        if (!voiced) {
            pa = static_cast<float>(unvoiced_);
            ratio = 1.0f;
        }
        const double ps = pa / std::clamp(ratio, 0.25f, 4.0f);
        // the grain's half-length as laid down: a period squeezed by the formant move; when nothing is squeezed and
        // the voice goes down, stretched to the spacing so there are no gaps
        const double f = std::clamp(static_cast<double>(formant), 0.25, 4.0);
        const double lOut = std::min(f > 1.0 ? pa / f : std::max<double>(pa / f, ps), maxL_ / std::max(1.0, f));
        // the analysis mark nearest this grain, one period after another (so the source keeps its own pace)
        while (ana_ + 0.5 * pa < syn_) ana_ += pa;
        while (ana_ - 0.5 * pa > syn_) ana_ -= pa;
        const double a = voiced ? ana_ : syn_;
        // every output sample j the grain covers: its distance from the grain's exact centre picks the window and
        // the source (so at a ratio and a formant of 1 the source is exactly j)
        const auto j0 = static_cast<std::int64_t>(std::ceil(syn_ - lOut)), j1 = static_cast<std::int64_t>(std::floor(syn_ + lOut));
        for (std::int64_t j = j0; j <= j1; ++j) {
            const double d = static_cast<double>(j) - syn_;
            const auto w = static_cast<float>(0.5 + 0.5 * std::cos(std::numbers::pi * d / lOut));
            const float v = in.at(a + d * f);
            const auto k = static_cast<std::size_t>(j & (kOut - 1));
            acc_[k] += w * v;
            nrm_[k] += w;
        }
        syn_ += ps;
    }

    std::uint32_t latency_ = 3200;
    double unvoiced_ = 240.0, maxL_ = 1598.0, syn_ = -1.0, ana_ = 0.0;
    std::vector<float> acc_, nrm_;
};

// Hz -> MIDI note number (fractional), for a reference A.
inline double midiOf(double hz, double refA = 440.0) noexcept { return 69.0 + 12.0 * std::log2(std::max(hz, 1e-3) / refA); }

} // namespace hem::rack::streets
