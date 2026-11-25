#pragma once
// =============================================================================
//  CleanPass for HEM-Elite-Rack (same as HybridCore's hc::fx::CleanPass; kept here because the
//  rack builds against a pinned HybridCore).
// =============================================================================
#include <algorithm>
#include <cstdint>
#include <vector>

namespace hem::rack {

// ---- clean pass -------------------------------------------------------------------------------
// While a plug-in's colour controls all sit at zero, its output is its own input, only delayed by
// the plug-in's reported latency (bit-exact: nothing added, nothing taken away). When a control
// moves off zero it crossfades to the processed sound over 10 ms (and back the same way), so it
// never clicks. The effect keeps processing underneath, so its meters stay live.
//   prepare(): once, with the largest latency the effect can report. push() before the effect's
//   own processing (stores the input), apply() after it (blends in the delayed input).
class CleanPass {
public:
    static constexpr std::uint32_t kMaxBlock = 4096;
    void prepare(double fs, std::uint32_t maxLatency) {
        std::uint32_t c = 1;
        while (c < maxLatency + kMaxBlock + 1) c <<= 1;
        l_.assign(c, 0.0f); r_.assign(c, 0.0f);
        dl_.assign(kMaxBlock, 0.0f); dr_.assign(kMaxBlock, 0.0f);
        mask_ = c - 1;
        step_ = static_cast<float>(1.0 / std::max(1.0, 0.010 * fs));
        reset(true);
    }
    void reset(bool clean) noexcept {
        std::fill(l_.begin(), l_.end(), 0.0f); std::fill(r_.begin(), r_.end(), 0.0f);
        w_ = 0; g_ = clean ? 1.0f : 0.0f;
        fresh_ = true;   // the first block after a reset starts where the settings are (no fade from the old state)
    }
    void push(const float* L, const float* R, std::uint32_t n, std::uint32_t latency) noexcept {
        n = std::min(n, kMaxBlock);
        latency = std::min(latency, mask_ - kMaxBlock);
        for (std::uint32_t i = 0; i < n; ++i) {
            l_[w_] = L[i]; r_[w_] = R[i];
            const std::uint32_t rd = (w_ - latency) & mask_;
            dl_[i] = l_[rd]; dr_[i] = r_[rd];
            w_ = (w_ + 1) & mask_;
        }
    }
    // sign -1: the clean sound comes out with its polarity flipped (a plug-in whose only setting is a FLIP).
    void apply(float* L, float* R, std::uint32_t n, bool clean, float sign = 1.0f) noexcept {
        n = std::min(n, kMaxBlock);
        const float target = clean ? 1.0f : 0.0f;
        if (fresh_) { g_ = target; fresh_ = false; }
        if (g_ == target && !clean) return;
        for (std::uint32_t i = 0; i < n; ++i) {
            g_ = target > g_ ? std::min(target, g_ + step_) : std::max(target, g_ - step_);
            if (g_ >= 1.0f) { L[i] = sign * dl_[i]; R[i] = sign * dr_[i]; }
            else if (g_ > 0.0f) { L[i] += g_ * (sign * dl_[i] - L[i]); R[i] += g_ * (sign * dr_[i] - R[i]); }
        }
    }
    [[nodiscard]] bool fullyClean() const noexcept { return g_ >= 1.0f; }

private:
    std::vector<float> l_, r_, dl_, dr_;
    std::uint32_t mask_ = 0, w_ = 0;
    float step_ = 0.002f, g_ = 1.0f;
    bool fresh_ = true;
};

} // namespace hem::rack
