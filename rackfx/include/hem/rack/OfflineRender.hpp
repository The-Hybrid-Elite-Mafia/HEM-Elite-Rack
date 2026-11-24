#pragma once
// A plug-in that works differently when the host renders faster than real time (a DAW export, the rack app's
// WAV export). The host calls setOfflineRender(true) before such a render, while it is not processing.
// FINGERPRINT uses it to make each new reverb response on the audio thread at a fixed block, instead of on its
// background thread, so an export never depends on how fast the computer is.
namespace hem::rack {

class OfflineRender {
public:
    virtual void setOfflineRender(bool on) = 0;

protected:
    ~OfflineRender() = default;
};

} // namespace hem::rack
