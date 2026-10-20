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
//                    under 1 the loud parts get louder, ATTACK, RELEASE, AUTO REL: the release follows the
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
//                    -24..0 dB: a soft stereo slap, left 120 ms, right 156 ms), THICKEN (on, and its LEVEL:
//                    two slightly late, drifting copies, one each side), OUTPUT ±18, ANALOG (a whisper of
//                    hiss under the music). Zero latency.
//  HIGH ROLLER       five-band parallel enhancer: each band's processed copy (squeezed hard and rounded,
//                    so its quiet detail comes up) is mixed with the untouched sound. Per band: GAIN 0–48 dB
//                    (of the processed copy only), WIDTH 0–3 (0 mono, 1 as it was, 3 three times the sides;
//                    on both parts), SOLO (hear only the soloed bands, both parts), BYPASS (that band's
//                    processed copy off). The four crossovers (20 Hz–18 kHz, each kept between its
//                    neighbours), INPUT ±18, OUTPUT ±18, DIRECT (the untouched sound, -44..0 dB or off),
//                    PUNCH 0–5 (how much of the hits gets past the squeeze: up = crisper, down = smoother).
//                    Zero latency.
//  THE MIDDLEMAN     centre and sides: the CENTER and SIDES faders (+6 down to off), MASTER (+6..-24). LOW,
//                    HIGH and PUNCH (0–100) say which fader the centre content of the lows (under 200 Hz),
//                    the highs (over 4 kHz) and the hits (while they rise) follows: 0 the CENTER fader, 100
//                    the SIDES fader. Meters: 0 how much of what comes out is centre (%), 1 / 2 the output
//                    peaks L / R. Zero latency.
//  THE SETUP         a filter played by three movers. TYPE: LO PASS, HI PASS, BAND PASS, NOTCH (FREQ 30 Hz–
//                    18 kHz, RES, SLOPE 12 / 24), COMB (DELAY 11–28 ms, FEEDBACK) or AMP (GAIN -80..0). SPREAD
//                    pulls left and right apart (±50 %), DRIVE pushes the filter's input, CRUSH lowers the
//                    sample rate after it, SMOOTH rounds off the movers' jumps. The movers: an LFO (SINE,
//                    TRIANGLE, PULSE, SAW, RANDOM; RATE free or a note of the tempo), a 16-step SEQUENCER
//                    (each step -100..+100 %, LAST step, SWING, RATE free or a note) and a FOLLOWER (the
//                    input's level). Each mover has a DEPTH (±100 %) on the three targets: the frequency
//                    (±5 octaves; COMB: its delay, AMP: its gain), the resonance (COMB: feedback) and the
//                    delay TIME. The DELAY: TIME (50 ms–1 s, or a note), RIGHT (100 / 150 / 200 % of the
//                    left), FEEDBACK, DRY / WET, ANALOG (time changes glide: the pitch bends) or DIGITAL,
//                    ROUTE: before the filter, or with the filter inside its feedback. MIX, INPUT, OUTPUT
//                    (-40..+10). MIDI keyboard tracking and the outside sidechain are not possible in the rack.
//                    Zero latency.
//  LOW RIDER         a new low octave: RANGE (20–120 Hz: the low-pass that picks what is used), NEW OCTAVE (the
//                    new signal an octave down, off..+30 dB), LO (the low-passed original, off..+30 dB; both
//                    saturate softly above 0 dB and never pass full scale), DIRECT (the untouched sound,
//                    off..0 dB), OUTPUT (-60..0), TRIM ±12, ALIGN (delays the untouched sound to line up with
//                    the new low end at its lowest notes). Latency 70 ms, always.
//  UNDERTOW          sub generator: RANGE MIN (20–120 Hz, 48 dB / oct) and RANGE MAX (40–240 Hz, 24 dB / oct)
//                    pick the band it follows; OCTAVE DOWN (an octave down) and TWO DOWN (two), each ON and a LEVEL
//                    (off..+12 dB); DRIVE (saturates the subs), DYNAMICS (0: the subs stay steady, 100: they
//                    follow every move of the bass), SUB IN MIDDLE (one sub from both sides, or one each), MIX
//                    (0 the original only, 50 both in full, 100 the subs only), OUTPUT (-18..0). Zero latency.
//  DEEP POCKETS      bass you hear on small speakers: under FREQUENCY (32–256 Hz) the bass is replaced, as much
//                    as you like, by its harmonics (which small speakers can play). INPUT (off..0), ORIGINAL
//                    BASS and HARMONICS (off..+12 dB), HIGH-PASS on the harmonics (6 dB @ 16 Hz, 12 or 24 dB
//                    at FREQUENCY), DECAY (-24..-9 dB: how fast each next harmonic falls; nearer 0 = richer),
//                    RATIO (1–4: quiet bass gets more harmonics), RESPONSE (how fast that follows), MONITOR
//                    (all / the harmonics / the original bass), the peak (held until RESET). Zero latency.
//  LOW PROFILE       the same idea, simpler: FREQUENCY, REAL BASS (in / out), AMOUNT (-24..+24 dB of
//                    harmonics), GAIN (off..0). Zero latency.
//  DOUBLE CROSS      mono to stereo that folds back to mono exactly: the middle is sent left and right by
//                    an amount that sweeps across the spectrum (L + R is always the original middle). KIND:
//                    SPREAD (2–22 sweeps), SPREAD 10 (2–10) or SPLIT (2–10, BASS SPREAD always 0, and SHAPE:
//                    0 triangle-like sweeps, 0.15 smooth, 1 square, past 1 beyond full). SPREAD 0–1.2 (1
//                    fills the space between the speakers), FREQ (32 Hz–16 kHz: under it BASS SPREAD takes over;
//                    BASS SPREAD 0 keeps the lows in the middle, 1 like the rest, more = wider), COMBS, FCENTER
//                    and FDENSITY (plus: the sweeps crowd around FCENTER; minus: they thin out there and crowd
//                    at the ends), FINE SHIFT ±1 (slides every frequency along the sweep, to balance the picture).
//                    The input first: INPUT 0..-24 dB, WIDTH 0 (mono)..1 (as it was), ROTATION ±45°, POLARITY
//                    L / R, SWAP. Meters: the highest peak left and right (or middle and sides, METER), held
//                    until RESET. Done on 2048-point frames; latency 2047 samples (43 ms), always.
//  CUTTHROAT         high-pass and low-pass, 18 dB per octave, a left and a right set: HIGH-PASS (on, and
//                    16 Hz–21.4 kHz), LOW-PASS (on, and its frequency); the LEFT / RIGHT tabs pick the set the
//                    knobs show; LINK makes the right side follow the left. Frequencies glide (no zipper).
//                    Zero latency.
//  TUNNEL VISION     one-knob filter: FILTER 0–10 (10 = open; down from there a low-pass closes from 20 kHz
//                    to 120 Hz), RESONANCE (OFF / LOW / MID / HIGH: a peak at the edge). Zero latency.
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
inline constexpr int kBands = 5, kStride = 4;
enum BandP : int { Gain, Width, Solo, Bypass };   // band b's control k: b * kStride + k
enum P : int { Cross1 = kBands * kStride, Cross2, Cross3, Cross4, Input, Output, Direct, Punch, kCount };
constexpr int band(int b, int k) noexcept { return b * kStride + k; }
} // namespace highroller

