// MOUTHPIECE, SLINGER and STREET TEAM — the voice and guitar dynamics and the channel (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"
#include "hem/rack/Plugins.hpp"

#include "SpaceKit.hpp"
#include "../maxim/LimitKit.hpp"
#include "../tone/EqKit.hpp"

namespace hem::rack {
namespace {
using namespace hideout;
const char* const kPushN[] = {"A", "B"};   // any change = a push

// A peak follower in dB, and a brick-wall-ish output limiter (instant attack, smooth release) shared by the three.
struct Limiter {
    float gr = 0.0f, rel = 0.0f;
    void prepare(double fs) noexcept { rel = coefMs(fs, 80.0); }
    // returns the gain to apply so |x| stays under `ceiling` (linear)
    float gain(float peak, float ceiling) noexcept {
        const float want = peak > ceiling ? dbOf(ceiling) - dbOf(peak) : 0.0f;
        gr = want < gr ? want : want + rel * (gr - want);
        return hc::fx::dbToGain(gr);
    }
};
} // namespace

// ---- MOUTHPIECE ----------------------------------------------------------------------------------
namespace mouthpiece {
namespace {
const ParamInfo kParams[kCount] = {
    {"gate", "Gate", "dB", -80.0f, 0.0f, -80.0f},
    {"press", "Press", "dB", -36.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -36.0f, 0.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"TOTAL", "dB", -36.0f, 0.0f}, {"OUTPUT", "dBFS", -60.0f, 0.0f}, {"INPUT", "dBFS", -60.0f, 0.0f}};
constexpr float kRatio = 3.0f, kKnee = 10.0f;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lim_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        env_ = gr_ = gate_ = 0.0f;
        lim_.gr = 0.0f;
        push_ = p(Reset);
        most_ = 0.0f;
        pkIn_ = pkOut_ = 0.0f;
        for (int m = 0; m < 3; ++m) publishMeter(m, m == 0 ? 0.0f : -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Reset) != push_) {
            push_ = p(Reset);
            most_ = 0.0f;
            pkIn_ = pkOut_ = 0.0f;
        }
        const float thr = p(Press), gateDb = p(Gate), ceil = hc::fx::dbToGain(p(Ceiling));
        const float makeup = -thr * (1.0f - 1.0f / kRatio) * 0.8f;
        const float att = coefMs(fs_, 3.0), rel = coefMs(fs_, 120.0), envRel = coefMs(fs_, 40.0);
        const float gOpen = coefMs(fs_, 2.0), gClose = coefMs(fs_, 80.0);
        float pin = 0.0f, pout = 0.0f, worst = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            pin = std::max(pin, x);
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float lv = dbOf(env_);
            // the gate: a soft 1:3 downward expander under GATE (off at -80)
            const float gWant = gateDb > -79.9f && lv < gateDb ? std::max(-40.0f, (lv - gateDb) * 2.0f) : 0.0f;
            gate_ = follow(gate_, gWant, gOpen, gClose);
            gr_ = follow(gr_, downDb(lv, thr, kRatio, kKnee), att, rel);
            const float total = gr_ + gate_;
            worst = std::min(worst, total);
            const float g = hc::fx::dbToGain(total + makeup);
            float l = L[i] * g, r = R[i] * g;
            const float lg = lim_.gain(std::max(std::fabs(l), std::fabs(r)), ceil);
            l *= lg;
            r *= lg;
            L[i] = l;
            R[i] = r;
            pout = std::max({pout, std::fabs(l), std::fabs(r)});
        }
        most_ = std::min(most_, worst);
        pkIn_ = std::max(pkIn_, pin);
        pkOut_ = std::max(pkOut_, pout);
        publishMeter(0, most_);
        publishMeter(1, std::max(dbOf(pkOut_), -100.0f));
        publishMeter(2, std::max(dbOf(pkIn_), -100.0f));
        publishReduction(worst);
    }

private:
    double fs_ = 48000.0;
    float env_ = 0.0f, gr_ = 0.0f, gate_ = 0.0f, push_ = 0.0f, most_ = 0.0f, pkIn_ = 0.0f, pkOut_ = 0.0f;
    Limiter lim_;
};
} // namespace
} // namespace mouthpiece

