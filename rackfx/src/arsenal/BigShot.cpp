// BIG SHOT — loudness maximizer, one band or the limiting shared out over 5 / 16 bands (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"
#include "hem/rack/KWeight.hpp"

#include <vector>

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace bigshot;

const char* const kBandsN[] = {"1 BAND", "5 BANDS", "16 BANDS"};
const char* const kStyleN[] = {"CLEAN", "PUNCHY", "WARM"};
const char* const kDitherN[] = {"OFF", "24 BIT", "16 BIT"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -24.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -3.0f, 0.0f, -0.3f},
    {"release", "Release", "ms", 1.0f, 1000.0f, 100.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"bands", "Bands", "", 0, 2, 0, Taper::Choice, kBandsN},
    {"prioritylow", "Priority Low", "dB", -6.0f, 6.0f, 0.0f},
    {"prioritymid", "Priority Mid", "dB", -6.0f, 6.0f, 0.0f},
    {"priorityhigh", "Priority High", "dB", -6.0f, 6.0f, 0.0f},
    {"style", "Style", "", 0, 2, 0, Taper::Choice, kStyleN},
    {"dither", "Dither", "", 0, 2, 0, Taper::Choice, kDitherN},
};
const hc::fx::MeterInfo kMeters[] = {{"LOUDNESS", "LUFS", -40.0f, 0.0f}, {"PEAK", "dBFS", -40.0f, 3.0f}};
constexpr int kMaxBands = 16;
constexpr std::uint32_t kWin = kLookahead + 1;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i == Release) return p(AutoRelease) < 0.5f;
        if (i == PriorityLow || i == PriorityMid || i == PriorityHigh) return p(Bands) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& ch : delay_) for (auto& d : ch) d.prepare(kLookahead + 1);
        hold_.assign(kWin, 1.0f);
        box_.assign(kWin, 1.0f);
        hc::fx::BiquadCoefs sh, hp;
        kWeighting(fs, sh, hp);
        kShelf_.set(sh);
        kHp_.set(hp);
        reset();
    }
    void reset() noexcept override {
        for (auto& x : xo_) x.clear();
        for (auto& ch : delay_) for (auto& d : ch) d.clear();
        std::fill(hold_.begin(), hold_.end(), 1.0f);
        std::fill(box_.begin(), box_.end(), 1.0f);
        boxSum_ = static_cast<double>(kWin);
        rel_ = 1.0f;
        pos_ = 0;
        memory_ = 0.0f;
        designed_ = -1;
        for (auto& e : energy_) e = 0.0f;
        for (auto& g : bandDb_) g = 0.0f;
        rng_ = 22222u;
        kShelf_.clear();
        kHp_.clear();
        ms_ = 0.0;
        metered_ = 0;
        pkHold_ = -100.0f;
        pkAge_ = 0.0;
        drive_.prepare(fs_, 30, hc::fx::dbToGain(-p(Threshold)));
        ceil_.prepare(fs_, 30, hc::fx::dbToGain(p(Ceiling)));
        design();
        publishReduction(0.0f);
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int nb = xo_[0].bands();
        const bool autoRel = p(AutoRelease) >= 0.5f;
        const int style = std::clamp(static_cast<int>(std::lround(p(Style))), 0, 2);
        const double relMs = p(Release) * (style == 1 ? 0.6 : 1.0);
        const float manualK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * relMs * 0.001)));
        const float chargeK = static_cast<float>(1.0 - std::exp(-1.0 / fs_));
        const float drainK = static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * 2.0)));
        const float eK = 1.0f - coefMs(fs_, 30.0);
        const int dither = std::clamp(static_cast<int>(std::lround(p(Dither))), 0, 2);
        const float lsb = dither == 2 ? 1.0f / 32768.0f : 1.0f / 8388608.0f;
        // priority per band: LOW / MID / HIGH spread over the bands (more priority = gives up less)
        float prioW[kMaxBands];
        for (int b = 0; b < nb; ++b) {
            const float t = nb == 1 ? 0.5f : static_cast<float>(b) / static_cast<float>(nb - 1);
            const float pr = t < 0.5f ? p(PriorityLow) + (p(PriorityMid) - p(PriorityLow)) * (t * 2.0f)
                                      : p(PriorityMid) + (p(PriorityHigh) - p(PriorityMid)) * ((t - 0.5f) * 2.0f);
            prioW[b] = hc::fx::dbToGain(-pr);
        }
        float most = 1.0f, pk = 0.0f;
        double kSum = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float d = drive_.next(hc::fx::dbToGain(-p(Threshold)));
            const float c = ceil_.next(hc::fx::dbToGain(p(Ceiling)));
            float bl[kMaxBands], br[kMaxBands];
            xo_[0].split(L[i] * d, bl);
            xo_[1].split(R[i] * d, br);
            // the peak of what would come out unchanged (the bands added back up)
            float sl = 0, sr = 0;
            for (int b = 0; b < nb; ++b) { sl += bl[b]; sr += br[b]; }
            const float peak = std::max(std::fabs(sl), std::fabs(sr));
            const float need = peak > c ? c / peak : 1.0f;
            // held over the look-ahead, released, then averaged over the look-ahead (no clicks)
            hold_[pos_] = need;
            float held = 1.0f;
            for (float v : hold_) held = std::min(held, v);
            const float relK = autoRel ? static_cast<float>(1.0 - std::exp(-1.0 / (fs_ * (0.02 + 0.35 * memory_) * (style == 1 ? 0.6 : 1.0)))) : manualK;
            rel_ = held < rel_ ? held : rel_ + (held - rel_) * relK;
            boxSum_ += static_cast<double>(rel_) - box_[pos_];
            box_[pos_] = rel_;
            pos_ = (pos_ + 1) % kWin;
            const float g = static_cast<float>(boxSum_ / static_cast<double>(kWin));
            memory_ += g < 0.79f ? (1.0f - memory_) * chargeK : -memory_ * drainK;
            most = std::min(most, g);
            const float rDb = 20.0f * std::log10(std::max(std::min(g, 1.0f), 1e-6f));
            // share the reduction out: the bands with the most energy (and the least priority) give the most;
            // weighted so that, on the energy the bands carry, it averages to the full reduction
            float sumE = 0, sumEW = 0, w[kMaxBands];
            for (int b = 0; b < nb; ++b) {
                const float e = std::max(bl[b] * bl[b], br[b] * br[b]);
                energy_[b] += (e - energy_[b]) * eK;
                w[b] = std::sqrt(std::max(energy_[b], 1e-12f)) * prioW[b];
                sumE += energy_[b];
                sumEW += energy_[b] * w[b];
            }
            const float norm = sumEW > 1e-20f ? sumE / sumEW : 1.0f;
            float yl = 0, yr = 0;
            for (int b = 0; b < nb; ++b) {
                const float gb = nb == 1 ? std::min(g, 1.0f) : hc::fx::dbToGain(std::max(rDb * w[b] * norm, -40.0f));
                yl += delay_[0][static_cast<std::size_t>(b)].process(bl[b], kLookahead) * gb;
                yr += delay_[1][static_cast<std::size_t>(b)].process(br[b], kLookahead) * gb;
            }
            // the final safety: never past the ceiling (WARM rounds into it instead of a hard stop)
            if (style == 2) { yl = roundInto(yl, c); yr = roundInto(yr, c); }
            if (dither > 0) {   // TPDF dither, then round to the bit depth
                yl = std::round((yl + lsb * (rand01() - rand01())) / lsb) * lsb;
                yr = std::round((yr + lsb * (rand01() - rand01())) / lsb) * lsb;
            }
            yl = std::clamp(yl, -c, c);   // last: nothing ever passes the ceiling
            yr = std::clamp(yr, -c, c);
            L[i] = yl;
            R[i] = yr;
            pk = std::max({pk, std::fabs(yl), std::fabs(yr)});
            const float kl = kHp_.l.tick(kShelf_.l.tick(yl)), kr = kHp_.r.tick(kShelf_.r.tick(yr));
            kSum += static_cast<double>(kl) * kl + static_cast<double>(kr) * kr;
        }
        if (pos_ == 0) {   // re-add the window once per cycle so rounding never drifts
            double s = 0;
            for (float v : box_) s += v;
            boxSum_ = s;
        }
        publishReduction(20.0f * std::log10(std::max(most, 1e-6f)));
        metered_ += n;
        const double k = std::max(1.0 - std::exp(-static_cast<double>(n) / (fs_ * 3.0)), static_cast<double>(n) / static_cast<double>(metered_));
        ms_ += (kSum / std::max(n, 1u) - ms_) * k;
        publishMeter(0, static_cast<float>(ms_ > 1e-12 ? -0.691 + 10.0 * std::log10(ms_) : -100.0));
        const float pkDb = dbOf(pk);
        pkAge_ += static_cast<double>(n) / fs_;
        if (pkDb >= pkHold_) { pkHold_ = pkDb; pkAge_ = 0.0; }
        else if (pkAge_ > 1.0) pkHold_ = std::max(pkDb, pkHold_ - 20.0f * static_cast<float>(n / fs_));
        publishMeter(1, std::max(pkHold_, -100.0f));
    }

