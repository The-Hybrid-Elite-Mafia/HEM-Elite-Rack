// Offline CPU benchmark for the staging engines (not part of any build target).
// g++ -std=c++23 -O2 -march=x86-64-v3 -Irackfx/include -Iexternal/HybridCore/fx/include -Iexternal/HybridCore/core/include -Itests \
//     tools/staging-bench/staging_bench.cpp rackfx/src/staging/*.cpp external/HybridCore/fx/src/*.cpp -o staging_bench
#include "hem/rack/Staging.hpp"
#include "Signals.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
using namespace hem::test;
static bool skip(const std::string& id) {
    for (const char* k : {"bypass", "listen", "solo", "freeze", "monitor", "mute", "reset", "tap", "match", "dim", "invert", "polarity", "swap", "lock"}) if (id.find(k) != std::string::npos) return true;
    return false;
}
int main() {
    const double fs = 48000; const unsigned blk = 512; const double secs = 10.0;
    Stereo src = noise(secs + 1.0, 0.15f, 3, fs); add(src, sine(220.0, secs + 1.0, 0.15f, fs)); add(src, sine(3100.0, secs + 1.0, 0.05f, fs));
    std::printf("%-26s %10s %10s %10s %10s\n", "ENGINE", "def ms/blk", "def %RT", "act ms/blk", "act %RT");
    for (const auto& info : hem::staging::plugins()) {
        double res[2];
        for (int mode = 0; mode < 2; ++mode) {
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep) {
                auto e = info.create(); e->prepare(fs, blk);
                if (mode == 1) {
                    auto ps = e->params();
                    for (size_t i = 0; i < ps.size(); ++i) {
                        const std::string id = ps[i].id;
                        if (skip(id)) continue;
                        if (ps[i].taper == hc::fx::Taper::Choice) continue;
                        if (ps[i].taper == hc::fx::Taper::Toggle) { e->set((int)i, 1.0f); continue; }
                        e->set((int)i, ps[i].min + 0.7f * (ps[i].max - ps[i].min));
                    }
                }
                Stereo s = src;
                const size_t warm = (size_t)fs;
                for (size_t k = 0; k < warm; k += blk) e->process(s.l.data() + k, s.r.data() + k, blk);
                const auto t0 = std::chrono::steady_clock::now();
                const size_t end = warm + (size_t)(secs * fs);
                for (size_t k = warm; k + blk <= end; k += blk) e->process(s.l.data() + k, s.r.data() + k, blk);
                const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                best = std::min(best, sec);
            }
            res[mode] = best;
        }
        const double blocks = secs * fs / blk;
        std::printf("%-26s %10.4f %9.2f%% %10.4f %9.2f%%\n", info.name, res[0] / blocks * 1000.0, res[0] / secs * 100.0, res[1] / blocks * 1000.0, res[1] / secs * 100.0);
    }
}
