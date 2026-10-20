// TERRITORY — 4- or 5-band dynamics, natural or linear phase (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

#include "hybrid/fx/Fft.hpp"

#include <complex>
#include <vector>

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace territory;

const char* const kTailN[] = {"QUICK", "LINGER"};
const char* const kPhaseN[] = {"NATURAL", "LINEAR"};
const char* const kBandsN[] = {"4 BANDS", "5 BANDS"};
const char* const kListenN[] = {"ALL", "1", "2", "3", "4", "5"};
const ParamInfo kParams[kCount] = {
    {"x1", "Crossover 1", "Hz", 30.0f, 500.0f, 120.0f, Taper::Log},
    {"x2", "Crossover 2", "Hz", 150.0f, 2500.0f, 600.0f, Taper::Log},
    {"x3", "Crossover 3", "Hz", 800.0f, 8000.0f, 2500.0f, Taper::Log},
    {"x4", "Crossover 4", "Hz", 2500.0f, 16000.0f, 7000.0f, Taper::Log},
    {"thr1", "Band 1 Threshold", "dB", -60.0f, 0.0f, -20.0f}, {"range1", "Band 1 Range", "dB", -24.0f, 12.0f, 0.0f}, {"gain1", "Band 1 Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"thr2", "Band 2 Threshold", "dB", -60.0f, 0.0f, -20.0f}, {"range2", "Band 2 Range", "dB", -24.0f, 12.0f, 0.0f}, {"gain2", "Band 2 Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"thr3", "Band 3 Threshold", "dB", -60.0f, 0.0f, -20.0f}, {"range3", "Band 3 Range", "dB", -24.0f, 12.0f, 0.0f}, {"gain3", "Band 3 Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"thr4", "Band 4 Threshold", "dB", -60.0f, 0.0f, -20.0f}, {"range4", "Band 4 Range", "dB", -24.0f, 12.0f, 0.0f}, {"gain4", "Band 4 Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"thr5", "Band 5 Threshold", "dB", -60.0f, 0.0f, -20.0f}, {"range5", "Band 5 Range", "dB", -24.0f, 12.0f, 0.0f}, {"gain5", "Band 5 Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"attack", "Attack", "ms", 0.5f, 200.0f, 10.0f, Taper::Log},
    {"release", "Release", "ms", 10.0f, 2000.0f, 150.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"knee", "Knee", "dB", 0.0f, 24.0f, 6.0f},
    {"tail", "Tail", "", 0, 1, 0, Taper::Choice, kTailN},
    {"phase", "Phase", "", 0, 1, 0, Taper::Choice, kPhaseN},
    {"bands", "Bands", "", 0, 1, 1, Taper::Choice, kBandsN},
    {"listen", "Listen", "", 0, 5, 0, Taper::Choice, kListenN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"BAND 1", "dB", -24.0f, 12.0f, true}, {"BAND 2", "dB", -24.0f, 12.0f, true}, {"BAND 3", "dB", -24.0f, 12.0f, true},
                                     {"BAND 4", "dB", -24.0f, 12.0f, true}, {"BAND 5", "dB", -24.0f, 12.0f, true}};

// The band's dynamic move for a level: above the threshold, RANGE < 0 pulls it down (3:1),
// RANGE > 0 lifts it (1:2), never more than |RANGE|. Soft knee of kneeDb.
float moveDb(float levelDb, float thr, float range, float kneeDb) noexcept {
    float over = levelDb - thr;
    if (kneeDb > 1e-3f) {
        if (over <= -kneeDb * 0.5f) over = 0.0f;
        else if (over < kneeDb * 0.5f) { const float k = over + kneeDb * 0.5f; over = k * k / (2.0f * kneeDb); }
    } else over = std::max(over, 0.0f);
    if (range < 0.0f) return std::max(range, -over * (2.0f / 3.0f));
    if (range > 0.0f) return std::min(range, over * 0.5f);
    return 0.0f;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        const bool five = p(Bands) >= 0.5f;
        if (!five && (i == X4 || i == thr(4) || i == range(4) || i == gain(4))) return false;
        if (i == Release) return p(AutoRelease) < 0.5f;
        for (int b = 0; b < kBands; ++b) if (i == thr(b)) return p(range(b)) != 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        natDelay_.prepare(kLatency);
        fft_.prepare(static_cast<int>(kFftSize));
        for (int c = 0; c < 2; ++c) {
            in_[c].assign(kFftSize, 0.0f);
            out_[c].assign(kFftSize, 0.0f);
            spec_[c].assign(kFftSize, {});
        }
        win_.resize(kFftSize);
        double s2 = 0;
        for (std::uint32_t i = 0; i < kFftSize; ++i) {
            win_[i] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kFftSize)));
            s2 += static_cast<double>(win_[i]) * win_[i];
        }
        sumW2_ = s2;
        binGain_.assign(kFftSize / 2 + 1, 1.0f);
        weights_.assign(static_cast<std::size_t>(kBands) * (kFftSize / 2 + 1), 0.0f);
        mem_.set(fs, 2.0, 3.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& x : xo_) x.clear();
        natDelay_.clear();
        for (int c = 0; c < 2; ++c) {
            std::fill(in_[c].begin(), in_[c].end(), 0.0f);
            std::fill(out_[c].begin(), out_[c].end(), 0.0f);
        }
        hopPos_ = 0;
        xDesigned_ = {-1, -1, -1, -1};
        bandsDesigned_ = -1;
        for (auto& v : envN_) v = 0.0f;
        for (auto& v : moveN_) v = 0.0f;
        for (auto& v : moveL_) v = 0.0f;
        mem_.v = 0.0f;
        phaseMix_ = p(Phase) >= 0.5f ? 1.0f : 0.0f;
        for (int b = 0; b < kBands; ++b) gainS_[b].prepare(fs_, 30, hc::fx::dbToGain(p(gain(b))));
        outS_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        for (int b = 0; b < kBands; ++b) publishMeter(b, 0.0f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int nb = bands();
        const float att = coefMs(fs_, p(Attack));
        const float knee = p(Knee);
        const int listen = std::clamp(static_cast<int>(std::lround(p(Listen))), 0, 5) - 1;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const bool linear = p(Phase) >= 0.5f;
        const float envRel = coefMs(fs_, 5.0);
        float most[kBands] = {0, 0, 0, 0, 0}, peakMove[kBands] = {0, 0, 0, 0, 0};
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            // --- natural phase: the crossover tree, a gain per band
            float band[2][kBands];
            xo_[0].split(L[i], band[0]);
            xo_[1].split(R[i], band[1]);
            float yN[2] = {0, 0};
            for (int b = 0; b < nb; ++b) {
                const float x = std::max(std::fabs(band[0][b]), std::fabs(band[1][b]));
                envN_[b] = x > envN_[b] ? x : x + envRel * (envN_[b] - x);
                const float target = moveDb(dbOf(envN_[b]), p(thr(b)), p(range(b)), knee);
                moveN_[b] = stepMove(moveN_[b], target, att, 1.0f);
                const float g = hc::fx::dbToGain(moveN_[b]) * gainS_[b].next(hc::fx::dbToGain(p(gain(b))));
                if (listen < 0 || listen == b) { yN[0] += band[0][b] * g; yN[1] += band[1][b] * g; }
                most[b] = std::min(most[b], moveN_[b]);
                if (std::fabs(moveN_[b]) > std::fabs(peakMove[b])) peakMove[b] = moveN_[b];
            }
            mem_.tick(std::min({moveN_[0], moveN_[1], moveN_[2]}) < -2.0f);
            const float nl = natDelay_.l.process(yN[0], kLatency), nr = natDelay_.r.process(yN[1], kLatency);
            // --- linear phase: overlap-add frames of the spectrum, gains set once per hop
            float yL[2];
            for (int c = 0; c < 2; ++c) {
                yL[c] = out_[c][hopPos_];
                in_[c][kFftSize - kHop + hopPos_] = io[c][i];
            }
            if (++hopPos_ == kHop) { hopPos_ = 0; frame(nb, listen, knee); }
            phaseMix_ = linear ? std::min(1.0f, phaseMix_ + step) : std::max(0.0f, phaseMix_ - step);
            const float o = outS_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (nl + phaseMix_ * (yL[0] - nl)) * o;
            R[i] = (nr + phaseMix_ * (yL[1] - nr)) * o;
        }
        for (int b = 0; b < kBands; ++b) publishMeter(b, b < nb ? (linear ? moveL_[b] : peakMove[b]) : 0.0f);
        float worst = 0.0f;
        for (int b = 0; b < nb; ++b) worst = std::min(worst, linear ? moveL_[b] : most[b]);
        publishReduction(worst);
    }

