// BODY DOUBLE — doubler: four voices, each late, detuned, wobbling and panned, with feedback (see Hideout.hpp).
//
// Each voice: its own delay line (the input, plus its own feedback through the HI-PASS), then a pitch
// shifter (window by RANGE) at DETUNE + MOD (+ an octave down with OCTAVE), then the voices' shelves,
// GAIN and PAN. The direct path waits half the shifter's window when ALIGN DIRECT is on.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace bodydouble {
namespace {
using namespace hideout;

const char* const kRangeN[] = {"20 HZ", "80 HZ"};
const char* const kRoutingN[] = {"SUM", "SPLIT"};
const char* const kPushN[] = {"A", "B"};
#define HEM_VOICE(n, g, pan, ms, cents)                                                              \
    {"v" #n "on", "Voice " #n, "", 0, 1, 1, Taper::Toggle, kOnOff},                                 \
    {"v" #n "gain", "Voice " #n " Gain", "dB", -48.0f, 0.0f, g},                                    \
    {"v" #n "pan", "Voice " #n " Pan", "deg", -45.0f, 45.0f, pan},                                  \
    {"v" #n "delay", "Voice " #n " Delay", "ms", 0.0f, 100.0f, ms},                                 \
    {"v" #n "detune", "Voice " #n " Detune", "cents", -100.0f, 100.0f, cents},                      \
    {"v" #n "feedback", "Voice " #n " Feedback", "%", 0.0f, 100.0f, 0.0f},                          \
    {"v" #n "octave", "Voice " #n " Octave", "", 0, 1, 0, Taper::Toggle, kOnOff},                   \
    {"v" #n "moddepth", "Voice " #n " Mod Depth", "cents", -200.0f, 200.0f, 0.0f},                  \
    {"v" #n "modrate", "Voice " #n " Mod Rate", "Hz", 0.1f, 400.0f, 1.0f, Taper::Log}
const ParamInfo kParams[kCount] = {
    HEM_VOICE(1, -6.0f, -45.0f, 9.4f, 8.0f), HEM_VOICE(2, -12.0f, 45.0f, 16.0f, -8.0f), HEM_VOICE(3, -6.0f, 45.0f, 23.7f, 5.0f),
    HEM_VOICE(4, -12.0f, -45.0f, 21.0f, -5.0f),
    {"directon", "Direct", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"directgain", "Direct Gain", "dB", -48.0f, 0.0f, 0.0f},
    {"directpan", "Direct Pan", "deg", -45.0f, 45.0f, 0.0f},
    {"aligndirect", "Dry In Time", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"fbhighpass", "Feedback Hi-Pass", "Hz", 16.0f, 21357.0f, 16.0f, Taper::Log},
    {"range", "Lowest Note", "", 0, 1, 1, Taper::Choice, kRangeN},
    {"modreset", "Mod Reset", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"routing", "Feed", "", 0, 1, 0, Taper::Choice, kRoutingN},
    {"lowon", "Low Shelf", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"lowgain", "Low Shelf Gain", "dB", -88.0f, 12.0f, 0.0f},
    {"lowfreq", "Low Shelf Freq", "Hz", 16.0f, 21357.0f, 101.0f, Taper::Log},
    {"highon", "High Shelf", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"highgain", "High Shelf Gain", "dB", -88.0f, 12.0f, 0.0f},
    {"highfreq", "High Shelf Freq", "Hz", 16.0f, 21357.0f, 1306.0f, Taper::Log},
    {"output", "Output", "dB", -24.0f, 0.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
#undef HEM_VOICE
const hc::fx::MeterInfo kMeters[] = {{"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}};
constexpr double kWinMs[2] = {48.0, 14.0};   // the shifter's window for RANGE 20 / 80 Hz (half of it is its delay)

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }
inline void panGains(float deg, float& l, float& r) noexcept {
    const float a = (std::clamp(deg, -45.0f, 45.0f) + 45.0f) / 90.0f * 0.5f * static_cast<float>(std::numbers::pi);
    l = std::cos(a) * 1.41421356f;
    r = std::sin(a) * 1.41421356f;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i < kVoices * kStride) return i % kStride == On || p(i - i % kStride + On) >= 0.5f;
        if (i == DirectGain || i == DirectPan) return p(DirectOn) >= 0.5f;
        if (i == LowGain || i == LowFreq) return p(LowOn) >= 0.5f;
        if (i == HighGain || i == HighFreq) return p(HighOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : late_) d.prepare(static_cast<std::uint32_t>(0.105 * fs) + 8);
        for (auto& v : shift_)
            for (int r = 0; r < 2; ++r) v[r].prepare(fs, kWinMs[r]);
        direct_[0].prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        direct_[1].prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : late_) d.clear();
        for (auto& v : shift_) for (auto& s : v) s.clear();
        for (auto& d : direct_) d.clear();
        for (auto& v : eq_) for (auto& b : v) b.clear();
        for (auto& b : fbHp_) b.clear();
        for (int v = 0; v < kVoices; ++v) { ph_[v] = 0.0; fb_[v] = 0.0f; }
        key_.fill(-999.0f);
        design();
        held_[0] = held_[1] = 0.0f;
        resetPush_ = p(Reset);
        modPush_ = p(ModReset);
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        if (p(Reset) != resetPush_) { resetPush_ = p(Reset); held_[0] = held_[1] = 0.0f; }
        if (p(ModReset) != modPush_) {   // MOD RESET: the wobbles start together again
            modPush_ = p(ModReset);
            for (double& ph : ph_) ph = 0.0;
        }
        const int range = pick(p(Range), 1);
        const bool split = p(Routing) >= 0.5f, lowOn = p(LowOn) >= 0.5f, highOn = p(HighOn) >= 0.5f;
        const auto alignN = p(AlignDirect) >= 0.5f ? static_cast<std::uint32_t>(kWinMs[range] * 0.5 * 0.001 * fs_) : 0u;
        float gl[kVoices], gr[kVoices], dly[kVoices], fbk[kVoices], cents[kVoices], depth[kVoices];
        double rate[kVoices];
        bool on[kVoices], oct[kVoices];
        for (int v = 0; v < kVoices; ++v) {
            on[v] = p(voice(v, On)) >= 0.5f;
            const float g = on[v] ? hc::fx::dbToGain(p(voice(v, Gain))) : 0.0f;
            panGains(p(voice(v, Pan)), gl[v], gr[v]);
            gl[v] *= g;
            gr[v] *= g;
            dly[v] = p(voice(v, Delay)) * 0.001f * static_cast<float>(fs_);
            fbk[v] = p(voice(v, Feedback)) * 0.01f;
            cents[v] = p(voice(v, Detune));
            depth[v] = p(voice(v, ModDepth));
            rate[v] = p(voice(v, ModRate)) / fs_;
            oct[v] = p(voice(v, Octave)) >= 0.5f;
        }
        const bool dirOn = p(DirectOn) >= 0.5f;
        float dl = 0.0f, dr = 0.0f;
        panGains(p(DirectPan), dl, dr);
        const float dg = dirOn ? hc::fx::dbToGain(p(DirectGain)) : 0.0f;
        const float wOut = hc::fx::dbToGain(p(Output));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            float yl = 0.0f, yr = 0.0f;
            for (int v = 0; v < kVoices; ++v) {
                const float src = split ? (v % 2 == 0 ? xl : xr) : 0.5f * (xl + xr);
                const float in = src + fbk[v] * fbHp_[v].tick(fb_[v]);
                late_[v].write(in);
                const float late = late_[v].read(std::max(dly[v], 0.0f));
                const float c = cents[v] + depth[v] * static_cast<float>(std::sin(2.0 * std::numbers::pi * ph_[v]));
                ph_[v] += rate[v];
                ph_[v] -= std::floor(ph_[v]);
                const float ratio = std::clamp(std::exp2(c / 1200.0f) * (oct[v] ? 0.5f : 1.0f), 0.5f, 2.0f);
                float y = shift_[v][range].tick(late, ratio);
                fb_[v] = y;
                if (lowOn) y = eq_[v][0].tick(y);
                if (highOn) y = eq_[v][1].tick(y);
                yl += y * gl[v];
                yr += y * gr[v];
            }
            // the direct: panned like a stereo balance, waiting for the voices when ALIGN DIRECT is on
            const float al = direct_[0].process(xl, alignN), ar = direct_[1].process(xr, alignN);
            const float o = out_.next(wOut);
            L[i] = (yl + al * dg * std::min(dl, 1.0f)) * o;
            R[i] = (yr + ar * dg * std::min(dr, 1.0f)) * o;
            held_[0] = std::max(held_[0], std::fabs(L[i]));
            held_[1] = std::max(held_[1], std::fabs(R[i]));
        }
        publishMeter(0, std::max(dbOf(held_[0]), -100.0f));
        publishMeter(1, std::max(dbOf(held_[1]), -100.0f));
    }

private:
    void design() noexcept {
        const std::array<float, 5> now{p(LowGain), p(LowFreq), p(HighGain), p(HighFreq), p(FbHighPass)};
        if (now == key_) return;
        key_ = now;
        const double top = fs_ * 0.45;
        const C lo = C::make(T::LowShelf, fs_, std::min<double>(p(LowFreq), top), 0.7071, p(LowGain));
        const C hi = C::make(T::HighShelf, fs_, std::min<double>(p(HighFreq), top), 0.7071, p(HighGain));
        const C hp = C::make(T::HighPass, fs_, std::min<double>(p(FbHighPass), top), 0.7071);
        for (int v = 0; v < kVoices; ++v) {
            eq_[v][0].c = lo;
            eq_[v][1].c = hi;
            fbHp_[v].c = hp;
        }
    }

    double fs_ = 48000.0, ph_[kVoices]{};
    hc::fx::DelayLine late_[kVoices], direct_[2];
    PitchShift shift_[kVoices][2];
    hc::fx::Biquad eq_[kVoices][2], fbHp_[kVoices];
    std::array<float, 5> key_{};
    float fb_[kVoices]{}, held_[2]{}, resetPush_ = 0.0f, modPush_ = 0.0f;
    hc::fx::Smoothed out_;
};

} // namespace
} // namespace bodydouble

std::unique_ptr<hc::fx::Effect> makeBodyDouble() { return std::make_unique<bodydouble::Impl>(); }

} // namespace hem::rack
