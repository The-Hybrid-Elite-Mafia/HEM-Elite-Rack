// THE DETECTIVE — names the song's key, two alternatives and its tuning (see Streets.hpp).
//
// Every 8192 samples the spectrum (55 Hz .. 2 kHz) is folded onto the 12 pitch classes and added up; the
// total is compared with the 24 major and minor key profiles (Krumhansl-Kessler): the best match is the key,
// the next two are the alternatives. The spectrum's peaks also say where the song's notes sit against
// A = 440 (their average offset, in cents): that is the TUNING. It stops by itself once the same key has
// held for 30 seconds of music, or when the music stops for 3 seconds. The sound passes untouched.
#include "hem/rack/Streets.hpp"

#include "hybrid/fx/Fft.hpp"

#include "../arsenal/Common.hpp"

#include <complex>
#include <vector>

namespace hem::rack {
namespace detective {
namespace {
using namespace arsenal;

const char* const kPushN[] = {"A", "B"};   // any change = a push
const char* const kSpellN[] = {"SHARPS", "FLATS"};
const char* const kRootN[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kScaleN[] = {"MAJOR", "MINOR"};
const ParamInfo kParams[kCount] = {
    {"detect", "Detect", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"spell", "Spell", "", 0, 1, 0, Taper::Choice, kSpellN},
    {"edit", "Edit", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"root", "Root", "", 0, 11, 0, Taper::Choice, kRootN},
    {"scale", "Scale", "", 0, 1, 0, Taper::Choice, kScaleN},
};
const hc::fx::MeterInfo kMeters[] = {{"KEY", "", -1.0f, 47.0f}, {"ALT 1", "", -1.0f, 47.0f}, {"ALT 2", "", -1.0f, 47.0f}, {"TUNING", "Hz", 430.0f, 450.0f},
                                     {"STATE", "", 0.0f, 4.0f}};
constexpr double kMajor[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
constexpr std::uint32_t kN = 8192;
constexpr double kHoldSeconds = 30.0, kQuietSeconds = 3.0, kFirstSeconds = 3.0;

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
    bool paramActive(int i) const noexcept override { return (i != Root && i != Scale) || p(Edit) >= 0.5f; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fft_.prepare(static_cast<int>(kN));
        buf_.assign(kN, 0.0f);
        spec_.assign(kN, {});
        mag_.assign(kN / 2, 0.0f);
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
        push_ = p(Detect);
        restart();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Detect) != push_) {   // DETECT: stop while listening, otherwise start over
            push_ = p(Detect);
            if (state_ == Listening || state_ == Waiting) finish();
            else restart();
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            buf_[pos_] = 0.5f * (L[i] + R[i]);
            if (++pos_ == kN) {
                pos_ = 0;
                if (state_ == Waiting || state_ == Listening) frame();
            }
        }
        show();
    }

private:
    void restart() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
        chroma_.fill(0.0);
        heard_ = quiet_ = held_ = 0.0;
        tunC_ = tunS_ = 0.0;
        key_ = alt1_ = alt2_ = -1;
        best_ = 0.0;
        state_ = Waiting;
    }
    void finish() noexcept { state_ = key_ >= 0 ? Found : Nothing; }

