// TEN TOES, FOUR CORNERS, SMOOTH TALKER — minimum-phase band EQs with a left and a right set (see ToneShop.hpp).
//
// One engine for all three: per side, input gain and phase (where the plate has them), the bands
// (eqkit::IirBands: eased settings, shape changes crossfaded), then the side's output gain and TRIM.
// LINK: the right side runs the left set. The drawn curves are the biquads the audio runs.
#include "hem/rack/ToneShop.hpp"

#include "hem/rack/Spectrum.hpp"

#include "EqKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace lineq;
using eqkit::Shape;

const char* const kPushN[] = {"A", "B"};   // any change = a push

// A TYPE entry: the shape and the Q range it really uses.
struct TypeDef {
    Shape shape;
    float qLo, qHi;
};

// the plain value's gain; the knob's bottom (`floor`) is off
inline float gainOrOff(float db, float floor) noexcept { return db <= floor + 0.01f ? 0.0f : hc::fx::dbToGain(db); }

// ---- TEN TOES ----------------------------------------------------------------------------------
namespace tt {
using namespace tentoes;
const char* const kKindN[] = {"BELL", "LO SHELF", "HI SHELF", "HI CUT", "LO CUT", "TIGHT BELL"};
#define HEM_BAND(s, n, hz)                                                                                \
    {s "b" #n "on", s " Band " #n " On", "", 0, 1, 1, Taper::Toggle, kOnOff},                             \
    {s "b" #n "type", s " Band " #n " Type", "", 0, tentoes::kKinds - 1, 0, Taper::Choice, kKindN},                \
    {s "b" #n "freq", s " Band " #n " Freq", "Hz", 16.0f, 21357.0f, hz, Taper::Log},                     \
    {s "b" #n "gain", s " Band " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                                  \
    {s "b" #n "q", s " Band " #n " Q", "", 0.5f, 100.0f, 1.0f, Taper::Log}
#define HEM_SIDE(s)                                                                                       \
    HEM_BAND(s, 1, 31.0f), HEM_BAND(s, 2, 62.0f), HEM_BAND(s, 3, 125.0f), HEM_BAND(s, 4, 250.0f),         \
    HEM_BAND(s, 5, 500.0f), HEM_BAND(s, 6, 1000.0f), HEM_BAND(s, 7, 2000.0f), HEM_BAND(s, 8, 4000.0f),    \
    HEM_BAND(s, 9, 8000.0f), HEM_BAND(s, 10, 16000.0f)
const ParamInfo kParams[kCount] = {
    HEM_SIDE("L"), HEM_SIDE("R"),
    {"link", "Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"inputl", "Input L", "dB", -60.0f, 0.0f, 0.0f},
    {"inputr", "Input R", "dB", -60.0f, 0.0f, 0.0f},
    {"phasel", "Phase L", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"phaser", "Phase R", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"outputl", "Output L", "dB", -24.0f, 12.0f, 0.0f},
    {"outputr", "Output R", "dB", -24.0f, 12.0f, 0.0f},
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},
};
#undef HEM_SIDE
#undef HEM_BAND
struct Cfg {
    static constexpr int kBands = tentoes::kBands, kSide = tentoes::kSide, kCount = tentoes::kCount, kLink = Link;
    static constexpr int kInL = InputL, kInR = InputR, kPhL = PhaseL, kPhR = PhaseR, kOutL = OutputL, kOutR = OutputR, kTrim = Trim;
    static constexpr float kInFloor = -60.0f, kOutFloor = -1000.0f, kTrimLo = -12.0f;
    static constexpr const ParamInfo* params = kParams;
    static TypeDef type(int, int t) noexcept {
        constexpr TypeDef k[tentoes::kKinds] = {{Shape::Bell, 0.5f, 100.0f}, {Shape::LowShelf, 0.5f, 4.0f}, {Shape::HighShelf, 0.5f, 4.0f},
                                       {Shape::HighCut, 0.5f, 4.0f}, {Shape::LowCut, 0.5f, 4.0f}, {Shape::PropBell, 0.5f, 100.0f}};
        return k[std::clamp(t, 0, tentoes::kKinds - 1)];
    }
};
} // namespace tt

// ---- FOUR CORNERS ------------------------------------------------------------------------------
namespace fc {
using namespace fourcorners;
const char* const kEdgeN[] = {"BELL", "LO SHELF", "HI SHELF", "LO SHELF BUMP", "HI SHELF BUMP", "LO CUT", "HI CUT"};
const char* const kMidN[] = {"BELL", "LO SHELF", "HI SHELF"};
#define HEM_BAND(s, n, names, kinds, hz, ty)                                                              \
    {s "b" #n "on", s " Filter " #n " On", "", 0, 1, 1, Taper::Toggle, kOnOff},                           \
    {s "b" #n "type", s " Filter " #n " Type", "", 0, kinds - 1, ty, Taper::Choice, names},               \
    {s "b" #n "freq", s " Filter " #n " Freq", "Hz", 16.0f, 21357.0f, hz, Taper::Log},                   \
    {s "b" #n "gain", s " Filter " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                                \
    {s "b" #n "q", s " Filter " #n " Q", "", 0.05f, 6.5f, 0.8f, Taper::Log}
#define HEM_SIDE(s)                                                                                       \
    HEM_BAND(s, 1, kEdgeN, fourcorners::kKinds, 92.0f, LowShelf), HEM_BAND(s, 2, kMidN, 3, 351.0f, fourcorners::Bell),             \
    HEM_BAND(s, 3, kMidN, 3, 2890.0f, fourcorners::Bell), HEM_BAND(s, 4, kEdgeN, fourcorners::kKinds, 10992.0f, HighShelf)
const ParamInfo kParams[kCount] = {
    HEM_SIDE("L"), HEM_SIDE("R"),
    {"link", "Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"gainl", "Gain L", "dB", -12.0f, 12.0f, 0.0f},
    {"gainr", "Gain R", "dB", -12.0f, 12.0f, 0.0f},
};
#undef HEM_SIDE
#undef HEM_BAND
struct Cfg {
    static constexpr int kBands = fourcorners::kBands, kSide = fourcorners::kSide, kCount = fourcorners::kCount, kLink = Link;
    static constexpr int kInL = -1, kInR = -1, kPhL = -1, kPhR = -1, kOutL = GainL, kOutR = GainR, kTrim = -1;
    static constexpr float kInFloor = -1000.0f, kOutFloor = -1000.0f, kTrimLo = 0.0f;
    static constexpr const ParamInfo* params = kParams;
    static TypeDef type(int, int t) noexcept {
        constexpr TypeDef k[fourcorners::kKinds] = {{Shape::Bell, 0.05f, 6.5f},       {Shape::LowShelf, 0.05f, 0.71f}, {Shape::HighShelf, 0.05f, 0.71f},
                                       {Shape::LowShelf, 0.05f, 6.5f},   {Shape::HighShelf, 0.05f, 6.5f}, {Shape::LowCut, 0.05f, 6.5f},
                                       {Shape::HighCut, 0.05f, 6.5f}};
        return k[std::clamp(t, 0, fourcorners::kKinds - 1)];
    }
};
} // namespace fc

// ---- SMOOTH TALKER -----------------------------------------------------------------------------
namespace sx {
using namespace smoothtalker;
const char* const kFirstN[] = {"LO CUT", "LO SHELF BUMP", "BELL"};
const char* const kLowN[] = {"LO SHELF", "BELL"};
const char* const kHighN[] = {"HI SHELF", "BELL"};
const char* const kLastN[] = {"HI CUT", "HI SHELF BUMP", "BELL"};
#define HEM_BAND(s, n, names, kinds, hz, ty, q, on)                                                       \
    {s "b" #n "on", s " Band " #n " On", "", 0, 1, on, Taper::Toggle, kOnOff},                            \
    {s "b" #n "type", s " Band " #n " Type", "", 0, kinds - 1, ty, Taper::Choice, names},                 \
    {s "b" #n "freq", s " Band " #n " Freq", "Hz", 16.0f, 21357.0f, hz, Taper::Log},                     \
    {s "b" #n "gain", s " Band " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                                  \
    {s "b" #n "q", s " Band " #n " Q", "", 0.26f, 6.5f, q, Taper::Log}
#define HEM_SIDE(s)                                                                                       \
    HEM_BAND(s, 1, kFirstN, 3, 30.0f, 0, 0.71f, 0), HEM_BAND(s, 2, kLowN, 2, 100.0f, 0, 0.71f, 1),       \
    HEM_BAND(s, 3, kLowN, 2, 400.0f, 1, 1.0f, 1), HEM_BAND(s, 4, kHighN, 2, 2000.0f, 1, 1.0f, 1),        \
    HEM_BAND(s, 5, kHighN, 2, 8000.0f, 0, 0.71f, 1), HEM_BAND(s, 6, kLastN, 3, 18000.0f, 0, 0.71f, 0)
const ParamInfo kParams[kCount] = {
    HEM_SIDE("L"), HEM_SIDE("R"),
    {"link", "Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"spectrum", "Spectrum", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -60.0f, 12.0f, 0.0f},
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},
};
#undef HEM_SIDE
#undef HEM_BAND
struct Cfg {
    static constexpr int kBands = smoothtalker::kBands, kSide = smoothtalker::kSide, kCount = smoothtalker::kCount, kLink = Link;
    static constexpr int kInL = -1, kInR = -1, kPhL = -1, kPhR = -1, kOutL = Output, kOutR = Output, kTrim = Trim;
    static constexpr float kInFloor = -1000.0f, kOutFloor = -60.0f, kTrimLo = -60.0f;
    static constexpr const ParamInfo* params = kParams;
    static TypeDef type(int b, int t) noexcept {
        constexpr TypeDef bell{Shape::Bell, 0.26f, 6.5f};
        if (b == 0) {
            constexpr TypeDef k[3] = {{Shape::LowCut, 0.71f, 1.41f}, {Shape::LowShelf, 0.71f, 1.41f}, bell};
            return k[std::clamp(t, 0, 2)];
        }
        if (b == kBands - 1) {
            constexpr TypeDef k[3] = {{Shape::HighCut, 0.71f, 1.41f}, {Shape::HighShelf, 0.71f, 1.41f}, bell};
            return k[std::clamp(t, 0, 2)];
        }
        // the plain shelves: no bump
        const TypeDef shelf{b < 3 ? Shape::LowShelf : Shape::HighShelf, 0.5f, 0.71f};
        return t >= 1 ? bell : shelf;
    }
};
} // namespace sx

// One band's settings from plain values (side 0 left, 1 right).
template <class Cfg>
eqkit::BandSet bandOf(const float* v, int side, int b) noexcept {
    const int o = side * Cfg::kSide;
    if (v[o + band(b, On)] < 0.5f) return {};
    const TypeDef t = Cfg::type(b, static_cast<int>(std::lround(v[o + band(b, Type)])));
    return {t.shape, v[o + band(b, Freq)], v[o + band(b, Gain)], std::clamp(v[o + band(b, Q)], t.qLo, t.qHi)};
}
template <class Cfg>
int sideOf(const float* v, int side) noexcept { return side == 1 && v[Cfg::kLink] < 0.5f ? 1 : 0; }

template <class Cfg>
double sideDb(const float* v, int side, double fs, double hz) noexcept {
    const int s = sideOf<Cfg>(v, side);
    double db = 0.0;
    for (int b = 0; b < Cfg::kBands; ++b) {
        const eqkit::BandSet bs = bandOf<Cfg>(v, s, b);
        C c[1];
        if (eqkit::biquadFor(bs.shape, fs, bs.hz, bs.gainDb, bs.q, c) == 1) db += eqkit::iirDb(c[0], fs, hz);
    }
    return db;
}
template <class Cfg>
int dotsOf(const float* v, double* hz, int max) noexcept {
    int n = 0;
    for (int b = 0; b < Cfg::kBands && n < max; ++b)
        if (v[band(b, On)] >= 0.5f) hz[n++] = v[band(b, Freq)];
    return n;
}

template <class Cfg>
class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return {Cfg::params, static_cast<std::size_t>(Cfg::kCount)}; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    bool paramActive(int i) const noexcept override {
        if (i >= 2 * Cfg::kSide) return true;
        if (i >= Cfg::kSide && p(Cfg::kLink) >= 0.5f) return false;   // the right set rests while linked
        const int k = i % kStride, base = i - k;
        return k == On || p(base + On) >= 0.5f;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& s : side_) s.prepare(fs);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        load();
        for (auto& s : side_) s.clear();
        inL_.prepare(fs_, 20, inGain(Cfg::kInL));
        inR_.prepare(fs_, 20, inGain(Cfg::kInR));
        outL_.prepare(fs_, 20, outGain(Cfg::kOutL));
        outR_.prepare(fs_, 20, outGain(Cfg::kOutR));
        push_ = Cfg::kTrim >= 0 ? p(Cfg::kTrim) : 0.0f;
        most_ = 0.0f;
        pkL_.clear();
        pkR_.clear();
        before_.reset();
        after_.reset();
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (Cfg::kTrim >= 0 && p(Cfg::kTrim) != push_) {
            push_ = p(Cfg::kTrim);
            if (most_ > 1e-6f) trimDb_ = std::clamp(trimDb_ - dbOf(most_), Cfg::kTrimLo, 12.0f);
            most_ = 0.0f;
        }
        load();
        before_.push(L, R, n);
        const float phL = Cfg::kPhL >= 0 && p(Cfg::kPhL) >= 0.5f ? -1.0f : 1.0f, phR = Cfg::kPhR >= 0 && p(Cfg::kPhR) >= 0.5f ? -1.0f : 1.0f;
        const float trim = hc::fx::dbToGain(trimDb_);
        const float wInL = inGain(Cfg::kInL), wInR = inGain(Cfg::kInR), wOutL = outGain(Cfg::kOutL) * trim, wOutR = outGain(Cfg::kOutR) * trim;
        float pl = 0.0f, pr = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            side_[0].step();
            side_[1].step();
            const float l = side_[0].run(0, L[i] * inL_.next(wInL) * phL);
            const float r = side_[1].run(0, R[i] * inR_.next(wInR) * phR);
            L[i] = l * outL_.next(wOutL);
            R[i] = r * outR_.next(wOutR);
            pl = std::max(pl, std::fabs(L[i]));
            pr = std::max(pr, std::fabs(R[i]));
        }
        most_ = std::max({most_, pl, pr});
        after_.push(L, R, n);
        publishMeter(0, pkL_.push(dbOf(pl), n, fs_));
        publishMeter(1, pkR_.push(dbOf(pr), n, fs_));
    }

    static constexpr hc::fx::MeterInfo kMeters[] = {{"OUT L", "dBFS", -60.0f, 3.0f}, {"OUT R", "dBFS", -60.0f, 3.0f}};

private:
    // the bands' targets from the parameters
    void load() noexcept {
        float v[Cfg::kCount];
        for (int i = 0; i < Cfg::kCount; ++i) v[i] = p(i);
        for (int s = 0; s < 2; ++s) {
            const int from = sideOf<Cfg>(v, s);
            for (int b = 0; b < Cfg::kBands; ++b) side_[s].set(b, bandOf<Cfg>(v, from, b));
        }
    }
    float inGain(int i) const noexcept { return i < 0 ? 1.0f : gainOrOff(p(i), Cfg::kInFloor); }
    float outGain(int i) const noexcept { return gainOrOff(p(i), Cfg::kOutFloor); }

    double fs_ = 48000.0;
    eqkit::IirBands<Cfg::kBands> side_[2];
    hc::fx::Smoothed inL_, inR_, outL_, outR_;
    float trimDb_ = 0.0f, push_ = 0.0f, most_ = 0.0f;   // trimDb_: only a push changes it (a reset keeps it)
    maxim::PeakHold pkL_, pkR_;
    SpectrumMeter before_, after_;
};

} // namespace

namespace tentoes {
double curveDb(const float* v, double fs, double hz) noexcept { return sideDb<tt::Cfg>(v, 0, fs, hz); }
double curveRightDb(const float* v, double fs, double hz) noexcept { return sideDb<tt::Cfg>(v, 1, fs, hz); }
const CurveInfo kCurve{&curveDb, &dotsOf<tt::Cfg>, 18.0f, &curveRightDb};
} // namespace tentoes
namespace fourcorners {
double curveDb(const float* v, double fs, double hz) noexcept { return sideDb<fc::Cfg>(v, 0, fs, hz); }
double curveRightDb(const float* v, double fs, double hz) noexcept { return sideDb<fc::Cfg>(v, 1, fs, hz); }
const CurveInfo kCurve{&curveDb, &dotsOf<fc::Cfg>, 18.0f, &curveRightDb};
} // namespace fourcorners
namespace smoothtalker {
double curveDb(const float* v, double fs, double hz) noexcept { return sideDb<sx::Cfg>(v, 0, fs, hz); }
double curveRightDb(const float* v, double fs, double hz) noexcept { return sideDb<sx::Cfg>(v, 1, fs, hz); }
const CurveInfo kCurve{&curveDb, &dotsOf<sx::Cfg>, 18.0f, &curveRightDb, -1, Spectrum};
} // namespace smoothtalker

std::unique_ptr<hc::fx::Effect> makeTenToes() { return std::make_unique<Impl<tt::Cfg>>(); }
std::unique_ptr<hc::fx::Effect> makeFourCorners() { return std::make_unique<Impl<fc::Cfg>>(); }
std::unique_ptr<hc::fx::Effect> makeSmoothTalker() { return std::make_unique<Impl<sx::Cfg>>(); }

} // namespace hem::rack
