// QUICK DRAW — the fast transistor (FET) limiting amplifier (see Arsenal.hpp).
//
// INPUT drives the signal into the FET stage (more input = more limiting; -30 on the dial = unity
// into it), whose threshold sits at -24 dBFS; OUTPUT sets the level out (-18 on the dial = unity).
// ATTACK and RELEASE are 1..7 dials where 7 is the fastest (attack 0.8 ms .. 20 us, release 1.1 s ..
// 50 ms). RATIO 4 / 8 / 12 / 20 : 1, or ALL (every ratio button in at once: the harder, rougher,
// over-compressing curve). The amplifier colours the sound more the harder it works; EARLY (the
// first revision) more than LATE. COMPRESS OFF leaves only the amplifier. 4x oversampled colour,
// latency 23 samples.
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

#include <vector>

namespace hem::rack {
namespace quickdraw {
namespace {
using namespace arsenal;

const char* const kRatioN[] = {"4:1", "8:1", "12:1", "20:1", "ALL"};
const char* const kRevN[] = {"EARLY", "LATE"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -60.0f, 0.0f, -30.0f},
    {"output", "Output", "dB", -60.0f, 0.0f, -18.0f},
    {"attack", "Attack", "/10", 1.0f, 7.0f, 3.0f},
    {"release", "Release", "/10", 1.0f, 7.0f, 4.0f},
    {"ratio", "Ratio", "", 0, 4, 0, Taper::Choice, kRatioN},
    {"compress", "Compress", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"revision", "Revision", "", 0, 1, 0, Taper::Choice, kRevN},
    {"automakeup", "Auto Makeup", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"analog", "Analog", "", 0, 2, 0, Taper::Choice, kMainsN},
    {"meter", "Meter", "", 0, 2, 1, Taper::Choice, kMeterN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"trim", "Trim", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};
constexpr float kRatios[] = {4.0f, 8.0f, 12.0f, 20.0f};
constexpr float kThresholdDb = -24.0f;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i == Attack || i == Release || i == Ratio) return p(Compress) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        col_.assign(mb, 0.0f);
        dry_.prepare(OS::latency());
        clean_.prepare(OS::latency());
        colD_.prepare(OS::latency());
        workK_ = 1.0f - coefMs(fs, 50.0);
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        auto_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        dry_.clear();
        clean_.clear();
        colD_.clear();
        work_ = 0.0f;
        env_ = 0.0f;
        gr_ = 0.0f;
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        auto_.clear();
        in_.prepare(fs_, 30, hc::fx::dbToGain(p(Input) + 30.0f));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output) + 18.0f));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        trim_.prepare(fs_, 30, hc::fx::dbToGain(p(Trim)));
        publishReduction(0.0f);
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const bool comp = p(Compress) >= 0.5f, autoMk = p(AutoMakeup) >= 0.5f;
        const int ri = std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 4);
        const bool all = ri == 4;
        // the dials: 1 = slowest, 7 = fastest
        const float a = std::clamp((p(Attack) - 1.0f) / 6.0f, 0.0f, 1.0f), r = std::clamp((p(Release) - 1.0f) / 6.0f, 0.0f, 1.0f);
        const float attMs = 0.8f * std::pow(0.025f, a), relMs = 1100.0f * std::pow(50.0f / 1100.0f, r);
        const float att = coefMs(fs_, attMs * (all ? 0.7f : 1.0f)), rel = coefMs(fs_, relMs * (all ? 0.8f : 1.0f));
        const float envRel = coefMs(fs_, 1.0);
        const bool early = p(Revision) < 0.5f;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(hc::fx::dbToGain(p(Input) + 30.0f));
            const float l = L[i] * g, rr = R[i] * g;
            vuIn_.tick(L[i], R[i]);
            const float x = std::max(std::fabs(l), std::fabs(rr));
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            float target = 0.0f;
            if (comp) {
                const float lvl = dbOf(env_);
                // ALL: a lower knee that bends past flat (loud parts come out a little quieter)
                target = all ? std::max(downDb(lvl, kThresholdDb - 4.0f, 60.0f, 2.0f) - 0.15f * std::max(lvl - kThresholdDb, 0.0f), -40.0f)
                             : downDb(lvl, kThresholdDb, kRatios[ri], 3.0f);
            }
            gr_ = follow(gr_, target, att, rel);
            most = std::min(most, gr_);
            const float mk = auto_.tick(gr_);
            const float gc = hc::fx::dbToGain(gr_ + (autoMk ? mk : 0.0f));
            wet_[0][i] = l * gc;
            wet_[1][i] = rr * gc;
            // how much of the amplifier's colour: some always, more the harder it limits (20 dB = all)
            work_ += (std::clamp(-gr_ / 20.0f, 0.0f, 1.0f) - work_) * workK_;
            col_[i] = std::min((early ? 0.35f : 0.15f) + (all ? 0.4f : 0.0f) + (early ? 0.65f : 0.45f) * work_, 1.0f);
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        // the amplifier: odd harmonics, mixed in more as it limits harder (and more with EARLY, and with ALL)
        constexpr float drive = 1.6f;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) x4[j] = softClip(x4[j] * drive) / drive;
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        const int mains = static_cast<int>(std::lround(p(Analog)));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float o = out_.next(hc::fx::dbToGain(p(Output) + 18.0f));
            const float dl = dry_.l.process(L[i], OS::latency()), dr = dry_.r.process(R[i], OS::latency());
            const float m = mix_.next(p(Mix) * 0.01f), t = trim_.next(hc::fx::dbToGain(p(Trim)));
            const float c = colD_.process(col_[i], OS::latency());
            const float cl = clean_.l.process(wet_[0][i], OS::latency()), cr = clean_.r.process(wet_[1][i], OS::latency());
            const float wl = (cl + c * (sat_[0][i] - cl)) * o, wr = (cr + c * (sat_[1][i] - cr)) * o;
            float yl = (dl + m * (wl - dl)) * t, yr = (dr + m * (wr - dr)) * t;
            hum_.tick(mains, 1.0f, yl, yr);
            L[i] = yl;
            R[i] = yr;
            vuOut_.tick(yl, yr);
        }
        publishReduction(most);
        publishMeter(0, vuIn_.vu());
        publishMeter(1, vuOut_.vu());
    }

private:
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, sat_;
    std::vector<float> col_;
    Delay2 dry_, clean_;
    hc::fx::DelayLine colD_;
    float env_ = 0.0f, gr_ = 0.0f, work_ = 0.0f, workK_ = 0.0f;
    Hum hum_;
    Vu vuIn_, vuOut_;
    AutoGain auto_;
    hc::fx::Smoothed in_, out_, mix_, trim_;
};

} // namespace
} // namespace quickdraw

std::unique_ptr<hc::fx::Effect> makeQuickDraw() { return std::make_unique<quickdraw::Impl>(); }

} // namespace hem::rack
