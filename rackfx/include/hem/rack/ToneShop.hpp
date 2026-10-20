#pragma once
// =============================================================================
//  The HEM tone shop: EQ, channel strips, tone and stereo (our own designs, our own names).
//
//  STRAIGHT SHOOTER  linear-phase EQ, six bands, no phase shift at all. Each band: ON, TYPE, FREQ,
//                    GAIN (±30 dB), Q. Band 1: LO CUT / LO SHELF / BELL / HI SHELF / HI CUT, 22 Hz–1 kHz,
//                    Q 0.6–2 (above 0.71 the cuts and shelves get a bump). Bands 2–6: BELL, the four
//                    "precision" shapes LO CUT / LO SHELF / HI SHELF / HI CUT (Q sets how steep, never
//                    a bump: cuts about 4.5 to 42 dB per octave) and the four "… RES" shapes (12 dB per
//                    octave, Q sets the bump), 258 Hz–21.9 kHz, Q 0.26–6.5. GAIN (the output, -30..+12 dB),
//                    TRIM (a push: the output comes down, or up by at most 12 dB, so the loudest peak
//                    since the last push lands on 0 dBFS), FILTER (STANDARD / EXACT / SMOOTH: the
//                    window the filter is cut with: the middle way, the tightest curve, the least
//                    ripple), DITHER (24-bit), SCALE (the screen shows ±12 or ±30 dB). The curve is
//                    turned into a symmetrical 8191-tap filter run by FFT convolution; latency 4607
//                    samples (96 ms at 48 kHz), always.
//  BOTTOM LINE       linear-phase low-end EQ, three bands, built the same way but twice as long (16383
//                    taps, latency 8703 samples, 181 ms) so the curve holds down low. Each band: ON,
//                    TYPE (the nine shapes above), FREQ 11–600 Hz (BELL from 32 Hz), GAIN ±30 (LO SHELF
//                    RES at most +10), Q 0.26–6.5. Same GAIN, TRIM, FILTER, DITHER, SCALE. Under about
//                    20 Hz a filter even this long can't follow the steepest shapes exactly.
//  TEN TOES         ten-band EQ with a left and a right set. Each band: ON, TYPE (BELL, LO SHELF, HI
//                    SHELF, HI CUT, LO CUT, TIGHT BELL: a bell that gets narrower the more it boosts or
//                    cuts), FREQ 16 Hz–21.4 kHz, GAIN ±18, Q 0.5–100 (shelves and cuts use at most 4).
//                    The LEFT / RIGHT tabs pick the set the knobs show; LINK makes the right side run
//                    the left set. INPUT L / R (0 down to off), PHASE L / R, OUTPUT L / R (-24..+12),
//                    TRIM (a push, ±12 dB: the loudest peak since the last push onto 0 dBFS). Zero latency.
//  FOUR CORNERS      four-filter EQ with a left and a right set. Filters 1 and 4: BELL, LO SHELF, HI
//                    SHELF, LO SHELF RES, HI SHELF RES, LO CUT, HI CUT; filters 2 and 3: BELL, LO SHELF,
//                    HI SHELF. ON, GAIN ±18, FREQ 16 Hz–21.4 kHz, Q 0.05–6.5 (the plain shelves stop at
//                    0.71, no bump; cuts 12 dB per octave). LINK, LEFT / RIGHT tabs, GAIN L / R ±12. Zero latency.
//  SMOOTH TALKER     six-band EQ with a left and a right set. Band 1: LO CUT / LO SHELF RES / BELL;
//                    2 and 3: LO SHELF / BELL; 4 and 5: HI SHELF / BELL; 6: HI CUT / HI SHELF RES / BELL.
//                    ON, GAIN ±18, FREQ 16 Hz–21.4 kHz, Q (bells 0.26–6.5; shelves and cuts 0.71–1.41).
//                    LINK, LEFT / RIGHT tabs, SPECTRUM (the music behind the curve on / off), OUTPUT
//                    (off..+12), TRIM (a push). Zero latency.
//  THE BOSS          British-console channel: SOURCE (MIC / LINE), PREAMP (MIC: 20–70 dB in 10 dB steps;
//                    more = the input stage driven harder, the level made up, so only the colour
//                    grows), INPUT ±18, the EQ: HIGH (a 10 kHz shelf, -16..+12 in 4 dB steps), MID
//                    (0–15 dB at 700 Hz–6 kHz, BOOST or CUT), LOW (a shelf at 60 / 120 / 250 / 400 Hz,
//                    0–15 dB, or OFF) and LOW CUT (a 50 Hz shelf down 3–15 dB, or OFF), EQ (in / out),
//                    ANALOG (mains hum OFF / 50 / 60 Hz), NOISE (FULL / LOW), PHASE, OUTPUT ±18, TRIM (a
//                    push: the loudest peak since the last push onto 0 dBFS, ±12 dB), METER (IN / OUT
//                    VU), VU CAL (0 VU = 8–24 dB under full scale). Latency 23.
//  INSIDE MAN        channel strip: GATE / EXPANDER (ON, MODE, THRESHOLD -80..+18, FLOOR off..0, RELEASE
//                    10 ms–5 s, and a key filter: ON, SOURCE MAIN / PRE EQ, TYPE LO CUT / HI CUT / BAND /
//                    NOTCH, FREQ, WIDTH 0.1–8 octaves), COMPRESSOR (ON, TYPE CLASSIC / VOCAL: the vocal
//                    one picks its own ratio, more the further over, THRESHOLD -60..+18, RATIO 0.5–50:
//                    under 1 the loud parts get louder, ATTACK, RELEASE, ARC: the release follows the
//                    music, and its own key filter), a four-band EQ (band 1: LO CUT / LO SHELF / LO SHELF
//                    RES / BELL; 2 and 3: BELL; 4: HI CUT / HI SHELF / HI SHELF RES / BELL; ON, GAIN ±18,
//                    FREQ, Q), ORDER (EQ first or the dynamics first), PHASE, OUTPUT ±30, ROTATE ±45°
//                    (turns the stereo picture; a centred sound moves to one side), and a clip guard that
//                    rounds anything over 0 dBFS (always on). The sidechain from outside is not possible in
//                    the rack. Zero latency.
//  KINGPIN           drum channel: TYPE (KICK, SNARE, HATS, TOMS, OVERHEAD, ROOM) picks how every
//                    control acts: the compressor's speed and the BASS / TREBLE frequencies. SENS ±50 (how
//                    hard the drum hits the compressor), COMPRESS 0–100, GATE ±50 (KICK only; all the way
//                    down = off), FX 0–100 (KICK: a short low boom, SNARE: a bright plate, TOMS: a room;
//                    not on the others), BASS and TREBLE 0–5 (1.5 dB a step; no BASS on OVERHEAD), OUTPUT
//                    ±50 (a fifth of a dB a step: ±10 dB), METER (IN / OUT VU), ANALOG (a whisper of hiss
//                    under the music). Zero latency.
//  GOLD CHAIN        one-knob tone: TONE adds warmth, density and sheen together (soft saturation,
//                    a gentle smile curve and slow glue compression that all grow with it); INPUT ±18
//                    drives it, LINK turns the output down as much as INPUT turns up, OUTPUT ±18, ANALOG
//                    (a whisper of hiss under the music). Latency 23.
//  IVORIES           piano tone: INPUT ±18, PIANO -50..+50 (up: brighter, leaner, more present; down: darker
//                    and fuller; either way it leans on a gentle compressor more), DELAY (on, and its LEVEL
//                    -24..0 dB: a soft stereo slap, left 120 ms, right 156 ms), DOUBLER (on, and its LEVEL:
//                    two slightly late, drifting copies, one each side), OUTPUT ±18, ANALOG (a whisper of
//                    hiss under the music). Zero latency.
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

