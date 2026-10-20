// REEL MONEY — tube tape machine with a tape echo (see Garage.hpp).
//
// In order: INTO TAPE, the input tube stage (4x oversampled harmonics), then two paths of the same length:
//   TAPE   pre-emphasis, the tape's saturation (TAPE LEVEL drives it; the level is made up), de-emphasis,
//          the head bump and the top-end loss of the SPEED and BIAS, WOBBLE (a moving delay and a little level
//          flutter), the echo taken off the tape (ECHO ...), HISS;
//   INPUT  the tube stage only, delayed to match.
// MONITOR crossfades between them, then OFF TAPE. The latency is the same in both: kReelLatency.
#include "hem/rack/Garage.hpp"

#include "EqKit.hpp"
#include "ToneCommon.hpp"
#include "../arsenal/Vintage.hpp"

namespace hem::rack {
namespace reelmoney {
namespace {
using namespace arsenal;
using eqkit::BandSet;
using eqkit::Shape;

const char* const kSpeedN[] = {"7.5 IPS", "15 IPS"};
const char* const kMonitorN[] = {"TAPE", "INPUT"};
const char* const kBiasN[] = {"NORMAL", "HOT"};
const char* const kStyleN[] = {"SLAP", "REPEATS"};
const char* const kMeterN[] = {"IN", "OUT"};
const ParamInfo kParams[kCount] = {
    {"speed", "Speed", "", 0, 1, 1, Taper::Choice, kSpeedN},
    {"monitor", "Monitor", "", 0, 1, 0, Taper::Choice, kMonitorN},
    {"bias", "Bias", "", 0, 1, 1, Taper::Choice, kBiasN},
    {"intotape", "Into Tape", "dB", -18.0f, 18.0f, 0.0f},
    {"offtape", "Off Tape", "dB", -18.0f, 18.0f, 0.0f},
    {"link", "Link", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"tapelevel", "Tape Level", "nWb/m", 150.0f, 1020.0f, 185.0f, Taper::Log},
    {"wobble", "Wobble", "", 0.0f, 100.0f, 50.0f},
    {"hiss", "Hiss", "dB", -41.0f, 0.0f, -41.0f},
    {"echotime", "Echo Time", "ms", 1.0f, 500.0f, 133.0f, Taper::Log},
    {"echostyle", "Echo Style", "", 0, 1, 1, Taper::Choice, kStyleN},
    {"echolevel", "Echo Level", "", 0.0f, 100.0f, 0.0f},
    {"echotone", "Echo Tone", "Hz", 200.0f, 16000.0f, 3500.0f, Taper::Log},
    {"meter", "Meter", "", 0, 1, 1, Taper::Choice, kMeterN},
    {"vuzero", "VU Zero", "dBFS", -24.0f, -8.0f, -18.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};
constexpr std::uint32_t kWowBase = 64;   // the moving delay's centre (samples); room for the deepest wobble
constexpr double kMaxEchoMs = 501.0;
constexpr float kRefFlux = 185.0f;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kReelLatency; }
    bool paramActive(int i) const noexcept override {
        const bool tape = p(Monitor) < 0.5f;
        if (i == EchoTime || i == EchoStyle || i == EchoTone) return tape && p(EchoLevel) > 0.0f;
        if (i == Speed || i == Bias || i == TapeLevel || i == Wobble || i == Hiss || i == EchoLevel) return tape;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        tube_.prepare();
        tape_.prepare();
        emph_.prepare(fs);
        tone_.prepare(fs);
        for (auto& d : wow_) d.prepare(2 * kWowBase + 8);
        matchIn_.prepare(kReelLatency - 23);
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(kMaxEchoMs * 0.001 * fs) + 8);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        fade_ = static_cast<float>(1.0 / (0.03 * fs));
        reset();
    }
    void reset() noexcept override {
        tube_.clear(true);
        tape_.clear(true);
        load();
        emph_.clear();
        tone_.clear();
        for (auto& d : wow_) d.clear();
        matchIn_.clear();
        for (auto& d : echo_) d.clear();
        for (auto& f : echoLp_) f.clear();
        for (auto& f : hissF_) for (auto& b : f) b.clear();
        vuIn_.clear();
        vuOut_.clear();
        wowPh_ = flPh_ = 0.0;
        drift_ = driftT_ = 0.0f;
        rng_ = 0x1234567u;
        tapeMix_ = p(Monitor) < 0.5f ? 1.0f : 0.0f;
        in_.prepare(fs_, 20, inGain());
        out_.prepare(fs_, 20, outGain());
        flux_.prepare(fs_, 40, fluxGain());
        wob_.prepare(fs_, 50, p(Wobble) * 0.01f);
        echoT_.prepare(fs_, 80, echoSamples());
        echoL_.prepare(fs_, 30, echoLevel());
        hiss_.prepare(fs_, 30, hissLevel());
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        load();
        const float wIn = inGain();
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = in_.next(wIn);
            L[i] *= g;
            R[i] *= g;
            vuIn_.tick(L[i], R[i]);
        }
        // the input tube stage: lopsided (2nd harmonic), clean when quiet
        tube_.run(L, R, n, true, 1.0f, [](float x) {
            const float u = 0.9f * x;
            return std::tanh(u + 0.12f * u * u) / 0.9f;
        });
        // a copy of the tube stage for INPUT monitoring, delayed to the tape path's length
        for (std::uint32_t k = 0; k < n; k += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - k);
            for (std::uint32_t i = 0; i < m; ++i) {
                inCopy_[0][i] = matchIn_.l.process(L[k + i], kReelLatency - 23);
                inCopy_[1][i] = matchIn_.r.process(R[k + i], kReelLatency - 23);
            }
            runTape(L + k, R + k, m);
        }
        vuOut();
    }

