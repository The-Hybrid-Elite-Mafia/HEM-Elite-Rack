// THE AUDITOR — loudness meter with gain and a true-peak limiter (see Garage.hpp).
//
// The sound: GAIN, then (always delayed by kAuditorLatency, so the latency never changes) the LIMITER: the
// true peak is read 4x oversampled one millisecond ahead, the gain eases down before a peak arrives and lets
// go over 100 ms. What comes out is measured: each side through the WEIGHTING and the optional pre-filters,
// squared and added up per 100 ms. Momentary = the last 400 ms, short term = the last SHORT TIME, long term =
// everything since RESET (EBU: gated at -70 LUFS and 10 LU under the gated mean; AVERAGE: no relative gate; DIALOGUE:
// only the 100 ms pieces where the speech band carries most of the sound), range = EBU Tech 3342 on the
// 3-second short-term values.
#include "hem/rack/Garage.hpp"

#include "hem/rack/KWeight.hpp"

#include "../arsenal/Common.hpp"
#include "WeightKit.hpp"

#include <complex>
#include <vector>

namespace hem::rack {
namespace auditor {
namespace {
using namespace arsenal;
using weights::onePole;
using weights::r468;

const char* const kMethodN[] = {"EBU", "AVERAGE", "DIALOGUE"};
const char* const kChanN[] = {"BOTH", "LEFT", "RIGHT"};
const char* const kWeightN[] = {"BS.1770", "LEQ(A)", "LEQ(B)", "LEQ(C)", "LEQ(M)", "NONE"};
const char* const kShortN[] = {"400 MS", "1 S", "2 S", "3 S", "10 S", "30 S", "1 MIN", "2 MIN"};
const char* const kScaleN[] = {"LKFS", "LU 18", "LUFS 18", "LU 9", "LUFS 9"};
const char* const kSlopeN[] = {"12 DB", "6 DB"};
const char* const kMeasureN[] = {"PAUSE", "PLAY"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"method", "Method", "", 0, 2, Ebu, Taper::Choice, kMethodN},
    {"channels", "Channels", "", 0, 2, 0, Taper::Choice, kChanN},
    {"weighting", "Weighting", "", 0, 5, Bs1770, Taper::Choice, kWeightN},
    {"shorttime", "Short Time", "", 0, 7, 3, Taper::Choice, kShortN},
    {"shortmax", "Short Max", "LUFS", -37.0f, -12.0f, -37.0f},
    {"shortmin", "Short Min", "LUFS", -91.0f, -12.0f, -91.0f},
    {"truepeakmax", "True Peak Max", "dBTP", -25.0f, 0.0f, -1.0f},
    {"target", "Target", "LUFS", -34.0f, -9.0f, -24.0f},
    {"scale", "Scale", "", 0, 4, 2, Taper::Choice, kScaleN},
    {"lpfon", "Low-Pass", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"lpffreq", "Low-Pass Freq", "Hz", 2000.0f, 18000.0f, 10000.0f, Taper::Log},
    {"lpfslope", "Low-Pass Slope", "", 0, 1, 0, Taper::Choice, kSlopeN},
    {"hpfon", "High-Pass", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"hpffreq", "High-Pass Freq", "Hz", 20.0f, 2000.0f, 120.0f, Taper::Log},
    {"hpfslope", "High-Pass Slope", "", 0, 1, 0, Taper::Choice, kSlopeN},
    {"measure", "Measure", "", 0, 1, 1, Taper::Choice, kMeasureN},
    {"reset", "Reset", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"clearpeaks", "Clear Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"limiter", "Limiter", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"gain", "Gain", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeterInfo[kMeters] = {
    {"SHORT TERM", "LUFS", -60.0f, 0.0f}, {"LONG TERM", "LUFS", -60.0f, 0.0f}, {"RANGE", "LU", 0.0f, 30.0f}, {"MOMENTARY", "LUFS", -60.0f, 0.0f},
    {"TRUE PEAK", "dBTP", -58.0f, 6.0f},  {"LIMITER", "dB", -24.0f, 0.0f},     {"UNDERS", "", 0.0f, 999.0f}, {"OVERS", "", 0.0f, 999.0f},
    {"TIMER", "s", 0.0f, 86400.0f},       {"TRIM", "dB", -36.0f, 12.0f},
};
constexpr int kShortHops[8] = {4, 10, 20, 30, 100, 300, 600, 1200};
constexpr int kGateBins = 8000;   // -70 .. +10 in 0.01 steps
constexpr int kBins = 800;        // -70 .. +10 in 0.1 steps
constexpr std::uint32_t kLook = 48;
constexpr int kFir = 1023;        // LEQ(M): the 468 curve as a linear-phase FIR (measurement only)
inline double lufsOf(double ms) { return ms > 0.0 ? -0.691 + 10.0 * std::log10(ms) : -100.0; }

class Impl final : public hc::fx::Effect {
public:
    Impl() : tp_{OS(1.0), OS(1.0)} { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }
    std::uint32_t latency() const noexcept override { return kAuditorLatency; }
    bool paramActive(int i) const noexcept override {
        if (i == ShortTime) return p(Method) >= 0.5f;   // EBU: always 3 s
        if (i == LpfFreq || i == LpfSlope) return p(LpfOn) >= 0.5f;
        if (i == HpfFreq || i == HpfSlope) return p(HpfOn) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        hop_ = static_cast<std::uint32_t>(std::lround(0.1 * fs));
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        up_.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        need_.assign(static_cast<std::size_t>(mb), 1.0f);
        gateE_.assign(kGateBins, 0.0);
        gateN_.assign(kGateBins, 0u);
        hist_.assign(kBins, 0u);
        hops_.assign(1200, 0.0);
        win_.assign(kLook + 1, 1.0f);
        delay_.prepare(kAuditorLatency);
        // LEQ(M): the 468 curve, 0 dB at 2 kHz, as a windowed linear-phase FIR
        fir_.assign(kFir, 0.0f);
        const double ref = r468(2000.0);
        for (int n = 0; n < kFir; ++n) {
            double acc = 0.0;
            for (int k = 0; k <= kFir / 2; ++k) {
                const double f = k * fs / kFir, mag = k == 0 ? 0.0 : r468(f) / ref;
                acc += (k == 0 ? 1.0 : 2.0) * mag * std::cos(2.0 * std::numbers::pi * k * (n - kFir / 2) / kFir);
            }
            const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / (kFir - 1));
            fir_[static_cast<std::size_t>(n)] = static_cast<float>(acc / kFir * w);
        }
        for (auto& h : firHist_) h.assign(kFir, 0.0f);
        reset();
    }

