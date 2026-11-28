// =============================================================================
//  32 MIXBOND — stereo mix-bus glue: a linked VCA compressor with stepped ATTACK (0.1 .. 30 ms),
//  RELEASE (0.1 .. 1.2 s or AUTO, which lengthens the longer the bus has been squeezed) and RATIO
//  (2 / 4 / 10), THRESHOLD and MAKEUP, a sidechain high-pass (OFF or 21..500 Hz) so the low end does
//  not pump the mix, an external key, parallel MIX, OUTPUT TRIM, ANALOG colour and an AUTOFADE
//  (FADE ON fades the output out over FADE TIMER seconds, FADE OFF fades it back in).
//  Different from BUSFOUNDRY (21: feedback / feedforward, tone weighting) and VCA COMPRESSOR (52:
//  smooth unstepped controls).
//  Dead flat on load: MIX 0 %, MAKEUP 0 dB, TRIM 0 dB, ANALOG off, FADE off.
//  ASSUMED: THRESHOLD 0 on the knob = -10 dBFS (the knob reads -15..+15 around that point); AUTO
//  release 0.1 s growing to 1.2 s with sustained gain reduction; ANALOG = a faint 2nd harmonic that
//  follows the squeeze plus hiss at about -96 dBFS on the compressed path.
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kThr, kMakeup, kAttack, kRelease, kRatio, kScHpOn, kScHp, kExtSc, kFadeTime, kFade, kMix, kTrim, kAnalog, kCompIn, kBypass, kCount };
const char* const kAttN[] = {"0.1 MS", "0.3 MS", "1 MS", "3 MS", "10 MS", "30 MS"};
const char* const kRelN[] = {"0.1 S", "0.3 S", "0.6 S", "1.2 S", "AUTO"};
const char* const kRatN[] = {"2", "4", "10"};
const char* const kOffOnN[] = {"OFF", "ON"};
const char* const kInOutN[] = {"OUT", "IN"};
constexpr std::array<float, 6> kAttMs{0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
constexpr std::array<float, 4> kRelS{0.1f, 0.3f, 0.6f, 1.2f};
constexpr std::array<float, 3> kRat{2.0f, 4.0f, 10.0f};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -15.0f, 15.0f, 0.0f},
    {"makeup", "MAKEUP", "dB", -5.0f, 15.0f, 0.0f},
    {"attack", "ATTACK", "", 0.0f, 5.0f, 3.0f, Taper::Choice, kAttN},
    {"release", "RELEASE", "", 0.0f, 4.0f, 4.0f, Taper::Choice, kRelN},
    {"ratio", "RATIO", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kRatN},
    {"sc_hp_on", "SIDECHAIN HP", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kOffOnN},
    {"sc_hp_freq", "SIDECHAIN HP FREQ", "Hz", 21.0f, 500.0f, 60.0f, Taper::Log},
    {"ext_sc", "EXTERNAL SC", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kOffOnN},
    {"fade_time", "FADE TIMER", "s", 1.0f, 60.0f, 10.0f},
    {"fade", "FADE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kOffOnN},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"trim", "OUTPUT TRIM", "dB", -18.0f, 18.0f, 0.0f},
    {"analog", "ANALOG", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kOffOnN},
    {"comp_in", "COMP", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kInOutN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"GAIN REDUCTION", "dB", -20, 10, true}, {"FADE", "%", 0, 100}};

