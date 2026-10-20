// HAYMAKER (one band) and FOUR KINGS (four bands) — transient shapers (see Arsenal.hpp).
//
// A transient is the level jumping faster than DURATION allows: a fast follower (instant up) against
// a slow one that takes DURATION to rise. How far the fast one gets ahead, past what SENS asks for,
// says how much of a transient it is; the gain moves by up to RANGE on it (plus = harder hits, minus =
// softer) and comes back to 0 dB in RELEASE. FOUR KINGS does this per band (Linkwitz-Riley split at
// 150 Hz, 1.2 kHz and 5 kHz; the bands add back up to an all-pass of the input), each band with its
// own RANGE, SENS and ON, the MASTER RANGE and SENS added to all; the low bands let go twice as slowly
// as RELEASE, the top band twice as fast. TRIM (a push) turns the output down by the loudest peak
// since the last push, so that peak lands on 0 dBFS.
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;

const char* const kPushN[] = {"A", "B"};   // any change = a push

// one detector and its gain (dB)
struct Shaper {
    float fast = 0, slow = 0, g = 0;
    void clear() noexcept { fast = slow = g = 0; }
    float tick(float x, float slowUp, float fastDown, float range, float sens, float att, float rel) noexcept {
        fast = x > fast ? x : x + fastDown * (fast - x);
        slow = x + (x > slow ? slowUp : fastDown) * (slow - x);
        const float rise = dbOf(fast) - dbOf(slow);
        const float thr = 6.0f - 0.5f * sens;   // SENS +10: 1 dB of jump counts; -10: it takes 11
        const float t = fast < 1e-6f ? 0.0f : std::clamp((rise - thr) / 6.0f, 0.0f, 1.0f);
        const float target = range * t;
        g = target + (std::fabs(target) > std::fabs(g) ? att : rel) * (g - target);
        return g;
    }
};

namespace hm {
using namespace haymaker;
const ParamInfo kParams[kCount] = {
    {"range", "Range", "dB", -24.0f, 18.0f, 6.0f},
    {"sens", "Sens", "", -10.0f, 10.0f, 0.0f},
    {"duration", "Duration", "ms", 0.01f, 500.0f, 4.1f, Taper::Log},
    {"release", "Release", "ms", 0.5f, 500.0f, 10.0f, Taper::Log},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"GAIN", "dB", -24.0f, 18.0f, true}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        sh_.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        trimDb_ = 0.0f;
        push_ = p(Trim);
        pk_.clear();
        publishMeter(0, 0.0f);
        publishMeter(1, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Trim) != push_) {
            push_ = p(Trim);
            if (pk_.hold > -99.0f) trimDb_ = std::clamp(trimDb_ - pk_.hold, -60.0f, 24.0f);
            pk_.clear();
        }
        const float slowUp = coefMs(fs_, p(Duration)), down = coefMs(fs_, 30.0);
        const float att = coefMs(fs_, 0.1), rel = coefMs(fs_, p(Release));
        float most = 0.0f, pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gDb = sh_.tick(std::max(std::fabs(L[i]), std::fabs(R[i])), slowUp, down, p(Range), p(Sens), att, rel);
            most = std::fabs(gDb) > std::fabs(most) ? gDb : most;
            const float g = hc::fx::dbToGain(gDb) * out_.next(hc::fx::dbToGain(p(Output) + trimDb_));
            L[i] *= g;
            R[i] *= g;
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        publishMeter(0, most);
        publishMeter(1, pk_.push(dbOf(pk), n, fs_));
        publishReduction(std::min(most, 0.0f));
    }

private:
    double fs_ = 48000.0;
    Shaper sh_;
    hc::fx::Smoothed out_;
    float trimDb_ = 0.0f, push_ = 0.0f;
    maxim::PeakHold pk_;
};
} // namespace hm

