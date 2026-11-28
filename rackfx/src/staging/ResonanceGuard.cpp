// =============================================================================
//  14 RESONANCEGUARD — finds and pushes down harsh ringing peaks only while they ring.
//  A short-time spectrum (sqrt-Hann, 75 % overlap) is compared, bin by bin, with a threshold
//  curve: the spectrum's own smoothed shape (DETAIL sets how smooth), or a LEARNed curve, or the
//  sidechain's (SC FOLLOW), moved by THRESHOLD, TILT and up to eight NODES. Anything poking over
//  the curve is pulled down by SENSITIVITY, never more than its zone's CUT LIMIT.
//  Different from V.P.R.A. (02, fixed vocal tone knobs + one band) and from a plain EQ:
//  it does nothing until something actually rings.
//  Dead flat on load: THRESHOLD +40 dB (nothing crosses it), no learning, no listen, mix 100 %,
//  trim 0 dB — the signal comes out exactly, delayed by the reported latency.
//  Latency: 2048 samples in both QUALITY and LOW LATENCY (LOW LATENCY's shorter frames are padded,
//  so the DAW's compensation never changes). Mix LOCK is a preset-browser setting, not part of the sound.
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>
#include <complex>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

constexpr int kNodes = 8, kNodePer = 5, kZones = 4;
enum Top {
    kThreshold, kShape, kTilt, kHearRemoved, kLearn, kUseCurve, kAutoUpdate, kClear,
    kScEnable, kScListen, kScFollow, kLowLatency, kStereo,
    kSensitivity, kAttack, kRelease, kDetail, kTiltFreq, kSmoothing, kMix, kAutoMakeup, kTrim, kLimiter, kBalance,
    kX1, kX2, kX3, kNode0
};
constexpr int kZone0 = kNode0 + kNodes * kNodePer;
enum Tail { kBypass = kZone0 + kZones * 2, kCount };
enum NodeP { nFreq, nGain, nQ, nType, nOn };
const char* const kStereoN[] = {"LINKED", "SPLIT L/R", "M/S"};
const char* const kNodeTypeN[] = {"BELL", "NOTCH", "LOW SHELF", "HIGH SHELF"};
constexpr std::array<float, kNodes> kNodeHz{100, 250, 500, 1000, 2000, 4000, 8000, 12000};

