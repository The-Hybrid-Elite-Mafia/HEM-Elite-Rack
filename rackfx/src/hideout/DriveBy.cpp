// DRIVE-BY — a sound passing by: pitch bend, level, pan and air (see Hideout.hpp).
//
// The source moves along a straight line that passes DISTANCE metres in front of the listener.
// What the listener hears left the source r / c seconds ago (c = 343 m/s): reading a delay line at
// that changing delay bends the pitch by itself. Level falls as DISTANCE / r, the pan follows
// the angle, and far away the highs fade (AIR).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace driveby {
namespace {
using namespace hideout;

const char* const kModeN[] = {"MANUAL", "AUTO"};
const char* const kSourceN[] = {"HOST", "BPM"};
const ParamInfo kParams[kCount] = {
    {"mode", "Mode", "", 0, 1, 1, Taper::Choice, kModeN},
    {"position", "Position", "%", -100.0f, 100.0f, 0.0f},
    {"rate", "Pass Every", "", 0, 7, 7, Taper::Choice, kNoteN},
    {"syncsource", "Tempo From", "", 0, 1, 0, Taper::Choice, kSourceN},
    {"bpm", "BPM", "", 40.0f, 240.0f, 120.0f},
    {"speed", "Speed", "m/s", 5.0f, 100.0f, 30.0f, Taper::Log},
    {"distance", "Distance", "m", 1.0f, 30.0f, 5.0f, Taper::Log},
    {"air", "Air", "%", 0.0f, 100.0f, 50.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kC = 343.0, kMaxX = 150.0, kManualX = 60.0;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        const bool autoMode = p(Mode) >= 0.5f;
        if (i == Position) return !autoMode;
        if (i == Rate || i == SyncSource || i == Speed) return autoMode;
        if (i == Bpm) return autoMode && p(SyncSource) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const double rMax = std::sqrt(kMaxX * kMaxX + 30.0 * 30.0);
        line_.prepare(static_cast<std::uint32_t>(rMax / kC * fs) + 16);
        reset();
    }
    void reset() noexcept override {
        line_.clear();
        air_[0] = air_[1] = 0.0f;
        phase_ = 0.0;
        pos_.prepare(fs_, 60, p(Position) * 0.01f);
        dist_.prepare(fs_, 60, p(Distance));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const bool autoMode = p(Mode) >= 0.5f;
        const int ri = std::clamp(static_cast<int>(std::lround(p(Rate))), 0, 7);
        const double periodS = beatsMs(kNoteBeats[ri], p(SyncSource) >= 0.5f ? p(Bpm) : tempo()) * 0.001;
        const double halfX = std::min(kMaxX, p(Speed) * periodS * 0.5);   // the pass covers SPEED x PERIOD metres
        const double step = 1.0 / (periodS * fs_);
        const float air = p(Air) * 0.01f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float d = dist_.next(p(Distance));
            double x = 0.0, edge = 1.0;
            if (autoMode) {
                phase_ += step;
                if (phase_ >= 1.0) phase_ -= 1.0;
                x = (2.0 * phase_ - 1.0) * halfX;
                // fade in / out at the very ends of each pass (where it jumps back to the start)
                edge = std::clamp(std::min(phase_, 1.0 - phase_) / 0.04, 0.0, 1.0);
            } else {
                x = static_cast<double>(pos_.next(p(Position) * 0.01f)) * kManualX;
            }
            const double r = std::sqrt(x * x + static_cast<double>(d) * d);
            const float delay = static_cast<float>(r / kC * fs_);
            line_.write(0.5f * (L[i] + R[i]));
            float y = line_.read(delay) * static_cast<float>(d / r * edge);
            // far away the highs fade: a one-pole low-pass that closes with distance
            const double cutoff = 18000.0 * std::pow(static_cast<double>(d) / r, 1.5 * air);
            const float k = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::min(cutoff, fs_ * 0.45) / fs_));
            air_[0] = y + k * (air_[0] - y);
            y = air_[0];
            const float pan = static_cast<float>(x / r);   // -1 left .. +1 right
            const float a = (pan + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
            const float wl = y * std::cos(a) * 1.4142f, wr = y * std::sin(a) * 1.4142f;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (wl - L[i])) * o;
            R[i] = (R[i] + m * (wr - R[i])) * o;
        }
    }

private:
    double fs_ = 48000.0;
    hc::fx::DelayLine line_;
    float air_[2]{};
    double phase_ = 0.0;
    hc::fx::Smoothed pos_, dist_, mix_, out_;
};

} // namespace
} // namespace driveby

std::unique_ptr<hc::fx::Effect> makeDriveBy() { return std::make_unique<driveby::Impl>(); }

} // namespace hem::rack
