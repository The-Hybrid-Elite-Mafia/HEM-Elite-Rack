// SWITCHBLADE — channel compressor + gate + steering filter (see Arsenal.hpp).
#include "hem/rack/Arsenal.hpp"

#include "Common.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
using namespace switchblade;

const char* const kTypeN[] = {"OFF", "HIGH-PASS", "LOW-PASS", "BAND-PASS", "BAND-CUT"};
const char* const kUseN[] = {"KEY", "BAND"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f},
    {"ratio", "Ratio", ":1", 1.0f, 30.0f, 3.0f, Taper::Log},
    {"attack", "Attack", "ms", 0.05f, 200.0f, 5.0f, Taper::Log},
    {"release", "Release", "ms", 5.0f, 2000.0f, 120.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"makeup", "Makeup", "dB", -12.0f, 24.0f, 0.0f},
    {"gateopen", "Gate Open", "dB", -90.0f, 0.0f, -90.0f},
    {"gateclose", "Gate Close", "dB", 0.0f, 20.0f, 4.0f},
    {"gaterange", "Gate Range", "dB", 0.0f, 80.0f, 40.0f},
    {"gatehold", "Gate Hold", "ms", 0.0f, 500.0f, 20.0f},
    {"gaterelease", "Gate Release", "ms", 5.0f, 2000.0f, 150.0f, Taper::Log},
    {"filtertype", "Filter", "", 0, 4, 0, Taper::Choice, kTypeN},
    {"filteruse", "Filter Use", "", 0, 1, 0, Taper::Choice, kUseN},
    {"filterfreq", "Filter Freq", "Hz", 20.0f, 20000.0f, 2500.0f, Taper::Log},
    {"filterq", "Filter Q", "", 0.3f, 6.0f, 0.7f, Taper::Log},
    {"lookahead", "Lookahead", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"listen", "Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -24.0f, 12.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"COMP", "dB", -40.0f, 0.0f}, {"GATE", "dB", -80.0f, 0.0f}, {"INPUT", "dB", -60.0f, 0.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLookahead; }
    bool paramActive(int i) const noexcept override {
        const bool filter = p(FilterType) >= 0.5f;
        if (i == Release) return p(AutoRelease) < 0.5f;
        if (i == FilterUse || i == FilterFreq) return filter;
        if (i == FilterQ) return p(FilterType) >= 2.5f;   // band-pass / band-cut have a width
        if (i >= GateClose && i <= GateRelease) return p(GateOpen) > -89.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        audio_.prepare(kLookahead);
        key_.prepare(kLookahead);
        mem_.set(fs, 2.0, 3.0);
        reset();
    }
    void reset() noexcept override {
        audio_.clear();
        key_.clear();
        for (auto& f : det_) f.clear();
        for (auto& f : band_) f.clear();
        design_ = {-1, -1, -1};
        env_ = 0.0f;
        gateEnv_ = 0.0f;
        gr_ = 0.0f;
        gate_ = 0.0f;
        open_ = false;
        holdLeft_ = 0;
        mem_.v = 0.0f;
        inLevel_ = 0.0;
        makeup_.prepare(fs_, 30, hc::fx::dbToGain(p(Makeup)));
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
        publishMeter(0, 0.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int type = std::clamp(static_cast<int>(std::lround(p(FilterType))), 0, 4);
        const bool bandMode = type > 0 && p(FilterUse) >= 0.5f;
        designFilters(type);
        const bool look = p(Lookahead) >= 0.5f, listen = p(Listen) >= 0.5f, autoRel = p(AutoRelease) >= 0.5f;
        const float thr = p(Threshold), ratio = p(Ratio);
        const float att = coefMs(fs_, p(Attack)), relManual = coefMs(fs_, p(Release));
        const float envRel = coefMs(fs_, 4.0);
        const bool gateOn = p(GateOpen) > -89.5f;
        const float openDb = p(GateOpen), closeDb = openDb - p(GateClose), floorDb = -p(GateRange);
        const float gAtt = coefMs(fs_, 0.3), gRel = coefMs(fs_, p(GateRelease));
        const auto holdN = static_cast<std::uint32_t>(p(GateHold) * 0.001f * static_cast<float>(fs_));
        float mostC = 0.0f, mostG = 0.0f;
        double inSum = 0.0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i], r = R[i];
            inSum += static_cast<double>(l) * l + static_cast<double>(r) * r;
            // what the detector hears: the input, or the filtered input (KEY), now or 2 ms late
            float kl = l, kr = r;
            if (type > 0) { kl = det_[0].tick(det_[1].tick(l)); kr = det_[2].tick(det_[3].tick(r)); }
            const float dl = audio_.l.process(l, kLookahead), dr = audio_.r.process(r, kLookahead);
            const float kdl = key_.l.process(kl, kLookahead), kdr = key_.r.process(kr, kLookahead);
            if (!look) { kl = kdl; kr = kdr; }   // without look-ahead the detector lines up with the audio
            const float x = std::max(std::fabs(kl), std::fabs(kr));
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float lv = dbOf(env_);
            // compressor
            const float target = downDb(lv, thr, ratio, 6.0f);
            const float rel = autoRel ? coefMs(fs_, 40.0 + 900.0 * mem_.v) : relManual;
            gr_ = follow(gr_, target, att, rel);
            mem_.tick(gr_ < -2.0f);
            // gate: opens at OPEN, shuts at CLOSE under it, holds, then falls to the floor
            gateEnv_ = x > gateEnv_ ? x : x + coefMs(fs_, 10.0) * (gateEnv_ - x);
            const float glv = dbOf(gateEnv_);
            if (!gateOn) open_ = true;
            else if (glv >= openDb) { open_ = true; holdLeft_ = holdN; }
            else if (glv < closeDb) {
                if (holdLeft_ > 0) --holdLeft_;
                else open_ = false;
            }
            const float gTarget = open_ ? 0.0f : floorDb;
            gate_ = gTarget > gate_ ? gTarget + gAtt * (gate_ - gTarget) : gTarget + gRel * (gate_ - gTarget);
            mostC = std::min(mostC, gr_);
            mostG = std::min(mostG, gate_);
            const float g = hc::fx::dbToGain(gr_ + gate_) * makeup_.next(hc::fx::dbToGain(p(Makeup)));
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            if (listen) { L[i] = kdl * o; R[i] = kdr * o; continue; }
            if (bandMode) {   // only the filtered band changes: band * g + (rest)
                const float bl = band_[0].tick(band_[1].tick(dl)), br = band_[2].tick(band_[3].tick(dr));
                L[i] = (dl + bl * (g - 1.0f)) * o;
                R[i] = (dr + br * (g - 1.0f)) * o;
            } else {
                L[i] = dl * g * o;
                R[i] = dr * g * o;
            }
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        if (gate_ > -1e-6f) gate_ = 0.0f;
        inLevel_ += (inSum / (2.0 * std::max<std::uint32_t>(n, 1)) - inLevel_) * 0.3;
        publishReduction(std::min(mostC, mostG));
        publishMeter(0, mostC);
        publishMeter(1, mostG);
        publishMeter(2, std::max(-100.0f, powDb(2.0 * inLevel_)));
    }

private:
    void designFilters(int type) noexcept {
        const float f = p(FilterFreq), q = type >= 3 ? p(FilterQ) : 0.7071f;
        if (type == design_[0] && f == design_[1] && q == design_[2]) return;
        design_ = {static_cast<float>(type), f, q};
        if (type == 0) return;
        // 4th order: two sections of the same filter (steeper for the detector and the band)
        T t = T::HighPass;
        if (type == 2) t = T::LowPass;
        else if (type == 3) t = T::BandPass;
        else if (type == 4) t = T::Notch;
        const C c = C::make(t, fs_, f, q);
        for (auto& b : det_) b.c = c;
        for (auto& b : band_) b.c = c;
    }

    double fs_ = 48000.0;
    Delay2 audio_, key_;
    hc::fx::Biquad det_[4], band_[4];
    std::array<float, 3> design_{-1, -1, -1};
    float env_ = 0, gateEnv_ = 0, gr_ = 0, gate_ = 0;
    bool open_ = false;
    std::uint32_t holdLeft_ = 0;
    Memory mem_;
    double inLevel_ = 0;
    hc::fx::Smoothed makeup_, out_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeSwitchblade() { return std::make_unique<Impl>(); }

} // namespace hem::rack
