// =============================================================================
//  16 BANDMOTION — the precise dynamic EQ: six floating bands placed anywhere (they may overlap)
//  plus HP / LP, each band in STEREO, MID or SIDE, triggered by its own band (SPLIT) or the full
//  signal (WIDE), from the input (INTERNAL) or a sidechain (EXTERNAL); per-band and filter SOLO.
//  RANGE moves the band once its level passes THRESHOLD: one dB per dB over (a 2:1 move),
//  up to RANGE; RANGE's sign picks cut or boost. AUTO release lengthens the release on long
//  material (program-dependent, ASSUMED 1x .. 3x).
//  Different from DYNAMIC EQ 8 (01): floating bands, mid / side per band, sidechain, solos.
//  Dead flat on load: every gain and range 0 dB, HP / LP off, mix 100 %, output 0 dB.
//  Analyzer display options are faceplate settings, not part of the sound.
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

constexpr int kBands = 6, kPer = 13;
enum Band { bOn, bChan, bType, bFreq, bQ, bGain, bRange, bThr, bAtt, bRel, bSource, bDetect, bSolo };
constexpr int kHp = kBands * kPer, kFiltPer = 5;   // on, freq, slope, chan, solo
constexpr int kLp = kHp + kFiltPer;
enum Global { kRelMode = kLp + kFiltPer, kMix, kOutput, kBypass, kCount };
const char* const kChanN[] = {"STEREO", "MID", "SIDE"};
const char* const kTypeN[] = {"BELL", "LOW SHELF", "HIGH SHELF"};
const char* const kSrcN[] = {"INTERNAL", "EXTERNAL"};
const char* const kDetN[] = {"SPLIT", "WIDE"};
const char* const kSlopeN[] = {"6 DB", "12 DB", "18 DB", "24 DB"};
const char* const kRelN[] = {"AUTO", "MANUAL"};
constexpr std::array<float, kBands> kDefHz{100, 300, 800, 2500, 6000, 12000};

