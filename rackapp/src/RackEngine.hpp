#pragma once
// =============================================================================
//  RackProcessor — what HEM-Elite-Rack plays: one song through a chain of up to 8 plug-ins,
//  with a loudness-matched A/B against the untouched song.
//
//  Signal per block:
//    song (or silence) → slot 1 → … → slot 8 → A/B → output
//    Each slot can be switched off (ON / BYPASS): the plug-in keeps running and its output
//    fades over 10 ms to its input delayed by the plug-in's latency, so nothing clicks or
//    moves in time. B (ORIGINAL) is the song delayed by the whole chain's latency, so A and B
//    line up exactly. MATCH LOUDNESS turns B up or down to the loudness of A (K-weighted,
//    3 s memory), so "louder" never wins the comparison.
//
//  Threads
//    message thread : submitSong / setSlot / moveSlot / setSlotOn / transport / collectRetired.
//                     Songs and plug-ins are handed over through wait-free rings; what the
//                     audio thread lets go of comes back through another ring and is freed here.
//    audio thread   : process() only. No allocation, no locks, no waiting.
//  Plug-in parameters are atomics: faceplates turn knobs on the live plug-in directly.
//  MIDI (keyboards): any thread hands a message to midi(); the audio thread passes notes to every plug-in
//  in the chain that plays notes (an instrument), and the sustain pedal to its pedal parameter.
// =============================================================================
#include "hem/rack/Notes.hpp"
#include "hybrid/core/SpscRing.hpp"
#include "hybrid/engine/AudioProcessor.hpp"
#include "hybrid/fx/Dsp.hpp"
#include "hybrid/fx/Fx.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hem::app {

inline constexpr int           kMaxSlots      = 8;
inline constexpr double        kRate          = 48000.0;
inline constexpr std::uint32_t kChunk         = 1024;      // the chain runs in chunks of at most this
inline constexpr std::uint32_t kMaxSlotDelay  = 8192;      // per plug-in latency the bypass path can follow
inline constexpr std::uint32_t kMaxChainDelay = kMaxSlots * kMaxSlotDelay;

struct Song {
    std::vector<float> left, right;   // 48 kHz
    std::string        name;
    [[nodiscard]] std::uint64_t frames() const noexcept { return left.size(); }
};

// Exponential K-weighted loudness meter (stereo), 3 s memory. Audio thread.
struct KMeter {
    hc::fx::Biquad2 shelf, hp;
    double ms = 0.0, coef = 0.0;
    void prepare(double fs);
    void clear() noexcept { shelf.clear(); hp.clear(); ms = 0.0; }
    // Adds a block (copies into `scratch`, which must hold n floats per channel).
    void add(const float* l, const float* r, std::uint32_t n, float* sl, float* sr) noexcept;
    [[nodiscard]] double lufs() const noexcept;   // -0.691 + 10 log10(ms)
};

class RackProcessor final : public hc::engine::AudioProcessor {
public:
    RackProcessor();
    ~RackProcessor() override;
    RackProcessor(const RackProcessor&)            = delete;
    RackProcessor& operator=(const RackProcessor&) = delete;

    // ---- message thread --------------------------------------------------------------------
    // Hands a song to the audio thread (null = no song). Playback stops and goes to the start.
    bool submitSong(std::shared_ptr<const Song> song);
    [[nodiscard]] std::shared_ptr<const Song> song() const noexcept { return ownedSong_; }
    // Puts a prepared plug-in (prepare(kRate, kChunk) already called) into a slot; null empties it.
    bool setSlot(int slot, std::unique_ptr<hc::fx::Effect> effect);
    bool moveSlot(int from, int to);     // swaps two slots
    void setSlotOn(int slot, bool on) noexcept;
    [[nodiscard]] bool slotOn(int slot) const noexcept;
    [[nodiscard]] hc::fx::Effect* effect(int slot) const noexcept;   // the live plug-in (faceplate), or null
    [[nodiscard]] int  usedSlots() const noexcept;
    void collectRetired() noexcept;
    // Applies queued changes on the message thread. Only while no stream runs this processor
    // (no device open), so the chain still updates when there is no sound card.
    void drainWhileStopped() noexcept { applyCommands(); }

