// THE SNITCH — tuner (see Streets.hpp).
#include "hem/rack/Streets.hpp"

#include "PitchKit.hpp"

namespace hem::rack {
namespace snitch {
namespace {
using namespace streets;

const ParamInfo kParams[kCount] = {
    {"reference", "Reference", "Hz", 430.0f, 450.0f, 440.0f},
    {"mute", "Mute", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
const hc::fx::MeterInfo kMeters[] = {{"NOTE", "", -1.0f, 127.0f}, {"CENTS", "cents", -50.0f, 50.0f}, {"FREQUENCY", "Hz", 0.0f, 2000.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        yin_.prepare(fs, 38.0, 1024, 256);   // down to below a bass's low E
        reset();
    }
    void reset() noexcept override {
        yin_.clear();
        held_ = 0;
        cents_ = 0.0f;
        mute_.prepare(fs_, 10, p(Mute) >= 0.5f ? 0.0f : 1.0f);
        show(-1.0f, 0.0f, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t i = 0; i < n; ++i) {
            if (yin_.push(0.5f * (L[i] + R[i]))) {
                if (yin_.hz() > 0.0f && yin_.confidence() > 0.8f) {
                    const double m = midiOf(yin_.hz(), p(Reference));
                    const double note = std::round(m);
                    const float c = static_cast<float>((m - note) * 100.0);
                    cents_ = held_ > 0 ? cents_ + 0.35f * (c - cents_) : c;   // a calm needle
                    held_ = 40;
                    show(static_cast<float>(note), cents_, yin_.hz());
                } else if (held_ > 0 && --held_ == 0) {
                    show(-1.0f, 0.0f, 0.0f);   // the note faded: clear after a moment
                }
            }
            const float g = mute_.next(p(Mute) >= 0.5f ? 0.0f : 1.0f);
            L[i] *= g;
            R[i] *= g;
        }
    }

private:
    void show(float note, float cents, float hz) noexcept {
        publishMeter(0, note);
        publishMeter(1, cents);
        publishMeter(2, hz);
    }

    double fs_ = 48000.0;
    Yin yin_;
    int held_ = 0;
    float cents_ = 0.0f;
    hc::fx::Smoothed mute_;
};

} // namespace
} // namespace snitch

std::unique_ptr<hc::fx::Effect> makeSnitch() { return std::make_unique<snitch::Impl>(); }

} // namespace hem::rack
