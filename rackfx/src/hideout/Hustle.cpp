// HUSTLE — one LFO driving tremolo, vibrato and pan (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace hustle {
namespace {
using namespace hideout;

const char* const kNoteChoiceN[] = {"HZ", "1/16", "1/8 T", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "BAR"};
const char* const kSourceN[] = {"HOST", "BPM"};
const char* const kWaveN[] = {"SINE", "TRI", "SQUARE", "RISE", "FALL"};
const ParamInfo kParams[kCount] = {
    {"rate", "Rate", "Hz", 0.05f, 20.0f, 4.0f, Taper::Log},
    {"note", "Note", "", 0, 8, 0, Taper::Choice, kNoteChoiceN},
    {"syncsource", "Tempo From", "", 0, 1, 0, Taper::Choice, kSourceN},
    {"bpm", "BPM", "", 40.0f, 240.0f, 120.0f},
    {"wave", "Wave", "", 0, 4, 0, Taper::Choice, kWaveN},
    {"tremolo", "Tremolo", "%", 0.0f, 100.0f, 50.0f},
    {"vibrato", "Vibrato", "%", 0.0f, 100.0f, 0.0f},
    {"pan", "Pan", "%", 0.0f, 100.0f, 0.0f},
    {"offset", "Offset", "deg", 0.0f, 360.0f, 90.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kVibCents = 50.0;                         // VIBRATO 100 % = +-50 cents
constexpr double kMaxSwingMs = 10.0;                       // the most the vibrato's delay may swing
constexpr double kBaseMs = kMaxSwingMs + 0.5;              // so the vibrato has room both ways: the latency

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return base_; }
    bool paramActive(int i) const noexcept override {
        const bool free = p(Note) < 0.5f;
        if (i == Rate) return free;
        if (i == SyncSource) return !free;
        if (i == Bpm) return !free && p(SyncSource) >= 0.5f;
        if (i == Offset) return p(Pan) > 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        base_ = static_cast<std::uint32_t>(std::lround(kBaseMs * 0.001 * fs));
        for (auto& d : vib_) d.prepare(2 * base_ + 8);
        dry_.prepare(base_);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : vib_) d.clear();
        dry_.clear();
        phase_ = 0.0;
        smA_ = smF_ = smP_ = 0.0f;
        trem_.prepare(fs_, 30, p(Tremolo) * 0.01f);
        vibr_.prepare(fs_, 30, p(Vibrato) * 0.01f);
        pan_.prepare(fs_, 30, p(Pan) * 0.01f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const double hz = rateHz();
        const double step = hz / fs_;
        const hideout::Wave w = static_cast<hideout::Wave>(std::clamp(static_cast<int>(std::lround(p(P::Wave))), 0, 4));
        const double off = p(Offset) / 360.0;
        // how far the delay swings for +-50 cents at this rate (a sine's slope), kept in range
        const float swing = static_cast<float>(std::min(kMaxSwingMs * 0.001 * fs_, (std::exp2(kVibCents / 1200.0) - 1.0) / (2.0 * std::numbers::pi * hz) * fs_));
        const float smooth = coefMs(fs_, 2.0);   // takes the clicks out of the square and saw edges
        for (std::uint32_t i = 0; i < n; ++i) {
            phase_ += step;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const float a = lfo(phase_, w), pn = lfo(phase_ + off, w);
            smA_ = a + smooth * (smA_ - a);
            smP_ = pn + smooth * (smP_ - pn);
            const float fm = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_));   // vibrato always smooth
            smF_ = fm;
            const float t = trem_.next(p(Tremolo) * 0.01f), v = vibr_.next(p(Vibrato) * 0.01f), pa = pan_.next(p(Pan) * 0.01f);
            // vibrato: read the delay around its centre
            const float at = static_cast<float>(base_) + v * swing * smF_;
            vib_[0].write(L[i]);
            vib_[1].write(R[i]);
            float yl = vib_[0].read(at), yr = vib_[1].read(at);
            // tremolo: down from full level, never up
            const float g = 1.0f - t * 0.5f * (1.0f + smA_);
            yl *= g;
            yr *= g;
            // pan: swing the image (equal power, centred = unchanged)
            const float ang = (pa * smP_ + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
            yl *= std::cos(ang) * 1.4142f;
            yr *= std::sin(ang) * 1.4142f;
            const float dl = dry_.l.process(L[i], base_), dr = dry_.r.process(R[i], base_);
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (dl + m * (yl - dl)) * o;
            R[i] = (dr + m * (yr - dr)) * o;
        }
    }

private:
    double rateHz() const noexcept {
        const int note = std::clamp(static_cast<int>(std::lround(p(Note))), 0, 8);
        if (note == 0) return p(Rate);
        const double bpm = p(SyncSource) >= 0.5f ? p(Bpm) : tempo();
        return 1000.0 / beatsMs(kNoteBeats[note - 1], bpm);
    }

    double fs_ = 48000.0;
    std::uint32_t base_ = 504;
    hc::fx::DelayLine vib_[2];
    Delay2 dry_;
    double phase_ = 0.0;
    float smA_ = 0, smF_ = 0, smP_ = 0;
    hc::fx::Smoothed trem_, vibr_, pan_, mix_, out_;
};

} // namespace
} // namespace hustle

std::unique_ptr<hc::fx::Effect> makeHustle() { return std::make_unique<hustle::Impl>(); }

} // namespace hem::rack
