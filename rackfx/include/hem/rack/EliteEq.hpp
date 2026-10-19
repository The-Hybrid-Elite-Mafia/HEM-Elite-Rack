#pragma once
// =============================================================================
//  HEM ELITE EQ — the mastering / mixing equalizer of HEM-Elite-Rack (our own design).
//
//  16 bands, each: ON, TYPE (bell, low / high shelf, low / high cut, notch, tilt), FREQ, GAIN,
//  Q, SLOPE (cuts: 6..48 dB/oct), STEREO (stereo, mid, side, left, right) and DYNAMIC
//  (THRESHOLD, RANGE, ATTACK, RELEASE): a dynamic band moves its gain by up to RANGE while the
//  level in that band is above THRESHOLD (3:1 above it), so a harsh spot is only cut when it
//  actually pokes out. Then CHARACTER (clean, console, tube; 4x oversampled), DRIVE, AUTO GAIN
//  (keeps the loudness where it was, so louder never fools you) and OUTPUT.
//
//  Filters: RBJ biquads (bell, shelves, notch), Butterworth cascades for the cuts (Q shapes the
//  corner), first-order tilt. Zero latency apart from the oversampler's 23 samples, which are
//  always present (the latency never changes with the settings).
//
//  Parameters are a flat list (hc::fx): band b's control c is param bandParam(b, c).
// =============================================================================
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Fx.hpp"

#include <array>
#include <memory>

namespace hem::rack {

namespace eq {
inline constexpr int kBands = 16;
enum class Type : int { Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, Tilt, kCount };
enum class Stereo : int { Stereo, Mid, Side, Left, Right, kCount };
enum Control : int { On, TypeC, Freq, Gain, Q, Slope, StereoC, Dyn, Threshold, Range, Attack, Release, kPerBand };
enum Global : int { Output = kBands * kPerBand, AutoGain, Character, Drive, kParamCount };
enum class Char : int { Clean, Console, Tube };
inline constexpr std::array<int, 6> kSlopes{6, 12, 18, 24, 36, 48};
[[nodiscard]] constexpr int bandParam(int band, int control) noexcept { return band * kPerBand + control; }

// One band's settings in plain units.
struct Band {
    bool   on = false;
    Type   type = Type::Bell;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
    int    slopeDb = 24;
    Stereo stereo = Stereo::Stereo;
};

// Up to 4 second-order sections (normalised: a0 = 1). First-order sections have b2 = a2 = 0.
struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
struct Sections {
    std::array<Section, 4> s{};
    int count = 0;
};
// The filter one band applies (exactly what the audio runs; also used to draw the curve).
[[nodiscard]] Sections design(const Band& band, double sampleRate) noexcept;
[[nodiscard]] double magnitudeDb(const Sections& s, double sampleRate, double hz) noexcept;
// Reads band b from an effect's current parameters.
[[nodiscard]] Band bandOf(const hc::fx::Effect& e, int b) noexcept;
[[nodiscard]] const char* typeName(Type t) noexcept;
[[nodiscard]] bool typeHasGain(Type t) noexcept;      // bell, shelves, tilt
[[nodiscard]] bool typeHasSlope(Type t) noexcept;     // cuts
[[nodiscard]] bool typeCanBeDynamic(Type t) noexcept; // bell, shelves, tilt, notch-free
} // namespace eq

class EliteEq;
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeEliteEq();

// What the faceplate reads besides the parameters.
class EliteEqView {
public:
    virtual ~EliteEqView() = default;
    // Gain the dynamic part of band b adds right now (dB, 0 when not dynamic).
    [[nodiscard]] virtual float dynamicGainDb(int band) const noexcept = 0;
    // Gain AUTO GAIN applies right now (dB).
    [[nodiscard]] virtual float autoGainDb() const noexcept = 0;
};

} // namespace hem::rack
