#pragma once
// =============================================================================
//  HEM THE CLEANER — adaptive hiss / noise reduction (our own design).
//
//  STFT (2048 points at 44.1 / 48 kHz, hop 1/4, square-root Hann in and out).
//  It learns the noise floor by itself (per frequency: median of 13 neighbouring bins, then the
//  lowest of that over the last ~4 s — a held note is never mistaken for noise) and keeps what
//  stands above it with a decision-directed Wiener gain (smooth, no "musical noise" twinkles):
//    NOISE REDUCTION 0..10  how hard (0 = untouched; 10 = up to 30 dB down)
//    FOCUS LO..HI           lean the work toward the lows (hum, rumble) or the highs (hiss)
//    AIR PRESERVE 0..10     protect the top end and the attacks of notes
//    FREQUENCY RANGE        only between LOW and HIGH (soft 1/3-octave edges)
//    ADAPT                  keep learning the noise (off = freeze what it learned)
//    LISTEN                 hear only what is being taken out
//  Latency: the window length (2048 samples at 48 kHz = 42.7 ms), constant.
// =============================================================================
#include "hem/rack/Spectrum.hpp"
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace restorer {
enum P : int { Reduction, Focus, Air, LowHz, HighHz, Adapt, Listen, Output, kCount };
} // namespace restorer

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeRestorer();

// The learned noise floor, in the spectrum display's dB scale (for the faceplate).
class NoiseProfileSource {
public:
    virtual ~NoiseProfileSource() = default;
    [[nodiscard]] virtual float noiseFloorDb(int displayBin) const noexcept = 0;
    [[nodiscard]] virtual float removedDb() const noexcept = 0;   // how much is being taken out overall (<= 0)
};

} // namespace hem::rack