private:
    static float roundInto(float y, float c) noexcept {
        const float k = 0.8f * c, a = std::fabs(y);
        if (a <= k) return y;
        const float t = (a - k) / (c - k);
        return std::copysign(k + (c - k) * t / (1.0f + t), y);
    }
    float rand01() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) / 16777216.0f;
    }
    void design() noexcept {
        const int sel = std::clamp(static_cast<int>(std::lround(p(Bands))), 0, 2);
        if (sel == designed_) return;
        designed_ = sel;
        float hz[kMaxBands];
        int nb = 1;
        if (sel == 1) {
            constexpr float x5[4] = {100.0f, 400.0f, 1600.0f, 6000.0f};
            for (int k = 0; k < 4; ++k) hz[k] = x5[k];
            nb = 5;
        } else if (sel == 2) {
            for (int k = 0; k < 15; ++k) hz[k] = 40.0f * std::pow(350.0f, static_cast<float>(k) / 14.0f);   // 40 Hz .. 14 kHz
            nb = 16;
        }
        for (auto& x : xo_) { x.design(fs_, hz, nb); x.clear(); }
        for (auto& ch : delay_) for (auto& d : ch) d.clear();
        for (auto& e : energy_) e = 0.0f;
    }

    double fs_ = 48000.0;
    Crossover<kMaxBands> xo_[2];
    std::array<std::array<hc::fx::DelayLine, kMaxBands>, 2> delay_{};
    std::vector<float> hold_, box_;
    double boxSum_ = 0;
    float rel_ = 1.0f, memory_ = 0.0f;
    std::uint32_t pos_ = 0;
    int designed_ = -1;
    float energy_[kMaxBands]{}, bandDb_[kMaxBands]{};
    std::uint32_t rng_ = 22222u;
    hc::fx::Biquad2 kShelf_, kHp_;
    double ms_ = 0, pkAge_ = 0;
    std::uint64_t metered_ = 0;
    float pkHold_ = -100.0f;
    hc::fx::Smoothed drive_, ceil_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeBigShot() { return std::make_unique<Impl>(); }

} // namespace hem::rack
