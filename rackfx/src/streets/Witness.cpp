// THE WITNESS — VU and peak meters (see Streets.hpp). The sound passes untouched.
#include "hem/rack/Streets.hpp"

#include "../arsenal/Common.hpp"

namespace hem::rack {
namespace witness {
namespace {
using namespace arsenal;

const char* const kHoldN[] = {"OFF", "1 S", "3 S"};
const ParamInfo kParams[kCount] = {
    {"calibration", "Calibration", "dBFS", -24.0f, -8.0f, -18.0f},
    {"hold", "Peak Hold", "", 0, 2, 1, Taper::Choice, kHoldN},
};
const hc::fx::MeterInfo kMeters[] = {{"VU L", "VU", -40.0f, 6.0f}, {"VU R", "VU", -40.0f, 6.0f}, {"PEAK L", "dBFS", -60.0f, 0.0f}, {"PEAK R", "dBFS", -60.0f, 0.0f}};
constexpr double kHoldS[] = {0.0, 1.0, 3.0};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        // a VU needle reaches 99 % of a step in 300 ms: a time constant of about 65 ms
        vuK_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.065 * fs)));
        fall_ = static_cast<float>(20.0 / fs);   // the peak falls 20 dB a second after its hold
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            avg_[c] = 0.0f;
            peak_[c] = -100.0f;
            held_[c] = 0;
            publishMeter(c, -100.0f);
            publishMeter(2 + c, -100.0f);
        }
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const auto holdN = static_cast<std::uint32_t>(kHoldS[std::clamp(static_cast<int>(std::lround(p(Hold))), 0, 2)] * fs_);
        const float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            float a = avg_[c], pk = peak_[c];
            std::uint32_t h = held_[c];
            for (std::uint32_t i = 0; i < n; ++i) {
                const float x = std::fabs(io[c][i]);
                a += (x - a) * vuK_;
                const float d = dbOf(x);
                if (d >= pk) { pk = d; h = holdN; }
                else if (h > 0) --h;
                else pk = std::max(pk - fall_, -100.0f);
            }
            avg_[c] = a;
            peak_[c] = pk;
            held_[c] = h;
            // a sine's average is 2/pi of its peak: read the peak-referenced level, against the calibration
            const float lv = dbOf(a * static_cast<float>(std::numbers::pi) * 0.5f);
            publishMeter(c, lv < -99.0f ? -100.0f : lv - p(Calibration));
            publishMeter(2 + c, pk < -99.0f ? -100.0f : pk);
        }
    }

private:
    double fs_ = 48000.0;
    float vuK_ = 0.0f, fall_ = 0.0f, avg_[2]{}, peak_[2]{-100.0f, -100.0f};
    std::uint32_t held_[2]{};
};

} // namespace
} // namespace witness

std::unique_ptr<hc::fx::Effect> makeWitness() { return std::make_unique<witness::Impl>(); }

} // namespace hem::rack
