// IRON FIST — input-driven console channel compressor + limiter + de-ess + iron (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

#include <vector>

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace ironfist;

const char* const kRatioN[] = {"1.5", "2", "3", "4", "6"};
const char* const kAttackN[] = {"FAST", "SLOW"};
const char* const kRecoveryN[] = {"100 ms", "400 ms", "800 ms", "1.5 s", "AUTO"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -10.0f, 30.0f, 0.0f},
    {"ratio", "Ratio", "", 0, 4, 2, Taper::Choice, kRatioN},
    {"attack", "Attack", "", 0, 1, 0, Taper::Choice, kAttackN},
    {"recovery", "Recovery", "", 0, 4, 4, Taper::Choice, kRecoveryN},
    {"limit", "Limiter", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"limitlevel", "Limit Level", "dB", -20.0f, 0.0f, -6.0f},
    {"deess", "De-Ess", "%", 0.0f, 100.0f, 0.0f},
    {"iron", "Iron", "%", 0.0f, 100.0f, 25.0f},
    {"output", "Output", "dB", -24.0f, 24.0f, 0.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override { return i != LimitLevel || p(Limit) >= 0.5f; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        clean_.prepare(OS::latency());
        mem_.set(fs, 2.0, 3.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        clean_.clear();
        sc_[0].clear();
        sc_[1].clear();
        deess_ = -1.0f;
        env_ = limEnv_ = 0.0f;
        gr_ = lim_ = 0.0f;
        mem_.v = 0.0f;
        ironMix_ = p(Iron) > 0.0f ? 1.0f : 0.0f;
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(DeEss) != deess_) {   // DE-ESS: the compressor's ear leans on 6 kHz, up to +15 dB
            deess_ = p(DeEss);
            const C c = C::make(T::Bell, fs_, 6000.0, 1.0, deess_ * 0.15);
            sc_[0].c = sc_[1].c = c;
        }
        const int ri = std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 4);
        const int rec = std::clamp(static_cast<int>(std::lround(p(Recovery))), 0, 4);
        const float att = coefMs(fs_, p(Attack) >= 0.5f ? 20.0 : 3.0);
        const float envRel = coefMs(fs_, 3.0);
        const bool limit = p(Limit) >= 0.5f;
        const float limLevel = p(LimitLevel);
        const float limAtt = coefMs(fs_, 0.3), limRel = coefMs(fs_, 60.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            const float l = L[i] * g, r = R[i] * g;
            const float x = std::max(std::fabs(sc_[0].tick(l)), std::fabs(sc_[1].tick(r)));
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float target = downDb(dbOf(env_), kThresholdDb, kRatios[ri], 6.0f);
            const double relMs = rec < 4 ? kRecoveryMs[rec] : 100.0 + 1400.0 * mem_.v;   // AUTO: quick after hits, slow when it's been working
            gr_ = follow(gr_, target, att, coefMs(fs_, relMs));
            mem_.tick(gr_ < -3.0f);
            const float gc = hc::fx::dbToGain(gr_);
            float yl = l * gc, yr = r * gc;
            // the limiter after it: catches what still pokes over LIMIT LEVEL
            const float y = std::max(std::fabs(yl), std::fabs(yr));
            limEnv_ = y > limEnv_ ? y : y + coefMs(fs_, 2.0) * (limEnv_ - y);
            const float lt = limit ? downDb(dbOf(limEnv_), limLevel, 20.0f, 2.0f) : 0.0f;
            lim_ = follow(lim_, lt, limAtt, limRel);
            const float gl = hc::fx::dbToGain(lim_);
            yl *= gl;
            yr *= gl;
            most = std::min(most, gr_ + lim_);
            wet_[0][i] = yl;
            wet_[1][i] = yr;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        if (lim_ > -1e-6f) lim_ = 0.0f;
        // the output iron, 4x oversampled: rounder lows and soft tops as it's driven
        const float iron = p(Iron) * 0.01f, drive = 1.0f + 2.5f * iron;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) x4[j] = tubeish(x4[j] * drive, 0.06f) / drive;
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        for (std::uint32_t i = 0; i < n; ++i) {
            ironMix_ = iron > 0.0f ? std::min(1.0f, ironMix_ + step) : std::max(0.0f, ironMix_ - step);
            const float cl = clean_.l.process(wet_[0][i], OS::latency()), cr = clean_.r.process(wet_[1][i], OS::latency());
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (cl + ironMix_ * (sat_[0][i] - cl)) * o;
            R[i] = (cr + ironMix_ * (sat_[1][i] - cr)) * o;
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, sat_;
    Delay2 clean_;
    hc::fx::Biquad sc_[2];
    float deess_ = -1.0f, env_ = 0, limEnv_ = 0, gr_ = 0, lim_ = 0, ironMix_ = 0;
    Memory mem_;
    hc::fx::Smoothed in_, out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeIronFist() { return std::make_unique<Impl>(); }

} // namespace hem::rack
