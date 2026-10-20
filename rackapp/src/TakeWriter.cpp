#include "TakeWriter.hpp"

#include <algorithm>
#include <cmath>

namespace hem::app {
namespace {
void put16(std::uint8_t* p, std::uint32_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); }
void put32(std::uint8_t* p, std::uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i)); }
constexpr std::uint32_t kHeader = 44;
} // namespace

bool TakeWriter::open(const std::filesystem::path& file, std::uint32_t sampleRate, std::uint64_t skipFrames) {
    finish();
#if defined(_WIN32)
    f_ = _wfopen(file.wstring().c_str(), L"wb");
#else
    f_ = std::fopen(file.string().c_str(), "wb");
#endif
    if (!f_) return false;
    frames_ = 0;
    skip_ = skipFrames;
    peak_ = 0.0f;
    std::uint8_t h[kHeader] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    put32(h + 16, 16);                  // fmt chunk size
    put16(h + 20, 1);                   // PCM
    put16(h + 22, 2);                   // channels
    put32(h + 24, sampleRate);
    put32(h + 28, sampleRate * 2 * 3);  // bytes a second
    put16(h + 32, 2 * 3);               // bytes a frame
    put16(h + 34, 24);                  // bits
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    if (std::fwrite(h, 1, kHeader, f_) != kHeader) { std::fclose(f_); f_ = nullptr; return false; }
    return true;
}

bool TakeWriter::write(const float* lr, std::size_t frames) {
    if (!f_) return false;
    const std::size_t skip = static_cast<std::size_t>(std::min<std::uint64_t>(skip_, frames));
    skip_ -= skip;
    lr += 2 * skip;
    frames -= skip;
    if (frames == 0) return true;
    buf_.resize(frames * 6);
    for (std::size_t i = 0; i < frames * 2; ++i) {
        const float x = std::clamp(lr[i], -1.0f, 1.0f);
        peak_ = std::max(peak_, std::fabs(x));
        const auto v = static_cast<std::int32_t>(std::lround(static_cast<double>(x) * 8388607.0));
        const auto u = static_cast<std::uint32_t>(v);
        buf_[3 * i] = static_cast<std::uint8_t>(u);
        buf_[3 * i + 1] = static_cast<std::uint8_t>(u >> 8);
        buf_[3 * i + 2] = static_cast<std::uint8_t>(u >> 16);
    }
    if (std::fwrite(buf_.data(), 1, buf_.size(), f_) != buf_.size()) return false;
    frames_ += frames;
    return true;
}

std::uint64_t TakeWriter::finish() {
    if (!f_) return frames_;
    const std::uint64_t data = frames_ * 6;
    std::uint8_t b[4];
    put32(b, static_cast<std::uint32_t>(std::min<std::uint64_t>(data + kHeader - 8, 0xFFFFFFFFull)));
    std::fseek(f_, 4, SEEK_SET);
    std::fwrite(b, 1, 4, f_);
    put32(b, static_cast<std::uint32_t>(std::min<std::uint64_t>(data, 0xFFFFFFFFull)));
    std::fseek(f_, 40, SEEK_SET);
    std::fwrite(b, 1, 4, f_);
    std::fclose(f_);
    f_ = nullptr;
    return frames_;
}

} // namespace hem::app
