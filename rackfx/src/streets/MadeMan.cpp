// MADE MAN — pitch correction for one voice (see Streets.hpp).
//
// The sung pitch at the moment being heard (the PSOLA input's pitch track, kLatency behind the input) is
// compared with the allowed notes: the nearest one is the target. The target glides to a new note over NOTE
// TRANSITION; the correction (RATIO of the way from the sung pitch to the target) glides over SPEED; SHIFT is
// added on top. When nothing is sung the correction lets go. The PSOLA voice applies it; FORMANT CORRECTED
// keeps the formants where they were, OFF lets them move with the pitch.
#include "hem/rack/Streets.hpp"

#include "PitchKit.hpp"

namespace hem::rack {
namespace mademan {
namespace {
using namespace streets;

const char* const kKeyN[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kScaleN[kScales] = {"CHROMATIC", "MAJOR", "MINOR", "HARMONIC MINOR", "MELODIC MINOR", "DORIAN", "PHRYGIAN", "LYDIAN",
                                      "MIXOLYDIAN", "LOCRIAN", "MAJOR PENTA", "MINOR PENTA", "BLUES", "WHOLE TONE", "DIMINISHED", "DOUBLE HARMONIC",
                                      "HUNGARIAN MINOR", "IN", "PHRYGIAN DOM", "HIRAJOSHI", "MY NOTES"};
const char* const kFormantN[] = {"CORRECTED", "OFF"};
#define HEM_NOTE(id, name) {"note" id, name, "", 0, 1, 1, Taper::Toggle, kOnOff}
const ParamInfo kParams[kCount] = {
    {"key", "Root", "", 0, 11, 0, Taper::Choice, kKeyN},
    {"scale", "Scale", "", 0, kScales - 1, 0, Taper::Choice, kScaleN},
    {"speed", "Speed", "ms", 0.0f, 400.0f, 20.0f},
    {"transition", "Note Transition", "ms", 0.0f, 400.0f, 60.0f},
    {"ratio", "Ratio", "%", 0.0f, 100.0f, 100.0f},
    {"formant", "Formant", "", 0, 1, 0, Taper::Choice, kFormantN},
    {"shift", "Shift", "st", -12.0f, 12.0f, 0.0f},
    {"reference", "Reference A", "Hz", 400.0f, 480.0f, 440.0f},
    {"minpitch", "Min Pitch", "Hz", 50.0f, 500.0f, 70.0f, Taper::Log},
    {"maxpitch", "Max Pitch", "Hz", 200.0f, 2000.0f, 1000.0f, Taper::Log},
    HEM_NOTE("c", "C"), HEM_NOTE("cs", "C#"), HEM_NOTE("d", "D"), HEM_NOTE("ds", "D#"), HEM_NOTE("e", "E"), HEM_NOTE("f", "F"),
    HEM_NOTE("fs", "F#"), HEM_NOTE("g", "G"), HEM_NOTE("gs", "G#"), HEM_NOTE("a", "A"), HEM_NOTE("as", "A#"), HEM_NOTE("b", "B"),
};
#undef HEM_NOTE
const hc::fx::MeterInfo kMeters[] = {{"NOTE", "", -1.0f, 127.0f}, {"PULL", "cents", -100.0f, 100.0f}};
// the scales, as steps above the root (bit k = k semitones up)
constexpr std::uint16_t steps(std::initializer_list<int> s) {
    std::uint16_t b = 0;
    for (int k : s) b = static_cast<std::uint16_t>(b | (1u << k));
    return b;
}
constexpr std::uint16_t kScale[kScales - 1] = {
    0x0FFF,                                  // chromatic
    steps({0, 2, 4, 5, 7, 9, 11}),           // major
    steps({0, 2, 3, 5, 7, 8, 10}),           // natural minor
    steps({0, 2, 3, 5, 7, 8, 11}),           // harmonic minor
    steps({0, 2, 3, 5, 7, 9, 11}),           // melodic minor
    steps({0, 2, 3, 5, 7, 9, 10}),           // dorian
    steps({0, 1, 3, 5, 7, 8, 10}),           // phrygian
    steps({0, 2, 4, 6, 7, 9, 11}),           // lydian
    steps({0, 2, 4, 5, 7, 9, 10}),           // mixolydian
    steps({0, 1, 3, 5, 6, 8, 10}),           // locrian
    steps({0, 2, 4, 7, 9}),                  // major pentatonic
    steps({0, 3, 5, 7, 10}),                 // minor pentatonic
    steps({0, 3, 5, 6, 7, 10}),              // blues
    steps({0, 2, 4, 6, 8, 10}),              // whole tone
    steps({0, 2, 3, 5, 6, 8, 9, 11}),        // diminished (whole-half)
    steps({0, 1, 4, 5, 7, 8, 11}),           // double harmonic
    steps({0, 2, 3, 6, 7, 8, 11}),           // hungarian minor
    steps({0, 1, 5, 7, 8}),                  // in
    steps({0, 1, 4, 5, 7, 8, 10}),           // phrygian dominant
    steps({0, 2, 3, 7, 8}),                  // hirajoshi
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override { return i < Note0 || p(Scale) >= kScales - 1.5f; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        in_.prepare(fs);
        syn_.prepare(fs, kLatency);
        reset();
    }
    void reset() noexcept override {
        in_.clear();
        syn_.clear();
        target_ = -1.0;
        pull_ = 0.0;
        publishMeter(0, -1.0f);
        publishMeter(1, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        in_.range(p(MinPitch), std::max(p(MaxPitch), p(MinPitch) + 1.0f), 0.8f);
        const std::uint16_t allowed = notes();
        const double refA = p(Reference), ratio = p(Ratio) * 0.01, shift = p(Shift);
        const double kSpeed = coef(p(Speed)), kTrans = coef(p(Transition));
        const bool keepFormant = p(Formant) < 0.5f;
        float sung = -1.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            in_.push(0.5f * (L[i] + R[i]));
            // the pitch at the moment being heard
            const float per = in_.period(in_.now() - 1 - static_cast<std::int64_t>(kLatency));
            double want = 0.0;
            if (per > 0.0f && allowed != 0) {
                const double m = 69.0 + 12.0 * std::log2(fs_ / per / refA);
                const double note = nearest(m, allowed);
                target_ = target_ < 0.0 ? note : target_ + kTrans * (note - target_);
                want = (target_ - m) * ratio;
                sung = static_cast<float>(std::round(m));
            } else {
                target_ = -1.0;   // nothing sung: let go
            }
            pull_ += kSpeed * (want - pull_);
            const auto r = static_cast<float>(std::exp2((pull_ + shift) / 12.0));
            const float y = syn_.tick(in_, r, keepFormant ? 1.0f : r);
            L[i] = y;
            R[i] = y;
        }
        publishMeter(0, sung);
        publishMeter(1, static_cast<float>(pull_ * 100.0));
    }

private:
    // a glide of `ms` (0 = at once)
    double coef(float ms) const noexcept { return ms <= 0.0f ? 1.0 : 1.0 - std::exp(-1.0 / (0.001 * ms * fs_)); }
    std::uint16_t notes() const noexcept {
        const int sc = std::clamp(static_cast<int>(std::lround(p(Scale))), 0, kScales - 1);
        if (sc == kScales - 1) {
            std::uint16_t b = 0;
            for (int k = 0; k < 12; ++k)
                if (p(Note0 + k) >= 0.5f) b = static_cast<std::uint16_t>(b | (1u << k));
            return b;
        }
        const int root = std::clamp(static_cast<int>(std::lround(p(Key))), 0, 11);
        std::uint16_t b = 0;
        for (int k = 0; k < 12; ++k)
            if (kScale[sc] & (1u << k)) b = static_cast<std::uint16_t>(b | (1u << ((root + k) % 12)));
        return b;
    }
    // the allowed note nearest m (any octave)
    static double nearest(double m, std::uint16_t allowed) noexcept {
        double best = std::round(m), bestD = 1e9;
        const double base = std::floor(m);
        for (int d = -12; d <= 12; ++d) {
            const double cand = base + d;
            const int pc = (static_cast<int>(cand) % 12 + 12) % 12;
            if ((allowed & (1u << pc)) && std::fabs(m - cand) < bestD) {
                bestD = std::fabs(m - cand);
                best = cand;
            }
        }
        return best;
    }

    double fs_ = 48000.0, target_ = -1.0, pull_ = 0.0;
    PsolaInput in_;
    PsolaVoice syn_;
};

} // namespace
} // namespace mademan

std::unique_ptr<hc::fx::Effect> makeMadeMan() { return std::make_unique<mademan::Impl>(); }

} // namespace hem::rack
