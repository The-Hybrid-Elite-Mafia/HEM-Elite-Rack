// hem_rack_tests — every check of HEM-Elite-Rack's DSP, faceplates and rack app (no sound card,
// no GPU needed). Exit code 1 when anything fails.
#include "Harness.hpp"

#include <cstdio>
#include <filesystem>

namespace hem::test {
namespace {
std::filesystem::path gExeDir;
}
void setExePath(const char* argv0) {
    std::error_code ec;
    gExeDir = std::filesystem::absolute(argv0, ec).parent_path();
}
std::filesystem::path exeDir() { return gExeDir; }
} // namespace hem::test

int main(int argc, char** argv) {
    using namespace hem::test;
    setExePath(argc > 0 ? argv[0] : ".");
    testSpectrum();
    testEliteEq();
    testRestorer();
    testBusGlue();
    testTape();
    testTubeEq();
    testLimiter();
    testWidth();
    testExciter();
    testLoudnessMeter();
    testMoreDynamics();
    testColorFx();
    testSpace();
    testMovement();
    testSpecialty();
    testHitman();
    testArsenal();
    testToneShop();
    testHideout();
    testStreets();
    testMaximizers();
    testFaces();
    testChain();
    testRackSession();
    testLiveRack();
    std::printf("\nhem_rack_tests: %d checks, %d failed\n", gChecks, gFails);
    return gFails ? 1 : 0;
}
