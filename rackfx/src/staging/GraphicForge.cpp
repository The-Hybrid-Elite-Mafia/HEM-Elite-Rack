// =============================================================================
//  17 GRAPHICFORGE — ten-band graphic EQ, 31 Hz .. 16 kHz, +-12 dB per fader, with
//  proportional Q: a band gets narrower the harder it is pushed (no Q knob, by design).
//  ANALOG adds a gentle transformer-style stage (soft saturation, a touch of low-end weight and
//  top roll-off) that stays in when EQ is switched OUT — like the hardware it follows.
//  Different from PARAMETRIC EQ (46, clean, true Q), CONSOLECORE (28) and SILKCONTOUR (18).
//  Dead flat on load: every fader 0 dB, EQ IN, ANALOG off, polarity normal, output 0 dB.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

constexpr int kBands = 10;
constexpr std::array<float, kBands> kHz{31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
enum P { kEqIn = kBands, kAnalog, kPolarity, kOutput, kCount };
const ParamInfo kParams[kCount] = {
    {"g31", "31 HZ", "dB", -12.0f, 12.0f, 0.0f}, {"g63", "63 HZ", "dB", -12.0f, 12.0f, 0.0f},
    {"g125", "125 HZ", "dB", -12.0f, 12.0f, 0.0f}, {"g250", "250 HZ", "dB", -12.0f, 12.0f, 0.0f},
    {"g500", "500 HZ", "dB", -12.0f, 12.0f, 0.0f}, {"g1k", "1 KHZ", "dB", -12.0f, 12.0f, 0.0f},
    {"g2k", "2 KHZ", "dB", -12.0f, 12.0f, 0.0f}, {"g4k", "4 KHZ", "dB", -12.0f, 12.0f, 0.0f},
    {"g8k", "8 KHZ", "dB", -12.0f, 12.0f, 0.0f}, {"g16k", "16 KHZ", "dB", -12.0f, 12.0f, 0.0f},
    {"eq_in", "EQ", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"analog", "ANALOG", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"polarity", "POLARITY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"output", "OUTPUT", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"LEFT", "dBFS", -60, 0}, {"RIGHT", "dBFS", -60, 0}, {"PEAK", "dBFS", -60, 0}};

// proportional Q: about 0.7 at a nudge, 2.5 at +-12 dB (ASSUMED curve)
[[nodiscard]] inline double qFor(float g) noexcept { return 0.7 + 0.15 * std::fabs(g); }

class GraphicForge final : public hc::fx::Effect {
public:
    GraphicForge() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override { return i >= kBands || p(kEqIn) > 0.5f; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (int b = 0; b < kBands; ++b) gl_[static_cast<std::size_t>(b)].prepare(fs, 32, 30.0, p(b));
        outG_.prepare(fs, 20.0, kit::dbToGain(p(kOutput))); anMix_.prepare(fs, 20.0, 0.0f);
        mL_.prepare(fs); mR_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& f : eq_) f.clear();
        on_.fill(false); keys_.fill(1e9f); weight_.clear(); roll_.clear(); peak_ = 0;
        weight_.set(hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, 80.0, 0.7, 0.6));
        roll_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(30000.0, fs_ * 0.45), 0.6));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        constexpr std::uint32_t kSub = 32;
        const bool eqIn = p(kEqIn) > 0.5f;
        for (std::uint32_t at = 0; at < n; at += kSub) {
            const std::uint32_t m = std::min(kSub, n - at);
            float* l = L + at; float* r = R + at;
            for (int b = 0; b < kBands; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                const float g = gl_[bi].step(eqIn ? p(b) : 0.0f);   // EQ OUT glides every band to 0 dB
                if (kit::isFlat(g) && !on_[bi]) continue;            // never used: skip (once used it runs, exact at 0 dB)
                on_[bi] = true;
                if (keys_[bi] != g) { keys_[bi] = g; eq_[bi].set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, kHz[bi], qFor(g), g)); }
                eq_[bi].run(l, r, m);
            }
            // ANALOG: a transformer-style stage, faded in / out over 20 ms
            const float anT = p(kAnalog) > 0.5f ? 1.0f : 0.0f;
            if (anT > 0.0f || anMix_.cur > 1e-6f) {
                for (std::uint32_t i = 0; i < m; ++i) { buf_[0][i] = l[i]; buf_[1][i] = r[i]; }
                weight_.run(buf_[0].data(), buf_[1].data(), m);
                for (std::uint32_t i = 0; i < m; ++i) {
                    for (int c = 0; c < 2; ++c) {
                        const float x = buf_[static_cast<std::size_t>(c)][i];
                        buf_[static_cast<std::size_t>(c)][i] = x - 0.08f * x * x * x + 0.02f * x * x;   // mostly 3rd, a little 2nd
                    }
                }
                roll_.run(buf_[0].data(), buf_[1].data(), m);
                for (std::uint32_t i = 0; i < m; ++i) {
                    const float k = anMix_.next(anT);
                    l[i] += k * (buf_[0][i] - l[i]); r[i] += k * (buf_[1][i] - r[i]);
                }
            }
            const float pol = p(kPolarity) > 0.5f ? -1.0f : 1.0f;
            const float oT = kit::dbToGain(p(kOutput)) * pol;
            if (!(kit::isFlat(oT, 1.0f) && kit::isFlat(outG_.cur, 1.0f)))
                for (std::uint32_t i = 0; i < m; ++i) { const float o = outG_.next(oT); l[i] *= o; r[i] *= o; }
            for (std::uint32_t i = 0; i < m; ++i) { l[i] = kit::sane(l[i]); r[i] = kit::sane(r[i]); }
        }
        mL_.feed(L, n); mR_.feed(R, n);
        peak_ = std::max(peak_ * 0.999f, std::max(mL_.env, mR_.env));
        publishMeter(0, mL_.db()); publishMeter(1, mR_.db()); publishMeter(2, kit::gainToDb(peak_));
    }

private:
    double fs_ = 48000;
    std::array<hc::fx::Biquad2, kBands> eq_{};
    std::array<kit::BlockGlide, kBands> gl_{};
    std::array<bool, kBands> on_{};
    std::array<float, kBands> keys_{};
    hc::fx::Biquad2 weight_, roll_;
    std::array<std::array<float, 32>, 2> buf_{};
    hc::fx::Smoothed outG_, anMix_;
    kit::PeakMeter mL_, mR_;
    float peak_ = 0;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeGraphicForge() { return std::make_unique<GraphicForge>(); }

} // namespace hem::staging