namespace middleman {
enum P : int { Low, High, Punch, Master, Center, Sides, kCount };
inline constexpr float kLowHz = 200.0f, kHighHz = 4000.0f;
} // namespace middleman

namespace tunnelvision {
enum P : int { Filter, Resonance, kCount };
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace tunnelvision

namespace thesetup {
enum Type : int { LowPass, HighPass, BandPass, Notch, Comb, Amp, kTypes };
enum Mover : int { Lfo, Seq, Follow, kMovers };
enum Target : int { ToFreq, ToRes, ToTime, kTargets };
inline constexpr int kSteps = 16;
enum P : int { Kind, Freq, CombDelay, Gain, Res, CombFeedback, Slope, Spread, Smooth, Drive, Crush,
               LfoRate, LfoNote, LfoSync, LfoWave, SeqRate, SeqNote, SeqSync, Swing, Last, Step1,
               DelayTime = Step1 + kSteps, DelayNote, DelaySync, DelayRight, DelayFeedback, DelayMix, DelayAnalog, DelayRoute,
               Depth0, Mix = Depth0 + kMovers * kTargets, Input, Output, kCount };
constexpr int depth(int mover, int target) noexcept { return Depth0 + target * kMovers + mover; }
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;   // the filter at rest (no movers)
extern const CurveInfo kCurve;
} // namespace thesetup

namespace lowrider {
enum P : int { Range, LowAir, Lo, Direct, Output, Trim, Align, kCount };
inline constexpr double kLatencyS = 0.07;
} // namespace lowrider
namespace undertow {
enum P : int { RangeMin, RangeMax, Sub1On, Sub1, Sub2On, Sub2, Drive, Dynamics, MonoSub, Mix, Output, kCount };
} // namespace undertow
namespace deeppockets {
enum Monitor : int { All, HarmonicsOnly, OriginalOnly };
enum P : int { Frequency, Input, Original, Harmonics, HighPass, Decay, Ratio, Response, Listen, Reset, kCount };
} // namespace deeppockets
namespace lowprofile {
enum P : int { Frequency, OriginalIn, Intensity, Gain, kCount };
} // namespace lowprofile

namespace doublecross {
enum Mode : int { SpreadMode, Spread10Mode, SplitMode };
enum P : int { Kind, Input, Width, Rotation, PolarityL, PolarityR, Swap, Spread, Freq, LfSpread, Sweeps, FCenter, FDensity, Tweak, Shape,
               MeterMode, Reset, kCount };
inline constexpr std::uint32_t kFrame = 2048, kHop = 512, kLatency = kFrame - 1;
// where across the spectrum the sound leans (-1 left .. +1 right; past ±1 = wider than the speakers) for these plain values
[[nodiscard]] double panAt(const float* plain, double hz) noexcept;
} // namespace doublecross

namespace cutthroat {
// the left set (HP on, HP freq, LP on, LP freq), then the right set, then LINK
inline constexpr int kSide = 4;
enum P : int { HpOn, HpFreq, LpOn, LpFreq, RHpOn = kSide, RHpFreq, RLpOn, RLpFreq, Link = 2 * kSide, kCount };
[[nodiscard]] double curveDb(const float* plain, double fs, double hz) noexcept;        // the left side
[[nodiscard]] double curveRightDb(const float* plain, double fs, double hz) noexcept;
extern const CurveInfo kCurve;
} // namespace cutthroat

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeStraightShooter();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBottomLine();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTenToes();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeFourCorners();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSmoothTalker();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeInsideMan();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLowRider();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeUndertow();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDeepPockets();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeLowProfile();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTunnelVision();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTheSetup();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeTheBoss();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeKingpin();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeGoldChain();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeIvories();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeHighRoller();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMiddleman();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeDoubleCross();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeCutthroat();

} // namespace hem::rack