    void frame() noexcept {
        double e = 0;
        for (std::uint32_t i = 0; i < kN; ++i) {
            spec_[i] = {buf_[i] * win_[i], 0.0f};
            e += static_cast<double>(buf_[i]) * buf_[i];
        }
        const double frameS = static_cast<double>(kN) / fs_;
        if (powDb(e / kN) < -60.0f) {   // silence teaches nothing; long enough, and it's over
            if (state_ == Listening && (quiet_ += frameS) >= kQuietSeconds) finish();
            return;
        }
        quiet_ = 0.0;
        state_ = Listening;
        fft_.forward(spec_.data());
        for (std::uint32_t k = 1; k < kN / 2; ++k) mag_[k] = std::abs(spec_[k]);
        float top = 0.0f;
        for (std::uint32_t k = 1; k < kN / 2; ++k)
            if (pc_[k] >= 0) {
                chroma_[static_cast<std::size_t>(pc_[k])] += mag_[k];
                top = std::max(top, mag_[k]);
            }
        // the tuning: each strong peak's offset from the nearest semitone (cents), averaged around the circle
        for (std::uint32_t k = 2; k + 1 < kN / 2; ++k) {
            if (pc_[k] < 0 || mag_[k] < 0.1f * top || mag_[k] < mag_[k - 1] || mag_[k] < mag_[k + 1]) continue;
            const double a = mag_[k - 1], b = mag_[k], c = mag_[k + 1], den = a - 2.0 * b + c;
            if (den >= 0.0) continue;
            const double hz = (static_cast<double>(k) + 0.5 * (a - c) / den) * fs_ / kN;   // the peak between the bins
            const double cents = 1200.0 * std::log2(hz / 440.0);
            const double off = cents - 100.0 * std::round(cents / 100.0);
            const double ang = 2.0 * std::numbers::pi * off / 100.0;
            tunC_ += b * std::cos(ang);
            tunS_ += b * std::sin(ang);
        }
        heard_ += frameS;
        if (heard_ < kFirstSeconds) return;   // a few seconds of music before it names anything
        double score[24], rot[12];
        for (int key = 0; key < 24; ++key) {
            const double* prof = key < 12 ? kMajor : kMinor;
            for (int i = 0; i < 12; ++i) rot[(i + key) % 12] = prof[i];
            score[key] = pearson(chroma_.data(), rot);
        }
        int order[3] = {-1, -1, -1};
        for (int r = 0; r < 3; ++r) {
            double best = -2.0;
            for (int key = 0; key < 24; ++key) {
                if (key == order[0] || key == order[1]) continue;
                if (score[key] > best) { best = score[key]; order[r] = key; }
            }
        }
        const int was = key_;
        key_ = score[order[0]] > 0.3 ? order[0] : -1;
        alt1_ = order[1];
        alt2_ = order[2];
        best_ = score[order[0]];
        held_ = key_ >= 0 && key_ == was ? held_ + frameS : 0.0;
        if (held_ >= kHoldSeconds) finish();
    }

    void show() noexcept {
        const float flats = p(Spell) >= 0.5f ? 24.0f : 0.0f;
        auto out = [flats](int k) { return k < 0 ? -1.0f : static_cast<float>(k) + flats; };
        if (p(Edit) >= 0.5f) {
            const int k = std::clamp(static_cast<int>(std::lround(p(Root))), 0, 11) + (p(Scale) >= 0.5f ? 12 : 0);
            publishMeter(0, out(k));
            publishMeter(1, -1.0f);
            publishMeter(2, -1.0f);
            publishMeter(4, static_cast<float>(Edited));
        } else {
            publishMeter(0, out(key_));
            const bool done = state_ == Found;   // the alternatives come when it's done
            publishMeter(1, done ? out(alt1_) : -1.0f);
            publishMeter(2, done ? out(alt2_) : -1.0f);
            publishMeter(4, static_cast<float>(state_));
        }
        const double c = (tunC_ != 0.0 || tunS_ != 0.0) ? std::atan2(tunS_, tunC_) / (2.0 * std::numbers::pi) * 100.0 : 0.0;
        publishMeter(3, static_cast<float>(440.0 * std::exp2(c / 1200.0)));
    }

    double fs_ = 48000.0;
    hc::fx::Fft fft_;
    std::vector<float> buf_, win_, mag_;
    std::vector<std::complex<float>> spec_;
    std::vector<int> pc_;
    std::uint32_t pos_ = 0;
    std::array<double, 12> chroma_{};
    double heard_ = 0.0, quiet_ = 0.0, held_ = 0.0, tunC_ = 0.0, tunS_ = 0.0, best_ = 0.0;
    int key_ = -1, alt1_ = -1, alt2_ = -1, state_ = Waiting;
    float push_ = 0.0f;
};

} // namespace
} // namespace detective

std::unique_ptr<hc::fx::Effect> makeDetective() { return std::make_unique<detective::Impl>(); }

} // namespace hem::rack
