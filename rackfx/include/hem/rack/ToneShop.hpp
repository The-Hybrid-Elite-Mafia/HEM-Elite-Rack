#pragma once
// =============================================================================
//  The HEM tone shop: EQ, channel strips, tone and stereo (our own designs, our own names).
//
//  STRAIGHT SHOOTER  linear-phase EQ: no phase shift at all, only level changes. LOW CUT and HIGH
//                    CUT (OFF / 12 / 24 / 48 dB per octave, and their frequency), a LOW shelf, three
//                    MID bells (FREQ, GAIN, Q), a HIGH shelf, OUTPUT. The curve is turned into a
//                    symmetrical 8191-tap filter run by FFT convolution; latency 4607 samples (96 ms
//                    at 48 kHz), always. Below about 40 Hz the very steepest cuts come out a little
//                    softer than drawn (a filter of that length can't be steeper down there).
//  THE BOSS          channel strip: GATE, compressor (THRESHOLD, RATIO, ATTACK, RELEASE), four-band
//                    EQ (LOW shelf at 60 / 100 / 200 Hz, LOW MID and HIGH MID bells with their
//                    frequency, HIGH shelf at 8 / 12 / 16 kHz), LOW CUT, ORDER (EQ before or after
//                    the dynamics), LIMIT (1 ms look-ahead ceiling at -0.3 dBFS), OUTPUT. Latency 48.
//  KINGPIN           drum channel: GATE (and its RELEASE), THUMP (low boost, THUMP FREQ), SNAP (high
//                    presence, SNAP FREQ), PUNCH (hits come forward), CRUSH (a squashed copy mixed in
//                    under the drums), DRIVE (tape-style saturation, 4x oversampled), OUTPUT. Latency 23.
//  GOLD CHAIN        one-knob tone: TONE adds warmth, density and sheen together (soft saturation,
//                    a gentle smile curve and slow glue compression that all grow with it); INPUT
//                    drives it, LINK turns the output down as much as INPUT turns up, MIX, OUTPUT.
//                    Latency 23.
//  IVORIES           piano tone: TONE (darker / brighter), BODY (low-mid bloom), COMPRESS, DELAY (a
//                    soft stereo slap, TIME), DOUBLER (two slightly delayed and detuned copies, left
//                    and right), WIDTH, OUTPUT. Zero latency.
//  HIGH ROLLER       five-band enhancer: SUB / LOW / MID / HIGH / AIR, each with LIFT (a dense,
//                    squeezed copy of that band added in: detail comes up) and WIDTH (0 = mono,
//                    100 = as it was, 200 = twice the sides); MIX, OUTPUT. Splits at 90, 400, 2000,
//                    7000 Hz. Zero latency.
//  THE MIDDLEMAN     centre / sides: CENTER and SIDES level, LOW BALANCE and HIGH BALANCE (under
//                    200 Hz / over 4 kHz, plus = more in the middle, minus = more on the sides), PUNCH
//                    (hits go to the middle, or minus: to the sides), OUTPUT. Mono stays mono when
//                    the sides are not touched. Meters: 0 centre level, 1 sides level (dB). Zero latency.
//  DOUBLE CROSS      mono to stereo: the sound is split into bands that lean left and right in turn,
//                    so the left and right always add back up to the original (mono-safe). SPREAD,
//                    STYLE (TIGHT 4 / MEDIUM 6 / WIDE 8 bands), LOW STAYS MONO (OFF / 100 / 200 / 300 Hz),
//                    CENTER, OUTPUT. Zero latency.
//  CUTTHROAT         filter: SWEEP is the one-knob move (left: the highs close down, right: the lows
//                    thin out), LOW CUT and HIGH CUT set the resting edges, RESONANCE, SLOPE 12 / 24 dB,
//                    DRIVE, MIX, OUTPUT. Zero latency.
// =============================================================================
#include "hem/rack/Plugins.hpp"
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace straight {
enum P : int { LowCutSlope, LowCut, LowFreq, LowGain, Mid1Freq, Mid1Gain, Mid1Q, Mid2Freq, Mid2Gain, Mid2Q,
               Mid3Freq, Mid3Gain, Mid3Q, HighFreq, HighGain, HighCut, HighCutSlope, Output, kCount };
inline constexpr std::uint32_t kBlock = 512, kParts = 16, kTaps = kBlock * kParts - 1, kLatency = kBlock + (kTaps - 1) / 2;
// The curve the audio runs (magnitude only: the filter is linear phase), from plain values.
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace straight

namespace boss {
enum P : int { Gate, Threshold, Ratio, Attack, Release, LowGain, LowFreq, LoMidGain, LoMidFreq, HiMidGain, HiMidFreq,
               HighGain, HighFreq, LowCut, Order, Limit, Output, kCount };
inline constexpr float kLowHz[] = {60.0f, 100.0f, 200.0f};
inline constexpr float kHighHz[] = {8000.0f, 12000.0f, 16000.0f};
inline constexpr float kCutHz[] = {0.0f, 40.0f, 80.0f, 120.0f};
inline constexpr std::uint32_t kLatency = 48;
} // namespace boss

namespace kingpin {
enum P : int { Gate, GateRelease, Thump, ThumpFreq, Snap, SnapFreq, Punch, Crush, Drive, Output, kCount };
} // namespace kingpin

namespace goldchain {
enum P : int { Input, Tone, Link, Mix, Output, kCount };
} // namespace goldchain

namespace ivories {
enum P : int { Tone, Body, Compress, Delay, Time, Doubler, Width, Output, kCount };
} // namespace ivories

namespace highroller {
inline constexpr int kBands = 5;
enum P : int { Lift1, Lift2, Lift3, Lift4, Lift5, Width1, Width2, Width3, Width4, Width5, Mix, Output, kCount };
inline constexpr float kSplitHz[kBands - 1] = {90.0f, 400.0f, 2000.0f, 7000.0f};
} // namespace highroller

namespace middleman {
enum P : int { Center, Sides, LowBalance, HighBalance, Punch, Output, kCount };
inline constexpr float kLowHz = 200.0f, kHighHz = 4000.0f;
} // namespace middleman

namespace doublecross {
enum P : int { Spread, Style, LowMono, Center, Output, kCount };
inline constexpr int kBandsFor[] = {4, 6, 8};
inline constexpr float kMonoHz[] = {0.0f, 100.0f, 200.0f, 300.0f};
} // namespace doublecross

namespace cutthroat {
enum P : int { Sweep, LowCut, HighCut, Resonance, Slope, Drive, Mix, Output, kCount };
// where the filters sit for these plain values (SWEEP folded in)
void edges(const float* plain, double& hpHz, double& lpHz, double& q) noexcept;
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace cutthroat

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStraightShooter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTheBoss();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeKingpin();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGoldChain();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeIvories();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHighRoller();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMiddleman();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDoubleCross();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeCutthroat();

} // namespace hem::rack
