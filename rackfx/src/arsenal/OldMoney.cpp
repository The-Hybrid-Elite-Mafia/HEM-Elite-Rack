// OLD MONEY — the smooth all-purpose compressor (see Arsenal.hpp).
//
// THRESHOLD, RATIO 0.5..50 (under 1 = the loud parts get louder: upward expansion), ATTACK, RELEASE
// (AUTO: the release follows the music — quick after short peaks, slower when it works hard; MANUAL:
// exactly RELEASE), BEHAVIOR SNAP BACK (lets go faster as it nears no reduction: more level) or EASE OUT
// (lets go slower near the end: smoother), CHARACTER WARM (a touch of low harmonics as it works) or
// SMOOTH (clean), OUTPUT (a soft limiter after it keeps the peaks under 0 dBFS), MIX, TRIM.
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

namespace hem::rack {
namespace oldmoney {
namespace {
using namespace arsenal;

const char* const kRelModeN[] = {"AUTO", "MANUAL"};
const char* const kBehaviorN[] = {"SNAP BACK", "EASE OUT"};
const char* const kCharacterN[] = {"COLORED", "PURE"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -60.0f, 0.0f, -20.0f},
    {"ratio", "Ratio", ":1", 0.5f, 50.0f, 1.0f, Taper::Log},
    {"attack", "Attack", "ms", 0.5f, 500.0f, 16.0f, Taper::Log},
    {"release", "Release", "ms", 5.0f, 5000.0f, 160.0f, Taper::Log},
    {"releasemode", "Release Mode", "", 0, 1, 0, Taper::Choice, kRelModeN},
    {"behavior", "Let-Go", "", 0, 1, 0, Taper::Choice, kBehaviorN},
    {"character", "Color", "", 0, 1, 0, Taper::Choice, kCharacterN},
    {"output", "Output", "dB", -30.0f, 30.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"trim", "Trim", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "dBFS", -60.0f, 3.0f}, {"OUTPUT", "dBFS", -60.0f, 3.0f}, {"LIMITER", "dB", -20.0f, 0.0f}};

// the gain change (dB) for a level: compression over the threshold for ratios over 1, the loud parts
// lifted for ratios under 1 (at most +24 dB), with a 6 dB soft knee
float gainDb(float level, float thr, float ratio) noexcept {
    if (ratio >= 1.0f) return downDb(level, thr, ratio, 6.0f);
    const float over = level - thr, K = 6.0f;
    float oe;
    if (over <= -K * 0.5f) return 0.0f;
    if (over >= K * 0.5f) oe = over;
    else { const float k = over + K * 0.5f; oe = k * k / (2.0f * K); }
    return std::min(oe * (1.0f / ratio - 1.0f), 24.0f);
}

// the output limiter: untouched under -3 dBFS, rounded into 0 dBFS above
inline float roundInto(float y) noexcept {
    constexpr float c = 1.0f, k = 0.708f;
    const float a = std::fabs(y);
    if (a <= k) return y;
    const float t = (a - k) / (c - k);
    return std::copysign(k + (c - k) * t / (1.0f + t), y);
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        mem_.set(fs, 1.0, 3.0);
        dcK_ = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * 8.0 / fs));
        reset();
    }
    void reset() noexcept override {
        env_ = 0.0f;
        gr_ = 0.0f;
        mem_.v = 0.0f;
        dcL_ = dcR_ = 0.0f;
        inPk_.clear();
        outPk_.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        trim_.prepare(fs_, 30, hc::fx::dbToGain(p(Trim)));
        warm_.prepare(fs_, 30, p(Character) < 0.5f ? 1.0f : 0.0f);
        publishReduction(0.0f);
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
        publishMeter(2, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold), ratio = p(Ratio);
        const float att = coefMs(fs_, p(Attack));
        const bool autoRel = p(ReleaseMode) < 0.5f, opto = p(Behavior) >= 0.5f;
        const double relMs = p(Release);
        const float envRel = coefMs(fs_, 2.0);
        float most = 0.0f, inPk = 0.0f, outPk = 0.0f, limMost = 1.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            inPk = std::max(inPk, x);
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float target = gainDb(dbOf(env_), thr, ratio);
            // the release: AUTO shortens it after single peaks and stretches it when working hard;
            // SNAP BACK speeds up as it nears no change, EASE OUT slows down there
            const float depth = std::clamp(std::fabs(gr_) / 10.0f, 0.0f, 1.0f);
            double ms = relMs * (autoRel ? 0.35 + 1.3 * mem_.v : 1.0);
            ms *= opto ? 1.0 + 3.0 * (1.0 - depth) : 0.25 + 0.75 * depth;
            if (target > 0.0f) gr_ = target + (target > gr_ ? att : coefMs(fs_, ms)) * (gr_ - target);   // lifting: attack up, release down
            else gr_ = follow(gr_, target, att, coefMs(fs_, ms));
            mem_.tick(gr_ < -3.0f);
            most = std::fabs(gr_) > std::fabs(most) ? gr_ : most;
            const float g = hc::fx::dbToGain(gr_);
            float yl = L[i] * g, yr = R[i] * g;
            // WARM: a little even harmonic content as it works (bounded), DC taken out
            const float w = warm_.next(p(Character) < 0.5f ? 1.0f : 0.0f) * std::min(std::fabs(gr_) / 12.0f, 1.0f) * 0.15f;
            yl += w * yl * yl / (1.0f + std::fabs(yl));
            yr += w * yr * yr / (1.0f + std::fabs(yr));
            dcL_ += (yl - dcL_) * dcK_;
            dcR_ += (yr - dcR_) * dcK_;
            yl -= dcL_;
            yr -= dcR_;
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            yl *= o;
            yr *= o;
            const float before = std::max(std::fabs(yl), std::fabs(yr));
            yl = roundInto(yl);
            yr = roundInto(yr);
            const float after = std::max(std::fabs(yl), std::fabs(yr));
            if (before > 1e-9f) limMost = std::min(limMost, after / before);
            const float m = mix_.next(p(Mix) * 0.01f), t = trim_.next(hc::fx::dbToGain(p(Trim)));
            L[i] = (L[i] + m * (yl - L[i])) * t;
            R[i] = (R[i] + m * (yr - R[i])) * t;
            outPk = std::max({outPk, std::fabs(L[i]), std::fabs(R[i])});
        }
        if (std::fabs(gr_) < 1e-6f) gr_ = 0.0f;
        publishReduction(std::min(most, 0.0f));
        publishMeter(0, inPk_.push(dbOf(inPk), n, fs_));
        publishMeter(1, outPk_.push(dbOf(outPk), n, fs_));
        publishMeter(2, dbOf(limMost));
    }

private:
    struct Hold {
        float hold = -100.0f;
        double age = 0;
        void clear() noexcept { hold = -100.0f; age = 0; }
        float push(float db, std::uint32_t n, double fs) noexcept {
            age += static_cast<double>(n) / fs;
            if (db >= hold) { hold = db; age = 0; }
            else if (age > 1.5) hold = std::max(db, hold - 20.0f * static_cast<float>(n / fs));
            return std::max(hold, -100.0f);
        }
    };
    double fs_ = 48000.0;
    float env_ = 0.0f, gr_ = 0.0f, dcL_ = 0.0f, dcR_ = 0.0f, dcK_ = 0.0f;
    Memory mem_;
    Hold inPk_, outPk_;
    hc::fx::Smoothed out_, mix_, trim_, warm_;
};

} // namespace
} // namespace oldmoney

std::unique_ptr<hc::fx::Effect> makeOldMoney() { return std::make_unique<oldmoney::Impl>(); }

} // namespace hem::rack
