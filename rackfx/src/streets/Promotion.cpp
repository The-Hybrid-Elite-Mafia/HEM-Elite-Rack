// PROMOTION — pitch shifter (see Streets.hpp).
//
// Four grain shifters per side, one per MODE: SMOOTH (70 ms grains, for held notes), SYNC (55 ms), TRANSIENT
// (40 ms, and on every attack the shifter jumps to the newest sound so the hit stays sharp) and PUNCHY
// (22 ms, for rhythm). The shorter ones are delayed to line up with SMOOTH, so the latency never changes;
// switching MODE crossfades. The ratio comes from SEMITONES + CENTS, or from FROM Hz -> TO Hz.
#include "hem/rack/Streets.hpp"

#include "../hideout/SpaceKit.hpp"

namespace hem::rack {
namespace promotion {
namespace {
using namespace hideout;

const char* const kEntryN[] = {"TRANSPOSE", "FREQUENCY"};
const char* const kModeN[] = {"SYNC", "SMOOTH", "TRANSIENT", "PUNCHY"};
const char* const kSemiN[] = {"-OCTAVE", "-MAJ 7TH", "-MIN 7TH", "-MAJ 6TH", "-MIN 6TH", "-5TH", "-TRITONE", "-4TH", "-MAJ 3RD", "-MIN 3RD", "-MAJ 2ND",
                              "-MIN 2ND", "UNISON", "+MIN 2ND", "+MAJ 2ND", "+MIN 3RD", "+MAJ 3RD", "+4TH", "+TRITONE", "+5TH", "+MIN 6TH", "+MAJ 6TH",
                              "+MIN 7TH", "+MAJ 7TH", "+OCTAVE"};
const ParamInfo kParams[kCount] = {
    {"entry", "Set By", "", 0, 1, 0, Taper::Choice, kEntryN},
    {"semitones", "Semitones", "", 0, 24, 12, Taper::Choice, kSemiN},
    {"cents", "Cents", "cents", 0.0f, 99.0f, 0.0f},
    {"fromhz", "From", "Hz", 20.0f, 20000.0f, 440.0f, Taper::Log},
    {"tohz", "To", "Hz", 20.0f, 20000.0f, 440.0f, Taper::Log},
    {"mode", "Mode", "", 0, kModes - 1, Transient, Taper::Choice, kModeN},
};
constexpr double kWinMs[kModes] = {55.0, 70.0, 40.0, 22.0};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return lat_; }
    bool paramActive(int i) const noexcept override {
        const bool freq = p(Entry) >= 0.5f;
        if (i == Semitones || i == Cents) return !freq;
        if (i == FromHz || i == ToHz) return freq;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lat_ = static_cast<std::uint32_t>(std::lround(1.0 + 0.5 * 0.070 * fs));
        for (int m = 0; m < kModes; ++m) {
            extra_[m] = static_cast<float>(0.5 * (0.070 - kWinMs[m] * 0.001) * fs);
            for (int c = 0; c < 2; ++c) {
                sh_[m][c].prepare(fs, kWinMs[m]);
                late_[m][c].prepare(static_cast<std::uint32_t>(extra_[m]) + 8);
            }
        }
        reset();
    }
    void reset() noexcept override {
        for (int m = 0; m < kModes; ++m)
            for (int c = 0; c < 2; ++c) {
                sh_[m][c].clear();
                late_[m][c].clear();
            }
        for (int m = 0; m < kModes; ++m) mix_[m] = m == mode() ? 1.0f : 0.0f;
        fast_ = slow_ = 0.0f;
        quiet_ = 0;
        cents_.prepare(fs_, 40, target());
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int want = mode();
        const float step = static_cast<float>(1.0 / (0.03 * fs_)), fA = coefMs(fs_, 0.5), fR = coefMs(fs_, 20.0), sA = coefMs(fs_, 15.0);
        const auto refractory = static_cast<std::uint32_t>(0.05 * fs_);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float ratio = std::exp2(cents_.next(target()) / 1200.0f);
            // an attack: the fast follower jumps 6 dB over the slow one
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            fast_ = x > fast_ ? x + fA * (fast_ - x) : x + fR * (fast_ - x);
            slow_ = fast_ > slow_ ? fast_ + sA * (slow_ - fast_) : fast_;
            bool attack = false;
            if (quiet_ > 0) --quiet_;
            else if (fast_ > 1e-4f && fast_ > 2.0f * slow_) {
                attack = true;
                quiet_ = refractory;
            }
            float y[2] = {0.0f, 0.0f};
            for (int m = 0; m < kModes; ++m) {
                mix_[m] = m == want ? std::min(1.0f, mix_[m] + step) : std::max(0.0f, mix_[m] - step);
                for (int c = 0; c < 2; ++c) {
                    if (attack && m == Transient && std::fabs(ratio - 1.0f) > 1e-4f) sh_[m][c].snap();   // at unison there's nothing to keep sharp
                    float v = sh_[m][c].tick(io[c][i], ratio);
                    if (extra_[m] > 0.0f) {
                        late_[m][c].write(v);
                        v = late_[m][c].read(extra_[m]);
                    }
                    y[c] += mix_[m] * v;
                }
            }
            io[0][i] = y[0];
            io[1][i] = y[1];
        }
    }

private:
    int mode() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Mode))), 0, kModes - 1); }
    // the shift in cents, up to an octave either way
    float target() const noexcept {
        double c;
        if (p(Entry) >= 0.5f) c = 1200.0 * std::log2(static_cast<double>(p(ToHz)) / std::max(1.0f, p(FromHz)));
        else c = (std::round(p(Semitones)) - 12.0) * 100.0 + p(Cents);
        return static_cast<float>(std::clamp(c, -1200.0, 1200.0));
    }

    double fs_ = 48000.0;
    PitchShift sh_[kModes][2];
    hc::fx::DelayLine late_[kModes][2];
    float extra_[kModes]{}, mix_[kModes]{}, fast_ = 0.0f, slow_ = 0.0f;
    std::uint32_t lat_ = 1681, quiet_ = 0;
    hc::fx::Smoothed cents_;
};

} // namespace
} // namespace promotion

std::unique_ptr<hc::fx::Effect> makePromotion() { return std::make_unique<promotion::Impl>(); }

} // namespace hem::rack
