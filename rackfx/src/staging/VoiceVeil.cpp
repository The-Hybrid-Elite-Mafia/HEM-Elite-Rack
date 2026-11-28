// =============================================================================
//  08 VOICEVEIL — vocal noise reduction (hum, hiss, room tone, fans) with classical DSP:
//  a short-time spectrum (sqrt-Hann, 75 % overlap), a per-bin noise profile learned in the gaps
//  (frames whose level sits at the running floor — a held note is never learned as noise),
//  and a smoothed Wiener-style gain. Until it has heard a gap it removes nothing. No trained model (approved 2026-11-27).
//  Different from ROOMERASER (41), which removes room echo, and from the rack's THE CLEANER,
//  a whole-mix hiss reducer: VoiceVeil's floor, gains and smoothing are voiced for a voice.
//  Dead flat on load: NOISE REDUCTION 0 (the dry voice, delayed by the reported latency), WIDTH 100 %.
//  Latency: one frame (1024 samples).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>
#include <complex>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P { kAmount, kModel, kAnalysis, kReset, kWidth, kBypass, kCount };
const char* const kModelN[] = {"VOCAL I", "VOCAL II", "SPEECH ECO"};
const char* const kAnaN[] = {"SINGLE", "DOUBLE"};
const ParamInfo kParams[kCount] = {
    {"reduction", "NOISE REDUCTION", "", 0.0f, 100.0f, 0.0f},
    {"model", "PROCESSING MODEL", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModelN},
    {"analysis", "ANALYSIS", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kAnaN},
    {"reset", "RESET HISTORY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"width", "STEREO WIDTH", "%", 0.0f, 100.0f, 100.0f},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "dB", -100, 0}, {"REDUCTION", "dB", -40, 0}, {"NOISE FLOOR", "dB", -120, 0}, {"OUTPUT", "dB", -100, 0},
};

constexpr int kN = 1024, kHop = 256, kBins = kN / 2 + 1;

// per model: noise over-estimate, gain floor at full reduction (dB), gain smoothing per frame, band (Hz)
struct Model { float over, floorDb, smooth, lo, hi; bool spectralSmooth; };
constexpr std::array<Model, 3> kModels{{
    {2.0f, -24.0f, 0.6f, 0.0f, 24000.0f, false},   // VOCAL I: gentle, keeps air
    {2.5f, -36.0f, 0.75f, 0.0f, 24000.0f, true},   // VOCAL II: stronger, smoothed across bins
    {1.8f, -30.0f, 0.5f, 90.0f, 8000.0f, false},   // SPEECH ECO: speech band only, outside it a fixed cut
}};

class VoiceVeil final : public hc::fx::Effect {
public:
    VoiceVeil() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kN; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fft_.prepare(kN);
        for (int i = 0; i < kN; ++i) win_[static_cast<std::size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(kit::kTau * static_cast<float>(i) / kN));
        widthG_.prepare(fs, 20.0, p(kWidth) * 0.01f);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : ch_) { c.in.fill(0.0f); c.out.fill(0.0f); c.dly.fill(0.0f); c.gain.fill(1.0f); c.pow.fill(0.0f); c.noise.fill(0.0f); c.floor = -1.0f; }
        pos_ = 0; hopCount_ = 0; wet_ = 0.0f; lastReset_ = p(kReset) > 0.5f; redDb_ = 0; floorDb_ = -120; inDb_ = outDb_ = -100;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const bool rst = p(kReset) > 0.5f;
        if (rst && !lastReset_) for (auto& c : ch_) { c.noise.fill(0.0f); c.gain.fill(1.0f); c.floor = -1.0f; }   // RESET HISTORY (on the press)
        lastReset_ = rst;
        const float amount = p(kAmount) * 0.01f;
        const float fadeC = hc::fx::onePoleCoef(fs_, 20.0);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float target = amount > 0.0f ? 1.0f : 0.0f;
            wet_ = target + fadeC * (wet_ - target);
            const auto at = static_cast<std::size_t>(pos_);
            for (int c = 0; c < 2; ++c) {
                Chan& ch = ch_[static_cast<std::size_t>(c)];
                const float x = io[c][i];
                const float dry = ch.dly[at];                  // the dry path: the same signal, kN samples late
                const float wet = ch.out[at];
                ch.out[at] = 0.0f;
                ch.in[at] = x; ch.dly[at] = x;
                io[c][i] = wet_ > 1e-6f ? dry + wet_ * (wet - dry) : dry;
            }
            pos_ = (pos_ + 1) % kN;
            if (++hopCount_ == kHop) { hopCount_ = 0; frame(amount); }
        }
        // stereo width (100 % = untouched)
        const float wT = p(kWidth) * 0.01f;
        if (!(kit::isFlat(wT, 1.0f) && kit::isFlat(widthG_.cur, 1.0f)))
            for (std::uint32_t i = 0; i < n; ++i) {
                const float w = widthG_.next(wT);
                const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * w;
                L[i] = kit::sane(m + s); R[i] = kit::sane(m - s);
            }
        publishMeter(0, inDb_); publishMeter(1, redDb_); publishMeter(2, floorDb_); publishMeter(3, outDb_);
        publishReduction(redDb_);
    }

