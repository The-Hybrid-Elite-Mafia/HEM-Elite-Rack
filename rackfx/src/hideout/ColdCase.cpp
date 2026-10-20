// COLD CASE — swept notches with a feedback loop (see Hideout.hpp).
//
// The notch input (the track plus the loop's return, through LOW CUT) runs through a chain of notch filters spread over
// three octaves above a swept base frequency; DEPTH blends the chain against the input (NOTCH FLIP turns notches into
// peaks). The wet sound goes round the loop (DELAY, the right side DENSITY longer, HI TAME, LOOP FILTER, DECAY), then
// HIGH CUT, WIDTH / ROTATE, PRE-DELAY and WET MODE before MIX.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace coldcase {
namespace {
using namespace hideout;

const char* const kPairsN[] = {"1", "2", "3", "4", "5", "6"};
const char* const kWaveN[] = {"SAW UP", "SAW DOWN", "TRIANGLE", "SINE", "SQUARE"};
const char* const kMultN[] = {"x1/4", "x1/2", "x1", "x2", "x4", "x8", "x16"};
const char* const kTempoN[] = {"HOST", "OWN"};
const char* const kLoopFilterN[] = {"OFF", "LOW-PASS", "HIGH-PASS"};
const char* const kWetModeN[] = {"STEREO", "MONO", "MONO FLIP"};
const char* const kPushN[] = {"A", "B"};   // any change = a push
const ParamInfo kParams[kCount] = {
    {"notches", "Notch Pairs", "", 0, 5, 2, Taper::Choice, kPairsN},
    {"half", "Half Pair", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"depth", "Depth", "", 0.0f, 100.0f, 80.0f},
    {"notchflip", "Notch Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"stereo", "Stereo", "", 0.0f, 100.0f, 0.0f},
    {"lowcuton", "Low Cut", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"lowcut", "Low Cut Freq", "Hz", 20.0f, 1000.0f, 80.0f, Taper::Log},
    {"run", "Run", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"wave", "Wave", "", 0, 4, 3, Taper::Choice, kWaveN},
    {"rate", "Rate", "/beat", 0.01f, 16.0f, 0.25f, Taper::Log},
    {"multiplier", "Multiplier", "", 0, 6, 2, Taper::Choice, kMultN},
    {"tempo", "Tempo", "", 0, 1, 0, Taper::Choice, kTempoN},
    {"bpm", "BPM", "", 20.0f, 300.0f, 120.0f},
    {"low", "Low", "Hz", 20.0f, 20000.0f, 200.0f, Taper::Log},
    {"high", "High", "Hz", 20.0f, 20000.0f, 2000.0f, Taper::Log},
    {"loop", "Loop", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"decay", "Decay", "", 0.0f, 100.0f, 40.0f},
    {"loopflip", "Loop Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"delay", "Delay", "ms", 0.1f, 100.0f, 5.0f, Taper::Log},
    {"density", "Density", "", 0.0f, 100.0f, 30.0f},
    {"hitame", "Hi Tame", "", 0.0f, 1.0f, 0.3f},
    {"loopfilter", "Loop Filter", "", 0, 2, 0, Taper::Choice, kLoopFilterN},
    {"loopfreq", "Loop Freq", "Hz", 20.0f, 20000.0f, 2000.0f, Taper::Log},
    {"width", "Width", "%", 0.0f, 200.0f, 100.0f},
    {"rotate", "Rotate", "deg", -45.0f, 45.0f, 0.0f},
    {"highcuton", "High Cut", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"highcut", "High Cut Freq", "Hz", 1000.0f, 20000.0f, 12000.0f, Taper::Log},
    {"predelay", "Pre-Delay", "ms", 0.0f, 100.0f, 0.0f},
    {"wetmode", "Wet Mode", "", 0, 2, 0, Taper::Choice, kWetModeN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"gain", "Gain", "dB", -18.0f, 18.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}};
constexpr int kMaxNotches = 13, kEvery = 8;
constexpr double kMult[] = {0.25, 0.5, 1, 2, 4, 8, 16};
constexpr double kMaxLoopMs = 205.0, kMaxPreMs = 101.0;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == LowCut) return p(LowCutOn) >= 0.5f;
        if (i == HighCut) return p(HighCutOn) >= 0.5f;
        if (i == LoopFreq) return p(LoopFilter) >= 0.5f;
        if (i == Bpm) return p(Tempo) >= 0.5f;
        if (i >= Decay && i <= LoopFreq) return p(LoopOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : loop_) d.prepare(static_cast<std::uint32_t>(kMaxLoopMs * 0.001 * fs) + 8);
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(kMaxPreMs * 0.001 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : loop_) d.clear();
        for (auto& d : pre_) d.clear();
        for (auto& ch : notch_) for (auto& f : ch) f.clear();
        for (auto& f : lowCut_) f.clear();
        for (auto& f : highCut_) f.clear();
        for (auto& f : loopFilter_) f.clear();
        tame_[0] = tame_[1] = ret_[0] = ret_[1] = 0.0f;
        phase_ = 0.0;
        count_ = 0;
        key_.fill(-1.0f);
        design();
        sweep(0.0);
        depth_.prepare(fs_, 20, p(Depth) * 0.01f);
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        gain_.prepare(fs_, 20, hc::fx::dbToGain(p(Gain)));
        loopG_.prepare(fs_, 20, loopGain());
        delay_.prepare(fs_, 60, delaySamples());
        preD_.prepare(fs_, 60, preSamples());
        push_ = p(Reset);
        pk_[0] = pk_[1] = 0.0f;
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        if (p(Reset) != push_) {
            push_ = p(Reset);
            pk_[0] = pk_[1] = 0.0f;
        }
        const double bpm = p(Tempo) >= 0.5f ? p(Bpm) : tempo();
        const double hz = p(Rate) * kMult[pick(Multiplier, 6)] * bpm / 60.0;
        const double step = p(Run) >= 0.5f ? hz / fs_ : 0.0;
        const int wave = pick(Wave, 4), mode = pick(WetMode, 2);
        const bool flip = p(NotchFlip) >= 0.5f, lowCut = p(LowCutOn) >= 0.5f, highCut = p(HighCutOn) >= 0.5f;
        const bool loopFilt = p(LoopFilter) >= 0.5f;
        const float sign = p(LoopFlip) >= 0.5f ? -1.0f : 1.0f, tame = p(HiTame) * 0.95f;
        const float wD = p(Depth) * 0.01f, wMix = p(Mix) * 0.01f, wGain = hc::fx::dbToGain(p(Gain)), wLoop = loopGain();
        const float wDelay = delaySamples(), wPre = preSamples(), dens = 1.0f + p(Density) * 0.01f;
        const float sideG = p(Width) * 0.01f, rot = p(Rotate) * std::numbers::pi_v<float> / 180.0f;
        const float cr = std::cos(rot), sr = std::sin(rot);
        const float maxLoop = static_cast<float>(kMaxLoopMs * 0.001 * fs_) - 2.0f;
        float io[2]{};
        for (std::uint32_t i = 0; i < n; ++i) {
            if (count_++ % kEvery == 0) sweep(shape(phase_, wave));
            phase_ += step;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const float d = depth_.next(wD), m = mix_.next(wMix), g = gain_.next(wGain), lg = loopG_.next(wLoop);
            const float dl = delay_.next(wDelay), pd = preD_.next(wPre);
            float wet[2];
            const float in[2] = {L[i], R[i]};
            for (int c = 0; c < 2; ++c) {
                float x = in[c] + lg * sign * ret_[c];
                if (lowCut) x = lowCut_[c].tick(x);
                float y = x;
                for (int k = 0; k < notches_; ++k) y = notch_[c][k].tick(y);
                const float v = flip ? x - d * (y - x) : x + d * (y - x);
                // the loop
                loop_[c].write(v);
                float r = loop_[c].read(std::min(c == 0 ? dl : dl * dens, maxLoop));
                tame_[c] = r + tame * (tame_[c] - r);
                r = tame_[c];
                if (loopFilt) r = loopFilter_[c].tick(r);
                ret_[c] = std::clamp(r, -4.0f, 4.0f);
                wet[c] = highCut ? highCut_[c].tick(v) : v;
            }
            // WIDTH, ROTATE, PRE-DELAY, WET MODE
            const float mid = 0.5f * (wet[0] + wet[1]), side = 0.5f * (wet[0] - wet[1]) * sideG;
            float wl = mid + side, wr = mid - side;
            const float rl = cr * wl - sr * wr, rr = sr * wl + cr * wr;
            pre_[0].write(rl);
            pre_[1].write(rr);
            wl = pre_[0].read(pd);
            wr = pre_[1].read(pd);
            if (mode == 1) wl = wr = 0.5f * (wl + wr);
            else if (mode == 2) wl = wr = 0.5f * (wr - wl);
            const float ol = ((1.0f - m) * in[0] + m * wl) * g, orr = ((1.0f - m) * in[1] + m * wr) * g;
            L[i] = ol;
            R[i] = orr;
            io[0] = std::max(io[0], std::fabs(ol));
            io[1] = std::max(io[1], std::fabs(orr));
        }
        pk_[0] = std::max(pk_[0], io[0]);
        pk_[1] = std::max(pk_[1], io[1]);
        publishMeter(0, std::max(dbOf(pk_[0]), -100.0f));
        publishMeter(1, std::max(dbOf(pk_[1]), -100.0f));
    }

private:
    int pick(int i, int hi) const noexcept { return std::clamp(static_cast<int>(std::lround(p(i))), 0, hi); }
    float loopGain() const noexcept { return p(LoopOn) >= 0.5f ? 0.97f * p(Decay) * 0.01f : 0.0f; }
    float delaySamples() const noexcept { return p(Delay) * 0.001f * static_cast<float>(fs_); }
    float preSamples() const noexcept { return p(PreDelay) * 0.001f * static_cast<float>(fs_); }
    static double shape(double ph, int wave) noexcept {
        switch (wave) {
        case 0: return ph;
        case 1: return 1.0 - ph;
        case 2: return 1.0 - std::fabs(2.0 * ph - 1.0);
        case 3: return 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * ph);
        default: return ph < 0.5 ? 0.0 : 1.0;
        }
    }
    // the notches for a sweep position s (0..1 between LOW and HIGH, on a log scale)
    void sweep(double s) noexcept {
        const double lo = p(LowHz), hi = p(HighHz), top = fs_ * 0.45;
        const double base = lo * std::pow(hi / lo, s);
        const double spacing = notches_ > 1 ? std::exp2(3.0 / (notches_ - 1)) : 1.0;
        const double spread = std::exp2(p(StereoSpread) * 0.01);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < notches_; ++k) {
                const double f = std::clamp(base * std::pow(spacing, k) * (c == 1 ? spread : 1.0), 20.0, top);
                notch_[c][k].c = C::make(T::Notch, fs_, f, 1.5);
            }
    }
    void design() noexcept {
        const std::array<float, 7> now{p(Notches), p(Half), p(LowCut), p(HighCut), p(LoopFilter), p(LoopFreq), 0.0f};
        if (now == key_) return;
        key_ = now;
        const double top = fs_ * 0.45;
        const int was = notches_;
        notches_ = std::min(kMaxNotches, 2 * (pick(Notches, 5) + 1) + (p(Half) >= 0.5f ? 1 : 0));
        if (notches_ != was) for (auto& ch : notch_) for (auto& f : ch) f.clear();
        lowCut_[0].c = lowCut_[1].c = C::make(T::HighPass, fs_, std::min<double>(p(LowCut), top), 0.7071);
        highCut_[0].c = highCut_[1].c = C::make(T::LowPass, fs_, std::min<double>(p(HighCut), top), 0.7071);
        const T lt = p(LoopFilter) >= 1.5f ? T::HighPass : T::LowPass;
        loopFilter_[0].c = loopFilter_[1].c = C::make(lt, fs_, std::min<double>(p(LoopFreq), top), 0.7071);
    }

    double fs_ = 48000.0, phase_ = 0.0;
    std::uint32_t count_ = 0;
    int notches_ = 6;
    hc::fx::DelayLine loop_[2], pre_[2];
    hc::fx::Biquad notch_[2][kMaxNotches], lowCut_[2], highCut_[2], loopFilter_[2];
    float tame_[2]{}, ret_[2]{}, pk_[2]{}, push_ = 0.0f;
    std::array<float, 7> key_{};
    hc::fx::Smoothed depth_, mix_, gain_, loopG_, delay_, preD_;
};

} // namespace
} // namespace coldcase

std::unique_ptr<hc::fx::Effect> makeColdCase() { return std::make_unique<coldcase::Impl>(); }

} // namespace hem::rack