private:
    static constexpr std::uint32_t kChunk = 64;

    float inGain() const noexcept { return hc::fx::dbToGain(p(IntoTape)); }
    float outGain() const noexcept { return hc::fx::dbToGain(p(OffTape) - (p(Link) >= 0.5f ? p(IntoTape) : 0.0f)); }
    float fluxGain() const noexcept { return p(TapeLevel) / kRefFlux; }
    float echoSamples() const noexcept { return static_cast<float>(p(EchoTime) * 0.001 * fs_); }
    float echoLevel() const noexcept { return p(EchoLevel) * 0.01f; }
    float hissLevel() const noexcept { return p(Hiss) <= -40.95f ? 0.0f : hc::fx::dbToGain(p(Hiss) - 62.0f); }   // 0 dB: about -62 dBFS RMS
    bool fast() const noexcept { return p(Speed) >= 0.5f; }
    bool hot() const noexcept { return p(Bias) >= 0.5f; }

    // the speed / bias filters, and the echo's and the hiss's tone
    void load() noexcept {
        const std::array<float, 3> key{p(Speed), p(Bias), p(EchoTone)};
        if (key == key_) return;
        key_ = key;
        const bool f = fast();
        // pre-emphasis before the tape, de-emphasis after (equal and opposite)
        emphDb_ = f ? 6.0f : 9.0f;
        emph_.set(0, BandSet{Shape::HighShelf, 3000.0f, emphDb_, 0.7071f});
        tone_.set(0, BandSet{Shape::HighShelf, 3000.0f, -emphDb_, 0.7071f});
        // the head bump and the dip above it
        tone_.set(1, BandSet{Shape::Bell, f ? 50.0f : 35.0f, f ? 1.5f : 2.0f, 1.2f});
        tone_.set(2, BandSet{Shape::Bell, f ? 110.0f : 75.0f, -0.8f, 1.4f});
        // the top end: slower tape and hotter bias lose more
        tone_.set(3, BandSet{Shape::HighCut, f ? 21000.0f : 14000.0f, 0.0f, 0.6f});
        tone_.set(4, hot() ? BandSet{Shape::HighShelf, 10000.0f, -1.5f, 0.7071f} : BandSet{});
        const C lp = C::make(T::LowPass, fs_, std::min<double>(p(EchoTone), fs_ * 0.45), 0.7071);
        for (auto& b : echoLp_) b.c = lp;
        // hiss: 15 IPS sits an octave higher than 7.5
        const double hz = f ? 8000.0 : 4000.0;
        for (auto& ch : hissF_) {
            ch[0].c = C::make(T::HighPass, fs_, hz * 0.25, 0.7071);
            ch[1].c = C::make(T::LowPass, fs_, std::min(hz * 2.5, fs_ * 0.45), 0.7071);
        }
    }

    float noise() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;
    }

    void runTape(float* L, float* R, std::uint32_t m) noexcept {
        // pre-emphasis
        float a[2][kChunk];
        for (std::uint32_t i = 0; i < m; ++i) {
            emph_.step();
            a[0][i] = emph_.run(0, L[i]);
            a[1][i] = emph_.run(1, R[i]);
        }
        // the tape: TAPE LEVEL drives it, the level comes back down after; NORMAL bias distorts more than HOT
        const float fl = flux_.next(fluxGain());
        const float k = (hot() ? 1.0f : 1.4f) * fl;
        tape_.run(a[0], a[1], m, true, 1.0f, [k](float x) {
            const float u = k * x;
            return (u / std::sqrt(1.0f + 0.6f * u * u)) / k;
        });
        const bool tapeOn = p(Monitor) < 0.5f;
        const float wWob = p(Wobble) * 0.01f, wEchoT = echoSamples(), wEchoL = echoLevel(), wHiss = hissLevel();
        const bool repeats = p(EchoStyle) >= 0.5f;
        const float wOut = outGain();
        const double wowInc = 2.0 * std::numbers::pi * (fast() ? 0.55 : 0.4) / fs_, flInc = 2.0 * std::numbers::pi * (fast() ? 9.0 : 6.0) / fs_;
        for (std::uint32_t i = 0; i < m; ++i) {
            tone_.step();
            float l = tone_.run(0, a[0][i]), r = tone_.run(1, a[1][i]);
            // WOBBLE: a slow drift, the wow and the flutter move the read point; the level flutters a little
            const float w = wob_.next(wWob);
            wowPh_ += wowInc;
            if (wowPh_ > 2.0 * std::numbers::pi) wowPh_ -= 2.0 * std::numbers::pi;
            flPh_ += flInc;
            if (flPh_ > 2.0 * std::numbers::pi) flPh_ -= 2.0 * std::numbers::pi;
            if ((++tick_ & 1023u) == 0) driftT_ = noise();
            drift_ += (driftT_ - drift_) * 2e-4f;
            const float mod = w * (24.0f * static_cast<float>(std::sin(wowPh_)) + 3.0f * static_cast<float>(std::sin(flPh_)) + 12.0f * drift_);
            const float lvl = 1.0f + w * 0.02f * static_cast<float>(std::sin(flPh_ * 0.5 + 1.0));
            wow_[0].write(l);
            wow_[1].write(r);
            l = wow_[0].read(static_cast<float>(kWowBase) + mod) * lvl;
            r = wow_[1].read(static_cast<float>(kWowBase) + mod) * lvl;
            // the echo off the tape: SLAP = one repeat, REPEATS = it feeds back
            const float et = echoT_.next(wEchoT), el = echoL_.next(wEchoL);
            const float back[2] = {echoLp_[0].tick(echo_[0].read(et)), echoLp_[1].tick(echo_[1].read(et))};
            const float fb = repeats ? 0.55f * el : 0.0f;
            echo_[0].write(l + fb * back[0]);
            echo_[1].write(r + fb * back[1]);
            l += el * back[0];
            r += el * back[1];
            // HISS
            const float h = hiss_.next(wHiss);
            if (h > 0.0f) {
                l += h * 2.0f * hissF_[0][1].tick(hissF_[0][0].tick(noise()));
                r += h * 2.0f * hissF_[1][1].tick(hissF_[1][0].tick(noise()));
            }
            // MONITOR: the tape, or the tube stage only (same delay)
            tapeMix_ = tapeOn ? std::min(1.0f, tapeMix_ + fade_) : std::max(0.0f, tapeMix_ - fade_);
            l = inCopy_[0][i] + tapeMix_ * (l - inCopy_[0][i]);
            r = inCopy_[1][i] + tapeMix_ * (r - inCopy_[1][i]);
            const float o = out_.next(wOut);
            L[i] = l * o;
            R[i] = r * o;
            vuOut_.tick(L[i], R[i]);
        }
    }
    void vuOut() noexcept {
        const float cal = -p(VuZero);
        publishMeter(0, vuIn_.vu(cal));
        publishMeter(1, vuOut_.vu(cal));
    }

    double fs_ = 48000.0, wowPh_ = 0.0, flPh_ = 0.0;
    tone::Drive4 tube_, tape_;
    eqkit::IirBands<1> emph_;
    eqkit::IirBands<5> tone_;
    hc::fx::DelayLine wow_[2], echo_[2];
    Delay2 matchIn_;
    hc::fx::Biquad echoLp_[2];
    std::array<std::array<hc::fx::Biquad, 2>, 2> hissF_{};
    Vu vuIn_, vuOut_;
    std::array<float, 3> key_{-1.0f, -1.0f, -1.0f};
    float inCopy_[2][kChunk]{};
    float emphDb_ = 6.0f, fade_ = 0.001f, tapeMix_ = 1.0f, drift_ = 0.0f, driftT_ = 0.0f;
    std::uint32_t rng_ = 0x1234567u, tick_ = 0;
    hc::fx::Smoothed in_, out_, flux_, wob_, echoT_, echoL_, hiss_;
};

} // namespace
} // namespace reelmoney

std::unique_ptr<hc::fx::Effect> makeReelMoney() { return std::make_unique<reelmoney::Impl>(); }

} // namespace hem::rack
