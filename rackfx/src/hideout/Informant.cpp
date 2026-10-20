// THE INFORMANT — vocoder with a built-in carrier (see Hideout.hpp).
//
// The input (the "voice") and the carrier run through the same bank of band-pass filters. Each
// carrier band is turned to the level the voice has in that band (the carrier band's own level is
// divided out first, so the voice's tone comes through whatever the carrier is), then they add up.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace informant {
namespace {
using namespace hideout;

const char* const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kOctaveN[] = {"2", "3", "4"};
const char* const kChordN[] = {"ONE", "FIFTH", "MAJOR", "MINOR", "OCTAVE"};
const char* const kWaveN[] = {"SAW", "PULSE", "NOISE"};
const char* const kBandsN[] = {"16", "24", "32"};
const ParamInfo kParams[kCount] = {
    {"note", "Note", "", 0, 11, 0, Taper::Choice, kNoteNames},
    {"octave", "Octave", "", 0, 2, 1, Taper::Choice, kOctaveN},
    {"chord", "Chord", "", 0, 4, 0, Taper::Choice, kChordN},
    {"wave", "Wave", "", 0, 2, 0, Taper::Choice, kWaveN},
    {"bands", "Bands", "", 0, 2, 1, Taper::Choice, kBandsN},
    {"react", "React", "ms", 2.0f, 200.0f, 20.0f, Taper::Log},
    {"shift", "Shift", "st", -12.0f, 12.0f, 0.0f},
    {"sibilance", "Sibilance", "%", 0.0f, 100.0f, 40.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr int kMaxBands = 32;
constexpr int kBandsFor[] = {16, 24, 32};
constexpr int kChord[5][3] = {{0, 99, 99}, {0, 7, 99}, {0, 4, 7}, {0, 3, 7}, {0, 12, 99}};
constexpr double kLowHz = 100.0, kHighHz = 8000.0;
constexpr float kMakeup = 4.0f;   // the narrow bands leave gaps between them: measured, brings a voice back to about its own level

float blep(float t, float dt) noexcept {
    if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        for (auto& b : ana_) for (auto& f : b) f.clear();
        for (auto& b : syn_) for (auto& f : b) f.clear();
        for (auto& f : sib_) f.clear();
        for (int k = 0; k < kMaxBands; ++k) { env_[k] = 0.0f; cenv_[k] = 0.0f; }
        for (auto& ph : osc_) ph = 0.0f;
        noise_ = 22222u;
        key_.fill(-1e9f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        sibG_.prepare(fs_, 30, p(Sibilance) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const int nb = bands_;
        const int wave = std::clamp(static_cast<int>(std::lround(p(P::Wave))), 0, 2);
        const int chord = std::clamp(static_cast<int>(std::lround(p(Chord))), 0, 4);
        const int note = std::clamp(static_cast<int>(std::lround(p(Note))), 0, 11);
        const int oct = std::clamp(static_cast<int>(std::lround(p(Octave))), 0, 2) + 2;
        float dt[3] = {0, 0, 0};
        int voices = 0;
        for (int v = 0; v < 3; ++v) {
            if (kChord[chord][v] > 24) continue;
            const double midi = 12.0 * (oct + 1) + note + kChord[chord][v];
            dt[voices++] = static_cast<float>(440.0 * std::exp2((midi - 69.0) / 12.0) / fs_);
        }
        const float vNorm = 1.0f / std::sqrt(static_cast<float>(std::max(voices, 1)));
        const float att = coefMs(fs_, 1.0), rel = coefMs(fs_, p(React)), crel = coefMs(fs_, 10.0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = 0.5f * (L[i] + R[i]);
            // the carrier
            float c = 0.0f;
            if (wave == 2) {
                noise_ = noise_ * 1664525u + 1013904223u;
                c = static_cast<float>(static_cast<std::int32_t>(noise_)) * (1.0f / 2147483648.0f);
            } else {
                for (int v = 0; v < voices; ++v) {
                    float& t = osc_[v];
                    t += dt[v];
                    if (t >= 1.0f) t -= 1.0f;
                    float s = 2.0f * t - 1.0f - blep(t, dt[v]);
                    if (wave == 1) {   // pulse: a saw minus the same saw half a cycle on
                        float t2 = t + 0.5f;
                        if (t2 >= 1.0f) t2 -= 1.0f;
                        s -= 2.0f * t2 - 1.0f - blep(t2, dt[v]);
                    }
                    c += s * vNorm;
                }
            }
            // the bands
            float y = 0.0f;
            for (int k = 0; k < nb; ++k) {
                const float a = ana_[k][1].tick(ana_[k][0].tick(x));
                const float s = syn_[k][1].tick(syn_[k][0].tick(c));
                const float ax = std::fabs(a), sx = std::fabs(s);
                env_[k] = ax + (ax > env_[k] ? att : rel) * (env_[k] - ax);
                cenv_[k] = sx + (sx > cenv_[k] ? att : crel) * (cenv_[k] - sx);
                const float g = std::min(env_[k] / std::max(cenv_[k], 1e-4f), 30.0f);
                y += s * g;
            }
            y *= kMakeup;
            // the esses come through as they are
            y += sibG_.next(p(Sibilance) * 0.01f) * sib_[1].tick(sib_[0].tick(x));
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (y - L[i])) * o;
            R[i] = (R[i] + m * (y - R[i])) * o;
        }
    }

private:
    void design() noexcept {
        const std::array<float, 2> k{p(Bands), p(Shift)};
        if (k == key_) return;
        const bool bandsChanged = k[0] != key_[0];
        key_ = k;
        bands_ = kBandsFor[std::clamp(static_cast<int>(std::lround(p(Bands))), 0, 2)];
        const double ratio = std::pow(kHighHz / kLowHz, 1.0 / (bands_ - 1));
        const double q = std::sqrt(ratio) / (ratio - 1.0);
        const double shift = std::exp2(p(Shift) / 12.0);
        for (int b = 0; b < bands_; ++b) {
            const double f = kLowHz * std::pow(ratio, b);
            const C a = C::make(T::BandPass, fs_, f, q), s = C::make(T::BandPass, fs_, std::min(f * shift, fs_ * 0.45), q);
            ana_[b][0].c = ana_[b][1].c = a;
            syn_[b][0].c = syn_[b][1].c = s;
        }
        sib_[0].c = C::make(T::HighPass, fs_, std::min(4500.0, fs_ * 0.4), 0.5412);
        sib_[1].c = C::make(T::HighPass, fs_, std::min(4500.0, fs_ * 0.4), 1.3066);
        if (bandsChanged) {
            for (auto& b : ana_) for (auto& f : b) f.clear();
            for (auto& b : syn_) for (auto& f : b) f.clear();
        }
    }

    double fs_ = 48000.0;
    hc::fx::Biquad ana_[kMaxBands][2], syn_[kMaxBands][2], sib_[2];
    float env_[kMaxBands]{}, cenv_[kMaxBands]{}, osc_[3]{};
    std::uint32_t noise_ = 22222u;
    int bands_ = 24;
    std::array<float, 2> key_{};
    hc::fx::Smoothed mix_, sibG_, out_;
};

} // namespace
} // namespace informant

std::unique_ptr<hc::fx::Effect> makeInformant() { return std::make_unique<informant::Impl>(); }

} // namespace hem::rack
