#pragma once
// =============================================================================
//  The EQ cloner: two plug-ins that work together, like the original pair.
//
//  THE MOLD      sits in a loop through a hardware EQ (or any outboard box): it sends a test signal out, hears it
//                come back, and works out what the box does to each frequency, about six times a second. In the
//                rack app it uses the sound card directly: SEND OUT (the output that feeds the box: 3 and up) and
//                RETURN IN (the input the box comes back on); the music in its slot passes untouched. In a DAW
//                (VST3) its own output is the test signal and its own input (left) the return: put it on a track
//                routed out to the box and back. HEADROOM 0..30 dB (default 14): how far under full scale the
//                test signal peaks. TRANSMIT sends it (off: silence; the original's TRANSMITTING light: ours is a switch,
//                so a forgotten MOLD never blasts test noise). CLEAR resets the clip lights. Readouts: SEND and RETURN levels, their clip
//                lights, LOOP (the round trip it found, ms) and RECEIVING (1 = the loop is closed and it hears
//                its own test signal come back).
//  THE FORGER    plays the captured curve on any track: MODE CAPTURE (follows THE MOLD as you turn the box's
//                knobs), HOLD (keeps the last curve; it is saved with the session), ADD (the box's new moves go on top
//                of the held curve); GAIN -48..+12 dB; SCALE ±16 / ±30 dB (the display); CLEAR (back to flat).
//                Readouts: TRIM (how far the output's peaks sit under 0 dBFS) and RECEIVING. Minimum-phase FIR,
//                4096 taps (made from the curve the way an analogue EQ would play it), partitioned FFT, latency 256.
//                The curve: 120 points from 25 Hz to 20 kHz (the original shows 25 Hz .. 16 kHz).
//  Only one MOLD at a time is listened to (the original allows one capture too); every FORGER hears it.
// =============================================================================
#include "hybrid/fx/Fx.hpp"

#include <cstdint>
#include <memory>

namespace hem::rack {

namespace clone {
inline constexpr int kPoints = 120;                        // the curve: points from kLoHz to kHiHz, log spaced
inline constexpr double kLoHz = 25.0, kHiHz = 20000.0;
[[nodiscard]] double pointHz(int i) noexcept;
inline constexpr float kCurveDb = 30.0f;                   // the curve's range, ±
// The last curve THE MOLD measured (any thread). Returns false when none has come in for half a second.
bool latestCurve(float* db, std::uint32_t* serial) noexcept;
} // namespace clone

namespace mold {
enum P : int { Headroom, SendOut, ReturnIn, ClearClips, Transmit, kCount };
enum Meter : int { SendDb, ReturnDb, ClipSend, ClipReturn, LoopMs, Receiving, kMeters };
} // namespace mold

namespace forger {
enum P : int { Mode, Gain, Scale, Clear, Curve0, kCount = Curve0 + clone::kPoints };
enum ModeV : int { Capture, Hold, Add };
enum Meter : int { Trim, Receiving, kMeters };
} // namespace forger

// A plug-in that uses the sound card's own inputs and outputs besides its slot's stereo (THE MOLD in the rack
// app). Before each process() call the rack hands it the return input's samples and a buffer for the send
// output (either may be null when the sound card has no such channel).
class HardwareLoop {
public:
    virtual ~HardwareLoop() = default;
    [[nodiscard]] virtual int sendOutput() const noexcept = 0;   // 0-based output channel, -1 = none
    [[nodiscard]] virtual int returnInput() const noexcept = 0;  // 0-based input channel, -1 = none
    virtual void hardware(const float* returnIn, float* sendOut, std::uint32_t frames) noexcept = 0;
};

// A plug-in whose plate draws a curve that is not made from its parameters (THE FORGER's clone).
class CloneSource {
public:
    virtual ~CloneSource() = default;
    [[nodiscard]] virtual float cloneDb(int point) const noexcept = 0;   // any thread
};

[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeMold();
[[nodiscard]] std::unique_ptr<hc::fx::Effect> makeForger();

} // namespace hem::rack