// The band EQs: band b's control k is parameter b * kStride + k (the right set, where there is one, kSide on).
namespace lineq {
inline constexpr int kStride = 5;
enum BandP : int { On, Type, Freq, Gain, Q };
constexpr int band(int b, int k) noexcept { return b * kStride + k; }
// the nine shapes of the full bands (STRAIGHT SHOOTER 2–6, BOTTOM LINE)
enum Kind : int { Bell, VLowCut, VLowShelf, VHighShelf, VHighCut, RLowCut, RLowShelf, RHighShelf, RHighCut, kKinds };
// STRAIGHT SHOOTER band 1
enum FirstKind : int { FLowCut, FLowShelf, FBell, FHighShelf, FHighCut, kFirstKinds };
} // namespace lineq

namespace straight {
inline constexpr int kBands = 6;
enum P : int { Output = kBands * lineq::kStride, Trim, Filter, Dither, Scale, kCount };
inline constexpr std::uint32_t kLatency = 4607;
// The curve the audio runs (magnitude only: the filter is linear phase), from plain values.
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace straight

namespace bottomline {
inline constexpr int kBands = 3;
enum P : int { Output = kBands * lineq::kStride, Trim, Filter, Dither, Scale, kCount };
inline constexpr std::uint32_t kLatency = 8703;
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace bottomline

// The minimum-phase EQs: a left set of bands, then a right set (`kSide` further on), then the globals.
namespace tentoes {
inline constexpr int kBands = 10, kSide = kBands * lineq::kStride;
enum Kind : int { Bell, LowShelf, HighShelf, HighCut, LowCut, TightBell, kKinds };
enum P : int { Link = 2 * kSide, InputL, InputR, PhaseL, PhaseR, OutputL, OutputR, Trim, kCount };
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;        // the left side
[[nodiscard]] double curveRightDb(const float* plain, double fs, double hz) noexcept;   // the right side
extern const CurveInfo kCurve;
} // namespace tentoes

namespace fourcorners {
inline constexpr int kBands = 4, kSide = kBands * lineq::kStride;
enum Kind : int { Bell, LowShelf, HighShelf, LowShelfRes, HighShelfRes, LowCut, HighCut, kKinds };
enum P : int { Link = 2 * kSide, GainL, GainR, kCount };
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
[[nodiscard]] double curveRightDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace fourcorners

namespace smoothtalker {
inline constexpr int kBands = 6, kSide = kBands * lineq::kStride;
// each band's TYPE list: 0 is the band's own shape (band 1 LO CUT, 2–3 LO SHELF, 4–5 HI SHELF, 6 HI CUT),
// then BELL; bands 1 and 6 add a resonant shelf
enum P : int { Link = 2 * kSide, Spectrum, Output, Trim, kCount };
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
[[nodiscard]] double curveRightDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace smoothtalker

namespace boss {
enum P : int { Source, Preamp, Input, High, MidGain, MidFreq, MidMode, LowFreq, LowGain, LowCut, EqIn, Analog, Noise, Phase,
               Output, Trim, Meter, VuCal, kCount };
inline constexpr float kHighDb[] = {-16, -12, -8, -4, 0, 4, 8, 12};
inline constexpr float kMidHz[] = {700, 1000, 1400, 2000, 2800, 3500, 4500, 6000};
inline constexpr float kLowHz[] = {0, 60, 120, 250, 400};
inline constexpr float kCutDb[] = {0, -3, -6, -9, -12, -15};
inline constexpr std::uint32_t kLatency = 23;
} // namespace boss

namespace insideman {
inline constexpr int kBands = 4;
enum Filter : int { FLowCut, FHighCut, FBand, FNotch };
enum P : int { GateOn, GateMode, GateThreshold, Floor, GateRelease, GKeyOn, GKeySource, GKeyType, GKeyFreq, GKeyWidth,
               CompOn, CompType, CompThreshold, Ratio, Attack, Release, Arc, CKeyOn, CKeySource, CKeyType, CKeyFreq, CKeyWidth,
               B1On, B1Type, B1Freq, B1Gain, B1Q, B2On, B2Freq, B2Gain, B2Q, B3On, B3Freq, B3Gain, B3Q, B4On, B4Type, B4Freq, B4Gain, B4Q,
               Order, Phase, Output, Rotate, kCount };
// band b's controls (bands 2 and 3 are bells only: no TYPE, -1)
inline constexpr int kOn[kBands] = {B1On, B2On, B3On, B4On}, kType[kBands] = {B1Type, -1, -1, B4Type};
inline constexpr int kFreq[kBands] = {B1Freq, B2Freq, B3Freq, B4Freq}, kGain[kBands] = {B1Gain, B2Gain, B3Gain, B4Gain};
inline constexpr int kQ[kBands] = {B1Q, B2Q, B3Q, B4Q};
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace insideman

namespace kingpin {
enum Drum : int { Kick, Snare, Hats, Toms, Overhead, Room, kDrums };
enum P : int { Type, Sens, Compress, Gate, Fx, Bass, Treble, Output, Meter, Analog, kCount };
inline constexpr float kBassHz[kDrums] = {60, 180, 300, 100, 0, 100};
inline constexpr float kTrebleHz[kDrums] = {4000, 6000, 10000, 5000, 12000, 8000};
} // namespace kingpin

namespace goldchain {
enum P : int { Input, Tone, Link, Output, Analog, kCount };
} // namespace goldchain

namespace ivories {
enum P : int { Input, Piano, DelayOn, DelayLevel, DoublerOn, DoublerLevel, Output, Analog, kCount };
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
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBottomLine();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTenToes();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFourCorners();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSmoothTalker();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeInsideMan();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTheBoss();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeKingpin();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGoldChain();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeIvories();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHighRoller();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMiddleman();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDoubleCross();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeCutthroat();

} // namespace hem::rack
