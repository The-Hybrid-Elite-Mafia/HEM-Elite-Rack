// THE CHAIRMAN — British-console channel: preamp, three-band EQ with a low cut, analog noise (see ToneShop.hpp).
//
// In order: INPUT and PHASE, the preamp stage (4x oversampled; how hard it is driven comes from SOURCE
// and PREAMP, the level is made up, so it only adds colour), the EQ (eqkit::IirBands: switching a band
// or the whole EQ crossfades), OUTPUT and TRIM, then the analog hum and hiss (ANALOG, NOISE).
#include "hem/rack/ToneShop.hpp"

#include "EqKit.hpp"
#include "ToneCommon.hpp"
#include "../arsenal/Vintage.hpp"

namespace hem::rack {
namespace chairman {
namespace {
using namespace arsenal;
using eqkit::Shape;

const char* const kSourceN[] = {"MIC", "LINE"};
const char* const kPreampN[] = {"20", "30", "40", "50", "60", "70"};
const char* const kHighN[] = {"-16", "-12", "-8", "-4", "0", "+4", "+8", "+12"};
const char* const kMidHzN[] = {"700", "1K", "1.4K", "2K", "2.8K", "3.5K", "4.5K", "6K"};
const char* const kMidModeN[] = {"BOOST", "CUT"};
const char* const kLowHzN[] = {"OFF", "60", "120", "250", "400"};
const char* const kCutN[] = {"OFF", "-3", "-6", "-9", "-12", "-15"};
const char* const kNoiseN[] = {"FULL", "LOW"};
const char* const kMeterIoN[] = {"IN", "OUT"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"source", "Source", "", 0, 1, 0, Taper::Choice, kSourceN},
    {"preamp", "Preamp", "", 0, 5, 1, Taper::Choice, kPreampN},
    {"input", "Input", "dB", -18.0f, 18.0f, 0.0f},
    {"high", "High", "", 0, 7, 4, Taper::Choice, kHighN},
    {"midgain", "Mid Gain", "dB", 0.0f, 15.0f, 0.0f},
    {"midfreq", "Mid Freq", "", 0, 7, 3, Taper::Choice, kMidHzN},
    {"midmode", "Mid Style", "", 0, 1, 0, Taper::Choice, kMidModeN},
    {"lowfreq", "Low Freq", "", 0, 4, 0, Taper::Choice, kLowHzN},
    {"lowgain", "Low Gain", "dB", 0.0f, 15.0f, 0.0f},
    {"lowcut", "Low Cut", "", 0, 5, 0, Taper::Choice, kCutN},
    {"eqin", "EQ", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"analog", "Hum", "", 0, 2, 2, Taper::Choice, kMainsN},
    {"noise", "Hiss", "", 0, 1, 0, Taper::Choice, kNoiseN},
    {"phase", "Phase", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"trim", "Trim", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"meter", "Meter", "", 0, 1, 1, Taper::Choice, kMeterIoN},
    {"vucal", "VU Zero", "dB", 8.0f, 24.0f, 18.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};
constexpr float kMidQ = 1.0f, kShelfQ = 0.7071f;

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i == Preamp) return p(Source) < 0.5f;
        if (i == LowGain) return p(LowFreq) >= 0.5f;
        if (i == VuCal || i == Meter) return true;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        pre_.prepare();
        eq_[0].prepare(fs);
        eq_[1].prepare(fs);
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        dcK_ = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs));
        reset();
    }
    void reset() noexcept override {
        pre_.clear();
        load();
        eq_[0].clear();
        eq_[1].clear();
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        dcL_ = dcR_ = 0.0f;
        in_.prepare(fs_, 20, inGain());
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output) + trimDb_));
        push_ = p(Trim);
        most_ = 0.0f;
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Trim) != push_) {
            push_ = p(Trim);
            if (most_ > 1e-6f) trimDb_ = std::clamp(trimDb_ - dbOf(most_), -12.0f, 12.0f);
            most_ = 0.0f;
        }
        load();
        const float wIn = inGain();
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(wIn);
            L[i] *= g;
            R[i] *= g;
            vuIn_.tick(L[i], R[i]);
        }
        // the preamp: driven by SOURCE / PREAMP, made up again (small signals come out as they went in)
        const float k = p(Source) < 0.5f ? static_cast<float>(pick(p(Preamp), 5)) / 5.0f : 0.1f;
        const float a = 0.35f + 4.65f * k, even = 0.12f * k;
        pre_.run(L, R, n, true, 1.0f, [a, even](float x) {
            const float u = a * x;
            return std::tanh(u + even * u * u) / a;
        });
        const int mains = pick(p(Analog), 2);
        const float noise = p(Noise) < 0.5f ? 1.0f : 0.25f;
        const float wOut = hc::fx::dbToGain(p(Output) + trimDb_);
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            dcL_ += (L[i] - dcL_) * dcK_;
            dcR_ += (R[i] - dcR_) * dcK_;
            eq_[0].step();
            eq_[1].step();
            float l = eq_[0].run(0, L[i] - dcL_), r = eq_[1].run(0, R[i] - dcR_);
            const float o = out_.next(wOut);
            l *= o;
            r *= o;
            hum_.tick(mains, noise, l, r);
            L[i] = l;
            R[i] = r;
            vuOut_.tick(l, r);
            pk = std::max({pk, std::fabs(l), std::fabs(r)});
        }
        most_ = std::max(most_, pk);
        publishMeter(0, vuIn_.vu(p(VuCal)));
        publishMeter(1, vuOut_.vu(p(VuCal)));
    }

private:
    float inGain() const noexcept { return hc::fx::dbToGain(p(Input)) * (p(Phase) >= 0.5f ? -1.0f : 1.0f); }
    // the EQ's four sections from the switches
    void load() noexcept {
        const bool on = p(EqIn) >= 0.5f;
        eqkit::BandSet b[4];
        if (on) {
            const float high = kHighDb[pick(p(High), 7)];
            if (high != 0.0f) b[0] = {Shape::HighShelf, 10000.0f, high, kShelfQ};
            if (p(MidGain) > 0.0f) b[1] = {Shape::Bell, kMidHz[pick(p(MidFreq), 7)], p(MidMode) < 0.5f ? p(MidGain) : -p(MidGain), kMidQ};
            const int lf = pick(p(LowFreq), 4);
            if (lf > 0 && p(LowGain) > 0.0f) b[2] = {Shape::LowShelf, kLowHz[lf], p(LowGain), kShelfQ};
            const int lc = pick(p(LowCut), 5);
            if (lc > 0) b[3] = {Shape::LowShelf, 50.0f, kCutDb[lc], kShelfQ};
        }
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < 4; ++k) eq_[s].set(k, b[k]);
    }

    double fs_ = 48000.0;
    tone::Drive4 pre_;
    eqkit::IirBands<4> eq_[2];
    Hum hum_;
    Vu vuIn_, vuOut_;
    hc::fx::Smoothed in_, out_;
    float dcL_ = 0, dcR_ = 0, dcK_ = 0;
    float trimDb_ = 0.0f, push_ = 0.0f, most_ = 0.0f;   // trimDb_: only a push changes it
};

} // namespace
} // namespace chairman

std::unique_ptr<hc::fx::Effect> makeChairman() { return std::make_unique<chairman::Impl>(); }

} // namespace hem::rack
