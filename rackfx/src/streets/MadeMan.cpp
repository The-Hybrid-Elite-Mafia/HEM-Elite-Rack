// MADE MAN — pitch correction for one voice (see Streets.hpp).
//
// The sung pitch is found (YIN, every 2.7 ms on the last 21 ms), the nearest note of the chosen
// key and scale is the target, and the correction glides there at SPEED. A short-grain pitch
// shifter applies it. When nobody sings (or the pitch is unsure) the correction lets go.
#include "hem/rack/Streets.hpp"

#include "../hideout/SpaceKit.hpp"
#include "PitchKit.hpp"

namespace hem::rack {
namespace mademan {
namespace {
using namespace streets;

const char* const kKeyN[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kScaleN[] = {"CHROMATIC", "MAJOR", "MINOR"};
const ParamInfo kParams[kCount] = {
    {"key", "Key", "", 0, 11, 0, Taper::Choice, kKeyN},
    {"scale", "Scale", "", 0, 2, 0, Taper::Choice, kScaleN},
    {"speed", "Speed", "ms", 0.0f, 400.0f, 40.0f},
    {"amount", "Amount", "%", 0.0f, 100.0f, 100.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"NOTE", "", -1.0f, 127.0f}, {"PULL", "cents", -100.0f, 100.0f}};
constexpr bool kMajor[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
constexpr bool kMinor[12] = {true, false, true, true, false, true, false, true, true, false, true, false};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    // the grains sit half a window late on average: hosts line the voice up with that, and the dry waits as long
    std::uint32_t latency() const noexcept override { return lat_; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        yin_.prepare(fs, 70.0, 512, 64);
        for (auto& s : shift_) s.prepare(fs, 25.0);
        lat_ = static_cast<std::uint32_t>(std::lround(1.0 + 0.5 * 0.025 * fs));
        dry_.prepare(lat_);
        reset();
    }
    void reset() noexcept override {
        yin_.clear();
        for (auto& s : shift_) s.clear();
        dry_.clear();
        want_ = 0.0f;
        corr_ = 0.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishMeter(0, -1.0f);
        publishMeter(1, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int key = std::clamp(static_cast<int>(std::lround(p(Key))), 0, 11);
        const int scale = std::clamp(static_cast<int>(std::lround(p(Scale))), 0, 2);
        const float amount = p(Amount) * 0.01f;
        const float k = p(Speed) < 0.5f ? 1.0f : 1.0f - std::exp(-1.0f / (p(Speed) * 0.001f * static_cast<float>(fs_)));
        const float letGo = 1.0f - std::exp(-1.0f / (0.08f * static_cast<float>(fs_)));
        for (std::uint32_t i = 0; i < n; ++i) {
            if (yin_.push(0.5f * (L[i] + R[i]))) {
                if (yin_.hz() > 0.0f && yin_.confidence() > 0.85f && yin_.levelDb() > -50.0f) {
                    const double m = midiOf(yin_.hz());
                    const int target = nearest(m, key, scale);
                    want_ = static_cast<float>((target - m) * 100.0) * amount;
                    voiced_ = true;
                    publishMeter(0, static_cast<float>(std::lround(m)));
                } else {
                    want_ = 0.0f;
                    voiced_ = false;
                    publishMeter(0, -1.0f);
                }
            }
            corr_ += (want_ - corr_) * (voiced_ ? k : letGo);
            const float ratio = std::exp2(corr_ / 1200.0f);
            const float m = mix_.next(p(Mix) * 0.01f);
            const float yl = shift_[0].tick(L[i], ratio), yr = shift_[1].tick(R[i], ratio);
            const float dl = dry_.l.process(L[i], lat_), dr = dry_.r.process(R[i], lat_);
            L[i] = dl + m * (yl - dl);
            R[i] = dr + m * (yr - dr);
        }
        publishMeter(1, corr_);
    }

private:
    // the nearest note (MIDI) the scale allows
    static int nearest(double midi, int key, int scale) noexcept {
        const int base = static_cast<int>(std::lround(midi));
        int best = base;
        double dist = 1e9;
        for (int d = -2; d <= 2; ++d) {
            const int c = base + d;
            const int pc = ((c - key) % 12 + 12) % 12;
            const bool ok = scale == 0 || (scale == 1 ? kMajor[pc] : kMinor[pc]);
            if (ok && std::fabs(c - midi) < dist) { dist = std::fabs(c - midi); best = c; }
        }
        return best;
    }

    double fs_ = 48000.0;
    Yin yin_;
    hideout::PitchShift shift_[2];
    hideout::Delay2 dry_;
    std::uint32_t lat_ = 601;
    float want_ = 0.0f, corr_ = 0.0f;
    bool voiced_ = false;
    hc::fx::Smoothed mix_;
};

} // namespace
} // namespace mademan

std::unique_ptr<hc::fx::Effect> makeMadeMan() { return std::make_unique<mademan::Impl>(); }

} // namespace hem::rack
