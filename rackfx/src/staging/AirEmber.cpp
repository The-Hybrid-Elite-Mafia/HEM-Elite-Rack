// =============================================================================
//  39 AIREMBER — sparkle and clarity: an AIR shelf (AIR_GAIN 0..12 dB from AIR_FREQ 8..20 kHz) plus an
//  EMBER exciter that grows gentle even harmonics out of the upper band (EMBER_DRIVE). SOFTNESS rolls
//  the added top off above 16 kHz so it never turns harsh. Both are added alongside the untouched
//  signal, so MIX blends the added part and nothing else moves.
//  Different from IMPACTCROWN (35: a whole loudness chain) and SILKCONTOUR (18: passive EQ curves).
//  Dead flat on load: AIR_GAIN 0, EMBER_DRIVE 0 (the output is the input, bit for bit), OUTPUT 0 dB.
//  ASSUMED: the exciter listens above 0.4 x AIR_FREQ (at least 2 kHz) and keeps harmonics above
//  0.5 x AIR_FREQ; EMBER_DRIVE 100 = harmonics about -20 dB under the band; SOFTNESS 0..100 moves a
//  low-pass on the added part from 22 kHz down to 16 kHz.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kAirGain, kAirFreq, kDrive, kSoft, kMix, kOut, kBypass, kCount };
const ParamInfo kParams[kCount] = {
    {"air_gain", "AIR GAIN", "dB", 0.0f, 12.0f, 0.0f},
    {"air_freq", "AIR FREQ", "Hz", 8000.0f, 20000.0f, 12000.0f, Taper::Log},
    {"ember_drive", "EMBER DRIVE", "%", 0.0f, 100.0f, 0.0f},
    {"softness", "SOFTNESS", "%", 0.0f, 100.0f, 50.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 100.0f},
    {"output", "OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 6}, {"OUT", "dB", -60, 6}, {"AIR ADDED", "dB", -60, 0}};
constexpr std::uint32_t kSub = 8;

class AirEmber final : public hc::fx::Effect {
public:
    AirEmber() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        gain_.prepare(fs, 20.0, 0.0f); drive_.prepare(fs, 20.0, 0.0f); mix_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f);
        for (auto& g : gl_) g.prepare(fs, kSub, 30.0, 0.0f);
        reset();
    }
    void reset() noexcept override { for (auto* f : {&shelf_, &band_, &keep_, &soft_}) f->clear(); key_ = -1; snap_ = true; loaded_ = false; pkI_ = pkO_ = add_ = 0; shelfOn_ = false; shelfFade_ = 0; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float gT = kit::dbToGain(p(kAirGain)) - 1.0f, dT = p(kDrive) * 0.01f, mT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOut));
        if (!loaded_) { gain_.cur = gT; drive_.cur = dT; mix_.cur = mT; out_.cur = oT; loaded_ = true; }
        float pkI = 0, pkO = 0, add = 0;
        float* io[2] = {L, R};
        for (std::uint32_t at = 0; at < n; at += kSub) {
            setFilters();
            snap_ = false;
            const std::uint32_t end = std::min(n, at + kSub);
            for (std::uint32_t i = at; i < end; ++i) {
                const float g = gain_.next(gT), d = drive_.next(dT), m = mix_.next(mT), o = out_.next(oT);
                (void)g;
                if (shelfOn_ && shelfFade_ < 1.0f) shelfFade_ = std::min(1.0f, shelfFade_ + static_cast<float>(1.0 / (0.02 * fs_)));
                for (std::size_t c = 0; c < 2; ++c) {
                    const float x = io[c][i];
                    pkI = std::max(pkI, std::fabs(x));
                    // the filters always run (cheap), so switching the air or the ember on never clicks
                    // AIR: a high shelf; the part it adds (shelf - input) joins the exciter's part
                    const float hs = shelfOn_ ? (c == 0 ? shelf_.l.tick(x) : shelf_.r.tick(x)) - x : 0.0f;
                    const float b = c == 0 ? band_.l.tick(x) : band_.r.tick(x);      // what the exciter listens to
                    const float h = c == 0 ? keep_.l.tick(b * b) : keep_.r.tick(b * b);   // even harmonics, DC and lows removed
                    float e = hs * shelfFade_ + d * 2.0f * h;
                    e = c == 0 ? soft_.l.tick(e) : soft_.r.tick(e);
                    float y = x;
                    y = x + m * e;   // e is exactly 0 with AIR 0 and EMBER 0: the input passes bit for bit
                    y *= o;
                    add = std::max(add, std::fabs(e));
                    pkO = std::max(pkO, std::fabs(y));
                    io[c][i] = kit::sane(y);
                }
            }
        }
        pkI_ = pkI; pkO_ = pkO; add_ = add;
        publishMeter(0, kit::gainToDb(pkI_)); publishMeter(1, kit::gainToDb(pkO_)); publishMeter(2, kit::gainToDb(add_));
    }

private:
    void setFilters() noexcept {
        auto glide = [this](std::size_t k, float t) { auto& g = gl_[k]; const float v = std::log(t); if (snap_) g.cur = v; return std::exp(g.step(v)); };
        const float f = glide(0, p(kAirFreq)), soft = glide(1, 22000.0f - 60.0f * p(kSoft));
        auto& ga = gl_[2];
        if (snap_) ga.cur = p(kAirGain);
        const float air = ga.step(p(kAirGain));
        if (air > 1e-4f) shelfOn_ = true;   // once used it keeps running (a 0 dB shelf passes the input)
        const float key = f * 7.0f + soft + air * 1.0e5f;
        if (key == key_) return;
        key_ = key;
        const double ny = fs_ * 0.45;
        shelf_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min<double>(f, ny), 0.7071, air));
        band_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, std::min<double>(std::max(2000.0f, 0.4f * f), ny), 0.7071));
        keep_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, std::min<double>(0.5f * f, ny), 0.7071));
        soft_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(soft, ny), 0.7071));
    }
    double fs_ = 48000;
    hc::fx::Biquad2 shelf_, band_, keep_, soft_;
    hc::fx::Smoothed gain_, drive_, mix_, out_;
    std::array<kit::BlockGlide, 3> gl_{};
    bool shelfOn_ = false;
    float shelfFade_ = 0;
    float key_ = -1, pkI_ = 0, pkO_ = 0, add_ = 0;
    bool snap_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeAirEmber() { return std::make_unique<AirEmber>(); }

} // namespace hem::staging
