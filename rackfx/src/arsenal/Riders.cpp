// WHEELMAN (vocal) and CHAUFFEUR (bass) — automatic level riders (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;

const char* const kSpeedN[] = {"FAST", "SLOW"};
const hc::fx::MeterInfo kMeters[] = {{"RIDE", "dB", -12.0f, 12.0f, true}, {"LEVEL", "dB", -70.0f, 0.0f}};

// The rider: while the voice / the bass is there, the wheel turns so its level sits at TARGET (within
// RANGE MIN .. MAX); while it isn't, the wheel goes to IDLE (within the range) over about a second.
struct Rider {
    float ride = 0.0f;
    void clear() noexcept { ride = 0.0f; }
    float tick(bool present, float level, float target, float lo, float hi, float idle, float k, float idleK) noexcept {
        const float want = present ? std::clamp(target - level, lo, hi) : std::clamp(idle, lo, hi);
        ride = want + (present ? k : idleK) * (ride - want);
        ride = std::clamp(ride, lo, hi);   // narrowing the range takes effect at once
        return ride;
    }
};

// ---- WHEELMAN: listens to the voice's range (150 Hz .. 8 kHz). VOCAL SENSITIVITY decides how loud the
// voice must be to count (+12 = even soft singing; -12 = only clear lines). STAGE: anything under BLEED FLOOR
// is the other instruments leaking into the mic and is left alone.
namespace wm {
using namespace wheelman;
const ParamInfo kParams[kCount] = {
    {"target", "Target", "dB", -42.0f, 0.0f, -21.0f},
    {"sensitivity", "Vocal Sensitivity", "dB", -12.0f, 12.0f, 0.0f},
    {"attack", "Attack", "", 0, 1, 0, Taper::Choice, kSpeedN},
    {"rangemin", "Range Min", "dB", -12.0f, 11.0f, -6.0f},
    {"rangemax", "Range Max", "dB", -11.0f, 12.0f, 6.0f},
    {"idle", "Idle", "dB", -12.0f, 12.0f, 0.0f},
    {"live", "Stage", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"spill", "Bleed Floor", "dB", -48.0f, -12.0f, -30.0f},
    {"output", "Output", "dB", -12.0f, 12.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override { return i != Spill || p(Live) >= 0.5f; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const C a = C::make(T::HighPass, fs, 150.0, 0.7071), b = C::make(T::LowPass, fs, 8000.0, 0.7071);
        det_[0].c = det_[2].c = a;
        det_[1].c = det_[3].c = b;
        reset();
    }
    void reset() noexcept override {
        for (auto& f : det_) f.clear();
        ms_ = 0.0;
        rider_.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float rmsK = 1.0f - coefMs(fs_, 30.0);
        const float k = coefMs(fs_, p(Attack) < 0.5f ? 60.0 : 300.0), idleK = coefMs(fs_, 1000.0);
        const float lo = std::min(p(RangeMin), p(RangeMax)), hi = std::max(p(RangeMin), p(RangeMax));
        const float floorDb = std::max(-50.0f - 2.5f * p(Sensitivity), p(Live) >= 0.5f ? p(Spill) : -100.0f);
        float lvShown = -100.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = det_[1].tick(det_[0].tick(L[i])), dr = det_[3].tick(det_[2].tick(R[i]));
            ms_ += (0.5 * (static_cast<double>(dl) * dl + static_cast<double>(dr) * dr) - ms_) * rmsK;
            const float lv = powDb(2.0 * ms_);
            lvShown = lv;
            const float ride = rider_.tick(lv > floorDb, lv, p(Target), lo, hi, p(Idle), k, idleK);
            const float g = hc::fx::dbToGain(ride) * out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
        }
        publishMeter(0, rider_.ride);
        publishMeter(1, std::max(lvShown, -100.0f));
        publishReduction(std::min(rider_.ride, 0.0f));
    }

private:
    double fs_ = 48000.0;
    hc::fx::Biquad det_[4];
    double ms_ = 0;
    Rider rider_;
    hc::fx::Smoothed out_;
};
} // namespace wm

