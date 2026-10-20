// HEAVYWEIGHT — punchy compressor with an analog stage (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

#include <vector>

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace heavyweight;

const char* const kSyncN[] = {"OFF", "1/32", "1/16", "1/8", "1/4", "1/2"};
const char* const kSourceN[] = {"HOST", "BPM"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -48.0f, 0.0f, -18.0f},
    {"ratio", "Ratio", ":1", 1.0f, 50.0f, 4.0f, Taper::Log},
    {"attack", "Attack", "ms", 0.5f, 100.0f, 10.0f, Taper::Log},
    {"release", "Release", "ms", 5.0f, 2000.0f, 120.0f, Taper::Log},
    {"sync", "Release Sync", "", 0, 5, 0, Taper::Choice, kSyncN},
    {"punch", "Punch", "%", 0.0f, 100.0f, 0.0f},
    {"analog", "Analog", "%", 0.0f, 100.0f, 0.0f},
    {"limit", "Limit", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"bpm", "BPM", "", 60.0f, 200.0f, 120.0f},
    {"syncsource", "Tempo From", "", 0, 1, 0, Taper::Choice, kSourceN},
};

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
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        const bool synced = p(Sync) >= 0.5f;
        if (i == Release) return !synced;
        if (i == SyncSource) return synced;
        if (i == Bpm) return synced && p(SyncSource) >= 0.5f;
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
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        dry_.clear();
        clean_.clear();
        env_ = fastE_ = slowE_ = 0.0f;
        gr_ = 0.0f;
        analogMix_ = p(Analog) > 0.0f ? 1.0f : 0.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        drive_.prepare(fs_, 30, p(Analog) * 0.01f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold), ratio = p(Ratio);
        const float att = coefMs(fs_, p(Attack));
        const int sync = std::clamp(static_cast<int>(std::lround(p(Sync))), 0, 5);
        const double bpm = p(SyncSource) >= 0.5f ? p(Bpm) : tempo();
        const float rel = coefMs(fs_, sync > 0 ? beatsMs(kNoteBeats[sync], bpm) : p(Release));
        const float envRel = coefMs(fs_, 2.0);
        const float fAtt = coefMs(fs_, 0.1), fRel = coefMs(fs_, 30.0), sAtt = coefMs(fs_, 20.0);
        const float punch = p(Punch) * 0.01f;
        const float makeupDb = -0.5f * downDb(0.0f, thr, ratio, 6.0f);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
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
        // the analog stage, 4x oversampled; at ANALOG 0 it fades out to the clean (equally delayed) path
        const float a = p(Analog) * 0.01f;
        const float driveStart = drive_.cur;
        for (int c = 0; c < 2; ++c) {
            drive_.cur = driveStart;   // the same drive ramp for both channels
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) {
                const float d = drive_.next(a) * 3.0f + 1.0f;
                x4[j] = tubeish(x4[j] * d, 0.12f) / d;
            }
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        const bool limit = p(Limit) >= 0.5f;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        for (std::uint32_t i = 0; i < n; ++i) {
            analogMix_ = a > 0.0f ? std::min(1.0f, analogMix_ + step) : std::max(0.0f, analogMix_ - step);
            const float cl = clean_.l.process(wet_[0][i], OS::latency()), cr = clean_.r.process(wet_[1][i], OS::latency());
            float yl = cl + analogMix_ * (sat_[0][i] - cl), yr = cr + analogMix_ * (sat_[1][i] - cr);
            const float dl = dry_.l.process(L[i], OS::latency()), dr = dry_.r.process(R[i], OS::latency());
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            yl = (dl + m * (yl - dl)) * o;
            yr = (dr + m * (yr - dr)) * o;
            if (limit) { yl = ceilingSoft(yl); yr = ceilingSoft(yr); }
            L[i] = yl;
            R[i] = yr;
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, sat_;
    Delay2 dry_, clean_;
    float env_ = 0, fastE_ = 0, slowE_ = 0, gr_ = 0, analogMix_ = 0;
    hc::fx::Smoothed mix_, out_, drive_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeHeavyweight() { return std::make_unique<Impl>(); }

} // namespace hem::rack
