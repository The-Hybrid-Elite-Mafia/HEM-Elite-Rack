// HEAVYWEIGHT — punchy compressor with an analog stage (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

#include <vector>

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace heavyweight;

const char* const kSyncN[] = {"MS", "HOST", "BPM"};
const char* const kNoteN[] = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "BAR", "2 BARS"};
const char* const kAnalogN[] = {"OFF", "1", "2", "3", "4"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -48.0f, 0.0f, -18.0f},
    {"ratio", "Ratio", ":1", 1.0f, 50.0f, 3.0f, Taper::Log},
    {"attack", "Attack", "ms", 0.5f, 100.0f, 7.0f, Taper::Log},
    {"punch", "Snap", "", 0.0f, 30.0f, 1.0f},
    {"release", "Release", "ms", 3.0f, 3000.0f, 100.0f, Taper::Log},
    {"sync", "Release Sync", "", 0, 2, 0, Taper::Choice, kSyncN},
    {"note", "Release Note", "", 0, 7, 3, Taper::Choice, kNoteN},
    {"bpm", "BPM", "BPM", 30.0f, 300.0f, 120.0f},
    {"analog", "Vintage", "", 0, 4, 2, Taper::Choice, kAnalogN},
    {"limiter", "Limiter", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"meter", "Meter", "", 0, 2, 1, Taper::Choice, kMeterN},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};
// ANALOG 1..4: drive into the stage, how much is even (tube / transformer) vs odd (transistor), how
// much of the shaped sound is mixed in, hiss
struct Flavor { float drive, even, odd, amount; bool hiss; };
constexpr Flavor kFlavor[5] = {{1, 0, 0, 0, false}, {1.2f, 0.25f, 0.0f, 0.25f, true}, {1.3f, 0.0f, 0.6f, 0.3f, true},
                               {2.0f, 0.12f, 0.4f, 0.5f, true}, {2.4f, 0.05f, 0.7f, 0.6f, false}};

// A soft ceiling at -0.3 dBFS: untouched below 70 % of it, rounded into it above.
inline float ceilingSoft(float y) noexcept {
    constexpr float c = 0.966f, k = 0.7f * c;
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
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        const int sync = static_cast<int>(std::lround(p(Sync)));
        if (i == Release) return sync == 0;
        if (i == Note) return sync != 0;
        if (i == Bpm) return sync == 2;
        return true;
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        dry_.prepare(OS::latency());
        clean_.prepare(OS::latency());
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        dry_.clear();
        clean_.clear();
        env_ = fastE_ = slowE_ = 0.0f;
        gr_ = 0.0f;
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        flavor_ = std::clamp(static_cast<int>(std::lround(p(Analog))), 0, 4);
        analogMix_ = flavor_ > 0 ? 1.0f : 0.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold), ratio = p(Ratio);
        const float att = coefMs(fs_, p(Attack));
        const int sync = std::clamp(static_cast<int>(std::lround(p(Sync))), 0, 2);
        const int note = std::clamp(static_cast<int>(std::lround(p(Note))), 0, 7);
        const double bpm = sync == 2 ? p(Bpm) : tempo();
        const float rel = coefMs(fs_, sync > 0 ? std::clamp(beatsMs(kNoteBeats[note], bpm), 3.0, 3000.0) : p(Release));
        const float envRel = coefMs(fs_, 2.0);
        const float fAtt = coefMs(fs_, 0.1), fRel = coefMs(fs_, 30.0), sAtt = coefMs(fs_, 20.0);
        const float punch = p(Punch) / 30.0f;
        const float makeupDb = -0.5f * downDb(0.0f, thr, ratio, 6.0f);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            vuIn_.tick(L[i], R[i]);
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float target = downDb(dbOf(env_), thr, ratio, 6.0f);
            gr_ = follow(gr_, target, att, rel);
            // PUNCH: while a hit is rising (fast envelope well over the slow one) the gain reduction eases off
            fastE_ = x + (x > fastE_ ? fAtt : fRel) * (fastE_ - x);
            slowE_ = x + (x > slowE_ ? sAtt : fRel) * (slowE_ - x);
            const float hit = std::clamp((dbOf(fastE_) - dbOf(slowE_)) / 9.0f, 0.0f, 1.0f);
            const float applied = gr_ * (1.0f - punch * hit);
            most = std::min(most, applied);
            const float g = hc::fx::dbToGain(applied + makeupDb);
            wet_[0][i] = L[i] * g;
            wet_[1][i] = R[i] * g;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        // the analog stage, 4x oversampled; switching it cross-fades with the clean (equally delayed) path
        const int fl = std::clamp(static_cast<int>(std::lround(p(Analog))), 0, 4);
        if (fl > 0) flavor_ = fl;   // the last flavour stays while ANALOG OFF fades it out
        const Flavor& f = kFlavor[flavor_];
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            if (flavor_ > 0)
                for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) {
                    const float v = x4[j] * f.drive;
                    const float shaped = ((1.0f - f.odd) * tubeish(v, f.even) + f.odd * softClip(v)) / f.drive;
                    x4[j] += f.amount * (shaped - x4[j]);
                }
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        const bool limit = p(Limiter) >= 0.5f;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        for (std::uint32_t i = 0; i < n; ++i) {
            analogMix_ = fl > 0 ? std::min(1.0f, analogMix_ + step) : std::max(0.0f, analogMix_ - step);
            const float cl = clean_.l.process(wet_[0][i], OS::latency()), cr = clean_.r.process(wet_[1][i], OS::latency());
            float yl = cl + analogMix_ * (sat_[0][i] - cl), yr = cr + analogMix_ * (sat_[1][i] - cr);
            const float dl = dry_.l.process(L[i], OS::latency()), dr = dry_.r.process(R[i], OS::latency());
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            yl = (dl + m * (yl - dl)) * o;
            yr = (dr + m * (yr - dr)) * o;
            if (fl > 0 && f.hiss) hum_.tick(3, analogMix_, yl, yr);
            // LIMITER on: rounded into -0.3 dBFS; off: a plain clipper at 0 dBFS
            if (limit) { yl = ceilingSoft(yl); yr = ceilingSoft(yr); }
            else { yl = std::clamp(yl, -1.0f, 1.0f); yr = std::clamp(yr, -1.0f, 1.0f); }
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
    Delay2 dry_, clean_;
    float env_ = 0, fastE_ = 0, slowE_ = 0, gr_ = 0, analogMix_ = 0;
    int flavor_ = 0;
    Hum hum_;
    Vu vuIn_, vuOut_;
    hc::fx::Smoothed mix_, out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeHeavyweight() { return std::make_unique<Impl>(); }

} // namespace hem::rack
