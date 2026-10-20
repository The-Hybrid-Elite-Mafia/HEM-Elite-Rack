// HUSTLE — one LFO driving tremolo, vibrato and pan (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace hustle {
namespace {
using namespace hideout;

const char* const kSyncN[] = {"AUTO", "MANUAL"};
const char* const kFactorN[] = {"x0.25", "x0.5", "x1", "x2", "x4", "x8", "x16", "x32", "x64", "x100"};
constexpr double kFactor[] = {0.25, 0.5, 1, 2, 4, 8, 16, 32, 64, 100};
const char* const kWaveN[] = {"SQUARE", "TRIANGLE", "SAW UP", "SAW DOWN", "SINE"};
constexpr hideout::Wave kWave[] = {hideout::Wave::Square, hideout::Wave::Triangle, hideout::Wave::SawUp, hideout::Wave::SawDown, hideout::Wave::Sine};
const char* const kPhaseN[] = {"0", "90", "180", "270"};
const ParamInfo kParams[kCount] = {
    {"sync", "Sync", "", 0, 1, 1, Taper::Choice, kSyncN},
    {"rate", "Rate", "Hz", 0.01f, 60.0f, 0.2f, Taper::Log},
    {"factor", "x Factor", "", 0, 9, 2, Taper::Choice, kFactorN},
    {"wave", "Wave", "", 0, 4, 4, Taper::Choice, kWaveN},
    {"amdepth", "AM Depth", "%", 0.0f, 100.0f, 50.0f},
    {"amon", "AM", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"amphase", "AM Phase", "", 0, 3, 0, Taper::Choice, kPhaseN},
    {"fmdepth", "FM Depth", "%", 0.0f, 100.0f, 50.0f},
    {"fmon", "FM", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"fmphase", "FM Phase", "", 0, 3, 0, Taper::Choice, kPhaseN},
    {"rotcenter", "Rotation Center", "deg", -180.0f, 180.0f, 0.0f},
    {"roton", "Rotation", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"rotrange", "Rotation Range", "deg", 1.0f, 360.0f, 90.0f},
    {"gain", "Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
constexpr double kVibCents = 50.0;                         // FM 100 % = +-50 cents
constexpr double kMaxSwingMs = 10.0;                       // the most the FM's delay may swing
constexpr double kBaseMs = kMaxSwingMs + 0.5;              // so the FM has room both ways: the latency

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return base_; }
    bool paramActive(int i) const noexcept override {
        if (i == Rate) return p(Sync) >= 0.5f;
        if (i == AmDepth || i == AmPhase) return p(AmOn) >= 0.5f;
        if (i == FmDepth || i == FmPhase) return p(FmOn) >= 0.5f;
        if (i == RotCenter || i == RotRange) return p(RotOn) >= 0.5f;
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
        smA_ = smP_ = 0.0f;
        trem_.prepare(fs_, 30, amAmount());
        vibr_.prepare(fs_, 30, fmAmount());
        center_.prepare(fs_, 30, rotCenter());
        range_.prepare(fs_, 30, rotRange());
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Gain)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const double hz = rateHz();
        const double step = hz / fs_;
        const hideout::Wave w = kWave[std::clamp(static_cast<int>(std::lround(p(Wave))), 0, 4)];
        const double amOff = 0.25 * std::lround(p(AmPhase)), fmOff = 0.25 * std::lround(p(FmPhase));
        // how far the delay swings for +-50 cents at this rate (a sine's slope), kept in range
        const float swing = static_cast<float>(std::min(kMaxSwingMs * 0.001 * fs_, (std::exp2(kVibCents / 1200.0) - 1.0) / (2.0 * std::numbers::pi * hz) * fs_));
        const float smooth = coefMs(fs_, 2.0);   // takes the clicks out of the square and saw edges
        const float wA = amAmount(), wF = fmAmount(), wC = rotCenter(), wR = rotRange(), wMix = p(Mix) * 0.01f, wOut = hc::fx::dbToGain(p(Gain));
        for (std::uint32_t i = 0; i < n; ++i) {
            phase_ += step;
            phase_ -= std::floor(phase_);
            const float a = lfo(phase_ + amOff, w), rot = lfo(phase_, w);
            smA_ = a + smooth * (smA_ - a);
            smP_ = rot + smooth * (smP_ - rot);
            const float fm = static_cast<float>(std::sin(2.0 * std::numbers::pi * (phase_ + fmOff)));   // the pitch move is always smooth
            const float t = trem_.next(wA), v = vibr_.next(wF), c = center_.next(wC), rg = range_.next(wR);
            // FM: read the delay around its centre
            const float at = static_cast<float>(base_) + v * swing * fm;
            vib_[0].write(L[i]);
            vib_[1].write(R[i]);
            float yl = vib_[0].read(at), yr = vib_[1].read(at);
            // AM: down from full level, never up
            const float g = 1.0f - t * 0.5f * (1.0f + smA_);
            yl *= g;
            yr *= g;
            // ROTATION: the picture turned to CENTER + RANGE / 2 x the LFO (plus = toward the left)
            const float deg = c + 0.5f * rg * smP_;
            const float ang = -0.5f * deg * static_cast<float>(std::numbers::pi / 180.0), cs = std::cos(ang), sn = std::sin(ang);
            const float rl = cs * yl - sn * yr, rr = sn * yl + cs * yr;
            const float dl = dry_.l.process(L[i], base_), dr = dry_.r.process(R[i], base_);
            const float m = mix_.next(wMix), o = out_.next(wOut);
            L[i] = (dl + m * (rl - dl)) * o;
            R[i] = (dr + m * (rr - dr)) * o;
        }
    }

private:
    float amAmount() const noexcept { return p(AmOn) >= 0.5f ? p(AmDepth) * 0.01f : 0.0f; }
    float fmAmount() const noexcept { return p(FmOn) >= 0.5f ? p(FmDepth) * 0.01f : 0.0f; }
    float rotCenter() const noexcept { return p(RotOn) >= 0.5f ? p(RotCenter) : 0.0f; }
    float rotRange() const noexcept { return p(RotOn) >= 0.5f ? p(RotRange) : 0.0f; }
    // AUTO: one cycle a beat of the host's tempo; then x FACTOR
    double rateHz() const noexcept {
        const double base = p(Sync) < 0.5f ? tempo() / 60.0 : p(Rate);
        return base * kFactor[std::clamp(static_cast<int>(std::lround(p(Factor))), 0, 9)];
    }

    double fs_ = 48000.0;
    std::uint32_t base_ = 504;
    hc::fx::DelayLine vib_[2];
    Delay2 dry_;
    double phase_ = 0.0;
    float smA_ = 0, smP_ = 0;
    hc::fx::Smoothed trem_, vibr_, center_, range_, mix_, out_;
};

} // namespace
} // namespace hustle

std::unique_ptr<hc::fx::Effect> makeHustle() { return std::make_unique<hustle::Impl>(); }

} // namespace hem::rack
