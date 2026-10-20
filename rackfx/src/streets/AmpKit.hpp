#pragma once
// Guitar and bass amp building blocks (private to rackfx): TORCH, HOT WIRE, BOTTOM FEEDER.
//
//  * Preamp: a voicing (input low cut, a pre-emphasis bell) and 1..3 clipping stages with coupling between
//    them, then a softer power stage, run 4x oversampled. Gain in dB; the level after it is made up.
//  * Cabinets: filter models of a speaker in its box, each with a microphone's colour on top (dynamic,
//    condenser or ribbon; on or off the speaker's axis). Our own models, not recordings.
//  * EarlyRoom: a few early reflections (no tail) for the "room" knobs.
#include "../tone/ToneCommon.hpp"

#include <array>

namespace hem::rack::amps {
using namespace tone;

struct Voice {
    double inHp, preHz, preDb;   // the input's low cut and a pre-emphasis bell
    float bias;                  // how lopsided the clipping is (even harmonics)
    int stages;                  // clipping stages (1 = clean amp)
};

// A 4x oversampled preamp + power stage for both channels, in chunks of 64 (no allocation).
class Preamp {
public:
    void prepare(double fs) {
        fs_ = fs;
        interK_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 140.0 / (fs * OS::kFactor)));
        clear();
    }
    void clear() noexcept {
        for (auto& o : os_) o.clear();
        for (auto& ch : inter_) ch.fill(0.0f);
    }
    // gain: linear into the first stage; makeup applied after
    void run(float* L, float* R, std::uint32_t n, const Voice& v, float gain, float makeup) noexcept {
        float* io[2] = {L, R};
        float x4[kChunk * OS::kFactor];
        for (std::uint32_t off = 0; off < n; off += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - off);
            for (int c = 0; c < 2; ++c) {
                os_[c].up(io[c] + off, x4, m);
                auto& hp = inter_[static_cast<std::size_t>(c)];
                for (std::uint32_t j = 0; j < m * OS::kFactor; ++j) {
                    float s = stage(x4[j] * gain, v.bias);
                    for (int k = 1; k < v.stages; ++k) {
                        const float lp = s + interK_ * (hp[static_cast<std::size_t>(k)] - s);
                        hp[static_cast<std::size_t>(k)] = lp;
                        s = stage((s - lp) * 2.0f, v.bias * 0.7f);
                    }
                    x4[j] = softClip(s * 1.4f) / 1.4f * makeup;
                }
                os_[c].down(x4, io[c] + off, m);
            }
        }
    }
    static constexpr std::uint32_t latency() noexcept { return OS::latency(); }

private:
    static constexpr std::uint32_t kChunk = 64;
    static float stage(float x, float bias) noexcept { return softClip(x + bias) - softClip(bias); }
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::array<float, 4>, 2> inter_{};
    float interK_ = 0.0f;
};

// ---- cabinets and microphones ----------------------------------------------------------------
struct CabSpec {
    double hpHz, hpQ;          // the speaker's low end
    double bumpHz, bumpDb;     // the box's resonance
    double peakHz, peakDb;     // the cone's upper-mid peak
    double dipHz, dipDb;       // its break-up dip
    double lpHz;               // where the speaker gives up
};
enum Mic : int { DynamicOn, DynamicOff, CondenserOn, CondenserOff, RibbonOn, RibbonOff, kMics };

