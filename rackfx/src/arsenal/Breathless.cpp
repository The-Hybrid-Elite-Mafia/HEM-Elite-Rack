// BREATHLESS — finds breaths between the words and turns them down (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace breathless;

const char* const kListenN[] = {"CLEANED", "REMOVED"};
const ParamInfo kParams[kCount] = {
    {"sensitivity", "Sensitivity", "%", 0.0f, 100.0f, 50.0f},
    {"levelgate", "Level Gate", "dB", -64.0f, 0.0f, -30.0f},
    {"reduction", "Reduction", "dB", -80.0f, 0.0f, -80.0f},
    {"fadeout", "Soft End", "ms", 0.1f, 200.0f, 5.0f, Taper::Log},
    {"fadein", "Soft Start", "ms", 0.1f, 200.0f, 5.0f, Taper::Log},
    {"roomtone", "Room Floor", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"listen", "Listen", "", 0, 1, 0, Taper::Choice, kListenN},
};
const hc::fx::MeterInfo kMeters[] = {{"BREATH", "%", 0.0f, 100.0f}, {"LEVEL", "dB", -80.0f, 0.0f}, {"REDUCTION", "dB", -60.0f, 0.0f}};

// How breath-like a short stretch is, 0..1: breath is noise in the middle highs (many zero crossings,
// but not as many as an "s") with little energy down where a voiced sound has its body.
float breathScore(float zcr, float lowShare) noexcept {
    const float rise = std::clamp((zcr - 0.03f) / 0.07f, 0.0f, 1.0f);     // noisy enough
    const float fall = std::clamp((0.32f - zcr) / 0.10f, 0.0f, 1.0f);     // but not an "s"
    const float noBody = std::clamp((0.45f - lowShare) / 0.35f, 0.0f, 1.0f);
    return rise * fall * noBody;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLookahead; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        audio_.prepare(kLookahead);
        lp_.c = C::make(T::LowPass, fs, 400.0, 0.7071);
        lp2_.c = lp_.c;
        hp_.c = C::make(T::HighPass, fs, 60.0, 0.7071);
        reset();
    }
    void reset() noexcept override {
        audio_.clear();
        lp_.clear();
        lp2_.clear();
        hp_.clear();
        ms_ = lowMs_ = 0.0;
        zcr_ = 0.0f;
        last_ = 0.0f;
        gr_ = 0.0f;
        score_ = 0.0f;
        roomDb_ = 0.0f;
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
        publishMeter(2, 0.0f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float avK = 1.0f - coefMs(fs_, 8.0);
        const float need = 1.0f - p(Sensitivity) * 0.01f;
        const float gateDb = p(LevelGate), red = p(Reduction) <= -79.9f ? -120.0f : p(Reduction);   // -80 = all the way
        const bool room = p(RoomTone) >= 0.5f;
        // the room's level: the quietest the track gets, falling up to 30 dB a second, rising 1 dB a second
        const float roomUp = static_cast<float>(1.0 / fs_), roomDown = static_cast<float>(30.0 / fs_);
        const float outC = coefMs(fs_, p(FadeOut)), inC = coefMs(fs_, p(FadeIn));
        const bool breaths = p(Listen) >= 0.5f;
        float lvShown = -100.0f, scoreMax = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float m = hp_.tick(0.5f * (L[i] + R[i]));   // mono, rumble removed
            const float lo = lp2_.tick(lp_.tick(m));
            ms_ += (static_cast<double>(m) * m - ms_) * avK;
            lowMs_ += (static_cast<double>(lo) * lo - lowMs_) * avK;
            const bool cross = (m >= 0.0f) != (last_ >= 0.0f) && std::fabs(m - last_) > 1e-6f;
            last_ = m;
            zcr_ += ((cross ? 1.0f : 0.0f) - zcr_) * avK;
            const float lv = powDb(2.0 * ms_);
            const float lowShare = ms_ > 1e-14 ? static_cast<float>(lowMs_ / ms_) : 1.0f;
            score_ = breathScore(zcr_, lowShare);
            // a breath: breath-like enough, quieter than LEVEL GATE, and not silence
            const bool breath = score_ >= need && lv < gateDb && lv > -80.0f;
            if (lv > -90.0f) roomDb_ = lv < roomDb_ ? std::max(lv, roomDb_ - roomDown) : roomDb_ + roomUp;
            // ROOM TONE: a breath only comes down as far as the room, so the gap isn't dead silence
            const float target = breath ? (room ? std::max(red, std::min(roomDb_ - lv, 0.0f)) : red) : 0.0f;
            gr_ = target + (target < gr_ ? outC : inC) * (gr_ - target);
            lvShown = lv;
            scoreMax = std::max(scoreMax, score_);
            const float g = hc::fx::dbToGain(gr_);
            const float al = audio_.l.process(L[i], kLookahead), ar = audio_.r.process(R[i], kLookahead);
            // the breaths path is exactly what was taken out: voice + breaths = the original
            L[i] = breaths ? al * (1.0f - g) : al * g;
            R[i] = breaths ? ar * (1.0f - g) : ar * g;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishMeter(0, scoreMax * 100.0f);
        publishMeter(1, std::max(lvShown, -100.0f));
        publishMeter(2, gr_);
        publishReduction(gr_);
    }

private:
    double fs_ = 48000.0;
    Delay2 audio_;
    hc::fx::Biquad lp_, lp2_, hp_;
    double ms_ = 0, lowMs_ = 0;
    float zcr_ = 0, last_ = 0, gr_ = 0, score_ = 0, roomDb_ = 0;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeBreathless() { return std::make_unique<Impl>(); }

} // namespace hem::rack
