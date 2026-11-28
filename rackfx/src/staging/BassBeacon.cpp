// =============================================================================
//  40 BASSBEACON — bass that reads on small speakers. SUB_GAIN lifts the low end around FREQ
//  (30..120 Hz); HARMONICS grows 2nd and 3rd harmonics out of that low band and adds them in the upper
//  bass (2..5 x FREQ), where small speakers can play them. TIGHT_HPF (OFF or 20..50 Hz) clears rumble
//  under the bass; MONO_BELOW (OFF or 60..200 Hz) sums everything under the cutoff to mono through a
//  Linkwitz-Riley crossover. MIX blends the added part, OUTPUT sets the level.
//  Different from SUBARCHITECT (03: sub synthesis for 808s) and IMPACTCROWN (35: a loudness chain).
//  Dead flat on load: SUB_GAIN 0, HARMONICS 0, TIGHT_HPF OFF, MONO_BELOW OFF, OUTPUT 0 dB.
//  ASSUMED: the lift is a bell of Q 1 at FREQ; HARMONICS 100 = harmonics about -12 dB under the low
//  band, 2nd and 3rd in about equal parts; TIGHT_HPF 24 dB/oct; MONO_BELOW at 24 dB/oct (when it is on,
//  the whole signal passes the crossover: same level, a slight phase turn around the cutoff).
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kSub, kFreq, kHarm, kTightOn, kTightF, kMonoOn, kMonoF, kMix, kOut, kBypass, kCount };
const ParamInfo kParams[kCount] = {
    {"sub_gain", "SUB GAIN", "dB", 0.0f, 12.0f, 0.0f},
    {"freq", "FREQ", "Hz", 30.0f, 120.0f, 60.0f, Taper::Log},
    {"harmonics", "HARMONICS", "%", 0.0f, 100.0f, 0.0f},
    {"tight_hpf_on", "TIGHT HPF", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"tight_hpf_freq", "TIGHT HPF FREQ", "Hz", 20.0f, 50.0f, 30.0f, Taper::Log},
    {"mono_below_on", "MONO BELOW", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"mono_below_freq", "MONO BELOW FREQ", "Hz", 60.0f, 200.0f, 100.0f, Taper::Log},
    {"mix", "MIX", "%", 0.0f, 100.0f, 100.0f},
    {"output", "OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"ORIGINAL BASS", "dB", -60, 0}, {"CREATED HARMONICS", "dB", -60, 0}, {"OUT", "dB", -60, 6}};
constexpr std::uint32_t kSubBlock = 8;

class BassBeacon final : public hc::fx::Effect {
public:
    BassBeacon() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kTightF) return p(kTightOn) > 0.5f;
        if (i == kMonoF) return p(kMonoOn) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        sub_.prepare(fs, 20.0, 0.0f); harm_.prepare(fs, 20.0, 0.0f); mix_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f);
        tight_.prepare(fs, 20.0, 0.0f); mono_.prepare(fs, 20.0, 0.0f);
        for (auto& g : gl_) g.prepare(fs, kSubBlock, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : ch_) { c.bp.clear(); c.lo.clear(); for (auto& f : c.hk) f.clear(); for (auto& f : c.tp) f.clear(); for (auto& f : c.mlp) f.clear(); for (auto& f : c.mhp) f.clear(); c.dc = 0; }
        key_ = -1; snap_ = true; loaded_ = false; bass_ = harmM_ = outPk_ = 0; bellOn_ = false; bellFade_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float sT = kit::dbToGain(p(kSub)) - 1.0f, hT = p(kHarm) * 0.01f, mT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOut));
        const float tT = p(kTightOn) > 0.5f ? 1.0f : 0.0f, moT = p(kMonoOn) > 0.5f ? 1.0f : 0.0f;
        if (!loaded_) { sub_.cur = sT; harm_.cur = hT; mix_.cur = mT; out_.cur = oT; tight_.cur = tT; mono_.cur = moT; loaded_ = true; }
        float bass = 0, harmM = 0, outPk = 0;
        for (std::uint32_t at = 0; at < n; at += kSubBlock) {
            setFilters();
            snap_ = false;
            const std::uint32_t end = std::min(n, at + kSubBlock);
            for (std::uint32_t i = at; i < end; ++i) {
                if (bellOn_ && bellFade_ < 1.0f) bellFade_ = std::min(1.0f, bellFade_ + static_cast<float>(1.0 / (0.02 * fs_ * 2.0)));
                const float s = sub_.next(sT), h = harm_.next(hT), m = mix_.next(mT), o = out_.next(oT), tw = tight_.next(tT), mw = mono_.next(moT);
                std::array<float, 2> x{L[i], R[i]};
                for (std::size_t c = 0; c < 2; ++c) {
                    Chan& k = ch_[c];
                    float v = x[c];
                    // TIGHT HPF (fades in / out over 20 ms; it always runs, so moving it never clicks)
                    const float hp = k.tp[1].tick(k.tp[0].tick(v));
                    v += tw * (hp - v);
                    const float lo = k.lo.tick(v);   // the low band the harmonics grow from
                    bass = std::max(bass, std::fabs(lo));
                    float add = bellOn_ ? (k.bp.tick(v) - v) * bellFade_ : 0.0f;   // SUB GAIN: a bell around FREQ (what it adds)
                    (void)s;
                    // HARMONICS: x^2 (2nd) and x^3 (3rd) of the low band, kept to 2..5 x FREQ (they grow with the bass level, like a speaker driven harder)
                    const float hl = (2.0f * lo * lo + 4.0f * lo * lo * lo);
                    k.dc += 0.0005f * (hl - k.dc);
                    const float hh = k.hk[1].tick(k.hk[0].tick(hl - k.dc));
                    add += h * 0.25f * hh;
                    harmM = std::max(harmM, std::fabs(h * 0.25f * hh));
                    x[c] = v + m * add;
                }
                // MONO BELOW: a Linkwitz-Riley split, the lows summed to mono, then put back together
                if (mw > 1e-6f || moT > 0.0f) {
                    std::array<float, 2> low{}, high{};
                    for (std::size_t c = 0; c < 2; ++c) {
                        Chan& k = ch_[c];
                        low[c] = k.mlp[1].tick(k.mlp[0].tick(x[c]));
                        high[c] = k.mhp[1].tick(k.mhp[0].tick(x[c]));
                    }
                    const float lm = 0.5f * (low[0] + low[1]);
                    for (std::size_t c = 0; c < 2; ++c) { const float y = lm + high[c]; x[c] += mw * (y - x[c]); }
                }
                for (std::size_t c = 0; c < 2; ++c) { const float y = x[c] * o; outPk = std::max(outPk, std::fabs(y)); (c == 0 ? L : R)[i] = kit::sane(y); }
            }
        }
        bass_ = bass; harmM_ = harmM; outPk_ = outPk;
        publishMeter(0, kit::gainToDb(bass_)); publishMeter(1, kit::gainToDb(harmM_)); publishMeter(2, kit::gainToDb(outPk_));
    }

