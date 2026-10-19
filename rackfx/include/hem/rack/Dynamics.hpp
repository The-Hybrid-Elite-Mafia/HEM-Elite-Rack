#pragma once
// =============================================================================
//  HEM-Elite-Rack's classic-style compressors (our own designs). Each publishes its gain
//  reduction (dB, <= 0) through hc::fx::Effect::reductionDb() for the faceplate's needle.
//
//  HEM VARI-MU — a variable-mu tube compressor: the ratio grows the harder it is pushed (gentle
//    at first, firmer on peaks), six TIME settings (5 and 6 release by themselves, longer after
//    long passages), STEREO or MID / SIDE, the tube's warmth (TUBE), MAKEUP, MIX.
//    Latency 23 samples (the tube stage is 4x oversampled), always.
//
//  HEM OPTO — an optical leveler: two knobs do it all. PEAK REDUCTION (how much), GAIN (makeup).
//    The light cell's attack is about 10 ms; it lets go in two stages, half quickly (~60 ms) and
//    the rest slowly (0.5 s, up to 5 s after it has been working hard for a while). COMPRESS or
//    LIMIT; EMPHASIS makes the detector listen harder to the highs (tames esses and cymbals
//    first). MIX. Zero latency.
//
//  HEM FET — a fast FET limiting amplifier: INPUT drives it (more input = more compression),
//    OUTPUT sets the level. ATTACK and RELEASE 1..7 (7 = fastest: 20 µs attack, 50 ms release),
//    RATIO 4, 8, 12, 20 or ALL (every button in: aggressive, overshooting, gritty). Its colour
//    grows with the compression. MIX. Latency 23 samples, always.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace varimu {
enum P : int { Input, Threshold, Time, Mode, Tube, Makeup, Mix, kCount };
// TIME: attack (ms) and release (s); settings 5 and 6 go from the short to the long release by themselves.
inline constexpr float kAttackMs[6] = {0.2f, 0.2f, 0.4f, 0.4f, 0.4f, 0.2f};
inline constexpr float kReleaseS[6] = {0.3f, 0.8f, 2.0f, 5.0f, 2.0f, 0.3f};
inline constexpr float kReleaseLongS[6] = {0.3f, 0.8f, 2.0f, 5.0f, 10.0f, 10.0f};
[[nodiscard]] float gainComputerDb(float levelDb, float thresholdDb) noexcept;   // ratio grows with level
} // namespace varimu

namespace opto {
enum P : int { PeakReduction, Gain, Mode, Emphasis, Mix, kCount };
[[nodiscard]] float gainComputerDb(float sidechainDb, bool limit) noexcept;
} // namespace opto

namespace fet {
enum P : int { Input, Output, Attack, Release, Ratio, Mix, kCount };
enum class RatioSel : int { R4, R8, R12, R20, All };
[[nodiscard]] float attackMs(float knob) noexcept;    // 1 -> 0.8 ms .. 7 -> 0.02 ms
[[nodiscard]] float releaseMs(float knob) noexcept;   // 1 -> 1100 ms .. 7 -> 50 ms
[[nodiscard]] float gainComputerDb(float levelDb, RatioSel r) noexcept;
} // namespace fet

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVariMu();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeOpto();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFet();

} // namespace hem::rack
