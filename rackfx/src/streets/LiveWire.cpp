// LIVE WIRE — test tones (see Streets.hpp).
#include "hem/rack/Streets.hpp"

#include "../arsenal/Common.hpp"

namespace hem::rack {
namespace livewire {
namespace {
using namespace arsenal;

const char* const kWaveN[] = {"SINE", "SQUARE", "SAW", "TRI", "WHITE", "PINK", "SWEEP"};
const char* const kModeN[] = {"REPLACE", "ADD"};
const char* const kChannelsN[] = {"BOTH", "LEFT", "RIGHT", "OPPOSED"};
const ParamInfo kParams[kCount] = {
    {"on", "On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"wave", "Wave", "", 0, 6, 0, Taper::Choice, kWaveN},
    {"freq", "Freq", "Hz", 20.0f, 20000.0f, 1000.0f, Taper::Log},
    {"level", "Level", "dBFS", -60.0f, 0.0f, -18.0f},
    {"sweeptime", "Sweep Time", "s", 1.0f, 30.0f, 10.0f, Taper::Log},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"channels", "Channels", "", 0, 3, 0, Taper::Choice, kChannelsN},
};

float blep(float t, float dt) noexcept {
    if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        const int w = static_cast<int>(std::lround(p(Wave)));
        if (i == Freq) return w <= 3;
        if (i == SweepTime) return w == 6;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        phase_ = 0.0;
        sweep_ = 0.0;
        rng_ = 12345u;
        for (auto& b : pink_) b = 0.0f;
        on_.prepare(fs_, 20, p(On) >= 0.5f ? 1.0f : 0.0f);
        level_.prepare(fs_, 30, hc::fx::dbToGain(p(Level)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int wave = std::clamp(static_cast<int>(std::lround(p(Wave))), 0, 6);
        const bool add = p(Mode) >= 0.5f;
        const int ch = std::clamp(static_cast<int>(std::lround(p(Channels))), 0, 3);
        const double f = std::min<double>(p(Freq), fs_ * 0.45);
        const double sweepStep = 1.0 / (p(SweepTime) * fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const bool wanted = p(On) >= 0.5f;
            float on = on_.next(wanted ? 1.0f : 0.0f);
            if (!wanted && on < 1e-5f) on = on_.cur = 0.0f;
            if (on <= 0.0f) continue;   // off: the sound passes untouched
            float s = 0.0f;
            double hz = f;
            if (wave == 6) {   // a log sweep 20 Hz .. 20 kHz, over and over
                sweep_ += sweepStep;
                if (sweep_ >= 1.0) sweep_ -= 1.0;
                hz = std::min(20.0 * std::pow(1000.0, sweep_), fs_ * 0.45);
            }
            const float dt = static_cast<float>(hz / fs_);
            phase_ += dt;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const float t = static_cast<float>(phase_);
            switch (wave) {
            case 0: case 6: s = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_)); break;
            case 1: {
                float t2 = t + 0.5f;
                if (t2 >= 1.0f) t2 -= 1.0f;
                s = (2.0f * t - 1.0f - blep(t, dt)) - (2.0f * t2 - 1.0f - blep(t2, dt));
                break;
            }
            case 2: s = 2.0f * t - 1.0f - blep(t, dt); break;
            case 3: s = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; break;
            case 4: s = white(); break;
            case 5: {   // pink: white through Paul Kellet's filter
                const float w = white();
                pink_[0] = 0.99886f * pink_[0] + w * 0.0555179f;
                pink_[1] = 0.99332f * pink_[1] + w * 0.0750759f;
                pink_[2] = 0.96900f * pink_[2] + w * 0.1538520f;
                pink_[3] = 0.86650f * pink_[3] + w * 0.3104856f;
                pink_[4] = 0.55000f * pink_[4] + w * 0.5329522f;
                pink_[5] = -0.7616f * pink_[5] - w * 0.0168980f;
                s = (pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362f) * 0.2f;
                pink_[6] = w * 0.115926f;
                break;
            }
            default: break;
            }
            s *= level_.next(hc::fx::dbToGain(p(Level)));
            const float sl = ch == 2 ? 0.0f : s, sr = ch == 1 ? 0.0f : ch == 3 ? -s : s;
            // REPLACE: fade the input out as the tone comes in; ADD: on top
            const float keep = add ? 1.0f : 1.0f - on;
            L[i] = L[i] * keep + sl * on;
            R[i] = R[i] * keep + sr * on;
        }
    }

private:
    float white() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(rng_)) * (1.0f / 2147483648.0f);
    }

    double fs_ = 48000.0;
    double phase_ = 0.0, sweep_ = 0.0;
    std::uint32_t rng_ = 12345u;
    float pink_[7]{};
    hc::fx::Smoothed on_, level_;
};

} // namespace
} // namespace livewire

std::unique_ptr<hc::fx::Effect> makeLiveWire() { return std::make_unique<livewire::Impl>(); }

} // namespace hem::rack
