#pragma once
// The rules every HEM plug-in follows (silence stays silence, no allocation on the audio thread,
// finite output, constant latency, parameter text round-trips) and the block-size check.
#include "Harness.hpp"
#include "Signals.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/rack/Spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <set>
#include <string>

namespace hem::test {

inline constexpr double kRulesFs = 48000.0;

// The rules every plug-in follows.
inline void commonRules(const rack::PluginInfo& info) {
    auto e = info.create();
    const auto ps = e->params();
    std::set<std::string> ids;
    bool ranges = true, roundTrip = true;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        const auto& p = ps[i];
        ids.insert(p.id);
        if (!(p.min < p.max) || p.def < p.min || p.def > p.max || (p.taper == hc::fx::Taper::Log && p.min <= 0)) ranges = false;
        for (float nrm : {0.0f, 0.37f, 1.0f}) {
            const float v = hc::fx::toPlain(p, nrm);
            float back = 0;
            if (!hc::fx::parseValue(p, hc::fx::formatValue(p, v), back)) { roundTrip = false; continue; }
            const float tol = p.taper == hc::fx::Taper::Log ? v * 0.02f + 0.6f : (p.max - p.min) * 0.02f + 0.06f;
            if (std::fabs(back - v) > tol) roundTrip = false;
        }
    }
    CHECK(!ps.empty() && ps.size() <= static_cast<std::size_t>(hc::fx::Effect::kMaxParams));
    CHECK(ids.size() == ps.size() && ranges && roundTrip);

    e->prepare(kRulesFs, 512);
    const std::uint32_t lat = e->latency();
    // silence stays silence, also with everything switched on
    Stereo z(48000 * 2);
    run(*e, z);
    CHECK(peak(z) < 1e-6f);
    // Every parameter swept at random while loud noise plays: finite, and not one allocation.
    if (auto* sp = dynamic_cast<rack::SpectrumSource*>(e.get())) { sp->spectrumBefore().setActive(true); sp->spectrumAfter().setActive(true); }
    Stereo s = noise(3.0, 0.9f, 11);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::uint64_t allocs = 0;
    {
        AllocScope scope;
        for (std::size_t i = 0; i < s.size(); i += 256) {
            for (int k = 0; k < 6; ++k) {
                const int pi = static_cast<int>(u(rng) * static_cast<float>(ps.size() - 1));
                e->set(pi, hc::fx::toPlain(ps[static_cast<std::size_t>(pi)], u(rng)));
            }
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(256, s.size() - i));
            e->process(s.l.data() + i, s.r.data() + i, n);
        }
        allocs = scope.count();
    }
    std::printf("  %s: %zu parameters, latency %u samples, allocations while playing: %llu\n", info.name, ps.size(), lat,
                static_cast<unsigned long long>(allocs));
    CHECK(allocs == 0);
    CHECK(finite(s));
    CHECK(e->latency() == lat);
    // silence again after setDefaults + reset
    e->setDefaults();
    e->reset();
    Stereo z2(48000);
    run(*e, z2);
    CHECK(peak(z2) < 1e-6f);
}

// Same result whatever the host's block size.
template <class Setup>
double blockSizeDifference(const rack::PluginInfo& info, Setup setup, const Stereo& in) {
    auto a = info.create(), b = info.create();
    setup(*a);
    setup(*b);
    a->prepare(kRulesFs, 512);
    b->prepare(kRulesFs, 512);
    Stereo x = in, y = in;
    run(*a, x, 512);
    run(*b, y, 1);
    double d = 0;
    for (std::size_t i = 0; i < x.size(); ++i) d = std::max({d, static_cast<double>(std::fabs(x.l[i] - y.l[i])), static_cast<double>(std::fabs(x.r[i] - y.r[i]))});
    return d;
}


} // namespace hem::test
