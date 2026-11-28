// =============================================================================
//  11 LEADSUITE — a whole vocal chain on six faders: BASS, TREBLE, COMPRESS, REVERB, DELAY,
//  PITCH (doubler). Each fader -10 .. +10 with three flavours and an OFF switch.
//  Different from CHANNELMATRIX (29) / CONSOLECORE (28): macro faders with voiced flavours,
//  not a knob per stage.
//  What a fader's sign means: BASS / TREBLE cut (-) or boost (+); COMPRESS squeezes the peaks (+)
//  or gently expands the quiet parts down (-); REVERB / DELAY / PITCH send level is the size of
//  the number, + bright return, - dark return.
//  Dead flat on load: every fader 0, input 0, output 0, balance 0, STEREO.
//  Its own reverb (a modulated allpass loop), delay and doubler — none of the rack's engines.
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
    kBass, kBassFl, kBassOff, kTreble, kTrebFl, kTrebOff, kComp, kCompFl, kCompOff,
    kVerb, kVerbFl, kVerbOff, kDelay, kDelayFl, kDelayOff, kPitch, kPitchFl, kPitchOff,
    kInput, kOutput, kBalance, kStereo, kSync, kBypass, kCount
};
const char* const kBassN[] = {"DEEP", "LOW", "WARM"};
const char* const kTrebN[] = {"PRESENCE", "BRIGHT", "AIR"};
const char* const kCompN[] = {"GENTLE", "PUNCHY", "HEAVY"};
const char* const kVerbN[] = {"ROOM", "HALL", "CHAMBER"};
const char* const kDelayN[] = {"1/16", "1/8", "1/4"};
const char* const kPitchN[] = {"DOUBLE", "WIDE", "ULTRA WIDE"};
const char* const kStereoN[] = {"MONO TO STEREO", "STEREO"};
#define HEM_FADER(id, name, fl, flN) \
    {id, name, "", -10.0f, 10.0f, 0.0f}, \
    {id "_flavour", name " FLAVOUR", "", 0.0f, 2.0f, fl, Taper::Choice, flN}, \
    {id "_off", name " OFF", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}
