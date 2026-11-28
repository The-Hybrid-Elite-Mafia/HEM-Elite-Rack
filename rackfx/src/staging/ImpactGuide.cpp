// =============================================================================
//  23 IMPACTGUIDE — spectral compressor: the signal is split into about a thousand frequency slices
//  and each slice is turned down only when it pokes over the THRESHOLD, so loud peaks in one band
//  are tamed without ducking the rest of the mix. FOCUS BAND (BELL / LOW SHELF / HIGH SHELF) tells
//  the detector where to listen harder (+ gain = compress that area more, - gain = protect it).
//  TILT leans the result DARK (-) or BRIGHT (+). SPEED SLOW / FAST, KNEE HARD / SOFT.
//  AUTO GAIN puts back the average level taken away; LEVEL MATCH (press) sets the output to match
//  the input loudness heard at the moment of the press.
//  MODE: QUALITY = 2048-point analysis (fine frequency detail); LOW LATENCY = 512-point analysis
//  (faster reaction). The host wrapper cannot announce a latency change, so both modes report the
//  same fixed 2048 samples: LOW LATENCY is padded and only changes the sound, not the delay.
//  Dead flat on load: THRESHOLD 0 dB = no processing, WET / DRY 0 %, OUTPUT 0 dB (the dry signal,
//  delayed by the reported latency).
//  ASSUMED: ratio 4:1, soft knee 12 dB wide, FAST 5 / 60 ms, SLOW 30 / 300 ms, 24 dB max cut,
//  TILT +-6 dB at the band edges, focus bell about one octave wide.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include "hybrid/fx/Fft.hpp"

#include <array>
#include <complex>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kThr, kFreq, kFGain, kShape, kTilt, kSpeed, kKnee, kMix, kOut, kAuto, kMatch, kMode, kCount };
const char* const kShapeN[] = {"BELL", "LOW SHELF", "HIGH SHELF"};
const char* const kSpeedN[] = {"SLOW", "FAST"};
const char* const kKneeN[] = {"HARD", "SOFT"};
const char* const kModeN[] = {"QUALITY", "LOW LATENCY"};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -36.0f, 0.0f, 0.0f},
    {"focus_freq", "FOCUS FREQUENCY", "Hz", 20.0f, 20000.0f, 1000.0f, Taper::Log},
    {"focus_gain", "FOCUS GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"focus_shape", "FOCUS SHAPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kShapeN},
    {"tilt", "TILT DARK / BRIGHT", "dB", -6.0f, 6.0f, 0.0f},
    {"speed", "SPEED", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kSpeedN},
    {"knee", "KNEE", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kKneeN},
    {"mix", "WET / DRY", "%", 0.0f, 100.0f, 0.0f},
    {"output", "OUTPUT GAIN", "dB", -24.0f, 24.0f, 0.0f},
    {"auto_gain", "AUTO GAIN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"level_match", "LEVEL MATCH", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"mode", "MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN},
};
// The full INPUT SPECTRUM and GAIN REDUCTION curves need more than the 10 meter slots the effect
// interface has; four zone readouts are published here, the full curves wait for the Phase 2 UI.
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "VU", -30, 10, true}, {"OUTPUT", "VU", -30, 10, true}, {"INPUT CLIP", "", 0, 1}, {"OUTPUT CLIP", "", 0, 1},
    {"GAIN REDUCTION", "dB", -24, 0}, {"LEVEL MATCH", "dB", -24, 24},
    {"GR LOW", "dB", -24, 0}, {"GR LOW MID", "dB", -24, 0}, {"GR HIGH MID", "dB", -24, 0}, {"GR HIGH", "dB", -24, 0}};

constexpr std::uint32_t kLatency = 2048;

struct Ctx {
    bool active = false;
    float thr = 0, slope = 0.75f, knee = 12, att = 0, relc = 0, focusHz = 1000, focusDb = 0, tilt = 0;
    int shape = 0;
    double fs = 48000;
};

template <int N>
struct Stft {
    static constexpr int kHop = N / 4, kBins = N / 2 + 1;
    hc::fx::Fft fft;
    std::array<float, N> win{};
    std::array<std::array<float, N>, 2> in{}, out{};
    std::array<std::array<std::complex<float>, N>, 2> sp{};
    std::array<float, kBins> env{}, gr{}, tmp{}, wDb{};
    int pos = 0, hop = 0;
    float key = -1;
    float inPow = 0, outPow = 0, grAvg = 0;
    std::array<float, 4> zone{};

