// =============================================================================
//  18 SILKCONTOUR — broadband and midrange passive-style tone (two modules in series).
//  BROADBAND: LOW BOOST and LOW ATTEN on one LOW FREQ (set both and the curve lifts the very
//  bottom while dipping just above it, instead of cancelling), HIGH BOOST with BANDWIDTH, HIGH ATTEN
//  on its own ATTEN FREQ. MIDRANGE: LOW-MID BOOST, MID DIP, HIGH-MID BOOST on stepped frequencies.
//  Amounts run 0 .. 10 like the hardware it follows. Each module has EQ IN, a NOISE FILTER
//  (a notch for 50 / 60 Hz hum and its 2nd and 3rd harmonics), OUTPUT TRIM, VU and bypass.
//  Different from GRAPHICFORGE (17, proportional-Q faders) and PARAMETRIC EQ (46, clean true Q).
//  Dead flat on load: every amount 0, noise filters OFF, trims 0 dB.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P {
    kLowFreq, kLowBoost, kLowAtten, kHighBoost, kHighFreq, kBandwidth, kHighAtten, kAttenFreq,
    kBbIn, kBbNoise, kBbTrim, kBbReset, kBbBypass,
    kLmBoost, kLmFreq, kDip, kDipFreq, kHmBoost, kHmFreq,
    kMdIn, kMdNoise, kMdTrim, kMdReset, kMdBypass, kCount
};
const char* const kLowFreqN[] = {"20 HZ", "30 HZ", "60 HZ", "100 HZ"};
const char* const kHighFreqN[] = {"3 KHZ", "4 KHZ", "5 KHZ", "8 KHZ", "10 KHZ", "12 KHZ", "16 KHZ"};
const char* const kBwN[] = {"SHARP", "MEDIUM", "WIDE"};
const char* const kAttFreqN[] = {"5 KHZ", "10 KHZ", "20 KHZ"};
const char* const kNoiseN[] = {"OFF", "50 HZ", "60 HZ"};
const char* const kLmN[] = {"200 HZ", "300 HZ", "500 HZ", "700 HZ", "1000 HZ"};
const char* const kDipN[] = {"200 HZ", "300 HZ", "500 HZ", "700 HZ", "1 KHZ", "1.5 KHZ", "2 KHZ", "3 KHZ", "4 KHZ", "5 KHZ", "7 KHZ"};
const char* const kHmN[] = {"1.5 KHZ", "2 KHZ", "3 KHZ", "4 KHZ", "5 KHZ"};
constexpr std::array<float, 4> kLowHz{20, 30, 60, 100};
constexpr std::array<float, 7> kHighHz{3000, 4000, 5000, 8000, 10000, 12000, 16000};
constexpr std::array<float, 3> kAttHz{5000, 10000, 20000};
constexpr std::array<float, 5> kLmHz{200, 300, 500, 700, 1000};
constexpr std::array<float, 11> kDipHz{200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000};
constexpr std::array<float, 5> kHmHz{1500, 2000, 3000, 4000, 5000};

