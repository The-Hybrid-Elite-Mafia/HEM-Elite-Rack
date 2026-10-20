// STRAIGHT SHOOTER (six bands) and BOTTOM LINE (three low bands) — linear-phase EQs (see ToneShop.hpp).
//
// The curve (the same one the faceplate draws) is turned into a symmetrical filter by eqkit::LinPhase;
// after it come GAIN and TRIM (a plain gain), then the 24-bit dither when it is on.
#include "hem/rack/ToneShop.hpp"

#include "hem/rack/Spectrum.hpp"

#include "EqKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace lineq;
using eqkit::Shape;

const char* const kKindN[] = {"BELL", "LO CUT", "LO SHELF", "HI SHELF", "HI CUT", "LO CUT RES", "LO SHELF RES", "HI SHELF RES", "HI CUT RES"};
const char* const kFirstN[] = {"LO CUT", "LO SHELF", "BELL", "HI SHELF", "HI CUT"};
const char* const kFilterN[] = {"STANDARD", "EXACT", "SMOOTH"};
const char* const kScaleN[] = {"12 dB", "30 dB"};
const char* const kPushN[] = {"A", "B"};   // any change = a push

constexpr Shape kKindShape[kKinds] = {Shape::Bell, Shape::VLowCut, Shape::VLowShelf, Shape::VHighShelf, Shape::VHighCut,
                                      Shape::LowCut, Shape::LowShelf, Shape::HighShelf, Shape::HighCut};
constexpr Shape kFirstShape[kFirstKinds] = {Shape::LowCut, Shape::LowShelf, Shape::Bell, Shape::HighShelf, Shape::HighCut};