namespace fk {
using namespace fourkings;
#define HEM_BAND(n)                                                                                   \
    {"range" #n, "Band " #n " Range", "dB", -24.0f, 18.0f, 6.0f},                                     \
    {"sens" #n, "Band " #n " Sens", "", -10.0f, 10.0f, 0.0f},                                         \
    {"on" #n, "Band " #n " On", "", 0, 1, 1, Taper::Toggle, kOnOff}
const ParamInfo kParams[kCount] = {
    HEM_BAND(1), HEM_BAND(2), HEM_BAND(3), HEM_BAND(4),
    {"mrange", "Master Range", "dB", -24.0f, 24.0f, 0.0f},
    {"msens", "Master Sens", "", -10.0f, 10.0f, 0.0f},
    {"duration", "Duration", "ms", 0.01f, 500.0f, 4.1f, Taper::Log},
    {"release", "Release", "ms", 0.5f, 500.0f, 10.0f, Taper::Log},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {{"BAND 1", "dB", -24.0f, 18.0f, true}, {"BAND 2", "dB", -24.0f, 18.0f, true}, {"BAND 3", "dB", -24.0f, 18.0f, true},
                                     {"BAND 4", "dB", -24.0f, 18.0f, true}, {"OUTPUT", "dBFS", -60.0f, 3.0f}};
constexpr float kRelScale[kBands] = {2.0f, 1.3f, 0.8f, 0.5f};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& x : xo_) x.design(fs, kCrossHz, kBands);
        reset();
    }
    void reset() noexcept override {
        for (auto& x : xo_) x.clear();
        for (auto& s : sh_) s.clear();
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        trimDb_ = 0.0f;
        push_ = p(Trim);
        pk_.clear();
        for (int b = 0; b < kBands; ++b) publishMeter(b, 0.0f);
        publishMeter(kBands, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Trim) != push_) {
            push_ = p(Trim);
            if (pk_.hold > -99.0f) trimDb_ = std::clamp(trimDb_ - pk_.hold, -60.0f, 24.0f);
            pk_.clear();
        }
        const float slowUp = coefMs(fs_, p(Duration)), down = coefMs(fs_, 30.0), att = coefMs(fs_, 0.1);
        float rel[kBands], range[kBands], sens[kBands];
        bool on[kBands];
        for (int b = 0; b < kBands; ++b) {
            rel[b] = coefMs(fs_, p(Release) * kRelScale[b]);
            range[b] = std::clamp(p(band(b, Range)) + p(MRange), -24.0f, 18.0f);
            sens[b] = std::clamp(p(band(b, Sens)) + p(MSens), -10.0f, 10.0f);
            on[b] = p(band(b, On)) >= 0.5f;
        }
        float most[kBands]{}, pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float bl[kBands], br[kBands];
            xo_[0].split(L[i], bl);
            xo_[1].split(R[i], br);
            float yl = 0.0f, yr = 0.0f;
            for (int b = 0; b < kBands; ++b) {
                const float gDb = sh_[b].tick(std::max(std::fabs(bl[b]), std::fabs(br[b])), slowUp, down, on[b] ? range[b] : 0.0f, sens[b], att, rel[b]);
                most[b] = std::fabs(gDb) > std::fabs(most[b]) ? gDb : most[b];
                const float g = hc::fx::dbToGain(gDb);
                yl += bl[b] * g;
                yr += br[b] * g;
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output) + trimDb_));
            L[i] = yl * o;
            R[i] = yr * o;
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        float worst = 0.0f;
        for (int b = 0; b < kBands; ++b) {
            publishMeter(b, most[b]);
            worst = std::min(worst, most[b]);
        }
        publishMeter(kBands, pk_.push(dbOf(pk), n, fs_));
        publishReduction(worst);
    }

private:
    double fs_ = 48000.0;
    Crossover<kBands> xo_[2];
    Shaper sh_[kBands];
    hc::fx::Smoothed out_;
    float trimDb_ = 0.0f, push_ = 0.0f;
    maxim::PeakHold pk_;
};
} // namespace fk

} // namespace

std::unique_ptr<hc::fx::Effect> makeHaymaker() { return std::make_unique<hm::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeFourKings() { return std::make_unique<fk::Impl>(); }

} // namespace hem::rack
