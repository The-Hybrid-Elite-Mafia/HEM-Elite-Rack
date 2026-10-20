// SWITCHBLADE — the channel compander: compressor / expander, gate / expander, filter (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace switchblade {
namespace {
using namespace arsenal;

const char* const kRefN[] = {"LOW", "PEAK"};
const char* const kEqN[] = {"WIDE", "SIDECHAIN", "SPLIT"};
const char* const kGateModeN[] = {"GATE", "EXPANDER"};
const char* const kTypeN[] = {"BAND", "NOTCH", "LO CUT", "HI CUT"};
const char* const kKeyN[] = {"STEREO", "L > R", "R > L"};
const char* const kMonitorN[] = {"AUDIO", "SIDECHAIN", "PASSIVE"};
const ParamInfo kParams[kCount] = {
    {"compon", "Comp On", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"levelref", "Level Ref", "", 0, 1, 0, Taper::Choice, kRefN},
    {"makeup", "Makeup", "dB", -40.0f, 40.0f, 0.0f},
    {"threshold", "Threshold", "dB", -100.0f, 0.0f, -18.0f},
    {"ratio", "Ratio", ":1", 0.5f, 50.0f, 3.0f, Taper::Log},
    {"cancel", "Cancel", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"attack", "Attack", "ms", 0.01f, 1000.0f, 5.0f, Taper::Log},
    {"release", "Release", "ms", 1.0f, 10000.0f, 120.0f, Taper::Log},
    {"pdr", "PDR", "ms", 0.0f, 1000.0f, 0.0f},
    {"compeq", "Comp EQ", "", 0, 2, 0, Taper::Choice, kEqN},
    {"gateon", "Gate On", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"gatemode", "Gate Mode", "", 0, 1, 0, Taper::Choice, kGateModeN},
    {"floor", "Floor", "dB", -80.0f, 12.0f, -80.0f},
    {"expslope", "Expander Slope", ":1", 2.0f, 10.0f, 2.0f},
    {"gateopen", "Gate Open", "dB", -100.0f, 0.0f, -60.0f},
    {"gateclose", "Gate Close", "dB", -100.0f, 0.0f, -64.0f},
    {"gateattack", "Gate Attack", "ms", 0.01f, 1000.0f, 0.5f, Taper::Log},
    {"gaterelease", "Gate Release", "ms", 1.0f, 10000.0f, 150.0f, Taper::Log},
    {"gatehold", "Gate Hold", "ms", 0.01f, 5000.0f, 20.0f, Taper::Log},
    {"gateeq", "Gate EQ", "", 0, 2, 0, Taper::Choice, kEqN},
    {"link", "Link", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"filtertype", "Filter", "", 0, 3, 2, Taper::Choice, kTypeN},
    {"filterfreq", "Filter Freq", "Hz", 16.0f, 21000.0f, 200.0f, Taper::Log},
    {"filterq", "Filter Q", "", 0.1f, 0.6f, 0.5f},
    {"lookahead", "Lookahead", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"key", "Key", "", 0, 2, 0, Taper::Choice, kKeyN},
    {"monitor", "Monitor", "", 0, 2, 0, Taper::Choice, kMonitorN},
    {"inputl", "Input Left", "dB", -24.0f, 0.0f, 0.0f},
    {"inputr", "Input Right", "dB", -24.0f, 0.0f, 0.0f},
    {"output", "Output", "dB", -40.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"COMP", "dB", -40.0f, 0.0f}, {"GATE", "dB", -80.0f, 0.0f}, {"INPUT", "dB", -60.0f, 0.0f}};

// the compressor's static curve (dB): RATIO over the threshold (with CANCEL the slope goes past flat),
// a 4 dB soft knee; between -80 and +24 dB
float compDb(float lv, float thr, float ratio, bool cancel) noexcept {
    const float r = cancel ? -std::max(ratio, 5.0f) : ratio;
    const float slope = 1.0f / r - 1.0f, over = lv - thr;
    constexpr float K = 4.0f;
    float oe;
    if (over <= -K * 0.5f) return 0.0f;
    if (over >= K * 0.5f) oe = over;
    else { const float k = over + K * 0.5f; oe = k * k / (2.0f * K); }
    return std::clamp(slope * oe, -80.0f, 24.0f);
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLookahead; }
    bool paramActive(int i) const noexcept override {
        const bool comp = p(CompOn) >= 0.5f, gate = p(GateOn) >= 0.5f, link = p(Link) >= 0.5f, isGate = p(GateMode) < 0.5f;
        if (i >= LevelRef && i <= CompEq) return comp;
        if (i == GateClose || i == GateHold) return gate && isGate;
        if (i == ExpSlope) return gate && !isGate;
        if (i == GateAttack || i == GateRelease || i == GateEq) return gate && !link;
        if (i >= GateMode && i <= GateEq) return gate;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        act_.prepare(kLookahead);
        pas_.prepare(kLookahead);
        reset();
    }
    void reset() noexcept override {
        act_.clear();
        pas_.clear();
        for (auto& f : filt_) f.clear();
        design_ = {-1, -1, -1};
        envC_ = envG_ = 0.0f;
        gr_ = gate_ = 0.0f;
        open_ = false;
        holdLeft_ = 0;
        eventN_ = lastEventN_ = 0;
        inLevel_ = 0.0;
        inL_.prepare(fs_, 30, hc::fx::dbToGain(p(InputL)));
        inR_.prepare(fs_, 30, hc::fx::dbToGain(p(InputR)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        makeup_.prepare(fs_, 30, hc::fx::dbToGain(makeupDb()));
        publishReduction(0.0f);
        publishMeter(0, 0.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int type = std::clamp(static_cast<int>(std::lround(p(FilterType))), 0, 3);
        designFilter(type);
        const bool compOn = p(CompOn) >= 0.5f, gateOn = p(GateOn) >= 0.5f, link = p(Link) >= 0.5f;
        const bool look = p(Lookahead) >= 0.5f, cancel = p(Cancel) >= 0.5f, isGate = p(GateMode) < 0.5f;
        const int key = std::clamp(static_cast<int>(std::lround(p(Key))), 0, 2);
        const int mon = std::clamp(static_cast<int>(std::lround(p(Monitor))), 0, 2);
        const int compEq = std::clamp(static_cast<int>(std::lround(p(CompEq))), 0, 2);
        const int gateEq = link ? compEq : std::clamp(static_cast<int>(std::lround(p(GateEq))), 0, 2);
        const float thr = p(Threshold), ratio = p(Ratio);
        const float att = coefMs(fs_, p(Attack));
        const float relMs = p(Release), pdrMs = p(Pdr);
        const float gAtt = coefMs(fs_, link ? p(Attack) : p(GateAttack)), gRel = coefMs(fs_, link ? p(Release) : p(GateRelease));
        const float envRel = coefMs(fs_, 4.0), gEnvRel = coefMs(fs_, 10.0);
        const float openDb = p(GateOpen), closeDb = std::min(p(GateClose), openDb);
        const float floorDb = p(Floor) <= -79.9f ? -120.0f : p(Floor);
        const float slope = p(ExpSlope);
        const auto holdN = static_cast<std::uint32_t>(p(GateHold) * 0.001f * static_cast<float>(fs_));
        const auto pdrN = static_cast<std::uint32_t>(pdrMs * 0.001f * static_cast<float>(fs_));
        const float relLong = coefMs(fs_, relMs), relPdr = coefMs(fs_, std::max(pdrMs, 0.5f));
        float mostC = 0.0f, mostG = 0.0f;
        double inSum = 0.0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i] * inL_.next(hc::fx::dbToGain(p(InputL))), r = R[i] * inR_.next(hc::fx::dbToGain(p(InputR)));
            inSum += static_cast<double>(l) * l + static_cast<double>(r) * r;
            // the filter: the active band and its complement (they add back up exactly)
            const float al = filt_[0].tick(l), ar = filt_[1].tick(r);
            const float dal = act_.l.process(al, kLookahead), dar = act_.r.process(ar, kLookahead);
            const float dpl = pas_.l.process(l - al, kLookahead), dpr = pas_.r.process(r - ar, kLookahead);
            // what each detector hears: the whole sound or the filter, now (LOOKAHEAD) or lined up with the audio
            const float wl = look ? l : dal + dpl, wr = look ? r : dar + dpr, fl = look ? al : dal, fr = look ? ar : dar;
            auto level = [key](float a, float b) {   // the KEY decides which side is listened to
                return key == 1 ? std::fabs(a) : key == 2 ? std::fabs(b) : std::max(std::fabs(a), std::fabs(b));
            };
            const float xc = compEq == 0 ? level(wl, wr) : level(fl, fr);
            const float xg = gateEq == 0 ? level(wl, wr) : level(fl, fr);
            envC_ = xc > envC_ ? xc : xc + envRel * (envC_ - xc);
            envG_ = xg > envG_ ? xg : xg + gEnvRel * (envG_ - xg);
            // compressor, with the program-dependent release: an event shorter than PDR lets go at PDR
            const float target = compOn ? compDb(dbOf(envC_), thr, ratio, cancel) : 0.0f;
            if (target < -0.5f) ++eventN_;
            else if (eventN_ > 0) { lastEventN_ = eventN_; eventN_ = 0; }
            const float rel = pdrN > 0 && lastEventN_ > 0 && lastEventN_ < pdrN ? relPdr : relLong;
            const bool deeper = std::fabs(target) > std::fabs(gr_);   // moving away from 0 dB = attack
            gr_ = target + (deeper ? att : rel) * (gr_ - target);
            // gate / expander
            float gTarget = 0.0f;
            if (gateOn) {
                const float glv = dbOf(envG_);
                if (isGate) {
                    if (glv >= openDb) { open_ = true; holdLeft_ = holdN; }
                    else if (glv < closeDb) {
                        if (holdLeft_ > 0) --holdLeft_;
                        else open_ = false;
                    }
                    gTarget = open_ ? 0.0f : floorDb;
                } else {
                    const float under = std::max(openDb - glv, 0.0f);
                    gTarget = floorDb >= 0.0f ? std::min(under * (slope - 1.0f), floorDb) : std::max(-under * (slope - 1.0f), floorDb);
                }
            }
            const bool opening = floorDb >= 0.0f ? gTarget < gate_ : gTarget > gate_;
            gate_ = gTarget + (opening ? gAtt : gRel) * (gate_ - gTarget);
            mostC = std::min(mostC, gr_);
            mostG = std::min(mostG, gate_);
            const float mk = makeup_.next(hc::fx::dbToGain(makeupDb())), o = out_.next(hc::fx::dbToGain(p(Output)));
            // SPLIT: the module works only on the active band; otherwise on both
            const float gc = compOn ? gr_ : 0.0f, gg = gateOn ? gate_ : 0.0f;
            const float gAct = hc::fx::dbToGain(gc + gg) * mk;
            const float gPas = hc::fx::dbToGain((compEq == 2 ? 0.0f : gc) + (gateEq == 2 ? 0.0f : gg)) * (compEq == 2 ? 1.0f : mk);
            float yl = dal * gAct + dpl * gPas, yr = dar * gAct + dpr * gPas;
            if (key == 1) yl = dal + dpl;   // the key side passes as it came
            else if (key == 2) yr = dar + dpr;
            if (mon == 1) { yl = dal; yr = dar; }
            else if (mon == 2) { yl = dpl; yr = dpr; }
            L[i] = yl * o;
            R[i] = yr * o;
        }
        if (std::fabs(gr_) < 1e-6f) gr_ = 0.0f;
        if (std::fabs(gate_) < 1e-6f) gate_ = 0.0f;
        inLevel_ += (inSum / (2.0 * std::max<std::uint32_t>(n, 1)) - inLevel_) * 0.3;
        publishReduction(std::min(mostC, mostG));
        publishMeter(0, mostC);
        publishMeter(1, mostG);
        publishMeter(2, std::max(-100.0f, powDb(2.0 * inLevel_)));
    }

private:
    // MAKEUP, plus with LEVEL REF PEAK what keeps a full-scale peak where it was
    float makeupDb() const noexcept {
        float m = p(Makeup);
        if (p(LevelRef) >= 0.5f && p(CompOn) >= 0.5f) m -= compDb(0.0f, p(Threshold), p(Ratio), p(Cancel) >= 0.5f);
        return std::clamp(m, -40.0f, 60.0f);
    }
    void designFilter(int type) noexcept {
        const float f = p(FilterFreq), q = p(FilterQ);
        if (static_cast<float>(type) == design_[0] && f == design_[1] && q == design_[2]) return;
        design_ = {static_cast<float>(type), f, q};
        // Q 0.1..0.6: the band's width (BAND / NOTCH), or how steep the cut is (0.6 = the usual)
        const C c = type == 0 ? C::make(T::BandPass, fs_, f, q)
                  : type == 1 ? C::make(T::Notch, fs_, f, q)
                  : C::make(type == 2 ? T::HighPass : T::LowPass, fs_, f, q / 0.6 * 0.7071);
        filt_[0].c = filt_[1].c = c;
    }

    double fs_ = 48000.0;
    Delay2 act_, pas_;
    hc::fx::Biquad filt_[2];
    std::array<float, 3> design_{-1, -1, -1};
    float envC_ = 0, envG_ = 0, gr_ = 0, gate_ = 0;
    bool open_ = false;
    std::uint32_t holdLeft_ = 0, eventN_ = 0, lastEventN_ = 0;
    double inLevel_ = 0;
    hc::fx::Smoothed inL_, inR_, out_, makeup_;
};

} // namespace
} // namespace switchblade

std::unique_ptr<hc::fx::Effect> makeSwitchblade() { return std::make_unique<switchblade::Impl>(); }

} // namespace hem::rack
