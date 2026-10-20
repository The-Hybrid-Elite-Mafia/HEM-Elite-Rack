// FOUNDRY, BOARDROOM (British console channel EQs) and MIDTOWN (tube midrange EQ): see Comeback.hpp.
//
// One engine runs all three. In order: polarity, the colour stage (4x oversampled beside a clean path of
// the same delay, so the latency never changes), the cuts (18 dB / oct = a first-order section and a
// second-order one with Q 1), the bands (eqkit::IirBands: every move glides, switching a band crossfades),
// OUTPUT, then the hiss / hum. The plan() functions turn the plain settings into cuts and bands; the
// curve MIDTOWN's plate draws comes from the same plan.
#include "hem/rack/Comeback.hpp"

#include "EqKit.hpp"
#include "ToneCommon.hpp"
#include "../arsenal/Vintage.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using eqkit::BandSet;
using eqkit::Shape;

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }

C firstOrder(double fs, double hz, bool lowPass) noexcept {
    const double k = std::tan(std::numbers::pi * std::clamp(hz, 5.0, fs * 0.45) / fs);
    C c;
    c.b0 = lowPass ? k / (1.0 + k) : 1.0 / (1.0 + k);
    c.b1 = lowPass ? c.b0 : -c.b0;
    c.b2 = 0.0;
    c.a1 = (k - 1.0) / (k + 1.0);
    c.a2 = 0.0;
    return c;
}

// What the settings ask for: the cuts (0 = off) and up to four bands.
struct Plan {
    float lowCutHz = 0.0f, highCutHz = 0.0f;
    BandSet band[4];
};
constexpr float kCutQ = 1.0f;   // with the first-order section: an 18 dB / oct Butterworth

// The first-order half of an 18 dB / oct cut: fades in and out when it's switched.
struct FirstOrder {
    hc::fx::Biquad f[2];
    float hz = 0.0f, mix = 0.0f;
    bool lowPass = false, on = false;
    void clear() noexcept {
        f[0].clear();
        f[1].clear();
        mix = on ? 1.0f : 0.0f;
    }
    void want(double fs, float wantHz) noexcept {
        on = wantHz > 0.0f;
        if (on && wantHz != hz) {
            if (mix <= 0.0f) clear();
            hz = wantHz;
            f[0].c = f[1].c = firstOrder(fs, hz, lowPass);
        }
    }
    void step(float s) noexcept { mix = on ? std::min(1.0f, mix + s) : std::max(0.0f, mix - s); }
    float run(int side, float x) noexcept {
        if (mix <= 0.0f) return x;
        const float y = f[side].tick(x);
        return x + mix * (y - x);
    }
};

double planDb(const Plan& pl, double fs, double hz) noexcept {
    double db = 0.0;
    if (pl.lowCutHz > 0.0f) {
        db += eqkit::iirDb(firstOrder(fs, pl.lowCutHz, false), fs, hz);
        db += eqkit::iirDb(C::make(T::HighPass, fs, std::min<double>(pl.lowCutHz, fs * 0.45), kCutQ), fs, hz);
    }
    if (pl.highCutHz > 0.0f) {
        db += eqkit::iirDb(firstOrder(fs, pl.highCutHz, true), fs, hz);
        db += eqkit::iirDb(C::make(T::LowPass, fs, std::min<double>(pl.highCutHz, fs * 0.45), kCutQ), fs, hz);
    }
    for (const auto& b : pl.band) {
        C c[1];
        if (b.shape != Shape::Off && eqkit::biquadFor(b.shape, fs, b.hz, b.gainDb, b.q, c) == 1) db += eqkit::iirDb(c[0], fs, hz);
    }
    return db;
}

struct Spec {
    std::span<const ParamInfo> params;
    std::span<const hc::fx::MeterInfo> meters;
    Plan (*plan)(const float* v) noexcept;
    bool (*active)(const float* v, int i) noexcept;
    int eqIn, phase, output, vintage, clear, hum;   // -1: not on this plug-in
    float colour;                                     // how hard the colour stage is driven (0: off)
};

