// =============================================================================
//  22 BANDCOUNCIL — six-band dynamics: four bands split by LOW / MID / HIGH crossovers (2..5),
//  plus two floating bands (FREE 1, FREE 6) placed anywhere with FREQ and Q. Each band:
//  THRESHOLD, GAIN (static), RANGE (minus = compress down by up to RANGE, plus = expand / lift by up
//  to RANGE, one dB per two dB over threshold), ATTACK, RELEASE, BYPASS, SOLO, LISTEN (hear what the
//  band's detector hears), SC (INT / EXT) and DETECTOR (FULL RANGE / BAND ONLY).
//  MASTER offsets every band's threshold, gain and range, scales attack / release (10..1000 %),
//  RELEASE MODE AUTO lengthens release on sustained material, BEHAVIOR SMOOTH (opto-like, slower and
//  program-dependent) or FAST (electro-like), KNEE SOFT / HARD.
//  The crossover is a Linkwitz-Riley 24 dB/oct tree with all-pass phase matching: the four bands add
//  back to a flat (all-pass) whole. When nothing is set (every band neutral, no solo / listen) the
//  split is skipped and the input passes exactly; it fades in over 20 ms once a band is used.
//  Different from BANDMOTION (16): crossover bands that tile the spectrum, not floating EQ bells.
//  Dead flat on load: every threshold, gain and range 0 dB, master offsets 0, output 0 dB.
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

constexpr int kBands = 6, kPer = 12;   // band order: FREE 1, BAND 2, BAND 3, BAND 4, BAND 5, FREE 6
enum B { bThr, bGain, bRange, bAtt, bRel, bBypass, bSolo, bSc, bListen, bDetect, bFreq, bQ };
enum G { kLowX = kBands * kPer, kMidX, kHighX, kXQ, kMThr, kMGain, kMRange, kMAtt, kMRel, kRelMode, kBehavior, kKnee, kOutput, kBypass, kCount };
const char* const kScN[] = {"INT", "EXT"};
const char* const kDetN[] = {"FULL RANGE", "BAND ONLY"};
const char* const kRelN[] = {"AUTO", "MANUAL"};
const char* const kBehN[] = {"SMOOTH", "FAST"};
const char* const kKneeN[] = {"SOFT", "HARD"};

