// INSIDE MAN — channel strip: gate / expander, compressor, four-band EQ, rotate, clip guard (see ToneShop.hpp).
//
// Signal: PHASE, then the EQ and the dynamics in the ORDER chosen (the gate before the compressor), then
// OUTPUT, ROTATE and the clip guard. Each dynamics section listens through its own key filter when that
// is on; its SOURCE is the signal arriving at the dynamics (MAIN) or the input before the EQ (PRE EQ:
// different only when the EQ comes first). Both sides share one detector, so the picture stays put.
#include "hem/rack/ToneShop.hpp"

#include "hem/rack/Spectrum.hpp"

#include "EqKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace insideman {
namespace {
using namespace arsenal;
using eqkit::Shape;

const char* const kGateModeN[] = {"GATE", "EXPANDER"};
const char* const kSourceN[] = {"MAIN", "BEFORE EQ"};
const char* const kFilterN[] = {"LO CUT", "HI CUT", "BAND", "NOTCH"};
const char* const kCompTypeN[] = {"CLASSIC", "VOCAL"};
const char* const kFirstN[] = {"LO CUT", "LO SHELF", "LO SHELF BUMP", "BELL"};
const char* const kLastN[] = {"HI CUT", "HI SHELF", "HI SHELF BUMP", "BELL"};
const char* const kOrderN[] = {"EQ FIRST", "DYN FIRST"};

#define HEM_KEY(pre, Pre)                                                                                   \
    {pre "keyon", Pre " Key Filter", "", 0, 1, 0, Taper::Toggle, kOnOff},                                   \
    {pre "keysource", Pre " Key Source", "", 0, 1, 0, Taper::Choice, kSourceN},                             \
    {pre "keytype", Pre " Key Type", "", 0, 3, 0, Taper::Choice, kFilterN},                                 \
    {pre "keyfreq", Pre " Key Freq", "Hz", 16.0f, 21357.0f, 733.0f, Taper::Log},                           \
    {pre "keywidth", Pre " Key Width", "oct", 0.1f, 8.0f, 1.0f, Taper::Log}
#define HEM_BAND(n, hz, q)                                                                                  \
    {"b" #n "freq", "Band " #n " Freq", "Hz", 16.0f, 21357.0f, hz, Taper::Log},                            \
    {"b" #n "gain", "Band " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                                         \
    {"b" #n "q", "Band " #n " Q", "", 0.26f, 6.5f, q, Taper::Log}
#define HEM_ON(n) {"b" #n "on", "Band " #n " On", "", 0, 1, 1, Taper::Toggle, kOnOff}
#define HEM_TYPE(n, names) {"b" #n "type", "Band " #n " Type", "", 0, 3, 1, Taper::Choice, names}
const ParamInfo kParams[kCount] = {
    {"gateon", "Gate On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"gatemode", "Gate Mode", "", 0, 1, 0, Taper::Choice, kGateModeN},
    {"gatethreshold", "Gate Threshold", "dB", -80.0f, 18.0f, -40.0f},
    {"floor", "Floor", "dB", -80.0f, 0.0f, -80.0f},
    {"gaterelease", "Gate Release", "ms", 10.0f, 5000.0f, 250.0f, Taper::Log},
    HEM_KEY("g", "Gate"),
    {"compon", "Comp On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"comptype", "Comp Type", "", 0, 1, 0, Taper::Choice, kCompTypeN},
    {"compthreshold", "Comp Threshold", "dB", -60.0f, 18.0f, 0.0f},
    {"ratio", "Ratio", ":1", 0.5f, 50.0f, 3.0f, Taper::Log},
    {"attack", "Attack", "ms", 0.01f, 500.0f, 16.0f, Taper::Log},
    {"release", "Release", "ms", 5.0f, 5000.0f, 160.0f, Taper::Log},
    {"arc", "Auto Rel", "", 0, 1, 1, Taper::Toggle, kOnOff},
    HEM_KEY("c", "Comp"),
    HEM_ON(1), HEM_TYPE(1, kFirstN), HEM_BAND(1, 60.0f, 0.71f),
    HEM_ON(2), HEM_BAND(2, 250.0f, 1.0f),
    HEM_ON(3), HEM_BAND(3, 1325.0f, 1.0f),
    HEM_ON(4), HEM_TYPE(4, kLastN), HEM_BAND(4, 8000.0f, 0.71f),
    {"order", "Order", "", 0, 1, 0, Taper::Choice, kOrderN},
    {"phase", "Phase", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -30.0f, 30.0f, 0.0f},
    {"rotate", "Place", "deg", -45.0f, 45.0f, 0.0f},
};
#undef HEM_BAND
#undef HEM_ON
#undef HEM_TYPE
#undef HEM_KEY
const hc::fx::MeterInfo kMeters[] = {{"GATE", "dB", -80.0f, 0.0f}, {"COMP", "dB", -40.0f, 0.0f}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};

// band b's settings from plain values
eqkit::BandSet bandOf(const float* v, int b) noexcept {
    if (v[kOn[b]] < 0.5f) return {};
    const int t = kType[b] < 0 ? 3 : static_cast<int>(std::lround(v[kType[b]]));
    const float hz = v[kFreq[b]], g = v[kGain[b]], q = v[kQ[b]];
    if (t == 3) return {Shape::Bell, hz, g, q};
    const bool low = b == 0;
    switch (t) {
    case 0: return {low ? Shape::LowCut : Shape::HighCut, hz, 0.0f, std::clamp(q, 0.5f, 2.0f)};
    case 1: return {low ? Shape::LowShelf : Shape::HighShelf, hz, g, std::min(q, 0.71f)};   // plain: never a bump
    default: return {low ? Shape::LowShelf : Shape::HighShelf, hz, g, std::max(q, 0.71f)};  // RES: Q adds the bump
    }
}

// a key filter's biquad: the cuts at 12 dB / oct, the band and the notch WIDTH octaves wide
C keyFilter(int type, double fs, double hz, double octaves) noexcept {
    const double p = std::pow(2.0, std::clamp(octaves, 0.1, 8.0)), q = std::sqrt(p) / (p - 1.0);
    switch (type) {
    case FLowCut: return C::make(T::HighPass, fs, hz, 0.7071);
    case FHighCut: return C::make(T::LowPass, fs, hz, 0.7071);
    case FBand: return C::make(T::BandPass, fs, hz, q);
    default: return C::make(T::Notch, fs, hz, q);
    }
}

// rounds into 1.0 above 0.9; never past it
inline float guard(float y) noexcept {
    const float a = std::fabs(y);
    if (a <= 0.9f) return y;
    const float t = (a - 0.9f) / 0.1f;
    return std::copysign(0.9f + 0.1f * t / (1.0f + t), y);
}

class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    bool paramActive(int i) const noexcept override {
        if (i >= GateMode && i <= GKeyWidth) return p(GateOn) >= 0.5f && (i <= GKeyOn || p(GKeyOn) >= 0.5f);
        if (i >= CompType && i <= CKeyWidth) {
            if (p(CompOn) < 0.5f) return false;
            if (i == Ratio) return p(CompType) < 0.5f;   // VOCAL picks its own
            return i <= CKeyOn || p(CKeyOn) >= 0.5f;
        }
        for (int b = 0; b < kBands; ++b) {
            if (i == kOn[b]) return true;
            if (i != kType[b] && i != kFreq[b] && i != kGain[b] && i != kQ[b]) continue;
            if (p(kOn[b]) < 0.5f) return false;
            if (i == kGain[b]) return kType[b] < 0 || std::lround(p(kType[b])) != 0;   // the cuts have no gain
            return true;
        }
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        eq_[0].prepare(fs);
        eq_[1].prepare(fs);
        arcMem_.set(fs, 0.5, 2.0);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        load();
        eq_[0].clear();
        eq_[1].clear();
        for (auto& k : gKey_) k.clear();
        for (auto& k : cKey_) k.clear();
        gEnv_ = cEnv_ = 0.0f;
        gGain_ = 0.0f;
        cGr_ = 0.0f;
        gOpen_ = false;
        arcMem_.v = 0.0f;
        design();
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        rot_.prepare(fs_, 20, p(Rotate));
        pk_.clear();
        before_.reset();
        after_.reset();
        publishMeter(0, 0.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        load();
        design();
        before_.push(L, R, n);
        const bool eqFirst = p(Order) < 0.5f;
        const float ph = p(Phase) >= 0.5f ? -1.0f : 1.0f;
        // gate
        const bool gateOn = p(GateOn) >= 0.5f, expander = p(GateMode) >= 0.5f;
        const float gThr = p(GateThreshold), floorDb = p(Floor) <= -79.9f ? -120.0f : p(Floor);
        const float gAtt = coefMs(fs_, 0.5), gRel = coefMs(fs_, p(GateRelease)), gEnvRel = coefMs(fs_, 15.0);
        const bool gKey = p(GKeyOn) >= 0.5f, gPre = p(GKeySource) >= 0.5f;
        // compressor
        const bool compOn = p(CompOn) >= 0.5f, vocal = p(CompType) >= 0.5f, arc = p(Arc) >= 0.5f;
        const float cThr = p(CompThreshold), ratio = p(Ratio);
        const float cAtt = coefMs(fs_, std::max(p(Attack), 0.01f)), cEnvRel = coefMs(fs_, 5.0);
        const float relMs = p(Release), relFixed = coefMs(fs_, relMs);
        const bool cKey = p(CKeyOn) >= 0.5f, cPre = p(CKeySource) >= 0.5f;
        const float wOut = hc::fx::dbToGain(p(Output)), wRot = p(Rotate);
        float gateMost = 0.0f, compMost = 0.0f, pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i] * ph, r = R[i] * ph;
            const float inL = l, inR = r;
            eq_[0].step();
            eq_[1].step();
            if (eqFirst) {
                l = eq_[0].run(0, l);
                r = eq_[1].run(0, r);
            }
            // what each detector hears
            const float preL = eqFirst ? inL : l, preR = eqFirst ? inR : r;
            float gx, cx;
            {
                float kl = gPre ? preL : l, kr = gPre ? preR : r;
                if (gKey) { kl = gKey_[0].tick(kl); kr = gKey_[1].tick(kr); }
                gx = std::max(std::fabs(kl), std::fabs(kr));
                kl = cPre ? preL : l;
                kr = cPre ? preR : r;
                if (cKey) { kl = cKey_[0].tick(kl); kr = cKey_[1].tick(kr); }
                cx = std::max(std::fabs(kl), std::fabs(kr));
            }
            // the gate / expander
            float gDb = 0.0f;
            if (gateOn) {
                gEnv_ = gx > gEnv_ ? gx : gx + gEnvRel * (gEnv_ - gx);
                const float lv = dbOf(gEnv_);
                float target;
                if (expander) {
                    target = std::max(std::min(lv - gThr, 0.0f), floorDb);   // 1:2 under the threshold, down to FLOOR
                } else {
                    if (lv >= gThr) gOpen_ = true;
                    else if (lv < gThr - 3.0f) gOpen_ = false;
                    target = gOpen_ ? 0.0f : floorDb;
                }
                gGain_ = target + (target > gGain_ ? gAtt : gRel) * (gGain_ - target);
                gDb = gGain_;
                gateMost = std::min(gateMost, gDb);
            } else {
                gGain_ = 0.0f;
            }
            // the compressor (it hears the gated sound)
            float cDb = 0.0f;
            if (compOn) {
                const float x = cx * hc::fx::dbToGain(gDb);
                cEnv_ = x > cEnv_ ? x : x + cEnvRel * (cEnv_ - x);
                const float over = dbOf(cEnv_) - cThr;
                float rr = ratio;
                if (vocal) rr = 1.5f + std::clamp(over, 0.0f, 24.0f) / 4.0f;   // more over: a higher ratio
                const float knee = 6.0f;
                float ov = 0.0f;
                if (over >= knee * 0.5f) ov = over;
                else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; ov = k * k / (2.0f * knee); }
                const float target = std::clamp((1.0f / std::max(rr, 0.05f) - 1.0f) * ov, -60.0f, 12.0f);
                // ARC: a long squeeze lets go slowly, a short one quickly
                arcMem_.tick(cGr_ < -3.0f);
                const float rel = arc ? coefMs(fs_, relMs * (0.4f + 1.6f * arcMem_.v)) : relFixed;
                const bool deeper = std::fabs(target) > std::fabs(cGr_);
                cGr_ = target + (deeper ? cAtt : rel) * (cGr_ - target);
                cDb = cGr_;
                compMost = std::min(compMost, cDb);
            } else {
                cGr_ = 0.0f;
            }
            const float g = hc::fx::dbToGain(gDb + cDb);
            l *= g;
            r *= g;
            if (!eqFirst) {
                l = eq_[0].run(0, l);
                r = eq_[1].run(0, r);
            }
            const float o = out_.next(wOut);
            l *= o;
            r *= o;
            // ROTATE: the stereo picture turned by the angle
            const float a = rot_.next(wRot) * static_cast<float>(std::numbers::pi / 180.0);
            const float cs = std::cos(a), sn = std::sin(a);
            const float yl = cs * l - sn * r, yr = sn * l + cs * r;
            L[i] = guard(yl);
            R[i] = guard(yr);
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        after_.push(L, R, n);
        publishMeter(0, gateMost);
        publishMeter(1, compMost);
        publishMeter(2, pk_.push(dbOf(pk), n, fs_));
        publishReduction(std::min(gateMost + compMost, 0.0f));
    }

private:
    void load() noexcept {
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        for (int b = 0; b < kBands; ++b) {
            const eqkit::BandSet s = bandOf(v, b);
            eq_[0].set(b, s);
            eq_[1].set(b, s);
        }
    }
    // the key filters, when their settings changed
    void design() noexcept {
        const std::array<float, 6> now{p(GKeyType), p(GKeyFreq), p(GKeyWidth), p(CKeyType), p(CKeyFreq), p(CKeyWidth)};
        if (now == key_) return;
        key_ = now;
        const C g = keyFilter(static_cast<int>(std::lround(now[0])), fs_, now[1], now[2]);
        const C c = keyFilter(static_cast<int>(std::lround(now[3])), fs_, now[4], now[5]);
        gKey_[0].c = gKey_[1].c = g;
        cKey_[0].c = cKey_[1].c = c;
    }

    double fs_ = 48000.0;
    eqkit::IirBands<kBands> eq_[2];
    hc::fx::Biquad gKey_[2], cKey_[2];
    std::array<float, 6> key_{-1, -1, -1, -1, -1, -1};
    float gEnv_ = 0, cEnv_ = 0, gGain_ = 0, cGr_ = 0;
    bool gOpen_ = false;
    Memory arcMem_;
    hc::fx::Smoothed out_, rot_;
    maxim::PeakHold pk_;
    SpectrumMeter before_, after_;
};

int dots(const float* v, double* hz, int max) noexcept {
    int n = 0;
    for (int b = 0; b < kBands && n < max; ++b)
        if (v[kOn[b]] >= 0.5f) hz[n++] = v[kFreq[b]];
    return n;
}

} // namespace

double curveDb(const float* v, double fs, double hz) noexcept {
    double db = 0.0;
    for (int b = 0; b < kBands; ++b) {
        const eqkit::BandSet s = bandOf(v, b);
        C c[1];
        if (eqkit::biquadFor(s.shape, fs, s.hz, s.gainDb, s.q, c) == 1) db += eqkit::iirDb(c[0], fs, hz);
    }
    return db;
}
const CurveInfo kCurve{&curveDb, &dots, 18.0f};

} // namespace insideman

std::unique_ptr<hc::fx::Effect> makeInsideMan() { return std::make_unique<insideman::Impl>(); }

} // namespace hem::rack