#define HEM_BAND(n) \
    {"b" #n "_on", "BAND " #n " ENABLE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle}, \
    {"b" #n "_channel", "BAND " #n " CHANNEL", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kChanN}, \
    {"b" #n "_type", "BAND " #n " TYPE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kTypeN}, \
    {"b" #n "_freq", "BAND " #n " FREQUENCY", "Hz", 16.0f, 21000.0f, kDefHz[n - 1], Taper::Log}, \
    {"b" #n "_q", "BAND " #n " Q", "", 0.4f, 60.0f, 1.0f, Taper::Log}, \
    {"b" #n "_gain", "BAND " #n " GAIN", "dB", -18.0f, 18.0f, 0.0f}, \
    {"b" #n "_range", "BAND " #n " RANGE", "dB", -18.0f, 18.0f, 0.0f}, \
    {"b" #n "_thr", "BAND " #n " THRESHOLD", "dB", -60.0f, 0.0f, 0.0f}, \
    {"b" #n "_att", "BAND " #n " ATTACK", "ms", 0.5f, 500.0f, 10.0f, Taper::Log}, \
    {"b" #n "_rel", "BAND " #n " RELEASE", "ms", 5.0f, 5000.0f, 120.0f, Taper::Log}, \
    {"b" #n "_source", "BAND " #n " SC SOURCE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kSrcN}, \
    {"b" #n "_detector", "BAND " #n " DETECTOR", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kDetN}, \
    {"b" #n "_solo", "BAND " #n " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}
#define HEM_FILT(id, nm, hz) \
    {id "_on", nm " ENABLE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {id "_freq", nm " FREQUENCY", "Hz", 16.0f, 21000.0f, hz, Taper::Log}, \
    {id "_slope", nm " SLOPE", "", 0.0f, 3.0f, 1.0f, Taper::Choice, kSlopeN}, \
    {id "_channel", nm " CHANNEL", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kChanN}, \
    {id "_solo", nm " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}

const ParamInfo kParams[kCount] = {
    HEM_BAND(1), HEM_BAND(2), HEM_BAND(3), HEM_BAND(4), HEM_BAND(5), HEM_BAND(6),
    HEM_FILT("hp", "HP", 30.0f), HEM_FILT("lp", "LP", 18000.0f),
    {"release_mode", "RELEASE MODE", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kRelN},
    {"mix", "MIX", "%", 0.0f, 100.0f, 100.0f},
    {"output", "OUTPUT", "dB", -18.0f, 18.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_BAND
#undef HEM_FILT

const hc::fx::MeterInfo kMeters[] = {
    {"OUTPUT", "dBFS", -60, 0},
    {"BAND 1", "dB", -18, 18, true}, {"BAND 2", "dB", -18, 18, true}, {"BAND 3", "dB", -18, 18, true},
    {"BAND 4", "dB", -18, 18, true}, {"BAND 5", "dB", -18, 18, true}, {"BAND 6", "dB", -18, 18, true}, {"SC INPUT", "dBFS", -60, 0},
};

class BandMotion final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    BandMotion() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i < kHp) { const int k = i % kPer; return k == bOn || p(i - k + bOn) > 0.5f; }
        if (i < kLp) return i == kHp || p(kHp) > 0.5f;
        if (i < kRelMode) return i == kLp || p(kLp) > 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        dryL_.assign(std::max<std::uint32_t>(maxBlock, kMaxBlock), 0.0f); dryR_.assign(dryL_.size(), 0.0f);
        for (int b = 0; b < kBands; ++b) bands_[static_cast<std::size_t>(b)].gain.prepare(fs, 32, 20.0, p(b * kPer + bGain));
        mixG_.prepare(fs, 20.0, p(kMix) * 0.01f); outG_.prepare(fs, 20.0, kit::dbToGain(p(kOutput)));
        outM_.prepare(fs); scM_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& b : bands_) { b.eq.clear(); b.det.clear(); b.solo.clear(); b.env = 0; b.dyn = 0; b.active = false; b.key = {}; b.slowEnv = 0; }
        for (auto& f : hp_) f.clear();
        for (auto& f : lp_) f.clear();
        hpKey_ = lpKey_ = -1.0f;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        std::copy(L, L + n, dryL_.data()); std::copy(R, R + n, dryR_.data());
        if (scL_) scM_.feed(scL_, n);
        bool anySolo = false;
        for (int b = 0; b < kBands; ++b) anySolo |= p(b * kPer + bOn) > 0.5f && p(b * kPer + bSolo) > 0.5f;
        anySolo |= (p(kHp) > 0.5f && p(kHp + 4) > 0.5f) || (p(kLp) > 0.5f && p(kLp + 4) > 0.5f);
        constexpr std::uint32_t kSub = 32;
        for (std::uint32_t at = 0; at < n; at += kSub) {
            const std::uint32_t m = std::min(kSub, n - at);
            if (anySolo) { soloBlock(L + at, R + at, m, at); continue; }
            filters(L + at, R + at, m);
            for (int b = 0; b < kBands; ++b) band(b, L + at, R + at, m, at);
        }
        const float mixT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOutput));
        const bool flat = kit::isFlat(mixT, 1.0f) && kit::isFlat(mixG_.cur, 1.0f) && kit::isFlat(oT, 1.0f) && kit::isFlat(outG_.cur, 1.0f);
        if (!flat)
            for (std::uint32_t i = 0; i < n; ++i) {
                const float mx = mixG_.next(mixT), o = outG_.next(oT);
                L[i] = (dryL_[i] + mx * (L[i] - dryL_[i])) * o; R[i] = (dryR_[i] + mx * (R[i] - dryR_[i])) * o;
            }
        for (std::uint32_t i = 0; i < n; ++i) { L[i] = kit::sane(L[i]); R[i] = kit::sane(R[i]); }
        outM_.feed(L, n);
        publishMeter(0, outM_.db());
        float most = 0;
        for (int b = 0; b < kBands; ++b) { publishMeter(1 + b, bands_[static_cast<std::size_t>(b)].dyn); most = std::min(most, bands_[static_cast<std::size_t>(b)].dyn); }
        publishMeter(7, scL_ ? scM_.db() : -120.0f);
        publishReduction(most);
        scL_ = scR_ = nullptr;
    }

private:
    struct Key { int type = -1; float hz = -1, q = -1, db = 1e9f; bool operator==(const Key&) const = default; };
    struct BandState { hc::fx::Biquad2 eq; kit::Svf det; hc::fx::Biquad2 solo; kit::BlockGlide gain; float env = 0, slowEnv = 0, dyn = 0; bool active = false; Key key{}; };

    // applies a 2-channel filter to L/R, the mid or the side of the block
    template <class F> static void onChannel(int chan, float* l, float* r, std::uint32_t m, F&& run) noexcept {
        if (chan == 0) { run(l, r, m); return; }
        std::array<float, 32> a{}, z{};
        for (std::uint32_t i = 0; i < m; ++i) a[i] = chan == 1 ? 0.5f * (l[i] + r[i]) : 0.5f * (l[i] - r[i]);
        run(a.data(), z.data(), m);   // second lane unused (zeros through the right filter)
        for (std::uint32_t i = 0; i < m; ++i) {
            const float mid = chan == 1 ? a[i] : 0.5f * (l[i] + r[i]), side = chan == 2 ? a[i] : 0.5f * (l[i] - r[i]);
            l[i] = mid + side; r[i] = mid - side;
        }
    }

    void filters(float* l, float* r, std::uint32_t m) noexcept {
        for (int f = 0; f < 2; ++f) {
            const int o = f == 0 ? kHp : kLp;
            if (p(o) < 0.5f) continue;
            auto& st = f == 0 ? hp_ : lp_;
            float& key = f == 0 ? hpKey_ : lpKey_;
            const int slope = static_cast<int>(p(o + 2));   // 6 / 12 / 18 / 24 dB per octave
            const float k = p(o + 1) * 10.0f + static_cast<float>(slope);
            if (key != k) {
                key = k;
                const BT t = f == 0 ? BT::HighPass : BT::LowPass;
                const double hz = p(o + 1);
                if (slope == 0 || slope == 2) {   // odd orders: a first-order section (bilinear one-pole)
                    const double c = std::tan(std::numbers::pi * std::clamp(hz, 5.0, fs_ * 0.49) / fs_);
                    hc::fx::BiquadCoefs fc{};
                    if (f == 0) { fc.b0 = 1.0 / (1.0 + c); fc.b1 = -fc.b0; fc.a1 = (c - 1.0) / (c + 1.0); }
                    else { fc.b0 = c / (1.0 + c); fc.b1 = fc.b0; fc.a1 = (c - 1.0) / (c + 1.0); }
                    st[0].set(fc);
                    st[1].set(hc::fx::BiquadCoefs::make(t, fs_, hz, 0.7071));   // used by 18 dB
                } else {
                    st[0].set(hc::fx::BiquadCoefs::make(t, fs_, hz, slope == 3 ? 0.5412 : 0.7071));
                    st[1].set(hc::fx::BiquadCoefs::make(t, fs_, hz, 1.3066));   // used by 24 dB (Butterworth pair)
                }
            }
            const int stages = slope == 0 ? 1 : slope == 1 ? 1 : 2;
            onChannel(static_cast<int>(p(o + 3)), l, r, m, [&](float* a, float* b, std::uint32_t mm) { for (int s = 0; s < stages; ++s) st[static_cast<std::size_t>(s)].run(a, b, mm); });
        }
    }

    void band(int b, float* l, float* r, std::uint32_t m, std::uint32_t at) noexcept {
        BandState& bs = bands_[static_cast<std::size_t>(b)];
        const int o = b * kPer;
        if (p(o + bOn) < 0.5f) { if (bs.active) { bs.eq.clear(); bs.active = false; bs.key = {}; } bs.dyn = 0; return; }
        const int type = static_cast<int>(p(o + bType)), chan = static_cast<int>(p(o + bChan));
        const float hz = p(o + bFreq), q = p(o + bQ), gain = bs.gain.step(p(o + bGain)), range = p(o + bRange);
        // detector: this band's range (SPLIT) or everything (WIDE), from the input or the sidechain
        const bool ext = p(o + bSource) > 0.5f && scL_;
        const bool split = p(o + bDetect) < 0.5f;
        bs.det.set(fs_, hz, std::max(0.5f, q));
        float relMs = p(o + bRel);
        const float att = hc::fx::onePoleCoef(fs_, p(o + bAtt));
        if (p(kRelMode) < 0.5f) relMs *= 1.0f + 2.0f * std::clamp(bs.slowEnv * 8.0f, 0.0f, 1.0f);   // AUTO: sustained material, longer release
        const float rel = hc::fx::onePoleCoef(fs_, relMs), slow = hc::fx::onePoleCoef(fs_, 500.0);
        for (std::uint32_t i = 0; i < m; ++i) {
            float x = ext ? 0.5f * (scL_[at + i] + scR_[at + i]) : chan == 2 ? 0.5f * (l[i] - r[i]) : 0.5f * (l[i] + r[i]);
            if (split) x = type == 1 ? bs.det.low(x) : type == 2 ? bs.det.high(x) : bs.det.band(x);
            const float a = std::fabs(x);
            bs.env = a + (a > bs.env ? att : rel) * (bs.env - a);
            bs.slowEnv = a + slow * (bs.slowEnv - a);
        }
        float dyn = 0.0f;
        if (!kit::isFlat(range)) {
            const float over = kit::gainToDb(bs.env * 1.4142f) - p(o + bThr);
            if (over > 0.0f) dyn = std::copysign(std::min(std::fabs(range), over * 0.5f), range);
        }
        bs.dyn = dyn;
        if (!bs.active && kit::isFlat(gain) && kit::isFlat(dyn)) return;   // never used: skip (once used it runs; exact at 0 dB)
        bs.active = true;
        const Key key{type, hz, q, gain + dyn};
        if (!(key == bs.key)) {
            bs.key = key;
            const BT t = type == 1 ? BT::LowShelf : type == 2 ? BT::HighShelf : BT::Bell;
            bs.eq.set(hc::fx::BiquadCoefs::make(t, fs_, hz, type == 0 ? q : std::min(q, 2.0f), gain + dyn));
        }
        onChannel(chan, l, r, m, [&](float* a, float* bb, std::uint32_t mm) { bs.eq.run(a, bb, mm); });
    }

    // SOLO: hear only what the soloed bands / filters cover
    void soloBlock(float* l, float* r, std::uint32_t m, std::uint32_t at) noexcept {
        (void)at;
        std::array<float, 32> sl{}, sr{};
        for (int b = 0; b < kBands; ++b) {
            const int o = b * kPer;
            if (p(o + bOn) < 0.5f || p(o + bSolo) < 0.5f) continue;
            BandState& bs = bands_[static_cast<std::size_t>(b)];
            bs.solo.set(hc::fx::BiquadCoefs::make(BT::BandPass, fs_, p(o + bFreq), p(o + bQ)));
            std::array<float, 32> a{}, c{};
            std::copy(l, l + m, a.begin()); std::copy(r, r + m, c.begin());
            bs.solo.run(a.data(), c.data(), m);
            for (std::uint32_t i = 0; i < m; ++i) { sl[i] += a[i]; sr[i] += c[i]; }
        }
        for (int f = 0; f < 2; ++f) {
            const int o = f == 0 ? kHp : kLp;
            if (p(o) < 0.5f || p(o + 4) < 0.5f) continue;
            // a filter's solo plays what it removes
            auto& st = f == 0 ? soloHp_ : soloLp_;
            st.set(hc::fx::BiquadCoefs::make(f == 0 ? BT::LowPass : BT::HighPass, fs_, p(o + 1), 0.7071));
            std::array<float, 32> a{}, c{};
            std::copy(l, l + m, a.begin()); std::copy(r, r + m, c.begin());
            st.run(a.data(), c.data(), m);
            for (std::uint32_t i = 0; i < m; ++i) { sl[i] += a[i]; sr[i] += c[i]; }
        }
        std::copy(sl.begin(), sl.begin() + m, l); std::copy(sr.begin(), sr.begin() + m, r);
    }

    double fs_ = 48000;
    std::array<BandState, kBands> bands_{};
    std::array<hc::fx::Biquad2, 2> hp_{}, lp_{};
    hc::fx::Biquad2 soloHp_, soloLp_;
    float hpKey_ = -1, lpKey_ = -1;
    std::vector<float> dryL_, dryR_;
    hc::fx::Smoothed mixG_, outG_;
    kit::PeakMeter outM_, scM_;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeBandMotion() { return std::make_unique<BandMotion>(); }

} // namespace hem::staging