private:
    struct Chan {
        std::array<float, kN> in{}, out{}, dly{};
        std::array<float, kBins> gain{}, pow{}, noise{};
        float floor = -1.0f;   // running floor of the frame level (-1 = not yet heard)
    };

    // one analysis frame per hop: spectrum -> noise floor -> gain -> back, overlap-added kN samples ahead
    void frame(float amount) noexcept {
        const Model& md = kModels[static_cast<std::size_t>(p(kModel))];
        const bool dbl = p(kAnalysis) > 0.5f;
        const float floorG = kit::dbToGain(md.floorDb * amount);
        float inPow = 0, outPow = 0, noisePow = 0, gainSum = 0;
        std::array<std::array<std::complex<float>, kN>, 2>& sp = spec_;
        for (int c = 0; c < 2; ++c) {
            Chan& ch = ch_[static_cast<std::size_t>(c)];
            for (int k = 0; k < kN; ++k) {
                const auto src = static_cast<std::size_t>((pos_ + k) % kN);   // oldest first
                sp[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)] = {ch.in[src] * win_[static_cast<std::size_t>(k)], 0.0f};
            }
            fft_.forward(sp[static_cast<std::size_t>(c)].data());
        }
        // the noise floor and the gain, from the summed (SINGLE) or each side's own (DOUBLE) spectrum
        for (int c = 0; c < (dbl ? 2 : 1); ++c) {
            Chan& ch = ch_[static_cast<std::size_t>(c)];
            // is this frame a gap? its level near the running floor (the floor drops at once, rises ~1 dB per second)
            float level = 0.0f;
            for (int b = 0; b < kBins; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                level += dbl ? std::norm(sp[static_cast<std::size_t>(c)][bi]) : 0.25f * std::norm(sp[0][bi] + sp[1][bi]);
            }
            ch.floor = ch.floor < 0.0f ? level : std::min(ch.floor * 1.0013f, level);
            const bool gap = level <= ch.floor * 2.0f;   // within 3 dB of the floor
            for (int b = 0; b < kBins; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                float pw = std::norm(sp[static_cast<std::size_t>(c)][bi]);
                if (!dbl) pw = 0.25f * std::norm(sp[0][bi] + sp[1][bi]);
                ch.pow[bi] = 0.85f * ch.pow[bi] + 0.15f * pw;   // steadier per-bin power: fewer random leaks ("musical noise")
                if (gap) ch.noise[bi] = 0.9f * ch.noise[bi] + 0.1f * pw;   // learn the noise only in the gaps
                const float hz = static_cast<float>(b) * static_cast<float>(fs_) / kN;
                float g = 1.0f - md.over * ch.noise[bi] / std::max(ch.pow[bi], 1e-12f);
                if (hz < md.lo || hz > md.hi) g = 0.0f;                    // ECO: outside the speech band, the floor
                g = std::max(g, 0.0f);
                g = floorG + (1.0f - floorG) * g;                           // never below the model's floor
                ch.gain[bi] = md.smooth * ch.gain[bi] + (1.0f - md.smooth) * g;
                inPow += pw; noisePow += ch.noise[bi]; gainSum += ch.gain[bi];
            }
            if (md.spectralSmooth) {
                float prev = ch.gain[0];
                for (int b = 1; b < kBins - 1; ++b) {
                    const auto bi = static_cast<std::size_t>(b);
                    const float cur = ch.gain[bi];
                    ch.gain[bi] = 0.25f * prev + 0.5f * cur + 0.25f * ch.gain[bi + 1];
                    prev = cur;
                }
            }
        }
        for (int c = 0; c < 2; ++c) {
            Chan& ch = ch_[static_cast<std::size_t>(c)];
            const auto& gsrc = ch_[static_cast<std::size_t>(dbl ? c : 0)].gain;
            auto& s = sp[static_cast<std::size_t>(c)];
            for (int b = 0; b < kBins; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                const float g = gsrc[bi];
                s[bi] *= g;
                if (b > 0 && b < kN / 2) s[static_cast<std::size_t>(kN - b)] *= g;
                outPow += std::norm(s[bi]);
            }
            fft_.inverse(s.data());
            // sqrt-Hann in and out at 75 % overlap sums to 2: scale by 0.5
            for (int k = 0; k < kN; ++k) {
                const auto dst = static_cast<std::size_t>((pos_ + k) % kN);
                ch.out[dst] += 0.5f * s[static_cast<std::size_t>(k)].real() * win_[static_cast<std::size_t>(k)];
            }
        }
        const int nb = dbl ? 2 * kBins : kBins;
        redDb_ = kit::gainToDb(gainSum / static_cast<float>(nb));
        floorDb_ = 10.0f * std::log10(std::max(noisePow / static_cast<float>(nb), 1e-12f));
        inDb_ = 10.0f * std::log10(std::max(inPow / static_cast<float>(nb), 1e-12f));
        outDb_ = 10.0f * std::log10(std::max(outPow / static_cast<float>(2 * kBins), 1e-12f));
    }

    double fs_ = 48000;
    hc::fx::Fft fft_;
    std::array<float, kN> win_{};
    std::array<Chan, 2> ch_{};
    std::array<std::array<std::complex<float>, kN>, 2> spec_{};
    int pos_ = 0, hopCount_ = 0;
    float wet_ = 0, redDb_ = 0, floorDb_ = -120, inDb_ = -100, outDb_ = -100;
    bool lastReset_ = false;
    hc::fx::Smoothed widthG_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeVoiceVeil() { return std::make_unique<VoiceVeil>(); }

} // namespace hem::staging
