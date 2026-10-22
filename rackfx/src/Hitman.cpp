// HEM HITMAN — dual FET peak compressor (see Hitman.hpp).
#include "hem/rack/Hitman.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {
namespace hitman {

float attackMs(float k) noexcept { return 0.8f * std::pow(0.02f / 0.8f, (std::clamp(k, 1.0f, 7.0f) - 1.0f) / 6.0f); }
float releaseMs(float k) noexcept { return 1100.0f * std::pow(50.0f / 1100.0f, (std::clamp(k, 1.0f, 7.0f) - 1.0f) / 6.0f); }

namespace {
// A FET compressor's threshold is fixed: INPUT drives the signal into it.
struct RatioSpec { float ratio, threshold, knee; };
constexpr RatioSpec kSpecs[5] = {{4, -14, 6}, {8, -12, 4}, {12, -10, 3}, {20, -8, 2}, {20, -12, 2}};
} // namespace

float gainComputerDb(float x, RatioSel r) noexcept {
    const RatioSpec& s = kSpecs[static_cast<int>(r)];
    const float over = x - s.threshold, slope = 1.0f / s.ratio - 1.0f;
    float g;
    if (over <= -s.knee * 0.5f) g = 0.0f;
    else if (over >= s.knee * 0.5f) g = slope * over;
    else { const float k = over + s.knee * 0.5f; g = slope * k * k / (2.0f * s.knee); }
    return r == RatioSel::All ? 1.3f * g : g;   // ALL: it overshoots, pulling harder than 20:1
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;

const char* const kModeN[] = {"L/R", "M/S"};
const char* const kOnOff[] = {"OFF", "ON"};
const char* const kRatioN[] = {"4", "8", "12", "20", "STACKED"};
const char* const kSatN[] = {"DRY", "CLIP", "WARM", "TRIODE", "TAPE"};
const char* const kSrcN[] = {"INT", "EXT"};
const ParamInfo kParams[kCount] = {
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"link", "Link Controls", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"input1", "Input 1", "dB", -24.0f, 24.0f, 0.0f},
    {"output1", "Output 1", "dB", -24.0f, 24.0f, 0.0f},
    {"attack1", "Attack 1", "", 1.0f, 7.0f, 4.0f},
    {"release1", "Release 1", "", 1.0f, 7.0f, 4.0f},
    {"input2", "Input 2", "dB", -24.0f, 24.0f, 0.0f},
    {"output2", "Output 2", "dB", -24.0f, 24.0f, 0.0f},
    {"attack2", "Attack 2", "", 1.0f, 7.0f, 4.0f},
    {"release2", "Release 2", "", 1.0f, 7.0f, 4.0f},
    {"ratio", "Ratio", "", 0, 4, 0, Taper::Choice, kRatioN},
    {"lookahead", "Lookahead", "ms", 0.0f, 2.0f, 0.0f},
    {"sclink", "SC Link", "%", 0.0f, 100.0f, 100.0f},
    {"saturation", "Saturation", "", 0, 4, 0, Taper::Choice, kSatN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"scsource", "SC Source", "", 0, 1, 0, Taper::Choice, kSrcN},
    {"sceq", "SC EQ", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"schpf", "SC High-Pass", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"scbellfreq", "SC Bell Freq", "Hz", 100.0f, 10000.0f, 1000.0f, Taper::Log},
    {"scbellgain", "SC Bell Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"scbellq", "SC Bell Q", "", 0.3f, 6.0f, 1.0f, Taper::Log},
    {"sclpf", "SC Low-Pass", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"sclisten", "SC Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
const hc::fx::MeterInfo kMeters[] = {{"GR 1", "dB", -30.0f, 0.0f}, {"GR 2", "dB", -30.0f, 0.0f}, {"INPUT", "dBFS", -60.0f, 0.0f}, {"OUTPUT", "dBFS", -60.0f, 0.0f}};

// The sidechain EQ: 12 dB/oct high-pass (off at its lowest), a bell, 12 dB/oct low-pass (off at its highest).
struct EqDesign { C hp, bell, lp; bool hpOn, lpOn; };
EqDesign scEqDesign(const float* v, double fs) noexcept {
    EqDesign e{};
    e.hpOn = v[ScHpf] > 20.5f;
    e.lpOn = v[ScLpf] < 19999.0f;
    const C id{1, 0, 0, 0, 0};
    e.hp = e.hpOn ? C::make(T::HighPass, fs, v[ScHpf], 0.7071) : id;
    e.bell = C::make(T::Bell, fs, std::min<double>(v[ScBellFreq], fs * 0.45), v[ScBellQ], v[ScBellGain]);
    e.lp = e.lpOn ? C::make(T::LowPass, fs, std::min<double>(v[ScLpf], fs * 0.45), 0.7071) : id;
    return e;
}
} // namespace

double scEqDb(const float* v, double fs, double hz) noexcept {
    if (v[ScEq] < 0.5f) return 0.0;
    const EqDesign e = scEqDesign(v, fs);
    return e.hp.magnitudeDb(fs, hz) + e.bell.magnitudeDb(fs, hz) + e.lp.magnitudeDb(fs, hz);
}

} // namespace hitman

namespace {
using namespace hitman;
using OS = hc::fx::Oversampler4;
inline float soft(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }
inline float dbOf(float x) noexcept { return 20.0f * std::log10(std::max(x, 1e-9f)); }

class Impl final : public hc::fx::Effect, public SpectrumSource, public SidechainSink {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return la_ + OS::latency(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }   // what the detector hears
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }     // what comes out
    void setSidechain(const float* l, const float* r) noexcept override { extL_ = l; extR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i >= Input2 && i <= Release2) return p(Link) < 0.5f;
        if (i >= ScHpf && i <= ScLpf) return p(ScEq) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        la_ = static_cast<std::uint32_t>(std::lround(0.002 * fs));   // the most LOOKAHEAD can be
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : gain_) v.assign(mb, 0.0f);
        for (auto& v : drive_) v.assign(mb, 0.0f);
        for (auto& v : outG_) v.assign(mb, 0.0f);
        scBuf_.assign(mb, 0.0f);
        for (auto& d : audio_) d.prepare(la_ + 1);
        for (auto& d : det_) d.prepare(la_ + 1);
        for (auto& d : dry_) d.prepare(la_ + OS::latency() + 1);
        for (auto& d : listen_) d.prepare(la_ + OS::latency() + 1);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }

    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto* set : {&audio_, &det_, &dry_, &listen_}) for (auto& d : *set) d.clear();
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        gr_[0] = gr_[1] = 0.0f;
        dc_[0] = dc_[1] = 0.0;
        tapeLp_[0] = tapeLp_[1] = 0.0f;
        eqKey_ = -1.0f;
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        for (int c = 0; c < 2; ++c) {
            in_[c].prepare(fs_, 30, hc::fx::dbToGain(v[c == 0 || v[Link] >= 0.5f ? Input1 : Input2]));
            out_[c].prepare(fs_, 30, hc::fx::dbToGain(v[c == 0 || v[Link] >= 0.5f ? Output1 : Output2]));
        }
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        before_.reset();
        after_.reset();
        for (int m = 0; m < 4; ++m) publishMeter(m, m < 2 ? 0.0f : -100.0f);
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        designEq();
        const bool ms = p(Mode) >= 0.5f, link = p(Link) >= 0.5f, listen = p(ScListen) >= 0.5f, eqOn = p(ScEq) >= 0.5f;
        const bool ext = p(ScSource) >= 0.5f && extL_ && extR_;   // EXT with nothing connected = internal
        const auto ratio = static_cast<RatioSel>(std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 4));
        const bool all = ratio == RatioSel::All;
        const auto sat = static_cast<Sat>(std::clamp(static_cast<int>(std::lround(p(Saturation))), 0, 4));
        const std::uint32_t laNow = std::min(la_, static_cast<std::uint32_t>(std::lround(p(Lookahead) * 0.001 * fs_)));
        const float scLink = p(ScLink) * 0.01f;
        float attC[2], relC[2];
        for (int c = 0; c < 2; ++c) {
            const bool own = c == 1 && !link;
            attC[c] = static_cast<float>(std::exp(-1.0 / (fs_ * attackMs(p(own ? Attack2 : Attack1)) * 0.001)));
            relC[c] = static_cast<float>(std::exp(-1.0 / (fs_ * releaseMs(p(own ? Release2 : Release1)) * 0.001)));
        }
        float most[2] = {0, 0}, inPeak = 0, outPeak = 0;
        // 1. matrix, input drive, the detector; the audio waits LOOKAHEAD-max samples, the detector
        //    (max - LOOKAHEAD), so the detector is LOOKAHEAD early and the latency never changes
        for (std::uint32_t i = 0; i < n; ++i) {
            inPeak = std::max({inPeak, std::fabs(L[i]), std::fabs(R[i])});
            float x[2] = {L[i], R[i]}, s[2] = {ext ? extL_[i] : L[i], ext ? extR_[i] : R[i]};
            if (ms) {
                x[0] = 0.5f * (L[i] + R[i]); x[1] = 0.5f * (L[i] - R[i]);
                const float a = s[0], b = s[1];
                s[0] = 0.5f * (a + b); s[1] = 0.5f * (a - b);
            }
            float lev[2], d[2];
            for (int c = 0; c < 2; ++c) {
                const float g = in_[c].next(hc::fx::dbToGain(p(c == 1 && !link ? Input2 : Input1)));
                x[c] *= g;
                float sc = s[c] * g;
                if (eqOn) { auto& e = eq_[static_cast<std::size_t>(c)]; sc = static_cast<float>(e[2].tick(e[1].tick(e[0].tick(sc)))); }
                d[c] = sc;
                (void)det_[c].process(sc, 0);
                lev[c] = std::fabs(det_[c].read(static_cast<float>(la_ - laNow)));
                wet_[static_cast<std::size_t>(c)][i] = audio_[c].process(x[c], la_);
            }
            scBuf_[i] = ms ? d[0] + d[1] : 0.5f * (d[0] + d[1]);
            (void)listen_[0].process(ms ? d[0] + d[1] : d[0], 0);
            (void)listen_[1].process(ms ? d[0] - d[1] : d[1], 0);
            const float both = std::max(lev[0], lev[1]);
            for (int c = 0; c < 2; ++c) {
                const float level = scLink * both + (1.0f - scLink) * lev[c];
                const float target = gainComputerDb(dbOf(level), ratio);
                gr_[c] = target + (target < gr_[c] ? attC[c] : relC[c]) * (gr_[c] - target);
                most[c] = std::min(most[c], gr_[c]);
                gain_[static_cast<std::size_t>(c)][i] = hc::fx::dbToGain(gr_[c]);
                drive_[static_cast<std::size_t>(c)][i] = 1.0f + 0.015f * -gr_[c];   // the FET's colour grows with the reduction
                outG_[static_cast<std::size_t>(c)][i] = out_[c].next(hc::fx::dbToGain(p(c == 1 && !link ? Output2 : Output1)));
            }
        }
        for (float& g : gr_) if (g > -1e-6f) g = 0.0f;
        before_.push(scBuf_.data(), scBuf_.data(), n);
        // 2. the gain cell and the saturation, 4x oversampled
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            const auto& gn = gain_[static_cast<std::size_t>(c)];
            const auto& dr = drive_[static_cast<std::size_t>(c)];
            const auto& og = outG_[static_cast<std::size_t>(c)];
            os_[c].up(wet_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t i = 0; i < n; ++i) {
                for (int k = 0; k < OS::kFactor; ++k) {
                    float& v = x4[i * OS::kFactor + static_cast<std::uint32_t>(k)];
                    float y = v;
                    if (all) { const float a = 0.35f * y; y = soft(a) / 0.35f; }   // ALL: overdriven before it turns down
                    const float xg = dr[i] * (y * gn[i]);
                    y = soft(xg) / dr[i] * og[i];   // OUTPUT drives the saturation, which is the last stage
                    v = saturate(y, sat);
                }
            }
            os_[c].down(x4, wet_[static_cast<std::size_t>(c)].data(), n);
        }
        // 3. DC (lopsided modes), tape's softened top, output, back to L/R, MIX, meters
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        const float tapeK = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 14000.0 / fs_));
        const std::uint32_t total = la_ + OS::latency();
        for (std::uint32_t i = 0; i < n; ++i) {
            float y[2];
            for (int c = 0; c < 2; ++c) {
                float v = wet_[static_cast<std::size_t>(c)][i];
                if (sat == Sat::Warm || sat == Sat::Triode) { dc_[c] += (v - dc_[c]) * dcK; v -= static_cast<float>(dc_[c]); }
                if (sat == Sat::Tape) { tapeLp_[c] = v + tapeK * (tapeLp_[c] - v); v = tapeLp_[c]; }
                y[c] = v;
            }
            float wl = ms ? y[0] + y[1] : y[0], wr = ms ? y[0] - y[1] : y[1];
            if (sat == Sat::Clip) {   // the down-sampling filter can ring past the ceiling on hard clipping: catch it
                wl = std::clamp(wl, -1.0f, 1.0f);
                wr = std::clamp(wr, -1.0f, 1.0f);
            }
            if (listen) { wl = listen_[0].read(static_cast<float>(total)); wr = listen_[1].read(static_cast<float>(total)); }
            const float m = mix_.next(p(Mix) * 0.01f);
            const float dl = dry_[0].process(L[i], total), drr = dry_[1].process(R[i], total);
            L[i] = listen ? wl : dl + m * (wl - dl);
            R[i] = listen ? wr : drr + m * (wr - drr);
            outPeak = std::max({outPeak, std::fabs(L[i]), std::fabs(R[i])});
        }
        after_.push(L, R, n);
        extL_ = extR_ = nullptr;   // only valid for this block
        publishMeter(0, most[0]);
        publishMeter(1, most[1]);
        publishMeter(2, inPeak > 0.0f ? dbOf(inPeak) : -100.0f);
        publishMeter(3, outPeak > 0.0f ? dbOf(outPeak) : -100.0f);
        publishReduction(std::min(most[0], most[1]));
    }

