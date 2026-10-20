// THE DETECTIVE — names the song's key (see Streets.hpp).
//
// Every 8192 samples the spectrum (55 Hz .. 2 kHz) is folded onto the 12 pitch classes; the running
// total is compared with the 24 major and minor key profiles (Krumhansl-Kessler) and the best match
// wins. LISTEN sets how long it remembers. The sound passes untouched.
#include "hem/rack/Streets.hpp"

#include "hybrid/fx/Fft.hpp"

#include "../arsenal/Common.hpp"

#include <complex>
#include <vector>

namespace hem::rack {
namespace detective {
namespace {
using namespace arsenal;

const char* const kListenN[] = {"10 S", "30 S", "SONG"};
const char* const kResetN[] = {"A", "B"};   // any change = start over
const ParamInfo kParams[kCount] = {
    {"listen", "Listen", "", 0, 2, 1, Taper::Choice, kListenN},
    {"reset", "Reset", "", 0, 1, 0, Taper::Toggle, kResetN},
};
const hc::fx::MeterInfo kMeters[] = {{"KEY", "", -1.0f, 23.0f}, {"SURE", "%", 0.0f, 100.0f}};
constexpr double kMajor[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
constexpr std::uint32_t kN = 8192;
constexpr double kSeconds[] = {10.0, 30.0, 0.0};

double pearson(const double* a, const double* b) noexcept {
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
    ma /= 12;
    mb /= 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        da += (a[i] - ma) * (a[i] - ma);
        db += (b[i] - mb) * (b[i] - mb);
    }
    return da > 0 && db > 0 ? num / std::sqrt(da * db) : 0.0;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fft_.prepare(static_cast<int>(kN));
        buf_.assign(kN, 0.0f);
        spec_.assign(kN, {});
        win_.resize(kN);
        for (std::uint32_t i = 0; i < kN; ++i) win_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kN));
        // each bin's pitch class (-1 = outside 55 Hz .. 2 kHz)
        pc_.assign(kN / 2, -1);
        for (std::uint32_t k = 1; k < kN / 2; ++k) {
            const double hz = k * fs / kN;
            if (hz < 55.0 || hz > 2000.0) continue;
            const int midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(hz / 440.0)));
            pc_[k] = ((midi % 12) + 12) % 12;
        }
        reset();
    }
    void reset() noexcept override {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
        chroma_.fill(0.0);
        heard_ = 0.0;
        resetSeen_ = p(Reset);
        publishMeter(0, -1.0f);
        publishMeter(1, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Reset) != resetSeen_) reset();   // RESET clicked
        for (std::uint32_t i = 0; i < n; ++i) {
            buf_[pos_] = 0.5f * (L[i] + R[i]);
            if (++pos_ == kN) { pos_ = 0; frame(); }
        }
    }

private:
    void frame() noexcept {
        double e = 0;
        for (std::uint32_t i = 0; i < kN; ++i) {
            spec_[i] = {buf_[i] * win_[i], 0.0f};
            e += static_cast<double>(buf_[i]) * buf_[i];
        }
        const int li = std::clamp(static_cast<int>(std::lround(p(Listen))), 0, 2);
        const double keep = kSeconds[li] > 0 ? std::exp(-static_cast<double>(kN) / (kSeconds[li] * fs_)) : 1.0;
        for (double& c : chroma_) c *= keep;
        if (powDb(e / kN) < -60.0f) return;   // silence teaches nothing
        fft_.forward(spec_.data());
        for (std::uint32_t k = 1; k < kN / 2; ++k)
            if (pc_[k] >= 0) chroma_[static_cast<std::size_t>(pc_[k])] += std::abs(spec_[k]);
        heard_ += static_cast<double>(kN) / fs_;
        if (heard_ < 3.0) return;   // a few seconds of music before it names anything
        double best = -2, second = -2;
        int bestKey = -1;
        double rot[12];
        for (int key = 0; key < 24; ++key) {
            const double* prof = key < 12 ? kMajor : kMinor;
            for (int i = 0; i < 12; ++i) rot[(i + key) % 12] = prof[i];
            const double r = pearson(chroma_.data(), rot);
            if (r > best) { second = best; best = r; bestKey = key; }
            else if (r > second) second = r;
        }
        publishMeter(0, best > 0.3 ? static_cast<float>(bestKey) : -1.0f);
        publishMeter(1, static_cast<float>(std::clamp(best, 0.0, 1.0) * 100.0));
    }

    double fs_ = 48000.0;
    hc::fx::Fft fft_;
    std::vector<float> buf_, win_;
    std::vector<std::complex<float>> spec_;
    std::vector<int> pc_;
    std::uint32_t pos_ = 0;
    std::array<double, 12> chroma_{};
    double heard_ = 0.0;
    float resetSeen_ = 0.0f;
};

} // namespace
} // namespace detective

std::unique_ptr<hc::fx::Effect> makeDetective() { return std::make_unique<detective::Impl>(); }

} // namespace hem::rack
