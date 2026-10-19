// HEM THE ACCOUNTANT (see Tools.hpp): BS.1770 / EBU R128 loudness, loudness range, true peak,
// correlation. The audio passes through untouched.
#include "hem/rack/Tools.hpp"

#include "hem/rack/KWeight.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace hem::rack {
namespace {
using namespace meter;
using hc::fx::ParamInfo;
using hc::fx::Taper;
using OS = hc::fx::Oversampler4;

const char* const kTargetN[] = {"-14", "-16", "-11", "-9"};
const char* const kResetN[] = {"A", "B"};   // any change = start over
const ParamInfo kParams[kCount] = {
    {"target", "Target", "", 0, 3, 0, Taper::Choice, kTargetN},
    {"reset", "Reset", "", 0, 1, 0, Taper::Toggle, kResetN},
};
const hc::fx::MeterInfo kMeterInfo[kMeters] = {
    {"MOMENTARY", "LUFS", -60.0f, 0.0f}, {"SHORT-TERM", "LUFS", -60.0f, 0.0f}, {"INTEGRATED", "LUFS", -60.0f, 0.0f},
    {"RANGE", "LU", 0.0f, 30.0f},        {"TRUE PEAK", "dBTP", -60.0f, 3.0f},  {"CORRELATION", "corr", -1.0f, 1.0f, true},
    {"TO TARGET", "LU", -30.0f, 30.0f, true},
};
// Every 400 ms block goes into a histogram (count and summed energy per 0.01 LU) instead of a list,
// so the gated integrated value costs the same after three minutes or three hours.
constexpr int kGateBins = 8000;   // -70 .. +10 LUFS in 0.01 LU steps (integrated)
constexpr int kBins = 800;        // -70 .. +10 LUFS in 0.1 LU steps (loudness range)
inline double lufsOf(double ms) { return ms > 0.0 ? -0.691 + 10.0 * std::log10(ms) : -100.0; }

class Impl final : public hc::fx::Effect {
public:
    Impl() : tp_{OS(1.0), OS(1.0)} { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        hop_ = static_cast<std::uint32_t>(std::lround(0.1 * fs));
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        up_.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        gateE_.assign(kGateBins, 0.0);
        gateN_.assign(kGateBins, 0u);
        hist_.assign(kBins, 0u);
        hc::fx::BiquadCoefs sh, hp;
        kWeighting(fs, sh, hp);
        kShelf_.set(sh);
        kHp_.set(hp);
        reset();
    }

    void reset() noexcept override {
        kShelf_.clear();
        kHp_.clear();
        for (auto& o : tp_) o.clear();
        hops_.fill(0.0);
        hopIdx_ = 0;
        hopsDone_ = 0;
        acc_ = 0.0;
        inHop_ = 0;
        std::fill(gateE_.begin(), gateE_.end(), 0.0);
        std::fill(gateN_.begin(), gateN_.end(), 0u);
        std::fill(hist_.begin(), hist_.end(), 0u);
        histEnergy_ = 0.0;
        histCount_ = 0;
        tpMax_ = 0.0f;
        lr_ = ll_ = rr_ = 0.0;
        resetSeen_ = p(Reset);
        for (int m = 0; m < kMeters; ++m) publishMeter(m, m == Correlation ? 1.0f : m == Range || m == ToTarget ? 0.0f : -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Reset) != resetSeen_) reset();   // RESET clicked
        const double mk = 1.0 - std::exp(-1.0 / (fs_ * 0.3));
        for (std::uint32_t i = 0; i < n; ++i) {
            const double l = kHp_.l.tick(kShelf_.l.tick(L[i])), r = kHp_.r.tick(kShelf_.r.tick(R[i]));
            acc_ += l * l + r * r;
            lr_ += (static_cast<double>(L[i]) * R[i] - lr_) * mk;
            ll_ += (static_cast<double>(L[i]) * L[i] - ll_) * mk;
            rr_ += (static_cast<double>(R[i]) * R[i] - rr_) * mk;
            if (++inHop_ == hop_) endHop();
        }
        const float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            tp_[c].up(io[c], up_.data(), n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) tpMax_ = std::max(tpMax_, std::fabs(up_[i]));
        }
        publishMeter(TruePeak, tpMax_ > 0.0f ? 20.0f * std::log10(tpMax_) : -100.0f);
        const double den = std::sqrt(ll_ * rr_);
        publishMeter(Correlation, den > 1e-12 ? static_cast<float>(std::clamp(lr_ / den, -1.0, 1.0)) : 1.0f);
    }