class MixBond final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    MixBond() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i == kScHp) return p(kScHpOn) > 0.5f;
        if (i >= kThr && i <= kExtSc) return p(kCompIn) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        mix_.prepare(fs, 20.0, 0.0f); trim_.prepare(fs, 20.0, 1.0f); mk_.prepare(fs, 20.0, 1.0f); thr_.prepare(fs, 30.0, -10.0f); an_.prepare(fs, 20.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        sc_.clear(); env_ = -120; gr_ = 0; work_ = 0; fadeG_ = p(kFade) > 0.5f ? 0.0f : 1.0f; dc_.fill(0); rng_ = 31337u; key_ = -1; scOn_ = false; loaded_ = false; grM_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; publishAll(); return; }
        const float mixT = p(kCompIn) > 0.5f ? p(kMix) * 0.01f : 0.0f, trimT = kit::dbToGain(p(kTrim)), mkT = kit::dbToGain(p(kMakeup));
        const float anT = p(kAnalog) > 0.5f ? 1.0f : 0.0f;
        if (!loaded_) { mix_.cur = mixT; trim_.cur = trimT; mk_.cur = mkT; thr_.cur = p(kThr) - 10.0f; an_.cur = anT; fadeG_ = p(kFade) > 0.5f ? 0.0f : 1.0f; loaded_ = true; }
        const float hz = p(kScHp);
        if (hz != key_) { key_ = hz; sc_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hz, 0.7071)); }
        if (p(kScHpOn) > 0.5f) scOn_ = true;
        const bool scHp = p(kScHpOn) > 0.5f;
        const bool ext = p(kExtSc) > 0.5f && scL_ != nullptr;
        const float att = hc::fx::onePoleCoef(fs_, kAttMs[static_cast<std::size_t>(p(kAttack))]);
        const int relI = static_cast<int>(p(kRelease));
        const float ratio = kRat[static_cast<std::size_t>(p(kRatio))];
        const float fadeStep = 1.0f / (p(kFadeTime) * static_cast<float>(fs_));
        const float fadeT = p(kFade) > 0.5f ? 0.0f : 1.0f;
        const float invFs = 1.0f / static_cast<float>(fs_);
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        grM_ = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            float yl = xl, yr = xr;
            // detector always runs (linked), so switching the compressor in never jumps
            float dl = ext ? scL_[i] : xl, dr = ext ? scR_[i] : xr;
            if (scOn_) { const float hl = sc_.l.tick(dl), hr = sc_.r.tick(dr); if (scHp) { dl = hl; dr = hr; } }
            const float lvl = kit::gainToDb(std::max(std::fabs(dl), std::fabs(dr)));
            const float t = thr_.next(p(kThr) - 10.0f);
            const float want = lvl > t ? (lvl - t) * (1.0f - 1.0f / ratio) : 0.0f;
            float relS;
            if (relI < 4) relS = kRelS[static_cast<std::size_t>(relI)];
            else relS = 0.1f + 1.1f * work_;   // AUTO: longer the longer it has been working
            const float rel = std::exp(-invFs / relS);
            gr_ = want + (want > gr_ ? att : rel) * (gr_ - want);
            work_ = std::clamp(work_ + invFs * (gr_ > 1.0f ? 0.5f : -0.25f), 0.0f, 1.0f);
            grM_ = std::max(grM_, gr_);
            if (wet) {
                const float g = kit::dbToGain(-gr_) * mk_.next(mkT);
                float cl = xl * g, cr = xr * g;
                const float a = an_.next(anT);
                if (a > 1e-6f) {
                    const float h = a * 0.015f * std::min(1.0f, gr_ / 6.0f + 0.2f);
                    cl += h * cl * cl; cr += h * cr * cr;
                    dc_[0] += 0.0005f * (cl - dc_[0]); dc_[1] += 0.0005f * (cr - dc_[1]);
                    cl -= dc_[0]; cr -= dc_[1];
                    rng_ = rng_ * 1664525u + 1013904223u;
                    const float nz = a * 1.6e-5f * (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f);
                    cl += nz; cr -= nz;
                }
                const float mx = mix_.next(mixT);
                yl = xl + mx * (cl - xl); yr = xr + mx * (cr - xr);
            }
            fadeG_ = fadeG_ < fadeT ? std::min(fadeT, fadeG_ + fadeStep) : std::max(fadeT, fadeG_ - fadeStep);
            const float o = trim_.next(trimT) * fadeG_ * fadeG_;   // squared ramp: an even-sounding fade
            L[i] = kit::sane(yl * o); R[i] = kit::sane(yr * o);
        }
        scL_ = scR_ = nullptr;
        publishAll();
    }

private:
    void publishAll() noexcept { publishMeter(0, std::clamp(-grM_, -20.0f, 10.0f)); publishMeter(1, fadeG_ * 100.0f); publishReduction(p(kCompIn) > 0.5f ? -grM_ : 0.0f); }
    double fs_ = 48000;
    hc::fx::Biquad2 sc_;
    hc::fx::Smoothed mix_, trim_, mk_, thr_, an_;
    std::array<float, 2> dc_{};
    float env_ = -120, gr_ = 0, work_ = 0, fadeG_ = 1, key_ = -1, grM_ = 0;
    bool scOn_ = false, loaded_ = false;
    std::uint32_t rng_ = 31337u;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeMixBond() { return std::make_unique<MixBond>(); }

} // namespace hem::staging
