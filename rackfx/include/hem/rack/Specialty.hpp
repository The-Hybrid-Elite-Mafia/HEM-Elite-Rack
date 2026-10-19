#pragma once
// =============================================================================
//  HEM-Elite-Rack specialty tools (our own designs). All zero latency.
//
//  HEM UNDERGROUND — more low end: OCTAVE makes a new sub one octave under the bass (tracks the bass
//    notes below FREQ), HARMONICS adds the bass's overtones so small speakers and phones "hear" the
//    low end. AMOUNT (how much is added), TONE (how dark the added part is), OUTPUT. The original
//    sound always passes untouched; AMOUNT 0 = exactly the input.
//  HEM CHOKEHOLD — controls the low end below FREQ (60..250 Hz, Linkwitz-Riley split, adds
//    back up flat): TIGHTEN (holds boomy notes down, fast), PUNCH (brings the attack of kicks and
//    bass notes forward), MONO (the low end in the centre), LOW GAIN, OUTPUT.
//  HEM SPOTLIGHT — a vocal chain on one plate: HIGH-PASS, BODY (200 Hz), COMPRESS (one knob),
//    DE-ESS (one knob), PRESENCE (4 kHz), AIR (12 kHz shelf), OUTPUT. With everything at 0 it is
//    exactly the input.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <memory>

namespace hem::rack {

namespace sub { enum P : int { Mode, Freq, Amount, Tone, Output, kCount }; }
namespace tightener { enum P : int { Freq, Tighten, Punch, Mono, LowGain, Output, kCount }; }
namespace vocal {
enum P : int { Hpf, Body, Compress, DeEss, Presence, Air, Output, kCount };
inline constexpr float kHpfHz[] = {0, 60, 80, 100, 120};
} // namespace vocal

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeSub();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeBassTightener();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeVocalStrip();

} // namespace hem::rack
