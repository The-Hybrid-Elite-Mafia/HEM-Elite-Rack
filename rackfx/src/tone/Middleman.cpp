// THE MIDDLEMAN — centre and sides faders; lows, highs and hits steered between them (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"
#include "../maxim/LimitKit.hpp"

#include <cmath>

namespace hem::rack {
namespace middleman {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"low", "Low", "%", 0.0f, 100.0f, 0.0f},
    {"high", "High", "%", 0.0f, 100.0f, 0.0f},
    {"punch", "Snap", "%", 0.0f, 100.0f, 0.0f},
    {"master", "All", "dB", -24.0f, 6.0f, 0.0f},
    {"center", "Center", "dB", -60.0f, 6.0f, 0.0f},
    {"sides", "Sides", "dB", -60.0f, 6.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"CENTER", "%", 0.0f, 100.0f}, {"OUT L", "dBFS", -36.0f, 0.0f}, {"OUT R", "dBFS", -36.0f, 0.0f}};
constexpr float kSplit[2] = {kLowHz, kHighHz};
inline float fader(float db) noexcept { return db <= -59.9f ? 0.0f : hc::fx::dbToGain(db); }   // the bottom is off

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& x : xo_) x.design(fs, kSplit, 3);
        reset();
    }
    void reset() noexcept override {
        for (auto& x : xo_) x.clear();
        fast_ = slow_ = 0.0f;
        mLevel_ = sLevel_ = 0.0;
        center_.prepare(fs_, 30, fader(p(Center)));
        sides_.prepare(fs_, 30, fader(p(Sides)));
        low_.prepare(fs_, 30, p(Low) * 0.01f);
        high_.prepare(fs_, 30, p(High) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Master)));
        pkL_.clear();
        pkR_.clear();
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
        publishMeter(2, -100.0f);
    }

    // Each range's centre content follows the CENTER fader at 0 and the SIDES fader at 100 (LOW under
    // 200 Hz, HIGH over 4 kHz, PUNCH: the hits, while they rise); the sides always follow SIDES.
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float fAtt = coefMs(fs_, 0.5), fRel = coefMs(fs_, 40.0), sAtt = coefMs(fs_, 20.0);
        const float punch = p(Punch) * 0.01f;
        const float wC = fader(p(Center)), wS = fader(p(Sides)), wLo = p(Low) * 0.01f, wHi = p(High) * 0.01f, wOut = hc::fx::dbToGain(p(Master));
        double mSum = 0.0, sSum = 0.0;
        float pl = 0.0f, pr = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
            float mb[3], sb[3];
            xo_[0].split(m, mb);
            xo_[1].split(s, sb);
            const float x = std::fabs(m);
            fast_ = envStep(fast_, x, fAtt, fRel);
            slow_ = envStep(slow_, x, sAtt, fRel);
            const float hit = std::clamp((dbOf(fast_) - dbOf(slow_)) / 12.0f, 0.0f, 1.0f);
            const float c = center_.next(wC), sd = sides_.next(wS), lo = low_.next(wLo), hi = high_.next(wHi);
            const float k[3] = {lo, 0.0f, hi};
            float mo = 0.0f, so = 0.0f;
            for (int b = 0; b < 3; ++b) {
                float t = k[b];
                t += (1.0f - t) * punch * hit;
                mo += mb[b] * (c + t * (sd - c));
                so += sb[b] * sd;
            }
            mSum += static_cast<double>(mo) * mo;
            sSum += static_cast<double>(so) * so;
            const float o = out_.next(wOut);
            L[i] = (mo + so) * o;
            R[i] = (mo - so) * o;
            pl = std::max(pl, std::fabs(L[i]));
            pr = std::max(pr, std::fabs(R[i]));
        }
        const double k = 1.0 / std::max<std::uint32_t>(n, 1);
        const double a = 1.0 - std::exp(-static_cast<double>(n) / (fs_ * kCenterTau));   // same smoothing at any block size
        mLevel_ += (mSum * k - mLevel_) * a;
        sLevel_ += (sSum * k - sLevel_) * a;
        const double total = mLevel_ + sLevel_;
        publishMeter(0, total > 1e-12 ? static_cast<float>(100.0 * mLevel_ / total) : 0.0f);
        publishMeter(1, pkL_.push(dbOf(pl), n, fs_));
        publishMeter(2, pkR_.push(dbOf(pr), n, fs_));
    }

private:
    double fs_ = 48000.0;
    Crossover<3> xo_[2];
    float fast_ = 0, slow_ = 0;
    static constexpr double kCenterTau = 0.030;   // 30 ms CENTER readout smoothing
    double mLevel_ = 0, sLevel_ = 0;
    hc::fx::Smoothed center_, sides_, low_, high_, out_;
    maxim::PeakHold pkL_, pkR_;
};

} // namespace
} // namespace middleman

std::unique_ptr<hc::fx::Effect> makeMiddleman() { return std::make_unique<middleman::Impl>(); }

} // namespace hem::rack
