// SILENCER, TIGHT LIPS and OMERTA — de-essers (see Arsenal.hpp).
//
// SILENCER and TIGHT LIPS listen through a filter at FREQ (HIGH-PASS, or BAND-PASS around it, 24 dB /
// oct); over THRESHOLD the esses come down (3 dB for every 4 over; TIGHT LIPS never more than RANGE).
// SPLIT turns down only what's above FREQ (the input minus its low-passed part, so the rest is
// untouched); WIDE turns the whole sound down for the moment. OMERTA doesn't use a frequency: it hears
// how much of a sound sits in the highs (an ess is mostly highs, a bright vowel isn't), so loud and soft
// esses at any pitch are caught alike; WIDTH says how strict (0 = only pure esses, 100 = anything
// bright); MODE blends from WIDE (0) to SPLIT (100); LOOKAHEAD catches the start of each ess (5 ms,
// its latency is the same either way); MONITOR plays what is being taken out.
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;

const char* const kSplitN[] = {"SPLIT", "WIDE"};
const char* const kFilterN[] = {"HIGH-PASS", "BAND-PASS"};
const char* const kMonitorN[] = {"AUDIO", "SIDECHAIN"};

// the classic detector + splitter (SILENCER, TIGHT LIPS)
struct Classic {
    hc::fx::Biquad sc[4], lp[2];
    float env = 0.0f, gr = 0.0f, designF = -1.0f, designBp = -1.0f;
    void clear() noexcept {
        for (auto& b : sc) b.clear();
        for (auto& b : lp) b.clear();
        env = gr = 0.0f;
        designF = designBp = -1.0f;
    }
    void design(double fs, float f, bool bp) noexcept {
        if (f == designF && (bp ? 1.0f : 0.0f) == designBp) return;
        designF = f;
        designBp = bp ? 1.0f : 0.0f;
        const C s = bp ? C::make(T::BandPass, fs, f, 1.4) : C::make(T::HighPass, fs, f, 0.7071);
        for (auto& b : sc) b.c = s;
        lp[0].c = lp[1].c = C::make(T::LowPass, fs, f, 0.7071);
    }
    // returns the gain (dB) for this sample; dl / dr get the detector signal
    float tick(float l, float r, float& dl, float& dr, float thr, float maxCut, float att, float rel) noexcept {
        dl = sc[1].tick(sc[0].tick(l));
        dr = sc[3].tick(sc[2].tick(r));
        const float x = std::max(std::fabs(dl), std::fabs(dr));
        env = x + (x > env ? att : rel) * (env - x);
        const float target = -std::min(maxCut, std::max(0.0f, dbOf(env) - thr) * 0.75f);
        gr = follow(gr, target, att, rel);
        return gr;
    }
    // apply: SPLIT = only the highs, WIDE = all of it
    void apply(float& l, float& r, float gDb, bool split) noexcept {
        const float g = hc::fx::dbToGain(gDb);
        const float hl = l - lp[0].tick(l), hr = r - lp[1].tick(r);
        if (split) { l += hl * (g - 1.0f); r += hr * (g - 1.0f); }
        else { l *= g; r *= g; }
    }
};

// ---- SILENCER
namespace si {
using namespace silencer;
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -60.0f, 0.0f, -30.0f},
    {"freq", "Frequency", "Hz", 2000.0f, 16000.0f, 5506.0f, Taper::Log},
    {"audio", "Audio", "", 0, 1, 0, Taper::Choice, kSplitN},
    {"sidechain", "Sidechain", "", 0, 1, 0, Taper::Choice, kFilterN},
    {"monitor", "Monitor", "", 0, 1, 0, Taper::Choice, kMonitorN},
};
const hc::fx::MeterInfo kMeters[] = {{"ENERGY", "dB", -60.0f, 0.0f}, {"MOST OFF", "dB", -40.0f, 0.0f}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        cl_.clear();
        most_.clear();
        out_.clear();
        publishMeter(0, -100.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        cl_.design(fs_, p(Freq), p(Sidechain) >= 0.5f);
        const bool split = p(Audio) < 0.5f, mon = p(Monitor) >= 0.5f;
        const float att = coefMs(fs_, 0.5), rel = coefMs(fs_, 60.0);
        float most = 0.0f, pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float dl, dr;
            const float g = cl_.tick(L[i], R[i], dl, dr, p(Threshold), 40.0f, att, rel);
            most = std::min(most, g);
            if (mon) { L[i] = dl; R[i] = dr; }
            else cl_.apply(L[i], R[i], g, split);
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        publishMeter(0, std::max(dbOf(cl_.env), -100.0f));
        publishMeter(1, -most_.push(-most, n, fs_));
        publishMeter(2, out_.push(dbOf(pk), n, fs_));
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    Classic cl_;
    maxim::PeakHold most_, out_;
};
} // namespace si

