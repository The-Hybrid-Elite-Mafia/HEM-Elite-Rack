// BODY DOUBLE — doubler: 2 or 4 late, slightly detuned voices spread left / right (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace bodydouble {
namespace {
using namespace hideout;

const char* const kVoicesN[] = {"2", "4"};
const ParamInfo kParams[kCount] = {
    {"voices", "Voices", "", 0, 1, 0, Taper::Choice, kVoicesN},
    {"delay", "Delay", "ms", 5.0f, 60.0f, 18.0f, Taper::Log},
    {"detune", "Detune", "cents", 0.0f, 30.0f, 8.0f},
    {"spread", "Spread", "%", 0.0f, 100.0f, 80.0f},
    {"voicelevel", "Voice Level", "dB", -24.0f, 0.0f, -6.0f},
    {"lowcut", "Low Cut", "Hz", 20.0f, 500.0f, 120.0f, Taper::Log},
    {"dry", "Dry", "dB", -24.0f, 0.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr int kMaxVoices = 4;
// each voice: how late (x DELAY), how far out of tune (x DETUNE, signed), where (x SPREAD), drift rate (Hz)
constexpr float kLate[kMaxVoices] = {1.0f, 1.37f, 0.71f, 1.83f};
constexpr float kTune[kMaxVoices] = {1.0f, -1.0f, 0.6f, -0.6f};
constexpr float kPan[kMaxVoices] = {-1.0f, 1.0f, -0.5f, 0.5f};
constexpr double kDrift[kMaxVoices] = {0.21, 0.17, 0.26, 0.13};
constexpr double kMaxDelayMs = 60.0 * 1.83 + 5.0;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : late_) d.prepare(static_cast<std::uint32_t>(kMaxDelayMs * 0.001 * fs) + 8);
        for (auto& s : shift_) s.prepare(fs, 30.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : late_) d.clear();
        for (auto& s : shift_) s.clear();
        for (auto& b : cut_) b.clear();
        cutHz_ = -1.0f;
        for (int v = 0; v < kMaxVoices; ++v) drift_[v] = 0.25 * v;
        delay_.prepare(fs_, 80, p(Delay));
        voices_.prepare(fs_, 30, hc::fx::dbToGain(p(VoiceLevel)));
        four_.prepare(fs_, 30, p(Voices) >= 0.5f ? 1.0f : 0.0f);
        dry_.prepare(fs_, 30, hc::fx::dbToGain(p(Dry)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float spread = p(Spread) * 0.01f, det = p(Detune);
        float panL[kMaxVoices], panR[kMaxVoices];
        for (int v = 0; v < kMaxVoices; ++v) {
            const float a = (kPan[v] * spread + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
            panL[v] = std::cos(a);
            panR[v] = std::sin(a);
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = cut_[0].tick(cut_[1].tick(0.5f * (L[i] + R[i])));
            const float dms = delay_.next(p(Delay));
            const float four = four_.next(p(Voices) >= 0.5f ? 1.0f : 0.0f);
            float wl = 0.0f, wr = 0.0f;
            for (int v = 0; v < kMaxVoices; ++v) {
                late_[v].write(x);
                drift_[v] += kDrift[v] / fs_;
                if (drift_[v] >= 1.0) drift_[v] -= 1.0;
                // the tuning breathes a little (+-20 %), so the voices never lock together
                const float cents = det * kTune[v] * (1.0f + 0.2f * static_cast<float>(std::sin(2.0 * std::numbers::pi * drift_[v])));
                const float y = shift_[v].tick(late_[v].read(dms * kLate[v] * 0.001f * static_cast<float>(fs_)), std::exp2(cents / 1200.0f));
                const float w = v < 2 ? 1.0f : four;
                wl += y * panL[v] * w;
                wr += y * panR[v] * w;
            }
            const float vg = voices_.next(hc::fx::dbToGain(p(VoiceLevel))), dg = dry_.next(hc::fx::dbToGain(p(Dry)));
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] * dg + wl * vg) * o;
            R[i] = (R[i] * dg + wr * vg) * o;
        }
    }

private:
    void design() noexcept {
        if (p(LowCut) == cutHz_) return;
        cutHz_ = p(LowCut);
        cut_[0].c = C::make(T::HighPass, fs_, cutHz_, 0.5412);
        cut_[1].c = C::make(T::HighPass, fs_, cutHz_, 1.3066);
    }

    double fs_ = 48000.0;
    hc::fx::DelayLine late_[kMaxVoices];
    PitchShift shift_[kMaxVoices];
    hc::fx::Biquad cut_[2];
    float cutHz_ = -1.0f;
    double drift_[kMaxVoices]{};
    hc::fx::Smoothed delay_, voices_, four_, dry_, out_;
};

} // namespace
} // namespace bodydouble

std::unique_ptr<hc::fx::Effect> makeBodyDouble() { return std::make_unique<bodydouble::Impl>(); }

} // namespace hem::rack