class Engine final : public hc::fx::Effect {
public:
    explicit Engine(const Spec& s) : s_(s) { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return s_.params; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return s_.meters; }
    std::uint32_t latency() const noexcept override { return kComebackEqLatency; }
    bool paramActive(int i) const noexcept override {
        float v[hc::fx::Effect::kMaxParams];
        for (std::size_t k = 0; k < s_.params.size(); ++k) v[k] = p(static_cast<int>(k));
        return s_.active(v, i);
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        colour_.prepare();
        eq_.prepare(fs);
        hum_.prepare(fs);
        cut_[0].lowPass = false;
        cut_[1].lowPass = true;
        fade_ = static_cast<float>(1.0 / (0.02 * fs));
        reset();
    }
    void reset() noexcept override {
        colour_.clear((s_.vintage < 0 || p(s_.vintage) >= 0.5f) && s_.colour > 0.0f);
        hum_.clear();
        dc_[0] = dc_[1] = 0.0f;
        load();
        eq_.clear();
        for (auto& c : cut_) c.clear();
        out_.prepare(fs_, 20, outGain());
        clearPeaks();
        if (s_.clear >= 0) push_ = p(s_.clear);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (s_.clear >= 0 && p(s_.clear) != push_) {
            push_ = p(s_.clear);
            clearPeaks();
        }
        load();
        const float pol = s_.phase >= 0 && p(s_.phase) >= 0.5f ? -1.0f : 1.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            L[i] *= pol;
            R[i] *= pol;
        }
        // the colour: lopsided, so it's mostly the 2nd harmonic; quiet signals come out as they went in
        const bool colourOn = s_.vintage < 0 || p(s_.vintage) >= 0.5f;
        const float a = s_.colour;
        colour_.run(L, R, n, colourOn && a > 0.0f, fade_, [a](float x) {
            const float u = a * x;
            return std::tanh(u + 0.15f * u * u) / a;
        });
        const int mains = s_.hum >= 0 ? pick(p(s_.hum), 2) : (colourOn && s_.vintage >= 0 ? 3 : 0);
        const float wOut = outGain();
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            eq_.step();
            for (auto& c : cut_) c.step(fade_);
            float l = cut_[1].run(0, cut_[0].run(0, L[i] - dc_[0])), r = cut_[1].run(1, cut_[0].run(1, R[i] - dc_[1]));
            dc_[0] += (L[i] - dc_[0]) * dcK_;   // the colour's lopsided curve adds a little DC: taken out
            dc_[1] += (R[i] - dc_[1]) * dcK_;
            l = eq_.run(0, l);
            r = eq_.run(1, r);
            const float g = out_.next(wOut);
            l *= g;
            r *= g;
            hum_.tick(mains, 0.5f, l, r);
            L[i] = l;
            R[i] = r;
            pk = std::max({pk, std::fabs(l), std::fabs(r)});
        }
        if (s_.meters.empty()) return;
        peak_ = std::max(peak_, pk);
        if (pk > 1.0f) clips_ += 1.0f;
        const float pdb = std::max(dbOf(peak_), -100.0f);
        publishMeter(0, pdb);
        publishMeter(1, peak_ > 1e-6f ? std::clamp(-0.1f - pdb, -99.0f, 99.0f) : 0.0f);
        publishMeter(2, clips_);
    }

private:
    float outGain() const noexcept { return hc::fx::dbToGain(p(s_.output)); }
    void clearPeaks() noexcept {
        peak_ = 0.0f;
        clips_ = 0.0f;
        if (s_.meters.empty()) return;
        publishMeter(0, -100.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, 0.0f);
    }
    void load() noexcept {
        float v[hc::fx::Effect::kMaxParams];
        for (std::size_t k = 0; k < s_.params.size(); ++k) v[k] = p(static_cast<int>(k));
        Plan pl = s_.plan(v);
        if (p(s_.eqIn) < 0.5f) pl = Plan{};   // EQ out: the curves go flat (the colour stays)
        cut_[0].want(fs_, pl.lowCutHz);
        cut_[1].want(fs_, pl.highCutHz);
        eq_.set(0, pl.lowCutHz > 0.0f ? BandSet{Shape::LowCut, pl.lowCutHz, 0.0f, kCutQ} : BandSet{});
        eq_.set(1, pl.highCutHz > 0.0f ? BandSet{Shape::HighCut, pl.highCutHz, 0.0f, kCutQ} : BandSet{});
        for (int b = 0; b < 4; ++b) eq_.set(2 + b, pl.band[b]);
    }

    const Spec& s_;
    double fs_ = 48000.0;
    tone::Drive4 colour_;
    eqkit::IirBands<6> eq_;
    FirstOrder cut_[2];
    arsenal::Hum hum_;
    hc::fx::Smoothed out_;
    float fade_ = 0.001f, dc_[2]{}, dcK_ = 2.6e-4f;
    float peak_ = 0.0f, clips_ = 0.0f, push_ = 0.0f;
};