    void reset() noexcept override {
        resetMeasure();
        for (auto& o : tp_) o.clear();
        for (auto& ch : wt_) for (auto& b : ch) b.clear();
        for (auto& ch : pre_) for (auto& b : ch) b.clear();
        for (auto& h : firHist_) std::fill(h.begin(), h.end(), 0.0f);
        firPos_ = 0;
        delay_.clear();
        std::fill(win_.begin(), win_.end(), 1.0f);
        winPos_ = 0;
        g_ = 1.0f;
        tpMax_ = 0.0f;
        grMax_ = 0.0f;
        hops_.assign(hops_.size(), 0.0);
        hopIdx_ = 0;
        hopsDone_ = 0;
        acc_ = spAcc_ = rawAcc_ = 0.0;
        inHop_ = 0;
        key_.fill(-1e9f);
        design();
        resetSeen_ = p(Reset);
        clearSeen_ = p(ClearPeaks);
        gain_.prepare(fs_, 20, hc::fx::dbToGain(p(Gain)));
        publishMeter(ShortTerm, -100.0f);
        publishMeter(Momentary, -100.0f);
        publishMeter(TruePeak, -100.0f);
        publishMeter(Reduction, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Reset) != resetSeen_) {
            resetSeen_ = p(Reset);
            resetMeasure();
        }
        if (p(ClearPeaks) != clearSeen_) {
            clearSeen_ = p(ClearPeaks);
            tpMax_ = 0.0f;
            grMax_ = 0.0f;
        }
        design();
        // GAIN, then the true peak of what's coming, 4x oversampled (each input sample's four)
        const float wG = hc::fx::dbToGain(p(Gain));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = gain_.next(wG);
            L[i] *= g;
            R[i] *= g;
        }
        std::fill(need_.begin(), need_.begin() + n, 0.0f);
        const float* io[2] = {L, R};
        for (int c = 0; c < 2; ++c) {
            tp_[c].up(io[c], up_.data(), n);
            for (std::uint32_t i = 0; i < n; ++i) {
                float m = 0.0f;
                for (std::uint32_t k = 0; k < OS::kFactor; ++k) m = std::max(m, std::fabs(up_[i * OS::kFactor + k]));
                need_[i] = std::max(need_[i], m);
            }
        }
        // the limiter (or just the delay)
        const bool lim = p(Limiter) >= 0.5f;
        const float ceil = hc::fx::dbToGain(p(TruePeakMax)), rel = coefMs(fs_, 100.0);
        float worst = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            win_[winPos_] = lim && need_[i] > ceil ? ceil / need_[i] : 1.0f;
            winPos_ = (winPos_ + 1) % win_.size();
            float want = 1.0f;
            for (float w : win_) want = std::min(want, w);   // the lowest gain the next millisecond needs
            // down quickly (it has the whole millisecond before the peak arrives), back up over 100 ms
            g_ = want < g_ ? g_ + 0.15f * (want - g_) : want + rel * (g_ - want);
            const float l = delay_.l.process(L[i], kAuditorLatency) * g_, r = delay_.r.process(R[i], kAuditorLatency) * g_;
            L[i] = l;
            R[i] = r;
            worst = std::min(worst, dbOf(g_));
        }
        grMax_ = std::min(grMax_, worst);
        // the true peak of what comes out (measured again after the limiter)
        float tpOut = 0.0f;
        for (int c = 0; c < 2; ++c) {
            tpOut_[c].up(io[c], up_.data(), n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) tpOut = std::max(tpOut, std::fabs(up_[i]));
        }
        tpMax_ = std::max(tpMax_, tpOut);
        // the loudness
        const int chans = std::clamp(static_cast<int>(std::lround(p(Channels))), 0, 2);
        const bool measuring = p(Measure) >= 0.5f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x[2] = {L[i], R[i]};
            double e = 0.0, sp = 0.0;
            for (int c = 0; c < 2; ++c) {
                float v = weigh(c, x[c]);
                for (auto& b : pre_[static_cast<std::size_t>(c)]) v = b.tick(v);
                if (chans == 0 || chans == c + 1) e += static_cast<double>(v) * v;
                const float s = speech_[c].tick(x[c]);
                if (chans == 0 || chans == c + 1) sp += static_cast<double>(s) * s;
            }
            ++firPos_;
            if (firPos_ >= kFir) firPos_ = 0;
            acc_ += e;
            spAcc_ += sp;
            rawAcc_ += static_cast<double>(x[0]) * x[0] + static_cast<double>(x[1]) * x[1];
            if (++inHop_ == hop_) endHop(measuring);
            if (measuring) timer_ += 1.0 / fs_;
        }
        publishMeter(TruePeak, tpMax_ > 0.0f ? dbOf(tpMax_) : -100.0f);
        publishMeter(Reduction, grMax_);
        publishMeter(Timer, static_cast<float>(timer_));
        publishMeter(Unders, static_cast<float>(unders_));
        publishMeter(Overs, static_cast<float>(overs_));
    }

