// STAKEOUT — analyzer: frequency bands, stereo position, meters (see Analyzer.hpp). The sound passes untouched.
//
// Every 4096 samples a 16384-point Hann FFT of each side
// is summed into the bands. A sine's level is read from the energy in its band (Parseval), so a full-scale
// sine reads 0 dB whatever band it falls in. The stereo position of a band comes from the left and right
// energies (where it sits) and their correlation (out of phase pushes it past 45 degrees).
#include "hem/rack/Analyzer.hpp"

#include "hybrid/fx/Fft.hpp"

#include "../arsenal/Common.hpp"
#include "WeightKit.hpp"

#include <atomic>
#include <complex>
#include <vector>

namespace hem::rack {
namespace stakeout {
namespace {
using namespace arsenal;

const char* const kResN[] = {"40 HZ", "20 HZ", "10 HZ"};
const char* const kModeN[] = {"PEAK", "RMS"};
const char* const kChanN[] = {"L / R", "L+R"};
const char* const kWeightN[] = {"NONE", "A", "B", "C"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"resolution", "LF Resolution", "", 0, 2, 0, Taper::Choice, kResN},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"response", "Response", "s", 0.05f, 5.0f, 0.5f, Taper::Log},
    {"channels", "Channels", "", 0, 1, 0, Taper::Choice, kChanN},
    {"weighting", "Weighting", "", 0, 3, 0, Taper::Choice, kWeightN},
    {"freeze", "Freeze", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"showmax", "Max Line", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"clearmax", "Clear Max", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"metermode", "Sum Meter", "", 0, 1, 0, Taper::Choice, kModeN},
    {"holdl", "Reset Hold L", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"holdr", "Reset Hold R", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"holdsum", "Reset Hold Sum", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"clearclips", "Clear Clips", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeterInfo[kMeters] = {
    {"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}, {"SUM", "dB", -60.0f, 6.0f}, {"HOLD L", "dBFS", -60.0f, 6.0f},
    {"HOLD R", "dBFS", -60.0f, 6.0f}, {"HOLD SUM", "dB", -60.0f, 6.0f}, {"CLIP L", "", 0.0f, 1.0f}, {"CLIP R", "", 0.0f, 1.0f},
};
constexpr int kN = 16384, kHop = 4096;   // 2.9 Hz bins: even the 10 Hz bands hold a tone whole
constexpr float kFloor = -120.0f;

class Impl final : public hc::fx::Effect, public AnalyzerSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }

    float rtaLevel(int side, int b) const noexcept override { return at(level_, side, b); }
    float rtaMax(int side, int b) const noexcept override { return at(max_, side, b); }
    float spdAngle(int b) const noexcept override { return b >= 0 && b < kRtaMaxBands ? angle_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed) : 0.0f; }
    float spdLevel(int b) const noexcept override { return b >= 0 && b < kRtaMaxBands ? spd_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed) : kFloor; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fft_.prepare(kN);
        ring_[0].assign(kN, 0.0f);
        ring_[1].assign(kN, 0.0f);
        spec_[0].assign(kN, {});
        spec_[1].assign(kN, {});
        win_.resize(kN);
        double w2 = 0.0;
        for (int i = 0; i < kN; ++i) {
            win_[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / kN));
            w2 += static_cast<double>(win_[static_cast<std::size_t>(i)]) * win_[static_cast<std::size_t>(i)];
        }
        norm_ = 4.0 / (static_cast<double>(kN) * w2);   // band energy -> a sine's amplitude squared
        reset();
    }
    void reset() noexcept override {
        for (auto& r : ring_) std::fill(r.begin(), r.end(), 0.0f);
        pos_ = 0;
        sinceHop_ = 0;
        clearDisplay();
        resolution_ = -1;
        for (int c = 0; c < 2; ++c) {
            peak_[c] = 0.0f;
            hold_[c] = 0.0f;
            clip_[c] = 0.0f;
        }
        for (auto& f : sumW_) f.clear();
        sum_ = sumMs_ = holdSum_ = 0.0f;
        pushSeen_ = {p(ClearMax), p(HoldL), p(HoldR), p(HoldSum), p(ClearClips)};
        for (int m = 0; m < kMeters; ++m) publishMeter(m, m >= ClipL ? 0.0f : -100.0f);
        weightKey_ = -1;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        pushes();
        design();
        // the meters: sample peaks per side, and the sum (half of L+R) through the weighting
        const float fall = static_cast<float>(std::pow(10.0, -20.0 / 20.0 / fs_));   // peaks fall 20 dB a second
        const float msK = coefMs(fs_, 300.0);
        const bool rms = p(MeterMode) >= 0.5f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x[2] = {L[i], R[i]};
            for (int c = 0; c < 2; ++c) {
                const float a = std::fabs(x[c]);
                peak_[c] = std::max(a, peak_[c] * fall);
                hold_[c] = std::max(hold_[c], a);
                if (a >= 0.9999f) clip_[c] = 1.0f;
            }
            float s = 0.5f * (x[0] + x[1]);
            for (int k = 0; k < nW_; ++k) s = sumW_[static_cast<std::size_t>(k)].tick(s);
            s *= wNorm_;
            const float as = std::fabs(s);
            sum_ = std::max(as, sum_ * fall);
            sumMs_ = s * s + msK * (sumMs_ - s * s);
            const float shown = rms ? std::sqrt(2.0f * sumMs_) : sum_;   // RMS reads a sine like its peak
            holdSum_ = std::max(holdSum_, shown);
            // the bands
            ring_[0][static_cast<std::size_t>(pos_)] = x[0];
            ring_[1][static_cast<std::size_t>(pos_)] = x[1];
            pos_ = (pos_ + 1) % kN;
            if (++sinceHop_ == kHop) {
                sinceHop_ = 0;
                analyse();
            }
        }
        auto dbv = [](float v) { return v > 1e-6f ? dbOf(v) : -100.0f; };
        publishMeter(PeakL, dbv(peak_[0]));
        publishMeter(PeakR, dbv(peak_[1]));
        publishMeter(Sum, dbv(rms ? std::sqrt(2.0f * sumMs_) : sum_));
        publishMeter(HoldPeakL, dbv(hold_[0]));
        publishMeter(HoldPeakR, dbv(hold_[1]));
        publishMeter(HoldSumM, dbv(holdSum_));
        publishMeter(ClipL, clip_[0]);
        publishMeter(ClipR, clip_[1]);
    }

