// CONSIGLIERE and STONE COLD — light-cell levelers (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Vintage.hpp"

namespace hem::rack {
namespace opto {
namespace {
using namespace arsenal;

const char* const kModeN[] = {"COMPRESS", "LIMIT"};
#define HEM_OPTO_TAIL                                                                          \
    {"automakeup", "Auto Makeup", "", 0, 1, 0, Taper::Toggle, kOnOff},                         \
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},                                      \
    {"hifreq", "Bright Detect", "", 0.0f, 100.0f, 50.0f},                                            \
    {"analog", "Hum", "", 0, 2, 0, Taper::Choice, kMainsN},                                 \
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},                                                 \
    {"trim", "Trim", "dB", -18.0f, 18.0f, 0.0f},                                               \
    {"meter", "Meter", "", 0, 2, 1, Taper::Choice, kMeterN}
const ParamInfo kParams2A[kCount] = {
    {"gain", "Gain", "", 0.0f, 100.0f, 0.0f},
    {"peakreduction", "Squeeze", "", 0.0f, 100.0f, 30.0f},
    HEM_OPTO_TAIL,
};
const ParamInfo kParams3A[kCount] = {
    {"gain", "Gain", "/10", 0.0f, 10.0f, 0.0f},
    {"peakreduction", "Squeeze", "/10", 0.0f, 10.0f, 3.0f},
    HEM_OPTO_TAIL,
};
#undef HEM_OPTO_TAIL
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};

// The gain computer: the cell starts working when the (boosted) sidechain reaches 0 dBFS; PEAK
// REDUCTION boosts the sidechain up to 40 dB, so more = it starts on lower signals.
float computerDb(float x, bool limit, bool solid) noexcept {
    const float K = limit ? 4.0f : (solid ? 6.0f : 10.0f);
    const float ratio = limit ? (solid ? 20.0f : 12.0f) : 3.0f;
    return downDb(x, 0.0f, ratio, K);
}

template <bool kSolid>
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kSolid ? kParams3A : kParams2A; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        mem_.set(fs, 3.0, 8.0);
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        auto_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        sc_[0].clear();
        sc_[1].clear();
        hf_ = -1.0f;
        fast_ = slow_ = 0.0f;
        mem_.v = 0.0f;
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        auto_.clear();
        gain_.prepare(fs_, 30, hc::fx::dbToGain(gainDb()));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        trim_.prepare(fs_, 30, hc::fx::dbToGain(p(Trim)));
        publishReduction(0.0f);
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(HiFreq) != hf_) {   // the detector's ear: 0 = flat, 100 = +12 dB above ~1.5 kHz
            hf_ = p(HiFreq);
            const C c = C::make(T::HighShelf, fs_, 1500.0, 0.7071, hf_ * 0.12);
            sc_[0].c = sc_[1].c = c;
        }
        const bool limit = p(Mode) >= 0.5f, autoMk = p(AutoMakeup) >= 0.5f;
        const float scDb = p(PeakReduction) * (kSolid ? 4.0f : 0.4f);
        const int mains = static_cast<int>(std::lround(p(Analog)));
        // light cell: ~10 ms attack; half lets go in ~60 ms, half slowly (0.5 .. 5 s with memory).
        // solid: ~1.5 ms attack; ~40 ms then 0.3 .. 1.5 s.
        const float att = coefMs(fs_, kSolid ? 1.5 : 10.0);
        const float fastRel = coefMs(fs_, kSolid ? 40.0 : 60.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            vuIn_.tick(L[i], R[i]);
            const float dl = sc_[0].tick(L[i]), dr = sc_[1].tick(R[i]);
            const float level = std::max(std::fabs(dl), std::fabs(dr));
            const float target = computerDb(dbOf(level) + scDb, limit, kSolid);
            const double slowS = kSolid ? 0.3 + 1.2 * mem_.v : 0.5 + 4.5 * mem_.v;
            const float slowRel = coefMs(fs_, slowS * 1000.0);
            fast_ = target + (target < fast_ ? att : fastRel) * (fast_ - target);
            slow_ = target + (target < slow_ ? att : slowRel) * (slow_ - target);
            const float gr = 0.5f * (fast_ + slow_);
            mem_.tick(target < -3.0f);
            most = std::min(most, gr);
            const float mk = auto_.tick(gr);
            const float g = hc::fx::dbToGain(gr + (autoMk ? mk : 0.0f)) * gain_.next(hc::fx::dbToGain(gainDb()));
            const float m = mix_.next(p(Mix) * 0.01f);
            float yl = L[i] * g, yr = R[i] * g;
            if constexpr (kSolid) {   // the solid-state amp's touch of grit: a little 3rd harmonic as it works
                const float d = std::min(-gr, 12.0f) / 12.0f * 0.08f;
                yl *= 1.0f - d * yl * yl / (1.0f + yl * yl);   // bounded: never flips or runs away
                yr *= 1.0f - d * yr * yr / (1.0f + yr * yr);
            }
            const float t = trim_.next(hc::fx::dbToGain(p(Trim)));
            float ol = (L[i] + m * (yl - L[i])) * t, orr = (R[i] + m * (yr - R[i])) * t;
            hum_.tick(mains, 1.0f, ol, orr);
            L[i] = ol;
            R[i] = orr;
            vuOut_.tick(ol, orr);
        }
        if (fast_ > -1e-6f) fast_ = 0.0f;
        if (slow_ > -1e-6f) slow_ = 0.0f;
        publishReduction(most);
        publishMeter(0, vuIn_.vu());
        publishMeter(1, vuOut_.vu());
    }

private:
    float gainDb() const noexcept { return p(Gain) * (kSolid ? 4.0f : 0.4f); }   // the dial's full turn = +40 dB

    double fs_ = 48000.0;
    hc::fx::Biquad sc_[2];
    float hf_ = -1.0f, fast_ = 0.0f, slow_ = 0.0f;
    Memory mem_;
    Hum hum_;
    Vu vuIn_, vuOut_;
    AutoGain auto_;
    hc::fx::Smoothed gain_, mix_, trim_;
};

} // namespace
} // namespace opto

std::unique_ptr<hc::fx::Effect> makeConsigliere() { return std::make_unique<opto::Impl<false>>(); }
std::unique_ptr<hc::fx::Effect> makeStoneCold() { return std::make_unique<opto::Impl<true>>(); }

} // namespace hem::rack
