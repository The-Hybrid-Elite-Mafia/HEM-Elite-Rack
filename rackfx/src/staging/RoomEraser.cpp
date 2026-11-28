// =============================================================================
//  41 ROOMERASER — takes the room off a recording while keeping the direct hit. A quick level follower
//  is compared with a held peak level: right at each attack they agree (the direct sound passes),
//  and as the sound dies into its room tail the quick level falls under the held peak; the further it
//  falls, the more the tail is turned down. AMOUNT sets how hard (up to 24 dB), SENSITIVITY how early
//  in the decay it starts, RELEASE how fast the cut moves in (the held level lets go at 4 x RELEASE).
//  BAND_FOCUS: FULL (the whole signal), VOCAL (only 200 Hz..6 kHz is cleaned), LOW-CUT (everything
//  above 150 Hz is cleaned, the lows are left alone). MIX, OUTPUT.
//  Classic envelope DSP (no neural model): strong on drums and speech in small rooms, weaker on long
//  dense halls where the tail overlaps the next note. NOT PROVEN on real recordings.
//  Different from VOICEVEIL (08: steady noise) and RESONANCEGUARD (13: ringing frequencies).
//  Dead flat on load: AMOUNT 0 (no cut), OUTPUT 0 dB.
//  ASSUMED: quick level = mean square, 1 ms / 8 ms; the cut starts when the quick level is 9 dB (SENSITIVITY 0)
//  down to 1 dB (SENSITIVITY 100) under the held level and is full (24 dB x AMOUNT) 6 dB later; it moves in
//  at 0.1 x RELEASE and lets go in 0.2 ms at a new attack; VOCAL / LOW-CUT bands are 12 dB/oct.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kAmount, kSens, kRelease, kFocus, kMix, kOut, kBypass, kCount };
const char* const kFocusN[] = {"FULL", "VOCAL", "LOW-CUT"};
const ParamInfo kParams[kCount] = {
    {"amount", "AMOUNT", "%", 0.0f, 100.0f, 0.0f},
    {"sensitivity", "SENSITIVITY", "%", 0.0f, 100.0f, 50.0f},
    {"release", "RELEASE", "ms", 20.0f, 500.0f, 120.0f, Taper::Log},
    {"band_focus", "BAND FOCUS", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kFocusN},
    {"mix", "MIX", "%", 0.0f, 100.0f, 100.0f},
    {"output", "OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 6}, {"OUT", "dB", -60, 6}, {"ROOM REDUCTION", "dB", -24, 0}};

class RoomEraser final : public hc::fx::Effect {
public:
    RoomEraser() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        hpV_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs, 200.0, 0.7071));
        lpV_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs, std::min(6000.0, fs * 0.45), 0.7071));
        hpL_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs, 150.0, 0.7071));
        amt_.prepare(fs, 20.0, 0.0f); mix_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f);
        reset();
    }
    void reset() noexcept override { hpV_.clear(); lpV_.clear(); hpL_.clear(); fast_ = 0; pow_ = 0; held_ = 0; cut_ = 0; cutM_ = 0; pkI_ = pkO_ = 0; loaded_ = false; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float aT = p(kAmount) * 0.01f, mT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOut));
        if (!loaded_) { amt_.cur = aT; mix_.cur = mT; out_.cur = oT; loaded_ = true; }
        const int focus = static_cast<int>(p(kFocus));
        const float start = 9.0f - 0.08f * p(kSens);   // dB under the held level where the cut begins (9 .. 1)
        const float fA = hc::fx::onePoleCoef(fs_, 1.0), fR = hc::fx::onePoleCoef(fs_, 8.0), hR = hc::fx::onePoleCoef(fs_, p(kRelease) * 4.0);
        const float cutUp = hc::fx::onePoleCoef(fs_, p(kRelease) * 0.1), cutDown = hc::fx::onePoleCoef(fs_, 1.0), cutOnset = hc::fx::onePoleCoef(fs_, 0.2);
        float pkI = 0, pkO = 0, cutM = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            // the part that gets cleaned (always computed, so changing BAND FOCUS never jumps from silence)
            const float vl = lpV_.l.tick(hpV_.l.tick(xl)), vr = lpV_.r.tick(hpV_.r.tick(xr));
            const float ll = hpL_.l.tick(xl), lr = hpL_.r.tick(xr);
            const float bl = focus == 0 ? xl : focus == 1 ? vl : ll, br = focus == 0 ? xr : focus == 1 ? vr : lr;
            const float e2 = 0.5f * (bl * bl + br * br);
            pow_ = e2 + (e2 > pow_ ? fA : fR) * (pow_ - e2);   // quick level (mean square: little ripple on steady tones)
            const float prevFast = fast_;
            fast_ = std::sqrt(pow_);
            const bool rising = fast_ > prevFast * 1.0005f;   // a new attack: let the cut go at once
            held_ = fast_ > held_ ? fast_ : fast_ + hR * (held_ - fast_);   // the direct level, let go slowly
            const float drop = kit::gainToDb(held_ + 1e-9f) - kit::gainToDb(fast_ + 1e-9f);   // how far into its decay the sound is
            const float a = amt_.next(aT);
            const float want = held_ > 1e-5f ? a * 24.0f * std::clamp((drop - start) / 6.0f, 0.0f, 1.0f) : cut_;
            cut_ = want + (want < cut_ ? (rising ? cutOnset : cutDown) : cutUp) * (cut_ - want);
            cutM = std::max(cutM, cut_);
            const float g = kit::dbToGain(-cut_) - 1.0f;
            const float m = mix_.next(mT), o = out_.next(oT);
            const float yl = (xl + m * g * bl) * o, yr = (xr + m * g * br) * o;
            pkI = std::max(pkI, std::max(std::fabs(xl), std::fabs(xr))); pkO = std::max(pkO, std::max(std::fabs(yl), std::fabs(yr)));
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        pkI_ = pkI; pkO_ = pkO; cutM_ = cutM;
        publishMeter(0, kit::gainToDb(pkI_)); publishMeter(1, kit::gainToDb(pkO_)); publishMeter(2, -cutM_);
        publishReduction(-cutM_);
    }

private:
    double fs_ = 48000;
    hc::fx::Biquad2 hpV_, lpV_, hpL_;
    hc::fx::Smoothed amt_, mix_, out_;
    float fast_ = 0, pow_ = 0, held_ = 0, cut_ = 0, cutM_ = 0, pkI_ = 0, pkO_ = 0;
    bool loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeRoomEraser() { return std::make_unique<RoomEraser>(); }

} // namespace hem::staging
