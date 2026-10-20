// KINGPIN — drum channel: gate, thump, snap, punch, crush, drive (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace kingpin {
namespace {
using namespace tone;

const ParamInfo kParams[kCount] = {
    {"gate", "Gate", "dB", -80.0f, 0.0f, -80.0f},
    {"gaterelease", "Gate Release", "ms", 10.0f, 1000.0f, 100.0f, Taper::Log},
    {"thump", "Thump", "dB", 0.0f, 12.0f, 0.0f},
    {"thumpfreq", "Thump Freq", "Hz", 40.0f, 200.0f, 70.0f, Taper::Log},
    {"snap", "Snap", "dB", 0.0f, 12.0f, 0.0f},
    {"snapfreq", "Snap Freq", "Hz", 1500.0f, 10000.0f, 4000.0f, Taper::Log},
    {"punch", "Punch", "%", 0.0f, 100.0f, 0.0f},
    {"crush", "Crush", "%", 0.0f, 100.0f, 0.0f},
    {"drive", "Drive", "%", 0.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i == GateRelease) return p(Gate) > -79.5f;
        if (i == ThumpFreq) return p(Thump) > 0.0f;
        if (i == SnapFreq) return p(Snap) > 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        drive_.prepare();
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        key_ = {-1, -1, -1, -1};
        gate_.clear();
        fast_ = slow_ = crushEnv_ = crushGr_ = 0.0f;
        drive_.clear();
        drive_.mix = p(Drive) > 0.0f ? 1.0f : 0.0f;
        crush_.prepare(fs_, 30, p(Crush) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const bool gateOn = p(Gate) > -79.5f;
        const float gEnvRel = coefMs(fs_, 8.0), gAtt = coefMs(fs_, 0.2), gRel = coefMs(fs_, p(GateRelease));
        const auto holdN = static_cast<std::uint32_t>(0.02 * fs_);
        const float fAtt = coefMs(fs_, 0.5), fRel = coefMs(fs_, 40.0), sAtt = coefMs(fs_, 15.0);
        const float punch = p(Punch) * 0.01f;
        const float cAtt = coefMs(fs_, 1.0), cRel = coefMs(fs_, 80.0), cEnvRel = coefMs(fs_, 3.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            // gate
            const float x0 = std::max(std::fabs(l), std::fabs(r));
            const float gg = gate_.tick(x0, gateOn, p(Gate), -60.0f, holdN, gEnvRel, gAtt, gRel);
            most = std::min(most, gg);
            const float ga = hc::fx::dbToGain(gg);
            l *= ga;
            r *= ga;
            // thump and snap
            for (auto& b : eq_[0]) l = b.tick(l);
            for (auto& b : eq_[1]) r = b.tick(r);
            // punch: the hit (fast envelope over the slow one) comes up by as much as 9 dB
            const float x = std::max(std::fabs(l), std::fabs(r));
            fast_ = envStep(fast_, x, fAtt, fRel);
            slow_ = envStep(slow_, x, sAtt, fRel);
            const float hit = std::clamp((dbOf(fast_) - dbOf(slow_)) / 12.0f, 0.0f, 1.0f);
            const float pg = hc::fx::dbToGain(9.0f * punch * hit);
            l *= pg;
            r *= pg;
            // crush: a hard-squashed copy (10:1 from -35 dB, +18 dB back) mixed in under the drums
            const float xc = std::max(std::fabs(l), std::fabs(r));
            crushEnv_ = xc > crushEnv_ ? xc : xc + cEnvRel * (crushEnv_ - xc);
            crushGr_ = follow(crushGr_, downDb(dbOf(crushEnv_), -35.0f, 10.0f, 4.0f), cAtt, cRel);
            const float cm = crush_.next(p(Crush) * 0.01f) * 0.7f;
            const float cg = hc::fx::dbToGain(crushGr_ + 18.0f);
            L[i] = l + cm * l * cg;
            R[i] = r + cm * r * cg;
        }
        if (gate_.gain > -1e-6f) gate_.gain = 0.0f;
        if (crushGr_ > -1e-6f) crushGr_ = 0.0f;
        // drive: tape-style saturation, 4x oversampled
        const float d = p(Drive) * 0.01f, g = 1.0f + 6.0f * d;
        drive_.run(L, R, n, d > 0.0f, static_cast<float>(1.0 / (fs_ * 0.02)),
                   [d, g](float s) { return s + d * (softClip(s * g) / g - s); });
        for (std::uint32_t i = 0; i < n; ++i) {
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= o;
            R[i] *= o;
        }
        publishReduction(most);
    }

private:
    void design() noexcept {
        const std::array<float, 4> k{p(Thump), p(ThumpFreq), p(Snap), p(SnapFreq)};
        if (k == key_) return;
        key_ = k;
        const C thump = C::make(T::Bell, fs_, p(ThumpFreq), 1.2, p(Thump));
        const C snap = C::make(T::Bell, fs_, std::min<double>(p(SnapFreq), fs_ * 0.45), 0.8, p(Snap));
        for (auto& ch : eq_) { ch[0].c = thump; ch[1].c = snap; }
    }

    double fs_ = 48000.0;
    std::array<std::array<hc::fx::Biquad, 2>, 2> eq_{};
    std::array<float, 4> key_{};
    GateStage gate_;
    float fast_ = 0, slow_ = 0, crushEnv_ = 0, crushGr_ = 0;
    Drive4 drive_;
    hc::fx::Smoothed crush_, out_;
};

} // namespace
} // namespace kingpin

std::unique_ptr<hc::fx::Effect> makeKingpin() { return std::make_unique<kingpin::Impl>(); }

} // namespace hem::rack
