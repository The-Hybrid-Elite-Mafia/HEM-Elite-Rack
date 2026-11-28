// =============================================================================
//  09 LEVELPILOT — automatic vocal leveling: a slow "hand on the fader" that rides the vocal
//  toward a target level. No ratio, no squash (that is VOICEPRESS, 13): it moves the whole
//  vocal's gain up or down, at motor speed, inside the limits you set.
//  MUSIC INFLUENCE (needs the music on the sidechain): the target rises when the music is loud.
//  BLEED GUARD: below this level it is not singing — the fader goes back to IDLE GAIN instead of
//  pulling up headphone bleed.
//  Dead flat on load: SPAN 0 dB (no travel), IDLE GAIN 0 dB, OUTPUT TRIM 0 dB.
//  AUTOMATION: OFF rides; READ plays back the RIDE GAIN control (which the host automates);
//  WRITE is not possible from inside a plug-in here (greyed) — the live ride is on the RIDE meter.
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kTarget, kMaxGain, kMinGain, kSpan, kMotor, kIdle, kSidechain, kInfluence, kBleed, kLowLatency, kAutomation, kRideGain, kTrim, kBypass, kCount };
const char* const kAutoN[] = {"OFF", "WRITE", "READ"};
const ParamInfo kParams[kCount] = {
    {"target", "TARGET", "dBFS", -42.0f, 0.0f, -21.0f},
    {"max_gain", "MAX GAIN", "dB", 0.0f, 12.0f, 6.0f},
    {"min_gain", "MIN GAIN", "dB", -12.0f, 0.0f, -6.0f},
    {"span", "SPAN", "dB", 0.0f, 24.0f, 0.0f},
    {"motor", "MOTOR RIDE", "", 0.0f, 100.0f, 50.0f},
    {"idle_gain", "IDLE GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"sidechain", "SIDECHAIN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"music_influence", "MUSIC INFLUENCE", "", 0.0f, 100.0f, 0.0f},
    {"bleed_guard", "BLEED GUARD", "dBFS", -80.0f, -24.0f, -48.0f},
    {"low_latency", "LOW LATENCY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"automation", "AUTOMATION", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kAutoN},
    {"ride_gain", "RIDE GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"output_trim", "OUTPUT TRIM", "dB", -12.0f, 12.0f, 0.0f},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"VOICE", "dBFS", -60, 0}, {"MUSIC", "dBFS", -60, 0}, {"RIDE", "dB", -12, 12, true}, {"OUTPUT", "dBFS", -60, 0}, {"RIDING", "", 0, 1},
};

class LevelPilot final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    LevelPilot() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i == kInfluence) return p(kSidechain) > 0.5f;
        if (i == kRideGain) return static_cast<int>(p(kAutomation)) == 2;
        if (i >= kTarget && i <= kMotor) return static_cast<int>(p(kAutomation)) != 2;
        return true;
    }

    void prepare(double fs, std::uint32_t) override { fs_ = fs; outM_.prepare(fs); trimG_.prepare(fs, 20.0, kit::dbToGain(p(kTrim))); reset(); }
    void reset() noexcept override { voice_ = music_ = 0.0f; ride_ = 0.0f; gain_ = kit::dbToGain(0.0f); riding_ = false; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        // level detectors: RMS-like, 50 ms (or 10 ms in LOW LATENCY)
        const double win = p(kLowLatency) > 0.5f ? 10.0 : 50.0;
        const float vc = hc::fx::onePoleCoef(fs_, win), mc = hc::fx::onePoleCoef(fs_, 300.0);
        // MOTOR RIDE: 0 fast (20 ms) .. 100 slow (2 s), log
        const float motorMs = 20.0f * std::pow(100.0f, p(kMotor) * 0.01f);
        const float rc = hc::fx::onePoleCoef(fs_, motorMs);
        const float gc = hc::fx::onePoleCoef(fs_, 5.0);   // the fader itself never jumps
        const bool readMode = static_cast<int>(p(kAutomation)) == 2;
        const bool sc = p(kSidechain) > 0.5f && scL_;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = 0.5f * (L[i] + R[i]);
            voice_ = x * x + vc * (voice_ - x * x);
            if (sc) { const float m = 0.5f * (scL_[i] + scR_[i]); music_ = m * m + mc * (music_ - m * m); }
            if ((i & 15u) == 0) steer(readMode, sc);
            ride_ = rideTarget_ + rc * (ride_ - rideTarget_);
            const float gT = kit::dbToGain(readMode ? p(kRideGain) : ride_);
            gain_ = gT + gc * (gain_ - gT);
            const float g = gain_ * trimG_.next(kit::dbToGain(p(kTrim)));
            L[i] = kit::sane(L[i] * g); R[i] = kit::sane(R[i] * g);
        }
        outM_.feed(L, n);
        publishMeter(0, 10.0f * std::log10(std::max(voice_ * 2.0f, 1e-12f)));
        publishMeter(1, 10.0f * std::log10(std::max(music_ * 2.0f, 1e-12f)));
        publishMeter(2, readMode ? p(kRideGain) : ride_);
        publishMeter(3, outM_.db());
        publishMeter(4, riding_ ? 1.0f : 0.0f);
        scL_ = scR_ = nullptr;
    }

private:
    void steer(bool readMode, bool sc) noexcept {
        if (readMode) { rideTarget_ = p(kRideGain); return; }
        const float idle = p(kIdle), half = 0.5f * p(kSpan);
        const float lvl = 10.0f * std::log10(std::max(voice_ * 2.0f, 1e-12f));   // sine-calibrated dBFS
        riding_ = half > 0.0f && lvl > p(kBleed);
        if (!riding_) { rideTarget_ = std::clamp(idle, idle - half, idle + half); return; }
        float target = p(kTarget);
        if (sc) {   // MUSIC INFLUENCE: up to +6 dB of target when the music is 12 dB over -24 dBFS (ASSUMED)
            const float mus = 10.0f * std::log10(std::max(music_ * 2.0f, 1e-12f));
            target += p(kInfluence) * 0.01f * std::clamp((mus + 24.0f) * 0.5f, -6.0f, 6.0f);
        }
        float want = target - lvl;
        want = std::clamp(want, p(kMinGain), p(kMaxGain));
        want = std::clamp(want, idle - half, idle + half);
        rideTarget_ = want;
    }

    double fs_ = 48000;
    float voice_ = 0, music_ = 0, ride_ = 0, rideTarget_ = 0, gain_ = 1;
    bool riding_ = false;
    kit::PeakMeter outM_;
    hc::fx::Smoothed trimG_;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeLevelPilot() { return std::make_unique<LevelPilot>(); }

} // namespace hem::staging