    void prepare() {
        fft.prepare(N);
        for (int i = 0; i < N; ++i) win[static_cast<std::size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(kit::kTau * static_cast<float>(i) / N));
        clear();
    }
    void clear() noexcept {
        for (auto& c : in) c.fill(0.0f);
        for (auto& c : out) c.fill(0.0f);
        env.fill(-120.0f); gr.fill(0.0f);
        pos = hop = 0; inPow = outPow = grAvg = 0; zone.fill(0.0f);
    }
    // one stereo sample in, one out (N samples late)
    void tick(float l, float r, float& ol, float& orr, const Ctx& c) noexcept {
        const auto at = static_cast<std::size_t>(pos);
        ol = out[0][at]; orr = out[1][at];
        out[0][at] = out[1][at] = 0.0f;
        in[0][at] = l; in[1][at] = r;
        pos = (pos + 1) % N;
        if (++hop == kHop) { hop = 0; frame(c); }
    }
    void weights(const Ctx& c) noexcept {
        const float k = c.focusHz * 7.0f + c.focusDb * 131.0f + c.tilt * 977.0f + static_cast<float>(c.shape) * 3.0f + static_cast<float>(c.fs) * 1e-3f;
        if (k == key) return;
        key = k;
        for (int b = 0; b < kBins; ++b) {
            const float hz = std::max(10.0f, static_cast<float>(b) * static_cast<float>(c.fs) / N);
            const float oct = std::log2(hz / c.focusHz);
            float f = 0.0f;   // 0..1 how much of the focus gain this slice gets
            if (c.shape == 0) f = std::exp(-0.5f * (oct / 0.5f) * (oct / 0.5f));   // bell, about an octave wide
            else if (c.shape == 1) f = 1.0f / (1.0f + std::exp(oct * 4.0f));          // low shelf: below the frequency
            else f = 1.0f / (1.0f + std::exp(-oct * 4.0f));                           // high shelf: above it
            // TILT: + (BRIGHT) makes the highs read quieter (cut less) and the lows hotter; +-tilt at 20 Hz / 20 kHz
            const float t = std::clamp(std::log2(hz / 632.0f) / 4.98f, -1.0f, 1.0f);
            wDb[static_cast<std::size_t>(b)] = c.focusDb * f - c.tilt * t;
        }
    }
    void frame(const Ctx& c) noexcept {
        weights(c);
        for (int ch = 0; ch < 2; ++ch) {
            auto& s = sp[static_cast<std::size_t>(ch)];
            const auto& src = in[static_cast<std::size_t>(ch)];
            for (int k = 0; k < N; ++k) s[static_cast<std::size_t>(k)] = {src[static_cast<std::size_t>((pos + k) % N)] * win[static_cast<std::size_t>(k)], 0.0f};
            fft.forward(s.data());
        }
        // a sine of amplitude A shows |X| = A * N / pi through the sqrt-Hann window
        constexpr float kScale = (kit::kPi / N) * (kit::kPi / N);
        float ip = 0, op = 0, gsum = 0;
        std::array<float, 4> zs{}, zn{};
        for (int b = 0; b < kBins; ++b) {
            const auto bi = static_cast<std::size_t>(b);
            const float pw = std::max(std::norm(sp[0][bi]), std::norm(sp[1][bi])) * kScale;
            const float det = 10.0f * std::log10(pw + 1e-20f) + wDb[bi];
            float& e = env[bi];
            e = det + (det > e ? c.att : c.relc) * (e - det);
            float g = 0.0f;
            if (c.active) {
                const float over = e - c.thr;
                if (c.knee <= 0.0f) g = over > 0.0f ? over * c.slope : 0.0f;
                else if (over > c.knee * 0.5f) g = over * c.slope;
                else if (over > -c.knee * 0.5f) { const float x = over + c.knee * 0.5f; g = c.slope * x * x / (2.0f * c.knee); }
                g = std::min(g, 24.0f);
            }
            gr[bi] = g;
        }
        // spread each cut over its neighbours so it does not ring: widen (max of three), then soften (3-tap average),
        // so a peak keeps its full cut and the slices around it follow
        tmp = gr;
        for (int b = 1; b < kBins - 1; ++b) gr[static_cast<std::size_t>(b)] = std::max({tmp[static_cast<std::size_t>(b - 1)], tmp[static_cast<std::size_t>(b)], tmp[static_cast<std::size_t>(b + 1)]});
        tmp = gr;
        for (int b = 1; b < kBins - 1; ++b) gr[static_cast<std::size_t>(b)] = 0.25f * tmp[static_cast<std::size_t>(b - 1)] + 0.5f * tmp[static_cast<std::size_t>(b)] + 0.25f * tmp[static_cast<std::size_t>(b + 1)];
        for (int b = 0; b < kBins; ++b) {
            const auto bi = static_cast<std::size_t>(b);
            const float g = kit::dbToGain(-gr[bi]);
            const float pw = std::norm(sp[0][bi]) + std::norm(sp[1][bi]);
            ip += pw; op += pw * g * g; gsum += gr[bi];
            const float hz = static_cast<float>(b) * static_cast<float>(c.fs) / N;
            const int z = hz < 200.0f ? 0 : hz < 1000.0f ? 1 : hz < 5000.0f ? 2 : 3;
            zs[static_cast<std::size_t>(z)] += gr[bi]; zn[static_cast<std::size_t>(z)] += 1.0f;
            for (auto& s : sp) {
                s[bi] *= g;
                if (b > 0 && b < N / 2) s[static_cast<std::size_t>(N - b)] *= g;
            }
        }
        for (int ch = 0; ch < 2; ++ch) {
            auto& s = sp[static_cast<std::size_t>(ch)];
            fft.inverse(s.data());
            auto& dst = out[static_cast<std::size_t>(ch)];
            for (int k = 0; k < N; ++k) dst[static_cast<std::size_t>((pos + k) % N)] += 0.5f * s[static_cast<std::size_t>(k)].real() * win[static_cast<std::size_t>(k)];
        }
        inPow = ip; outPow = op; grAvg = gsum / kBins;
        for (std::size_t z = 0; z < 4; ++z) zone[z] = zn[z] > 0 ? zs[z] / zn[z] : 0.0f;
    }
};