private:
    // Every 100 ms: momentary (last 4), short-term (last 30), the gated integrated value and the range.
    void endHop() noexcept {
        hops_[hopIdx_] = acc_ / static_cast<double>(hop_);
        hopIdx_ = (hopIdx_ + 1) % hops_.size();
        ++hopsDone_;
        acc_ = 0.0;
        inHop_ = 0;
        auto meanOf = [&](std::size_t k) {
            double s = 0;
            for (std::size_t j = 1; j <= k; ++j) s += hops_[(hopIdx_ + hops_.size() - j) % hops_.size()];
            return s / static_cast<double>(k);
        };
        if (hopsDone_ >= 4) {
            const double m = meanOf(4);
            publishMeter(Momentary, static_cast<float>(lufsOf(m)));
            // integrated: absolute gate -70 LUFS, then relative gate 10 LU under the gated mean
            if (const double lm = lufsOf(m); lm > -70.0) {
                const auto bin = static_cast<std::size_t>(std::clamp(static_cast<int>((lm + 70.0) * 100.0), 0, kGateBins - 1));
                gateE_[bin] += m;
                ++gateN_[bin];
                double s1 = 0;
                std::uint64_t c1 = 0;
                for (int k = 0; k < kGateBins; ++k) { s1 += gateE_[static_cast<std::size_t>(k)]; c1 += gateN_[static_cast<std::size_t>(k)]; }
                const double gate = lufsOf(s1 / static_cast<double>(c1)) - 10.0;
                const int g0 = std::clamp(static_cast<int>(std::ceil((gate + 70.0) * 100.0)), 0, kGateBins);
                double s2 = 0;
                std::uint64_t c2 = 0;
                for (int k = g0; k < kGateBins; ++k) { s2 += gateE_[static_cast<std::size_t>(k)]; c2 += gateN_[static_cast<std::size_t>(k)]; }
                const float integ = c2 > 0 ? static_cast<float>(lufsOf(s2 / static_cast<double>(c2))) : -100.0f;
                publishMeter(Integrated, integ);
                publishMeter(ToTarget, integ > -99.0f ? integ - kTargets[std::clamp(static_cast<int>(p(Target)), 0, 3)] : 0.0f);
            }
        }
        if (hopsDone_ >= 30) {
            const double st = meanOf(30);
            const double lst = lufsOf(st);
            publishMeter(ShortTerm, static_cast<float>(lst));
            // loudness range (EBU Tech 3342): short-term values, gated at -70 and 20 LU under their mean,
            // from the 10th to the 95th percentile
            if (lst > -70.0) {
                const int bin = std::clamp(static_cast<int>((lst + 70.0) * 10.0), 0, kBins - 1);
                ++hist_[static_cast<std::size_t>(bin)];
                histEnergy_ += st;
                ++histCount_;
                const double gate = lufsOf(histEnergy_ / static_cast<double>(histCount_)) - 20.0;
                const int g0 = std::clamp(static_cast<int>(std::ceil((gate + 70.0) * 10.0)), 0, kBins);
                std::uint64_t total = 0;
                for (int b = g0; b < kBins; ++b) total += hist_[static_cast<std::size_t>(b)];
                if (total > 0) {
                    auto pct = [&](double q) {
                        const std::uint64_t want = static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(total)));
                        std::uint64_t run = 0;
                        for (int b = g0; b < kBins; ++b) {
                            run += hist_[static_cast<std::size_t>(b)];
                            if (run >= std::max<std::uint64_t>(want, 1)) return -70.0 + (b + 0.5) * 0.1;
                        }
                        return 10.0;
                    };
                    publishMeter(Range, static_cast<float>(std::max(0.0, pct(0.95) - pct(0.10))));
                }
            }
        }
    }

    double fs_ = 48000;
    std::uint32_t hop_ = 4800, inHop_ = 0;
    hc::fx::Biquad2 kShelf_, kHp_;
    OS tp_[2];
    std::vector<float> up_;
    std::vector<double> gateE_;
    std::vector<std::uint32_t> gateN_;
    std::vector<std::uint32_t> hist_;
    std::array<double, 30> hops_{};
    std::size_t hopIdx_ = 0;
    std::uint64_t hopsDone_ = 0, histCount_ = 0;
    double acc_ = 0, histEnergy_ = 0, lr_ = 0, ll_ = 0, rr_ = 0;
    float tpMax_ = 0, resetSeen_ = 0;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeLoudnessMeter() { return std::make_unique<Impl>(); }

} // namespace hem::rack