// ---- TIGHT LIPS
namespace tl {
using namespace tightlips;
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -80.0f, 0.0f, -40.0f},
    {"range", "Range", "dB", -48.0f, 0.0f, -16.0f},
    {"freq", "Frequency", "Hz", 2000.0f, 16000.0f, 5000.0f, Taper::Log},
    {"filter", "Sidechain", "", 0, 1, 0, Taper::Choice, kFilterN},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kSplitN},
    {"listen", "Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        cl_.clear();
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        cl_.design(fs_, p(Freq), p(Filter) >= 0.5f);
        const bool split = p(Mode) < 0.5f, listen = p(Listen) >= 0.5f;
        const float att = coefMs(fs_, 0.5), rel = coefMs(fs_, 60.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float dl, dr;
            const float g = cl_.tick(L[i], R[i], dl, dr, p(Threshold), -p(Range), att, rel);
            most = std::min(most, g);
            if (listen) { L[i] = dl; R[i] = dr; }
            else cl_.apply(L[i], R[i], g, split);
        }
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    Classic cl_;
};
} // namespace tl

// ---- OMERTA
namespace om {
using namespace omerta;
constexpr float kSplitHz = 4500.0f;   // where "the highs" start for its ear and for SPLIT
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -80.0f, 0.0f, -12.5f},
    {"range", "Range", "dB", -48.0f, 0.0f, -14.0f},
    {"width", "Detection Width", "%", 0.0f, 100.0f, 50.0f},
    {"mode", "Mode", "%", 0.0f, 100.0f, 50.0f},
    {"lookahead", "Lookahead", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"monitor", "Monitor", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kLookahead; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        audio_.prepare(kLookahead);
        hfD_.prepare(kLookahead);
        fullD_.prepare(kLookahead);
        const C hp = C::make(T::HighPass, fs, kSplitHz, 0.7071);
        for (auto& b : hp_) b.c = hp;
        lp_[0].c = lp_[1].c = C::make(T::LowPass, fs, kSplitHz, 0.7071);
        reset();
    }
    void reset() noexcept override {
        audio_.clear();
        hfD_.clear();
        fullD_.clear();
        for (auto& b : hp_) b.clear();
        for (auto& b : lp_) b.clear();
        envHf_ = envFull_ = gr_ = 0.0f;
        mode_.prepare(fs_, 30, p(Mode) * 0.01f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold), maxCut = -p(Range);
        const float need = 3.0f + p(Width) * 0.12f;   // how close to "all highs" a sound must be (3 .. 15 dB)
        const bool look = p(Lookahead) >= 0.5f, mon = p(Monitor) >= 0.5f;
        const float att = coefMs(fs_, 0.5), rel = coefMs(fs_, 60.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i], r = R[i];
            float hl = hp_[1].tick(hp_[0].tick(l)), hr = hp_[3].tick(hp_[2].tick(r));
            float fl = l, fr = r;
            const float ahl = hfD_.l.process(hl, kLookahead), ahr = hfD_.r.process(hr, kLookahead);
            const float afl = fullD_.l.process(fl, kLookahead), afr = fullD_.r.process(fr, kLookahead);
            if (!look) { hl = ahl; hr = ahr; fl = afl; fr = afr; }
            const float hf = std::max(std::fabs(hl), std::fabs(hr)), full = std::max(std::fabs(fl), std::fabs(fr));
            envHf_ = hf + (hf > envHf_ ? att : rel) * (envHf_ - hf);
            envFull_ = full + (full > envFull_ ? att : rel) * (envFull_ - full);
            const float share = dbOf(envHf_) - dbOf(envFull_);   // 0 dB = all of it is highs
            const float amount = std::clamp((share + need) / need, 0.0f, 1.0f);
            const float target = -std::min(maxCut, std::max(0.0f, dbOf(envHf_) - thr) * 0.75f * amount);
            gr_ = follow(gr_, target, att, rel);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_);
            const float al = audio_.l.process(l, kLookahead), ar = audio_.r.process(r, kLookahead);
            const float sl = al - lp_[0].tick(al), sr = ar - lp_[1].tick(ar);   // the highs of the late audio
            const float m = mode_.next(p(Mode) * 0.01f);   // 0 = WIDE .. 1 = SPLIT
            const float wl = al * g, wr = ar * g, pl = al + sl * (g - 1.0f), pr = ar + sr * (g - 1.0f);
            const float yl = wl + m * (pl - wl), yr = wr + m * (pr - wr);
            L[i] = mon ? al - yl : yl;
            R[i] = mon ? ar - yr : yr;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }

private:
    double fs_ = 48000.0;
    Delay2 audio_, hfD_, fullD_;
    hc::fx::Biquad hp_[4], lp_[2];
    float envHf_ = 0, envFull_ = 0, gr_ = 0;
    hc::fx::Smoothed mode_;
};
} // namespace om

} // namespace

std::unique_ptr<hc::fx::Effect> makeSilencer() { return std::make_unique<si::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeTightLips() { return std::make_unique<tl::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeOmerta() { return std::make_unique<om::Impl>(); }

} // namespace hem::rack