#define HEM_NODE(n) \
    {"node" #n "_freq", "NODE " #n " FREQUENCY", "Hz", 20.0f, 20000.0f, kNodeHz[n - 1], Taper::Log}, \
    {"node" #n "_gain", "NODE " #n " GAIN", "dB", -12.0f, 12.0f, 0.0f}, \
    {"node" #n "_q", "NODE " #n " Q", "", 0.3f, 10.0f, 1.0f, Taper::Log}, \
    {"node" #n "_type", "NODE " #n " TYPE", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kNodeTypeN}, \
    {"node" #n "_on", "NODE " #n " ENABLE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}
#define HEM_ZONE(n, nm) \
    {"zone" #n "_limit", nm " CUT LIMIT", "dB", -48.0f, 0.0f, -12.0f}, \
    {"zone" #n "_solo", nm " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}

const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -40.0f, 40.0f, 40.0f},
    {"shape", "SHAPE", "", 0.0f, 100.0f, 50.0f},
    {"tilt", "TILT", "dB", -12.0f, 12.0f, 0.0f},
    {"hear_removed", "HEAR REMOVED", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"learn", "LEARN INPUT", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"use_curve", "USE CURVE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"auto_update", "AUTO UPDATE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"clear", "CLEAR CURVE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"sc_enable", "SC ENABLE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"sc_listen", "SC LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"sc_follow", "SC FOLLOW", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"low_latency", "LOW LATENCY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"stereo", "STEREO MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kStereoN},
    {"sensitivity", "SENSITIVITY", "", 0.0f, 100.0f, 70.0f},
    {"attack", "ATTACK", "ms", 0.1f, 500.0f, 10.0f, Taper::Log},
    {"release", "RELEASE", "ms", 10.0f, 5000.0f, 250.0f, Taper::Log},
    {"detail", "DETAIL", "", 0.0f, 100.0f, 50.0f},
    {"tilt_freq", "TILT FREQUENCY", "Hz", 100.0f, 10000.0f, 2000.0f, Taper::Log},
    {"smoothing", "SMOOTHING", "", 0.0f, 100.0f, 60.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 100.0f},
    {"auto_makeup", "AUTO MAKEUP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"output_trim", "OUTPUT TRIM", "dB", -12.0f, 12.0f, 0.0f},
    {"limiter", "LIMITER", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"balance", "BALANCE", "", -100.0f, 100.0f, 0.0f},
    {"x1", "LOW / LOW-MID", "Hz", 50.0f, 1000.0f, 200.0f, Taper::Log},
    {"x2", "LOW-MID / HIGH-MID", "Hz", 500.0f, 6000.0f, 2000.0f, Taper::Log},
    {"x3", "HIGH-MID / HIGH", "Hz", 2000.0f, 16000.0f, 8000.0f, Taper::Log},
    HEM_NODE(1), HEM_NODE(2), HEM_NODE(3), HEM_NODE(4), HEM_NODE(5), HEM_NODE(6), HEM_NODE(7), HEM_NODE(8),
    HEM_ZONE(1, "LOW"), HEM_ZONE(2, "LOW-MID"), HEM_ZONE(3, "HIGH-MID"), HEM_ZONE(4, "HIGH"),
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_NODE
#undef HEM_ZONE

const hc::fx::MeterInfo kMeters[] = {
    {"REDUCTION", "dB", -24, 0}, {"LOW", "dB", -24, 0}, {"LOW-MID", "dB", -24, 0}, {"HIGH-MID", "dB", -24, 0}, {"HIGH", "dB", -24, 0},
    {"INPUT", "dBFS", -60, 0}, {"OUTPUT", "dBFS", -60, 0},
};

constexpr int kMaxN = 2048, kMaxBins = kMaxN / 2 + 1, kLat = kMaxN;

class ResonanceGuard final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    ResonanceGuard() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLat; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fftBig_.prepare(kMaxN); fftSmall_.prepare(512);
        for (int i = 0; i < kMaxN; ++i) winBig_[static_cast<std::size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(kit::kTau * static_cast<float>(i) / kMaxN));
        for (int i = 0; i < 512; ++i) winSmall_[static_cast<std::size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(kit::kTau * static_cast<float>(i) / 512));
        mixG_.prepare(fs, 20.0, p(kMix) * 0.01f); trimG_.prepare(fs, 20.0, kit::dbToGain(p(kTrim)));
        inM_.prepare(fs); outM_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : ch_) { c.in.fill(0); c.out.fill(0); c.dly.fill(0); c.psm.fill(0); c.gain.fill(1); }
        sc_.fill(0); scPow_.fill(0); learned_.fill(0); learnFrames_ = 0;
        pos_ = 0; hop_ = 0; wet_ = 0; lastClear_ = false; lastN_ = 0; zoneRed_.fill(0); redDb_ = 0; limG_ = 1; mkDb_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inM_.feed(L, n);
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; outM_.feed(L, n); return; }
        const int N = p(kLowLatency) > 0.5f ? 512 : kMaxN, H = N / 4;
        if (N != lastN_) { for (auto& c : ch_) { c.out.fill(0); c.psm.fill(0); c.gain.fill(1); } lastN_ = N; hop_ = 0; }
        if (p(kClear) > 0.5f && !lastClear_) { learned_.fill(0); learnFrames_ = 0; }
        lastClear_ = p(kClear) > 0.5f;
        const bool listen = p(kScEnable) > 0.5f && p(kScListen) > 0.5f && scL_;
        bool solo = false;
        for (int z = 0; z < kZones; ++z) solo |= p(kZone0 + 2 * z + 1) > 0.5f;
        const bool active = p(kThreshold) < 39.9f || p(kHearRemoved) > 0.5f || solo;
        const float fadeC = hc::fx::onePoleCoef(fs_, 20.0);
        const float mixT = p(kMix) * 0.01f, trimT = kit::dbToGain(p(kTrim) + (p(kAutoMakeup) > 0.5f ? mkDb_ : 0.0f));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float wT = active ? 1.0f : 0.0f;
            wet_ = wT + fadeC * (wet_ - wT);
            const float mix = mixG_.next(mixT), trim = trimG_.next(trimT);
            const auto at = static_cast<std::size_t>(pos_);
            // the analysis input (stereo mode decides what the two lanes carry)
            const float l = L[i], r = R[i];
            const int mode = static_cast<int>(p(kStereo));
            const float a0 = mode == 2 ? 0.5f * (l + r) : l, a1 = mode == 2 ? 0.5f * (l - r) : r;
            float y[2] = {0, 0}, dry[2] = {0, 0};
            for (int c = 0; c < 2; ++c) {
                Chan& ch = ch_[static_cast<std::size_t>(c)];
                dry[c] = ch.dly[at];                                    // kLat samples ago (plain, for the dry path)
                const int pad = kLat - N;                               // LOW LATENCY output waits in the same line
                y[c] = ch.out[static_cast<std::size_t>((pos_ + kLat - pad) % kLat)];
                ch.out[static_cast<std::size_t>((pos_ + kLat - pad) % kLat)] = 0.0f;
                ch.in[at] = c == 0 ? a0 : a1;
                ch.dly[at] = io[c][i];
            }
            const float scD = sc_[at];                                  // the sidechain, kLat samples late (for LISTEN)
            sc_[at] = scL_ ? 0.5f * (scL_[i] + scR_[i]) : 0.0f;
            // back from M/S if needed
            float wl = y[0], wr = y[1];
            if (mode == 2) { wl = y[0] + y[1]; wr = y[0] - y[1]; }
            float ol = dry[0], orr = dry[1];
            if (wet_ > 1e-6f) {
                if (p(kHearRemoved) > 0.5f) { wl = dry[0] - wl; wr = dry[1] - wr; }   // HEAR REMOVED: only what it takes out
                ol = dry[0] + wet_ * mix * (wl - dry[0]); orr = dry[1] + wet_ * mix * (wr - dry[1]);
            }
            if (listen) { ol = orr = scD; }
            ol *= trim; orr *= trim;
            if (p(kLimiter) > 0.5f) {   // a safety limiter at -0.3 dBFS, instant down, 50 ms back
                const float pk = std::max(std::fabs(ol), std::fabs(orr)), ceil = 0.966f;
                const float need = pk > ceil ? ceil / pk : 1.0f;
                limG_ = std::min(need, 1.0f + hc::fx::onePoleCoef(fs_, 50.0) * (limG_ - 1.0f));
                ol *= limG_; orr *= limG_;
            }
            L[i] = kit::sane(ol); R[i] = kit::sane(orr);
            pos_ = (pos_ + 1) % kLat;
            if (++hop_ >= H) { hop_ = 0; frame(N); }
        }
        outM_.feed(L, n);
        publishMeter(0, redDb_);
        for (int z = 0; z < kZones; ++z) publishMeter(1 + z, zoneRed_[static_cast<std::size_t>(z)]);
        publishMeter(5, inM_.db()); publishMeter(6, outM_.db());
        publishReduction(redDb_);
        scL_ = scR_ = nullptr;
    }

private:
    struct Chan { std::array<float, kLat> in{}, out{}, dly{}; std::array<float, kMaxBins> psm{}, gain{}; };

    [[nodiscard]] int zoneOf(float hz) const noexcept { return hz < p(kX1) ? 0 : hz < p(kX2) ? 1 : hz < p(kX3) ? 2 : 3; }

    // threshold offset of the nodes at a frequency (dB)
    [[nodiscard]] float nodes(float hz) const noexcept {
        float off = 0.0f;
        for (int k = 0; k < kNodes; ++k) {
            const int o = kNode0 + k * kNodePer;
            if (p(o + nOn) < 0.5f) continue;
            const float oct = std::log2(hz / p(o + nFreq)), q = p(o + nQ);
            switch (static_cast<int>(p(o + nType))) {
            case 1: off += p(o + nGain) * std::exp(-0.5f * std::pow(oct * q * 4.0f, 2.0f)); break;          // NOTCH: very narrow
            case 2: off += p(o + nGain) / (1.0f + std::exp(oct * q * 4.0f)); break;                          // LOW SHELF
            case 3: off += p(o + nGain) / (1.0f + std::exp(-oct * q * 4.0f)); break;                         // HIGH SHELF
            default: off += p(o + nGain) * std::exp(-0.5f * std::pow(oct * q, 2.0f)); break;                // BELL
            }
        }
        return off;
    }

    void frame(int N) noexcept {
        hc::fx::Fft& fft = N == kMaxN ? fftBig_ : fftSmall_;
        const float* win = N == kMaxN ? winBig_.data() : winSmall_.data();
        const int bins = N / 2 + 1;
        const float hopMs = 1000.0f * static_cast<float>(N / 4) / static_cast<float>(fs_);
        const float att = std::exp(-hopMs / std::max(p(kAttack), 0.1f)), rel = std::exp(-hopMs / p(kRelease));
        // spectra of both lanes, and the sidechain's
        for (int c = 0; c < 2; ++c) {
            auto& s = spec_[static_cast<std::size_t>(c)];
            for (int k = 0; k < N; ++k) s[static_cast<std::size_t>(k)] = {ch_[static_cast<std::size_t>(c)].in[static_cast<std::size_t>((pos_ - N + k + 2 * kLat) % kLat)] * win[k], 0.0f};
            fft.forward(s.data());
        }
        const bool sc = p(kScEnable) > 0.5f && scL_;
        if (sc) {
            for (int k = 0; k < N; ++k) scSpec_[static_cast<std::size_t>(k)] = {sc_[static_cast<std::size_t>((pos_ - N + k + 2 * kLat) % kLat)] * win[k], 0.0f};
            fft.forward(scSpec_.data());
            for (int b = 0; b < bins; ++b) scPow_[static_cast<std::size_t>(b)] = 0.8f * scPow_[static_cast<std::size_t>(b)] + 0.2f * std::norm(scSpec_[static_cast<std::size_t>(b)]);
        }
        const bool linked = static_cast<int>(p(kStereo)) == 0;
        const float binHz = static_cast<float>(fs_) / static_cast<float>(N);
        const float detailOct = 1.0f / 24.0f + (1.0f - 1.0f / 24.0f) * (1.0f - p(kDetail) * 0.01f);   // DETAIL 100 = 1/24 oct, 0 = 1 oct
        const float sens = p(kSensitivity) * 0.01f;
        const float gamma = 0.5f + p(kShape) * 0.015f;   // SHAPE: 0.5 soft .. 2 hard reaction to the excess
        std::array<float, kZones> zr{};
        float sumRed = 0;
        for (int c = 0; c < 2; ++c) {
            Chan& ch = ch_[static_cast<std::size_t>(c)];
            if (linked && c == 1) { ch.gain = ch_[0].gain; break; }
            for (int b = 0; b < bins; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                float pw = std::norm(spec_[static_cast<std::size_t>(c)][bi]);
                if (linked) pw = 0.5f * (std::norm(spec_[0][bi]) + std::norm(spec_[1][bi]));
                if (sc) pw = scPow_[bi];                                       // SC ENABLE: the sidechain triggers the cuts
                ch.psm[bi] = pw + (pw > ch.psm[bi] ? att : rel) * (ch.psm[bi] - pw);
            }
            // learning (from lane 0 / linked)
            if (c == 0 && (p(kLearn) > 0.5f || p(kAutoUpdate) > 0.5f)) {
                const float k = p(kLearn) > 0.5f ? 1.0f / static_cast<float>(++learnFrames_ + 1) : 0.002f;
                for (int b = 0; b < bins; ++b) learned_[static_cast<std::size_t>(b)] += k * (10.0f * std::log10(ch.psm[static_cast<std::size_t>(b)] + 1e-20f) - learned_[static_cast<std::size_t>(b)]);
            }
            // reference curve in dB: smoothed spectrum (+-detail octaves), the learned curve, or the sidechain's
            for (int b = 0; b < bins; ++b) ldb_[static_cast<std::size_t>(b)] = 10.0f * std::log10(ch.psm[static_cast<std::size_t>(b)] + 1e-20f);
            for (int b = 1; b < bins; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                const float hz = static_cast<float>(b) * binHz;
                float ref;
                if (p(kUseCurve) > 0.5f && learnFrames_ > 0) ref = learned_[bi];
                else if (p(kScFollow) > 0.5f && sc) ref = 10.0f * std::log10(scPow_[bi] + 1e-20f);
                else {
                    const int lo = std::max(1, static_cast<int>(static_cast<float>(b) * std::exp2(-detailOct))), hi = std::min(bins - 1, static_cast<int>(static_cast<float>(b) * std::exp2(detailOct)) + 1);
                    double acc = 0; int cnt = 0;
                    for (int k = lo; k <= hi; k += std::max(1, (hi - lo) / 16)) { acc += ldb_[static_cast<std::size_t>(k)]; ++cnt; }
                    ref = static_cast<float>(acc / std::max(cnt, 1));
                }
                // +6 dB built in (ASSUMED): plain noise wanders a few dB over its own average; a resonance stands far above it
                const float thr = ref + 6.0f + p(kThreshold) + p(kTilt) * std::log2(hz / p(kTiltFreq)) / 5.0f + nodes(hz);
                const float excess = ldb_[bi] - thr;
                const int z = zoneOf(hz);
                const float limit = -p(kZone0 + 2 * z);
                float red = excess > 0.0f ? std::min(limit, std::pow(excess, gamma) * sens) : 0.0f;
                if (!linked) red *= c == 0 ? (1.0f - std::max(0.0f, p(kBalance) * 0.01f)) : (1.0f + std::min(0.0f, p(kBalance) * 0.01f));
                gtmp_[bi] = kit::dbToGain(-red);
                zr[static_cast<std::size_t>(z)] = std::min(zr[static_cast<std::size_t>(z)], -red);
                sumRed += red;
            }
            gtmp_[0] = 1.0f;
            // SMOOTHING: across neighbouring bins
            const float sm = p(kSmoothing) * 0.005f;   // 0 .. 0.5
            for (int b = 1; b < bins - 1; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                ch.gain[bi] = (1.0f - 2.0f * sm) * gtmp_[bi] + sm * (gtmp_[bi - 1] + gtmp_[bi + 1]);
            }
            ch.gain[0] = gtmp_[0]; ch.gain[static_cast<std::size_t>(bins - 1)] = gtmp_[static_cast<std::size_t>(bins - 1)];
        }
        // zone SOLO: only the soloed zones' bins pass
        bool solo = false;
        for (int z = 0; z < kZones; ++z) solo |= p(kZone0 + 2 * z + 1) > 0.5f;
        for (int c = 0; c < 2; ++c) {
            Chan& ch = ch_[static_cast<std::size_t>(c)];
            auto& s = spec_[static_cast<std::size_t>(c)];
            for (int b = 0; b < bins; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                float g = ch.gain[bi];
                if (solo && p(kZone0 + 2 * zoneOf(static_cast<float>(b) * binHz) + 1) < 0.5f) g = 0.0f;
                s[bi] *= g;
                if (b > 0 && b < N / 2) s[static_cast<std::size_t>(N - b)] *= g;
            }
            fft.inverse(s.data());
            for (int k = 0; k < N; ++k) {
                const auto dst = static_cast<std::size_t>((pos_ + k) % kLat);
                ch.out[dst] += 0.5f * s[static_cast<std::size_t>(k)].real() * win[k];
            }
        }
        zoneRed_ = zr;
        redDb_ = -sumRed / static_cast<float>(linked ? bins : 2 * bins);
        mkDb_ = std::min(6.0f, -redDb_ * 0.5f);   // AUTO MAKEUP: half the average cut back
    }

    double fs_ = 48000;
    hc::fx::Fft fftBig_, fftSmall_;
    std::array<float, kMaxN> winBig_{};
    std::array<float, 512> winSmall_{};
    std::array<Chan, 2> ch_{};
    std::array<std::array<std::complex<float>, kMaxN>, 2> spec_{};
    std::array<std::complex<float>, kMaxN> scSpec_{};
    std::array<float, kLat> sc_{};
    std::array<float, kMaxBins> scPow_{}, learned_{}, ldb_{}, gtmp_{};
    std::array<float, kZones> zoneRed_{};
    int pos_ = 0, hop_ = 0, lastN_ = 0, learnFrames_ = 0;
    float wet_ = 0, redDb_ = 0, limG_ = 1, mkDb_ = 0;
    bool lastClear_ = false;
    hc::fx::Smoothed mixG_, trimG_;
    kit::PeakMeter inM_, outM_;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeResonanceGuard() { return std::make_unique<ResonanceGuard>(); }

} // namespace hem::staging