private:
    float weigh(int c, float x) noexcept {
        const int w = weight_;
        if (w == LeqM) {
            auto& h = firHist_[static_cast<std::size_t>(c)];
            h[static_cast<std::size_t>(firPos_)] = x;
            float acc = 0.0f;
            int k = firPos_;
            for (int j = 0; j < kFir; ++j) {
                acc += fir_[static_cast<std::size_t>(j)] * h[static_cast<std::size_t>(k)];
                if (--k < 0) k = kFir - 1;
            }
            return acc;
        }
        if (w == NoWeight) return x;
        float v = x;
        auto& f = wt_[static_cast<std::size_t>(c)];
        for (int k = 0; k < nWt_; ++k) v = f[static_cast<std::size_t>(k)].tick(v);
        return v * wtNorm_;
    }

    void design() noexcept {
        const std::array<float, 8> k{p(Weighting), p(LpfOn), p(LpfFreq), p(LpfSlope), p(HpfOn), p(HpfFreq), p(HpfSlope), 0.0f};
        if (k == key_) return;
        key_ = k;
        weight_ = std::clamp(static_cast<int>(std::lround(p(Weighting))), 0, 5);
        // the weighting curves (A, B and C from their poles; normalised to 0 dB at 1 kHz)
        std::array<C, 6> w{};
        nWt_ = 0;
        constexpr double f1 = 20.598997, f2 = 107.65265, f3 = 737.86223, f4 = 12194.217, f5 = 158.5;
        auto hp = [&](double hz) { w[static_cast<std::size_t>(nWt_++)] = onePole(fs_, hz, true); };
        auto lp = [&](double hz) { w[static_cast<std::size_t>(nWt_++)] = onePole(fs_, hz, false); };
        if (weight_ == Bs1770) {
            C sh, h;
            kWeighting(fs_, sh, h);
            w[0] = sh;
            w[1] = h;
            nWt_ = 2;
        } else if (weight_ == LeqA) {
            hp(f1); hp(f1); hp(f2); hp(f3); lp(f4); lp(f4);
        } else if (weight_ == LeqB) {
            hp(f1); hp(f1); hp(f5); lp(f4); lp(f4);
        } else if (weight_ == LeqC) {
            hp(f1); hp(f1); lp(f4); lp(f4);
        }
        double norm = 0.0;
        for (int i = 0; i < nWt_; ++i) norm += w[static_cast<std::size_t>(i)].magnitudeDb(fs_, 1000.0);
        wtNorm_ = weight_ == Bs1770 ? 1.0f : static_cast<float>(std::pow(10.0, -norm / 20.0));
        for (auto& ch : wt_)
            for (int i = 0; i < 6; ++i) ch[static_cast<std::size_t>(i)].c = w[static_cast<std::size_t>(i)];
        // the pre-filters
        const double top = fs_ * 0.45;
        const bool lpOn = p(LpfOn) >= 0.5f, hpOn = p(HpfOn) >= 0.5f;
        const C lpC = !lpOn ? C{} : p(LpfSlope) >= 0.5f ? onePole(fs_, std::min<double>(p(LpfFreq), top), false)
                                                        : C::make(T::LowPass, fs_, std::min<double>(p(LpfFreq), top), 0.7071);
        const C hpC = !hpOn ? C{} : p(HpfSlope) >= 0.5f ? onePole(fs_, p(HpfFreq), true) : C::make(T::HighPass, fs_, p(HpfFreq), 0.7071);
        for (auto& ch : pre_) { ch[0].c = lpC; ch[1].c = hpC; }
        // DIALOGUE: the speech band
        const C sb = C::make(T::BandPass, fs_, 1000.0, 0.55);
        for (auto& s : speech_) s.c = sb;
    }

    void resetMeasure() noexcept {
        std::fill(gateE_.begin(), gateE_.end(), 0.0);
        std::fill(gateN_.begin(), gateN_.end(), 0u);
        std::fill(hist_.begin(), hist_.end(), 0u);
        histEnergy_ = 0.0;
        histCount_ = 0;
        unders_ = overs_ = 0;
        wasOver_ = wasUnder_ = false;
        timer_ = 0.0;
        longTerm_ = -100.0f;
        publishMeter(LongTerm, -100.0f);
        publishMeter(Range, 0.0f);
        publishMeter(Trim, 0.0f);
        publishMeter(Timer, 0.0f);
        publishMeter(Unders, 0.0f);
        publishMeter(Overs, 0.0f);
    }

    float shown(double lufs) const noexcept {   // in the SCALE's units (LU = LUFS + 23)
        if (lufs <= -99.0) return -100.0f;
        const int s = std::clamp(static_cast<int>(std::lround(p(Scale))), 0, 4);
        return static_cast<float>(s == 1 || s == 3 ? lufs + 23.0 : lufs);
    }

    // every 100 ms
    void endHop(bool measuring) noexcept {
        const double hopE = acc_ / static_cast<double>(hop_);
        const double spFrac = rawAcc_ > 0.0 ? spAcc_ / rawAcc_ : 0.0;
        hops_[hopIdx_] = hopE;
        hopIdx_ = (hopIdx_ + 1) % hops_.size();
        ++hopsDone_;
        acc_ = spAcc_ = rawAcc_ = 0.0;
        inHop_ = 0;
        auto meanOf = [&](std::size_t k) {
            double s = 0;
            for (std::size_t j = 1; j <= k; ++j) s += hops_[(hopIdx_ + hops_.size() - j) % hops_.size()];
            return s / static_cast<double>(k);
        };
        const int method = std::clamp(static_cast<int>(std::lround(p(Method))), 0, 2);
        if (hopsDone_ >= 4) {
            const double m = meanOf(4);
            publishMeter(Momentary, shown(lufsOf(m)));
            // the long term
            const double lm = lufsOf(m);
            const bool counts = measuring && lm > -70.0 && (method != Dial || spFrac > 0.5);
            if (counts) {
                const auto bin = static_cast<std::size_t>(std::clamp(static_cast<int>((lm + 70.0) * 100.0), 0, kGateBins - 1));
                gateE_[bin] += m;
                ++gateN_[bin];
            }
            double s1 = 0;
            std::uint64_t c1 = 0;
            for (int k = 0; k < kGateBins; ++k) { s1 += gateE_[static_cast<std::size_t>(k)]; c1 += gateN_[static_cast<std::size_t>(k)]; }
            if (c1 > 0) {
                int g0 = 0;
                if (method != Lm1) {   // the relative gate, 10 LU under the gated mean
                    const double gate = lufsOf(s1 / static_cast<double>(c1)) - 10.0;
                    g0 = std::clamp(static_cast<int>(std::ceil((gate + 70.0) * 100.0)), 0, kGateBins);
                }
                double s2 = 0;
                std::uint64_t c2 = 0;
                for (int k = g0; k < kGateBins; ++k) { s2 += gateE_[static_cast<std::size_t>(k)]; c2 += gateN_[static_cast<std::size_t>(k)]; }
                longTerm_ = c2 > 0 ? static_cast<float>(lufsOf(s2 / static_cast<double>(c2))) : -100.0f;
                publishMeter(LongTerm, shown(longTerm_));
                const bool targetOn = p(Target) > -33.5f;
                publishMeter(Trim, targetOn && longTerm_ > -99.0f ? std::clamp(p(Target) - longTerm_, -36.0f, 12.0f) : 0.0f);
            }
        }
        const int shortHops = method == Ebu ? 30 : kShortHops[std::clamp(static_cast<int>(std::lround(p(ShortTime))), 0, 7)];
        if (hopsDone_ >= static_cast<std::uint64_t>(shortHops)) {
            const double lst = lufsOf(meanOf(static_cast<std::size_t>(shortHops)));
            publishMeter(ShortTerm, shown(lst));
            if (measuring) {   // the warning counters: each time it crosses the line
                const bool over = p(ShortMax) > -36.5f && lst > p(ShortMax);
                const bool under = p(ShortMin) > -90.5f && lst < p(ShortMin) && lst > -70.0;
                if (over && !wasOver_) ++overs_;
                if (under && !wasUnder_) ++unders_;
                wasOver_ = over;
                wasUnder_ = under;
            }
        }
        // the loudness range, from the 3-second values
        if (measuring && hopsDone_ >= 30) {
            const double st = meanOf(30), lst = lufsOf(st);
            if (lst > -70.0) {
                const int bin = std::clamp(static_cast<int>((lst + 70.0) * 10.0), 0, kBins - 1);
                ++hist_[static_cast<std::size_t>(bin)];
                histEnergy_ += st;
                ++histCount_;
                const double gate = lufsOf(histEnergy_ / static_cast<double>(histCount_)) - 20.0;
                const int g0 = std::clamp(static_cast<int>(std::ceil((gate + 70.0) * 10.0)), 0, kBins);
                std::uint64_t total = 0;
                for (int b = g0; b < kBins; ++b) total += hist_[static_cast<std::size_t>(b)];
                if (total > 0) {
                    auto pct = [&](double q) {
                        const auto want = static_cast<std::uint64_t>(std::ceil(q * static_cast<double>(total)));
                        std::uint64_t run = 0;
                        for (int b = g0; b < kBins; ++b) {
                            run += hist_[static_cast<std::size_t>(b)];
                            if (run >= std::max<std::uint64_t>(want, 1)) return -70.0 + (b + 0.5) * 0.1;
                        }
                        return 10.0;
                    };
                    publishMeter(Range, static_cast<float>(std::max(0.0, pct(0.95) - pct(0.10))));
                }
            }
        }
    }

    double fs_ = 48000.0;
    std::uint32_t hop_ = 4800, inHop_ = 0;
    OS tp_[2], tpOut_[2] = {OS(1.0), OS(1.0)};
    std::vector<float> up_, need_, win_, fir_;
    std::array<std::vector<float>, 2> firHist_;
    int firPos_ = 0;
    std::size_t winPos_ = 0;
    Delay2 delay_;
    std::array<std::array<hc::fx::Biquad, 6>, 2> wt_{};
    std::array<std::array<hc::fx::Biquad, 2>, 2> pre_{};
    hc::fx::Biquad speech_[2];
    int weight_ = Bs1770, nWt_ = 2;
    float wtNorm_ = 1.0f;
    std::vector<double> gateE_, hops_;
    std::vector<std::uint32_t> gateN_, hist_;
    std::size_t hopIdx_ = 0;
    std::uint64_t hopsDone_ = 0, histCount_ = 0;
    double acc_ = 0.0, spAcc_ = 0.0, rawAcc_ = 0.0, histEnergy_ = 0.0, timer_ = 0.0;
    float g_ = 1.0f, tpMax_ = 0.0f, grMax_ = 0.0f, longTerm_ = -100.0f, resetSeen_ = 0.0f, clearSeen_ = 0.0f;
    int unders_ = 0, overs_ = 0;
    bool wasOver_ = false, wasUnder_ = false;
    std::array<float, 8> key_{};
    hc::fx::Smoothed gain_;
};

} // namespace
} // namespace auditor

std::unique_ptr<hc::fx::Effect> makeAuditor() { return std::make_unique<auditor::Impl>(); }

} // namespace hem::rack
