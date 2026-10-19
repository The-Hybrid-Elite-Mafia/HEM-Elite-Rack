#pragma once
// Minimal test harness for hem_rack_tests (same style as The Hybrid Core's hc_tests).
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace hem::test {

inline int gFails  = 0;
inline int gChecks = 0;

// Heap allocation counter (operator new replacement in AllocTracker.cpp), counting only
// inside an AllocScope: proves the audio path never allocates.
inline std::atomic<bool>          gTrackAllocs{false};
inline std::atomic<std::uint64_t> gAllocCount{0};
struct AllocScope {
    AllocScope() noexcept { gAllocCount.store(0); gTrackAllocs.store(true); }
    ~AllocScope() { gTrackAllocs.store(false); }
    [[nodiscard]] std::uint64_t count() const noexcept { return gAllocCount.load(); }
};

void testEliteEq();
void testRestorer();
void testBusGlue();
void testTape();
void testTubeEq();
void testLimiter();
void testWidth();
void testExciter();
void testLoudnessMeter();
void testMoreDynamics();
void testColorFx();
void testSpace();
void testMovement();
void testSpecialty();
void testSpectrum();
void testFaces();
void testChain();
void testRackSession();

void setExePath(const char* argv0);
[[nodiscard]] std::filesystem::path exeDir();

} // namespace hem::test

#define CHECK(cond)                                                               \
    do {                                                                          \
        ++::hem::test::gChecks;                                                   \
        if (!(cond)) {                                                            \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            ++::hem::test::gFails;                                                \
        }                                                                         \
    } while (0)
