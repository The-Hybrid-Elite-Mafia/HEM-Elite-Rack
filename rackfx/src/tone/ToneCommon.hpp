#pragma once
// Helpers shared by the tone-shop plug-ins (private to rackfx).
#include "../arsenal/Common.hpp"

namespace hem::rack::tone {
using namespace arsenal;

// A 4x oversampled waveshaper beside a clean path of the same delay (OS::latency()). When it is
// switched off it fades to the clean path, so the latency never changes. Works in chunks of 64
// samples on the stack: any host block size, no allocation.
struct Drive4 {
    OS os[2];
    Delay2 clean;
    float mix = 0.0f;
    void prepare() { clean.prepare(OS::latency()); }
    void clear() noexcept {
        os[0].clear();
        os[1].clear();
        clean.clear();
        mix = 0.0f;
    }
    // shape(x) runs on every oversampled sample; on = whether the shaped path is heard; step = fade per sample
    template <class Shape>
    void run(float* L, float* R, std::uint32_t n, bool on, float step, Shape shape) noexcept {
        constexpr std::uint32_t kChunk = 64;
        float x4[kChunk * OS::kFactor], sat[2][kChunk];
        float* io[2] = {L, R};
        for (std::uint32_t off = 0; off < n; off += kChunk) {
            const std::uint32_t m = std::min(kChunk, n - off);
            for (int c = 0; c < 2; ++c) {
                os[c].up(io[c] + off, x4, m);
                for (std::uint32_t j = 0; j < m * OS::kFactor; ++j) x4[j] = shape(x4[j]);
                os[c].down(x4, sat[c], m);
            }
            for (std::uint32_t i = 0; i < m; ++i) {
                mix = on ? std::min(1.0f, mix + step) : std::max(0.0f, mix - step);
                const float cl = clean.l.process(L[off + i], OS::latency()), cr = clean.r.process(R[off + i], OS::latency());
                L[off + i] = cl + mix * (sat[0][i] - cl);
                R[off + i] = cr + mix * (sat[1][i] - cr);
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
