// =============================================================================
//  47 REVERB — an all-purpose reverb with five room types, each its own design (not one engine with
//  presets):
//    HALL     six delay lines mixed through an orthogonal (Householder) matrix: smooth and wide
//    PLATE    two cross-coupled loops of nested all-passes: very dense, bright, fast build-up
//    ROOM     sixteen early reflections into four short damped combs per side: close and woody
//    CHAMBER  a ring of six all-pass + delay stages: thick, diffuse, hard walls
//    SPRING   two dispersive loops (a long chain of all-passes inside each): the drip and "boing"
//  Common controls: PRE-DELAY (0..200 ms), SIZE (0.5..10), DECAY (0.2..10 s), DAMPING (1..20 kHz:
//  how fast the highs die), DIFFUSION (input smear, 0 = none), EARLY REFLECTIONS (level), MODULATION
//  (0 = still, up to 2 Hz of slow pitch drift in the tail), REVERB EQ (LOW CUT, HIGH CUT on the wet),
//  WIDTH, MIX, OUTPUT, FREEZE (the tail holds forever, new sound is kept out), BYPASS.
//  Written for this plugin only: no shared rack reverb engine.
//  Different from CHAMBERPLATE (24: models of real chambers / plates) and DEPTHMATRIX (27: routing).
//  Dead flat on load: MIX 0 %, OUTPUT 0 dB.
//  ASSUMED: SIZE scales every delay length; SPRING bandwidth 4.5 kHz; MODULATION depth 0.3 ms x sqrt(SIZE);
//  early-reflection taps 4..60 ms x SIZE; ROOM keeps its own reflections at half level on top of the
//  EARLY REFLECTIONS knob.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kPre, kSize, kDecay, kDamp, kDiff, kEr, kMod, kMix, kOut, kAlgo, kLowCut, kHighCut, kWidth, kFreeze, kBypass, kCount };
const char* const kAlgoN[] = {"HALL", "PLATE", "ROOM", "CHAMBER", "SPRING"};
const ParamInfo kParams[kCount] = {
    {"pre_delay", "PRE-DELAY", "ms", 0.0f, 200.0f, 0.0f},
    {"size", "SIZE", "", 0.5f, 10.0f, 1.0f, Taper::Log},
    {"decay", "DECAY", "s", 0.2f, 10.0f, 2.0f, Taper::Log},
    {"damping", "DAMPING", "Hz", 1000.0f, 20000.0f, 8000.0f, Taper::Log},
    {"diffusion", "DIFFUSION", "%", 0.0f, 100.0f, 0.0f},
    {"early_reflections", "EARLY REFLECTIONS", "%", 0.0f, 100.0f, 0.0f},
    {"modulation", "MODULATION", "Hz", 0.0f, 2.0f, 0.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"algorithm", "ALGORITHM", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kAlgoN},
    {"low_cut", "LOW CUT", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"high_cut", "HIGH CUT", "Hz", 4000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"width", "WIDTH", "%", 0.0f, 100.0f, 100.0f},
    {"freeze", "FREEZE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 0}, {"OUT", "dB", -60, 0}, {"INPUT VU", "VU", -20, 3, true}, {"OUTPUT VU", "VU", -20, 3, true}};

constexpr float kMaxSize = 10.0f;

struct Ap {   // Schroeder all-pass; read before write, so `len` is the exact delay
    hc::fx::DelayLine d;
    void prepare(std::uint32_t m) { d.prepare(m + 4); }
    void clear() noexcept { d.clear(); }
    float tick(float x, float g, float len) noexcept { const float v = d.read(std::max(0.0f, len - 1.0f)); const float w = x + g * v; d.write(w); return v - g * w; }
};
struct Nested {   // an all-pass whose own delay holds a second all-pass (plate density)
    hc::fx::DelayLine d;
    Ap inner;
    void prepare(std::uint32_t outer, std::uint32_t in) { d.prepare(outer + 4); inner.prepare(in); }
    void clear() noexcept { d.clear(); inner.clear(); }
    float tick(float x, float g, float len, float gi, float lenIn) noexcept {
        const float v = d.read(std::max(0.0f, len - lenIn - 1.0f));
        const float vi = inner.tick(v, gi, lenIn);
        const float w = x + g * vi;
        d.write(w);
        return vi - g * w;
    }
};

class HemReverb final : public hc::fx::Effect {
public:
    HemReverb() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const double sc = fs * 0.001 * kMaxSize;   // samples per base-ms at the largest size
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(fs * 0.21));
        for (auto& d : er_) d.prepare(static_cast<std::uint32_t>(fs * 0.65));
        for (auto& c : diff_) for (auto& a : c) a.prepare(static_cast<std::uint32_t>(6.0 * sc));
        for (auto& l : fdn_) l.prepare(static_cast<std::uint32_t>(70.0 * sc) + 64);
        for (auto& c : plateN_) for (auto& a : c) a.prepare(static_cast<std::uint32_t>(32.0 * sc), static_cast<std::uint32_t>(10.0 * sc));
        for (auto& d : plateD_) d.prepare(static_cast<std::uint32_t>(40.0 * sc) + 64);
        for (auto& c : comb_) for (auto& d : c) d.prepare(static_cast<std::uint32_t>(32.0 * sc));
        for (auto& s : ring_) { s.ap.prepare(static_cast<std::uint32_t>(12.0 * sc)); s.d.prepare(static_cast<std::uint32_t>(40.0 * sc) + 64); }
        for (auto& d : springD_) d.prepare(static_cast<std::uint32_t>(50.0 * sc) + 64);
        mix_.prepare(fs, 20.0, 0.0f); out_.prepare(fs, 20.0, 1.0f); inG_.prepare(fs, 20.0, 1.0f); preS_.prepare(fs, 80.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : pre_) d.clear();
        for (auto& d : er_) d.clear();
        for (auto& c : diff_) for (auto& a : c) a.clear();
        for (auto& l : fdn_) l.clear();
        for (auto& c : plateN_) for (auto& a : c) a.clear();
        for (auto& d : plateD_) d.clear();
        for (auto& c : comb_) for (auto& d : c) d.clear();
        for (auto& s : ring_) { s.ap.clear(); s.d.clear(); s.lp = 0; }
        for (auto& d : springD_) d.clear();
        for (auto& c : springAp_) for (auto& a : c) a = {};
        lp_.fill(0); combLp_ = {}; plateLp_.fill(0); plateFb_.fill(0); springLp_.fill(0); springBw_.fill(0); springFb_.fill(0);
        hp_.clear(); hc_.clear(); key_ = -1; hpOn_ = hcOn_ = false; modPh_ = 0; loaded_ = false; vuI_ = vuO_ = 0; inPk_ = outPk_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float mixT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOut));
        const bool freeze = p(kFreeze) > 0.5f;
        const float fs = static_cast<float>(fs_);
        const float preT = p(kPre) * 0.001f * fs;
        if (!loaded_) { mix_.cur = mixT; out_.cur = oT; inG_.cur = freeze ? 0.0f : 1.0f; preS_.cur = preT; loaded_ = true; }
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        float inPk = 0, outPk = 0;
        if (!wet) {
            if (!(kit::isFlat(oT, 1.0f) && kit::isFlat(out_.cur, 1.0f))) for (std::uint32_t i = 0; i < n; ++i) { const float o = out_.next(oT); L[i] *= o; R[i] *= o; }
            meters(L, R, n, L, R); return;
        }
        setEq();
        const int algo = static_cast<int>(p(kAlgo));
        // DECAY calibration: the loop gains are set from the knob, then scaled per type so the measured RT60
        // (backward-integrated, -5..-25 dB) lands on the knob at the default damping (measured 0.80..0.88 x before)
        static constexpr std::array<float, 5> kRtCal{0.85f, 0.87f, 0.82f, 0.86f, 0.885f};
        const float size = p(kSize), rt = p(kDecay) / kRtCal[static_cast<std::size_t>(std::clamp(static_cast<int>(p(kAlgo)), 0, 4))];
        const float ms = fs * 0.001f * size;   // samples per base-ms at this size
        // whole-sample lengths: a fractional read is a gentle low-pass, and in a loop it would dull (and, in
        // FREEZE, slowly drain) the tail; only the modulation moves a read between samples
        auto W = [](float x) { return std::max(1.0f, std::round(x)); };
        const float dampC = freeze ? 0.0f : std::exp(-kit::kTau * p(kDamp) / fs);
        const float diffG = 0.75f * p(kDiff) * 0.01f;
        const float erG = p(kEr) * 0.01f;
        const float modRate = p(kMod), modDepth = modRate > 0.0f ? 0.0003f * fs * std::sqrt(size) : 0.0f;
        auto loopGain = [&](float lenSamples) { return freeze ? 0.9999f : std::min(0.9995f, std::pow(10.0f, -3.0f * lenSamples / (fs * rt))); };
        const float w = p(kWidth) * 0.01f;
        static constexpr std::array<float, 12> kErMs{4.3f, 7.1f, 9.7f, 13.1f, 16.9f, 21.3f, 26.1f, 31.7f, 37.3f, 43.9f, 51.1f, 59.3f};
        static constexpr std::array<float, 6> kFdnMs{37.1f, 41.3f, 47.9f, 53.7f, 59.3f, 67.1f};
        std::array<float, 6> fdnLen{}, fdnG{};
        for (std::size_t k = 0; k < 6; ++k) { fdnLen[k] = W(kFdnMs[k] * ms); fdnG[k] = loopGain(fdnLen[k]); }
        static constexpr std::array<float, 4> kCombMs{11.3f, 13.7f, 16.1f, 19.3f};
        std::array<float, 4> combLen{}, combG{};
        for (std::size_t k = 0; k < 4; ++k) { combLen[k] = W(kCombMs[k] * ms); combG[k] = loopGain(combLen[k]); }
        static constexpr std::array<float, 6> kRingAp{3.1f, 4.7f, 5.3f, 6.1f, 7.3f, 8.9f}, kRingD{17.3f, 21.1f, 23.9f, 27.7f, 31.1f, 35.3f};
        std::array<float, 6> ringA{}, ringD{}, ringG{};
        for (std::size_t k = 0; k < 6; ++k) { ringA[k] = W(kRingAp[k] * ms); ringD[k] = W(kRingD[k] * ms); ringG[k] = loopGain(ringA[k] + ringD[k]); }
        const float plateLoopL = (13.0f + 19.0f + 29.0f) * ms, plateLoopR = (14.0f + 21.0f + 31.0f) * ms;
        const float plateGL = loopGain(plateLoopL), plateGR = loopGain(plateLoopR);
        const std::array<float, 2> springLen{W(37.0f * ms), W(41.0f * ms)};
        const std::array<float, 2> springG{loopGain(springLen[0]), loopGain(springLen[1])};
        const float springBw = std::exp(-kit::kTau * 4500.0f / fs);
        static constexpr std::array<float, 4> kDiffMs{1.3f, 2.1f, 3.4f, 5.1f};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            const float ig = inG_.next(freeze ? 0.0f : 1.0f);
            const float pd = preS_.next(preT);
            pre_[0].write(xl * ig); pre_[1].write(xr * ig);
            float il = pre_[0].read(pd), ir = pre_[1].read(pd);
            if (diffG > 0.0f) for (std::size_t k = 0; k < 4; ++k) { il = diff_[0][k].tick(il, diffG, W(kDiffMs[k] * ms)); ir = diff_[1][k].tick(ir, diffG, W(kDiffMs[k] * ms * 1.11f)); }
            // early reflections (from the pre-delayed input)
            er_[0].write(il); er_[1].write(ir);
            float el = 0, erv = 0;
            const bool needEr = erG > 0.0f || algo == 2;
            if (needEr) {
                float sg = 1.0f, gk = 0.9f;
                const int taps = algo == 2 ? 12 : 8;
                for (int k = 0; k < taps; ++k) {
                    const float t = std::min(kErMs[static_cast<std::size_t>(k)] * ms, 0.6f * fs);
                    el += sg * gk * er_[0].read(t); erv += sg * gk * er_[1].read(t * 1.07f);
                    sg = -sg; gk *= 0.86f;
                }
                el *= 0.35f; erv *= 0.35f;
            }
            modPh_ += static_cast<double>(modRate) / fs_; modPh_ -= std::floor(modPh_);
            const float mod = modDepth * std::sin(kit::kTau * static_cast<float>(modPh_));
            float tl = 0, tr = 0;
            switch (algo) {
                case 0: {   // HALL: 6 lines through a Householder matrix
                    std::array<float, 6> y{};
                    float sum = 0;
                    for (std::size_t k = 0; k < 6; ++k) {
                        const float m = (k % 2 ? -mod : mod) * (0.5f + 0.1f * static_cast<float>(k));
                        float v = fdn_[k].read(std::max(1.0f, fdnLen[k] - 1.0f + m));
                        lp_[k] = v + dampC * (lp_[k] - v);
                        y[k] = lp_[k] * fdnG[k];
                        sum += y[k];
                    }
                    const float hh = sum * (2.0f / 6.0f);
                    for (std::size_t k = 0; k < 6; ++k) fdn_[k].write(y[k] - hh + (k % 2 ? ir : il) * 0.5f);
                    tl = 0.6f * (y[0] + y[2] + y[4]); tr = 0.6f * (y[1] + y[3] + y[5]);
                    break;
                }
                case 1: {   // PLATE: two loops of nested all-passes, each feeding the other
                    float a = il + plateFb_[1];
                    a = plateN_[0][0].tick(a, 0.6f, W(13.0f * ms), 0.5f, W(4.1f * ms));
                    a = plateN_[0][1].tick(a, 0.6f, W(19.0f * ms), 0.5f, W(5.3f * ms));
                    float b = ir + plateFb_[0];
                    b = plateN_[1][0].tick(b, 0.6f, W(14.0f * ms), 0.5f, W(4.4f * ms));
                    b = plateN_[1][1].tick(b, 0.6f, W(21.0f * ms), 0.5f, W(5.9f * ms));
                    const float da = plateD_[0].read(std::max(1.0f, W(29.0f * ms) - 1.0f + mod)), db = plateD_[1].read(std::max(1.0f, W(31.0f * ms) - 1.0f - mod));
                    plateD_[0].write(a); plateD_[1].write(b);
                    plateLp_[0] = da + dampC * (plateLp_[0] - da); plateLp_[1] = db + dampC * (plateLp_[1] - db);
                    plateFb_[0] = plateLp_[0] * plateGL; plateFb_[1] = plateLp_[1] * plateGR;
                    tl = 0.7f * (a + da); tr = 0.7f * (b + db);
                    break;
                }
                case 2: {   // ROOM: its own reflections plus four short damped combs a side
                    float cl = 0, cr = 0;
                    for (std::size_t k = 0; k < 4; ++k) {
                        const float yl = comb_[0][k].read(std::max(0.0f, combLen[k] - 1.0f)), yr = comb_[1][k].read(std::max(0.0f, W(combLen[k] * 1.04f) - 1.0f));
                        combLp_[0][k] = yl + dampC * (combLp_[0][k] - yl); combLp_[1][k] = yr + dampC * (combLp_[1][k] - yr);
                        comb_[0][k].write(il * 0.5f + el * 0.5f + combG[k] * combLp_[0][k]);
                        comb_[1][k].write(ir * 0.5f + erv * 0.5f + combG[k] * combLp_[1][k]);
                        cl += yl; cr += yr;
                    }
                    tl = 0.3f * cl + 0.5f * el; tr = 0.3f * cr + 0.5f * erv;
                    break;
                }
                case 3: {   // CHAMBER: a ring of six all-pass + delay stages
                    float x = ring_[5].last;
                    std::array<float, 6> o{};
                    for (std::size_t k = 0; k < 6; ++k) {
                        if (k == 0) x += il;
                        if (k == 3) x += ir;
                        auto& s = ring_[k];
                        float v = s.ap.tick(x, 0.65f, ringA[k]);
                        const float y = s.d.read(std::max(0.0f, ringD[k] - 1.0f + (k % 2 ? mod : -mod)));
                        s.d.write(v);
                        s.lp = y + dampC * (s.lp - y);
                        s.last = s.lp * ringG[k];
                        o[k] = s.last;
                        x = s.last;
                    }
                    tl = 0.5f * (o[0] - o[2] + o[4]); tr = 0.5f * (o[1] - o[3] + o[5]);
                    break;
                }
                default: {   // SPRING: a long chain of first-order all-passes inside each loop (dispersion)
                    std::array<float, 2> in{il, ir}, outv{};
                    for (std::size_t c = 0; c < 2; ++c) {
                        float v = in[c] + springFb_[c];
                        for (auto& a : springAp_[c]) {   // first-order all-pass, coefficient 0.6
                            const float y = -0.6f * v + a.x1 + 0.6f * a.y1;
                            a.x1 = v; a.y1 = y; v = y;
                        }
                        springBw_[c] = v + springBw * (springBw_[c] - v);   // the spring's limited bandwidth
                        const float d = springD_[c].read(std::max(1.0f, springLen[c] - 1.0f + (c ? -mod : mod)));
                        springD_[c].write(springBw_[c]);
                        springLp_[c] = d + dampC * (springLp_[c] - d);
                        springFb_[c] = springLp_[c] * springG[c];
                        outv[c] = d;
                    }
                    tl = 0.8f * outv[0]; tr = 0.8f * outv[1];
                    break;
                }
            }
            tl += erG * el; tr += erG * erv;   // the EARLY REFLECTIONS knob, on every type
            if (hpOn_) { tl = hp_.l.tick(tl); tr = hp_.r.tick(tr); }
            if (hcOn_) { tl = hc_.l.tick(tl); tr = hc_.r.tick(tr); }
            const float mm = 0.5f * (tl + tr), ss = 0.5f * (tl - tr) * w;
            tl = mm + ss; tr = mm - ss;
            const float mx = mix_.next(mixT), o = out_.next(oT);
            L[i] = kit::sane((xl + mx * (tl - xl)) * o);
            R[i] = kit::sane((xr + mx * (tr - xr)) * o);
            inPk = std::max(inPk, std::max(std::fabs(xl), std::fabs(xr)));
            outPk = std::max(outPk, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
        inPk_ = inPk; outPk_ = outPk;
        publishAll(n);
    }

