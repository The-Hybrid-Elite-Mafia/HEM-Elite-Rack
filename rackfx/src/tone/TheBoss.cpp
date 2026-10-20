// THE BOSS — channel strip: gate, compressor, four-band EQ, low cut, limiter (see ToneShop.hpp).
#include "hem/rack/ToneShop.hpp"

#include "../arsenal/Common.hpp"

namespace hem::rack {
namespace boss {
namespace {
using namespace arsenal;

const char* const kLowN[] = {"60 Hz", "100 Hz", "200 Hz"};
const char* const kHighN[] = {"8 kHz", "12 kHz", "16 kHz"};
const char* const kCutN[] = {"OFF", "40 Hz", "80 Hz", "120 Hz"};
const char* const kOrderN[] = {"EQ FIRST", "DYN FIRST"};
const ParamInfo kParams[kCount] = {
    {"gate", "Gate", "dB", -80.0f, 0.0f, -80.0f},
    {"threshold", "Threshold", "dB", -50.0f, 0.0f, -18.0f},
    {"ratio", "Ratio", ":1", 1.0f, 20.0f, 3.0f, Taper::Log},
    {"attack", "Attack", "ms", 0.1f, 100.0f, 10.0f, Taper::Log},
    {"release", "Release", "ms", 20.0f, 2000.0f, 150.0f, Taper::Log},
    {"lowgain", "Low", "dB", -15.0f, 15.0f, 0.0f},
    {"lowfreq", "Low Freq", "", 0, 2, 1, Taper::Choice, kLowN},
    {"lomidgain", "Low Mid", "dB", -15.0f, 15.0f, 0.0f},
    {"lomidfreq", "Low Mid Freq", "Hz", 150.0f, 2000.0f, 500.0f, Taper::Log},
    {"himidgain", "High Mid", "dB", -15.0f, 15.0f, 0.0f},
    {"himidfreq", "High Mid Freq", "Hz", 800.0f, 8000.0f, 3000.0f, Taper::Log},
    {"highgain", "High", "dB", -15.0f, 15.0f, 0.0f},
    {"highfreq", "High Freq", "", 0, 2, 1, Taper::Choice, kHighN},
    {"lowcut", "Low Cut", "", 0, 3, 0, Taper::Choice, kCutN},
    {"order", "Order", "", 0, 1, 0, Taper::Choice, kOrderN},
    {"limit", "Limit", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr float kCeiling = 0.966f;   // -0.3 dBFS

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i == LowFreq) return p(LowGain) != 0.0f;
        if (i == LoMidFreq) return p(LoMidGain) != 0.0f;
        if (i == HiMidFreq) return p(HiMidGain) != 0.0f;
        if (i == HighFreq) return p(HighGain) != 0.0f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        delay_.prepare(kLatency);
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        delay_.clear();
        key_.fill(-1e9f);
        env_ = gateEnv_ = gr_ = gate_ = 0.0f;
        open_ = false;
        holdLeft_ = 0;
        limG_ = 1.0f;
        for (auto& v : want_) v = 1.0f;
        wantPos_ = 0;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const bool eqFirst = p(Order) < 0.5f, limit = p(Limit) >= 0.5f;
        const float thr = p(Threshold), ratio = p(Ratio);
        const float att = coefMs(fs_, p(Attack)), rel = coefMs(fs_, p(Release)), envRel = coefMs(fs_, 4.0);
        const bool gateOn = p(Gate) > -79.5f;
        const float openDb = p(Gate), closeDb = openDb - 4.0f;
        const float gAtt = coefMs(fs_, 0.3), gRel = coefMs(fs_, 120.0);
        const auto holdN = static_cast<std::uint32_t>(0.03 * fs_);
        const float limAtt = coefMs(fs_, 0.2), limRel = coefMs(fs_, 60.0);
        float most = 0.0f, mostLim = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            if (eqFirst) eq(l, r);
            // dynamics: the gate, then the compressor (one detector for both sides)
            const float x = std::max(std::fabs(l), std::fabs(r));
            gateEnv_ = x > gateEnv_ ? x : x + coefMs(fs_, 10.0) * (gateEnv_ - x);
            const float glv = dbOf(gateEnv_);
            if (!gateOn) open_ = true;
            else if (glv >= openDb) { open_ = true; holdLeft_ = holdN; }
            else if (glv < closeDb) {
                if (holdLeft_ > 0) --holdLeft_;
                else open_ = false;
            }
            const float gTarget = open_ ? 0.0f : -40.0f;
            gate_ = gTarget > gate_ ? gTarget + gAtt * (gate_ - gTarget) : gTarget + gRel * (gate_ - gTarget);
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            gr_ = follow(gr_, downDb(dbOf(env_), thr, ratio, 6.0f), att, rel);
            most = std::min(most, gr_ + gate_);
            const float g = hc::fx::dbToGain(gr_ + gate_);
            l *= g;
            r *= g;
            if (!eqFirst) eq(l, r);
            // the limiter looks 1 ms ahead: the smallest gain any sample in the window needs, reached in time
            const float pk = std::max(std::fabs(l), std::fabs(r));
            want_[wantPos_] = limit && pk > kCeiling ? kCeiling / pk : 1.0f;
            wantPos_ = (wantPos_ + 1) % (kLatency + 1);
            float need = 1.0f;
            for (float w : want_) need = std::min(need, w);
            limG_ = need < limG_ ? need + limAtt * (limG_ - need) : need + limRel * (limG_ - need);
            const float dl = delay_.l.process(l, kLatency), dr = delay_.r.process(r, kLatency);
            float yl = dl, yr = dr;
            if (limit) {
                yl = std::clamp(dl * limG_, -kCeiling, kCeiling);
                yr = std::clamp(dr * limG_, -kCeiling, kCeiling);
                mostLim = std::min(mostLim, dbOf(limG_));
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = yl * o;
            R[i] = yr * o;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        if (gate_ > -1e-6f) gate_ = 0.0f;
        if (limG_ > 1.0f - 1e-7f) limG_ = 1.0f;
        publishReduction(std::min(most, mostLim));
    }

private:
    void eq(float& l, float& r) noexcept {
        for (auto& b : eq_[0]) l = b.tick(l);
        for (auto& b : eq_[1]) r = b.tick(r);
    }
    void design() noexcept {
        std::array<float, 9> k{p(LowGain), p(LowFreq), p(LoMidGain), p(LoMidFreq), p(HiMidGain), p(HiMidFreq), p(HighGain), p(HighFreq), p(LowCut)};
        if (k == key_) return;
        key_ = k;
        const C id{};
        const int lf = std::clamp(static_cast<int>(std::lround(p(LowFreq))), 0, 2);
        const int hf = std::clamp(static_cast<int>(std::lround(p(HighFreq))), 0, 2);
        const int lc = std::clamp(static_cast<int>(std::lround(p(LowCut))), 0, 3);
        const double top = fs_ * 0.45;
        const C cut = lc > 0 ? C::make(T::HighPass, fs_, kCutHz[lc], 0.7071) : id;
        const C low = C::make(T::LowShelf, fs_, kLowHz[lf], 0.7071, p(LowGain));
        const C lm = C::make(T::Bell, fs_, p(LoMidFreq), 0.9, p(LoMidGain));
        const C hm = C::make(T::Bell, fs_, p(HiMidFreq), 0.9, p(HiMidGain));
        const C high = C::make(T::HighShelf, fs_, std::min<double>(kHighHz[hf], top), 0.7071, p(HighGain));
        for (auto& ch : eq_) { ch[0].c = cut; ch[1].c = low; ch[2].c = lm; ch[3].c = hm; ch[4].c = high; }
    }

    double fs_ = 48000.0;
    std::array<std::array<hc::fx::Biquad, 5>, 2> eq_{};
    std::array<float, 9> key_{};
    Delay2 delay_;
    float env_ = 0, gateEnv_ = 0, gr_ = 0, gate_ = 0, limG_ = 1;
    bool open_ = false;
    std::uint32_t holdLeft_ = 0;
    std::array<float, kLatency + 1> want_{};   // the window covers the sample going out now
    std::uint32_t wantPos_ = 0;
    hc::fx::Smoothed out_;
};

} // namespace
} // namespace boss

std::unique_ptr<hc::fx::Effect> makeTheBoss() { return std::make_unique<boss::Impl>(); }

} // namespace hem::rack
