// The rack app's audio side without a sound card: the chain, ON / BYPASS, A / B with matched
// loudness, the transport, the WAV-export render and the saved chain text.
#include "Harness.hpp"
#include "Signals.hpp"

#include "Chain.hpp"
#include "RackEngine.hpp"

#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"

#include <cmath>
#include <memory>
#include <string>

namespace hem::test {
namespace {

using namespace hem::app;

// A test "song": 0.1 s of silence (so the transport's 5 ms fade-in touches nothing), then music-ish
// material: a bass line, a 1.2 kHz voice, hiss.
std::shared_ptr<Song> testSong(double seconds) {
    auto s = std::make_shared<Song>();
    const std::size_t n = static_cast<std::size_t>(seconds * kRate);
    s->left.assign(n, 0.0f);
    s->right.assign(n, 0.0f);
    std::uint32_t rng = 1;
    for (std::size_t i = 4800; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        rng = rng * 1664525u + 1013904223u;
        const float hiss = 0.01f * (static_cast<float>(rng >> 8) / 8388608.0f - 1.0f);
        const float bass = 0.3f * static_cast<float>(std::sin(2 * std::numbers::pi * 55.0 * t));
        const float voice = 0.15f * static_cast<float>(std::sin(2 * std::numbers::pi * 1200.0 * t) * (0.6 + 0.4 * std::sin(2 * std::numbers::pi * 2.0 * t)));
        s->left[i] = bass + voice + hiss;
        s->right[i] = bass - 0.5f * voice + hiss;
    }
    s->name = "test";
    return s;
}

SlotState eqState(float shelfGainDb) {
    using namespace rack::eq;
    SlotState st;
    st.name = "SURGEON";
    auto e = makePlugin("SURGEON");
    e->set(bandParam(0, On), 1);
    e->set(bandParam(0, TypeC), static_cast<float>(static_cast<int>(Type::HighShelf)));
    e->set(bandParam(0, Freq), 800);
    e->set(bandParam(0, Gain), shelfGainDb);
    e->set(bandParam(1, On), 1);
    e->set(bandParam(1, TypeC), static_cast<float>(static_cast<int>(Type::Bell)));
    e->set(bandParam(1, Freq), 200);
    e->set(bandParam(1, Gain), -4);
    e->set(bandParam(1, Q), 2);
    const auto ps = e->params();
    for (std::size_t i = 0; i < ps.size(); ++i) st.values.emplace_back(ps[i].id, e->get(static_cast<int>(i)));
    return st;
}

SlotState restorerState() {
    SlotState st;
    st.name = "THE CLEANER";
    auto e = makePlugin("THE CLEANER");
    e->set(rack::restorer::Reduction, 6.0f);
    const auto ps = e->params();
    for (std::size_t i = 0; i < ps.size(); ++i) st.values.emplace_back(ps[i].id, e->get(static_cast<int>(i)));
    return st;
}

// Plays the whole song through the rack in hardware-sized blocks; returns what came out.
Stereo play(RackProcessor& rack, std::size_t frames, std::uint32_t block) {
    Stereo out(frames);
    rack.play();
    for (std::size_t i = 0; i < frames; i += block) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, frames - i));
        rack.render(out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

double maxDiff(const std::vector<float>& a, const std::vector<float>& b, std::size_t from, std::size_t to) {
    double m = 0;
    for (std::size_t i = from; i < to && i < a.size() && i < b.size(); ++i) m = std::max(m, std::fabs(static_cast<double>(a[i]) - b[i]));
    return m;
}

} // namespace

void testChain() {
    std::printf("[rack chain]\n");
    const auto song = testSong(4.0);
    const std::size_t n = song->frames();

    // 1. Export = what you hear: the offline render equals the live chain (144-frame blocks),
    //    shifted by the chain's latency.
    {
        const std::vector<SlotState> chain{restorerState(), eqState(4.0f)};
        RackProcessor rack;
        (void)rack.submitSong(song);
        for (std::size_t i = 0; i < chain.size(); ++i) (void)rack.setSlot(static_cast<int>(i), makePlugin(chain[i]));
        const Stereo live = play(rack, n + 4096, 144);
        const std::uint32_t lat = rack.latency();
        std::vector<float> offL, offR;
        std::string err;
        CHECK(renderOffline(*song, chain, offL, offR, {}, &err));
        CHECK(offL.size() == n);
        std::vector<float> liveAligned(live.l.begin() + lat, live.l.begin() + lat + static_cast<std::ptrdiff_t>(n));
        const double d = maxDiff(liveAligned, offL, 0, n);
        std::printf("  export vs live: latency %u samples, largest difference %.2e\n", lat, d);
        CHECK(lat == 2048 + 23);
        CHECK(d < 1e-5);
    }
    // 2. ON / BYPASS: a switched-off slot plays its input delayed by its latency — the song itself here.
    {
        RackProcessor rack;
        (void)rack.submitSong(song);
        (void)rack.setSlot(0, makePlugin(eqState(9.0f)));
        rack.setSlotOn(0, false);
        const Stereo out = play(rack, n, 256);
        std::vector<float> want(n, 0.0f);
        for (std::size_t i = 23; i < n; ++i) want[i] = song->left[i - 23];
        const double d = maxDiff(out.l, want, 9600, n);
        std::printf("  bypassed SURGEON vs the song delayed 23 samples: %.2e\n", d);
        CHECK(d < 1e-6);
    }
    // 3. A / B: B is the untouched song lined up with A; with MATCH LOUDNESS it is brought to A's loudness.
    {
        RackProcessor rack;
        (void)rack.submitSong(song);
        (void)rack.setSlot(0, makePlugin(eqState(9.0f)));   // +9 dB top: A is clearly louder
        rack.setMatch(false);
        rack.setOriginal(true);
        const Stereo out = play(rack, n, 512);
        std::vector<float> want(n, 0.0f);
        for (std::size_t i = 23; i < n; ++i) want[i] = song->left[i - 23];
        const double d = maxDiff(out.l, want, 9600, n);
        std::printf("  B (no match) vs the song: %.2e\n", d);
        CHECK(d < 1e-6);

        RackProcessor m;
        (void)m.submitSong(song);
        (void)m.setSlot(0, makePlugin(eqState(9.0f)));
        m.setMatch(true);
        (void)play(m, n, 512);
        const float a = m.processedLufs(), b = m.originalLufs(), g = m.matchGainDb();
        std::printf("  A %.2f LUFS, B %.2f LUFS, B raised by %.2f dB\n", a, b, g);
        CHECK(a > b + 1.0f);
        CHECK(std::fabs((b + g) - a) < 0.3f);
    }
    // 4. No allocation while playing, editing, bypassing, switching A/B.
    {
        RackProcessor rack;
        (void)rack.submitSong(song);
        (void)rack.setSlot(0, makePlugin(restorerState()));
        (void)rack.setSlot(1, makePlugin(eqState(3.0f)));
        std::vector<float> l(480), r(480);
        rack.play();
        rack.render(l.data(), r.data(), 480);   // applies the queued changes
        AllocScope scope;
        for (int b = 0; b < 200; ++b) {
            if (b == 50) rack.setSlotOn(1, false);
            if (b == 80) rack.setOriginal(true);
            if (b == 120) rack.seek(48000);
            rack.render(l.data(), r.data(), 480);
        }
        CHECK(scope.count() == 0);
    }
    // 5. Transport: seek, the end of the song, loop.
    {
        RackProcessor rack;
        (void)rack.submitSong(song);
        std::vector<float> l(480), r(480);
        rack.seek(96000);
        rack.play();
        for (int i = 0; i < 10; ++i) rack.render(l.data(), r.data(), 480);
        std::printf("  seek to 96000 then 4800 samples of play: playhead %llu\n", static_cast<unsigned long long>(rack.playhead()));
        CHECK(rack.playhead() > 96000 && rack.playhead() <= 96000 + 4800);
        rack.seek(n - 1000);
        for (int i = 0; i < 10; ++i) rack.render(l.data(), r.data(), 480);
        CHECK(!rack.wantsPlay());          // stopped at the end
        CHECK(rack.playhead() == n);
        rack.setLoop(true);
        rack.seek(n - 1000);
        rack.play();
        for (int i = 0; i < 10; ++i) rack.render(l.data(), r.data(), 480);
        CHECK(rack.wantsPlay());
        CHECK(rack.playhead() < 5000);     // wrapped round to the start
    }
    // 6. Moving slots swaps them; emptying a slot frees the plug-in (after the audio side lets go).
    {
        RackProcessor rack;
        (void)rack.setSlot(0, makePlugin("THE CLEANER"));
        (void)rack.setSlot(1, makePlugin("SURGEON"));
        std::vector<float> l(64), r(64);
        rack.render(l.data(), r.data(), 64);
        CHECK(rack.moveSlot(0, 1));
        rack.render(l.data(), r.data(), 64);
        CHECK(std::string(pluginName(*rack.effect(0))) == "SURGEON");
        CHECK(std::string(pluginName(*rack.effect(1))) == "THE CLEANER");
        CHECK(rack.latency() == 2048 + 23);
        (void)rack.setSlot(1, nullptr);
        rack.render(l.data(), r.data(), 64);
        rack.collectRetired();
        CHECK(rack.usedSlots() == 1);
        CHECK(rack.latency() == 23);
    }
}

void testRackSession() {
    std::printf("[rack session]\n");
    RackProcessor rack;
    SlotState eq = eqState(5.5f);
    eq.slot = 2;
    (void)rack.setSlot(2, makePlugin(eq));
    (void)rack.setSlot(5, makePlugin(restorerState()));
    rack.setSlotOn(5, false);
    const auto chain = snapshotChain(rack);
    CHECK(chain.size() == 2);
    const std::string text = chainToText(chain);
    std::string note;
    const auto back = chainFromText(text + "slot7=NOT A PLUGIN|on=1\n", &note);
    CHECK(back.size() == 2);
    CHECK(!note.empty());
    bool same = back.size() == chain.size();
    for (std::size_t i = 0; same && i < back.size(); ++i) {
        same = back[i].slot == chain[i].slot && back[i].name == chain[i].name && back[i].on == chain[i].on && back[i].values.size() == chain[i].values.size();
        for (std::size_t k = 0; same && k < back[i].values.size(); ++k)
            same = back[i].values[k].first == chain[i].values[k].first && back[i].values[k].second == chain[i].values[k].second;
    }
    CHECK(same);
    CHECK(back[0].slot == 2 && back[0].name == "SURGEON" && back[0].on);
    CHECK(back[1].slot == 5 && back[1].name == "THE CLEANER" && !back[1].on);
    // Restoring builds plug-ins with exactly those settings.
    auto e = makePlugin(back[0]);
    CHECK(std::fabs(e->get(rack::eq::bandParam(0, rack::eq::Gain)) - 5.5f) < 1e-6f);
    std::printf("  chain text: %zu bytes for 2 plug-ins, round trip exact\n", text.size());
}

} // namespace hem::test