class ImpactGuide final : public hc::fx::Effect {
public:
    ImpactGuide() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i == kFreq || i == kFGain || i == kShape || i == kTilt || i == kSpeed || i == kKnee) return p(kThr) < -0.05f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        q_.prepare(); l_.prepare();
        for (auto& d : dry_) d.prepare(kLatency);
        for (auto& d : pad_) d.prepare(kLatency - 512);
        mix_.prepare(fs, 20.0, 0.0f); out_.prepare(fs, 20.0, 1.0f); mode_.prepare(fs, 20.0, p(kMode) > 0.5f ? 1.0f : 0.0f); make_.prepare(fs, 200.0, 1.0f);
        reset();
    }
    void reset() noexcept override {
        q_.clear(); l_.clear();
        for (auto& d : dry_) d.clear();
        for (auto& d : pad_) d.clear();
        running_ = false; primed_ = 0; makeDb_ = 0; matchDb_ = 0; matchPrev_ = false;
        rmsIn_ = rmsOut_ = 0; vuIn_ = vuOut_ = 0; clipIn_ = clipOut_ = 0;
        mix_.cur = 0.0f;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float mixT = p(kMix) * 0.01f;
        const bool want = mixT > 0.0f || mix_.cur > 1e-6f;
        if (want && !running_) { q_.clear(); l_.clear(); for (auto& d : pad_) d.clear(); primed_ = 0; }
        running_ = want;
        Ctx c;
        c.fs = fs_;
        c.thr = p(kThr);
        c.active = c.thr < -0.05f;
        c.slope = 1.0f - 1.0f / 4.0f;
        c.knee = p(kKnee) > 0.5f ? 12.0f : 0.0f;
        const bool fast = p(kSpeed) > 0.5f;
        c.focusHz = p(kFreq); c.focusDb = p(kFGain); c.shape = static_cast<int>(p(kShape)); c.tilt = p(kTilt);
        const float outT = kit::dbToGain(p(kOut)), modeT = p(kMode) > 0.5f ? 1.0f : 0.0f;
        const bool autoG = p(kAuto) > 0.5f;
        const float rmsC = hc::fx::onePoleCoef(fs_, 300.0), vuC = hc::fx::onePoleCoef(fs_, 300.0), clipHold = static_cast<float>(fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            const float dl = dry_[0].process(xl, kLatency), dr = dry_[1].process(xr, kLatency);
            float yl = dl, yr = dr;
            if (running_) {
                // frame-rate envelope coefficients (one step per hop), per engine size
                Ctx cq = c, cl = c;
                cq.att = std::exp(-static_cast<float>(Stft<2048>::kHop) / (static_cast<float>(fs_) * (fast ? 0.005f : 0.030f)));
                cq.relc = std::exp(-static_cast<float>(Stft<2048>::kHop) / (static_cast<float>(fs_) * (fast ? 0.060f : 0.300f)));
                cl.att = std::exp(-static_cast<float>(Stft<512>::kHop) / (static_cast<float>(fs_) * (fast ? 0.005f : 0.030f)));
                cl.relc = std::exp(-static_cast<float>(Stft<512>::kHop) / (static_cast<float>(fs_) * (fast ? 0.060f : 0.300f)));
                float ql, qr, ll, lr;
                q_.tick(xl, xr, ql, qr, cq);
                l_.tick(xl, xr, ll, lr, cl);
                ll = pad_[0].process(ll, kLatency - 512); lr = pad_[1].process(lr, kLatency - 512);
                if (primed_ < kLatency) ++primed_;   // the analysis has to fill before the wet path can open
                const float m = mode_.next(modeT);
                const float wl = ql + m * (ll - ql), wr = qr + m * (lr - qr);
                const float mk = make_.next(autoG ? kit::dbToGain(makeDb_) : 1.0f);
                const float mx = mix_.next(primed_ >= kLatency ? mixT : 0.0f);
                yl = dl + mx * (wl * mk - dl); yr = dr + mx * (wr * mk - dr);
            }
            const float o = out_.next(outT);
            yl *= o; yr *= o;
            // LEVEL MATCH: long-term loudness in vs out (before the match trim)
            const float pin = 0.5f * (xl * xl + xr * xr), pout = 0.5f * (yl * yl + yr * yr);
            rmsIn_ = pin + rmsC * (rmsIn_ - pin); rmsOut_ = pout + rmsC * (rmsOut_ - pout);
            const float mg = kit::dbToGain(matchDb_);
            yl *= mg; yr *= mg;
            const float ain = 0.5f * (std::fabs(xl) + std::fabs(xr)), aout = 0.5f * (std::fabs(yl) + std::fabs(yr));
            vuIn_ = ain + vuC * (vuIn_ - ain); vuOut_ = aout + vuC * (vuOut_ - aout);
            clipIn_ = std::max(clipIn_ - 1.0f, std::fabs(xl) >= 1.0f || std::fabs(xr) >= 1.0f ? clipHold : 0.0f);
            clipOut_ = std::max(clipOut_ - 1.0f, std::fabs(yl) >= 1.0f || std::fabs(yr) >= 1.0f ? clipHold : 0.0f);
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        // AUTO GAIN: put back the energy the cuts removed (0..24 dB), followed slowly
        if (running_) {
            const float ip = modeT > 0.5f ? l_.inPow : q_.inPow, op = modeT > 0.5f ? l_.outPow : q_.outPow;
            if (ip > 1e-12f) makeDb_ = std::clamp(10.0f * std::log10(ip / std::max(op, 1e-20f)), 0.0f, 24.0f);
        } else makeDb_ = 0;
        const bool match = p(kMatch) > 0.5f;
        if (match && !matchPrev_ && rmsIn_ > 1e-8f && rmsOut_ > 1e-10f)
            matchDb_ = std::clamp(10.0f * std::log10(rmsIn_ / rmsOut_), -24.0f, 24.0f);
        matchPrev_ = match;
        const auto& eng = p(kMode) > 0.5f ? l_.grAvg : q_.grAvg;
        const float gr = running_ ? -eng : 0.0f;
        publishMeter(0, std::clamp(kit::gainToDb(vuIn_ * 1.11f) + 18.0f, -30.0f, 10.0f));   // 0 VU = -18 dBFS
        publishMeter(1, std::clamp(kit::gainToDb(vuOut_ * 1.11f) + 18.0f, -30.0f, 10.0f));
        publishMeter(2, clipIn_ > 0 ? 1.0f : 0.0f); publishMeter(3, clipOut_ > 0 ? 1.0f : 0.0f);
        publishMeter(4, gr); publishMeter(5, matchDb_);
        for (int z = 0; z < 4; ++z) publishMeter(6 + z, running_ ? -(p(kMode) > 0.5f ? l_.zone : q_.zone)[static_cast<std::size_t>(z)] : 0.0f);
        publishReduction(gr);
    }

private:
    double fs_ = 48000;
    Stft<2048> q_;
    Stft<512> l_;
    std::array<hc::fx::DelayLine, 2> dry_{}, pad_{};
    hc::fx::Smoothed mix_, out_, mode_, make_;
    bool running_ = false, matchPrev_ = false;
    std::uint32_t primed_ = 0;
    float makeDb_ = 0, matchDb_ = 0, rmsIn_ = 0, rmsOut_ = 0, vuIn_ = 0, vuOut_ = 0, clipIn_ = 0, clipOut_ = 0;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeImpactGuide() { return std::make_unique<ImpactGuide>(); }

} // namespace hem::staging
