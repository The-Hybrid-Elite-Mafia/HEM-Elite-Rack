// THE WITNESS — VU meters with clip lights (see Streets.hpp). The sound passes untouched.
//
// The needle follows the rectified average (a VU's 300 ms rise: a time constant of about 65 ms), read against
// a sine's peak so a sine HEADROOM dB under full scale reads 0 VU. Every 64 samples the needles' values go into
// a one-second history; DISPLAY DELAY shows them from that far back.
#include "hem/rack/Streets.hpp"

#include "../arsenal/Common.hpp"

namespace hem::rack {
namespace witness {
namespace {
using namespace arsenal;

const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"headroom", "Headroom", "dB", 4.0f, 26.0f, 18.0f},
    {"displaydelay", "Display Delay", "ms", 0.0f, 1000.0f, 0.0f},
    {"clear", "Clear", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"VU L", "VU", -40.0f, 6.0f}, {"VU R", "VU", -40.0f, 6.0f}, {"CLIP L", "", 0.0f, 1.0f}, {"CLIP R", "", 0.0f, 1.0f}};
constexpr std::uint32_t kStep = 64;
constexpr std::size_t kHistory = 1024;   // > 1 s of steps at 48 kHz
constexpr float kClip = 0.99896f;        // -0.009 dBFS

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        vuK_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.065 * fs)));
        reset();
    }
    void reset() noexcept override {
        avg_[0] = avg_[1] = 0.0f;
        clip_[0] = clip_[1] = 0.0f;
        for (auto& h : hist_) h.fill(0.0f);
        head_ = 0;
        clock_ = 0;
        push_ = p(Clear);
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
        publishMeter(2, 0.0f);
        publishMeter(3, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Clear) != push_) {
            push_ = p(Clear);
            clip_[0] = clip_[1] = 0.0f;
        }
        const float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) {
                const float x = std::fabs(io[c][i]);
                avg_[c] += (x - avg_[c]) * vuK_;
                if (x >= kClip) clip_[c] = 1.0f;
            }
            if (++clock_ == kStep) {
                clock_ = 0;
                head_ = (head_ + 1) % kHistory;
                hist_[0][head_] = avg_[0];
                hist_[1][head_] = avg_[1];
            }
        }
        const auto back = static_cast<std::size_t>(std::lround(p(DisplayDelay) * 0.001 * fs_ / kStep));
        const std::size_t at = (head_ + kHistory - std::min(back, kHistory - 1)) % kHistory;
        for (int c = 0; c < 2; ++c) {
            // a sine's average is 2/pi of its peak: read the peak-referenced level, against HEADROOM
            const float a = back == 0 ? avg_[c] : hist_[c][at];
            const float lv = dbOf(a * static_cast<float>(std::numbers::pi) * 0.5f);
            publishMeter(c, lv < -99.0f ? -100.0f : lv + p(Headroom));
            publishMeter(2 + c, clip_[c]);
        }
    }

private:
    double fs_ = 48000.0;
    float vuK_ = 0.0f, avg_[2]{}, clip_[2]{}, push_ = 0.0f;
    std::array<std::array<float, kHistory>, 2> hist_{};
    std::size_t head_ = 0;
    std::uint32_t clock_ = 0;
};

} // namespace
} // namespace witness

std::unique_ptr<hc::fx::Effect> makeWitness() { return std::make_unique<witness::Impl>(); }

} // namespace hem::rack