// ---- CHAUFFEUR: listens to the bass range (30 .. 300 Hz). A note counts when it is over IGNORE BLEED
// and the bass range carries enough of the sound (NOTE DETECT: 100 = even a note that is mostly
// overtones); IGNORE ARTIFACTS holds off until a note has lasted 0..40 ms, so clicks and finger noise
// don't move the wheel. RESPONSE FAST or SLOW.
namespace cf {
using namespace chauffeur;
const ParamInfo kParams[kCount] = {
    {"target", "Target", "dB", -42.0f, 0.0f, -14.0f},
    {"notesens", "Note Detect", "%", 0.0f, 100.0f, 50.0f},
    {"response", "Response", "", 0, 1, 1, Taper::Choice, kSpeedN},
    {"ignorespill", "Ignore Bleed", "dB", -50.0f, -3.0f, -40.0f},
    {"ignoreartifacts", "Ignore Artifacts", "%", 0.0f, 100.0f, 50.0f},
    {"rangemin", "Range Min", "dB", -12.0f, 11.0f, -6.0f},
    {"rangemax", "Range Max", "dB", -11.0f, 12.0f, 6.0f},
    {"idle", "Idle", "dB", -12.0f, 12.0f, 0.0f},
    {"output", "Output", "dB", -12.0f, 12.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const C a = C::make(T::HighPass, fs, 30.0, 0.7071), b = C::make(T::LowPass, fs, 300.0, 0.7071);
        det_[0].c = det_[2].c = a;
        det_[1].c = det_[3].c = b;
        reset();
    }
    void reset() noexcept override {
        for (auto& f : det_) f.clear();
        ms_ = full_ = 0.0;
        held_ = 0;
        rider_.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float rmsK = 1.0f - coefMs(fs_, 15.0);
        const float k = coefMs(fs_, p(Response) < 0.5f ? 40.0 : 200.0), idleK = coefMs(fs_, 1000.0);
        const float lo = std::min(p(RangeMin), p(RangeMax)), hi = std::max(p(RangeMin), p(RangeMax));
        const float shareNeed = -6.0f - 0.15f * p(NoteSens);   // dB of the whole the bass range must carry
        const auto holdN = static_cast<std::uint32_t>(p(IgnoreArtifacts) * 0.0004f * static_cast<float>(fs_));
        float lvShown = -100.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = det_[1].tick(det_[0].tick(L[i])), dr = det_[3].tick(det_[2].tick(R[i]));
            ms_ += (0.5 * (static_cast<double>(dl) * dl + static_cast<double>(dr) * dr) - ms_) * rmsK;
            full_ += (0.5 * (static_cast<double>(L[i]) * L[i] + static_cast<double>(R[i]) * R[i]) - full_) * rmsK;
            const float lv = powDb(2.0 * ms_), share = powDb(ms_) - powDb(full_);
            lvShown = lv;
            const bool note = lv > p(IgnoreSpill) && share > shareNeed;
            held_ = note ? std::min(held_ + 1, holdN + 1) : 0;
            const float ride = rider_.tick(note && held_ > holdN, lv, p(Target), lo, hi, p(Idle), k, idleK);
            const float g = hc::fx::dbToGain(ride) * out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
        }
        publishMeter(0, rider_.ride);
        publishMeter(1, std::max(lvShown, -100.0f));
        publishReduction(std::min(rider_.ride, 0.0f));
    }

private:
    double fs_ = 48000.0;
    hc::fx::Biquad det_[4];
    double ms_ = 0, full_ = 0;
    std::uint32_t held_ = 0;
    Rider rider_;
    hc::fx::Smoothed out_;
};
} // namespace cf

} // namespace

std::unique_ptr<hc::fx::Effect> makeWheelman() { return std::make_unique<wm::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeChauffeur() { return std::make_unique<cf::Impl>(); }

} // namespace hem::rack