const ParamInfo kParams[kCount] = {
    HEM_FADER("bass", "BASS", 0.0f, kBassN), HEM_FADER("treble", "TREBLE", 0.0f, kTrebN),
    HEM_FADER("compress", "COMPRESS", 0.0f, kCompN), HEM_FADER("reverb", "REVERB", 0.0f, kVerbN),
    HEM_FADER("delay", "DELAY", 1.0f, kDelayN), HEM_FADER("pitch", "PITCH", 0.0f, kPitchN),
    {"input", "INPUT SENSITIVITY", "dB", -10.0f, 10.0f, 0.0f},
    {"output", "OUTPUT", "dB", -10.0f, 10.0f, 0.0f},
    {"balance", "BALANCE", "dB", -6.0f, 6.0f, 0.0f},
    {"stereo", "STEREO MODE", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kStereoN},
    {"tempo_sync", "TEMPO SYNC", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_FADER
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT L", "dB", -20, 6}, {"INPUT R", "dB", -20, 6}, {"OUTPUT L", "dB", -20, 6}, {"OUTPUT R", "dB", -20, 6}, {"COMPRESSION", "dB", -20, 0},
};

class LeadSuite final : public hc::fx::Effect {
public:
    LeadSuite() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        const int f = i / 3;
        if (i < kInput && i % 3 != 2) return p(f * 3 + 2) < 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        dlyL_.prepare(static_cast<std::uint32_t>(fs * 2.5)); dlyR_.prepare(static_cast<std::uint32_t>(fs * 2.5));
        dblL_.prepare(static_cast<std::uint32_t>(fs * 0.06)); dblR_.prepare(static_cast<std::uint32_t>(fs * 0.06));
        for (auto& a : verbAp_) a.prepare(static_cast<std::uint32_t>(fs * 0.2));
        verbLoopL_.prepare(static_cast<std::uint32_t>(fs * 0.25)); verbLoopR_.prepare(static_cast<std::uint32_t>(fs * 0.25));
        verbPre_.prepare(static_cast<std::uint32_t>(fs * 0.1));
        bassG_.prepare(fs, 32, 30.0, 0.0f); trebG_.prepare(fs, 32, 30.0, 0.0f);
        for (auto* s : {&inG_, &outG_, &balL_, &balR_}) s->prepare(fs, 20.0, 1.0f);
        for (auto* s : {&verbS_, &delayS_, &pitchS_, &compAmt_}) s->prepare(fs, 30.0, 0.0f);
        inML_.prepare(fs); inMR_.prepare(fs); outML_.prepare(fs); outMR_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        bassEq_.clear(); trebEq_.clear(); dlyL_.clear(); dlyR_.clear(); dblL_.clear(); dblR_.clear();
        for (auto& a : verbAp_) a.clear();
        verbLoopL_.clear(); verbLoopR_.clear(); verbPre_.clear();
        env_ = 0; grDb_ = 0; vLpL_ = vLpR_ = 0; dLp_[0] = dLp_[1] = 0; rLp_[0] = rLp_[1] = 0; ph_ = 0;
        bassOn_ = trebOn_ = false; keys_.fill(1e9f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inML_.feed(L, n); inMR_.feed(R, n);
        if (p(kBypass) > 0.5f) { outML_.feed(L, n); outMR_.feed(R, n); publishAll(); return; }
        if (p(kStereo) < 0.5f) std::copy(L, L + n, R);              // MONO TO STEREO: the left feeds both sides
        const float inT = kit::dbToGain(p(kInput));
        if (!(kit::isFlat(inT, 1.0f) && kit::isFlat(inG_.cur, 1.0f)))
            for (std::uint32_t i = 0; i < n; ++i) { const float g = inG_.next(inT); L[i] *= g; R[i] *= g; }
        constexpr std::uint32_t kSub = 32;
        for (std::uint32_t at = 0; at < n; at += kSub) eq(L + at, R + at, std::min(kSub, n - at));
        compress(L, R, n);
        effects(L, R, n);
        const float bal = p(kBalance);
        const float blT = bal > 0.0f ? kit::dbToGain(-bal) : 1.0f, brT = bal < 0.0f ? kit::dbToGain(bal) : 1.0f;
        const float oT = kit::dbToGain(p(kOutput));
        const bool flat = kit::isFlat(blT, 1.0f) && kit::isFlat(brT, 1.0f) && kit::isFlat(oT, 1.0f) &&
                          kit::isFlat(balL_.cur, 1.0f) && kit::isFlat(balR_.cur, 1.0f) && kit::isFlat(outG_.cur, 1.0f);
        if (!flat)
            for (std::uint32_t i = 0; i < n; ++i) { const float o = outG_.next(oT); L[i] *= o * balL_.next(blT); R[i] *= o * balR_.next(brT); }
        for (std::uint32_t i = 0; i < n; ++i) { L[i] = kit::sane(L[i]); R[i] = kit::sane(R[i]); }
        outML_.feed(L, n); outMR_.feed(R, n);
        publishAll();
    }

private:
    [[nodiscard]] float fader(int k) const noexcept { return p(k + 2) > 0.5f ? 0.0f : p(k); }   // OFF = 0
    bool setKey(int k, float v) noexcept { if (keys_[static_cast<std::size_t>(k)] == v) return false; keys_[static_cast<std::size_t>(k)] = v; return true; }

    // BASS: DEEP 60 Hz shelf, LOW 120 Hz shelf, WARM 250 Hz bell. TREBLE: PRESENCE 3 kHz bell, BRIGHT 6 kHz shelf, AIR 12 kHz shelf.
    // +-10 on the fader = +-12 dB
    void eq(float* l, float* r, std::uint32_t m) noexcept {
        const float b = bassG_.step(fader(kBass) * 1.2f), t = trebG_.step(fader(kTreble) * 1.2f);
        if (!kit::isFlat(b) || bassOn_) {
            bassOn_ = true;
            const int fl = static_cast<int>(p(kBassFl));
            if (setKey(0, b) | setKey(1, static_cast<float>(fl))) {
                const BT ty = fl == 2 ? BT::Bell : BT::LowShelf;
                bassEq_.set(hc::fx::BiquadCoefs::make(ty, fs_, std::array<double, 3>{60, 120, 250}[static_cast<std::size_t>(fl)], fl == 2 ? 0.9 : 0.7, b));
            }
            bassEq_.run(l, r, m);
        }
        if (!kit::isFlat(t) || trebOn_) {
            trebOn_ = true;
            const int fl = static_cast<int>(p(kTrebFl));
            if (setKey(2, t) | setKey(3, static_cast<float>(fl))) {
                const BT ty = fl == 0 ? BT::Bell : BT::HighShelf;
                trebEq_.set(hc::fx::BiquadCoefs::make(ty, fs_, std::array<double, 3>{3000, 6000, 12000}[static_cast<std::size_t>(fl)], fl == 0 ? 0.8 : 0.7, t));
            }
            trebEq_.run(l, r, m);
        }
    }

    // COMPRESS +: GENTLE 2:1 slow, PUNCHY 4:1 fast, HEAVY 8:1 fast; threshold falls 2.4 dB per step from -6 dBFS,
    // half the reduction is made up. COMPRESS -: a 2:1 expander below -30 dBFS + 2 dB per step.
    void compress(float* L, float* R, std::uint32_t n) noexcept {
        const float v = fader(kComp);
        const float amt = compAmt_.cur;
        if (kit::isFlat(v) && kit::isFlat(amt)) { grDb_ = 0.0f; return; }
        static constexpr std::array<float, 3> kRatio{2.0f, 4.0f, 8.0f}, kAtt{20.0f, 2.0f, 1.0f}, kRel{250.0f, 80.0f, 60.0f};
        const int fl = static_cast<int>(p(kCompFl));
        const float att = hc::fx::onePoleCoef(fs_, kAtt[static_cast<std::size_t>(fl)]), rel = hc::fx::onePoleCoef(fs_, kRel[static_cast<std::size_t>(fl)]);
        float worst = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = compAmt_.next(v);
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            env_ = x + (x > env_ ? att : rel) * (env_ - x);
            const float lvl = kit::gainToDb(env_);
            float gr = 0.0f;
            if (a > 0.0f) {
                const float thr = -6.0f - 2.4f * a, ratio = kRatio[static_cast<std::size_t>(fl)];
                if (lvl > thr) gr = -(lvl - thr) * (1.0f - 1.0f / ratio);
                gr += -gr * 0.5f * std::min(1.0f, a / 10.0f);   // half made up at full
                gr = std::min(gr, 0.0f);
            } else if (a < 0.0f) {
                const float thr = -30.0f + 2.0f * (-a);
                if (lvl < thr) gr = -(thr - lvl);                 // 2:1 downward expansion
                gr = std::max(gr, -24.0f);
            }
            const float g = kit::dbToGain(gr);
            L[i] *= g; R[i] *= g;
            worst = std::min(worst, gr);
        }
        grDb_ = worst;
    }

    void effects(float* L, float* R, std::uint32_t n) noexcept {
        const float vT = std::fabs(fader(kVerb)) * 0.1f, dT = std::fabs(fader(kDelay)) * 0.1f, pT = std::fabs(fader(kPitch)) * 0.1f;
        if (vT <= 0.0f && dT <= 0.0f && pT <= 0.0f && verbS_.cur < 1e-5f && delayS_.cur < 1e-5f && pitchS_.cur < 1e-5f) return;
        const bool vDark = fader(kVerb) < 0.0f, dDark = fader(kDelay) < 0.0f, pDark = fader(kPitch) < 0.0f;
        // REVERB: ROOM 0.6 s, HALL 2.2 s, CHAMBER 1.3 s (decay set by the loop gain), 20 / 45 / 30 ms pre-delay
        static constexpr std::array<float, 3> kLoopMs{43.0f, 97.0f, 67.0f}, kRt{0.6f, 2.2f, 1.3f}, kPre{20.0f, 45.0f, 30.0f};
        const int vf = static_cast<int>(p(kVerbFl));
        const float loopMs = kLoopMs[static_cast<std::size_t>(vf)];
        const float loopG = std::pow(10.0f, -3.0f * loopMs * 0.001f / kRt[static_cast<std::size_t>(vf)]);
        // DELAY: 1/16, 1/8, 1/4 of the song's beat (TEMPO SYNC) or 125 / 250 / 500 ms
        static constexpr std::array<float, 3> kDiv{0.25f, 0.5f, 1.0f};
        const float beatMs = 60000.0f / std::max(tempo(), 20.0f);
        const float dMs = p(kSync) > 0.5f ? beatMs * kDiv[static_cast<std::size_t>(p(kDelayFl))] : 500.0f * kDiv[static_cast<std::size_t>(p(kDelayFl))];
        const float dSamp = std::min(static_cast<float>(fs_) * dMs * 0.001f, static_cast<float>(fs_ * 2.4));
        // PITCH: DOUBLE +-8 cents / 15 ms, WIDE +-15 / 25 ms, ULTRA WIDE +-25 / 35 ms
        static constexpr std::array<float, 3> kCents{8.0f, 15.0f, 25.0f}, kDblMs{15.0f, 25.0f, 35.0f};
        const int pf = static_cast<int>(p(kPitchFl));
        const float base = static_cast<float>(fs_) * kDblMs[static_cast<std::size_t>(pf)] * 0.001f;
        const float sweep = base * (std::exp2(kCents[static_cast<std::size_t>(pf)] / 1200.0f) - 1.0f) * 8.0f;   // a slow 0.3 Hz sweep detunes by about +-cents
        const float darkC = hc::fx::onePoleCoef(fs_, 0.05);   // dark returns: ~3 kHz one-pole
        for (std::uint32_t i = 0; i < n; ++i) {
            const float vs = verbS_.next(vT), ds = delayS_.next(dT), ps = pitchS_.next(pT);
            const float mono = 0.5f * (L[i] + R[i]);
            float addL = 0, addR = 0;
            if (vs > 1e-6f) {
                verbPre_.write(mono);
                float x = verbPre_.read(static_cast<float>(fs_) * kPre[static_cast<std::size_t>(vf)] * 0.001f);
                for (std::size_t a = 0; a < verbAp_.size(); ++a) {          // diffusion
                    static constexpr std::array<float, 4> kApMs{4.7f, 3.6f, 12.7f, 9.3f};
                    const float d = static_cast<float>(fs_) * kApMs[a] * 0.001f;
                    const float b = verbAp_[a].read(d);
                    verbAp_[a].write(x + 0.6f * b);
                    x = b - 0.6f * x;
                }
                const float tl = verbLoopL_.read(static_cast<float>(fs_) * loopMs * 0.001f);
                const float tr = verbLoopR_.read(static_cast<float>(fs_) * loopMs * 0.00113f);
                vLpL_ = tl + 0.35f * (vLpL_ - tl); vLpR_ = tr + 0.35f * (vLpR_ - tr);
                verbLoopL_.write(x + loopG * vLpR_); verbLoopR_.write(x + loopG * vLpL_);   // cross-fed loop: a wide tail
                float yl = tl, yr = tr;
                if (vDark) { rLp_[0] = yl + darkC * (rLp_[0] - yl); rLp_[1] = yr + darkC * (rLp_[1] - yr); yl = rLp_[0]; yr = rLp_[1]; }
                addL += vs * 0.5f * yl; addR += vs * 0.5f * yr;
            }
            if (ds > 1e-6f) {
                const float tl = dlyL_.read(dSamp), tr = dlyR_.read(dSamp);
                dlyL_.write(mono + 0.35f * tr); dlyR_.write(0.35f * tl);          // ping-pong feedback
                float yl = tl, yr = tr;
                if (dDark) { dLp_[0] = yl + darkC * (dLp_[0] - yl); dLp_[1] = yr + darkC * (dLp_[1] - yr); yl = dLp_[0]; yr = dLp_[1]; }
                addL += ds * yl; addR += ds * yr;
            }
            if (ps > 1e-6f) {
                ph_ += 0.3 / fs_; if (ph_ >= 1.0) ph_ -= 1.0;
                const float lfo = std::sin(kit::kTau * static_cast<float>(ph_));
                dblL_.write(mono); dblR_.write(mono);
                float yl = dblL_.read(base + sweep * lfo), yr = dblR_.read(base * 1.3f - sweep * lfo);
                if (pDark) { yl *= 0.7f; yr *= 0.7f; }
                addL += ps * yl; addR += ps * yr;
            }
            L[i] += addL; R[i] += addR;
        }
    }

    void publishAll() noexcept {
        publishMeter(0, inML_.db()); publishMeter(1, inMR_.db()); publishMeter(2, outML_.db()); publishMeter(3, outMR_.db()); publishMeter(4, grDb_);
        publishReduction(grDb_);
    }

    double fs_ = 48000;
    hc::fx::Biquad2 bassEq_, trebEq_;
    kit::BlockGlide bassG_, trebG_;
    bool bassOn_ = false, trebOn_ = false;
    std::array<float, 4> keys_{};
    hc::fx::Smoothed inG_, outG_, balL_, balR_, verbS_, delayS_, pitchS_, compAmt_;
    float env_ = 0, grDb_ = 0, vLpL_ = 0, vLpR_ = 0;
    std::array<float, 2> dLp_{}, rLp_{};
    double ph_ = 0;
    hc::fx::DelayLine dlyL_, dlyR_, dblL_, dblR_, verbLoopL_, verbLoopR_, verbPre_;
    std::array<hc::fx::DelayLine, 4> verbAp_;
    kit::PeakMeter inML_, inMR_, outML_, outMR_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeLeadSuite() { return std::make_unique<LeadSuite>(); }

} // namespace hem::staging
