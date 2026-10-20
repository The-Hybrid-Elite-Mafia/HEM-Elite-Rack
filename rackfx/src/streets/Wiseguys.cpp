// WISEGUYS — harmonizer: three pitch-shifted voices over the dry sound (see Streets.hpp).
#include "hem/rack/Streets.hpp"

#include "../hideout/SpaceKit.hpp"

namespace hem::rack {
namespace wiseguys {
namespace {
using namespace hideout;

const ParamInfo kParams[kCount] = {
    {"interval1", "Voice 1 Interval", "st", -12.0f, 12.0f, 4.0f},
    {"level1", "Voice 1 Level", "%", 0.0f, 100.0f, 70.0f},
    {"pan1", "Voice 1 Pan", "%", -100.0f, 100.0f, -50.0f},
    {"interval2", "Voice 2 Interval", "st", -12.0f, 12.0f, 7.0f},
    {"level2", "Voice 2 Level", "%", 0.0f, 100.0f, 70.0f},
    {"pan2", "Voice 2 Pan", "%", -100.0f, 100.0f, 50.0f},
    {"interval3", "Voice 3 Interval", "st", -12.0f, 12.0f, -12.0f},
    {"level3", "Voice 3 Level", "%", 0.0f, 100.0f, 0.0f},
    {"pan3", "Voice 3 Pan", "%", -100.0f, 100.0f, 0.0f},
    {"dry", "Dry", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& v : voice_) v.prepare(fs, 45.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& v : voice_) v.clear();
        for (int k = 0; k < kVoices; ++k) {
            semis_[k].prepare(fs_, 40, std::round(p(Interval1 + 3 * k)));
            level_[k].prepare(fs_, 30, p(Level1 + 3 * k) * 0.01f);
            pan_[k].prepare(fs_, 30, p(Pan1 + 3 * k) * 0.01f);
        }
        dry_.prepare(fs_, 30, p(Dry) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = 0.5f * (L[i] + R[i]);
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kVoices; ++k) {
                const float st = semis_[k].next(std::round(p(Interval1 + 3 * k)));   // whole semitones: harmonies stay in tune
                const float y = voice_[k].tick(x, std::exp2(st / 12.0f)) * level_[k].next(p(Level1 + 3 * k) * 0.01f);
                const float a = (pan_[k].next(p(Pan1 + 3 * k) * 0.01f) + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
                wl += y * std::cos(a) * 1.4142f;
                wr += y * std::sin(a) * 1.4142f;
            }
            const float d = dry_.next(p(Dry) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] * d + wl) * o;
            R[i] = (R[i] * d + wr) * o;
        }
    }

private:
    double fs_ = 48000.0;
    PitchShift voice_[kVoices];
    hc::fx::Smoothed semis_[kVoices], level_[kVoices], pan_[kVoices], dry_, out_;
};

} // namespace
} // namespace wiseguys

std::unique_ptr<hc::fx::Effect> makeWiseguys() { return std::make_unique<wiseguys::Impl>(); }

} // namespace hem::rack
