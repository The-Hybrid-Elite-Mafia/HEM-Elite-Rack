// PROMOTION — pitch shifter (see Streets.hpp).
#include "hem/rack/Streets.hpp"

#include "../hideout/SpaceKit.hpp"

namespace hem::rack {
namespace promotion {
namespace {
using namespace hideout;

const char* const kCharN[] = {"SMOOTH", "TIGHT"};
const ParamInfo kParams[kCount] = {
    {"shift", "Shift", "st", -12.0f, 12.0f, 0.0f},
    {"fine", "Fine", "cents", -100.0f, 100.0f, 0.0f},
    {"character", "Character", "", 0, 1, 0, Taper::Choice, kCharN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    // the grains sit half a SMOOTH window late on average; TIGHT is delayed to match, and so is the dry
    std::uint32_t latency() const noexcept override { return lat_; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (int c = 0; c < 2; ++c) {
            smooth_[c].prepare(fs, 70.0);
            tight_[c].prepare(fs, 28.0);
        }
        lat_ = static_cast<std::uint32_t>(std::lround(1.0 + 0.5 * 0.070 * fs));
        extra_ = static_cast<float>(0.5 * (0.070 - 0.028) * fs);
        for (auto& d : late_) d.prepare(static_cast<std::uint32_t>(extra_) + 8);
        dry_.prepare(lat_);
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) { smooth_[c].clear(); tight_[c].clear(); late_[c].clear(); }
        dry_.clear();
        cents_.prepare(fs_, 40, target());
        which_.prepare(fs_, 30, p(Character) >= 0.5f ? 1.0f : 0.0f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float ratio = std::exp2(cents_.next(target()) / 1200.0f);
            const float t = which_.next(p(Character) >= 0.5f ? 1.0f : 0.0f);
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                const float a = smooth_[c].tick(x, ratio);
                late_[c].write(tight_[c].tick(x, ratio));
                const float b = late_[c].read(extra_);
                const float y = a + t * (b - a);
                const float d = c == 0 ? dry_.l.process(x, lat_) : dry_.r.process(x, lat_);
                io[c][i] = (d + m * (y - d)) * o;
            }
        }
    }

private:
    float target() const noexcept { return p(Shift) * 100.0f + p(Fine); }

    double fs_ = 48000.0;
    PitchShift smooth_[2], tight_[2];
    hc::fx::DelayLine late_[2];
    Delay2 dry_;
    std::uint32_t lat_ = 1681;
    float extra_ = 1008.0f;
    hc::fx::Smoothed cents_, which_, mix_, out_;
};

} // namespace
} // namespace promotion

std::unique_ptr<hc::fx::Effect> makePromotion() { return std::make_unique<promotion::Impl>(); }

} // namespace hem::rack
