// DRIVE-BY — a sound passing by on a track (see Hideout.hpp).
//
// The source moves along a straight line that passes CLOSE metres in front of the listener, from START to
// END in TRACK TIME seconds, nearest at CENTER TIME. What the listener hears left the source r / c seconds
// ago (c = 343 m/s): reading a delay line at that changing delay bends the pitch by itself (PITCH scales
// the delay, so 0 % = no bend). GAIN follows (CLOSE / r) ^ (GAIN / 100), PAN the angle, AIR closes a
// low-pass with distance, and the REVERB rises as it moves away.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace driveby {
namespace {
using namespace hideout;

const char* const kDirN[] = {"LEFT TO RIGHT", "RIGHT TO LEFT"};
const char* const kModeN[] = {"LOOP", "ONCE"};
const char* const kTriggerN[] = {"SOUND", "BUTTON"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"tracktime", "Pass Time", "s", 0.5f, 30.0f, 4.0f, Taper::Log},
    {"centertime", "Closest At", "%", 5.0f, 95.0f, 50.0f},
    {"start", "Start", "%", -100.0f, 100.0f, -100.0f},
    {"end", "End", "%", -100.0f, 100.0f, 100.0f},
    {"close", "Close", "%", 0.0f, 100.0f, 70.0f},
    {"direction", "Direction", "", 0, 1, 0, Taper::Choice, kDirN},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"trigger", "Trigger", "", 0, 1, 0, Taper::Choice, kTriggerN},
    {"fire", "Fire", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"gain", "Gain", "%", 0.0f, 200.0f, 100.0f},
    {"gainon", "Gain On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"pan", "Pan", "%", 0.0f, 200.0f, 100.0f},
    {"panon", "Pan On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"pitch", "Pitch", "%", 0.0f, 200.0f, 100.0f},
    {"pitchon", "Pitch On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"air", "Air Loss", "%", 0.0f, 200.0f, 100.0f},
    {"airon", "Air Loss On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"reverb", "Reverb", "dB", -60.0f, 0.0f, -18.0f},
    {"reverbon", "Reverb On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"reverbtime", "Reverb Length", "s", 0.2f, 10.0f, 2.0f, Taper::Log},
    {"brightness", "Reverb Tone", "", 0.0f, 1.0f, 0.5f},
};
const hc::fx::MeterInfo kMeters[] = {{"PROGRESS", "s", 0.0f, 30.0f}};
constexpr double kC = 343.0, kHalfTrack = 60.0;   // metres either side of the listener at START / END +-100 %
constexpr double kFar = 50.0, kNear = 1.0;          // CLOSE 0 % passes 50 m away, 100 % 1 m away

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == Gain) return p(GainOn) >= 0.5f;
        if (i == Pan) return p(PanOn) >= 0.5f;
        if (i == Pitch) return p(PitchOn) >= 0.5f;
        if (i == Air) return p(AirOn) >= 0.5f;
        if (i == Reverb || i == ReverbTime || i == Brightness) return p(ReverbOn) >= 0.5f;
        if (i == Fire) return p(Trigger) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const double rMax = std::sqrt(kHalfTrack * kHalfTrack + kFar * kFar);
        line_.prepare(static_cast<std::uint32_t>(2.0 * rMax / kC * fs) + 16);
        fdn_.prepare(fs, 1.0);
        reset();
    }
    void reset() noexcept override {
        line_.clear();
        fdn_.clear();
        air_[0] = air_[1] = 0.0f;
        env_ = 0.0f;
        // ENERGY waits for the sound; MANUAL starts at once in CONTINUOUS, waits for FIRE in ONE-SHOT
        running_ = p(Trigger) >= 0.5f && p(Mode) < 0.5f;
        t_ = 0.0;
        push_ = p(Fire);
        revKey_ = {-1.0f, -1.0f};
        design();
        delay_ = -1.0f;
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const double T = p(TrackTime), c = std::clamp(p(CenterTime) * 0.01, 0.05, 0.95);
        const bool reverse = p(Direction) >= 0.5f, oneShot = p(Mode) >= 0.5f, manual = p(Trigger) >= 0.5f;
        double x0 = p(Start) * 0.01 * kHalfTrack, x1 = p(End) * 0.01 * kHalfTrack;
        if (reverse) std::swap(x0, x1);
        const double d = kFar * std::pow(kNear / kFar, p(Close) * 0.01);
        const float gainK = p(GainOn) >= 0.5f ? p(Gain) * 0.01f : 0.0f, panK = p(PanOn) >= 0.5f ? p(Pan) * 0.01f : 0.0f;
        const float pitchK = p(PitchOn) >= 0.5f ? p(Pitch) * 0.01f : 0.0f, airK = p(AirOn) >= 0.5f ? p(Air) * 0.01f : 0.0f;
        const float rev = p(ReverbOn) >= 0.5f && p(Reverb) > -59.9f ? hc::fx::dbToGain(p(Reverb)) : 0.0f;
        if (p(Fire) != push_) {   // FIRE: a new pass now, even if one is under way
            push_ = p(Fire);
            if (manual) { running_ = true; t_ = 0.0; }
        }
        const float envRel = coefMs(fs_, 200.0), maxD = static_cast<float>(line_.buf.size()) - 4.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float in = 0.5f * (L[i] + R[i]);
            const float a = std::fabs(in);
            const bool wasQuiet = env_ < 0.01f;
            env_ = a > env_ ? a : a + envRel * (env_ - a);
            if (!manual && !running_ && wasQuiet && env_ >= 0.01f) { running_ = true; t_ = 0.0; }   // ENERGY: the sound starts the pass
            // where on the track: nearest at CENTER TIME
            const double u = std::clamp(t_ / T, 0.0, 1.0);
            const double s = u < c ? 0.5 * u / c : 0.5 + 0.5 * (u - c) / (1.0 - c);
            const double x = x0 + (x1 - x0) * s;
            if (running_) {
                t_ += 1.0 / fs_;
                if (t_ >= T) {
                    if (oneShot) { running_ = false; t_ = T; }
                    else t_ -= T;
                }
            }
            const double r = std::sqrt(x * x + d * d);
            const float want = std::min(static_cast<float>(pitchK * r / kC * fs_), maxD);
            delay_ = want;
            line_.write(in);
            float y = line_.read(delay_) * static_cast<float>(std::pow(d / r, static_cast<double>(gainK)));
            const double cutoff = 18000.0 * std::pow(d / r, 1.5 * airK);
            const float k = static_cast<float>(std::exp(-2.0 * std::numbers::pi * std::min(cutoff, fs_ * 0.45) / fs_));
            air_[0] = y + k * (air_[0] - y);
            y = airK > 0.0f ? air_[0] : y;
            const float pan = std::clamp(static_cast<float>(x / r) * panK, -1.0f, 1.0f);
            const float ang = (pan + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
            float yl = y * std::cos(ang) * 1.4142f, yr = y * std::sin(ang) * 1.4142f;
            // the reverb: more of it as the sound moves away
            const float send = rev * static_cast<float>(1.0 - d / r) * y;
            float rl = 0.0f, rr = 0.0f;
            fdn_.tick(send, send, rl, rr);
            L[i] = yl + rl;
            R[i] = yr + rr;
        }
        publishMeter(0, static_cast<float>(t_));
    }

private:
    void design() noexcept {
        const std::array<float, 2> k{p(ReverbTime), p(Brightness)};
        if (k == revKey_) return;
        revKey_ = k;
        fdn_.set(1.0, p(ReverbTime), 2000.0 * std::pow(8.0, static_cast<double>(p(Brightness))));
    }

    double fs_ = 48000.0, t_ = 0.0;
    hc::fx::DelayLine line_;
    Fdn8 fdn_;
    float air_[2]{}, env_ = 0.0f, delay_ = -1.0f, push_ = 0.0f;
    bool running_ = false;
    std::array<float, 2> revKey_{};
};

} // namespace
} // namespace driveby

std::unique_ptr<hc::fx::Effect> makeDriveBy() { return std::make_unique<driveby::Impl>(); }

} // namespace hem::rack
