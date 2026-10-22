// DEAD DROP — hybrid delay (see Hideout.hpp).
//
// One delay line per side (3.6 s + the modulation). The repeats go round through the filters, the
// ANALOG stage (saturation that also holds FEEDBACK past 100 % in, a little noise, a darker loop) and,
// with LOFI, a sample-and-hold at a quarter of the rate. PING PONG crosses the feedback over (the input
// goes into the starting side only).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace deaddrop {
namespace {
using namespace hideout;

const char* const kSyncN[] = {"HOST", "BPM", "MS"};
const char* const kNotesN[] = {"1/64", "1/32 T", "1/32", "1/16 T", "1/16", "1/16 D", "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D",
                               "1/2 T", "1/2", "1/2 D", "1 BAR", "2 BAR"};
constexpr double kBeats[] = {1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 0.25, 0.375, 1.0 / 3, 0.5, 0.75, 2.0 / 3, 1.0, 1.5, 4.0 / 3, 2.0, 3.0, 4.0, 8.0};
constexpr int kNotes = 17;
const char* const kStartN[] = {"LEFT", "RIGHT"};
const char* const kAnalogN[] = {"OFF", "1", "2", "3", "4"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"sync", "Sync", "", 0, 2, 0, Taper::Choice, kSyncN},
    {"bpm", "BPM", "", 40.0f, 300.0f, 120.0f},
    {"time", "Time", "ms", 1.0f, 3500.0f, 375.0f, Taper::Log},
    {"note", "Note", "", 0, kNotes - 1, 8, Taper::Choice, kNotesN},
    {"tap", "Tap", "", 0, 1, 0, Taper::Toggle, kPushN},
    {"dual", "Split Times", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"timer", "Time R", "ms", 1.0f, 3500.0f, 500.0f, Taper::Log},
    {"noter", "Note R", "", 0, kNotes - 1, 10, Taper::Choice, kNotesN},
    {"feedback", "Feedback", "%", 0.0f, 200.0f, 60.0f},
    {"pingpong", "Ping Pong", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"pingstart", "Ping Pong Start", "", 0, 1, 0, Taper::Choice, kStartN},
    {"phasel", "Phase L", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"phaser", "Phase R", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"highpass", "Hi-Pass", "Hz", 19.0f, 20000.0f, 19.0f, Taper::Log},
    {"lowpass", "Lo-Pass", "Hz", 20.0f, 21000.0f, 21000.0f, Taper::Log},
    {"link", "Tie Filters", "Hz", 20.0f, 20000.0f, 632.0f, Taper::Log},
    {"moddepth", "Mod Depth", "", 0.0f, 100.0f, 0.0f},
    {"modrate", "Mod Rate", "Hz", 0.1f, 6000.0f, 0.1f, Taper::Log},
    {"analog", "Vintage", "", 0, 4, 2, Taper::Choice, kAnalogN},
    {"lofi", "Lo-Res", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"OUTPUT", "dBFS", -48.0f, 0.0f}};
