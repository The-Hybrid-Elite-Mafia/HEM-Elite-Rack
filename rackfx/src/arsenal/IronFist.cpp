// IRON FIST — input-driven console channel compressor + limiter, de-ess, analog iron (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

#include <vector>

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace ironfist;

const char* const kRatioN[] = {"1.5:1", "2:1", "3:1", "4:1", "6:1"};
const char* const kReleaseN[] = {"400 MS", "800 MS", "1.5 S", "AUTO"};
const char* const kLimRelN[] = {"100 MS", "200 MS", "800 MS", "AUTO"};
const char* const kAnalogN[] = {"OFF", "25 %", "50 %", "100 %"};
constexpr float kAnalogAmt[] = {0.0f, 0.25f, 0.5f, 1.0f};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -24.0f, 18.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"meter", "Meter", "", 0, 2, 1, Taper::Choice, kMeterN},
    {"analog", "Vintage", "", 0, 3, 1, Taper::Choice, kAnalogN},
    {"compress", "Compress", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"ratio", "Ratio", "", 0, 4, 2, Taper::Choice, kRatioN},
    {"release", "Release", "", 0, 3, 3, Taper::Choice, kReleaseN},
    {"deess", "De-Ess", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"limiter", "Limiter", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"limitlevel", "Limit At", "dBu", 4.0f, 12.0f, 8.0f},
    {"limitattack", "Limit Attack", "us", 1.0f, 1000.0f, 100.0f, Taper::Log},
    {"limitrelease", "Limit Release", "", 0, 3, 3, Taper::Choice, kLimRelN},
    {"automakeup", "Auto Makeup", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"trim", "Trim", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i == LimitLevel || i == LimitAttack || i == LimitRelease) return p(Limiter) >= 0.5f;
        if (i == Ratio || i == Release || i == DeEss) return p(Compress) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        clean_.prepare(OS::latency());
        dry_.prepare(OS::latency());
        mem_.set(fs, 2.0, 3.0);
        limMem_.set(fs, 2.0, 3.0);
        // DE-ESS: the detector only hears the highs (4th-order high-pass at 5 kHz)
        hp_[0][0].c = hp_[0][1].c = hp_[1][0].c = hp_[1][1].c = C::make(T::HighPass, fs, 5000.0, 0.7071);
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        clean_.clear();
        dry_.clear();
        for (auto& c : hp_) for (auto& b : c) b.clear();
        env_ = limEnv_ = 0.0f;
        gr_ = lim_ = 0.0f;
        mem_.v = limMem_.v = 0.0f;
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        const int an = std::clamp(static_cast<int>(std::lround(p(Analog))), 0, 3);
        ironMix_ = an > 0 ? 1.0f : 0.0f;
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(outDb()));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        trim_.prepare(fs_, 30, hc::fx::dbToGain(p(Trim)));
        publishReduction(0.0f);
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int ri = std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 4);
        const int rel = std::clamp(static_cast<int>(std::lround(p(Release))), 0, 3);
        const int lrel = std::clamp(static_cast<int>(std::lround(p(LimitRelease))), 0, 3);
        const bool comp = p(Compress) >= 0.5f, deess = p(DeEss) >= 0.5f, limit = p(Limiter) >= 0.5f;
        const float att = coefMs(fs_, 3.0);   // the compressor's attack is fixed, as on the hardware
        const float envRel = coefMs(fs_, 3.0);
        const float limLevel = p(LimitLevel) - 22.0f;   // dBu: +22 dBu = 0 dBFS
        const float limAtt = coefMs(fs_, p(LimitAttack) * 0.001);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input)));
            const float l = L[i] * g, r = R[i] * g;
            vuIn_.tick(l, r);
            float dl = l, dr = r;
            if (deess) { dl = hp_[0][1].tick(hp_[0][0].tick(l)); dr = hp_[1][1].tick(hp_[1][0].tick(r)); }
            const float x = std::max(std::fabs(dl), std::fabs(dr));
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float target = comp ? downDb(dbOf(env_), kThresholdDb, kRatios[ri], 6.0f) : 0.0f;
            const double relMs = rel < 3 ? kReleaseMs[rel] : 100.0 + 1400.0 * mem_.v;   // AUTO: quick after hits, slow when it's been working
            gr_ = follow(gr_, target, att, coefMs(fs_, relMs));
            mem_.tick(gr_ < -3.0f);
            const float gc = hc::fx::dbToGain(gr_);
            float yl = l * gc, yr = r * gc;
            // the limiter after it: catches what still pokes over LIMIT LEVEL
            const float y = std::max(std::fabs(yl), std::fabs(yr));
            limEnv_ = y > limEnv_ ? y : y + coefMs(fs_, 2.0) * (limEnv_ - y);
            const float lt = limit ? downDb(dbOf(limEnv_), limLevel, 20.0f, 2.0f) : 0.0f;
            const double lrMs = lrel < 3 ? kLimitReleaseMs[lrel] : 100.0 + 700.0 * limMem_.v;
            lim_ = follow(lim_, lt, limAtt, coefMs(fs_, lrMs));
            limMem_.tick(lim_ < -2.0f);
            const float gl = hc::fx::dbToGain(lim_);
            yl *= gl;
            yr *= gl;
            most = std::min(most, gr_ + lim_);
            wet_[0][i] = yl;
            wet_[1][i] = yr;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        if (lim_ > -1e-6f) lim_ = 0.0f;
        // ANALOG: the output iron, 4x oversampled (rounder lows and soft tops as it's driven), and the
        // console's hum and hiss under the music
        const int an = std::clamp(static_cast<int>(std::lround(p(Analog))), 0, 3);
        const float amt = kAnalogAmt[an], drive = 1.0f + 2.5f * amt;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) x4[j] = tubeish(x4[j] * drive, 0.06f) / drive;
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        for (std::uint32_t i = 0; i < n; ++i) {
            ironMix_ = amt > 0.0f ? std::min(1.0f, ironMix_ + step) : std::max(0.0f, ironMix_ - step);
            const float cl = clean_.l.process(wet_[0][i], OS::latency()), cr = clean_.r.process(wet_[1][i], OS::latency());
            const float o = out_.next(hc::fx::dbToGain(outDb()));
            float yl = (cl + ironMix_ * (sat_[0][i] - cl)) * o, yr = (cr + ironMix_ * (sat_[1][i] - cr)) * o;
            const float dl = dry_.l.process(L[i], OS::latency()), dr = dry_.r.process(R[i], OS::latency());
            const float m = mix_.next(p(Mix) * 0.01f), t = trim_.next(hc::fx::dbToGain(p(Trim)));
            yl = (dl + m * (yl - dl)) * t;
            yr = (dr + m * (yr - dr)) * t;
            hum_.tick(amt > 0.0f ? 1 : 0, amt, yl, yr);
            L[i] = yl;
            R[i] = yr;
            vuOut_.tick(yl, yr);
        }
        publishReduction(most);
        publishMeter(0, vuIn_.vu());
        publishMeter(1, vuOut_.vu());
    }

private:
    // AUTO MAKEUP: the output comes down as INPUT goes up (and up as it goes down), most of the way
    float outDb() const noexcept { return p(Output) - (p(AutoMakeup) >= 0.5f ? 0.8f * p(Input) : 0.0f); }

    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, sat_;
    Delay2 clean_, dry_;
    hc::fx::Biquad hp_[2][2];
    float env_ = 0, limEnv_ = 0, gr_ = 0, lim_ = 0, ironMix_ = 0;
    Memory mem_, limMem_;
    Hum hum_;
    Vu vuIn_, vuOut_;
    hc::fx::Smoothed in_, out_, mix_, trim_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeIronFist() { return std::make_unique<Impl>(); }

} // namespace hem::rack