    void play() noexcept { play_.store(true, std::memory_order_relaxed); }
    void pause() noexcept { play_.store(false, std::memory_order_relaxed); }
    void stop() noexcept { play_.store(false, std::memory_order_relaxed); seek(0); }
    void seek(std::uint64_t frame) noexcept { seek_.store(static_cast<std::int64_t>(frame), std::memory_order_relaxed); }
    void setLoop(bool on) noexcept { loop_.store(on, std::memory_order_relaxed); }
    void setOriginal(bool b) noexcept { original_.store(b, std::memory_order_relaxed); }   // true = B (untouched song)
    void setMatch(bool on) noexcept { match_.store(on, std::memory_order_relaxed); }
    // One MIDI message (status, data 1, data 2), from any thread (several MIDI inputs at once are fine).
    void midi(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) noexcept;
    [[nodiscard]] bool wantsPlay() const noexcept { return play_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool loop() const noexcept { return loop_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool original() const noexcept { return original_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool match() const noexcept { return match_.load(std::memory_order_relaxed); }

    // Readouts (any thread).
    [[nodiscard]] std::uint64_t playhead() const noexcept { return playhead_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t songFrames() const noexcept { return songFrames_.load(std::memory_order_relaxed); }
    [[nodiscard]] float peak(int ch) const noexcept { return peak_[static_cast<std::size_t>(ch & 1)].exchange(0.0f, std::memory_order_relaxed); }
    [[nodiscard]] float outLufs() const noexcept { return outLufs_.load(std::memory_order_relaxed); }
    [[nodiscard]] float processedLufs() const noexcept { return wetLufs_.load(std::memory_order_relaxed); }
    [[nodiscard]] float originalLufs() const noexcept { return dryLufs_.load(std::memory_order_relaxed); }
    [[nodiscard]] float matchGainDb() const noexcept { return matchDb_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint32_t latency() const noexcept { return latency_.load(std::memory_order_relaxed); }

    // ---- hc::engine::AudioProcessor --------------------------------------------------------
    void prepare(const hc::io::StreamConfig& config) override;
    void process(const hc::engine::ProcessContext& ctx) noexcept override;

    // Runs the chain on a stereo block outside any stream (tests; the same code the audio thread runs).
    void render(float* left, float* right, std::uint32_t frames) noexcept;

private:
    struct Command {
        enum class Type : std::uint8_t { Song, Slot, Move };
        Type            type  = Type::Slot;
        int             a = 0, b = 0;
        const Song*     song  = nullptr;
        hc::fx::Effect* fx    = nullptr;
    };
    struct Retired {
        const Song*     song = nullptr;
        hc::fx::Effect* fx   = nullptr;
    };
    struct SlotRt {
        hc::fx::Effect*    fx = nullptr;
        rack::NoteSink*    notes = nullptr;        // the plug-in plays notes
        float              mix = 1.0f;            // 1 = plug-in, 0 = its delayed input
        std::vector<float> dl, dr;                // bypass delay (kMaxSlotDelay)
        std::uint32_t      pos = 0;
    };
    void applyCommands() noexcept;
    void applyMidi() noexcept;
    struct Midi { std::uint8_t status = 0, d1 = 0, d2 = 0; };
    hc::core::SpscRing<Midi, 1024> midi_;
    std::atomic_flag midiLock_ = ATOMIC_FLAG_INIT;   // between the MIDI inputs only (the audio thread never takes it)
    void renderChunk(float* l, float* r, std::uint32_t n) noexcept;
    void readSong(float* l, float* r, std::uint32_t n) noexcept;

    hc::core::SpscRing<Command, 256> commands_;
    hc::core::SpscRing<Retired, 256> retired_;

    // message-thread view
    std::array<std::unique_ptr<hc::fx::Effect>, kMaxSlots> owned_{};
    std::vector<std::unique_ptr<hc::fx::Effect>> leaving_;   // waiting for the audio thread to let go
    std::vector<std::shared_ptr<const Song>> songsLeaving_;
    std::shared_ptr<const Song> ownedSong_;
    std::array<std::atomic<bool>, kMaxSlots> on_{};

    // audio thread
    std::array<SlotRt, kMaxSlots> rt_{};
    const Song* song_ = nullptr;
    std::uint64_t pos_ = 0;
    std::int64_t pendingSeek_ = -1;
    float fade_ = 0.0f;                          // transport de-click gain
    float abMix_ = 0.0f;                         // 0 = A (processed), 1 = B (original)
    float matchGain_ = 1.0f;
    std::vector<float> dryL_, dryR_;             // chain-latency delay for B
    std::uint32_t dryPos_ = 0;
    std::vector<float> inL_, inR_, tmpL_, tmpR_, bL_, bR_, kL_, kR_, tmpMonoL_, tmpMonoR_;
    KMeter wetMeter_, dryMeter_, outMeter_;
    std::uint32_t meterTick_ = 0;

    std::atomic<bool> play_{false}, loop_{false}, original_{false}, match_{true};
    std::atomic<std::int64_t> seek_{-1};
    std::atomic<std::uint64_t> playhead_{0}, songFrames_{0};
    mutable std::array<std::atomic<float>, 2> peak_{};
    std::atomic<float> outLufs_{-144.0f}, wetLufs_{-144.0f}, dryLufs_{-144.0f}, matchDb_{0.0f};
    std::atomic<std::uint32_t> latency_{0};
};

} // namespace hem::app