constexpr double kLineS = 3.7;
constexpr float kLinkHome = 632.0f;   // FILTER LINK's resting place: both filters where their own knobs put them

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        const int s = pick(p(Sync), 2);
        if (i == Time) return s == 2;
        if (i == Note) return s != 2;
        if (i == Bpm) return s == 1;
        if (i == TimeR) return p(Dual) >= 0.5f && s == 2;
        if (i == NoteR) return p(Dual) >= 0.5f && s != 2;
        if (i == PingStart) return p(PingPong) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : line_) d.prepare(static_cast<std::uint32_t>(kLineS * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : line_) d.clear();
        for (auto& c : hp_) c.clear();
        for (auto& c : lp_) c.clear();
        for (auto& c : dark_) c.clear();
        key_.fill(-1.0f);
        design();
        ph_ = 0.0;
        dly_[0] = dly_[1] = -1.0f;
        hold_[0] = hold_[1] = 0.0f;
        holdN_ = 0;
        rng_ = 0x51ED270Bu;
        push_ = p(Tap);
        since_ = 1e9;
        tapS_ = 0.0;
        tapKey_ = {p(Sync), p(Time), p(Note), p(Bpm)};
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        meterHold_.clear();
        publishMeter(0, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        // TAP: two presses 0.05–3.5 s apart; it holds until SYNC, TIME, NOTE or BPM is moved
        since_ += n;
        if (p(Tap) != push_) {
            push_ = p(Tap);
            const double s = (since_ - n) / fs_;
            if (s >= 0.05 && s <= 3.5) {
                tapS_ = s;
                tapKey_ = {p(Sync), p(Time), p(Note), p(Bpm)};
            }
            since_ = n;
        }
        if (std::array<float, 4>{p(Sync), p(Time), p(Note), p(Bpm)} != tapKey_) tapS_ = 0.0;
        const int sync = pick(p(Sync), 2);
        double bpm = sync == 0 ? tempo() : p(Bpm);
        if (tapS_ > 0.0 && sync == 1) bpm = 60.0 / tapS_;   // BPM: the taps set the tempo (a quarter note)
        auto timeMs = [&](int timeP, int noteP) {
            if (sync == 2) return tapS_ > 0.0 ? tapS_ * 1000.0 : static_cast<double>(p(timeP));
            int note = pick(p(noteP), kNotes - 1);
            if (tapS_ > 0.0 && sync == 0) {   // HOST: the tapped time, snapped to the nearest note
                double best = 1e9;
                for (int k = 0; k < kNotes; ++k) {
                    const double d = std::fabs(std::log(kBeats[k] * 60.0 / bpm / tapS_));
                    if (d < best) { best = d; note = k; }
                }
            }
            return kBeats[note] * 60000.0 / bpm;
        };
        const double msL = std::clamp(timeMs(Time, Note), 1.0, 3500.0);
        const double msR = p(Dual) >= 0.5f ? std::clamp(timeMs(TimeR, NoteR), 1.0, 3500.0) : msL;
        const float want[2] = {static_cast<float>(msL * 0.001 * fs_), static_cast<float>(msR * 0.001 * fs_)};
        const float fb = p(Feedback) * 0.01f;
        const bool ping = p(PingPong) >= 0.5f, startR = p(PingStart) >= 0.5f;
        const float phL = p(PhaseL) >= 0.5f ? -1.0f : 1.0f, phR = p(PhaseR) >= 0.5f ? -1.0f : 1.0f;
        const int analog = pick(p(Analog), 4);
        const float drive = 1.0f + 0.5f * static_cast<float>(analog), noise = analog > 0 ? std::pow(10.0f, (-96.0f + 6.0f * analog) / 20.0f) : 0.0f;
        const bool lofi = p(LoFi) >= 0.5f;
        const double rate = p(ModRate);
        const float depth = p(ModDepth) * 0.01f * 0.004f * static_cast<float>(fs_);   // at 100: ±4 ms
        const float glide = coefMs(fs_, 50.0), wMix = p(Mix) * 0.01f, wOut = hc::fx::dbToGain(p(Output));
        const float maxD = static_cast<float>(kLineS * fs_) - 8.0f;
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            const float mod = depth * static_cast<float>(std::sin(2.0 * std::numbers::pi * ph_));
            ph_ += rate / fs_;
            ph_ -= std::floor(ph_);
            float y[2];
            for (int c = 0; c < 2; ++c) {
                dly_[c] = dly_[c] < 0.0f ? want[c] : want[c] + glide * (dly_[c] - want[c]);
                y[c] = line_[c].read(std::clamp(dly_[c] + mod * (c == 0 ? 1.0f : -1.0f), 1.0f, maxD) - 1.0f);
            }
            // the loop: filters, analog, lofi
            float f[2];
            for (int c = 0; c < 2; ++c) {
                float v = y[c];
                if (hpOn_) v = hp_[c].tick(v);
                if (lpOn_) v = lp_[c].tick(v);
                if (analog > 0) {
                    v = dark_[c].tick(v);
                    rng_ = rng_ * 1664525u + 1013904223u;
                    // the noise rides with what goes round (digital silence stays silent)
                    const float ride = std::min(1.0f, std::fabs(v) * 100.0f);
                    v = std::tanh(drive * v) / drive + ride * noise * (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f);
                } else if (fb > 1.0f) {
                    v = std::tanh(v);   // past 100 % even the clean loop needs a ceiling
                }
                f[c] = v;
            }
            if (lofi) {   // a quarter of the sample rate
                if (holdN_ == 0) { hold_[0] = f[0]; hold_[1] = f[1]; }
                holdN_ = (holdN_ + 1) % 4;
                f[0] = hold_[0];
                f[1] = hold_[1];
            }
            if (ping) {
                const float in = 0.5f * (xl + xr);
                line_[0].write((startR ? 0.0f : in) + fb * f[1]);
                line_[1].write((startR ? in : 0.0f) + fb * f[0]);
            } else {
                line_[0].write(xl + fb * f[0]);
                line_[1].write(xr + fb * f[1]);
            }
            const float m = mix_.next(wMix), o = out_.next(wOut);
            const float wl = f[0] * phL, wr = f[1] * phR;
            L[i] = (xl + m * (wl - xl)) * o;
            R[i] = (xr + m * (wr - xr)) * o;
            pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])});
        }
        publishMeter(0, meterHold_.push(std::max(dbOf(pk), -100.0f), n, fs_));
    }

private:
    void design() noexcept {
        const std::array<float, 4> now{p(HighPass), p(LowPass), p(Link), p(Analog)};
        if (now == key_) return;
        key_ = now;
        const float shift = p(Link) / kLinkHome;
        hpOn_ = p(HighPass) > 19.5f;
        lpOn_ = p(LowPass) < 20500.0f;
        const double top = fs_ * 0.45;
        const C h = C::make(T::HighPass, fs_, std::clamp(static_cast<double>(p(HighPass) * shift), 10.0, top), 0.7071);
        const C l = C::make(T::LowPass, fs_, std::clamp(static_cast<double>(p(LowPass) * shift), 20.0, top), 0.7071);
        const C d = C::make(T::LowPass, fs_, std::min(16000.0 - 2000.0 * pick(p(Analog), 4), top), 0.7071);
        for (int c = 0; c < 2; ++c) {
            hp_[c].c = h;
            lp_[c].c = l;
            dark_[c].c = d;
        }
    }

    double fs_ = 48000.0, ph_ = 0.0, since_ = 1e9, tapS_ = 0.0;
    hc::fx::DelayLine line_[2];
    hc::fx::Biquad hp_[2], lp_[2], dark_[2];
    std::array<float, 4> key_{}, tapKey_{};
    bool hpOn_ = false, lpOn_ = false;
    float dly_[2]{-1.0f, -1.0f}, hold_[2]{}, push_ = 0.0f;
    maxim::PeakHold meterHold_;
    int holdN_ = 0;
    std::uint32_t rng_ = 0x51ED270Bu;
    hc::fx::Smoothed mix_, out_;
};

} // namespace
} // namespace deaddrop

std::unique_ptr<hc::fx::Effect> makeDeadDrop() { return std::make_unique<deaddrop::Impl>(); }

} // namespace hem::rack