private:
    [[nodiscard]] int bands() const noexcept { return p(Bands) >= 0.5f ? 5 : 4; }
    // attack moves away from 0, release back toward it; TAIL and AUTO shape the release
    float stepMove(float cur, float target, float att, float hops) const noexcept {
        double relMs = p(Release);
        if (p(AutoRelease) >= 0.5f) relMs = 40.0 + 1200.0 * mem_.v;
        const float depth = std::min(std::fabs(cur) / 6.0f, 1.0f);
        relMs *= p(Tail) >= 0.5f ? (1.0 + 2.0 * (1.0 - depth)) : (0.5 + depth);
        const bool away = std::fabs(target) > std::fabs(cur) || (target > 0.0f) != (cur > 0.0f);
        float k;
        if (hops == 1.0f) k = away ? att : coefMs(fs_, relMs);
        else k = static_cast<float>(std::pow(away ? att : coefMs(fs_, relMs), hops));
        return target + k * (cur - target);
    }
    void design() noexcept {
        const int nb = bands();
        std::array<float, 4> x{p(X1), p(X2), p(X3), p(X4)};
        for (int k = 1; k < 4; ++k) x[k] = std::max(x[k], x[k - 1] * 1.25f);   // keep them in order
        if (x == xDesigned_ && nb == bandsDesigned_) return;
        xDesigned_ = x;
        bandsDesigned_ = nb;
        for (auto& xo : xo_) xo.design(fs_, x.data(), nb);
        // the linear path's per-bin band weights (they always add up to 1)
        const std::size_t bins = kFftSize / 2 + 1;
        for (std::size_t k = 0; k < bins; ++k) {
            const double f = std::max(1.0, static_cast<double>(k) * fs_ / kFftSize);
            double above = 1.0;
            for (int b = 0; b < nb; ++b) {
                double w = above;
                if (b + 1 < nb) {
                    const double low = 1.0 / (1.0 + std::pow(f / x[static_cast<std::size_t>(b)], 4.0));
                    w = above * low;
                    above *= 1.0 - low;
                }
                weights_[static_cast<std::size_t>(b) * bins + k] = static_cast<float>(w);
            }
        }
    }
    void frame(int nb, int listen, float knee) noexcept {
        const std::size_t bins = kFftSize / 2 + 1;
        for (int c = 0; c < 2; ++c) {
            auto& s = spec_[c];
            for (std::uint32_t i = 0; i < kFftSize; ++i) s[i] = {in_[c][i] * win_[i], 0.0f};
            fft_.forward(s.data());
        }
        // band levels (as a sine's peak would read), then the moves, once per hop
        const float att = coefMs(fs_, p(Attack));
        for (int b = 0; b < nb; ++b) {
            double pw[2] = {0, 0};
            for (std::size_t k = 1; k < bins; ++k) {
                const double w = weights_[static_cast<std::size_t>(b) * bins + k];
                for (int c = 0; c < 2; ++c) pw[c] += w * w * std::norm(spec_[c][k]);
            }
            const double a2 = 4.0 * std::max(pw[0], pw[1]) / (kFftSize * sumW2_);
            const float target = moveDb(powDb(a2), p(thr(b)), p(range(b)), knee);
            moveL_[b] = stepMove(moveL_[b], target, att, static_cast<float>(kHop));
            bandGainL_[b] = hc::fx::dbToGain(moveL_[b] + p(gain(b)));
            if (listen >= 0 && listen != b) bandGainL_[b] = 0.0f;
        }
        for (std::size_t k = 0; k < bins; ++k) {
            float g = 0.0f;
            for (int b = 0; b < nb; ++b) g += weights_[static_cast<std::size_t>(b) * bins + k] * bandGainL_[b];
            binGain_[k] = g;
        }
        const float norm = 0.5f;   // sqrt-Hann in and out at 75 % overlap adds up to 2
        for (int c = 0; c < 2; ++c) {
            auto& s = spec_[c];
            for (std::size_t k = 0; k < bins; ++k) s[k] *= binGain_[k];
            for (std::size_t k = bins; k < kFftSize; ++k) s[k] = std::conj(s[kFftSize - k]);
            fft_.inverse(s.data());
            auto& o = out_[c];
            std::copy(o.begin() + kHop, o.end(), o.begin());
            std::fill(o.end() - kHop, o.end(), 0.0f);
            for (std::uint32_t i = 0; i < kFftSize; ++i) o[i] += s[i].real() * win_[i] * norm;
            auto& in = in_[c];
            std::copy(in.begin() + kHop, in.end(), in.begin());
        }
    }

    double fs_ = 48000.0;
    Crossover<kBands> xo_[2];
    Delay2 natDelay_;
    hc::fx::Fft fft_;
    std::vector<float> in_[2], out_[2], win_, binGain_, weights_;
    std::vector<std::complex<float>> spec_[2];
    double sumW2_ = 1.0;
    std::uint32_t hopPos_ = 0;
    std::array<float, 4> xDesigned_{-1, -1, -1, -1};
    int bandsDesigned_ = -1;
    float envN_[kBands]{}, moveN_[kBands]{}, moveL_[kBands]{}, bandGainL_[kBands]{1, 1, 1, 1, 1};
    float phaseMix_ = 0.0f;
    Memory mem_;
    hc::fx::Smoothed gainS_[kBands], outS_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeTerritory() { return std::make_unique<Impl>(); }

} // namespace hem::rack
