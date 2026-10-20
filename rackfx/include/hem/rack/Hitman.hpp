#pragma once
// =============================================================================
//  HEM HITMAN — a dual FET peak compressor (our own design, in the classic two-channel FET style):
//  fast, comparatively transparent, and it eats transients.
//
//    MODE L/R or M/S: channel 1 = left or mid, channel 2 = right or side.
//    LINK: channel 2 follows channel 1's INPUT, OUTPUT, ATTACK, RELEASE.
//    Per channel: INPUT (drives it: more input = more compression), OUTPUT, ATTACK 1..7 and
//      RELEASE 1..7 (7 = fastest: 20 µs attack, 50 ms release).
//    RATIO 4, 8, 12, 20 or ALL (every button in: harder than 20:1, overdriven and gritty).
//    LOOKAHEAD 0..2 ms: the detector hears the peak before it arrives. The latency stays the same
//      whatever it is set to (2 ms + the oversampler), so changing it never shifts the timing.
//    SC LINK 0..100 %: how much the two channels' detectors follow each other.
//    SIDECHAIN: SOURCE internal or external (the VST3 has a sidechain input; the rack app has no
//      second input, so EXT falls back to internal there), SC EQ (high-pass, bell, low-pass on what
//      the detector hears), LISTEN (hear the detector).
//    SATURATION after the compressor and OUTPUT (so OUTPUT drives it), 4x oversampled: DRY, CLIP
//      (soft ceiling at 0 dBFS), WARM, TRIODE (even harmonics), TAPE (odd harmonics, softened top).
//    MIX (dry delayed to match).
//  Meters: 0 = channel 1 gain reduction, 1 = channel 2 gain reduction, 2 = input peak, 3 = output peak.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace hitman {
enum P : int {
    Mode, Link, Input1, Output1, Attack1, Release1, Input2, Output2, Attack2, Release2,
    Ratio, Lookahead, ScLink, Saturation, Mix,
    ScSource, ScEq, ScHpf, ScBellFreq, ScBellGain, ScBellQ, ScLpf, ScListen, kCount
};
enum class RatioSel : int { R4, R8, R12, R20, All };
enum class Sat : int { Dry, Clip, Warm, Triode, Tape };
[[nodiscard]] float attackMs(float knob) noexcept;    // 1 -> 0.8 ms .. 7 -> 0.02 ms
[[nodiscard]] float releaseMs(float knob) noexcept;   // 1 -> 1100 ms .. 7 -> 50 ms
[[nodiscard]] float gainComputerDb(float levelDb, RatioSel r) noexcept;
// The sidechain EQ's response (what the faceplate draws), from plain parameter values.
[[nodiscard]] double scEqDb(const float* plain, double sampleRate, double hz) noexcept;
} // namespace hitman

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHitman();

struct CurveInfo;
extern const CurveInfo kHitmanScCurve;

} // namespace hem::rack