#define HEM_BAND(id, nm, hz) \
    {id "_thr", nm " THRESHOLD", "dB", -60.0f, 0.0f, 0.0f}, \
    {id "_gain", nm " GAIN", "dB", -18.0f, 18.0f, 0.0f}, \
    {id "_range", nm " RANGE", "dB", -24.0f, 24.0f, 0.0f}, \
    {id "_att", nm " ATTACK", "ms", 0.5f, 500.0f, 10.0f, Taper::Log}, \
    {id "_rel", nm " RELEASE", "ms", 5.0f, 5000.0f, 120.0f, Taper::Log}, \
    {id "_bypass", nm " BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {id "_solo", nm " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {id "_sc", nm " SC", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kScN}, \
    {id "_listen", nm " LISTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {id "_detector", nm " DETECTOR", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kDetN}, \
    {id "_freq", nm " FREQ", "Hz", 20.0f, 20000.0f, hz, Taper::Log}, \
    {id "_q", nm " Q", "", 0.3f, 10.0f, 1.0f, Taper::Log}

const ParamInfo kParams[kCount] = {
    HEM_BAND("free1", "FREE 1", 80.0f), HEM_BAND("band2", "BAND 2", 100.0f), HEM_BAND("band3", "BAND 3", 600.0f),
    HEM_BAND("band4", "BAND 4", 3000.0f), HEM_BAND("band5", "BAND 5", 10000.0f), HEM_BAND("free6", "FREE 6", 8000.0f),
    {"low_xover", "LOW XOVER", "Hz", 40.0f, 1000.0f, 150.0f, Taper::Log},
    {"mid_xover", "MID XOVER", "Hz", 200.0f, 6000.0f, 1500.0f, Taper::Log},
    {"high_xover", "HIGH XOVER", "Hz", 1500.0f, 16000.0f, 6000.0f, Taper::Log},
    {"xover_q", "XOVER Q", "", 0.5f, 1.2f, 0.7071f},
    {"master_thr", "MASTER THRESHOLD", "dB", -30.0f, 30.0f, 0.0f},
    {"master_gain", "MASTER GAIN", "dB", -18.0f, 18.0f, 0.0f},
    {"master_range", "MASTER RANGE", "dB", -24.0f, 24.0f, 0.0f},
    {"master_attack", "MASTER ATTACK", "%", 10.0f, 1000.0f, 100.0f, Taper::Log},
    {"master_release", "MASTER RELEASE", "%", 10.0f, 1000.0f, 100.0f, Taper::Log},
    {"release_mode", "RELEASE MODE", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kRelN},
    {"behavior", "BEHAVIOR", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kBehN},
    {"knee", "KNEE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kKneeN},
    {"output", "OUTPUT", "dB", -18.0f, 18.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {
    {"FREE 1", "dB", -24, 24, true}, {"BAND 2", "dB", -24, 24, true}, {"BAND 3", "dB", -24, 24, true},
    {"BAND 4", "dB", -24, 24, true}, {"BAND 5", "dB", -24, 24, true}, {"FREE 6", "dB", -24, 24, true},
    {"OUTPUT L", "dBFS", -60, 0}, {"OUTPUT R", "dBFS", -60, 0}, {"CLIP", "", 0, 1},
};

class BandCouncil final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    BandCouncil() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i < kLowX) { const int b = i / kPer, k = i % kPer; if (k == bFreq || k == bQ) return b == 0 || b == 5; return k == bBypass || p(b * kPer + bBypass) < 0.5f; }
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& b : st_) b.gain.prepare(fs, 20.0, 1.0f);
        outG_.prepare(fs, 20.0, 1.0f); splitMix_.prepare(fs, 20.0, 0.0f); mL_.prepare(fs); mR_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& b : st_) { b.env = 0; b.slow = 0; b.dyn = 0; b.eq.clear(); b.det.clear(); b.on = false; b.key = 1e9f; }
        for (auto& t : tree_) t = Tree{};
        splitMix_.cur = 0.0f;
        xKey_ = -1; clip_ = false;
        setCrossover();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        bool anySolo = false, anyListen = false;
        for (int b = 0; b < kBands; ++b) { anySolo |= p(b * kPer + bSolo) > 0.5f; anyListen |= p(b * kPer + bListen) > 0.5f; }
        setCrossover();
        const float attS = p(kMAtt) * 0.01f * (p(kBehavior) < 0.5f ? 2.0f : 1.0f);    // SMOOTH: twice as slow
        const float relS = p(kMRel) * 0.01f * (p(kBehavior) < 0.5f ? 2.0f : 1.0f);
        const bool soft = p(kKnee) < 0.5f;
        const float og = kit::dbToGain(p(kOutput));
        bool engaged = anySolo || anyListen || !kit::isFlat(p(kMGain)) || !kit::isFlat(p(kMRange));
        for (int b = 0; b < kBands && !engaged; ++b) engaged = !kit::isFlat(p(b * kPer + bGain)) || !kit::isFlat(p(b * kPer + bRange));
        if (!engaged && splitMix_.cur < 1e-6f) {   // nothing set: the input passes exactly (detectors keep listening for the meters)
            splitMix_.cur = 0.0f;
            const float o0 = og;
            if (!(kit::isFlat(o0, 1.0f) && kit::isFlat(outG_.cur, 1.0f)))
                for (std::uint32_t i = 0; i < n; ++i) { const float o = outG_.next(o0); L[i] *= o; R[i] *= o; }
            for (auto& b : st_) b.dyn = 0.0f;
            mL_.feed(L, n); mR_.feed(R, n);
            for (int b = 0; b < kBands; ++b) publishMeter(b, 0.0f);
            publishMeter(6, mL_.db()); publishMeter(7, mR_.db()); publishMeter(8, clip_ ? 1.0f : 0.0f);
            publishReduction(0.0f);
            scL_ = scR_ = nullptr;
            return;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x[2] = {L[i], R[i]};
            const float sc = scL_ ? 0.5f * (scL_[i] + scR_[i]) : 0.0f;
            // the four crossover bands: split at MID, then the low side at LOW, the high side at HIGH;
            // each side gets the other split's all-pass so all four stay in phase
            std::array<std::array<float, 2>, 4> band{};
            for (int c = 0; c < 2; ++c) {
                Tree& t = tree_[static_cast<std::size_t>(c)];
                float lo = static_cast<float>(t.midLp[1].tick(static_cast<float>(t.midLp[0].tick(x[c]))));
                float hi = static_cast<float>(t.midHp[1].tick(static_cast<float>(t.midHp[0].tick(x[c]))));
                lo = static_cast<float>(t.apHigh.tick(lo));
                hi = static_cast<float>(t.apLow.tick(hi));
                band[0][static_cast<std::size_t>(c)] = static_cast<float>(t.lowLp[1].tick(static_cast<float>(t.lowLp[0].tick(lo))));
                band[1][static_cast<std::size_t>(c)] = static_cast<float>(t.lowHp[1].tick(static_cast<float>(t.lowHp[0].tick(lo))));
                band[2][static_cast<std::size_t>(c)] = static_cast<float>(t.highLp[1].tick(static_cast<float>(t.highLp[0].tick(hi))));
                band[3][static_cast<std::size_t>(c)] = static_cast<float>(t.highHp[1].tick(static_cast<float>(t.highHp[0].tick(hi))));
            }
            float yl = 0, yr = 0, listenL = 0, listenR = 0;
            for (int k = 0; k < 4; ++k) {
                const int b = k + 1;
                auto& bd = band[static_cast<std::size_t>(k)];
                const float det = p(b * kPer + bSc) > 0.5f && scL_ ? sc : p(b * kPer + bDetect) > 0.5f ? 0.5f * (bd[0] + bd[1]) : 0.5f * (x[0] + x[1]);
                const float g = gainOf(b, det, attS, relS, soft);
                const bool pass = !anySolo || p(b * kPer + bSolo) > 0.5f;
                if (pass) { yl += bd[0] * g; yr += bd[1] * g; }
                if (p(b * kPer + bListen) > 0.5f) { listenL += det; listenR += det; }
            }
            // the floating bands: dynamic bells on the summed signal; their own band-pass is the detector (BAND ONLY) and the SOLO signal
            for (int b : {0, 5}) {
                const float mono = 0.5f * (x[0] + x[1]);
                const float bp = st_[static_cast<std::size_t>(b)].det.band(mono);
                const float det = p(b * kPer + bSc) > 0.5f && scL_ ? sc : p(b * kPer + bDetect) > 0.5f ? bp : mono;
                const float g = gainOf(b, det, attS, relS, soft);
                if (anySolo) { if (p(b * kPer + bSolo) > 0.5f) { yl += bp * g; yr += bp * g; } }
                else { float s[2] = {yl, yr}; floatBand(b, g, s); yl = s[0]; yr = s[1]; }
                if (p(b * kPer + bListen) > 0.5f) { listenL += det; listenR += det; }
            }
            if (anyListen) { yl = listenL; yr = listenR; }
            const float sm = splitMix_.next(engaged ? 1.0f : 0.0f);
            yl = x[0] + sm * (yl - x[0]); yr = x[1] + sm * (yr - x[1]);
            const float o = outG_.next(og);
            yl *= o; yr *= o;
            if (std::fabs(yl) >= 1.0f || std::fabs(yr) >= 1.0f) clip_ = true;
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        mL_.feed(L, n); mR_.feed(R, n);
        float most = 0;
        for (int b = 0; b < kBands; ++b) { publishMeter(b, st_[static_cast<std::size_t>(b)].dyn); most = std::min(most, st_[static_cast<std::size_t>(b)].dyn); }
        publishMeter(6, mL_.db()); publishMeter(7, mR_.db()); publishMeter(8, clip_ ? 1.0f : 0.0f);
        publishReduction(most);
        scL_ = scR_ = nullptr;
    }

private:
    struct Band { float env = 0, slow = 0, dyn = 0, key = 1e9f; bool on = false; kit::Svf det; hc::fx::Biquad2 eq; hc::fx::Smoothed gain; };

    void setCrossover() noexcept {
        const float key = p(kLowX) + p(kMidX) * 7.0f + p(kHighX) * 13.0f + p(kXQ) * 100000.0f;
        if (key == xKey_) return;
        xKey_ = key;
        const double lo = p(kLowX), mid = std::max(p(kMidX), p(kLowX) * 1.2f), hi = std::max(p(kHighX), p(kMidX) * 1.2f), q = p(kXQ);
        for (auto& t : tree_) {
            for (int k = 0; k < 2; ++k) {
                t.midLp[static_cast<std::size_t>(k)].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, mid, q);
                t.midHp[static_cast<std::size_t>(k)].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, mid, q);
                t.lowLp[static_cast<std::size_t>(k)].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, lo, q);
                t.lowHp[static_cast<std::size_t>(k)].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, lo, q);
                t.highLp[static_cast<std::size_t>(k)].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hi, q);
                t.highHp[static_cast<std::size_t>(k)].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hi, q);
            }
            t.apHigh.c = hc::fx::BiquadCoefs::make(BT::AllPass, fs_, hi, q);   // the low side, matched to the high split
            t.apLow.c = hc::fx::BiquadCoefs::make(BT::AllPass, fs_, lo, q);    // the high side, matched to the low split
        }
        for (int b : {0, 5}) st_[static_cast<std::size_t>(b)].det.set(fs_, p(b * kPer + bFreq), p(b * kPer + bQ));
    }

    // a band's gain (linear) from its detector: static GAIN + RANGE move once over THRESHOLD, with MASTER offsets
    float gainOf(int b, float det, float attS, float relS, bool soft) noexcept {
        Band& bd = st_[static_cast<std::size_t>(b)];
        const int o = b * kPer;
        if (p(o + bBypass) > 0.5f) { bd.dyn = 0; return 1.0f; }
        const float a = std::fabs(det);
        const float ca = hc::fx::onePoleCoef(fs_, p(o + bAtt) * attS);
        float relMs = p(o + bRel) * relS;
        if (p(kRelMode) < 0.5f) relMs *= 1.0f + 2.0f * std::clamp(bd.slow * 8.0f, 0.0f, 1.0f);   // AUTO
        const float cr = hc::fx::onePoleCoef(fs_, relMs);
        bd.env = a + (a > bd.env ? ca : cr) * (bd.env - a);
        bd.slow = a + 0.99995f * (bd.slow - a);
        const float range = p(o + bRange) + p(kMRange);
        float dyn = 0.0f;
        if (!kit::isFlat(range)) {
            const float over = kit::gainToDb(bd.env * 1.4142f) - (p(o + bThr) + p(kMThr));
            float move = 0.0f;
            if (soft) move = 0.5f * (over > 3.0f ? over : over > -3.0f ? (over + 3.0f) * (over + 3.0f) / 12.0f : 0.0f);
            else move = over > 0.0f ? 0.5f * over : 0.0f;
            dyn = std::copysign(std::min(std::fabs(range), move), range);
        }
        bd.dyn = dyn;
        return bd.gain.next(kit::dbToGain(p(o + bGain) + p(kMGain) + dyn));
    }

    void floatBand(int b, float g, float* s) noexcept {
        Band& bd = st_[static_cast<std::size_t>(b)];
        const float db = kit::gainToDb(g);
        if (kit::isFlat(db, 0.0f) && !bd.on) return;
        bd.on = true;
        const int o = b * kPer;
        const float dq = std::round(db * 20.0f) / 20.0f;   // coefficients follow in 0.05 dB steps
        const float key = dq * 1000.0f + p(o + bFreq) + p(o + bQ) * 0.01f;
        if (std::fabs(bd.key - key) > 1e-3f) { bd.key = key; bd.eq.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, p(o + bFreq), p(o + bQ), dq)); }
        float l = s[0], r = s[1];
        bd.eq.run(&l, &r, 1);
        s[0] = l; s[1] = r;
    }

    double fs_ = 48000;
    std::array<Band, kBands> st_{};
    struct Tree { std::array<hc::fx::Biquad, 2> midLp{}, midHp{}, lowLp{}, lowHp{}, highLp{}, highHp{}; hc::fx::Biquad apHigh, apLow; };
    std::array<Tree, 2> tree_{};
    hc::fx::Smoothed splitMix_;
    float xKey_ = -1;
    bool clip_ = false;
    hc::fx::Smoothed outG_;
    kit::PeakMeter mL_, mR_;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeBandCouncil() { return std::make_unique<BandCouncil>(); }

} // namespace hem::staging
