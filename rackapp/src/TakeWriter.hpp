#pragma once
// =============================================================================
//  TakeWriter — a WAV file written while it is recorded (24-bit PCM, stereo, 48 kHz): the header goes first
//  with the sizes left open, the frames are appended as they come, finish() writes the sizes in.
//  skip(n): the first n frames handed over are left out (they line the take up with the song: the sound
//  card's round trip, and the chain's delay for the processed take). Message thread (or any one thread).
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace hem::app {

class TakeWriter {
public:
    TakeWriter() = default;
    ~TakeWriter() { finish(); }
    TakeWriter(const TakeWriter&)            = delete;
    TakeWriter& operator=(const TakeWriter&) = delete;

    bool open(const std::filesystem::path& file, std::uint32_t sampleRate, std::uint64_t skipFrames);
    // interleaved L / R
    bool write(const float* lr, std::size_t frames);
    // writes the sizes; returns the frames in the file
    std::uint64_t finish();
    [[nodiscard]] bool isOpen() const noexcept { return f_ != nullptr; }
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] float peak() const noexcept { return peak_; }

private:
    std::FILE* f_ = nullptr;
    std::uint64_t frames_ = 0, skip_ = 0;
    float peak_ = 0.0f;
    std::vector<std::uint8_t> buf_;
};

} // namespace hem::app
