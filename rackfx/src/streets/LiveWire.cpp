// LIVE WIRE — signal generator (see Streets.hpp).
//
// ON fades the input out and the signal in (and back). PINK is white noise through Paul Kellet's filter;
// both noises are kept to 20 Hz .. 21 kHz and scaled to the RMS of a sine at the same GAIN.
#include "hem/rack/Streets.hpp"

#include "../arsenal/Common.hpp"

namespace hem::rack {
namespace livewire {
namespace {
using namespace arsenal;

const char* const kTypeN[] = {"PINK", "WHITE", "SINE"};
const char* const kQuickFN[] = {"KNOB", "100 HZ", "1 KHZ", "10 KHZ"};
const char* const kQuickGN[] = {"KNOB", "-6", "-12", "-20"};
const char* const kSendN[] = {"L", "R", "L+R"};
const ParamInfo kParams[kCount] = {
    {"on", "On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"type", "Type", "", 0, 2, Pink, Taper::Choice, kTypeN},
    {"freq", "Freq", "Hz", 20.0f, 21000.0f, 1000.0f, Taper::Log},
    {"quickfreq", "Quick Freq", "", 0, 3, 0, Taper::Choice, kQuickFN},
    {"gain", "Gain", "dB", -120.0f, 0.0f, -20.0f},
    {"quickgain", "Quick Gain", "", 0, 3, 0, Taper::Choice, kQuickGN},
    {"send", "Send", "", 0, 2, 2, Taper::Choice, kSendN},
    {"flip", "Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
constexpr double kQuickHz[] = {0.0, 100.0, 1000.0, 10000.0};
constexpr float kQuickDb[] = {0.0f, -6.0f, -12.0f, -20.0f};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        if (i == Freq) return type() == Sine && quickF() == 0;
        if (i == QuickFreq) return type() == Sine;
        if (i == Gain) return quickG() == 0;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const C hp = C::make(T::HighPass, fs, 20.0, 0.7071), lp = C::make(T::LowPass, fs, std::min(21000.0, fs * 0.45), 0.7071);
        for (auto& b : band_) { b[0].c = hp; b[1].c = lp; }
        reset();
    }
    void reset() noexcept override {
        phase_ = 0.0;
        rng_ = 12345u;
        for (auto& b : pink_) b = 0.0f;
        for (auto& b : band_) { b[0].clear(); b[1].clear(); }
        on_.prepare(fs_, 20, p(On) >= 0.5f ? 1.0f : 0.0f);
        level_.prepare(fs_, 30, gain());
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int t = type(), send = std::clamp(static_cast<int>(std::lround(p(Send))), 0, 2);
        const double hz = std::min(quickF() > 0 ? kQuickHz[quickF()] : static_cast<double>(p(Freq)), fs_ * 0.45);
        const bool flip = p(Flip) >= 0.5f;
        const float wGain = gain();
        for (std::uint32_t i = 0; i < n; ++i) {
            const bool wanted = p(On) >= 0.5f;
            float on = on_.next(wanted ? 1.0f : 0.0f);
            if (!wanted && on < 1e-5f) on = on_.cur = 0.0f;
            if (on <= 0.0f) continue;   // off: the sound passes untouched
            float s;
            if (t == Sine) {
                phase_ += hz / fs_;
                if (phase_ >= 1.0) phase_ -= 1.0;
                s = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_));
            } else {
                const float w = white();
                float v = w;
                if (t == Pink) {
                    pink_[0] = 0.99886f * pink_[0] + w * 0.0555179f;
                    pink_[1] = 0.99332f * pink_[1] + w * 0.0750759f;
                    pink_[2] = 0.96900f * pink_[2] + w * 0.1538520f;
                    pink_[3] = 0.86650f * pink_[3] + w * 0.3104856f;
                    pink_[4] = 0.55000f * pink_[4] + w * 0.5329522f;
                    pink_[5] = -0.7616f * pink_[5] - w * 0.0168980f;
                    v = (pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362f) * kPinkScale;
                    pink_[6] = w * 0.115926f;
                } else {
                    v *= kWhiteScale;
                }
                s = band_[0][1].tick(band_[0][0].tick(v));
            }
            s *= level_.next(wGain);
            const float sl = send == 1 ? 0.0f : s, sr = send == 0 ? 0.0f : (flip ? -s : s);
            L[i] = L[i] * (1.0f - on) + sl * on;
            R[i] = R[i] * (1.0f - on) + sr * on;
        }
    }

private:
    // scaled (measured, after the 20 Hz .. 21 kHz band) so the noise has the RMS of a sine at the same GAIN
    static constexpr float kWhiteScale = 1.3150f, kPinkScale = 0.4660f;
    int type() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Type))), 0, 2); }
    int quickF() const noexcept { return std::clamp(static_cast<int>(std::lround(p(QuickFreq))), 0, 3); }
    int quickG() const noexcept { return std::clamp(static_cast<int>(std::lround(p(QuickGain))), 0, 3); }
    float gain() const noexcept { return hc::fx::dbToGain(quickG() > 0 ? kQuickDb[quickG()] : p(Gain)); }
    float white() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(rng_)) * (1.0f / 2147483648.0f);
    }

    double fs_ = 48000.0;
    double phase_ = 0.0;
    std::uint32_t rng_ = 12345u;
    float pink_[7]{};
    std::array<std::array<hc::fx::Biquad, 2>, 1> band_{};
    hc::fx::Smoothed on_, level_;
};

} // namespace
} // namespace livewire

std::unique_ptr<hc::fx::Effect> makeLiveWire() { return std::make_unique<livewire::Impl>(); }

} // namespace hem::rack
