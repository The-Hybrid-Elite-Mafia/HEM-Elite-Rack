#pragma once
// An effect that plays notes (an instrument). The host hands it MIDI-style events for the block it is
// about to process, on the audio thread, before that block's process() call; `offset` is the sample in
// that block where the event lands (0 .. block-1). Notes are MIDI numbers (60 = middle C), velocity
// 0..1. sustainParam() is the parameter the sustain pedal (MIDI controller 64) moves, or -1.
namespace hem::rack {
class NoteSink {
public:
    virtual ~NoteSink() = default;
    virtual void noteOn(int note, float velocity, int offset) noexcept = 0;
    virtual void noteOff(int note, int offset) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;
    [[nodiscard]] virtual int sustainParam() const noexcept { return -1; }
};
} // namespace hem::rack