#define HEM_BAND(n, types, kinds, lo, hi, hz, ty, q, qlo, qhi, on)                                         \
    {"b" #n "on", "Band " #n " On", "", 0, 1, on, Taper::Toggle, kOnOff},                                  \
    {"b" #n "type", "Band " #n " Type", "", 0, kinds - 1, ty, Taper::Choice, types},                       \
    {"b" #n "freq", "Band " #n " Freq", "Hz", lo, hi, hz, Taper::Log},                                     \
    {"b" #n "gain", "Band " #n " Gain", "dB", -30.0f, 30.0f, 0.0f},                                        \
    {"b" #n "q", "Band " #n " Q", "", qlo, qhi, q, Taper::Log}
#define HEM_GLOBALS                                                                                          \
    {"gain", "Gain", "dB", -30.0f, 12.0f, 0.0f},                                                             \
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},                                                    \
    {"filter", "Filter", "", 0, 2, 0, Taper::Choice, kFilterN},                                              \
    {"dither", "Dither", "", 0, 1, 0, Taper::Toggle, kOnOff},                                                \
    {"scale", "Scale", "", 0, 1, 1, Taper::Choice, kScaleN}

const ParamInfo kStraight[straight::kCount] = {
    HEM_BAND(1, kFirstN, kFirstKinds, 22.0f, 1000.0f, 96.0f, FLowShelf, 1.2f, 0.6f, 2.0f, 1),
    HEM_BAND(2, kKindN, kKinds, 258.0f, 21900.0f, 258.0f, Bell, 1.0f, 0.26f, 6.5f, 1),
    HEM_BAND(3, kKindN, kKinds, 258.0f, 21900.0f, 689.0f, Bell, 1.0f, 0.26f, 6.5f, 1),
    HEM_BAND(4, kKindN, kKinds, 258.0f, 21900.0f, 1809.0f, Bell, 1.0f, 0.26f, 6.5f, 1),
    HEM_BAND(5, kKindN, kKinds, 258.0f, 21900.0f, 4479.0f, Bell, 1.0f, 0.26f, 6.5f, 1),
    HEM_BAND(6, kKindN, kKinds, 258.0f, 21900.0f, 11025.0f, RHighShelf, 0.9f, 0.26f, 6.5f, 1),
    HEM_GLOBALS,
};
const ParamInfo kBottom[bottomline::kCount] = {
    HEM_BAND(1, kKindN, kKinds, 11.0f, 600.0f, 32.0f, VLowCut, 0.9f, 0.26f, 6.5f, 0),
    HEM_BAND(2, kKindN, kKinds, 11.0f, 600.0f, 139.0f, Bell, 0.9f, 0.26f, 6.5f, 1),
    HEM_BAND(3, kKindN, kKinds, 11.0f, 600.0f, 600.0f, VHighShelf, 2.0f, 0.26f, 6.5f, 1),
    HEM_GLOBALS,
};
#undef HEM_BAND
#undef HEM_GLOBALS
const hc::fx::MeterInfo kMeters[] = {{"OUTPUT", "dBFS", -60.0f, 3.0f}};

// What makes the two different.
struct StraightCfg {
    static constexpr int kBands = straight::kBands, kCount = straight::kCount;
    static constexpr std::uint32_t kParts = 16, kDesign = 16384;
    static constexpr const ParamInfo* params = kStraight;
    static Shape shape(int b, int type) noexcept {
        return b == 0 ? kFirstShape[std::clamp(type, 0, kFirstKinds - 1)] : kKindShape[std::clamp(type, 0, kKinds - 1)];
    }
    static float gain(Shape, float g) noexcept { return g; }
    static float freq(Shape, float hz) noexcept { return hz; }
};
struct BottomCfg {
    static constexpr int kBands = bottomline::kBands, kCount = bottomline::kCount;
    static constexpr std::uint32_t kParts = 32, kDesign = 32768;
    static constexpr const ParamInfo* params = kBottom;
    static Shape shape(int, int type) noexcept { return kKindShape[std::clamp(type, 0, kKinds - 1)]; }
    static float gain(Shape s, float g) noexcept { return s == Shape::LowShelf ? std::min(g, 10.0f) : g; }
    static float freq(Shape s, float hz) noexcept { return s == Shape::Bell ? std::max(hz, 32.0f) : hz; }
};

template <class Cfg>
double curveOf(const float* v, double fs, double hz) noexcept {
    double db = 0.0;
    for (int b = 0; b < Cfg::kBands; ++b) {
        if (v[band(b, On)] < 0.5f) continue;
        const Shape s = Cfg::shape(b, static_cast<int>(std::lround(v[band(b, Type)])));
        db += eqkit::magDb(s, Cfg::freq(s, v[band(b, Freq)]), Cfg::gain(s, v[band(b, Gain)]), v[band(b, Q)], hz, fs * 0.45);
    }
    return db;
}
template <class Cfg>
int dotsOf(const float* v, double* hz, int max) noexcept {
    int n = 0;
    for (int b = 0; b < Cfg::kBands && n < max; ++b)
        if (v[band(b, On)] >= 0.5f) {
            const Shape s = Cfg::shape(b, static_cast<int>(std::lround(v[band(b, Type)])));
            hz[n++] = Cfg::freq(s, v[band(b, Freq)]);
        }
    return n;
}

template <class Cfg>
class Impl final : public hc::fx::Effect, public SpectrumSource, public eqkit::LinSource {
    static constexpr int kGain = Cfg::kBands * kStride, kTrim = kGain + 1, kFilter = kGain + 2, kDither = kGain + 3;
    using Engine = eqkit::LinPhase<Cfg::kParts, Cfg::kDesign>;

public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return {Cfg::params, static_cast<std::size_t>(Cfg::kCount)}; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return Engine::kLatency; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    bool paramActive(int i) const noexcept override {
        if (i < kGain && i % kStride != On) return p(i - i % kStride + On) >= 0.5f;
        return true;
    }

    // eqkit::LinSource
    bool takeChange() noexcept override {
        std::array<float, Cfg::kCount> now{};
        for (int i = 0; i <= kFilter; ++i) now[static_cast<std::size_t>(i)] = i == kGain || i == kTrim ? 0.0f : p(i);
        if (now == designed_) return false;
        designed_ = now;
        return true;
    }
    double magnitudeDb(double hz) const noexcept override { return curveOf<Cfg>(designed_.data(), fs_, hz); }
    int window() const noexcept override { return static_cast<int>(std::lround(designed_[static_cast<std::size_t>(kFilter)])); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        designed_.fill(-1.0f);
        eng_.prepare(fs, this);
        idr_.prepare(fs);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        eng_.reset();
        idr_.clear();
        push_ = p(kTrim);
        most_ = 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(kGain)));
        pk_.clear();
        before_.reset();
        after_.reset();
        publishMeter(0, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kTrim) != push_) {   // TRIM: the loudest peak since the last push onto 0 dBFS (at most +12 dB)
            push_ = p(kTrim);
            if (most_ > 1e-6f) trimDb_ = std::clamp(trimDb_ - dbOf(most_), -60.0f, 12.0f);
            most_ = 0.0f;
        }
        idr_.set(p(kDither) >= 0.5f ? 24 : 0, 0, 3, 64.0f);
        before_.push(L, R, n);
        eng_.process(L, R, n);
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = out_.next(hc::fx::dbToGain(p(kGain) + trimDb_));
            L[i] = idr_.tick(0, L[i] * g);
            R[i] = idr_.tick(1, R[i] * g);
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        most_ = std::max(most_, pk);
        after_.push(L, R, n);
        publishMeter(0, pk_.push(dbOf(pk), n, fs_));
    }

private:
    double fs_ = 48000.0;
    Engine eng_;
    std::array<float, Cfg::kCount> designed_{};
    maxim::Idr idr_;
    hc::fx::Smoothed out_;
    float trimDb_ = 0.0f, push_ = 0.0f, most_ = 0.0f;   // trimDb_: only a push changes it (a reset keeps it)
    maxim::PeakHold pk_;
    SpectrumMeter before_, after_;
};

} // namespace

namespace straight {
double curveDb(const float* v, double fs, double hz) noexcept { return curveOf<StraightCfg>(v, fs, hz); }
const CurveInfo kCurve{&curveDb, &dotsOf<StraightCfg>, 30.0f, nullptr, Scale, -1};
} // namespace straight
namespace bottomline {
double curveDb(const float* v, double fs, double hz) noexcept { return curveOf<BottomCfg>(v, fs, hz); }
const CurveInfo kCurve{&curveDb, &dotsOf<BottomCfg>, 30.0f, nullptr, Scale, -1};
} // namespace bottomline

std::unique_ptr<hc::fx::Effect> makeStraightShooter() { return std::make_unique<Impl<StraightCfg>>(); }
std::unique_ptr<hc::fx::Effect> makeBottomLine() { return std::make_unique<Impl<BottomCfg>>(); }

} // namespace hem::rack
