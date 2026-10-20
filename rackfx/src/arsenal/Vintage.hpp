#pragma once
// Parts the hardware-style compressors share (private to rackfx): the mains hum of ANALOG, the VU
// meters behind a METER IN / GR / OUT switch, automatic make-up gain, and a quiet output trim.
#include "Common.hpp"

namespace hem::rack::arsenal {

inline const char* const kMainsN[] = {"OFF", "50 HZ", "60 HZ"};
inline const char* const kMeterN[] = {"IN", "GR", "OUT"};

// ANALOG: the faint mains hum of the hardware (50 or 60 Hz with a little of its 2nd and 3rd
// harmonics) and a whisper of hiss, riding 60 dB under the music. It follows the music's level
// (10 ms up, 400 ms down), so digital silence stays silent.
class Hum {
public:
    void prepare(double fs) {
        fs_ = fs;
        up_ = 1.0f - coefMs(fs, 10.0);
        down_ = 1.0f - coefMs(fs, 400.0);
        clear();
    }
    void clear() noexcept { env_ = 0.0f; ph_ = 0.0; }
    // mains: 0 off, 1 = 50 Hz, 2 = 60 Hz, 3 = hiss only; amount: 1 = the usual level. Adds to l, r.
    void tick(int mains, float amount, float& l, float& r) noexcept {
        const float lev = std::max(std::fabs(l), std::fabs(r));
        env_ += (lev - env_) * (lev > env_ ? up_ : down_);
        if (mains <= 0 || env_ < 1e-9f) return;
        float hum = 0.0f;
        if (mains < 3) {
            ph_ += (mains == 1 ? 50.0 : 60.0) / fs_;
            if (ph_ >= 1.0) ph_ -= 1.0;
            const double w = 2.0 * std::numbers::pi * ph_;
            hum = static_cast<float>(0.75 * std::sin(w) + 0.3 * std::sin(2 * w) + 0.15 * std::sin(3 * w));
        }
        const float k = env_ * amount * 0.001f;
        l += k * (hum + 0.5f * noise());
        r += k * (hum + 0.5f * noise());
    }

private:
    float noise() noexcept {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;
    }
    double fs_ = 48000.0, ph_ = 0.0;
    float env_ = 0.0f, up_ = 0.0f, down_ = 0.0f;
    std::uint32_t rng_ = 0x2545F491u;
};

// A VU meter: average power over ~300 ms; 0 VU = `calDb` under full scale (-18 dBFS RMS by default).
struct Vu {
    double ms = 0, k = 0;
    void prepare(double fs) { k = 1.0 - std::exp(-1.0 / (fs * 0.065)); ms = 0; }
    void clear() noexcept { ms = 0; }
    void tick(float l, float r) noexcept { ms += (0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r) - ms) * k; }
    [[nodiscard]] float vu(float calDb = 18.0f) const noexcept { return std::max(powDb(ms), -100.0f) + calDb; }
};

// AUTO MAKEUP: gives back what the compressor takes, on average (2 s), so the level stays put.
struct AutoGain {
    float avg = 0, k = 0;
    void prepare(double fs) { k = 1.0f - coefMs(fs, 2000.0); avg = 0; }
    void clear() noexcept { avg = 0; }
    // grDb <= 0; returns the make-up (dB, >= 0)
    float tick(float grDb) noexcept {
        avg += (grDb - avg) * k;
        return std::min(-avg, 30.0f);
    }
};

} // namespace hem::rack::arsenal
