// =============================================================================
//  46 PARAMETRIC EQ — a clean, precise, colourless EQ: five bands (LOW 20..250 Hz, LOW MID 80..500 Hz,
//  MID 250 Hz..2.5 kHz, HIGH MID 1..8 kHz, HIGH 4..20 kHz), each with POWER, FILTER TYPE (BELL / LOW SHELF /
//  HIGH SHELF / NOTCH), FREQ, GAIN (+-18 dB) and Q (0.3..10); an HPF (20 Hz..1 kHz) and an LPF
//  (1..20 kHz), each with POWER and SLOPE 12 / 24 / 48 dB/oct (Butterworth); GLOBAL GAIN.
//  EQ MODE: STEREO (one knob set on both sides), L/R (set A on the left, set B on the right) or
//  MID/SIDE (set A on the mid, set B on the side). The ANALYZER switch (PRE / POST / BOTH) only picks
//  what the faceplate graph shows.
//  Different from CONSOLECORE (28: console curves) and GRAPHICFORGE (17: fixed bands): pure math curves,
//  no colour, no dynamics.
//  Dead flat on load: every gain 0 dB, HPF and LPF off, GLOBAL GAIN 0 dB.
//  ASSUMED: default frequencies 100 / 250 / 1000 / 3000 / 10000 Hz, types LOW SHELF / BELL / BELL /
//  BELL / HIGH SHELF, Q 0.7; NOTCH ignores GAIN (its width is Q); set B needs an A / B selector on the
//  faceplate (the panel shows one set).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

constexpr int kBands = 5, kBandP = 5, kFiltP = 6, kSetP = kBands * kBandP + kFiltP;   // 31 per set
enum BP { bPower, bType, bFreq, bGain, bQ };
enum FP { fHpOn, fHpF, fHpSlope, fLpOn, fLpF, fLpSlope };
enum P { kSetA = 0, kSetB = kSetP, kMode = 2 * kSetP, kGlobal, kAnalyzer, kBypass, kCount };
const char* const kTypeN[] = {"BELL", "LOW SHELF", "HIGH SHELF", "NOTCH"};
const char* const kSlopeN[] = {"12 dB", "24 dB", "48 dB"};
const char* const kModeN[] = {"STEREO", "L/R", "MID/SIDE"};
const char* const kAnaN[] = {"PRE", "POST", "BOTH"};
#define HEM_B(id, nm, lo, hi, f, t)                                                       \
    {id "_power", nm " POWER", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},                      \
    {id "_type", nm " FILTER TYPE", "", 0.0f, 3.0f, t, Taper::Choice, kTypeN},           \
    {id "_freq", nm " FREQ", "Hz", lo, hi, f, Taper::Log},                                \
    {id "_gain", nm " GAIN", "dB", -18.0f, 18.0f, 0.0f},                                  \
    {id "_q", nm " Q", "", 0.3f, 10.0f, 0.7f, Taper::Log}
#define HEM_SET(s, sn)                                                                    \
    HEM_B(s "_low", sn "LOW", 20.0f, 250.0f, 100.0f, 1.0f), HEM_B(s "_lowmid", sn "LOW MID", 80.0f, 500.0f, 250.0f, 0.0f), \
    HEM_B(s "_mid", sn "MID", 250.0f, 2500.0f, 1000.0f, 0.0f), HEM_B(s "_highmid", sn "HIGH MID", 1000.0f, 8000.0f, 3000.0f, 0.0f), \
    HEM_B(s "_high", sn "HIGH", 4000.0f, 20000.0f, 10000.0f, 2.0f),                       \
    {s "_hpf_power", sn "HPF POWER", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},                \
    {s "_hpf_freq", sn "HPF FREQ", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},              \
    {s "_hpf_slope", sn "HPF SLOPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kSlopeN},       \
    {s "_lpf_power", sn "LPF POWER", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},                \
    {s "_lpf_freq", sn "LPF FREQ", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},        \
    {s "_lpf_slope", sn "LPF SLOPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kSlopeN}
