// =============================================================================
//  19 GLOWLEVEL — optical-style leveling: two big knobs (COMPRESSION = how hard, GAIN = how loud)
//  and no attack / release knobs on purpose — the "light cell" sets its own timing from the music:
//  about 10 ms to grab, a quick first recovery to half, then a slow tail that gets slower the
//  longer and harder it has been working (0.5 .. 5 s). COMPRESS about 3:1, LIMITER about 10:1.
//  HI FREQ DETECTOR EMPHASIS: 100 = flat; lower makes highs push the cell harder (de-essy leveling).
//  Different from OPTICAL COMPRESSOR (45): that one has threshold / ratio / attack / release knobs.
//  Dead flat on load (verified spec): COMPRESSION 0, GAIN 0 (= 0 dB), MIX 0 %, trim 0 dB, noise OFF.
//  GAIN 0..100 maps to 0..+40 dB (ASSUMED). The meter selector (IN / GR / OUT) only picks the display.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kGain, kComp, kMode, kAutoMakeup, kHiFreq, kMix, kTrim, kNoise, kMeterSel, kClipReset, kBypass, kCount };
const char* const kModeN[] = {"COMPRESS", "LIMITER"};
const char* const kNoiseN[] = {"OFF", "50 HZ", "60 HZ"};
const char* const kMeterN[] = {"IN", "GR", "OUT"};
const ParamInfo kParams[kCount] = {
    {"gain", "GAIN", "", 0.0f, 100.0f, 0.0f},
    {"compression", "COMPRESSION", "", 0.0f, 100.0f, 0.0f},
    {"mode", "MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN},
    {"auto_makeup", "AUTO MAKEUP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"hi_freq", "HI FREQ DETECTOR EMPHASIS", "", 0.0f, 100.0f, 100.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"trim", "POST-MIX TRIM", "dB", -18.0f, 18.0f, 0.0f},
    {"analog_noise", "ANALOG NOISE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kNoiseN},
    {"meter", "METER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kMeterN},
    {"clip_reset", "CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"VU", "dB", -20, 6, true}, {"GAIN REDUCTION", "dB", -30, 0}, {"CLIP", "", 0, 1}, {"OUTPUT", "dBFS", -60, 0}};

class GlowLevel final : public hc::fx::Effect {
public:
    GlowLevel() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs; mix_.prepare(fs, 20.0, 0.0f); trim_.prepare(fs, 20.0, 1.0f); gain_.prepare(fs, 20.0, 1.0f); comp_.prepare(fs, 20.0, 0.0f);
        outM_.prepare(fs); reset();
    }
    void reset() noexcept override { dc_.fill(0); det_.clear(); cell_ = 0; slow_ = 0; work_ = 0; grDb_ = 0; vuIn_ = vuOut_ = 0; clip_ = false; humPh_ = 0; rng_ = 777u; hfKey_ = -1; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kClipReset) > 0.5f) clip_ = false;
        if (p(kBypass) > 0.5f) { publishAll(); return; }
        const float hf = p(kHiFreq);
        if (hfKey_ != hf) { hfKey_ = hf; det_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, 3000.0, 0.7, (100.0f - hf) * 0.15f)); }   // up to +15 dB of highs into the cell
        const bool limit = p(kMode) > 0.5f;
        const float ratio = limit ? 10.0f : 3.0f;
        const float att = hc::fx::onePoleCoef(fs_, 10.0), fastRel = hc::fx::onePoleCoef(fs_, 60.0);
        const float mixT = p(kMix) * 0.01f, trimT = kit::dbToGain(p(kTrim));
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        const float c = fs_ > 0 ? 1.0f / static_cast<float>(fs_) : 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = L[i], dr = R[i];
            vuIn_ = 0.5f * (std::fabs(dl) + std::fabs(dr)) + 0.9995f * (vuIn_ - 0.5f * (std::fabs(dl) + std::fabs(dr)));
            float yl = dl, yr = dr;
            if (wet) {
                const float compAmt = comp_.next(p(kComp));
                // detector: peak of the (emphasised) mono signal, scaled by COMPRESSION (0 .. +40 dB into the cell)
                float sl = 0.5f * (dl + dr), sr = 0.0f;
                det_.run(&sl, &sr, 1);
                const float drive = kit::dbToGain(compAmt * 0.4f);
                const float x = std::fabs(sl) * drive;
                const float over = std::max(0.0f, kit::gainToDb(x) - (-10.0f));   // the cell's knee sits at -10 dBFS after drive (ASSUMED)
                const float want = compAmt > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;
                // the light cell: fast grab, quick first recovery, slow tail that lengthens with work done
                if (want > cell_) cell_ = want + att * (cell_ - want);
                else {
                    const float tailMs = 500.0f + 4500.0f * std::min(1.0f, work_);
                    const float slowC = std::exp(-1000.0f * c / tailMs);
                    cell_ = cell_ > 0.5f * slow_ ? want + fastRel * (cell_ - want) : want + slowC * (cell_ - want);
                }
                slow_ = std::max(cell_, slow_ * 0.99995f);
                work_ = std::clamp(work_ + c * (cell_ > 1.0f ? 0.3f : -0.1f), 0.0f, 1.0f);   // ~3 s of hard work for the longest tail
                grDb_ = -cell_;
                float g = kit::dbToGain(-cell_) * gain_.next(kit::dbToGain(p(kGain) * 0.4f));
                if (p(kAutoMakeup) > 0.5f) g *= kit::dbToGain(compAmt * 0.4f * (1.0f - 1.0f / ratio) * 0.5f);
                yl = dl * g; yr = dr * g;
                // analog character: a faint even harmonic that grows with the cell's work
                const float h = 0.02f * std::min(1.0f, cell_ / 12.0f);
                yl += h * yl * yl; yr += h * yr * yr;   // x^2: an even harmonic (its DC is removed below)
                dc_[0] += 0.0005f * (yl - dc_[0]); dc_[1] += 0.0005f * (yr - dc_[1]);
                if (h > 0.0f) { yl -= dc_[0]; yr -= dc_[1]; }
                const float mx = mix_.next(mixT);
                yl = dl + mx * (yl - dl); yr = dr + mx * (yr - dr);
            } else grDb_ = 0.0f;
            const float t = trim_.next(trimT);
            yl *= t; yr *= t;
            if (static_cast<int>(p(kNoise)) > 0) {   // ANALOG NOISE: mains hum (+2nd harmonic) and hiss, around -80 dBFS
                const float hz = static_cast<int>(p(kNoise)) == 1 ? 50.0f : 60.0f;
                humPh_ += static_cast<double>(hz) / fs_; humPh_ -= std::floor(humPh_);
                rng_ = rng_ * 1664525u + 1013904223u;
                const float hiss = (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f) * 6e-5f;
                const float hum = 1e-4f * std::sin(kit::kTau * static_cast<float>(humPh_)) + 3e-5f * std::sin(2.0f * kit::kTau * static_cast<float>(humPh_));
                yl += hum + hiss; yr += hum - hiss;
            }
            if (std::fabs(yl) >= 1.0f || std::fabs(yr) >= 1.0f) clip_ = true;
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
            vuOut_ = 0.5f * (std::fabs(yl) + std::fabs(yr)) + 0.9995f * (vuOut_ - 0.5f * (std::fabs(yl) + std::fabs(yr)));
        }
        outM_.feed(L, n);
        publishAll();
    }

private:
    void publishAll() noexcept {
        const int sel = static_cast<int>(p(kMeterSel));
        const float vu = sel == 1 ? grDb_ : kit::gainToDb((sel == 0 ? vuIn_ : vuOut_) * 1.11f) + 18.0f;   // 0 VU = -18 dBFS
        publishMeter(0, std::clamp(vu, -20.0f, 6.0f)); publishMeter(1, grDb_); publishMeter(2, clip_ ? 1.0f : 0.0f); publishMeter(3, outM_.db());
        publishReduction(grDb_);
    }
    double fs_ = 48000;
    hc::fx::Biquad2 det_;
    hc::fx::Smoothed mix_, trim_, gain_, comp_;
    std::array<float, 2> dc_{};
    float cell_ = 0, slow_ = 0, work_ = 0, grDb_ = 0, vuIn_ = 0, vuOut_ = 0, hfKey_ = -1;
    bool clip_ = false;
    double humPh_ = 0;
    std::uint32_t rng_ = 777u;
    kit::PeakMeter outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeGlowLevel() { return std::make_unique<GlowLevel>(); }

} // namespace hem::staging