const ParamInfo kParams[kCount] = {
    {"low_freq", "LOW FREQ", "", 0.0f, 3.0f, 2.0f, Taper::Choice, kLowFreqN},
    {"low_boost", "LOW BOOST", "", 0.0f, 10.0f, 0.0f},
    {"low_atten", "LOW ATTEN", "", 0.0f, 10.0f, 0.0f},
    {"high_boost", "HIGH BOOST", "", 0.0f, 10.0f, 0.0f},
    {"high_freq", "HIGH FREQ", "", 0.0f, 6.0f, 4.0f, Taper::Choice, kHighFreqN},
    {"bandwidth", "BANDWIDTH", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kBwN},
    {"high_atten", "HIGH ATTEN", "", 0.0f, 10.0f, 0.0f},
    {"atten_freq", "ATTEN FREQ", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kAttFreqN},
    {"bb_eq_in", "BROADBAND EQ IN", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"bb_noise", "BROADBAND NOISE FILTER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kNoiseN},
    {"bb_trim", "BROADBAND OUTPUT TRIM", "dB", -18.0f, 18.0f, 0.0f},
    {"bb_clip_reset", "BROADBAND CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bb_bypass", "BROADBAND BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"lm_boost", "LOW-MID BOOST", "", 0.0f, 10.0f, 0.0f},
    {"lm_freq", "LOW-MID FREQ", "", 0.0f, 4.0f, 2.0f, Taper::Choice, kLmN},
    {"mid_dip", "MID DIP", "", 0.0f, 10.0f, 0.0f},
    {"dip_freq", "MID DIP FREQ", "", 0.0f, 10.0f, 4.0f, Taper::Choice, kDipN},
    {"hm_boost", "HIGH-MID BOOST", "", 0.0f, 10.0f, 0.0f},
    {"hm_freq", "HIGH-MID FREQ", "", 0.0f, 4.0f, 2.0f, Taper::Choice, kHmN},
    {"md_eq_in", "MIDRANGE EQ IN", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"md_noise", "MIDRANGE NOISE FILTER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kNoiseN},
    {"md_trim", "MIDRANGE OUTPUT TRIM", "dB", -18.0f, 18.0f, 0.0f},
    {"md_clip_reset", "MIDRANGE CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"md_bypass", "MIDRANGE BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"BROADBAND VU", "dB", -18, 18, true}, {"MIDRANGE VU", "dB", -18, 18, true}, {"BROADBAND CLIP", "", 0, 1}, {"MIDRANGE CLIP", "", 0, 1},
};

// one EQ section: up to 4 filters whose gains glide; a section that was never used costs nothing
struct Section {
    std::array<hc::fx::Biquad2, 4> f{};
    std::array<kit::BlockGlide, 4> g{};
    std::array<bool, 4> on{};
    std::array<float, 4> key{};
    void prepare(double fs) { for (auto& x : g) x.prepare(fs, 32, 30.0, 0.0f); }
    void clear() { for (auto& x : f) x.clear(); on.fill(false); key.fill(1e9f); }
};

class SilkContour final : public hc::fx::Effect {
public:
    SilkContour() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs; bb_.prepare(fs); md_.prepare(fs);
        bbTrim_.prepare(fs, 20.0, 1.0f); mdTrim_.prepare(fs, 20.0, 1.0f);
        reset();
    }
    void reset() noexcept override {
        bb_.clear(); md_.clear();
        for (auto& n : notchBb_) n.clear();
        for (auto& n : notchMd_) n.clear();
        vu_.fill(0); clip_.fill(false); noiseKey_.fill(-1);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBbReset) > 0.5f) clip_[0] = false;
        if (p(kMdReset) > 0.5f) clip_[1] = false;
        constexpr std::uint32_t kSub = 32;
        for (std::uint32_t at = 0; at < n; at += kSub) {
            const std::uint32_t m = std::min(kSub, n - at);
            float* l = L + at; float* r = R + at;
            if (p(kBbBypass) < 0.5f) {
                const bool in = p(kBbIn) > 0.5f;
                const float lf = kLowHz[static_cast<std::size_t>(p(kLowFreq))];
                static constexpr std::array<float, 3> kBwQ{2.5f, 1.2f, 0.6f};
                // amounts 0..10 -> dB: low boost +13.5, low atten -17.5, high boost +16, high atten -16 at 10
                band(bb_, 0, l, r, m, BT::LowShelf, lf, 0.55f, in ? p(kLowBoost) * 1.35f : 0.0f);
                // the passive network's interaction (ASSUMED shape): with BOOST also up, the ATTEN corner moves up to 2.5 x LOW FREQ
                // and its depth eases by up to 45 %, so boost + atten lift the bottom and dip just above it instead of cancelling
                const float bAmt = p(kLowBoost) * 0.1f;
                const float attDb = -p(kLowAtten) * 1.75f * (1.0f - 0.45f * bAmt);
                band(bb_, 1, l, r, m, BT::LowShelf, lf * (1.0f + 1.5f * bAmt), 0.6f, in ? attDb : 0.0f);
                band(bb_, 2, l, r, m, BT::Bell, kHighHz[static_cast<std::size_t>(p(kHighFreq))], kBwQ[static_cast<std::size_t>(p(kBandwidth))], in ? p(kHighBoost) * 1.6f : 0.0f);
                band(bb_, 3, l, r, m, BT::HighShelf, kAttHz[static_cast<std::size_t>(p(kAttenFreq))], 0.7f, in ? -p(kHighAtten) * 1.6f : 0.0f);
                noise(notchBb_, 0, static_cast<int>(p(kBbNoise)), l, r, m);
                trim(bbTrim_, p(kBbTrim), l, r, m);
                vu(0, l, r, m);
            }
            if (p(kMdBypass) < 0.5f) {
                const bool in = p(kMdIn) > 0.5f;
                band(md_, 0, l, r, m, BT::Bell, kLmHz[static_cast<std::size_t>(p(kLmFreq))], 1.0f, in ? p(kLmBoost) : 0.0f);          // +10 dB at 10
                band(md_, 1, l, r, m, BT::Bell, kDipHz[static_cast<std::size_t>(p(kDipFreq))], 1.2f, in ? -p(kDip) : 0.0f);           // -10 dB at 10
                band(md_, 2, l, r, m, BT::Bell, kHmHz[static_cast<std::size_t>(p(kHmFreq))], 1.0f, in ? p(kHmBoost) : 0.0f);          // +10 dB at 10
                noise(notchMd_, 1, static_cast<int>(p(kMdNoise)), l, r, m);
                trim(mdTrim_, p(kMdTrim), l, r, m);
                vu(1, l, r, m);
            }
            for (std::uint32_t i = 0; i < m; ++i) { l[i] = kit::sane(l[i]); r[i] = kit::sane(r[i]); }
        }
        publishMeter(0, vuDb(0)); publishMeter(1, vuDb(1));
        publishMeter(2, clip_[0] ? 1.0f : 0.0f); publishMeter(3, clip_[1] ? 1.0f : 0.0f);
    }

private:
    void band(Section& s, int k, float* l, float* r, std::uint32_t m, BT type, float hz, float q, float targetDb) noexcept {
        const auto ki = static_cast<std::size_t>(k);
        const float g = s.g[ki].step(targetDb);
        if (kit::isFlat(g) && !s.on[ki]) return;
        s.on[ki] = true;
        const float key = g * 1000.0f + hz + q * 0.001f;
        if (s.key[ki] != key) { s.key[ki] = key; s.f[ki].set(hc::fx::BiquadCoefs::make(type, fs_, hz, q, g)); }
        s.f[ki].run(l, r, m);
    }
    // hum notches at the mains frequency, x2 and x3
    void noise(std::array<hc::fx::Biquad2, 3>& nt, int which, int mode, float* l, float* r, std::uint32_t m) noexcept {
        if (mode == 0) return;
        const float base = mode == 1 ? 50.0f : 60.0f;
        if (noiseKey_[static_cast<std::size_t>(which)] != base) {
            noiseKey_[static_cast<std::size_t>(which)] = base;
            for (int h = 0; h < 3; ++h) nt[static_cast<std::size_t>(h)].set(hc::fx::BiquadCoefs::make(BT::Notch, fs_, base * static_cast<float>(h + 1), 8.0));
        }
        for (auto& f : nt) f.run(l, r, m);
    }
    static void trim(hc::fx::Smoothed& s, float db, float* l, float* r, std::uint32_t m) noexcept {
        const float t = kit::dbToGain(db);
        if (kit::isFlat(t, 1.0f) && kit::isFlat(s.cur, 1.0f)) return;
        for (std::uint32_t i = 0; i < m; ++i) { const float g = s.next(t); l[i] *= g; r[i] *= g; }
    }
    // VU: 300 ms average, 0 VU = -18 dBFS
    void vu(int k, const float* l, const float* r, std::uint32_t m) noexcept {
        const auto ki = static_cast<std::size_t>(k);
        const float c = hc::fx::onePoleCoef(fs_, 300.0);
        for (std::uint32_t i = 0; i < m; ++i) {
            const float a = 0.5f * (std::fabs(l[i]) + std::fabs(r[i]));
            vu_[ki] = a + c * (vu_[ki] - a);
            if (std::fabs(l[i]) >= 1.0f || std::fabs(r[i]) >= 1.0f) clip_[ki] = true;
        }
    }
    [[nodiscard]] float vuDb(int k) const noexcept { return std::clamp(kit::gainToDb(vu_[static_cast<std::size_t>(k)] * 1.11f) + 18.0f, -18.0f, 18.0f); }

    double fs_ = 48000;
    Section bb_, md_;
    std::array<hc::fx::Biquad2, 3> notchBb_{}, notchMd_{};
    std::array<float, 2> noiseKey_{}, vu_{};
    std::array<bool, 2> clip_{};
    hc::fx::Smoothed bbTrim_, mdTrim_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeSilkContour() { return std::make_unique<SilkContour>(); }

} // namespace hem::staging