const ParamInfo kParams[kCount] = {
    HEM_SET("a", ""), HEM_SET("b", "B "),
    {"eq_mode", "EQ MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"global_gain", "GLOBAL GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"analyzer", "ANALYZER", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kAnaN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_B
#undef HEM_SET
const hc::fx::MeterInfo kMeters[] = {{"IN L", "dB", -60, 6}, {"IN R", "dB", -60, 6}, {"OUT L", "dB", -60, 6}, {"OUT R", "dB", -60, 6}};

constexpr std::uint32_t kSub = 8;
// Butterworth section Qs: 12 dB = 1 section, 24 dB = 2, 48 dB = 4
constexpr std::array<std::array<float, 4>, 3> kBwQ{{{0.7071f, 0, 0, 0}, {0.5412f, 1.3066f, 0, 0}, {0.5098f, 0.6013f, 0.9000f, 2.5629f}}};
constexpr std::array<int, 3> kSections{1, 2, 4};

class ParametricEq final : public hc::fx::Effect {
public:
    ParametricEq() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i >= kSetB && i < kMode) return static_cast<int>(p(kMode)) != 0;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& c : ch_) { for (auto& g : c.gl) g.prepare(fs, kSub, 30.0, 0.0f); for (auto& s : c.on) s.prepare(fs, 20.0, 0.0f); }
        glob_.prepare(fs, 20.0, 1.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : ch_) {
            for (auto& f : c.band) f.clear();
            for (auto& f : c.hp) f.clear();
            for (auto& f : c.lp) f.clear();
            c.used.fill(false); c.fade.fill(0); c.key = -1;
        }
        snap_ = true; loaded_ = false; pk_ = {};
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float gT = kit::dbToGain(p(kGlobal));
        if (!loaded_) { glob_.cur = gT; loaded_ = true; }
        const int mode = static_cast<int>(p(kMode));
        for (std::uint32_t i = 0; i < n; ++i) { pk_.in[0] = std::max(pk_.in[0], std::fabs(L[i])); pk_.in[1] = std::max(pk_.in[1], std::fabs(R[i])); }
        if (mode == 2) for (std::uint32_t i = 0; i < n; ++i) { const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]); L[i] = m; R[i] = s; }
        float* io[2] = {L, R};
        for (std::size_t c = 0; c < 2; ++c) run(ch_[c], io[c], n, c == 1 && mode != 0 ? kSetB : kSetA);
        if (mode == 2) for (std::uint32_t i = 0; i < n; ++i) { const float l = L[i] + R[i], r = L[i] - R[i]; L[i] = l; R[i] = r; }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = glob_.next(gT);
            L[i] = kit::sane(L[i] * g); R[i] = kit::sane(R[i] * g);
            pk_.out[0] = std::max(pk_.out[0], std::fabs(L[i])); pk_.out[1] = std::max(pk_.out[1], std::fabs(R[i]));
        }
        snap_ = false;
        publishMeter(0, kit::gainToDb(pk_.in[0])); publishMeter(1, kit::gainToDb(pk_.in[1]));
        publishMeter(2, kit::gainToDb(pk_.out[0])); publishMeter(3, kit::gainToDb(pk_.out[1]));
        pk_ = {};
    }

