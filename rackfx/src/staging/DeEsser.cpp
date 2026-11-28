// =============================================================================
//  43 DE-ESSER — turns down harsh S and T sounds only while they happen. The detector listens through a
//  band-pass centred on FREQ (2..12 kHz); when that band goes over THRESH the gain comes down, by at
//  most RANGE. MODE: SPLIT-BAND (only the sibilant top, from about FREQ up, is turned down) or WIDEBAND
//  (the whole signal ducks). LISTEN plays what the detector hears. ATTACK and RELEASE set the timing.
//  Different from RESONANCEGUARD (13: ringing peaks anywhere) and DYNAMIC EQ 8 (01: general bands).
//  Dead flat on load: RANGE 0 dB (and THRESH 0 dB): no cut.
//  ASSUMED: 4:1 above the threshold; detector band Q 1.4; SPLIT-BAND works on the input minus a 12 dB/oct
//  low-pass at 0.75 x FREQ (complementary: with no cut it adds back to the input exactly).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kThr, kRange, kFreq, kMode, kListen, kAtt, kRel, kBypass, kCount };
const char* const kModeN[] = {"SPLIT-BAND", "WIDEBAND"};
const ParamInfo kParams[kCount] = {
    {"thresh", "THRESH", "dB", -48.0f, 0.0f, 0.0f},
    {"range", "RANGE", "dB", 0.0f, 24.0f, 0.0f},
    {"freq", "FREQ", "Hz", 2000.0f, 12000.0f, 6500.0f, Taper::Log},
    {"mode", "MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN},
    {"listen", "LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"attack", "ATTACK", "ms", 0.1f, 10.0f, 1.0f, Taper::Log},
    {"release", "RELEASE", "ms", 10.0f, 200.0f, 50.0f, Taper::Log},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 6}, {"OUT", "dB", -60, 6}, {"GAIN REDUCTION", "dB", -24, 0}};
constexpr std::uint32_t kSub = 8;

class DeEsser final : public hc::fx::Effect {
public:
    DeEsser() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        thr_.prepare(fs, 30.0, 0.0f); rng_.prepare(fs, 30.0, 0.0f); lis_.prepare(fs, 20.0, 0.0f);
        gl_.prepare(fs, kSub, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override { det_.clear(); hi_.clear(); env_ = 0; gr_ = 0; grM_ = 0; key_ = -1; snap_ = true; loaded_ = false; pkI_ = pkO_ = 0; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float lisT = p(kListen) > 0.5f ? 1.0f : 0.0f;
        if (!loaded_) { thr_.cur = p(kThr); rng_.cur = p(kRange); lis_.cur = lisT; loaded_ = true; }
        const bool wide = p(kMode) > 0.5f;
        const float att = hc::fx::onePoleCoef(fs_, p(kAtt)), rel = hc::fx::onePoleCoef(fs_, p(kRel));
        float pkI = 0, pkO = 0, grM = 0;
        for (std::uint32_t at = 0; at < n; at += kSub) {
            setFilters();
            snap_ = false;
            const std::uint32_t end = std::min(n, at + kSub);
            for (std::uint32_t i = at; i < end; ++i) {
                const float xl = L[i], xr = R[i];
                const float dl = det_.l.tick(xl), dr = det_.r.tick(xr);   // what the detector hears
                const float hl = xl - hi_.l.tick(xl), hr = xr - hi_.r.tick(xr);   // the sibilant top: the input minus its low part (they add back exactly)
                const float lvl = kit::gainToDb(std::max(std::fabs(dl), std::fabs(dr)) + 1e-9f);
                env_ = lvl + (lvl > env_ ? att : rel) * (env_ - lvl);
                const float t = thr_.next(p(kThr)), rg = rng_.next(p(kRange));
                const float want = std::min(rg, std::max(0.0f, (env_ - t) * 0.75f));   // 4:1, never deeper than RANGE
                gr_ = want + (want > gr_ ? att : rel) * (gr_ - want);
                grM = std::max(grM, gr_);
                const float g = kit::dbToGain(-gr_) - 1.0f;   // 0 when there is no cut: the input passes exactly
                float yl, yr;
                if (wide) { yl = xl * (1.0f + g); yr = xr * (1.0f + g); }
                else { yl = xl + g * hl; yr = xr + g * hr; }
                const float lw = lis_.next(lisT);
                if (lw > 1e-6f) { yl += lw * (dl - yl); yr += lw * (dr - yr); }
                pkI = std::max(pkI, std::max(std::fabs(xl), std::fabs(xr))); pkO = std::max(pkO, std::max(std::fabs(yl), std::fabs(yr)));
                L[i] = kit::sane(yl); R[i] = kit::sane(yr);
            }
        }
        pkI_ = pkI; pkO_ = pkO; grM_ = grM;
        publishMeter(0, kit::gainToDb(pkI_)); publishMeter(1, kit::gainToDb(pkO_)); publishMeter(2, -grM_);
        publishReduction(-grM_);
    }

private:
    void setFilters() noexcept {
        auto& g = gl_;
        const float v = std::log(p(kFreq));
        if (snap_) g.cur = v;
        const float f = std::exp(g.step(v));
        if (f == key_) return;
        key_ = f;
        const double ny = fs_ * 0.45;
        det_.set(hc::fx::BiquadCoefs::make(BT::BandPass, fs_, std::min<double>(f, ny), 1.4));
        hi_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(0.75f * f, ny), 0.7071));
    }
    double fs_ = 48000;
    hc::fx::Biquad2 det_, hi_;
    hc::fx::Smoothed thr_, rng_, lis_;
    kit::BlockGlide gl_;
    float env_ = 0, gr_ = 0, grM_ = 0, key_ = -1, pkI_ = 0, pkO_ = 0;
    bool snap_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeDeEsser() { return std::make_unique<DeEsser>(); }

} // namespace hem::staging