const char* const kInOut[] = {"OUT", "IN"};
const char* const kPushN[] = {"A", "B"};   // any change = a push
const hc::fx::MeterInfo kPeakMeters[] = {{"PEAK", "dBFS", -60.0f, 6.0f}, {"TO TOP", "dB", -24.0f, 60.0f}, {"CLIPS", "", 0.0f, 999.0f}};
bool always(const float*, int) noexcept { return true; }

// ---- FOUNDRY -------------------------------------------------------------------------------
namespace fo {
using namespace foundry;
const char* const kCutN[] = {"OFF", "45", "50", "70", "80", "160", "300", "360"};
const char* const kLowN[] = {"OFF", "35", "60", "100", "110", "220"};
const char* const kMidN[] = {"OFF", "360", "700", "1.2K", "1.6K", "3.2K", "3.8K", "4.8K", "7K", "7.2K"};
const char* const kHighN[] = {"OFF", "10K", "12K"};
const ParamInfo kParams[kCount] = {
    {"lowcut", "Low Cut", "Hz", 0, 7, 0, Taper::Choice, kCutN},
    {"lowfreq", "Lows At", "Hz", 0, 5, 2, Taper::Choice, kLowN},
    {"lowgain", "Lows", "dB", -18.0f, 18.0f, 0.0f},
    {"midfreq", "Middle At", "Hz", 0, 9, 4, Taper::Choice, kMidN},
    {"midgain", "Middle", "dB", -18.0f, 18.0f, 0.0f},
    {"highfreq", "Highs At", "Hz", 0, 2, 2, Taper::Choice, kHighN},
    {"highgain", "Highs", "dB", -18.0f, 18.0f, 0.0f},
    {"eqin", "EQ", "", 0, 1, 1, Taper::Toggle, kInOut},
    {"phase", "Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"vintage", "Vintage", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"clear", "Clear", "", 0, 1, 0, Taper::Toggle, kPushN},
};
constexpr float kShelfQ = 0.7071f, kMidQ = 1.4f;
Plan plan(const float* v) noexcept {
    Plan pl;
    pl.lowCutHz = kCutHz[pick(v[LowCut], 7)];
    if (const float hz = kLowHz[pick(v[LowFreq], 5)]; hz > 0.0f) pl.band[0] = {Shape::LowShelf, hz, v[LowGain], kShelfQ};
    if (const float hz = kMidHz[pick(v[MidFreq], 9)]; hz > 0.0f) pl.band[1] = {Shape::Bell, hz, v[MidGain], kMidQ};
    if (const float hz = kHighHz[pick(v[HighFreq], 2)]; hz > 0.0f) pl.band[2] = {Shape::HighShelf, hz, v[HighGain], kShelfQ};
    return pl;
}
bool active(const float* v, int i) noexcept {
    if (i == LowGain) return v[LowFreq] >= 0.5f;
    if (i == MidGain) return v[MidFreq] >= 0.5f;
    if (i == HighGain) return v[HighFreq] >= 0.5f;
    return true;
}
const Spec kSpec{kParams, kPeakMeters, &plan, &active, EqIn, Phase, Output, Vintage, Clear, -1, 1.2f};
} // namespace fo

// ---- BOARDROOM -----------------------------------------------------------------------------
namespace bd {
using namespace boardroom;
const char* const kHpN[] = {"OFF", "27", "47", "82", "150", "270"};
const char* const kLpN[] = {"OFF", "18K", "12K", "8.7K", "5.6K", "3.9K"};
const char* const kLowN[] = {"OFF", "33", "56", "100", "180", "330"};
const char* const kLoMidN[] = {"OFF", "220", "270", "330", "390", "470", "560", "680", "820", "1K", "1.2K"};
const char* const kHiMidN[] = {"OFF", "1.5K", "1.8K", "2.2K", "2.7K", "3.3K", "3.9K", "4.7K", "5.6K", "8.2K"};
const char* const kHighN[] = {"3.3K", "4.7K", "6.8K", "10K", "15K"};
const char* const kShapeN[] = {"SHELF", "BELL"};
const char* const kWidthN[] = {"WIDE", "TIGHT"};
const ParamInfo kParams[kCount] = {
    {"highpass", "High-Pass", "Hz", 0, 5, 0, Taper::Choice, kHpN},
    {"lowpass", "Low-Pass", "Hz", 0, 5, 0, Taper::Choice, kLpN},
    {"lowfreq", "Lows At", "Hz", 0, 5, 3, Taper::Choice, kLowN},
    {"lowgain", "Lows", "dB", -18.0f, 18.0f, 0.0f},
    {"lowbell", "Lows Shape", "", 0, 1, 0, Taper::Toggle, kShapeN},
    {"lomidfreq", "Low Mids At", "Hz", 0, 10, 4, Taper::Choice, kLoMidN},
    {"lomidgain", "Low Mids", "dB", -18.0f, 18.0f, 0.0f},
    {"lomidtight", "Low Mids Width", "", 0, 1, 0, Taper::Toggle, kWidthN},
    {"himidfreq", "High Mids At", "Hz", 0, 9, 5, Taper::Choice, kHiMidN},
    {"himidgain", "High Mids", "dB", -18.0f, 18.0f, 0.0f},
    {"himidtight", "High Mids Width", "", 0, 1, 0, Taper::Toggle, kWidthN},
    {"highfreq", "Highs At", "Hz", 0, 4, 3, Taper::Choice, kHighN},
    {"highgain", "Highs", "dB", -18.0f, 18.0f, 0.0f},
    {"highbell", "Highs Shape", "", 0, 1, 0, Taper::Toggle, kShapeN},
    {"eqin", "EQ", "", 0, 1, 1, Taper::Toggle, kInOut},
    {"phase", "Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"vintage", "Vintage", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"clear", "Clear", "", 0, 1, 0, Taper::Toggle, kPushN},
};
constexpr float kShelfQ = 0.7071f, kEdgeBellQ = 0.9f, kWideQ = 0.8f, kTightQ = 2.5f, kWideReach = 12.0f / 18.0f;
Plan plan(const float* v) noexcept {
    Plan pl;
    pl.lowCutHz = kHpHz[pick(v[HighPass], 5)];
    pl.highCutHz = kLpHz[pick(v[LowPass], 5)];
    const bool lowBell = v[LowBell] >= 0.5f, highBell = v[HighBell] >= 0.5f;
    if (const float hz = kLowHz[pick(v[LowFreq], 5)]; hz > 0.0f)
        pl.band[0] = {lowBell ? Shape::Bell : Shape::LowShelf, hz, v[LowGain], lowBell ? kEdgeBellQ : kShelfQ};
    if (const float hz = kLoMidHz[pick(v[LoMidFreq], 10)]; hz > 0.0f) {
        const bool t = v[LoMidTight] >= 0.5f;
        pl.band[1] = {Shape::Bell, hz, v[LoMidGain] * (t ? 1.0f : kWideReach), t ? kTightQ : kWideQ};
    }
    if (const float hz = kHiMidHz[pick(v[HiMidFreq], 9)]; hz > 0.0f) {
        const bool t = v[HiMidTight] >= 0.5f;
        pl.band[2] = {Shape::Bell, hz, v[HiMidGain] * (t ? 1.0f : kWideReach), t ? kTightQ : kWideQ};
    }
    pl.band[3] = {highBell ? Shape::Bell : Shape::HighShelf, kHighHz[pick(v[HighFreq], 4)], v[HighGain], highBell ? kEdgeBellQ : kShelfQ};
    return pl;
}
bool active(const float* v, int i) noexcept {
    if (i == LowGain || i == LowBell) return v[LowFreq] >= 0.5f;
    if (i == LoMidGain || i == LoMidTight) return v[LoMidFreq] >= 0.5f;
    if (i == HiMidGain || i == HiMidTight) return v[HiMidFreq] >= 0.5f;
    return true;
}
const Spec kSpec{kParams, kPeakMeters, &plan, &active, EqIn, Phase, Output, Vintage, Clear, -1, 1.2f};
} // namespace bd

// ---- MIDTOWN -------------------------------------------------------------------------------
namespace md {
using namespace midtown;
const char* const kLowN[] = {"200", "300", "500", "700", "1K"};
const char* const kDipN[] = {"200", "300", "500", "700", "1K", "1.5K", "2K", "3K", "4K", "5K", "7K"};
const char* const kHighN[] = {"1.5K", "2K", "3K", "4K", "5K"};
const ParamInfo kParams[kCount] = {
    {"lowfreq", "Low Peak At", "Hz", 0, 4, 2, Taper::Choice, kLowN},
    {"lowpeak", "Low Peak", "/10", 0.0f, 10.0f, 0.0f},
    {"dipfreq", "Dip At", "Hz", 0, 10, 4, Taper::Choice, kDipN},
    {"dip", "Dip", "/10", 0.0f, 10.0f, 0.0f},
    {"highfreq", "High Peak At", "Hz", 0, 4, 2, Taper::Choice, kHighN},
    {"highpeak", "High Peak", "/10", 0.0f, 10.0f, 0.0f},
    {"eqin", "EQ", "", 0, 1, 1, Taper::Toggle, kInOut},
    {"hum", "Hum", "", 0, 2, 0, Taper::Choice, kMainsN},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr float kLowQ = 1.0f, kDipQ = 1.2f, kHighQ = 1.4f;
Plan plan(const float* v) noexcept {
    Plan pl;
    if (v[LowPeak] > 0.0f) pl.band[0] = {Shape::Bell, kLowHz[pick(v[LowFreq], 4)], v[LowPeak], kLowQ};             // 0..+10 dB
    if (v[Dip] > 0.0f) pl.band[1] = {Shape::Bell, kDipHz[pick(v[DipFreq], 10)], -v[Dip], kDipQ};                     // 0..-10 dB
    if (v[HighPeak] > 0.0f) pl.band[2] = {Shape::Bell, kHighHz[pick(v[HighFreq], 4)], 0.8f * v[HighPeak], kHighQ};  // 0..+8 dB
    return pl;
}
const Spec kSpec{kParams, {}, &plan, &always, EqIn, -1, Output, -1, -1, midtown::Hum, 0.8f};
} // namespace md

} // namespace

namespace midtown {
namespace {
double curveDb(const float* v, double fs, double hz) noexcept { return v[EqIn] < 0.5f ? 0.0 : planDb(md::plan(v), fs, hz); }
int dotsOf(const float* v, double* hz, int max) noexcept {
    int n = 0;
    if (max > 0) hz[n++] = kLowHz[pick(v[LowFreq], 4)];
    if (max > 1) hz[n++] = kDipHz[pick(v[DipFreq], 10)];
    if (max > 2) hz[n++] = kHighHz[pick(v[HighFreq], 4)];
    return n;
}
} // namespace
const CurveInfo kCurve{&curveDb, &dotsOf, 12.0f};
} // namespace midtown

std::unique_ptr<hc::fx::Effect> makeFoundry() { return std::make_unique<Engine>(fo::kSpec); }
std::unique_ptr<hc::fx::Effect> makeBoardroom() { return std::make_unique<Engine>(bd::kSpec); }
std::unique_ptr<hc::fx::Effect> makeMidtown() { return std::make_unique<Engine>(md::kSpec); }

} // namespace hem::rack
