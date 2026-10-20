// SIX SHOOTER — rhythmic multi-tap delay (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace sixshooter {
namespace {
using namespace hideout;

const char* const kShotsN[] = {"1", "2", "3", "4", "5", "6"};
const char* const kNoteChoiceN[] = {"FREE", "1/16", "1/8 T", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "BAR"};
const char* const kSourceN[] = {"HOST", "BPM"};
const ParamInfo kParams[kCount] = {
    {"shots", "Shots", "", 0, 5, 2, Taper::Choice, kShotsN},
    {"note", "Note", "", 0, 8, 3, Taper::Choice, kNoteChoiceN},
    {"time", "Time", "ms", 10.0f, 1000.0f, 250.0f, Taper::Log},
    {"syncsource", "Tempo From", "", 0, 1, 0, Taper::Choice, kSourceN},
    {"bpm", "BPM", "", 40.0f, 240.0f, 120.0f},
    {"decay", "Decay", "%", 0.0f, 100.0f, 30.0f},
    {"spread", "Spread", "%", 0.0f, 100.0f, 60.0f},
    {"swing", "Swing", "%", 0.0f, 50.0f, 0.0f},
    {"feedback", "Feedback", "%", 0.0f, 90.0f, 0.0f},
    {"tone", "Tone", "Hz", 1000.0f, 20000.0f, 8000.0f, Taper::Log},
    {"mix", "Mix", "%", 0.0f, 100.0f, 35.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kMaxSeconds = 4.0;   // the whole round of shots fits in this

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        const bool free = p(Note) < 0.5f;
        if (i == Time) return free;
        if (i == SyncSource) return !free;
        if (i == Bpm) return !free && p(SyncSource) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        line_.prepare(static_cast<std::uint32_t>(kMaxSeconds * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        line_.clear();
        for (auto& f : tone_) f.clear();
        fbTone_.clear();
        last_ = 0.0f;
        toneHz_ = -1.0f;
        spacing_.prepare(fs_, 120, spacingSamples());
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int shots = std::clamp(static_cast<int>(std::lround(p(Shots))), 0, 5) + 1;
        const float perShot = hc::fx::dbToGain(-12.0f * p(Decay) * 0.01f);
        const float spread = p(Spread) * 0.01f, swing = p(Swing) * 0.01f, fb = p(Feedback) * 0.01f;
        float gain[6], panL[6], panR[6];
        float g = 1.0f;
        for (int k = 0; k < 6; ++k) {
            gain[k] = g;
            g *= perShot;
            const float pan = (k % 2 == 0 ? -1.0f : 1.0f) * spread;   // -1 left .. +1 right, equal power
            const float a = (pan + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
            panL[k] = std::cos(a) * 1.4142f;
            panR[k] = std::sin(a) * 1.4142f;
        }
        const float target = spacingSamples();
        for (std::uint32_t i = 0; i < n; ++i) {
            const float sp = spacing_.next(target);
            const float x = 0.5f * (L[i] + R[i]);
            line_.write(x + fb * fbTone_.tick(last_));
            float wl = 0.0f, wr = 0.0f;
            float at = 0.0f;
            for (int k = 0; k < shots; ++k) {
                at = sp * static_cast<float>(k + 1) + (k % 2 == 1 ? swing * sp : 0.0f);
                const float y = line_.read(at) * gain[k];
                wl += y * panL[k];
                wr += y * panR[k];
                if (k == shots - 1) last_ = line_.read(at);
            }
            wl = tone_[0].tick(wl);
            wr = tone_[1].tick(wr);
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (wl - L[i])) * o;
            R[i] = (R[i] + m * (wr - R[i])) * o;
        }
    }

private:
    // samples between shots, kept so the whole round (6 shots with swing) fits the line
    float spacingSamples() const noexcept {
        const int note = std::clamp(static_cast<int>(std::lround(p(Note))), 0, 8);
        double ms = p(Time);
        if (note > 0) ms = beatsMs(kNoteBeats[note - 1], p(SyncSource) >= 0.5f ? p(Bpm) : tempo());
        const double maxSpacing = (kMaxSeconds * fs_ - 16.0) / 6.5;
        return static_cast<float>(std::clamp(ms * 0.001 * fs_, 4.0, maxSpacing));
    }
    void design() noexcept {
        if (p(Tone) == toneHz_) return;
        toneHz_ = p(Tone);
        const C c = C::make(T::LowPass, fs_, std::min<double>(toneHz_, fs_ * 0.45), 0.7071);
        tone_[0].c = tone_[1].c = fbTone_.c = c;
    }

    double fs_ = 48000.0;
    hc::fx::DelayLine line_;
    hc::fx::Biquad tone_[2], fbTone_;
    float last_ = 0.0f, toneHz_ = -1.0f;
    hc::fx::Smoothed spacing_, mix_, out_;
};

} // namespace
} // namespace sixshooter

std::unique_ptr<hc::fx::Effect> makeSixShooter() { return std::make_unique<sixshooter::Impl>(); }

} // namespace hem::rack
