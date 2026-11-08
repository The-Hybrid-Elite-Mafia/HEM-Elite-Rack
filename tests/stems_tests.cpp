// The rack app's stems: up to 13 stems loaded at once, each in its own slot with its level, pan, mute and solo,
// mixed into the chain. Checks the mix against the stems sample by sample, the settings' gains, solo / mute,
// stems of different lengths, one stem swapped while the rest play on, the per-stem meters, the WAV-export mix
// (the same numbers as the live one), B (ORIGINAL) = the stem mix without the chain, no allocation on the audio
// path with 13 stems, and the time 13 stems take per block.
#include "Harness.hpp"

#include "Chain.hpp"
#include "RackEngine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace hem::test {
namespace {

using namespace hem::app;

// A stem: 0.1 s of silence (the transport's 5 ms fade-in touches nothing), then a sine at its own frequency, its
// own level, and on the right a different level (so left and right can be told apart). mono: one channel only.
std::shared_ptr<Song> stemTone(double seconds, double hz, float levelL, float levelR, bool mono = false) {
    auto s = std::make_shared<Song>();
    const std::size_t n = static_cast<std::size_t>(seconds * kRate);
    s->left.assign(n, 0.0f);
    if (!mono) s->right.assign(n, 0.0f);
    for (std::size_t i = 4800; i < n; ++i) {
        const float v = static_cast<float>(std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / kRate));
        s->left[i] = levelL * v;
        if (!mono) s->right[i] = levelR * v;
    }
    char b[48];
    std::snprintf(b, sizeof b, "tone %.0f Hz", hz);
    s->name = b;
    return s;
}

struct Out { std::vector<float> l, r; };
Out playFor(RackProcessor& rack, std::size_t frames, std::uint32_t block = 480) {
    Out o{std::vector<float>(frames), std::vector<float>(frames)};
    rack.play();
    for (std::size_t i = 0; i < frames; i += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, frames - i));
        rack.render(o.l.data() + i, o.r.data() + i, n);
    }
    return o;
}

// The largest difference from what the stems should add up to at these gains (per stem gl / gr), over [from, to).
double mixError(const Out& o, const StemSet& stems, const std::vector<std::pair<float, float>>& g, std::size_t from, std::size_t to) {
    double m = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        double el = 0.0, er = 0.0;
        for (int k = 0; k < kMaxStems; ++k) {
            const auto& s = stems[static_cast<std::size_t>(k)];
            if (!s || i >= s->frames()) continue;
            el += static_cast<double>(s->left[i]) * g[static_cast<std::size_t>(k)].first;
            er += static_cast<double>(s->rightOrLeft()[i]) * g[static_cast<std::size_t>(k)].second;
        }
        m = std::max({m, std::fabs(el - o.l[i]), std::fabs(er - o.r[i])});
    }
    return m;
}

double rms(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(std::max<std::size_t>(to - from, 1)));
}

} // namespace

