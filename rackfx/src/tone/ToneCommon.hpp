#pragma once
// Helpers shared by the tone-shop plug-ins (private to rackfx).
#include "../arsenal/Common.hpp"

namespace hem::rack::tone {
using namespace arsenal;

// A 4x oversampled waveshaper beside a clean path of the same delay (OS::latency()). Only what the shape
// ADDS (shape(x) - x: its harmonics) goes through the oversampler; the sound itself takes the plain delay,
// so the oversampler's filters never dull the top octave. When it is switched off it fades out, so the
// latency never changes. Works in chunks of 64 samples on the stack: any host block size, no allocation.
struct Drive4 {
    OS os[2];
    Delay2 clean;
    float mix = 0.0f;
    void prepare() { clean.prepare(OS::latency()); }
    void clear(bool on = false) noexcept {
        os[0].clear();
        os[1].clear();
        clean.clear();
        mix = on ? 1.0f : 0.0f;
    }
    // shape(x) runs on every oversampled sample; on = whether the shaped path is heard; step = fade per sample
    template <class Shape>
    void run(float* L, float* R, std::uint32_t n, bool on, float step, Shape shape) noexcept {
        constexpr std::uint32_t kChunk = 64;
        float x4[kChunk * OS::kFactor], add[2][kChunk];
        float* io[2] = {L, R};
        for (std::uint32_t off = 0; off < n; off += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - off);
            for (int c = 0; c < 2; ++c) {
                os[c].up(io[c] + off, x4, m);
                for (std::uint32_t j = 0; j < m * OS::kFactor; ++j) x4[j] = shape(x4[j]) - x4[j];
                os[c].down(x4, add[c], m);
            }
            for (std::uint32_t i = 0; i < m; ++i) {
                mix = on ? std::min(1.0f, mix + step) : std::max(0.0f, mix - step);
                L[off + i] = clean.l.process(L[off + i], OS::latency()) + mix * add[0][i];
                R[off + i] = clean.r.process(R[off + i], OS::latency()) + mix * add[1][i];
            }
        }
    }
};

// Peak follower with separate attack / release (coefficients from coefMs).
inline float envStep(float env, float x, float att, float rel) noexcept { return x + (x > env ? att : rel) * (env - x); }

// A gate: opens at openDb, closes 4 dB under it after the hold, falls to floorDb.
struct GateStage {
    float env = 0.0f, gain = 0.0f;   // gain in dB (<= 0)
    bool open = false;
    std::uint32_t holdLeft = 0;
    void clear() noexcept { env = gain = 0.0f; open = false; holdLeft = 0; }
    float tick(float x, bool on, float openDb, float floorDb, std::uint32_t holdN, float envRel, float att, float rel) noexcept {
        env = x > env ? x : x + envRel * (env - x);
        const float lv = dbOf(env);
        if (!on) open = true;
        else if (lv >= openDb) { open = true; holdLeft = holdN; }
        else if (lv < openDb - 4.0f) {
            if (holdLeft > 0) --holdLeft;
            else open = false;
        }
        const float target = open ? 0.0f : floorDb;
        gain = target > gain ? target + att * (gain - target) : target + rel * (gain - target);
        return gain;
    }
};

} // namespace hem::rack::tone