private:
    void meters(const float* L, const float* R, std::uint32_t n, const float*, const float*) noexcept {
        float pk = 0;
        for (std::uint32_t i = 0; i < n; ++i) pk = std::max(pk, std::max(std::fabs(L[i]), std::fabs(R[i])));
        inPk_ = outPk_ = pk;
        publishAll(n);
    }
    void publishAll(std::uint32_t n) noexcept {
        const float c = std::pow(hc::fx::onePoleCoef(fs_, 300.0), static_cast<float>(n));
        vuI_ = inPk_ * 0.707f + c * (vuI_ - inPk_ * 0.707f); vuO_ = outPk_ * 0.707f + c * (vuO_ - outPk_ * 0.707f);
        publishMeter(0, kit::gainToDb(inPk_)); publishMeter(1, kit::gainToDb(outPk_));
        publishMeter(2, std::clamp(kit::gainToDb(vuI_) + 18.0f, -20.0f, 3.0f)); publishMeter(3, std::clamp(kit::gainToDb(vuO_) + 18.0f, -20.0f, 3.0f));
    }
    void setEq() noexcept {
        const float lc = p(kLowCut), hcf = p(kHighCut), key = lc * 1e5f + hcf;
        if (key == key_) return;
        key_ = key;
        if (lc > 20.01f) hpOn_ = true;
        if (hcf < 19999.0f) hcOn_ = true;
        hp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, lc, 0.7071));
        hc_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(hcf, fs_ * 0.45), 0.7071));
    }
    struct Stage { Ap ap; hc::fx::DelayLine d; float lp = 0, last = 0; };
    struct Ap1 { float x1 = 0, y1 = 0; };
    double fs_ = 48000;
    std::array<hc::fx::DelayLine, 2> pre_{}, er_{}, plateD_{}, springD_{};
    std::array<std::array<Ap, 4>, 2> diff_{};
    std::array<hc::fx::DelayLine, 6> fdn_{};
    std::array<std::array<Nested, 2>, 2> plateN_{};
    std::array<std::array<hc::fx::DelayLine, 4>, 2> comb_{};
    std::array<Stage, 6> ring_{};
    std::array<std::array<Ap1, 24>, 2> springAp_{};
    std::array<float, 6> lp_{};
    std::array<std::array<float, 4>, 2> combLp_{};
    std::array<float, 2> plateLp_{}, plateFb_{}, springLp_{}, springBw_{}, springFb_{};
    hc::fx::Biquad2 hp_, hc_;
    hc::fx::Smoothed mix_, out_, inG_, preS_;
    double modPh_ = 0;
    float key_ = -1, vuI_ = 0, vuO_ = 0, inPk_ = 0, outPk_ = 0;
    bool hpOn_ = false, hcOn_ = false, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeHemReverb() { return std::make_unique<HemReverb>(); }

} // namespace hem::staging
