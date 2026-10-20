// THE RACKET — drive pedal: overdrive, distortion, fuzz, octave fuzz (see Streets.hpp).
#include "hem/rack/Streets.hpp"

#include "../tone/ToneCommon.hpp"

namespace hem::rack {
namespace racket {
namespace {
using namespace tone;

const char* const kTypeN[] = {"OVERDRIVE", "DISTORTION", "FUZZ", "OCTAVE"};
const ParamInfo kParams[kCount] = {
    {"type", "Type", "", 0, 3, 0, Taper::Choice, kTypeN},
    {"drive", "Drive", "%", 0.0f, 100.0f, 50.0f},
    {"tone", "Tone", "%", 0.0f, 100.0f, 50.0f},
    {"level", "Level", "dB", -24.0f, 12.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
constexpr float kGainLo[4] = {6.0f, 15.0f, 25.0f, 25.0f}, kGainHi[4] = {40.0f, 50.0f, 60.0f, 60.0f};

// the clipping curves (all pass through 0)
float fuzzCurve(float x) noexcept { return x > 0.0f ? 1.0f - std::exp(-x) : -0.6f * (1.0f - std::exp(x / 0.6f)); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        dry_.prepare(OS::latency());
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& ch : pre_) for (auto& b : ch) b.clear();
        for (auto& ch : post_) for (auto& b : ch) b.clear();
        dry_.clear();
        for (auto& d : dc4_) d = 0.0f;
        dcL_ = dcR_ = 0.0f;
        key_.fill(-1e9f);
        level_.prepare(fs_, 30, hc::fx::dbToGain(p(Level)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    static constexpr std::uint32_t kChunk = 64;

    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        design();
        const int type = type_;
        const float g = hc::fx::dbToGain(kGainLo[type] + (kGainHi[type] - kGainLo[type]) * p(Drive) * 0.01f);
        const float makeup = type == 0 ? 1.0f / std::sqrt(g) : 0.5f;
        float* io[2] = {L, R};
        float dry[2][kChunk], x4[kChunk * OS::kFactor];
        for (int c = 0; c < 2; ++c) {
            for (std::uint32_t i = 0; i < n; ++i) {
                dry[c][i] = c == 0 ? dry_.l.process(io[c][i], OS::latency()) : dry_.r.process(io[c][i], OS::latency());
                float x = io[c][i];
                for (auto& b : pre_[c]) x = b.tick(x);
                io[c][i] = x;
            }
            os_[c].up(io[c], x4, n);
            float& dc = dc4_[c];
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) {
                const float v = x4[j] * g;
                float y = 0.0f;
                switch (type) {
                case 0: y = softClip(v); break;                                  // smooth, even both ways
                case 1: y = std::tanh(2.0f * (v + 0.1f)) - std::tanh(0.2f); break; // harder, a little lopsided
                case 2: y = fuzzCurve(v); break;                                  // squared off, very lopsided
                default: {                                                        // the octave: the wave folded up, then fuzzed
                    const float folded = std::fabs(v) * 0.8f;
                    dc += (folded - dc) * 0.0002f;
                    y = 0.5f * fuzzCurve(v) + 0.7f * fuzzCurve(folded - dc);
                    break;
                }
                }
                x4[j] = y;
            }
            os_[c].down(x4, io[c], n);
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float lv = level_.next(hc::fx::dbToGain(p(Level))), m = mix_.next(p(Mix) * 0.01f);
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i] * makeup;
                for (auto& b : post_[c]) x = b.tick(x);
                float& d = c == 0 ? dcL_ : dcR_;
                d += (x - d) * 0.0005f;
                x = (x - d) * lv;
                io[c][i] = dry[c][i] + m * (x - dry[c][i]);
            }
        }
    }

    void design() noexcept {
        const std::array<float, 2> k{p(Type), p(Tone)};
        if (k == key_) return;
        key_ = k;
        type_ = std::clamp(static_cast<int>(std::lround(p(Type))), 0, 3);
        const C id{};
        const C hp = C::make(T::HighPass, fs_, type_ == 0 ? 160.0 : 90.0, 0.7071);
        const C hump = type_ == 0 ? C::make(T::Bell, fs_, 720.0, 0.7, 6.0) : id;   // the overdrive leans on the mids
        for (auto& ch : pre_) { ch[0].c = hp; ch[1].c = hump; }
        // TONE: from dark (600 Hz) to bright (9 kHz), a 12 dB slope, plus the pedal's own top-end roll-off
        const double f = 600.0 * std::pow(15.0, p(Tone) * 0.01);
        const C lp = C::make(T::LowPass, fs_, std::min(f, fs_ * 0.45), 0.7071);
        const C top = C::make(T::LowPass, fs_, std::min(type_ == 2 || type_ == 3 ? 6000.0 : 9000.0, fs_ * 0.45), 0.7071);
        for (auto& ch : post_) { ch[0].c = lp; ch[1].c = top; }
    }

    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::array<hc::fx::Biquad, 2>, 2> pre_{}, post_{};
    Delay2 dry_;
    float dc4_[2]{}, dcL_ = 0.0f, dcR_ = 0.0f;
    int type_ = 0;
    std::array<float, 2> key_{};
    hc::fx::Smoothed level_, mix_;
};

} // namespace
} // namespace racket

std::unique_ptr<hc::fx::Effect> makeRacket() { return std::make_unique<racket::Impl>(); }

} // namespace hem::rack
