// =============================================================================
//  27 DEPTHMATRIX — a routable space workstation: four delays (DUAL ECHO, TRIGGER ECHO, SLAPBACK,
//  ENSEMBLE) and four reverbs (PLATE, ROOM, HALL, HUGE), a 4 x 5 send grid and an 8-strip mixer.
//    DUAL ECHO     two independent echoes (TIME 1 left, TIME 2 right + OFFSET), feedback, mod.
//    TRIGGER ECHO  an echo that only records while it is triggered: AUTO opens on each hit (a
//                  transient detector), MANUAL opens while the TRIGGER button is held.
//    SLAPBACK      a tape slap: VSO 7.5 / 15 / 30 IPS doubles / keeps / halves the time and gets
//                  darker and wobblier at the slower speeds.
//    ENSEMBLE      three slightly spread, moving taps per side (0 % = one plain tap); NARROW keeps
//                  the sides moving together, WIDE moves them apart.
//    Reverbs: each its own size and density, with PREDELAY, RT, RT LOW (x), RT HIGH (x); HUGE DECAY
//    stretches the HUGE tail up to five times.
//  Routing: the input feeds all four delays and all four reverbs; each delay also sends to each
//  reverb (default -inf) and to its own mixer strip (OUTPUT, default 0 dB). Every strip has a fader,
//  HP, LP, EN, SOLO and MUTE; the eight strips sum to the wet signal.
//  The reverbs are written for this plugin only (a four-line network with three-band decay).
//  Dead flat on load: WET / DRY 0 %, INPUT and OUTPUT 0 dB. LOCK MIX is a preset feature (it keeps
//  the wet / dry setting when presets change); it does not change the sound.
//  ASSUMED: default times and decays; TRIGGER ECHO hit = fast level 6 dB over the slow level,
//  held 80 ms; SLAPBACK tone 5 / 9 / 15 kHz; ENSEMBLE spread up to +-6 %.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

constexpr int kDelays = 4, kVerbs = 4, kStrips = 8, kDests = 5;
// parameter blocks
constexpr int kDBase = 0, kDSize = 8;                       // per delay: time1, time2, sync, division, feedback, offset, mod depth, mod rate
constexpr int kXBase = kDBase + kDelays * kDSize;           // 32: trigger enable, trigger mode, trigger, slap VSO, ensemble width, ensemble amount
constexpr int kTrigEn = kXBase, kTrigMode = kXBase + 1, kTrigBtn = kXBase + 2, kVso = kXBase + 3, kEnsWide = kXBase + 4, kEnsAmt = kXBase + 5;
constexpr int kRBase = kXBase + 6, kRSize = 4;              // per reverb: predelay, rt, rt low, rt high
constexpr int kHugeDecay = kRBase + kVerbs * kRSize;        // 54
constexpr int kSBase = kHugeDecay + 1;                       // 55: sends [delay][dest]
constexpr int kMBase = kSBase + kDelays * kDests, kMSize = 6;   // 75: per strip: fader, hp, lp, en, solo, mute
constexpr int kInput = kMBase + kStrips * kMSize;           // 123
constexpr int kOutput = kInput + 1, kMix = kInput + 2, kLockMix = kInput + 3, kBypass = kInput + 4, kCount = kInput + 5;

const char* const kDivN[] = {"1/32", "1/16", "1/16 D", "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D", "1/2", "1/1"};
constexpr std::array<float, 11> kDivBeats{0.125f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 4.0f};
const char* const kModeN[] = {"AUTO", "MANUAL"};
const char* const kVsoN[] = {"7.5 IPS", "15 IPS", "30 IPS"};
const char* const kEnsN[] = {"NARROW", "WIDE"};