private:
    static float saturate(float x, Sat s) noexcept {
        switch (s) {
        case Sat::Dry: return x;
        case Sat::Clip: {   // a soft knee from -3 dB into a hard ceiling at 0 dBFS
            const float a = std::fabs(x), k = 0.29f;
            if (a <= 1.0f - k) return x;
            const float t = std::min((a - (1.0f - k)) / (2.0f * k), 1.0f);
            return std::copysign(std::min((1.0f - k) + k * (2.0f * t - t * t), 1.0f), x);
        }
        case Sat::Warm: { const float v = 1.4f * x; return soft(v + 0.08f * v * v) / 1.4f; }
        case Sat::Triode: { const float v = 1.8f * x; return soft(v + 0.3f * v * v) / 1.8f; }
        case Sat::Tape: { const float v = 1.6f * x; return soft(v) / 1.6f; }
        }
        return x;
    }
    void designEq() noexcept {
        const float key = p(ScHpf) * 7.0f + p(ScBellFreq) * 3.0f + p(ScBellGain) * 11.0f + p(ScBellQ) * 13.0f + p(ScLpf);
        if (key == eqKey_) return;
        eqKey_ = key;
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        const EqDesign e = scEqDesign(v, fs_);
        for (auto& ch : eq_) { ch[0].c = e.hp; ch[1].c = e.bell; ch[2].c = e.lp; }
    }

    double fs_ = 48000;
    std::uint32_t la_ = 96;
    OS os_[2];
    std::array<std::vector<float>, 2> up_, wet_, gain_, drive_, outG_;
    std::vector<float> scBuf_;
    hc::fx::DelayLine audio_[2], det_[2], dry_[2], listen_[2];
    std::array<std::array<hc::fx::Biquad, 3>, 2> eq_{};
    float gr_[2] = {0, 0}, tapeLp_[2] = {0, 0}, eqKey_ = -1;
    double dc_[2] = {0, 0};
    hc::fx::Smoothed in_[2], out_[2], mix_;
    const float* extL_ = nullptr;
    const float* extR_ = nullptr;
    SpectrumMeter before_, after_;
};

double curveDb(const float* v, double fs, double hz) noexcept { return scEqDb(v, fs, hz); }
int curveDots(const float* v, double* hz, int max) noexcept {
    int n = 0;
    if (v[ScEq] < 0.5f) return 0;
    if (v[ScHpf] > 20.5f && n < max) hz[n++] = v[ScHpf];
    if (n < max) hz[n++] = v[ScBellFreq];
    if (v[ScLpf] < 19999.0f && n < max) hz[n++] = v[ScLpf];
    return n;
}

} // namespace

std::unique_ptr<hc::fx::Effect> makeHitman() { return std::make_unique<Impl>(); }
const CurveInfo kHitmanScCurve{&curveDb, &curveDots, 18.0f};

} // namespace hem::rack