private:
    static float at(const std::array<std::array<std::atomic<float>, kRtaMaxBands>, 2>& a, int side, int b) noexcept {
        if (side < 0 || side > 1 || b < 0 || b >= kRtaMaxBands) return kFloor;
        return a[static_cast<std::size_t>(side)][static_cast<std::size_t>(b)].load(std::memory_order_relaxed);
    }
    void clearDisplay() noexcept {
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < kRtaMaxBands; ++b) {
                level_[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)].store(kFloor, std::memory_order_relaxed);
                max_[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)].store(kFloor, std::memory_order_relaxed);
                avg_[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)] = 0.0;
                cur_[static_cast<std::size_t>(c)][static_cast<std::size_t>(b)] = kFloor;
            }
        for (int b = 0; b < kRtaMaxBands; ++b) {
            angle_[static_cast<std::size_t>(b)].store(0.0f, std::memory_order_relaxed);
            spd_[static_cast<std::size_t>(b)].store(kFloor, std::memory_order_relaxed);
            spdE_[static_cast<std::size_t>(b)] = 0.0;
            spdA_[static_cast<std::size_t>(b)] = 0.0;
        }
    }
    void pushes() noexcept {
        const std::array<float, 5> now{p(ClearMax), p(HoldL), p(HoldR), p(HoldSum), p(ClearClips)};
        if (now[0] != pushSeen_[0])
            for (auto& side : max_)
                for (auto& m : side) m.store(kFloor, std::memory_order_relaxed);
        if (now[1] != pushSeen_[1]) hold_[0] = 0.0f;
        if (now[2] != pushSeen_[2]) hold_[1] = 0.0f;
        if (now[3] != pushSeen_[3]) holdSum_ = 0.0f;
        if (now[4] != pushSeen_[4]) clip_[0] = clip_[1] = 0.0f;
        pushSeen_ = now;
    }
    // the sum meter's weighting filter (A / B / C from their poles, 0 dB at 1 kHz)
    void design() noexcept {
        const int w = std::clamp(static_cast<int>(std::lround(p(Weighting))), 0, 3);
        if (w == weightKey_) return;
        weightKey_ = w;
        using namespace weights;
        std::array<hc::fx::BiquadCoefs, 6> c{};
        nW_ = 0;
        auto hp = [&](double hz) { c[static_cast<std::size_t>(nW_++)] = onePole(fs_, hz, true); };
        auto lp = [&](double hz) { c[static_cast<std::size_t>(nW_++)] = onePole(fs_, hz, false); };
        if (w == A) { hp(kF1); hp(kF1); hp(kF2); hp(kF3); lp(kF4); lp(kF4); }
        else if (w == B) { hp(kF1); hp(kF1); hp(kF5); lp(kF4); lp(kF4); }
        else if (w == weights::C) { hp(kF1); hp(kF1); lp(kF4); lp(kF4); }
        double norm = 0.0;
        for (int i = 0; i < nW_; ++i) norm += c[static_cast<std::size_t>(i)].magnitudeDb(fs_, 1000.0);
        wNorm_ = static_cast<float>(std::pow(10.0, -norm / 20.0));
        for (int i = 0; i < 6; ++i) {
            sumW_[static_cast<std::size_t>(i)].c = c[static_cast<std::size_t>(i)];
            sumW_[static_cast<std::size_t>(i)].clear();
        }
    }

    void analyse() noexcept {
        if (p(Freeze) >= 0.5f) return;
        const int res = std::clamp(static_cast<int>(std::lround(p(Resolution))), 0, 2);
        if (res != resolution_) {   // the bands changed: start the display over
            resolution_ = res;
            clearDisplay();
        }
        for (int c = 0; c < 2; ++c) {
            auto& sp = spec_[static_cast<std::size_t>(c)];
            const auto& rg = ring_[static_cast<std::size_t>(c)];
            for (int i = 0; i < kN; ++i) sp[static_cast<std::size_t>(i)] = {rg[static_cast<std::size_t>((pos_ + i) % kN)] * win_[static_cast<std::size_t>(i)], 0.0f};
            fft_.forward(sp.data());
        }
        const int nb = bandCount(res), weighting = std::clamp(static_cast<int>(std::lround(p(Weighting))), 0, 3);
        const bool rms = p(Mode) >= 0.5f, sum = p(Channels) >= 0.5f;
        const double hopS = static_cast<double>(kHop) / fs_, resp = std::max(0.05f, p(Response));
        const float fallDb = static_cast<float>(20.0 * hopS / resp);      // PEAK: 20 dB in RESPONSE
        const double keep = std::exp(-hopS / resp);                         // RMS: the average's memory
        const double binHz = fs_ / kN;
        for (int b = 0; b < nb; ++b) {
            // the band's edges: the low bands RESOLUTION wide around their centre; above, halfway (in octaves) to the
            // neighbours (the first high band starts where the low ones end)
            const int low = lowBands(res);
            const double c0 = bandHz(res, b);
            const double loE = b < low ? c0 - 0.5 * kLowStep[res] : b == low ? 240.0 + 0.5 * kLowStep[res] : std::sqrt(bandHz(res, b - 1) * c0);
            const double hiE = b < low ? c0 + 0.5 * kLowStep[res] : b + 1 < nb ? std::sqrt(c0 * bandHz(res, b + 1)) : 21000.0;
            const int k0 = std::max(1, static_cast<int>(std::ceil(loE / binHz))), k1 = std::min(kN / 2 - 1, static_cast<int>(std::floor(hiE / binHz)));
            double eL = 0.0, eR = 0.0, cross = 0.0;
            for (int k = k0; k <= std::max(k0, k1); ++k) {
                const auto l = spec_[0][static_cast<std::size_t>(k)], r = spec_[1][static_cast<std::size_t>(k)];
                eL += std::norm(l);
                eR += std::norm(r);
                cross += static_cast<double>(l.real()) * r.real() + static_cast<double>(l.imag()) * r.imag();
            }
            const double wDb = weights::curveDb(weighting, bandHz(res, b));
            const double e[2] = {sum ? 0.5 * (eL + eR) : eL, eR};
            for (int c = 0; c < (sum ? 1 : 2); ++c) {
                const auto u = static_cast<std::size_t>(c), v = static_cast<std::size_t>(b);
                const double amp2 = e[c] * norm_;
                float shown;
                if (rms) {
                    avg_[u][v] = amp2 + keep * (avg_[u][v] - amp2);
                    shown = avg_[u][v] > 1e-14 ? static_cast<float>(10.0 * std::log10(avg_[u][v]) + wDb) : kFloor;
                } else {
                    const float now = amp2 > 1e-14 ? static_cast<float>(10.0 * std::log10(amp2) + wDb) : kFloor;
                    cur_[u][v] = std::max(now, cur_[u][v] - fallDb);
                    shown = cur_[u][v];
                }
                shown = std::max(shown, kFloor);
                level_[u][v].store(shown, std::memory_order_relaxed);
                // the max is kept even while its line is hidden
                if (shown > max_[u][v].load(std::memory_order_relaxed)) max_[u][v].store(shown, std::memory_order_relaxed);
            }
            if (sum) {   // one set of bars: the right shows the same
                level_[1][static_cast<std::size_t>(b)].store(level_[0][static_cast<std::size_t>(b)].load(std::memory_order_relaxed), std::memory_order_relaxed);
                max_[1][static_cast<std::size_t>(b)].store(max_[0][static_cast<std::size_t>(b)].load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            // the stereo position: where between the sides (45 degrees each way), and past that when out of phase
            const double tot = eL + eR;
            double ang = 0.0;
            if (tot > 1e-20) {
                ang = std::atan2(std::sqrt(eR), std::sqrt(eL)) * 180.0 / std::numbers::pi - 45.0;
                const double corr = cross / std::max(std::sqrt(eL * eR), 1e-30);
                if (corr < 0.0) {
                    const double s = ang >= 0.0 ? 1.0 : -1.0;
                    ang = s * (std::fabs(ang) + (90.0 - std::fabs(ang)) * std::min(1.0, -corr));
                }
            }
            const auto v = static_cast<std::size_t>(b);
            spdE_[v] = tot * norm_ * 0.5 + keep * (spdE_[v] - tot * norm_ * 0.5);
            spdA_[v] = ang + 0.5 * (spdA_[v] - ang);
            spd_[v].store(spdE_[v] > 1e-14 ? static_cast<float>(10.0 * std::log10(spdE_[v])) : kFloor, std::memory_order_relaxed);
            angle_[v].store(static_cast<float>(spdA_[v]), std::memory_order_relaxed);
        }
    }

    double fs_ = 48000.0, norm_ = 1.0;
    hc::fx::Fft fft_;
    std::array<std::vector<float>, 2> ring_;
    std::array<std::vector<std::complex<float>>, 2> spec_;
    std::vector<float> win_;
    int pos_ = 0, sinceHop_ = 0, resolution_ = -1, weightKey_ = -1, nW_ = 0;
    std::array<std::array<std::atomic<float>, kRtaMaxBands>, 2> level_{}, max_{};
    std::array<std::array<double, kRtaMaxBands>, 2> avg_{};
    std::array<std::array<float, kRtaMaxBands>, 2> cur_{};
    std::array<std::atomic<float>, kRtaMaxBands> angle_{}, spd_{};
    std::array<double, kRtaMaxBands> spdE_{}, spdA_{};
    std::array<hc::fx::Biquad, 6> sumW_{};
    float wNorm_ = 1.0f, peak_[2]{}, hold_[2]{}, clip_[2]{}, sum_ = 0.0f, sumMs_ = 0.0f, holdSum_ = 0.0f;
    std::array<float, 5> pushSeen_{};
};

} // namespace
} // namespace stakeout

std::unique_ptr<hc::fx::Effect> makeStakeout() { return std::make_unique<stakeout::Impl>(); }

} // namespace hem::rack
