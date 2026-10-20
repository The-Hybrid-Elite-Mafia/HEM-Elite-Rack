// SWERVE — flanger (see Hideout.hpp).
//
// Per side: a short delay line swept by an LFO around DELAY (DEPTH 100 % = from 0 up to twice DELAY). The wet copy goes
// through the FILTER (when on) and back into the line through FEEDBACK. TAPE delays the dry by DELAY too, so the sweep
// passes through zero. The right LFO runs STEREO degrees behind the left. TIE keeps RATE x DEPTH (the pitch wobble)
// where it was when TIE went on: moving one moves the sound of the other.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace swerve {
namespace {
using namespace hideout;

const char* const kSyncN[] = {"FREE", "HOST"};
const char* const kWaveN[] = {"SINE", "TRIANGLE"};
const char* const kFilterN[] = {"OFF", "LOW-PASS", "HIGH-PASS"};
const char* const kPushN[] = {"A", "B"};   // any change = a push
const ParamInfo kParams[kCount] = {
    {"delay", "Delay", "ms", 0.0f, 20.0f, 2.2f},
    {"depth", "Depth", "%", 0.0f, 100.0f, 100.0f},
    {"rate", "Rate", "", 0.0f, 20.0f, 0.2f},
    {"sync", "Sync", "", 0, 1, 0, Taper::Choice, kSyncN},
    {"wave", "Wave", "", 0, 1, 0, Taper::Choice, kWaveN},
    {"tie", "Tie", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"hold", "Hold", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"stereo", "Stereo", "deg", 0.0f, 180.0f, 0.0f},
    {"feedback", "Feedback", "%", 0.0f, 100.0f, 50.0f},
    {"loopflip", "Loop Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
    {"wetflip", "Wet Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"filter", "Filter", "", 0, 2, 0, Taper::Choice, kFilterN},
    {"filterfreq", "Filter Freq", "Hz", 20.0f, 20000.0f, 1000.0f, Taper::Log},
    {"tape", "Tape", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"gain", "Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}};
constexpr double kMaxMs = 41.0;   // twice the longest DELAY, and a little room

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == FilterFreq) return p(FilterType) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : line_) d.prepare(static_cast<std::uint32_t>(kMaxMs * 0.001 * fs) + 8);
        for (auto& d : dry_) d.prepare(static_cast<std::uint32_t>(kMaxMs * 0.001 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : line_) d.clear();
        for (auto& d : dry_) d.clear();
        for (auto& f : filter_) f.clear();
        fb_[0] = fb_[1] = 0.0f;
        phase_ = 0.0;
        key_.fill(-1.0f);
        design();
        tieOn_ = p(Tie) >= 0.5f;
        tieProduct_ = p(Depth) * std::max(p(Rate), 0.01f);
        lastDepth_ = p(Depth);
        lastRate_ = p(Rate);
        push_ = p(Reset);
        base_.prepare(fs_, 40, delaySamples());
        swing_.prepare(fs_, 40, swingSamples(p(Depth)));
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        fbG_.prepare(fs_, 20, p(Feedback) * 0.01f);
        gain_.prepare(fs_, 20, hc::fx::dbToGain(p(Gain)));
        tapeMix_.prepare(fs_, 20, p(Tape) >= 0.5f ? 1.0f : 0.0f);
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
        const float depth = tiedDepth();
        const double hz = p(Sync) >= 0.5f ? p(Rate) * tempo() / 60.0 : p(Rate);
        const double step = p(Hold) >= 0.5f ? 0.0 : hz / fs_;
        const double off = p(StereoOffset) / 360.0;
        const bool tri = p(Wave) >= 0.5f, filt = p(FilterType) >= 0.5f;
        const float loopSign = p(LoopFlip) >= 0.5f ? -1.0f : 1.0f, wetSign = p(WetFlip) >= 0.5f ? -1.0f : 1.0f;
        const float wBase = delaySamples(), wSwing = swingSamples(depth), wMix = p(Mix) * 0.01f, wFb = p(Feedback) * 0.01f;
        const float wGain = hc::fx::dbToGain(p(Gain)), wTape = p(Tape) >= 0.5f ? 1.0f : 0.0f;
        const float maxD = static_cast<float>(kMaxMs * 0.001 * fs_) - 2.0f;
        float io[2]{};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float b = base_.next(wBase), sw = swing_.next(wSwing), m = mix_.next(wMix), fb = fbG_.next(wFb);
            const float g = gain_.next(wGain), tp = tapeMix_.next(wTape);
            float* ch[2] = {L, R};
            for (int c = 0; c < 2; ++c) {
                double ph = phase_ - (c == 1 ? off : 0.0);
                ph -= std::floor(ph);
                const float lfo = tri ? static_cast<float>(1.0 - 4.0 * std::fabs(ph - 0.5)) : static_cast<float>(std::sin(2.0 * std::numbers::pi * ph));
                const float x = ch[c][i];
                line_[c].write(x + loopSign * fb * 0.98f * fb_[c]);
                float wet = line_[c].read(std::clamp(b + sw * lfo, 0.0f, maxD));
                if (filt) wet = filter_[c].tick(wet);
                fb_[c] = wet;
                dry_[c].write(x);
                const float d = x + tp * (dry_[c].read(b) - x);   // TAPE: the dry waits as long as DELAY
                const float y = ((1.0f - m) * d + m * wetSign * wet) * g;
                ch[c][i] = y;
                io[c] = std::max(io[c], std::fabs(y));
            }
            phase_ += step;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
        pk_[0] = std::max(pk_[0], io[0]);
        pk_[1] = std::max(pk_[1], io[1]);
        publishMeter(0, std::max(dbOf(pk_[0]), -100.0f));
        publishMeter(1, std::max(dbOf(pk_[1]), -100.0f));
    }

private:
    float delaySamples() const noexcept { return p(Delay) * 0.001f * static_cast<float>(fs_); }
    // DEPTH 100 %: the sweep goes from 0 up to twice DELAY
    float swingSamples(float depth) const noexcept { return depth * 0.01f * delaySamples(); }
    // TIE: RATE x DEPTH stays where it was when TIE went on; moving DEPTH sets a new product, moving RATE changes the depth heard
    float tiedDepth() noexcept {
        const bool on = p(Tie) >= 0.5f;
        const float d = p(Depth), r = std::max(p(Rate), 0.01f);
        if (on && !tieOn_) tieProduct_ = d * r;
        else if (on && d != lastDepth_) tieProduct_ = d * r;
        tieOn_ = on;
        lastDepth_ = d;
        lastRate_ = p(Rate);
        return on ? std::clamp(tieProduct_ / r, 0.0f, 100.0f) : d;
    }
    void design() noexcept {
        const std::array<float, 2> now{p(FilterType), p(FilterFreq)};
        if (now == key_) return;
        key_ = now;
        const T t = p(FilterType) >= 1.5f ? T::HighPass : T::LowPass;
        filter_[0].c = filter_[1].c = C::make(t, fs_, std::min<double>(p(FilterFreq), fs_ * 0.45), 0.7071);
    }

    double fs_ = 48000.0, phase_ = 0.0;
    hc::fx::DelayLine line_[2], dry_[2];
    hc::fx::Biquad filter_[2];
    float fb_[2]{}, pk_[2]{}, push_ = 0.0f, tieProduct_ = 0.0f, lastDepth_ = 0.0f, lastRate_ = 0.0f;
    bool tieOn_ = false;
    std::array<float, 2> key_{};
    hc::fx::Smoothed base_, swing_, mix_, fbG_, gain_, tapeMix_;
};

} // namespace
} // namespace swerve

std::unique_ptr<hc::fx::Effect> makeSwerve() { return std::make_unique<swerve::Impl>(); }

} // namespace hem::rack