// ---- SLINGER -------------------------------------------------------------------------------------
namespace slinger {
namespace {
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dBFS", -60.0f, 0.0f, 0.0f},
    {"attack", "Attack", "ms", 0.0f, 50.0f, 5.0f},
    {"gain", "Gain", "dB", -30.0f, 0.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "dBFS", -60.0f, 0.0f}, {"ATTEN", "dB", -30.0f, 0.0f}, {"OUTPUT", "dBFS", -60.0f, 0.0f},
                                     {"LIMITER", "dB", -12.0f, 0.0f}};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lim_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        env_ = gr_ = 0.0f;
        lim_.gr = 0.0f;
        hold_ = 0.0f;
        push_ = p(Reset);
        mostAtt_ = mostLim_ = 0.0f;
        pkIn_ = pkOut_ = 0.0f;
        publishMeter(0, -100.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, -100.0f);
        publishMeter(3, 0.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Reset) != push_) {
            push_ = p(Reset);
            mostAtt_ = mostLim_ = 0.0f;
            pkIn_ = pkOut_ = 0.0f;
        }
        const float thr = p(Threshold);
        const float ratio = 1.0f + (-thr) / 10.0f;   // 0 dB = 1:1 .. -60 dB = 7:1
        const float makeup = -thr * (1.0f - 1.0f / ratio) * 0.85f;
        const float out = hc::fx::dbToGain(p(Gain));
        const float att = coefMs(fs_, std::max(0.05, static_cast<double>(p(Attack)))), envRel = coefMs(fs_, 15.0);
        const float relFast = coefMs(fs_, 60.0), relSlow = coefMs(fs_, 600.0), holdRel = coefMs(fs_, 1500.0);
        const float ceil = hc::fx::dbToGain(-0.1f);
        float pin = 0.0f, pout = 0.0f, wAtt = 0.0f, wLim = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            pin = std::max(pin, x);
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float want = downDb(dbOf(env_), thr, ratio, 6.0f);
            // the release follows the playing: long, steady compression lets go slowly, short peaks quickly
            hold_ = std::min(want, hold_ * holdRel);
            const float depth = std::clamp(hold_ / -12.0f, 0.0f, 1.0f);
            const float rel = relFast + depth * (relSlow - relFast);
            gr_ = follow(gr_, want, att, rel);
            const float g = hc::fx::dbToGain(gr_ + makeup) * out;
            float l = L[i] * g, r = R[i] * g;
            const float lg = lim_.gain(std::max(std::fabs(l), std::fabs(r)), ceil);
            l *= lg;
            r *= lg;
            L[i] = l;
            R[i] = r;
            pout = std::max({pout, std::fabs(l), std::fabs(r)});
            wAtt = std::min(wAtt, gr_ + lim_.gr);
            wLim = std::min(wLim, lim_.gr);
        }
        mostAtt_ = std::min(mostAtt_, wAtt);
        mostLim_ = std::min(mostLim_, wLim);
        pkIn_ = std::max(pkIn_, pin);
        pkOut_ = std::max(pkOut_, pout);
        publishMeter(0, std::max(dbOf(pkIn_), -100.0f));
        publishMeter(1, mostAtt_);
        publishMeter(2, std::max(dbOf(pkOut_), -100.0f));
        publishMeter(3, mostLim_);
        publishReduction(wAtt);
    }

private:
    double fs_ = 48000.0;
    float env_ = 0.0f, gr_ = 0.0f, hold_ = 0.0f, push_ = 0.0f, mostAtt_ = 0.0f, mostLim_ = 0.0f, pkIn_ = 0.0f, pkOut_ = 0.0f;
    Limiter lim_;
};
} // namespace
} // namespace slinger

