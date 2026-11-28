// =============================================================================
//  45 OPTICAL COMPRESSOR — the smooth light-cell sound with full control: THRESHOLD, RATIO
//  (1.5:1 .. 12:1), ATTACK, RELEASE, KNEE (SOFT / MED / HARD), MAKEUP, MIX. The cell does not let go
//  in a straight line: it recovers part of the way at the RELEASE time, then the rest slowly, and the
//  slow part gets slower the longer it has been held down. MODE sets how slow and sticky the cell is:
//  CLEAN (light), SMOOTH (more), VINTAGE (most, plus a faint warmth that grows with the squeeze).
//  SIDECHAIN: INT, HPF (the detector ignores the lows below 100 Hz) or EXT (an external key).
//  Different from GLOWLEVEL (19: no timing knobs on purpose) and VCA COMPRESSOR (52: fast, exact).
//  Dead flat on load: THRESHOLD 0 dB, RATIO 1.5:1, MAKEUP 0 dB, MIX 0 %.
//  ASSUMED: knee widths 12 / 6 / 0 dB; slow tail 2x / 4x / 8x RELEASE for CLEAN / SMOOTH / VINTAGE,
//  attack 1x / 1.5x / 2.5x; the detector averages over 10 ms; 0 VU = -18 dBFS.
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kThr, kRatio, kAtt, kRel, kKnee, kMakeup, kMix, kMode, kSc, kBypass, kCount };
const char* const kKneeN[] = {"SOFT", "MED", "HARD"};
const char* const kModeN[] = {"CLEAN", "SMOOTH", "VINTAGE"};
const char* const kScN[] = {"INT", "HPF", "EXT"};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -40.0f, 0.0f, 0.0f},
    {"ratio", "RATIO", ":1", 1.5f, 12.0f, 1.5f, Taper::Log},
    {"attack", "ATTACK", "ms", 1.0f, 300.0f, 30.0f, Taper::Log},
    {"release", "RELEASE", "ms", 10.0f, 3000.0f, 300.0f, Taper::Log},
    {"knee", "KNEE", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kKneeN},
    {"makeup", "MAKEUP", "dB", -12.0f, 24.0f, 0.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"mode", "MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"sidechain", "SIDECHAIN", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kScN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 0}, {"OUT", "dB", -60, 0}, {"VU INPUT", "VU", -20, 3, true}, {"VU OUTPUT", "VU", -20, 3, true}, {"GAIN REDUCTION", "dB", -24, 0}};

class OpticalComp final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    OpticalComp() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        hp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs, 100.0, 0.7071));
        mix_.prepare(fs, 20.0, 0.0f); mk_.prepare(fs, 20.0, 1.0f); thr_.prepare(fs, 30.0, 0.0f); rat_.prepare(fs, 30.0, 1.5f);
        reset();
    }
    void reset() noexcept override { hp_.clear(); det_ = 0; cell_ = 0; slow_ = 0; held_ = 0; dc_.fill(0); grM_ = 0; inPk_ = outPk_ = vuI_ = vuO_ = 0; loaded_ = false; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        const float mixT = p(kMix) * 0.01f, mkT = kit::dbToGain(p(kMakeup));
        if (!loaded_) { mix_.cur = mixT; mk_.cur = mkT; thr_.cur = p(kThr); rat_.cur = p(kRatio); loaded_ = true; }
        const int mode = static_cast<int>(p(kMode)), sc = static_cast<int>(p(kSc));
        static constexpr std::array<float, 3> kAttMul{1.0f, 1.5f, 2.5f}, kTail{2.0f, 4.0f, 8.0f}, kKneeDb{12.0f, 6.0f, 0.0f};
        const float att = hc::fx::onePoleCoef(fs_, p(kAtt) * kAttMul[static_cast<std::size_t>(mode)]);
        const float rel = hc::fx::onePoleCoef(fs_, p(kRel));
        const float knee = kKneeDb[static_cast<std::size_t>(p(kKnee))];
        const float detC = hc::fx::onePoleCoef(fs_, 10.0), vuC = hc::fx::onePoleCoef(fs_, 300.0);
        const float invFs = static_cast<float>(1.0 / fs_);
        const bool ext = sc == 2 && scL_ != nullptr;
        grM_ = 0;
        float inPk = 0, outPk = 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            float dl = ext ? scL_[i] : xl, dr = ext ? scR_[i] : xr;
            const float hl = hp_.l.tick(dl), hr = hp_.r.tick(dr);   // always runs, so switching to HPF never jumps
            if (sc == 1) { dl = hl; dr = hr; }
            const float e = 0.5f * (dl * dl + dr * dr);
            det_ = e + detC * (det_ - e);
            const float lvl = 10.0f * std::log10(std::max(2.0f * det_, 1e-12f));   // a sine reads its peak level
            const float t = thr_.next(p(kThr)), r = rat_.next(p(kRatio));
            const float over = lvl - t;
            float want;
            if (knee > 0 && std::fabs(over) < knee * 0.5f) { const float x = over + knee * 0.5f; want = (1.0f - 1.0f / r) * x * x / (2.0f * knee); }
            else want = over > 0 ? over * (1.0f - 1.0f / r) : 0.0f;
            // the light cell: grab at ATTACK; let go part way at RELEASE, then a slow tail that lengthens with hold time
            if (want > cell_) cell_ = want + att * (cell_ - want);
            else {
                const float tailMs = p(kRel) * kTail[static_cast<std::size_t>(mode)] * (1.0f + 2.0f * held_);
                const float slowC = std::exp(-1000.0f * invFs / tailMs);
                const float fastTarget = std::max(want, 0.4f * slow_);
                cell_ = cell_ > fastTarget ? fastTarget + rel * (cell_ - fastTarget) : want + slowC * (cell_ - want);
            }
            slow_ = std::max(cell_, slow_ * std::exp(-1000.0f * invFs / (p(kRel) * kTail[static_cast<std::size_t>(mode)])));
            held_ = std::clamp(held_ + invFs * (cell_ > 1.0f ? 0.3f : -0.15f), 0.0f, 1.0f);
            grM_ = std::max(grM_, cell_);
            const float g = kit::dbToGain(-cell_) * mk_.next(mkT);
            float cl = xl * g, cr = xr * g;
            if (mode == 2 && cell_ > 0.0f) {   // VINTAGE warmth grows with the squeeze
                const float h = 0.01f * std::min(1.0f, cell_ / 10.0f);
                cl += h * cl * cl; cr += h * cr * cr;
                dc_[0] += 0.0005f * (cl - dc_[0]); dc_[1] += 0.0005f * (cr - dc_[1]);
                cl -= dc_[0]; cr -= dc_[1];
            }
            const float mx = mix_.next(mixT);
            const float yl = xl + mx * (cl - xl), yr = xr + mx * (cr - xr);
            inPk = std::max(inPk, std::max(std::fabs(xl), std::fabs(xr))); outPk = std::max(outPk, std::max(std::fabs(yl), std::fabs(yr)));
            const float ai = 0.5f * (std::fabs(xl) + std::fabs(xr)), ao = 0.5f * (std::fabs(yl) + std::fabs(yr));
            vuI_ = ai + vuC * (vuI_ - ai); vuO_ = ao + vuC * (vuO_ - ao);
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        inPk_ = inPk; outPk_ = outPk; scL_ = scR_ = nullptr;
        publishMeter(0, kit::gainToDb(inPk_)); publishMeter(1, kit::gainToDb(outPk_));
        publishMeter(2, std::clamp(kit::gainToDb(vuI_ * 1.11f) + 18.0f, -20.0f, 3.0f)); publishMeter(3, std::clamp(kit::gainToDb(vuO_ * 1.11f) + 18.0f, -20.0f, 3.0f));
        const float gr = mixT > 0.0f ? -grM_ : 0.0f;
        publishMeter(4, gr); publishReduction(gr);
    }

private:
    double fs_ = 48000;
    hc::fx::Biquad2 hp_;
    hc::fx::Smoothed mix_, mk_, thr_, rat_;
    std::array<float, 2> dc_{};
    float det_ = 0, cell_ = 0, slow_ = 0, held_ = 0, grM_ = 0, inPk_ = 0, outPk_ = 0, vuI_ = 0, vuO_ = 0;
    bool loaded_ = false;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeOpticalComp() { return std::make_unique<OpticalComp>(); }

} // namespace hem::staging