private:
    struct Chan {
        hc::fx::Biquad bp, lo;
        std::array<hc::fx::Biquad, 2> hk{}, tp{}, mlp{}, mhp{};
        float dc = 0;
    };
    void setFilters() noexcept {
        auto glide = [this](std::size_t k, float t) { auto& g = gl_[k]; const float v = std::log(t); if (snap_) g.cur = v; return std::exp(g.step(v)); };
        const float f = glide(0, p(kFreq)), tf = glide(1, p(kTightF)), mf = glide(2, p(kMonoF));
        auto& gs = gl_[3];
        if (snap_) gs.cur = p(kSub);
        const float sg = gs.step(p(kSub));
        if (sg > 1e-4f) bellOn_ = true;
        const float key = f * 7.0f + tf * 1000.0f + mf * 0.37f + sg * 1.0e5f;
        if (key == key_) return;
        key_ = key;
        const auto bp = hc::fx::BiquadCoefs::make(BT::Bell, fs_, f, 1.0, sg);
        const auto lo = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, f * 1.5f, 0.7071);
        const auto h1 = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, f * 1.8f, 0.7071), h2 = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, f * 5.0f, 0.7071);
        const auto t0 = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, tf, 0.5412), t1 = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, tf, 1.3066);
        const auto ml = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, mf, 0.7071), mh = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, mf, 0.7071);
        for (auto& c : ch_) {
            c.bp.c = bp; c.lo.c = lo; c.hk[0].c = h1; c.hk[1].c = h2; c.tp[0].c = t0; c.tp[1].c = t1;
            c.mlp[0].c = ml; c.mlp[1].c = ml; c.mhp[0].c = mh; c.mhp[1].c = mh;
        }
    }
    double fs_ = 48000;
    std::array<Chan, 2> ch_{};
    hc::fx::Smoothed sub_, harm_, mix_, out_, tight_, mono_;
    std::array<kit::BlockGlide, 4> gl_{};
    bool bellOn_ = false;
    float bellFade_ = 0;
    float key_ = -1, bass_ = 0, harmM_ = 0, outPk_ = 0;
    bool snap_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeBassBeacon() { return std::make_unique<BassBeacon>(); }

} // namespace hem::staging