// ---- STREET TEAM ---------------------------------------------------------------------------------
namespace streetteam {
namespace {
using eqkit::Shape;
const char* const kFirstN[] = {"HIGH-PASS", "LO SHELF", "BELL", "HI SHELF"};
const char* const kMidN[] = {"LO SHELF", "BELL", "HI SHELF"};
const char* const kLastN[] = {"LO SHELF", "BELL", "HI SHELF", "LOW-PASS"};
#define HEM_BAND(n, names, kinds, ty, hz)                                                      \
    {"b" #n "on", "Band " #n " On", "", 0, 1, 0, Taper::Toggle, kOnOff},                       \
    {"b" #n "type", "Band " #n " Type", "", 0, kinds - 1, ty, Taper::Choice, names},           \
    {"b" #n "freq", "Band " #n " Freq", "Hz", 16.0f, 21357.0f, hz, Taper::Log},               \
    {"b" #n "gain", "Band " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                            \
    {"b" #n "q", "Band " #n " Q", "", 0.5f, 30.0f, 6.0f, Taper::Log}
const ParamInfo kParams[kCount] = {
    HEM_BAND(1, kFirstN, 4, 2, 63.0f), HEM_BAND(2, kMidN, 3, 1, 250.0f), HEM_BAND(3, kMidN, 3, 1, 933.0f), HEM_BAND(4, kLastN, 4, 1, 4000.0f),
    {"input", "Input", "dB", -60.0f, 0.0f, 0.0f},
    {"output", "Output", "dB", -24.0f, 12.0f, 0.0f},
    {"compthreshold", "Comp Threshold", "dBFS", -52.0f, 0.0f, 0.0f},
    {"ratio", "Ratio", ":1", 0.5f, 40.0f, 1.0f, Taper::Log},
    {"compattack", "Comp Attack", "ms", 0.1f, 1000.0f, 2.0f, Taper::Log},
    {"comprelease", "Comp Release", "ms", 1.0f, 10000.0f, 50.0f, Taper::Log},
    {"gatethreshold", "Gate Threshold", "dB", -96.0f, 0.0f, -96.0f},
    {"floor", "Floor", "dB", -96.0f, 0.0f, -96.0f},
    {"gateattack", "Gate Attack", "ms", 0.1f, 1000.0f, 2.0f, Taper::Log},
    {"gaterelease", "Gate Release", "ms", 1.0f, 10000.0f, 30.0f, Taper::Log},
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {{"ENERGY", "dBFS", -80.0f, 0.0f}, {"COMP", "dB", -48.0f, 12.0f}, {"GATE", "dB", -48.0f, 12.0f},
                                     {"OUTPUT", "dBFS", -30.0f, 0.0f}};

Shape shapeOf(int b, int type) noexcept {
    switch (b) {
    case 0: { constexpr Shape k[] = {Shape::LowCut, Shape::LowShelf, Shape::Bell, Shape::HighShelf}; return k[std::clamp(type, 0, 3)]; }
    case 3: { constexpr Shape k[] = {Shape::LowShelf, Shape::Bell, Shape::HighShelf, Shape::HighCut}; return k[std::clamp(type, 0, 3)]; }
    default: { constexpr Shape k[] = {Shape::LowShelf, Shape::Bell, Shape::HighShelf}; return k[std::clamp(type, 0, 2)]; }
    }
}
eqkit::BandSet bandOf(const float* v, int b) noexcept {
    eqkit::BandSet s;
    if (v[band(b, On)] < 0.5f) return s;
    s.shape = shapeOf(b, static_cast<int>(std::lround(v[band(b, Type)])));
    s.hz = v[band(b, Freq)];
    const bool bell = s.shape == Shape::Bell;
    s.gainDb = s.shape == Shape::LowCut || s.shape == Shape::HighCut ? 0.0f : v[band(b, BandGain)];
    s.q = bell ? v[band(b, Q)] : 0.7071f;   // Q only shapes the bells; shelves and the 12 dB cuts have none
    return s;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i < Input) {
            const int b = i / kStride, k = i % kStride;
            if (k == On) return true;
            if (p(band(b, On)) < 0.5f) return false;
            const Shape s = shapeOf(b, static_cast<int>(std::lround(p(band(b, Type)))));
            if (k == Q) return s == Shape::Bell;
            if (k == BandGain) return s != Shape::LowCut && s != Shape::HighCut;
        }
        if (i == Floor || i == GateAttack || i == GateRelease) return p(GateThreshold) > -95.9f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        eq_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        for (int b = 0; b < kBands; ++b) eq_.set(b, bandOf(v, b));
        eq_.clear();
        env_ = cgr_ = ggr_ = 0.0f;
        in_.prepare(fs_, 20, inGain());
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        holdE_.clear();
        holdO_.clear();
        publishMeter(0, -100.0f);
        publishMeter(1, 0.0f);
        publishMeter(2, 0.0f);
        publishMeter(3, -100.0f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        for (int b = 0; b < kBands; ++b) eq_.set(b, bandOf(v, b));
        const float thr = p(CompThreshold), ratio = p(Ratio), gThr = p(GateThreshold), floorDb = p(Floor);
        const bool gateOn = gThr > -95.9f;
        const float cAtt = coefMs(fs_, p(CompAttack)), cRel = coefMs(fs_, p(CompRelease));
        const float gAtt = coefMs(fs_, p(GateAttack)), gRel = coefMs(fs_, p(GateRelease)), envRel = coefMs(fs_, 10.0);
        const float wIn = inGain(), wOut = hc::fx::dbToGain(p(Output));
        float pe = 0.0f, po = 0.0f, worstC = 0.0f, worstG = 0.0f, mostC = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn);
            eq_.step();
            float l = eq_.run(0, L[i] * gi), r = eq_.run(1, R[i] * gi);
            const float x = std::max(std::fabs(l), std::fabs(r));
            pe = std::max(pe, x);
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float lv = dbOf(env_);
            // the gate: under its threshold it closes down to FLOOR (-96 = all the way)
            const float gWant = gateOn && lv < gThr ? std::max(floorDb, -96.0f) : 0.0f;
            ggr_ = gWant > ggr_ ? gWant + gAtt * (ggr_ - gWant) : gWant + gRel * (ggr_ - gWant);
            // the compressor (or, under 1:1, an upward expander over the threshold)
            float cWant = 0.0f;
            if (lv > thr) cWant = ratio >= 1.0f ? -(lv - thr) * (1.0f - 1.0f / ratio) : std::min(12.0f, (lv - thr) * (1.0f / ratio - 1.0f));
            cgr_ = std::fabs(cWant) > std::fabs(cgr_) ? cWant + cAtt * (cgr_ - cWant) : cWant + cRel * (cgr_ - cWant);
            const float g = hc::fx::dbToGain(cgr_ + ggr_) * out_.next(wOut);
            l *= g;
            r *= g;
            L[i] = l;
            R[i] = r;
            po = std::max({po, std::fabs(l), std::fabs(r)});
            worstC = std::fabs(cgr_) > std::fabs(worstC) ? cgr_ : worstC;
            worstG = std::min(worstG, ggr_);
            mostC = std::min(mostC, cgr_);
        }
        publishMeter(0, holdE_.push(std::max(dbOf(pe), -100.0f), n, fs_));
        publishMeter(1, worstC);
        publishMeter(2, worstG);
        publishMeter(3, holdO_.push(std::max(dbOf(po), -100.0f), n, fs_));
        publishReduction(std::min(mostC, 0.0f));
    }

private:
    float inGain() const noexcept { return p(Input) <= -59.95f ? 0.0f : hc::fx::dbToGain(p(Input)); }

    double fs_ = 48000.0;
    eqkit::IirBands<kBands> eq_;
    float env_ = 0.0f, cgr_ = 0.0f, ggr_ = 0.0f;
    maxim::PeakHold holdE_, holdO_;
    hc::fx::Smoothed in_, out_;
};
} // namespace

// The curve the plate draws: exactly what the bands run (the biquads' own response).
double curveDb(const float* v, double fs, double hz) noexcept {
    double db = 0.0;
    for (int b = 0; b < kBands; ++b) {
        const eqkit::BandSet s = bandOf(v, b);
        if (s.shape == Shape::Off) continue;
        C c[1];
        if (eqkit::biquadFor(s.shape, fs, s.hz, s.gainDb, s.q, c) == 1) db += eqkit::iirDb(c[0], fs, hz);
    }
    return db;
}
int dotsOf(const float* v, double* hz, int max) noexcept {
    int n = 0;
    for (int b = 0; b < kBands && n < max; ++b)
        if (v[band(b, On)] >= 0.5f) hz[n++] = v[band(b, Freq)];
    return n;
}
const CurveInfo kCurve{&curveDb, &dotsOf, 18.0f};
} // namespace streetteam

std::unique_ptr<hc::fx::Effect> makeMouthpiece() { return std::make_unique<mouthpiece::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeSlinger() { return std::make_unique<slinger::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeStreetTeam() { return std::make_unique<streetteam::Impl>(); }

} // namespace hem::rack
