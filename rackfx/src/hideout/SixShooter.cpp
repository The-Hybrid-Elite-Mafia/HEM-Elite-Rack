// SIX SHOOTER — six-tap delay with per-tap EQ, a modulator and a filtered feedback path (see Hideout.hpp).
//
// One delay line per side (7 s). Each tap reads it at its own time (snapped to the GRID when SNAP is on,
// swung by the MODULATOR), goes through its EQ, GAIN and ROTATE, and the taps are added to the DIRECT
// sound. FEEDBACK NORMAL sends the taps' sum back into the line; FEEDBACK TAP reads its own time; either
// way through its filter, GAIN and ROTATE.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace sixshooter {
namespace {
using namespace hideout;

const char* const kEqN[] = {"WIDE BELL", "NARROW BELL", "HI SHELF", "LO SHELF", "HI-PASS", "LO-PASS"};
const char* const kSyncN[] = {"HOST", "MANUAL"};
const char* const kGridN[] = {"FREE", "1/4", "1/4 T", "1/8", "1/8 T", "1/16"};
constexpr double kGridBeats[] = {0.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25};
const char* const kModSyncN[] = {"TEMPO", "MANUAL"};
const char* const kModNoteN[] = {"4 BEATS", "2 BEATS", "1 BEAT", "1/2", "1/4", "1/8"};
constexpr double kModBeats[] = {4, 2, 1, 0.5, 0.25, 0.125};
const char* const kFbModeN[] = {"NORMAL", "TAP"};
const char* const kFbFilterN[] = {"LO-PASS", "LO SHELF", "LO DECAY"};
const char* const kPushN[] = {"A", "B"};

#define HEM_TAP(n, ms, g, rot)                                                                     \
    {"tap" #n "on", "Tap " #n " On", "", 0, 1, 1, Taper::Toggle, kOnOff},                          \
    {"tap" #n "delay", "Tap " #n " Delay", "ms", 1.0f, 6000.0f, ms, Taper::Log},                  \
    {"tap" #n "gain", "Tap " #n " Gain", "dB", -24.0f, 0.0f, g},                                   \
    {"tap" #n "rotate", "Tap " #n " Rotate", "deg", -45.0f, 45.0f, rot},                           \
    {"tap" #n "eqon", "Tap " #n " EQ", "", 0, 1, 0, Taper::Toggle, kOnOff},                        \
    {"tap" #n "eqtype", "Tap " #n " EQ Type", "", 0, 5, 0, Taper::Choice, kEqN},                   \
    {"tap" #n "eqfreq", "Tap " #n " EQ Freq", "Hz", 100.0f, 20000.0f, 2000.0f, Taper::Log},       \
    {"tap" #n "eqgain", "Tap " #n " EQ Gain", "dB", -18.0f, 18.0f, 0.0f}
const ParamInfo kParams[kCount] = {
    HEM_TAP(1, 125.0f, -6.0f, -30.0f), HEM_TAP(2, 250.0f, -8.0f, 30.0f), HEM_TAP(3, 375.0f, -10.0f, -20.0f),
    HEM_TAP(4, 500.0f, -12.0f, 20.0f), HEM_TAP(5, 625.0f, -15.0f, -10.0f), HEM_TAP(6, 750.0f, -18.0f, 10.0f),
    {"directon", "Direct", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"directgain", "Direct Gain", "dB", -24.0f, 0.0f, 0.0f},
    {"directrotate", "Direct Rotate", "deg", -45.0f, 45.0f, 0.0f},
    {"sync", "Tempo", "", 0, 1, 0, Taper::Choice, kSyncN},
    {"bpm", "BPM", "", 40.0f, 1200.0f, 120.0f, Taper::Log},
    {"grid", "Grid", "", 0, 5, 0, Taper::Choice, kGridN},
    {"snap", "Snap", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"tap", "Tap Tempo", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"modon", "Modulator", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"modsync", "Mod Sync", "", 0, 1, 1, Taper::Choice, kModSyncN},
    {"modrate", "Mod Rate", "Hz", 0.0f, 20.0f, 1.0f},
    {"modnote", "Mod Note", "", 0, 5, 2, Taper::Choice, kModNoteN},
    {"moddepth", "Mod Depth", "ms", 0.0f, 500.0f, 5.0f},
    {"fbon", "Feedback", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"fbmode", "Feedback Mode", "", 0, 1, 0, Taper::Choice, kFbModeN},
    {"fbgain", "Feedback Gain", "%", 0.0f, 100.0f, 30.0f},
    {"fbdelay", "Feedback Delay", "ms", 1.0f, 6000.0f, 500.0f, Taper::Log},
    {"fbrotate", "Feedback Rotate", "deg", -45.0f, 45.0f, 0.0f},
    {"fbfilter", "Feedback Filter", "", 0, 2, 0, Taper::Choice, kFbFilterN},
    {"fbfreq", "Feedback Freq", "Hz", 100.0f, 20000.0f, 6000.0f, Taper::Log},
    {"fbfiltergain", "Feedback Filter Gain", "dB", -18.0f, 0.0f, -3.0f},
    {"output", "Output", "dB", -12.0f, 12.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
#undef HEM_TAP
const hc::fx::MeterInfo kMeters[] = {{"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}};
constexpr double kLineS = 7.0;

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }

C tapEq(int type, double fs, double hz, double gainDb) noexcept {
    hz = std::min(hz, fs * 0.45);
    switch (type) {
    case 0: return C::make(T::Bell, fs, hz, 0.7, gainDb);
    case 1: return C::make(T::Bell, fs, hz, 2.5, gainDb);
    case 2: return C::make(T::HighShelf, fs, hz, 0.7071, gainDb);
    case 3: return C::make(T::LowShelf, fs, hz, 0.7071, gainDb);
    case 4: return C::make(T::HighPass, fs, hz, 0.7071);
    default: return C::make(T::LowPass, fs, hz, 0.7071);
    }
}
C fbFilter(int type, double fs, double hz, double gainDb) noexcept {
    hz = std::min(hz, fs * 0.45);
    switch (type) {
    case 0: return C::make(T::LowPass, fs, hz, 0.7071);
    case 1: return C::make(T::LowShelf, fs, hz, 0.7071, gainDb);
    default: return C::make(T::HighShelf, fs, hz, 0.5, std::min(gainDb, 0.0));   // LO DECAY: a gentle cut-only top shelf
    }
}
// rotate a stereo pair by deg degrees (+ = toward the right)
inline void rotate(float& l, float& r, float c, float s) noexcept {
    const float a = c * l - s * r, b = s * l + c * r;
    l = a;
    r = b;
}

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i < kTaps * kStride) {
            const int t = i / kStride, k = i % kStride;
            if (k == On) return true;
            if (p(tap(t, On)) < 0.5f) return false;
            if (k == EqType || k == EqFreq) return p(tap(t, EqOn)) >= 0.5f;
            if (k == EqGain) return p(tap(t, EqOn)) >= 0.5f && pick(p(tap(t, EqType)), 5) < 4;
            return true;
        }
        if (i == Bpm || i == Tap) return p(Sync) >= 0.5f;
        if (i == ModRate) return p(ModOn) >= 0.5f && p(ModSync) >= 0.5f;
        if (i == ModNote) return p(ModOn) >= 0.5f && p(ModSync) < 0.5f;
        if (i == ModSync || i == ModDepth) return p(ModOn) >= 0.5f;
        if (i == FbDelay) return p(FbOn) >= 0.5f && p(FbMode) >= 0.5f;
        if (i == FbFilterGain) return p(FbOn) >= 0.5f && pick(p(FbFilter), 2) > 0;
        if (i > FbOn && i <= FbFilterGain) return p(FbOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : line_) d.prepare(static_cast<std::uint32_t>(kLineS * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : line_) d.clear();
        for (auto& t : eq_) for (auto& b : t) b.clear();
        for (auto& b : fbEq_) b.clear();
        key_.fill(-999.0f);
        design();
        modPh_ = 0.0;
        fbL_ = fbR_ = 0.0f;
        held_[0] = held_[1] = 0.0f;
        push_ = p(Reset);
        tapPush_ = p(Tap);
        sinceTap_ = 1e9;
        tappedBpm_ = 0.0f;
        bpmAtTap_ = p(Bpm);
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        if (p(Reset) != push_) {
            push_ = p(Reset);
            held_[0] = held_[1] = 0.0f;
        }
        // TAP: two presses 0.1–3 s apart set the tempo (until BPM is turned)
        sinceTap_ += n;
        if (p(Tap) != tapPush_) {
            tapPush_ = p(Tap);
            const double s = (sinceTap_ - n) / fs_;
            if (s >= 0.1 && s <= 3.0) {
                tappedBpm_ = static_cast<float>(60.0 / s);
                bpmAtTap_ = p(Bpm);
            }
            sinceTap_ = n;
        }
        if (p(Bpm) != bpmAtTap_) tappedBpm_ = 0.0f;
        const double bpm = p(Sync) < 0.5f ? tempo() : (tappedBpm_ > 0.0f ? tappedBpm_ : p(Bpm));
        // the taps' times (ms -> samples), snapped to the grid when asked
        const int grid = pick(p(Grid), 5);
        const double step = kGridBeats[grid] * 60000.0 / bpm;
        float delay[kTaps], gain[kTaps], rc[kTaps], rs[kTaps];
        bool on[kTaps], eqOn[kTaps];
        for (int t = 0; t < kTaps; ++t) {
            double ms = p(tap(t, Delay));
            if (grid > 0 && p(Snap) >= 0.5f) ms = std::max(step, std::round(ms / step) * step);
            delay[t] = static_cast<float>(std::min(ms, 6000.0) * 0.001 * fs_);
            on[t] = p(tap(t, On)) >= 0.5f;
            eqOn[t] = p(tap(t, EqOn)) >= 0.5f;
            gain[t] = on[t] ? hc::fx::dbToGain(p(tap(t, Gain))) : 0.0f;
            const float a = p(tap(t, Rotate)) * static_cast<float>(std::numbers::pi / 180.0);
            rc[t] = std::cos(a);
            rs[t] = std::sin(a);
        }
        const bool dirOn = p(DirectOn) >= 0.5f;
        const float dirG = dirOn ? hc::fx::dbToGain(p(DirectGain)) : 0.0f, da = p(DirectRotate) * static_cast<float>(std::numbers::pi / 180.0);
        const float dc = std::cos(da), ds = std::sin(da);
        const bool modOn = p(ModOn) >= 0.5f;
        const double modHz = p(ModSync) < 0.5f ? bpm / 60.0 / kModBeats[pick(p(ModNote), 5)] : p(ModRate);
        const float depth = modOn ? p(ModDepth) * 0.001f * static_cast<float>(fs_) * 0.5f : 0.0f;
        const bool fbOn = p(FbOn) >= 0.5f, fbTap = p(FbMode) >= 0.5f;
        const float fbG = fbOn ? p(FbGain) * 0.01f : 0.0f, fbD = p(FbDelay) * 0.001f * static_cast<float>(fs_);
        const float fa = p(FbRotate) * static_cast<float>(std::numbers::pi / 180.0), fc = std::cos(fa), fs = std::sin(fa);
        const float wOut = hc::fx::dbToGain(p(Output));
        const float maxD = static_cast<float>(kLineS * fs_) - 4.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            // the modulator swings every tap's time (a sine, half the depth either way, each tap a little apart)
            const double ph = modPh_;
            modPh_ += modHz / fs_;
            modPh_ -= std::floor(modPh_);
            float yl = 0.0f, yr = 0.0f;
            for (int t = 0; t < kTaps; ++t) {
                if (!on[t]) continue;
                const float m = depth * static_cast<float>(1.0 + std::sin(2.0 * std::numbers::pi * (ph + t / 6.0)));
                const float d = std::clamp(delay[t] + m, 1.0f, maxD);
                float tl = line_[0].read(d - 1.0f), tr = line_[1].read(d - 1.0f);
                if (eqOn[t]) {
                    tl = eq_[t][0].tick(tl);
                    tr = eq_[t][1].tick(tr);
                }
                tl *= gain[t];
                tr *= gain[t];
                rotate(tl, tr, rc[t], rs[t]);
                yl += tl;
                yr += tr;
            }
            // the feedback: the taps' sum (NORMAL) or its own time (TAP), filtered and turned
            float fl = 0.0f, fr = 0.0f;
            if (fbOn) {
                fl = fbTap ? line_[0].read(std::clamp(fbD, 1.0f, maxD) - 1.0f) : yl;
                fr = fbTap ? line_[1].read(std::clamp(fbD, 1.0f, maxD) - 1.0f) : yr;
                fl = fbEq_[0].tick(fl) * fbG;
                fr = fbEq_[1].tick(fr) * fbG;
                rotate(fl, fr, fc, fs);
            }
            line_[0].write(xl + fl);
            line_[1].write(xr + fr);
            float ol = xl * dirG, orr = xr * dirG;
            rotate(ol, orr, dc, ds);
            const float o = out_.next(wOut);
            L[i] = (ol + yl) * o;
            R[i] = (orr + yr) * o;
            held_[0] = std::max(held_[0], std::fabs(L[i]));
            held_[1] = std::max(held_[1], std::fabs(R[i]));
        }
        publishMeter(0, std::max(dbOf(held_[0]), -100.0f));
        publishMeter(1, std::max(dbOf(held_[1]), -100.0f));
    }

private:
    void design() noexcept {
        std::array<float, kTaps * 3 + 3> now{};
        for (int t = 0; t < kTaps; ++t) {
            now[static_cast<std::size_t>(t * 3)] = p(tap(t, EqType));
            now[static_cast<std::size_t>(t * 3 + 1)] = p(tap(t, EqFreq));
            now[static_cast<std::size_t>(t * 3 + 2)] = p(tap(t, EqGain));
        }
        now[kTaps * 3] = p(FbFilter);
        now[kTaps * 3 + 1] = p(FbFreq);
        now[kTaps * 3 + 2] = p(FbFilterGain);
        if (now == key_) return;
        key_ = now;
        for (int t = 0; t < kTaps; ++t) {
            const C c = tapEq(pick(p(tap(t, EqType)), 5), fs_, p(tap(t, EqFreq)), p(tap(t, EqGain)));
            eq_[t][0].c = eq_[t][1].c = c;
        }
        fbEq_[0].c = fbEq_[1].c = fbFilter(pick(p(FbFilter), 2), fs_, p(FbFreq), p(FbFilterGain));
    }

    double fs_ = 48000.0, modPh_ = 0.0, sinceTap_ = 1e9;
    hc::fx::DelayLine line_[2];
    hc::fx::Biquad eq_[kTaps][2], fbEq_[2];
    std::array<float, kTaps * 3 + 3> key_{};
    float fbL_ = 0.0f, fbR_ = 0.0f, held_[2]{}, push_ = 0.0f, tapPush_ = 0.0f, tappedBpm_ = 0.0f, bpmAtTap_ = 0.0f;
    hc::fx::Smoothed out_;
};

} // namespace
} // namespace sixshooter

std::unique_ptr<hc::fx::Effect> makeSixShooter() { return std::make_unique<sixshooter::Impl>(); }

} // namespace hem::rack
