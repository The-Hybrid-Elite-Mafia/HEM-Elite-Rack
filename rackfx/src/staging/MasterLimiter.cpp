// =============================================================================
//  44 MASTER LIMITER — the last plugin on a master: loud and safe, no peak over the CEILING.
//  THRESHOLD pushes the level in, CEILING is the most the output reaches. LOOKAHEAD (0.1..10 ms)
//  sets how early the limiter starts to turn down; the reported latency is a fixed 10 ms in every
//  setting (the host wrapper cannot announce a change), the lookahead window moves inside it.
//  ISP on: peaks between the samples are caught too, read at the OVERSAMPLING rate (1x = off,
//  2x / 4x / 8x interpolated); TRUE PEAK on: the meters read true peak.
//  LIMITER MODE: BRICKWALL (hard knee) / TRANSPARENT (gentler, slower recovery) / LOUD (a soft knee in
//  the top 3 dB and quicker recovery, for density). CHARACTER: CLEAN / PUNCH (quicker recovery after
//  hits) / MODERN (two-stage recovery) / ANALOG (a faint even harmonic before the limiter) /
//  AGGRESSIVE (a soft pre-shave of the very top). RELEASE (1..2000 ms) with RELEASE MODE AUTO
//  (longer when the limiting is sustained) / FAST (0.3x) / SLOW (3x). LINK: L/R = one gain on both
//  sides, MID = the cut lands on the mid first, SIDE = on the side first (all stay under the ceiling).
//  Reported latency: 10 ms + 6 samples (the peak reader looks at the middle of its taps).
//  INPUT / OUTPUT GAIN, DITHER (TPDF) at BIT DEPTH 16 / 24.
//  Different from PEAKCITADEL (34: multiband, profiles) — single band, settable lookahead, link modes.
//  Dead flat on load: THRESHOLD 0, CEILING 0 dBFS, gains 0 dB, DITHER OFF (normal audio passes, delayed
//  by the reported latency).
//  ASSUMED: OVERSAMPLING default 4x; mode / character release factors; ANALOG = 1 % 2nd harmonic;
//  AGGRESSIVE shave above 90 % of the ceiling; OUTPUT GAIN is applied before the ceiling stage so the
//  ceiling always holds (input and output gain +-12 dB).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>
#include <numbers>
#include <vector>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kThr, kCeil, kRel, kLook, kChar, kTp, kIsp, kInGain, kOutGain, kDither, kBits, kMode, kOs, kRelMode, kLink, kBypass, kCount };
const char* const kCharN[] = {"CLEAN", "PUNCH", "MODERN", "ANALOG", "AGGRESSIVE"};
const char* const kBitsN[] = {"16", "24"};
const char* const kModeN[] = {"BRICKWALL", "TRANSPARENT", "LOUD"};
const char* const kOsN[] = {"1X", "2X", "4X", "8X"};
const char* const kRelN[] = {"AUTO", "FAST", "SLOW"};
const char* const kLinkN[] = {"L/R", "MID", "SIDE"};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -40.0f, 0.0f, 0.0f},
    {"ceiling", "CEILING", "dBFS", -4.0f, 0.0f, 0.0f},
    {"release", "RELEASE", "ms", 1.0f, 2000.0f, 100.0f, Taper::Log},
    {"lookahead", "LOOKAHEAD", "ms", 0.1f, 10.0f, 1.0f, Taper::Log},
    {"character", "CHARACTER", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kCharN},
    {"true_peak", "TRUE PEAK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"isp", "ISP", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"input_gain", "INPUT GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"output_gain", "OUTPUT GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"dither", "DITHER", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bit_depth", "BIT DEPTH", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kBitsN},
    {"limiter_mode", "LIMITER MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"oversampling", "OVERSAMPLING", "", 0.0f, 3.0f, 2.0f, Taper::Choice, kOsN},
    {"release_mode", "RELEASE MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kRelN},
    {"link", "LINK", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kLinkN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 0}, {"OUT", "dB", -60, 0}, {"INPUT VU", "dB", -60, 6, true}, {"OUTPUT VU", "dB", -60, 6, true}, {"GAIN REDUCTION", "dB", -20, 0}};

// lookahead gain over a settable window: lowest need over the window, release, then a box average
// over the window, so the gain at the protected sample is never above what that sample needs
struct Window {
    std::vector<float> dqV, box;
    std::vector<std::uint32_t> dqT;
    std::uint32_t cap = 2, w = 1, head = 0, tail = 0, t = 0, bPos = 0;
    double sum = 1;
    float rel = 1, slow = 1;
    void prepare(std::uint32_t maxW) { cap = maxW + 2; dqV.assign(cap, 1.0f); dqT.assign(cap, 0); box.assign(maxW, 1.0f); w = 1; clear(); }
    void clear() noexcept { head = tail = t = 0; bPos = 0; rel = slow = 1; std::fill(box.begin(), box.end(), 1.0f); sum = static_cast<double>(w); }
    void setWindow(std::uint32_t nw) noexcept {
        nw = std::clamp<std::uint32_t>(nw, 1, static_cast<std::uint32_t>(box.size()));
        if (nw == w) return;
        w = nw; bPos = 0;
        std::fill(box.begin(), box.begin() + w, rel);   // restart the average at the present gain
        sum = static_cast<double>(rel) * w;
    }
    float tick(float need, float relC, float slowC, bool twoStage) noexcept {
        while (head != tail && dqV[(tail + cap - 1) % cap] >= need) tail = (tail + cap - 1) % cap;
        dqV[tail] = need; dqT[tail] = t; tail = (tail + 1) % cap;
        while (t - dqT[head] > w) head = (head + 1) % cap;
        const float m = dqV[head];
        ++t;
        rel = m < rel ? m : m + relC * (rel - m);
        if (twoStage) { slow = m < slow ? m : m + slowC * (slow - m); rel = std::min(rel, 0.5f * (rel + slow)); }   // MODERN: a slow second stage
        sum += static_cast<double>(rel) - static_cast<double>(box[bPos]);
        box[bPos] = rel; bPos = (bPos + 1) % w;
        return std::min(1.0f, static_cast<float>(sum / static_cast<double>(w)));
    }
};

// interpolated peak read: windowed-sinc, 12 taps a phase, phases 1/os .. (os-1)/os. It reads 6 samples
// back (the middle of its taps), so the audio is delayed 6 samples more than the lookahead to match.
constexpr std::uint32_t kIspLag = 6;
struct Isp {
    std::array<float, 12> z{};
    int pos = 0;
    std::array<std::array<float, 12>, 7> h{};
    int os = 1;
    void prepare(int factor) {
        os = factor;
        for (int ph = 1; ph < os; ++ph)
            for (int k = 0; k < 12; ++k) {
                const double x = (k - 5.5) - (static_cast<double>(ph) / os - 0.5);
                const double s = std::fabs(x) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                h[static_cast<std::size_t>(ph - 1)][static_cast<std::size_t>(k)] = static_cast<float>(s * (0.5 + 0.5 * std::cos(std::numbers::pi * x / 6.5)));
            }
    }
    void clear() noexcept { z.fill(0); pos = 0; }
    float peak(float x) noexcept {
        z[static_cast<std::size_t>(pos)] = x; pos = (pos + 1) % 12;
        // the reading covers the two middle taps (6 and 5 samples old) and the points between them
        float m = std::max(std::fabs(z[static_cast<std::size_t>((pos + 5) % 12)]), std::fabs(z[static_cast<std::size_t>((pos + 6) % 12)]));
        (void)x;
        for (int ph = 0; ph < os - 1; ++ph) {
            float acc = 0;
            for (int k = 0; k < 12; ++k) acc += h[static_cast<std::size_t>(ph)][static_cast<std::size_t>(k)] * z[static_cast<std::size_t>((pos + k) % 12)];
            m = std::max(m, std::fabs(acc));
        }
        return m;
    }
};

class MasterLimiter final : public hc::fx::Effect {
public:
    MasterLimiter() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_ + kIspLag; }
    bool paramActive(int i) const noexcept override {
        if (i == kBits) return p(kDither) > 0.5f;
        if (i == kOs) return p(kIsp) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lat_ = static_cast<std::uint32_t>(std::lround(fs * 0.010));
        for (auto& w : win_) w.prepare(lat_);
        for (auto& d : aud_) d.prepare(lat_ + kIspLag + 4);
        for (auto& d : needD_) d.prepare(lat_ + 4);
        osKey_ = -1;
        for (auto* s : {&in_, &thrG_, &out_, &ceil_}) s->prepare(fs, 20.0, 1.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& w : win_) w.clear();
        for (auto& d : aud_) d.clear();
        for (auto& d : needD_) { d.clear(); std::fill(d.buf.begin(), d.buf.end(), 1.0f); }   // nothing asked for yet = unity gain
        for (auto& s : isp_) s.clear();
        for (auto& s : ispMeter_) s.clear();
        sus_ = 0; dc_.fill(0); rng_ = 97531u; grM_ = 0; inPk_ = outPk_ = 0; vuI_ = vuO_ = 0; loaded_ = false;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float lat = static_cast<float>(lat_ + kIspLag);
        if (p(kBypass) > 0.5f) {   // bypass keeps the reported delay so the host's timing holds
            for (std::uint32_t i = 0; i < n; ++i) { aud_[0].write(L[i]); aud_[1].write(R[i]); L[i] = aud_[0].read(lat); R[i] = aud_[1].read(lat); }
            return;
        }
        const int osI = static_cast<int>(p(kOs));
        const bool ispOn = p(kIsp) > 0.5f;
        const float osKey = static_cast<float>(ispOn ? osI : 0);
        if (osKey != osKey_) { osKey_ = osKey; const int f = ispOn ? 1 << osI : 1; for (auto& s : isp_) s.prepare(f); for (auto& s : ispMeter_) s.prepare(8); }
        const float inT = kit::dbToGain(p(kInGain)), thrT = kit::dbToGain(-p(kThr)), outT = kit::dbToGain(p(kOutGain)), cT = kit::dbToGain(p(kCeil));
        if (!loaded_) { in_.cur = inT; thrG_.cur = thrT; out_.cur = outT; ceil_.cur = cT; loaded_ = true; }
        const auto w = static_cast<std::uint32_t>(std::clamp(std::lround(p(kLook) * 0.001 * fs_), 1l, static_cast<long>(lat_)));
        for (auto& x : win_) x.setWindow(w);
        const float dly = static_cast<float>(lat_ - w);   // the need waits so the window ends at the protected sample
        const int mode = static_cast<int>(p(kMode)), ch = static_cast<int>(p(kChar)), relMode = static_cast<int>(p(kRelMode)), link = static_cast<int>(p(kLink));
        const bool tpMeter = p(kTp) > 0.5f;
        float relMs = p(kRel);
        if (mode == 1) relMs *= 2.0f; else if (mode == 2) relMs *= 0.5f;
        if (ch == 1) relMs *= 0.6f;
        if (relMode == 1) relMs *= 0.3f; else if (relMode == 2) relMs *= 3.0f;
        const float slowC = hc::fx::onePoleCoef(fs_, relMs * 5.0);
        const float invFs = static_cast<float>(1.0 / fs_);
        const bool dither = p(kDither) > 0.5f;
        const float q = p(kBits) > 0.5f ? 8388608.0f : 32768.0f;
        const float vuC = hc::fx::onePoleCoef(fs_, 300.0);
        grM_ = 0;
        float inPk = 0, outPk = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(inT) * thrG_.next(thrT) * out_.next(outT);
            const float c = ceil_.next(cT);
            float xl = L[i] * g, xr = R[i] * g;
            inPk = std::max(inPk, std::max(std::fabs(L[i]), std::fabs(R[i])));
            if (ch == 3) {   // ANALOG: a faint even harmonic
                xl += 0.01f * xl * xl; xr += 0.01f * xr * xr;
                dc_[0] += 0.0005f * (xl - dc_[0]); dc_[1] += 0.0005f * (xr - dc_[1]);
                xl -= dc_[0]; xr -= dc_[1];
            } else if (ch == 4) {   // AGGRESSIVE: shave the very top softly first
                auto shave = [c](float v) { const float k = 0.9f * c, a = std::fabs(v); return a <= k ? v : std::copysign(k + (c - k) * std::tanh((a - k) / (c - k)), v); };
                xl = shave(xl); xr = shave(xr);
            }
            // peaks (between the samples too, with ISP)
            const float m = 0.5f * (xl + xr), s = 0.5f * (xl - xr);
            float pl, pr, pm, ps;
            pl = isp_[0].peak(xl); pr = isp_[1].peak(xr); pm = isp_[2].peak(m); ps = isp_[3].peak(s);   // 1x when ISP is off
            // needs: LOUD adds a soft knee in the top 3 dB
            auto need = [mode, c](float pk) {
                if (pk <= 1e-9f) return 1.0f;
                if (mode != 2) return pk > c ? c / pk : 1.0f;
                const float l = kit::gainToDb(pk), cd = kit::gainToDb(c);
                if (l < cd - 3.0f) return 1.0f;
                return std::min(1.0f, kit::dbToGain(cd - 3.0f * std::exp(-(l - cd + 3.0f) / 3.0f) - l));
            };
            float nA, nB;   // L/R: one gain; MID / SIDE: gain on the first component, then the other if it alone is too big
            if (link == 0) { nA = nB = need(std::max(pl, pr)); }
            else {
                const float first = link == 1 ? pm : ps, second = link == 1 ? ps : pm;
                const float nSecond = second > c ? c / second : 1.0f;   // the other part only moves if it alone is over the ceiling
                const float rest = std::max(0.0f, c - second * nSecond);
                const float nFirst = first + second * nSecond > c ? (first > 1e-9f ? std::clamp(rest / first, 0.0f, 1.0f) : 1.0f) : 1.0f;
                nA = link == 1 ? nFirst : nSecond; nB = link == 1 ? nSecond : nFirst;   // A = mid gain, B = side gain
            }
            needD_[0].write(nA); needD_[1].write(nB);
            const float dA = needD_[0].read(dly), dB = needD_[1].read(dly);
            // release: AUTO lengthens with sustained limiting
            float rms = relMs;
            if (relMode == 0) { sus_ = std::clamp(sus_ + invFs * (std::min(dA, dB) < 0.89f ? 0.7f : -0.35f), 0.0f, 1.0f); rms *= 0.3f + 1.2f * sus_; }
            const float relC = hc::fx::onePoleCoef(fs_, rms);
            const float gA = win_[0].tick(dA, relC, slowC, ch == 2), gB = link == 0 ? gA : win_[1].tick(dB, relC, slowC, ch == 2);
            aud_[0].write(xl); aud_[1].write(xr);
            const float al = aud_[0].read(lat), ar = aud_[1].read(lat);
            float yl, yr;
            if (link == 0) { yl = al * gA; yr = ar * gA; }
            else { const float am = 0.5f * (al + ar) * gA, as = 0.5f * (al - ar) * gB; yl = am + as; yr = am - as; }
            grM_ = std::max(grM_, -kit::gainToDb(std::min(gA, gB)));
            if (dither) {
                rng_ = rng_ * 1664525u + 1013904223u; const float r1 = static_cast<float>(rng_ >> 8) / 16777216.0f;
                rng_ = rng_ * 1664525u + 1013904223u; const float r2 = static_cast<float>(rng_ >> 8) / 16777216.0f;
                yl = std::round(yl * q + (r1 - r2)) / q;
                rng_ = rng_ * 1664525u + 1013904223u; const float r3 = static_cast<float>(rng_ >> 8) / 16777216.0f;
                rng_ = rng_ * 1664525u + 1013904223u; const float r4 = static_cast<float>(rng_ >> 8) / 16777216.0f;
                yr = std::round(yr * q + (r3 - r4)) / q;
            }
            const float po = tpMeter ? std::max(ispMeter_[0].peak(yl), ispMeter_[1].peak(yr)) : std::max(std::fabs(yl), std::fabs(yr));
            outPk = std::max(outPk, po);
            const float ai = 0.5f * (std::fabs(L[i]) + std::fabs(R[i])), ao = 0.5f * (std::fabs(yl) + std::fabs(yr));
            vuI_ = ai + vuC * (vuI_ - ai); vuO_ = ao + vuC * (vuO_ - ao);
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        inPk_ = inPk; outPk_ = outPk;
        publishMeter(0, kit::gainToDb(inPk_)); publishMeter(1, kit::gainToDb(outPk_));
        publishMeter(2, kit::gainToDb(vuI_ * 1.11f)); publishMeter(3, kit::gainToDb(vuO_ * 1.11f));
        publishMeter(4, -grM_); publishReduction(-grM_);
    }

private:
    double fs_ = 48000;
    std::uint32_t lat_ = 480;
    std::array<Window, 2> win_{};
    std::array<hc::fx::DelayLine, 2> aud_{}, needD_{};
    std::array<Isp, 4> isp_{};
    std::array<Isp, 2> ispMeter_{};
    hc::fx::Smoothed in_, thrG_, out_, ceil_;
    std::array<float, 2> dc_{};
    float sus_ = 0, grM_ = 0, inPk_ = 0, outPk_ = 0, vuI_ = 0, vuO_ = 0, osKey_ = -1;
    std::uint32_t rng_ = 97531u;
    bool loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeMasterLimiter() { return std::make_unique<MasterLimiter>(); }

} // namespace hem::staging