private:
    struct Chan {
        std::array<hc::fx::Biquad, kBands> band{};
        std::array<hc::fx::Biquad, 4> hp{}, lp{};
        std::array<kit::BlockGlide, 17> gl{};          // 5 freq, 5 gain, 5 Q, HPF freq, LPF freq
        std::array<hc::fx::Smoothed, kBands + 2> on{};  // power of each band, HPF, LPF (20 ms fades)
        std::array<bool, kBands + 2> used{};
        std::array<float, kBands + 2> fade{};
        float key = -1;
    };
    float glide(Chan& h, std::size_t k, float t, bool logd) noexcept {
        auto& g = h.gl[k];
        const float v = logd ? std::log(t) : t;
        if (snap_) g.cur = v;
        const float o = g.step(v);
        return logd ? std::exp(o) : o;
    }
    void run(Chan& h, float* x, std::uint32_t n, int b0) noexcept {
        const float inc = static_cast<float>(1.0 / (0.02 * fs_));
        std::array<float, kBands + 2> onT{};
        for (int b = 0; b < kBands; ++b) onT[static_cast<std::size_t>(b)] = p(b0 + b * kBandP + bPower) > 0.5f ? 1.0f : 0.0f;
        const int f0 = b0 + kBands * kBandP;
        onT[5] = p(f0 + fHpOn) > 0.5f ? 1.0f : 0.0f; onT[6] = p(f0 + fLpOn) > 0.5f ? 1.0f : 0.0f;
        if (snap_) for (std::size_t k = 0; k < onT.size(); ++k) h.on[k].cur = onT[k];
        const int hpS = static_cast<int>(p(f0 + fHpSlope)), lpS = static_cast<int>(p(f0 + fLpSlope));
        for (std::uint32_t at = 0; at < n; at += kSub) {
            const std::uint32_t end = std::min(n, at + kSub);
            std::array<float, kBands> f{}, g{}, q{};
            std::array<int, kBands> type{};
            float key = static_cast<float>(b0) + static_cast<float>(hpS) * 3.0f + static_cast<float>(lpS) * 5.0f;
            for (int b = 0; b < kBands; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                const int q0 = b0 + b * kBandP;
                f[bi] = glide(h, bi, p(q0 + bFreq), true); g[bi] = glide(h, 5 + bi, p(q0 + bGain), false); q[bi] = glide(h, 10 + bi, p(q0 + bQ), true);
                type[bi] = static_cast<int>(p(q0 + bType));
                key += (f[bi] * 0.01f + g[bi] * 7.0f + q[bi] * 3.0f + static_cast<float>(type[bi]) * 11.0f) * static_cast<float>(b + 1) * 1.37f;
            }
            const float hpF = glide(h, 15, p(f0 + fHpF), true), lpF = glide(h, 16, p(f0 + fLpF), true);
            key += hpF * 0.123f + lpF * 0.0071f;
            if (key != h.key) {
                h.key = key;
                const double ny = fs_ * 0.45;
                for (std::size_t b = 0; b < kBands; ++b) {
                    const double hz = std::min<double>(f[b], ny);
                    if (type[b] == 3 || std::fabs(g[b]) > 1e-4f) h.used[b] = true;
                    switch (type[b]) {
                        case 1: h.band[b].c = hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, hz, q[b], g[b]); break;
                        case 2: h.band[b].c = hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, hz, q[b], g[b]); break;
                        case 3: h.band[b].c = hc::fx::BiquadCoefs::make(BT::Notch, fs_, hz, q[b]); break;
                        default: h.band[b].c = hc::fx::BiquadCoefs::make(BT::Bell, fs_, hz, q[b], g[b]); break;
                    }
                }
                for (int s = 0; s < kSections[static_cast<std::size_t>(hpS)]; ++s) h.hp[static_cast<std::size_t>(s)].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hpF, kBwQ[static_cast<std::size_t>(hpS)][static_cast<std::size_t>(s)]);
                for (int s = 0; s < kSections[static_cast<std::size_t>(lpS)]; ++s) h.lp[static_cast<std::size_t>(s)].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lpF, ny), kBwQ[static_cast<std::size_t>(lpS)][static_cast<std::size_t>(s)]);
            }
            if (onT[5] > 0.5f) h.used[5] = true;
            if (onT[6] > 0.5f) h.used[6] = true;
            for (std::uint32_t i = at; i < end; ++i) {
                float v = x[i];
                for (std::size_t k = 0; k < kBands + 2; ++k) {
                    const float w = h.on[k].next(onT[k]);
                    if (!h.used[k]) continue;
                    if (h.fade[k] < 1.0f) h.fade[k] = std::min(1.0f, h.fade[k] + inc);   // a stage that starts running fades in
                    float y;
                    if (k < kBands) y = h.band[k].tick(v);
                    else if (k == 5) { y = v; for (int s = 0; s < kSections[static_cast<std::size_t>(hpS)]; ++s) y = h.hp[static_cast<std::size_t>(s)].tick(y); }
                    else { y = v; for (int s = 0; s < kSections[static_cast<std::size_t>(lpS)]; ++s) y = h.lp[static_cast<std::size_t>(s)].tick(y); }
                    v += w * h.fade[k] * (y - v);
                }
                x[i] = v;
            }
        }
    }

    struct Pk { std::array<float, 2> in{}, out{}; };
    double fs_ = 48000;
    std::array<Chan, 2> ch_{};
    hc::fx::Smoothed glob_;
    Pk pk_{};
    bool snap_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeParametricEq() { return std::make_unique<ParametricEq>(); }

} // namespace hem::staging