constexpr int kCabSections = 8;
// The biquads of a cabinet heard through a microphone (`mic` < 0: no microphone colour).
inline std::array<C, kCabSections> cabinet(const CabSpec& s, int mic, double fs) noexcept {
    const double top = fs * 0.45;
    std::array<C, kCabSections> c{};
    c[0] = C::make(T::HighPass, fs, s.hpHz, s.hpQ);
    c[1] = C::make(T::Bell, fs, s.bumpHz, 1.0, s.bumpDb);
    c[2] = C::make(T::Bell, fs, std::min(s.peakHz, top), 1.3, s.peakDb);
    c[3] = C::make(T::Bell, fs, std::min(s.dipHz, top), 1.6, s.dipDb);
    c[4] = C::make(T::LowPass, fs, std::min(s.lpHz, top), 0.6);
    c[5] = C::make(T::LowPass, fs, std::min(s.lpHz * 1.15, top), 1.0);
    if (mic >= 0) {
        const bool off = mic % 2 == 1;
        switch (mic / 2) {
        case 0:   // dynamic: a presence lift, tight lows
            c[6] = C::make(T::Bell, fs, std::min(4500.0, top), 1.2, off ? 1.0 : 4.0);
            c[7] = C::make(T::LowShelf, fs, 150.0, 0.7071, off ? 0.5 : -1.0);
            break;
        case 1:   // condenser: open top, full
            c[6] = C::make(T::HighShelf, fs, std::min(6000.0, top), 0.7071, off ? -1.0 : 2.5);
            c[7] = C::make(T::LowShelf, fs, 200.0, 0.7071, 1.5);
            break;
        default:   // ribbon: soft top, round lows
            c[6] = C::make(T::HighShelf, fs, std::min(3500.0, top), 0.7071, off ? -7.0 : -4.0);
            c[7] = C::make(T::LowShelf, fs, 180.0, 0.7071, off ? 3.0 : 2.0);
            break;
        }
        if (off) c[6] = C::make(T::HighShelf, fs, std::min(2500.0, top), 0.7071, mic / 2 == 2 ? -8.0 : -5.0);   // off axis: darker
    }
    return c;
}

// One path through a cabinet, both channels.
struct CabPath {
    std::array<std::array<hc::fx::Biquad, kCabSections>, 2> f{};
    void clear() noexcept { for (auto& ch : f) for (auto& b : ch) b.clear(); }
    void set(const std::array<C, kCabSections>& c) noexcept {
        for (auto& ch : f)
            for (int i = 0; i < kCabSections; ++i) ch[static_cast<std::size_t>(i)].c = c[static_cast<std::size_t>(i)];
    }
    float tick(int side, float x) noexcept {
        for (auto& b : f[static_cast<std::size_t>(side)]) x = b.tick(x);
        return x;
    }
};

// A few early reflections, no tail.
class EarlyRoom {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (auto& d : d_) d.prepare(static_cast<std::uint32_t>(0.025 * fs) + 8);
    }
    void clear() noexcept { for (auto& d : d_) d.clear(); }
    float tick(int side, float x) noexcept {
        auto& d = d_[static_cast<std::size_t>(side)];
        d.write(x);
        float r = 0.0f;
        for (int k = 0; k < kTaps; ++k) r += kGain[k] * d.read(kMs[k] * 0.001f * static_cast<float>(fs_) * (side == 0 ? 1.0f : 1.13f));
        return r;
    }

private:
    static constexpr int kTaps = 6;
    static constexpr float kMs[kTaps] = {2.9f, 4.7f, 7.3f, 10.1f, 13.9f, 19.7f};
    static constexpr float kGain[kTaps] = {0.42f, -0.33f, 0.27f, -0.21f, 0.16f, 0.11f};
    double fs_ = 48000.0;
    std::array<hc::fx::DelayLine, 2> d_;
};

// The output stage of the Voltage-style amps: never past -0.1 dBFS (instant catch, 60 ms let-go), counting
// the blocks where it had to catch a peak.
class OutLimit {
public:
    void prepare(double fs) { rel_ = coefMs(fs, 60.0); clear(); }
    void clear() noexcept { g_ = 1.0f; }
    // returns true when it caught something in this block
    bool run(float* L, float* R, std::uint32_t n) noexcept {
        constexpr float kCeil = 0.98855309f;   // -0.1 dBFS
        bool caught = false;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float pk = std::max(std::fabs(L[i]), std::fabs(R[i]));
            const float want = pk > kCeil ? kCeil / pk : 1.0f;
            if (want < 1.0f) caught = true;
            g_ = want < g_ ? want : want + rel_ * (g_ - want);
            L[i] *= g_;
            R[i] *= g_;
        }
        return caught;
    }

private:
    float g_ = 1.0f, rel_ = 0.0f;
};

// 0..10 knobs: 5 = flat; each step = `perStep` dB
inline float knobDb(float v, float perStep = 2.4f) noexcept { return (v - 5.0f) * perStep; }
// 0..10 volume knobs: 5 = unity, 0 = silent, 10 = +6 dB
inline float knobVol(float v) noexcept { return std::max(v, 0.0f) / 5.0f; }

} // namespace hem::rack::amps
