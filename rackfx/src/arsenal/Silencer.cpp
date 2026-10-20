// SILENCER — de-esser, classic filter detector or smart share-of-highs detector (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace silencer;

const char* const kDetectN[] = {"CLASSIC", "SMART"};
const char* const kFilterN[] = {"HIGH-PASS", "BAND-PASS"};
const char* const kSplitN[] = {"SPLIT", "WIDE"};
const ParamInfo kParams[kCount] = {
    {"detect", "Detect", "", 0, 1, 1, Taper::Choice, kDetectN},
    {"freq", "Frequency", "Hz", 2000.0f, 16000.0f, 6000.0f, Taper::Log},
    {"filter", "Filter", "", 0, 1, 0, Taper::Choice, kFilterN},
    {"width", "Width", "%", 0.0f, 100.0f, 50.0f},
    {"threshold", "Threshold", "dB", -60.0f, 0.0f, -26.0f},
    {"range", "Range", "dB", 0.0f, 30.0f, 10.0f},
    {"split", "Mode", "", 0, 1, 0, Taper::Choice, kSplitN},
    {"lookahead", "Lookahead", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"listen", "Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kLookahead; }
    bool paramActive(int i) const noexcept override {
        const bool smart = p(Detect) >= 0.5f;
        if (i == Filter) return !smart;
        if (i == Width) return smart;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        audio_.prepare(kLookahead);
        det_.prepare(kLookahead);
        full_.prepare(kLookahead);
        reset();
    }
    void reset() noexcept override {
        audio_.clear();
        det_.clear();
        full_.clear();
        for (auto& b : lp_) b.clear();
        for (auto& b : sc_) b.clear();
        designed_ = {-1, -1};
        envHf_ = envFull_ = 0.0f;
        gr_ = 0.0f;
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const bool smart = p(Detect) >= 0.5f, bandPass = p(Filter) >= 0.5f;
        const float f = p(Freq);
        if (f != designed_[0] || (smart ? 0.0f : (bandPass ? 1.0f : 0.0f)) != designed_[1]) {
            designed_ = {f, smart ? 0.0f : (bandPass ? 1.0f : 0.0f)};
            const C lp = C::make(T::LowPass, fs_, f, 0.7071);
            const C sc = (!smart && bandPass) ? C::make(T::BandPass, fs_, f, 1.4) : C::make(T::HighPass, fs_, f, 0.7071);
            for (auto& b : lp_) b.c = lp;
            for (auto& b : sc_) b.c = sc;
        }
        const float thr = p(Threshold), range = p(Range);
        const float need = 3.0f + p(Width) * 0.12f;   // SMART: how close to "all highs" a sound must be (3 .. 15 dB)
        const bool wide = p(Split) >= 0.5f, look = p(Lookahead) >= 0.5f, listen = p(Listen) >= 0.5f;
        const float att = coefMs(fs_, 0.5), rel = coefMs(fs_, 60.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i], r = R[i];
            // the detector (24 dB / oct) — heard now (LOOKAHEAD) or lined up with the audio
            float dl = sc_[1].tick(sc_[0].tick(l)), dr = sc_[3].tick(sc_[2].tick(r));
            float fl = l, fr = r;
            const float adl = det_.l.process(dl, kLookahead), adr = det_.r.process(dr, kLookahead);
            const float afl = full_.l.process(fl, kLookahead), afr = full_.r.process(fr, kLookahead);
            if (!look) { dl = adl; dr = adr; fl = afl; fr = afr; }
            const float hf = std::max(std::fabs(dl), std::fabs(dr)), full = std::max(std::fabs(fl), std::fabs(fr));
            envHf_ = hf + (hf > envHf_ ? att : rel) * (envHf_ - hf);
            envFull_ = full + (full > envFull_ ? att : rel) * (envFull_ - full);
            const float over = std::max(0.0f, dbOf(envHf_) - thr);
            float amount = 1.0f;
            if (smart) {   // an ess is a sound that is mostly highs: how much of it sits above FREQUENCY
                const float share = dbOf(envHf_) - dbOf(envFull_);   // 0 dB = all of it
                amount = std::clamp((share + need) / need, 0.0f, 1.0f);
            }
            const float target = -std::min(range, over * 0.75f * amount);
            gr_ = follow(gr_, target, att, rel);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_);
            const float al = audio_.l.process(l, kLookahead), ar = audio_.r.process(r, kLookahead);
            // the ess band = the input minus its low-passed part, so band + rest adds back exactly
            const float hl = al - lp_[0].tick(al), hr = ar - lp_[1].tick(ar);
            if (listen) { L[i] = adl; R[i] = adr; continue; }
            if (wide) { L[i] = al * g; R[i] = ar * g; }
            else {   // only the highs go down
                L[i] = al + hl * (g - 1.0f);
                R[i] = ar + hr * (g - 1.0f);
            }
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    Delay2 audio_, det_, full_;
    hc::fx::Biquad lp_[2], sc_[4];
    std::array<float, 2> designed_{-1, -1};
    float envHf_ = 0, envFull_ = 0, gr_ = 0;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeSilencer() { return std::make_unique<Impl>(); }

} // namespace hem::rack
