// HOTHEAD — one-knob overdrive (see Garage.hpp).
//
// In order: a pedal's input coupling (a gentle low cut that rises with DRIVE), a mid push around 720 Hz,
// the clipping (lopsided like a pair of mismatched diodes, 4x oversampled: only what it adds goes through the
// oversampler), a warmth filter that closes from 20 kHz to 3.5 kHz, and the level made up so turning DRIVE
// doesn't simply make it louder. At DRIVE 0 every stage is flat and the clipping is faded out: the sound comes
// through untouched, 23 samples late.
#include "hem/rack/Garage.hpp"

#include "EqKit.hpp"
#include "ToneCommon.hpp"

namespace hem::rack {
namespace hothead {
namespace {
using namespace arsenal;
using eqkit::BandSet;
using eqkit::Shape;

const ParamInfo kParams[kCount] = {
    {"drive", "Drive", "", 0.0f, 10.0f, 0.0f},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        clip_.prepare();
        pre_.prepare(fs);
        post_.prepare(fs);
        fade_ = static_cast<float>(1.0 / (0.03 * fs));
        reset();
    }
    void reset() noexcept override {
        load();
        pre_.clear();
        post_.clear();
        clip_.clear(p(Drive) > 0.0f);
        drive_.prepare(fs_, 30, gainFor(p(Drive)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        load();
        const float d = p(Drive), wG = gainFor(d);
        for (std::uint32_t i = 0; i < n; ++i) {
            pre_.step();
            L[i] = pre_.run(0, L[i]);
            R[i] = pre_.run(1, R[i]);
        }
        // the clipping: DRIVE pushes the sound into it, then it is brought down again so a typical level
        // (about -12 dBFS) comes out about where it went in
        for (std::uint32_t k = 0; k < n; k += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - k);
            for (std::uint32_t i = 0; i + 1 < m; ++i) (void)drive_.next(wG);
            const float g = drive_.next(wG), mk = 0.25f + 0.75f / g;
            clip_.run(L + k, R + k, m, d > 0.0f, fade_, [g, mk](float x) {
                const float u = g * x;
                return mk * (u >= 0.0f ? std::tanh(u) : 0.85f * std::tanh(u / 0.85f));
            });
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            post_.step();
            L[i] = post_.run(0, L[i]);
            R[i] = post_.run(1, R[i]);
        }
    }

private:
    static constexpr std::uint32_t kChunk = 64;
    // DRIVE 0..10: the gain into the clipping (1x .. about 60x)
    static float gainFor(float d) noexcept { return hc::fx::dbToGain(3.6f * d); }
    void load() noexcept {
        const float d = p(Drive), t = d / 10.0f;
        if (d == key_) return;
        key_ = d;
        pre_.set(0, d > 0.0f ? BandSet{Shape::LowCut, 20.0f + 100.0f * t, 0.0f, 0.7071f} : BandSet{});
        pre_.set(1, d > 0.0f ? BandSet{Shape::Bell, 720.0f, 6.0f * t, 0.8f} : BandSet{});
        post_.set(0, d > 0.0f ? BandSet{Shape::HighCut, static_cast<float>(20000.0 * std::pow(3500.0 / 20000.0, t)), 0.0f, 0.7071f} : BandSet{});
    }

    double fs_ = 48000.0;
    tone::Drive4 clip_;
    eqkit::IirBands<2> pre_;
    eqkit::IirBands<1> post_;
    hc::fx::Smoothed drive_;
    float fade_ = 0.001f, key_ = -1.0f;
};

} // namespace
} // namespace hothead

std::unique_ptr<hc::fx::Effect> makeHothead() { return std::make_unique<hothead::Impl>(); }

} // namespace hem::rack