#define HEM_DLY(id, nm, t1, t2)                                                       \
    {id "_time1", nm " TIME 1", "ms", 1.0f, 2000.0f, t1, Taper::Log},                \
    {id "_time2", nm " TIME 2", "ms", 1.0f, 2000.0f, t2, Taper::Log},                \
    {id "_sync", nm " SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},                    \
    {id "_division", nm " DIVISION", "", 0.0f, 10.0f, 7.0f, Taper::Choice, kDivN},    \
    {id "_feedback", nm " FEEDBACK", "%", 0.0f, 95.0f, 0.0f},                         \
    {id "_offset", nm " OFFSET", "ms", 0.0f, 50.0f, 0.0f},                            \
    {id "_mod_depth", nm " MOD DEPTH", "%", 0.0f, 100.0f, 0.0f},                      \
    {id "_mod_rate", nm " MOD RATE", "Hz", 0.05f, 10.0f, 0.5f, Taper::Log}
#define HEM_RV(id, nm, rt)                                                            \
    {id "_predelay", nm " PREDELAY", "ms", 0.0f, 250.0f, 0.0f},                       \
    {id "_rt", nm " RT", "s", 0.2f, 20.0f, rt, Taper::Log},                           \
    {id "_rt_low", nm " RT LOW", "x", 0.5f, 2.0f, 1.0f},                              \
    {id "_rt_high", nm " RT HIGH", "x", 0.1f, 1.0f, 0.6f}
#define HEM_SEND(d, dn, r, rn, def) {d "_to_" r, dn " TO " rn, "dB", -60.0f, 6.0f, def}
#define HEM_SENDS(d, dn)                                                              \
    HEM_SEND(d, dn, "plate", "PLATE", -60.0f), HEM_SEND(d, dn, "room", "ROOM", -60.0f),  \
    HEM_SEND(d, dn, "hall", "HALL", -60.0f), HEM_SEND(d, dn, "huge", "HUGE", -60.0f),    \
    HEM_SEND(d, dn, "out", "OUTPUT", 0.0f)
#define HEM_STRIP(id, nm)                                                             \
    {id "_fader", nm " FADER", "dB", -60.0f, 12.0f, 0.0f},                            \
    {id "_hp", nm " HP", "Hz", 20.0f, 2000.0f, 20.0f, Taper::Log},                    \
    {id "_lp", nm " LP", "Hz", 500.0f, 20000.0f, 20000.0f, Taper::Log},               \
    {id "_en", nm " EN", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},                        \
    {id "_solo", nm " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},                    \
    {id "_mute", nm " MUTE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}

const ParamInfo kParams[kCount] = {
    HEM_DLY("dual", "DUAL ECHO", 300.0f, 450.0f), HEM_DLY("trigger", "TRIGGER ECHO", 250.0f, 250.0f),
    HEM_DLY("slap", "SLAPBACK", 90.0f, 110.0f), HEM_DLY("ensemble", "ENSEMBLE", 40.0f, 60.0f),
    {"trigger_enable", "TRIGGER ECHO ENABLE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"trigger_mode", "TRIGGER MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN},
    {"trigger", "TRIGGER", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"slap_vso", "SLAPBACK VSO", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kVsoN},
    {"ensemble_width", "ENSEMBLE WIDTH", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kEnsN},
    {"ensemble_amount", "ENSEMBLE AMOUNT", "%", 0.0f, 100.0f, 0.0f},
    HEM_RV("plate", "PLATE", 1.8f), HEM_RV("room", "ROOM", 0.9f), HEM_RV("hall", "HALL", 2.5f), HEM_RV("huge", "HUGE", 6.0f),
    {"huge_decay", "HUGE DECAY", "%", 0.0f, 100.0f, 0.0f},
    HEM_SENDS("dual", "DUAL ECHO"), HEM_SENDS("trigger", "TRIGGER ECHO"), HEM_SENDS("slap", "SLAPBACK"), HEM_SENDS("ensemble", "ENSEMBLE"),
    HEM_STRIP("mix_dual", "DUAL ECHO"), HEM_STRIP("mix_trigger", "TRIGGER ECHO"), HEM_STRIP("mix_slap", "SLAPBACK"), HEM_STRIP("mix_ensemble", "ENSEMBLE"),
    HEM_STRIP("mix_plate", "PLATE"), HEM_STRIP("mix_room", "ROOM"), HEM_STRIP("mix_hall", "HALL"), HEM_STRIP("mix_huge", "HUGE"),
    {"input", "INPUT", "dB", -24.0f, 12.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 12.0f, 0.0f},
    {"mix", "WET / DRY", "%", 0.0f, 100.0f, 0.0f},
    {"lock_mix", "LOCK MIX", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_DLY
#undef HEM_RV
#undef HEM_SEND
#undef HEM_SENDS
#undef HEM_STRIP
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "dBFS", -60, 0}, {"OUTPUT", "dBFS", -60, 0},
    {"DUAL ECHO", "dBFS", -60, 0}, {"TRIGGER ECHO", "dBFS", -60, 0}, {"SLAPBACK", "dBFS", -60, 0}, {"ENSEMBLE", "dBFS", -60, 0},
    {"PLATE", "dBFS", -60, 0}, {"ROOM", "dBFS", -60, 0}, {"HALL", "dBFS", -60, 0}, {"HUGE", "dBFS", -60, 0}};

// ---- the reverb: four delay lines, an orthogonal mix, a passive three-band decay per line ----
struct Verb {
    struct Ap {
        hc::fx::DelayLine d;
        float tick(float x, float g, float len) noexcept { const float v = d.read(std::max(0.0f, len - 1.0f)); const float w = x + g * v; d.write(w); return v - g * w; }
    };
    std::array<hc::fx::DelayLine, 4> line{};
    std::array<hc::fx::DelayLine, 2> pre{};
    std::array<std::array<Ap, 3>, 2> diff{};
    std::array<float, 4> s1{}, s2{}, last{};
    double ph = 0;
    void prepare(double fs) {
        for (auto& l : line) l.prepare(static_cast<std::uint32_t>(fs * 0.15));
        for (auto& p : pre) p.prepare(static_cast<std::uint32_t>(fs * 0.26));
        for (auto& c : diff) for (auto& a : c) a.d.prepare(static_cast<std::uint32_t>(fs * 0.01));
        clear();
    }
    void clear() noexcept {
        for (auto& l : line) l.clear();
        for (auto& p : pre) p.clear();
        for (auto& c : diff) for (auto& a : c) a.d.clear();
        s1.fill(0); s2.fill(0); last.fill(0); ph = 0;
    }
    struct Set {
        std::array<float, 4> len{}, gM{}, cL{}, cH{};
        float aLow = 0, aHigh = 0, diffG = 0.65f, diffScale = 1, pre = 0, wobble = 0;
    };
    void tick(float il, float ir, float& ol, float& orr, const Set& s, double fs) noexcept {
        pre[0].write(il); pre[1].write(ir);
        float xl = pre[0].read(s.pre), xr = pre[1].read(s.pre);
        static constexpr std::array<float, 3> kD{1.3f, 2.9f, 4.7f};
        for (std::size_t k = 0; k < 3; ++k) {
            const float len = kD[k] * 0.001f * static_cast<float>(fs) * s.diffScale;
            xl = diff[0][k].tick(xl, s.diffG, len);
            xr = diff[1][k].tick(xr, s.diffG, len * 1.07f);
        }
        ph += 0.2 / fs; ph -= std::floor(ph);
        std::array<float, 4> y{};
        for (std::size_t k = 0; k < 4; ++k) {
            const float w = s.wobble * std::sin(kit::kTau * static_cast<float>(ph + 0.25 * static_cast<double>(k)));
            float v = line[k].read(std::max(1.0f, s.len[k] - 1.0f + w));
            // low shelf then high shelf, both first order (monotonic), then the mid decay: never above the largest band gain
            s1[k] = v + s.aLow * (s1[k] - v);
            v += s.cL[k] * s1[k];
            s2[k] = v + s.aHigh * (s2[k] - v);
            v += s.cH[k] * (v - s2[k]);
            y[k] = v * s.gM[k];
        }
        // orthogonal 4 x 4 mix (Hadamard / 2)
        const float a = y[0] + y[1], b = y[0] - y[1], c = y[2] + y[3], d = y[2] - y[3];
        const std::array<float, 4> m{0.5f * (a + c), 0.5f * (b + d), 0.5f * (a - c), 0.5f * (b - d)};
        line[0].write(m[0] + xl); line[1].write(m[1] - xl); line[2].write(m[2] + xr); line[3].write(m[3] - xr);
        ol = 0.5f * (y[0] + y[1]); orr = 0.5f * (y[2] - y[3]);
    }
};

class DepthMatrix final : public hc::fx::Effect {
public:
    DepthMatrix() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i < kXBase) {
            const int d = i / kDSize, k = i % kDSize;
            const bool sync = p(kDBase + d * kDSize + 2) > 0.5f;
            if (k == 0 || k == 1) return !sync;
            if (k == 3) return sync;
            if (k == 7) return p(kDBase + d * kDSize + 6) > 0.0f;
        }
        if (i == kTrigMode) return p(kTrigEn) > 0.5f;
        if (i == kTrigBtn) return p(kTrigEn) > 0.5f && p(kTrigMode) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (int d = 0; d < kDelays; ++d) for (auto& l : dl_[static_cast<std::size_t>(d)]) l.prepare(static_cast<std::uint32_t>(fs * (d == 2 ? 4.2 : 2.2)));
        for (auto& v : verb_) v.prepare(fs);
        for (auto& s : send_) s.prepare(fs, 20.0, 0.0f);
        for (auto& s : strip_) s.prepare(fs, 20.0, 1.0f);
        for (auto& t : time_) t.prepare(fs, 80.0, 0.0f);
        in_.prepare(fs, 20.0, 1.0f); out_.prepare(fs, 20.0, 1.0f); mix_.prepare(fs, 20.0, 0.0f); gate_.prepare(fs, 2.0, 1.0f);
        for (auto& m : m_) m.prepare(fs);
        primed_ = false;
        reset();
    }
    void reset() noexcept override {
        for (auto& d : dl_) for (auto& l : d) l.clear();
        for (auto& v : verb_) v.clear();
        for (auto& f : hp_) f.clear();
        for (auto& f : lp_) f.clear();
        for (auto& f : slapLp_) f.clear();
        lfo_.fill(0); fast_ = slow_ = 0; hold_ = 0; fKey_.fill(-1); slapKey_ = -1; filtOn_.fill(false);
        primed_ = false; loaded_ = false;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float inT = kit::dbToGain(p(kInput)), oT = kit::dbToGain(p(kOutput)), mixT = p(kMix) * 0.01f;
        if (!loaded_) { in_.cur = inT; out_.cur = oT; mix_.cur = mixT; loaded_ = true; }   // a loaded session starts where it was saved
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        m_[0].feed(L, n); m_[0].feed(R, n);
        publishMeter(0, m_[0].db());
        if (!wet) {
            const bool flat = kit::isFlat(inT, 1.0f) && kit::isFlat(oT, 1.0f) && kit::isFlat(in_.cur, 1.0f) && kit::isFlat(out_.cur, 1.0f);
            if (!flat) for (std::uint32_t i = 0; i < n; ++i) { const float g = in_.next(inT) * out_.next(oT); L[i] *= g; R[i] *= g; }
            m_[1].feed(L, n); m_[1].feed(R, n);
            publishMeter(1, m_[1].db());
            return;
        }
        const float fs = static_cast<float>(fs_);
        // ---- per-block settings ----
        std::array<std::array<float, 2>, kDelays> tT{};
        std::array<float, kDelays> fb{}, dev{}, rate{};
        const std::array<float, 3> kVsoMul{2.0f, 1.0f, 0.5f};
        const int vso = static_cast<int>(p(kVso));
        for (int d = 0; d < kDelays; ++d) {
            const int b = kDBase + d * kDSize;
            float t1 = p(b), t2 = p(b + 1);
            if (p(b + 2) > 0.5f) { t1 = t2 = kDivBeats[static_cast<std::size_t>(p(b + 3))] * 60000.0f / std::max(tempo(), 20.0f); }
            t2 += p(b + 5);
            float mul = d == 2 ? kVsoMul[static_cast<std::size_t>(vso)] : 1.0f;
            const float maxMs = d == 2 ? 4000.0f : 2100.0f;
            tT[static_cast<std::size_t>(d)] = {std::clamp(t1 * mul, 1.0f, maxMs) * 0.001f * fs, std::clamp(t2 * mul, 1.0f, maxMs) * 0.001f * fs};
            fb[static_cast<std::size_t>(d)] = p(b + 4) * 0.01f;
            dev[static_cast<std::size_t>(d)] = p(b + 6) * 0.01f * 0.003f * fs;
            rate[static_cast<std::size_t>(d)] = p(b + 7);
        }
        const float slapHz = std::array<float, 3>{5000.0f, 9000.0f, 15000.0f}[static_cast<std::size_t>(vso)];
        if (slapHz != slapKey_) { slapKey_ = slapHz; for (auto& f : slapLp_) f.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(slapHz, fs_ * 0.45), 0.7071); }
        const float slapWob = std::array<float, 3>{0.0012f, 0.0006f, 0.0003f}[static_cast<std::size_t>(vso)] * fs;
        const float ens = p(kEnsAmt) * 0.01f;
        const bool ensWide = p(kEnsWide) > 0.5f;
        const bool trigOn = p(kTrigEn) > 0.5f, trigManual = p(kTrigMode) > 0.5f, trigBtn = p(kTrigBtn) > 0.5f;
        // reverbs
        std::array<Verb::Set, kVerbs> vs{};
        static constexpr std::array<std::array<float, 4>, 4> kLenMs{{{7.9f, 11.3f, 13.7f, 17.1f}, {13.0f, 17.0f, 23.0f, 29.0f}, {37.0f, 47.0f, 61.0f, 73.0f}, {71.0f, 89.0f, 107.0f, 131.0f}}};
        static constexpr std::array<float, 4> kDiffG{0.75f, 0.6f, 0.65f, 0.7f}, kDiffS{0.6f, 0.5f, 1.0f, 1.4f};
        const float aLow = std::exp(-kit::kTau * 300.0f / fs), aHigh = std::exp(-kit::kTau * 4000.0f / fs);
        for (int r = 0; r < kVerbs; ++r) {
            const int b = kRBase + r * kRSize;
            auto& s = vs[static_cast<std::size_t>(r)];
            float rt = p(b + 1);
            if (r == 3) rt *= 1.0f + 4.0f * p(kHugeDecay) * 0.01f;
            const float rtL = rt * p(b + 2), rtH = rt * p(b + 3);
            for (std::size_t k = 0; k < 4; ++k) {
                s.len[k] = kLenMs[static_cast<std::size_t>(r)][k] * 0.001f * fs;
                const float gm = std::min(0.995f, std::pow(10.0f, -3.0f * s.len[k] / (fs * rt)));
                const float gl = std::min(0.995f, std::pow(10.0f, -3.0f * s.len[k] / (fs * rtL)));
                const float gh = std::min(gm, std::pow(10.0f, -3.0f * s.len[k] / (fs * rtH)));
                s.gM[k] = gm; s.cL[k] = gl / gm - 1.0f; s.cH[k] = gh / gm - 1.0f;
            }
            s.aLow = aLow; s.aHigh = aHigh; s.diffG = kDiffG[static_cast<std::size_t>(r)]; s.diffScale = kDiffS[static_cast<std::size_t>(r)];
            s.pre = p(b) * 0.001f * fs; s.wobble = r == 3 ? 0.0005f * fs : 0.0f;
        }
        // strips: solo / mute / enable -> one gain each
        bool anySolo = false;
        for (int s = 0; s < kStrips; ++s) anySolo = anySolo || p(kMBase + s * kMSize + 4) > 0.5f;
        std::array<float, kStrips> stripT{};
        for (int s = 0; s < kStrips; ++s) {
            const int b = kMBase + s * kMSize;
            const bool live = p(b + 3) > 0.5f && p(b + 5) < 0.5f && (!anySolo || p(b + 4) > 0.5f);
            stripT[static_cast<std::size_t>(s)] = live ? kit::dbToGain(p(b) <= -59.9f ? -120.0f : p(b)) : 0.0f;
            const float hp = p(b + 1), lp = p(b + 2), key = hp * 100000.0f + lp;
            if (key != fKey_[static_cast<std::size_t>(s)]) {
                fKey_[static_cast<std::size_t>(s)] = key;
                if (hp > 20.01f || lp < 19999.0f) filtOn_[static_cast<std::size_t>(s)] = true;   // once used, keep running (no click on return)
                hp_[static_cast<std::size_t>(s)].set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 0.7071));
                lp_[static_cast<std::size_t>(s)].set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, fs_ * 0.45), 0.7071));
            }
        }
        std::array<float, kDelays * kDests> sendT{};
        for (int i = 0; i < kDelays * kDests; ++i) sendT[static_cast<std::size_t>(i)] = p(kSBase + i) <= -59.9f ? 0.0f : kit::dbToGain(p(kSBase + i));
        if (!primed_) {   // start at the set times, sends and strip gains (no glide from the defaults)
            for (int d = 0; d < kDelays; ++d) { time_[static_cast<std::size_t>(2 * d)].cur = tT[static_cast<std::size_t>(d)][0]; time_[static_cast<std::size_t>(2 * d + 1)].cur = tT[static_cast<std::size_t>(d)][1]; }
            for (std::size_t k = 0; k < send_.size(); ++k) send_[k].cur = sendT[k];
            for (std::size_t k = 0; k < strip_.size(); ++k) strip_[k].cur = stripT[k];
            primed_ = true;
        }
        const float fA = hc::fx::onePoleCoef(fs_, 1.0), fR = hc::fx::onePoleCoef(fs_, 30.0), sA = hc::fx::onePoleCoef(fs_, 30.0), sR = hc::fx::onePoleCoef(fs_, 300.0);
        const auto holdLen = static_cast<std::uint32_t>(0.08 * fs_);
        std::array<float, kStrips> lvl{};

        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(inT);
            const float xl = L[i] * g, xr = R[i] * g;
            // TRIGGER ECHO gate
            const float a = 0.5f * (std::fabs(xl) + std::fabs(xr));
            fast_ = a + (a > fast_ ? fA : fR) * (fast_ - a);
            slow_ = a + (a > slow_ ? sA : sR) * (slow_ - a);
            if (!trigManual && fast_ > 2.0f * slow_ && fast_ > 0.003f) hold_ = holdLen;
            const bool open = !trigOn || (trigManual ? trigBtn : hold_ > 0);
            if (hold_ > 0) --hold_;
            const float gate = gate_.next(open ? 1.0f : 0.0f);
            // ---- delays ----
            std::array<std::array<float, 2>, kDelays> y{};
            for (int d = 0; d < kDelays; ++d) {
                const auto di = static_cast<std::size_t>(d);
                auto& ln = dl_[di];
                lfo_[di] += static_cast<double>(rate[di]) / fs_; lfo_[di] -= std::floor(lfo_[di]);
                const float ph = static_cast<float>(lfo_[di]);
                const float t0 = time_[2 * di].next(tT[di][0]), t1 = time_[2 * di + 1].next(tT[di][1]);
                const float mo = dev[di] * std::sin(kit::kTau * ph);
                float yl = 0, yr = 0, il = xl, ir = xr;
                if (d == 3) {   // ENSEMBLE: three spread, moving taps per side
                    for (int k = -1; k <= 1; ++k) {
                        const float kf = static_cast<float>(k);
                        const float phk = ph + (kf + 1.0f) / 3.0f;
                        const float mk = ens * 0.0015f * fs * std::sin(kit::kTau * phk), mr = ens * 0.0015f * fs * std::sin(kit::kTau * (phk + (ensWide ? 0.5f : 0.0f)));
                        yl += ln[0].read(std::max(0.0f, t0 * (1.0f + 0.06f * ens * kf) - 1.0f + mo + mk));
                        yr += ln[1].read(std::max(0.0f, t1 * (1.0f - 0.06f * ens * kf) - 1.0f + mo + mr));
                    }
                    yl *= 1.0f / 3.0f; yr *= 1.0f / 3.0f;
                } else {
                    const float w = d == 2 ? slapWob * std::sin(kit::kTau * slapPh_) : 0.0f;   // tape wow on the slap
                    yl = ln[0].read(std::max(0.0f, t0 - 1.0f + mo + w));
                    yr = ln[1].read(std::max(0.0f, t1 - 1.0f + mo - w));
                }
                if (d == 2) {   // SLAPBACK: mono tape send, darker repeats
                    yl = slapLp_[0].tick(yl); yr = slapLp_[1].tick(yr);
                    il = ir = 0.5f * (xl + xr);
                }
                if (d == 3) il = ir = 0.5f * (xl + xr);
                if (d == 1) { il *= gate; ir *= gate; }
                ln[0].write(il + fb[di] * yl); ln[1].write(ir + fb[di] * yr);
                y[di] = {yl, yr};
            }
            slapPh_ += 0.5f / fs; slapPh_ -= std::floor(slapPh_);
            // ---- reverbs: the input plus whatever the grid sends them ----
            std::array<std::array<float, 2>, kVerbs> rv{};
            for (int r = 0; r < kVerbs; ++r) {
                float sl = xl, sr = xr;
                for (int d = 0; d < kDelays; ++d) {
                    const float s = send_[static_cast<std::size_t>(d * kDests + r)].next(sendT[static_cast<std::size_t>(d * kDests + r)]);
                    sl += s * y[static_cast<std::size_t>(d)][0]; sr += s * y[static_cast<std::size_t>(d)][1];
                }
                verb_[static_cast<std::size_t>(r)].tick(sl, sr, rv[static_cast<std::size_t>(r)][0], rv[static_cast<std::size_t>(r)][1], vs[static_cast<std::size_t>(r)], fs_);
            }
            // ---- mixer ----
            float wl = 0, wr = 0;
            for (int s = 0; s < kStrips; ++s) {
                const auto si = static_cast<std::size_t>(s);
                float cl, cr;
                if (s < kDelays) {
                    const float o = send_[si * kDests + 4].next(sendT[si * kDests + 4]);
                    cl = y[si][0] * o; cr = y[si][1] * o;
                } else { cl = rv[si - kDelays][0]; cr = rv[si - kDelays][1]; }
                if (filtOn_[si]) { cl = lp_[si].l.tick(hp_[si].l.tick(cl)); cr = lp_[si].r.tick(hp_[si].r.tick(cr)); }
                const float sg = strip_[si].next(stripT[si]);
                cl *= sg; cr *= sg;
                lvl[si] = std::max(lvl[si], std::max(std::fabs(cl), std::fabs(cr)));
                wl += cl; wr += cr;
            }
            const float mx = mix_.next(mixT), o = out_.next(oT);
            L[i] = kit::sane((xl + mx * (wl - xl)) * o);
            R[i] = kit::sane((xr + mx * (wr - xr)) * o);
        }
        m_[1].feed(L, n); m_[1].feed(R, n);
        publishMeter(1, m_[1].db());
        for (int s = 0; s < kStrips; ++s) publishMeter(2 + s, kit::gainToDb(lvl[static_cast<std::size_t>(s)]));
    }

private:
    double fs_ = 48000;
    std::array<std::array<hc::fx::DelayLine, 2>, kDelays> dl_{};
    std::array<Verb, kVerbs> verb_{};
    std::array<hc::fx::Smoothed, kDelays * kDests> send_{};
    std::array<hc::fx::Smoothed, kStrips> strip_{};
    std::array<hc::fx::Smoothed, kDelays * 2> time_{};
    hc::fx::Smoothed in_, out_, mix_, gate_;
    std::array<hc::fx::Biquad2, kStrips> hp_{}, lp_{};
    std::array<hc::fx::Biquad, 2> slapLp_{};
    std::array<double, kDelays> lfo_{};
    std::array<float, kStrips> fKey_{};
    std::array<bool, kStrips> filtOn_{};
    float fast_ = 0, slow_ = 0, slapKey_ = -1, slapPh_ = 0;
    std::uint32_t hold_ = 0;
    bool primed_ = false, loaded_ = false;
    std::array<kit::PeakMeter, 2> m_{};
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeDepthMatrix() { return std::make_unique<DepthMatrix>(); }

} // namespace hem::staging