void testStems() {
    std::printf("[stems]\n");
    CHECK(kMaxStems == 13);   // the rig's stem count, from The Hybrid Core's config

    // 13 stems at once, each its own tone, levels and lengths; stem 5 mono; stem 13 the shortest
    StemSet stems{};
    const double hz[13] = {55, 82, 110, 147, 196, 262, 349, 466, 622, 831, 1109, 1480, 1976};
    for (int k = 0; k < kMaxStems; ++k) {
        const double secs = k == 12 ? 1.0 : 2.0 + 0.05 * k;
        stems[static_cast<std::size_t>(k)] = stemTone(secs, hz[k], 0.04f + 0.002f * k, 0.03f + 0.001f * k, k == 4);
    }
    {
        RackProcessor rack;
        CHECK(rack.submitStems(stems));
        CHECK(rack.stemCount() == 13);
        CHECK(rack.songFrames() == stems[11]->frames());   // as long as the longest
        const std::size_t n = stems[11]->frames();
        const Out o = playFor(rack, n);
        std::vector<std::pair<float, float>> unity(13, {1.0f, 1.0f});
        const double err = mixError(o, stems, unity, 4800, n);
        std::printf("  13 stems at 0 dB, centre: largest difference from their sum %.2e\n", err);
        CHECK(err < 1e-6);
        // stem 13 (1.0 s) has ended: past it the mix is the other 12 only (also covered by mixError)
        CHECK(stems[12]->frames() < n);
        // the per-stem meters read each stem's real peak (stem 1: 0.04 left, 0.03 right -> 0.04)
        rack.stop();
        (void)rack.stemPeak(0);
        rack.play();
        std::vector<float> l(4800), r(4800);
        rack.seek(24000);
        for (int b = 0; b < 30; ++b) rack.render(l.data(), r.data(), 480);
        const float p1 = rack.stemPeak(0), p5 = rack.stemPeak(4), p13 = rack.stemPeak(12);
        std::printf("  meters: stem 1 peak %.4f (0.0400), stem 5 (mono) %.4f (0.0480), stem 13 %.4f (0.0640)\n", p1, p5, p13);
        CHECK(std::fabs(p1 - 0.04f) < 0.0005f);
        CHECK(std::fabs(p5 - 0.048f) < 0.0005f);
        CHECK(std::fabs(p13 - 0.064f) < 0.0005f);
    }

    // level, pan, mute and solo: the exact gains (after the 10 ms glide)
    {
        auto gains = [](StemMix m, bool anySolo) { std::pair<float, float> g; stemGains(m, anySolo, g.first, g.second); return g; };
        const auto g6 = gains(StemMix{-6.0f, 0.0f, false, false}, false);
        CHECK(std::fabs(g6.first - 0.501187f) < 1e-5f && g6.first == g6.second);
        const auto gp = gains(StemMix{0.0f, 0.5f, false, false}, false);
        CHECK(std::fabs(gp.first - 0.5f) < 1e-6f && gp.second == 1.0f);
        const auto gL = gains(StemMix{0.0f, -1.0f, false, false}, false);
        CHECK(gL.first == 1.0f && gL.second == 0.0f);
        CHECK(gains(StemMix{kStemMinDb, 0.0f, false, false}, false).first == 0.0f);
        CHECK(gains(StemMix{0.0f, 0.0f, true, false}, false).first == 0.0f);    // muted
        CHECK(gains(StemMix{0.0f, 0.0f, false, false}, true).first == 0.0f);    // another is soloed
        CHECK(gains(StemMix{0.0f, 0.0f, false, true}, true).first == 1.0f);     // this one is
        CHECK(gains(StemMix{0.0f, 0.0f, true, true}, true).first == 0.0f);      // mute wins

        RackProcessor rack;
        CHECK(rack.submitStems(stems));
        StemMixes mix{};
        mix[0] = {-6.0f, 0.0f, false, false};
        mix[1] = {+6.0f, 0.5f, false, false};
        mix[2] = {0.0f, -1.0f, false, false};
        mix[3] = {0.0f, 0.0f, true, false};     // muted
        mix[4] = {kStemMinDb, 0.0f, false, false};
        for (int k = 0; k < kMaxStems; ++k) rack.setStemMix(k, mix[static_cast<std::size_t>(k)]);
        const std::size_t n = 48000;
        const Out o = playFor(rack, n);
        std::vector<std::pair<float, float>> g(13);
        for (int k = 0; k < kMaxStems; ++k) stemGains(mix[static_cast<std::size_t>(k)], false, g[static_cast<std::size_t>(k)].first, g[static_cast<std::size_t>(k)].second);
        const double err = mixError(o, stems, g, 4800 + 4800, n);   // after the glide (10 ms; 100 ms allowed)
        std::printf("  levels / pan / mute: largest difference from the expected mix %.2e\n", err);
        CHECK(err < 1e-5);
        CHECK(rack.stemMix(1).gainDb == 6.0f && rack.stemMix(1).pan == 0.5f && rack.stemMix(3).mute);

        // solo stems 7 and 9: only they play
        StemMix s7 = rack.stemMix(6), s9 = rack.stemMix(8);
        s7.solo = s9.solo = true;
        rack.setStemMix(6, s7);
        rack.setStemMix(8, s9);
        const Out so = playFor(rack, n);
        std::vector<std::pair<float, float>> gs(13, {0.0f, 0.0f});
        gs[6] = gs[8] = {1.0f, 1.0f};
        // the transport went on from where it was: compare against the stems at those positions
        const std::uint64_t at = rack.playhead() - n;
        double serr = 0.0;
        for (std::size_t i = 9600; i < n; ++i) {   // after the gains' 10 ms glide (100 ms allowed)
            const std::size_t k = static_cast<std::size_t>(at) + i;
            const double el = stems[6]->left[k] + stems[8]->left[k], er = stems[6]->right[k] + stems[8]->right[k];
            serr = std::max({serr, std::fabs(el - so.l[i]), std::fabs(er - so.r[i])});
        }
        std::printf("  solo 7 + 9: largest difference from those two alone %.2e\n", serr);
        CHECK(serr < 1e-5);
    }

    // one stem swapped while the others play on: the transport keeps its place
    {
        RackProcessor rack;
        CHECK(rack.submitStems(stems));
        std::vector<float> l(480), r(480);
        rack.play();
        for (int b = 0; b < 50; ++b) rack.render(l.data(), r.data(), 480);
        const std::uint64_t before = rack.playhead();
        CHECK(rack.setStem(2, stemTone(3.0, 700.0, 0.05f, 0.05f)));
        rack.render(l.data(), r.data(), 480);
        std::printf("  stem 3 swapped while playing: playhead %llu -> %llu (keeps its place), length now %.2f s\n",
                    static_cast<unsigned long long>(before), static_cast<unsigned long long>(rack.playhead()), static_cast<double>(rack.songFrames()) / kRate);
        CHECK(rack.playhead() == before + 480);
        CHECK(rack.songFrames() == static_cast<std::uint64_t>(3.0 * kRate));
        CHECK(rack.setStem(2, nullptr));
        CHECK(rack.stemCount() == 12);
        CHECK(rack.songFrames() == stems[11]->frames());
        // a single song is stem 1 with the rest empty
        CHECK(rack.submitSong(stemTone(1.0, 440.0, 0.1f, 0.1f)));
        CHECK(rack.stemCount() == 1 && rack.stem(0) && !rack.stem(1));
        rack.collectRetired();
    }

    // the WAV export's mix: the same as the live one, and B (ORIGINAL) plays it untouched by the chain
    {
        StemMixes mix{};
        mix[0] = {-3.0f, -0.25f, false, false};
        mix[5] = {+4.0f, 0.75f, false, false};
        mix[9] = {0.0f, 0.0f, true, false};
        const auto mixed = mixStems(stems, mix, "mixdown");
        CHECK(mixed->frames() == stems[11]->frames());
        RackProcessor rack;
        CHECK(rack.submitStems(stems));
        for (int k = 0; k < kMaxStems; ++k) rack.setStemMix(k, mix[static_cast<std::size_t>(k)]);
        const std::size_t n = 48000;
        const Out o = playFor(rack, n);
        double err = 0.0;
        for (std::size_t i = 4800; i < n; ++i) err = std::max({err, std::fabs(static_cast<double>(mixed->left[i]) - o.l[i]), std::fabs(static_cast<double>(mixed->right[i]) - o.r[i])});
        std::printf("  export mix vs live mix: largest difference %.2e\n", err);
        CHECK(err < 1e-6);
        // the export renders the mix through the chain (empty chain: the mix itself)
        std::vector<float> el, er;
        CHECK(renderOffline(*mixed, {}, el, er, {}, nullptr));
        CHECK(el.size() == mixed->frames());
        double e2 = 0.0;
        for (std::size_t i = 0; i < el.size(); ++i) e2 = std::max(e2, std::fabs(static_cast<double>(el[i]) - mixed->left[i]));
        CHECK(e2 == 0.0);

        // a chain that changes the sound: B (ORIGINAL) still plays the plain stem mix
        RackProcessor ab;
        CHECK(ab.submitStems(stems));
        for (int k = 0; k < kMaxStems; ++k) ab.setStemMix(k, mix[static_cast<std::size_t>(k)]);
        auto gain = makePlugin("HOT WIRE");
        if (gain) {
            gain->prepare(kRate, kChunk);
            CHECK(ab.setSlot(0, std::move(gain)));
        }
        ab.setMatch(false);
        ab.setOriginal(true);
        const Out b = playFor(ab, n);
        const std::uint32_t lat = ab.latency();
        double berr = 0.0;
        for (std::size_t i = 4800 + 2400 + lat; i < n; ++i) berr = std::max(berr, std::fabs(static_cast<double>(mixed->left[i - lat]) - b.l[i]));
        std::printf("  B (ORIGINAL) with a chain: largest difference from the plain stem mix %.2e (chain delay %u samples)\n", berr, lat);
        CHECK(berr < 1e-6);
    }

    // the audio path with 13 stems: no allocation, and its time per block
    {
        RackProcessor rack;
        CHECK(rack.submitStems(stems));
        std::vector<float> l(480), r(480);
        rack.play();
        rack.render(l.data(), r.data(), 480);
        std::uint64_t allocs = 0;
        {
            AllocScope scope;
            for (int b = 0; b < 200; ++b) {
                StemMix m = rack.stemMix(b % 13);
                m.gainDb = -static_cast<float>(b % 7);
                m.solo = b % 50 == 0;
                rack.setStemMix(b % 13, m);
                rack.render(l.data(), r.data(), 480);
                (void)rack.stemPeak(b % 13);
            }
            allocs = scope.count();
        }
        std::printf("  13 stems, 200 blocks while moving faders: %llu heap allocations\n", static_cast<unsigned long long>(allocs));
        CHECK(allocs == 0);
        rack.setLoop(true);
        const int blocks = 2000;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b) rack.render(l.data(), r.data(), 480);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / blocks;
        std::printf("  13 stems, no plug-ins: %.1f us per 480-frame block (10 ms of audio) = %.2f%% of one core\n", us, us / 10000.0 * 100.0);
        CHECK(us < 1000.0);   // under 10 % of the block's time
        (void)rms;
    }
}

} // namespace hem::test
