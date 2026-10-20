// THE SNITCH — tuner (see Streets.hpp).
//
// The pitch comes from a YIN detector. MODE says which notes count (a preset of the twelve, or MY NOTES:
// the twelve switches); the tuner picks the counting note nearest the sound, in any octave, and shows how
// far off it is. The sound passes untouched.
#include "hem/rack/Streets.hpp"

#include "PitchKit.hpp"

namespace hem::rack {
namespace snitch {
namespace {
using namespace streets;

const char* const kModeN[] = {"CHROMATIC", "GUITAR", "DROP D", "OPEN G", "OPEN D", "OPEN E", "DADGAD", "HALF DOWN", "MY NOTES"};
#define HEM_NOTE(id, name) {"note" id, name, "", 0, 1, 1, Taper::Toggle, kOnOff}
const ParamInfo kParams[kCount] = {
    {"mode", "Mode", "", 0, kTunings - 1, 0, Taper::Choice, kModeN},
    {"reference", "Reference A", "cents", -6.0f, 6.0f, 0.0f},
    HEM_NOTE("c", "C"), HEM_NOTE("cs", "C#"), HEM_NOTE("d", "D"), HEM_NOTE("ds", "D#"), HEM_NOTE("e", "E"), HEM_NOTE("f", "F"),
    HEM_NOTE("fs", "F#"), HEM_NOTE("g", "G"), HEM_NOTE("gs", "G#"), HEM_NOTE("a", "A"), HEM_NOTE("as", "A#"), HEM_NOTE("b", "B"),
};
#undef HEM_NOTE
const hc::fx::MeterInfo kMeters[] = {{"NOTE", "", -1.0f, 127.0f}, {"CENTS", "cents", -50.0f, 50.0f}, {"FREQUENCY", "Hz", 0.0f, 2000.0f},
                                     {"STANDING", "", -1.0f, 1.0f}};
// the notes each tuning counts (bit k = pitch class k, C = 0)
constexpr std::uint16_t bits(std::initializer_list<int> pcs) {
    std::uint16_t b = 0;
    for (int k : pcs) b = static_cast<std::uint16_t>(b | (1u << k));
    return b;
}
constexpr std::uint16_t kPreset[kTunings] = {
    0x0FFF,                     // CHROMATIC
    bits({4, 9, 2, 7, 11}),     // GUITAR: E A D G B E
    bits({2, 9, 7, 11, 4}),     // DROP D: D A D G B E
    bits({2, 7, 11}),           // OPEN G: D G D G B D
    bits({2, 9, 6}),            // OPEN D: D A D F# A D
    bits({4, 11, 8}),           // OPEN E: E B E G# B E
    bits({2, 9, 7}),            // DADGAD
    bits({3, 8, 1, 6, 10}),     // HALF DOWN: Eb Ab Db Gb Bb Eb
    0,                          // MY NOTES: the switches
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override { return i < Note0 || p(Mode) >= MyNotes - 0.5f; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        yin_.prepare(fs, 38.0, 1024, 256);   // down to below a bass's low E
        reset();
    }
    void reset() noexcept override {
        yin_.clear();
        held_ = 0;
        cents_ = 0.0f;
        note_ = -1.0;
        show(-1.0f, 0.0f, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t i = 0; i < n; ++i) {
            if (!yin_.push(0.5f * (L[i] + R[i]))) continue;
            const std::uint16_t on = enabled();
            if (yin_.hz() > 0.0f && yin_.confidence() > 0.8f && on != 0) {
                const double refA = 440.0 * std::exp2(static_cast<double>(p(Reference)) / 1200.0);
                const double m = midiOf(yin_.hz(), refA);
                // the switched-on note nearest the sound (any octave)
                double best = 0.0, bestD = 1e9;
                const double base = std::floor(m);
                for (int d = -12; d <= 12; ++d) {
                    const double cand = base + d;
                    const int pc = (static_cast<int>(cand) % 12 + 12) % 12;
                    if (!(on & (1u << pc))) continue;
                    if (std::fabs(m - cand) < bestD) {
                        bestD = std::fabs(m - cand);
                        best = cand;
                    }
                }
                const float c = std::clamp(static_cast<float>((m - best) * 100.0), -50.0f, 50.0f);
                cents_ = held_ > 0 && best == note_ ? cents_ + 0.35f * (c - cents_) : c;   // a calm needle
                note_ = best;
                held_ = 40;
                show(static_cast<float>(best), cents_, yin_.hz());
            } else if (held_ > 0 && --held_ == 0) {
                show(-1.0f, 0.0f, 0.0f);   // the note faded: clear after a moment
            }
        }
        (void)R;
    }

private:
    std::uint16_t enabled() const noexcept {
        const int mode = std::clamp(static_cast<int>(std::lround(p(Mode))), 0, kTunings - 1);
        if (mode != MyNotes) return kPreset[mode];
        std::uint16_t b = 0;
        for (int k = 0; k < 12; ++k)
            if (p(Note0 + k) >= 0.5f) b = static_cast<std::uint16_t>(b | (1u << k));
        return b;
    }
    void show(float note, float cents, float hz) noexcept {
        publishMeter(0, note);
        publishMeter(1, cents);
        publishMeter(2, hz);
        publishMeter(3, note < 0.0f ? 0.0f : cents <= -3.0f ? -1.0f : cents >= 3.0f ? 1.0f : 0.0f);
    }

    double fs_ = 48000.0, note_ = -1.0;
    Yin yin_;
    int held_ = 0;
    float cents_ = 0.0f;
};

} // namespace
} // namespace snitch

std::unique_ptr<hc::fx::Effect> makeSnitch() { return std::make_unique<snitch::Impl>(); }

} // namespace hem::rack
