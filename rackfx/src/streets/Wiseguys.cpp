// WISEGUYS — voice harmonizer: six PSOLA voices over the original (see Streets.hpp).
//
// The pitch is read from the left and right together. Each voice: its own PSOLA synthesis at its PITCH (with
// FORMANT moved by its own amount plus what LINK adds), then its DELAY (plus its ANIMATE drift), GAIN and PAN.
// The DIRECT voice is the input itself, delayed to line up (kLatency), then its own DELAY and GAIN.
#include "hem/rack/Streets.hpp"

#include "PitchKit.hpp"

namespace hem::rack {
namespace wiseguys {
namespace {
using namespace streets;

const char* const kLinkN[] = {"OFF", "SPEECH", "SINGING", "HORNS", "REEDS", "BOWED", "FULL", "OPPOSITE"};
const char* const kModeN[] = {"ALL ROUND", "STEADY SINGER", "LIVELY SINGER", "BIG VIBRATO", "STEADY PLAYER", "PURE TONES", "LOW END"};
#define HEM_VOICE(n, pitch, pan)                                                                \
    {"on" #n, "Voice " #n " On", "", 0, 1, 0, Taper::Toggle, kOnOff},                           \
    {"pitch" #n, "Voice " #n " Pitch", "st", -12.0f, 12.0f, pitch},                             \
    {"formant" #n, "Voice " #n " Formant", "st", -12.0f, 12.0f, 0.0f},                          \
    {"gain" #n, "Voice " #n " Gain", "dB", -24.0f, 0.0f, -6.0f},                                \
    {"pan" #n, "Voice " #n " Pan", "deg", -45.0f, 45.0f, pan},                                  \
    {"delay" #n, "Voice " #n " Delay", "ms", 0.0f, 100.0f, 0.0f}
const ParamInfo kParams[kCount] = {
    HEM_VOICE(1, 4.0f, -30.0f), HEM_VOICE(2, 7.0f, 30.0f), HEM_VOICE(3, 12.0f, 0.0f),
    HEM_VOICE(4, -5.0f, -15.0f), HEM_VOICE(5, -12.0f, 0.0f), HEM_VOICE(6, 9.0f, 15.0f),
    {"directon", "Direct On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"directgain", "Direct Gain", "dB", -24.0f, 0.0f, 0.0f},
    {"directdelay", "Direct Delay", "ms", 0.0f, 100.0f, 0.0f},
    {"animate", "Animate", "ms", 0.0f, 100.0f, 0.0f},
    {"link", "Link", "", 0, 7, 0, Taper::Choice, kLinkN},
    {"mode", "Mode", "", 0, 6, 0, Taper::Choice, kModeN},
    {"minpitch", "Min Pitch", "Hz", 30.0f, 1000.0f, 50.0f, Taper::Log},
    {"maxpitch", "Max Pitch", "Hz", 100.0f, 4000.0f, 1000.0f, Taper::Log},
};
#undef HEM_VOICE
// LINK: how much of a voice's pitch move its formant follows (our own amounts)
constexpr float kLinkFollow[8] = {0.0f, 0.15f, 0.3f, 0.5f, 0.6f, 0.7f, 1.0f, -1.0f};
// MODE: how sure the pitch reading has to be
constexpr float kSure[7] = {0.8f, 0.85f, 0.7f, 0.75f, 0.85f, 0.9f, 0.8f};
constexpr double kMaxDelayMs = 205.0;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i < DirectOn) return i % kStride == On || p(i - i % kStride + On) >= 0.5f;
        if (i == DirectGain || i == DirectDelay) return p(DirectOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        in_.prepare(fs);
        for (auto& v : syn_) v.prepare(fs, kLatency);
        const auto dl = static_cast<std::uint32_t>(kMaxDelayMs * 0.001 * fs) + 8;
        for (auto& d : vd_) d.prepare(dl);
        for (auto& d : dd_) d.prepare(kLatency + dl);
        reset();
    }
    void reset() noexcept override {
        in_.clear();
        for (auto& v : syn_) v.clear();
        for (auto& d : vd_) d.clear();
        for (auto& d : dd_) d.clear();
        rng_ = 0x51EDu;
        for (int v = 0; v < kVoices; ++v) {
            drift_[v] = driftT_[v] = 0.0f;
            on_[v] = p(voice(v, On)) >= 0.5f ? 1.0f : 0.0f;
            gain_[v].prepare(fs_, 30, hc::fx::dbToGain(p(voice(v, Gain))));
            delay_[v].prepare(fs_, 80, 0.0f);
        }
        dOn_ = p(DirectOn) >= 0.5f ? 1.0f : 0.0f;
        dGain_.prepare(fs_, 30, hc::fx::dbToGain(p(DirectGain)));
        dDelay_.prepare(fs_, 80, static_cast<float>(p(DirectDelay) * 0.001 * fs_));
        tick_ = 0;
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        in_.range(p(MinPitch), std::max(p(MaxPitch), p(MinPitch) + 1.0f), kSure[std::clamp(static_cast<int>(std::lround(p(Mode))), 0, 6)]);
        const float follow = kLinkFollow[std::clamp(static_cast<int>(std::lround(p(Link))), 0, 7)];
        const float step = static_cast<float>(1.0 / (0.02 * fs_)), animate = p(Animate);
        float ratio[kVoices], formant[kVoices], wGain[kVoices], panL[kVoices], panR[kVoices], baseDelay[kVoices];
        bool want[kVoices];
        for (int v = 0; v < kVoices; ++v) {
            const float st = p(voice(v, Pitch));
            ratio[v] = std::exp2(st / 12.0f);
            formant[v] = std::exp2((p(voice(v, Formant)) + follow * st) / 12.0f);
            wGain[v] = hc::fx::dbToGain(p(voice(v, Gain)));
            const float pan = p(voice(v, Pan)) / 45.0f;
            panL[v] = std::min(1.0f, 1.0f - pan);
            panR[v] = std::min(1.0f, 1.0f + pan);
            baseDelay[v] = static_cast<float>(p(voice(v, Delay)) * 0.001 * fs_);
            want[v] = p(voice(v, On)) >= 0.5f;
        }
        const bool dWant = p(DirectOn) >= 0.5f;
        const float wDg = hc::fx::dbToGain(p(DirectGain)), wDd = static_cast<float>(p(DirectDelay) * 0.001 * fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i], r = R[i];
            in_.push(0.5f * (l + r));
            // ANIMATE: each voice drifts toward a new random offset every 50 ms
            if (++tick_ >= static_cast<std::uint32_t>(0.05 * fs_)) {
                tick_ = 0;
                for (float& t : driftT_) t = 0.5f * (rnd() + 1.0f);
            }
            float outL = 0.0f, outR = 0.0f;
            for (int v = 0; v < kVoices; ++v) {
                on_[v] = want[v] ? std::min(1.0f, on_[v] + step) : std::max(0.0f, on_[v] - step);
                drift_[v] += (driftT_[v] - drift_[v]) * 2e-4f;
                const float y = syn_[v].tick(in_, ratio[v], formant[v]);   // always run: switching on is instant and clean
                const float d = delay_[v].next(baseDelay[v] + drift_[v] * animate * 0.001f * static_cast<float>(fs_));
                vd_[v].write(y);
                const float g = gain_[v].next(wGain[v]) * on_[v];
                if (g <= 0.0f) continue;
                const float z = vd_[v].read(d) * g;
                outL += panL[v] * z;
                outR += panR[v] * z;
            }
            // the direct voice: the input, lined up with the voices, then its own delay
            dOn_ = dWant ? std::min(1.0f, dOn_ + step) : std::max(0.0f, dOn_ - step);
            const float dd = static_cast<float>(kLatency) + dDelay_.next(wDd), dg = dGain_.next(wDg) * dOn_;
            dd_[0].write(l);
            dd_[1].write(r);
            L[i] = outL + dg * dd_[0].read(dd);
            R[i] = outR + dg * dd_[1].read(dd);
        }
    }

private:
    float rnd() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;
    }

    double fs_ = 48000.0;
    PsolaInput in_;
    PsolaVoice syn_[kVoices];
    hc::fx::DelayLine vd_[kVoices], dd_[2];
    float on_[kVoices]{}, drift_[kVoices]{}, driftT_[kVoices]{}, dOn_ = 1.0f;
    hc::fx::Smoothed gain_[kVoices], delay_[kVoices], dGain_, dDelay_;
    std::uint32_t rng_ = 0x51EDu, tick_ = 0;
};

} // namespace
} // namespace wiseguys

std::unique_ptr<hc::fx::Effect> makeWiseguys() { return std::make_unique<wiseguys::Impl>(); }

} // namespace hem::rack
