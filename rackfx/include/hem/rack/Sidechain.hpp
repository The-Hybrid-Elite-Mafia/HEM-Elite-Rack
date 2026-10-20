#pragma once
// An effect that can listen to an external sidechain (a second stereo input). The host hands it
// the sidechain samples for the block it is about to process; nullptr = none connected.
// The pointers are only valid for the next process() call.
namespace hem::rack {
class SidechainSink {
public:
    virtual ~SidechainSink() = default;
    virtual void setSidechain(const float* left, const float* right) noexcept = 0;
};
} // namespace hem::rack
