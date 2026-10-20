// HEAVY HITTER — drum enhancer (see Garage.hpp).
//
// In order: SENSITIVITY, SNAP (a transient shaper: a fast and a slow follower; where the fast one is
// above the slow one a hit is starting, and that moment is lifted by up to 12 dB at SNAP 100), THUMP and
// TREBLE (eqkit bands placed per drum: everything glides), OUTPUT. Zero latency.
#include "hem/rack/Garage.hpp"

#include "EqKit.hpp"
#include "ToneCommon.hpp"

namespace hem::rack {
namespace heavyhitter {
namespace {
using namespace arsenal;
using eqkit::BandSet;
using eqkit::Shape;

const char* const kDrumN[] = {"KICK", "SNARE TOP", "SNARE UNDER", "HATS", "TOMS", "OVERHEADS", "ROOM"};
const char* const kMeterN[] = {"IN", "OUT"};
const ParamInfo kParams[kCount] = {
    {"drum", "Drum", "", 0, kKinds - 1, 0, Taper::Choice, kDrumN},
    {"sensitivity", "Sensitivity", "dB", -18.0f, 18.0f, 0.0f},
    {"thump", "Thump", "dB", -12.0f, 12.0f, 0.0f},
    {"treble", "Treble", "dB", -12.0f, 12.0f, 0.0f},
    {"snap", "Snap", "", 0.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"meter", "Meter", "", 0, 1, 1, Taper::Choice, kMeterN},
};
const hc::fx::MeterInfo kMeters[] = {{"LEVEL", "dBFS", -24.0f, 0.0f}, {"INPUT", "dBFS", -24.0f, 0.0f}};

struct Voice {
    Shape thumpShape;
    float thumpHz, thumpQ;
    Shape trebleShape;
    float trebleHz, trebleQ;
    float slowMs;   // how long a hit's "front" is (the slow follower's attack)
};
constexpr Voice kVoices[kKinds] = {
    {Shape::Bell, 60.0f, 0.9f, Shape::HighShelf, 4000.0f, 0.7071f, 12.0f},       // KICK: the chest, then the beater
    {Shape::Bell, 200.0f, 1.0f, Shape::HighShelf, 6000.0f, 0.7071f, 8.0f},       // SNARE TOP: the body, the crack
    {Shape::Bell, 180.0f, 1.0f, Shape::HighShelf, 8000.0f, 0.7071f, 6.0f},       // SNARE UNDER: the wires
    {Shape::LowShelf, 300.0f, 0.7071f, Shape::HighShelf, 10000.0f, 0.7071f, 3.0f},   // HATS
    {Shape::Bell, 100.0f, 0.9f, Shape::HighShelf, 5000.0f, 0.7071f, 10.0f},      // TOMS
    {Shape::LowShelf, 150.0f, 0.7071f, Shape::HighShelf, 12000.0f, 0.7071f, 6.0f},   // OVERHEADS
    {Shape::LowShelf, 120.0f, 0.7071f, Shape::HighShelf, 8000.0f, 0.7071f, 20.0f},   // ROOM
};
constexpr float kSnapMaxDb = 12.0f;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        eq_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        load();
        eq_.clear();
        fast_ = slow_ = 0.0f;
        in_.prepare(fs_, 20, hc::fx::dbToGain(p(Sensitivity)));
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        snap_.prepare(fs_, 20, p(Snap) * 0.01f);
        pkIn_ = pkOut_ = 0.0f;
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        load();
        const Voice& v = kVoices[kind()];
        const float fAtt = coefMs(fs_, 0.3), fRel = coefMs(fs_, 30.0), sAtt = coefMs(fs_, v.slowMs), sRel = coefMs(fs_, 30.0);
        const float wIn = hc::fx::dbToGain(p(Sensitivity)), wOut = hc::fx::dbToGain(p(Output)), wSnap = p(Snap) * 0.01f;
        float pi = 0.0f, po = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn);
            float l = L[i] * gi, r = R[i] * gi;
            const float x = std::max(std::fabs(l), std::fabs(r));
            pi = std::max(pi, x);
            fast_ = tone::envStep(fast_, x, fAtt, fRel);
            slow_ = tone::envStep(slow_, fast_, sAtt, sRel);   // follows the fast one: steady sounds give no lead
            // the front of a hit: the fast follower is ahead of the slow one
            const float lead = fast_ > 1e-6f && slow_ > 1e-9f ? std::clamp(dbOf(fast_) - dbOf(slow_), 0.0f, kSnapMaxDb) : 0.0f;
            const float g = hc::fx::dbToGain(snap_.next(wSnap) * lead);
            l *= g;
            r *= g;
            eq_.step();
            l = eq_.run(0, l);
            r = eq_.run(1, r);
            const float o = out_.next(wOut);
            l *= o;
            r *= o;
            L[i] = l;
            R[i] = r;
            po = std::max({po, std::fabs(l), std::fabs(r)});
        }
        const float fall = static_cast<float>(std::exp(-static_cast<double>(n) / (0.3 * fs_)));   // the readouts fall back in about 300 ms
        pkIn_ = std::max(pi, pkIn_ * fall);
        pkOut_ = std::max(po, pkOut_ * fall);
        publishMeter(0, std::max(dbOf(p(Meter) < 0.5f ? pkIn_ : pkOut_), -100.0f));
        publishMeter(1, std::max(dbOf(pkIn_), -100.0f));
    }

private:
    int kind() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Drum))), 0, kKinds - 1); }
    void load() noexcept {
        const Voice& v = kVoices[kind()];
        eq_.set(0, p(Thump) != 0.0f ? BandSet{v.thumpShape, v.thumpHz, p(Thump), v.thumpQ} : BandSet{});
        eq_.set(1, p(Treble) != 0.0f ? BandSet{v.trebleShape, v.trebleHz, p(Treble), v.trebleQ} : BandSet{});
    }

    double fs_ = 48000.0;
    eqkit::IirBands<2> eq_;
    float fast_ = 0.0f, slow_ = 0.0f, pkIn_ = 0.0f, pkOut_ = 0.0f;
    hc::fx::Smoothed in_, out_, snap_;
};

} // namespace
} // namespace heavyhitter

std::unique_ptr<hc::fx::Effect> makeHeavyHitter() { return std::make_unique<heavyhitter::Impl>(); }

} // namespace hem::rack
