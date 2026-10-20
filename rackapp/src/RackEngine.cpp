#include "RackEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hem::app {

// ---- KMeter --------------------------------------------------------------------------------
void KMeter::prepare(double fs) {
    using T = hc::fx::BiquadCoefs::Type;
    shelf.set(hc::fx::BiquadCoefs::make(T::HighShelf, fs, 1500.0, 0.7071, 4.0));
    hp.set(hc::fx::BiquadCoefs::make(T::HighPass, fs, 38.0, 0.5));
    coef = 1.0 / (fs * 3.0);
    clear();
}

void KMeter::add(const float* l, const float* r, std::uint32_t n, float* sl, float* sr) noexcept {
    if (n == 0) return;
    std::memcpy(sl, l, n * sizeof(float));
    std::memcpy(sr, r, n * sizeof(float));
    shelf.run(sl, sr, n);
    hp.run(sl, sr, n);
    double s = 0.0;
    for (std::uint32_t i = 0; i < n; ++i) s += static_cast<double>(sl[i]) * sl[i] + static_cast<double>(sr[i]) * sr[i];
    const double blockMs = s / n;
    if (blockMs < 1e-8) return;   // silence (below about -80 LUFS) does not pull the reading down
    const double k = std::exp(-static_cast<double>(n) * coef);
    ms = ms <= 0.0 ? blockMs : ms * k + blockMs * (1.0 - k);
}

double KMeter::lufs() const noexcept { return ms > 0.0 ? -0.691 + 10.0 * std::log10(ms) : -144.0; }

// ---- RackProcessor: message thread ---------------------------------------------------------
RackProcessor::RackProcessor() {
    for (auto& s : rt_) { s.dl.assign(kMaxSlotDelay, 0.0f); s.dr.assign(kMaxSlotDelay, 0.0f); }
    for (auto& o : on_) o.store(true, std::memory_order_relaxed);
    dryL_.assign(kMaxChainDelay, 0.0f);
    dryR_.assign(kMaxChainDelay, 0.0f);
    for (auto* v : {&inL_, &inR_, &tmpL_, &tmpR_, &bL_, &bR_, &kL_, &kR_, &tmpMonoL_, &tmpMonoR_, &liveL_, &liveR_}) v->assign(kChunk, 0.0f);
    recWetRing_ = std::make_unique<hc::core::SpscRing<Frame, kRecRing>>();
    recDryRing_ = std::make_unique<hc::core::SpscRing<Frame, kRecRing>>();
    wetMeter_.prepare(kRate);
    dryMeter_.prepare(kRate);
    outMeter_.prepare(kRate);
}

RackProcessor::~RackProcessor() = default;   // the engine deletes us off the audio thread; owned_ frees the plug-ins

bool RackProcessor::submitSong(std::shared_ptr<const Song> song) {
    play_.store(false, std::memory_order_relaxed);
    seek_.store(-1, std::memory_order_relaxed);   // a seek meant for the old song
    Command c;
    c.type = Command::Type::Song;
    c.song = song.get();
    if (!commands_.tryPush(c)) return false;
    if (ownedSong_) songsLeaving_.push_back(std::move(ownedSong_));
    ownedSong_ = std::move(song);
    songFrames_.store(ownedSong_ ? ownedSong_->frames() : 0, std::memory_order_relaxed);
    playhead_.store(0, std::memory_order_relaxed);
    return true;
}

bool RackProcessor::setSlot(int slot, std::unique_ptr<hc::fx::Effect> effect) {
    if (slot < 0 || slot >= kMaxSlots) return false;
    Command c;
    c.type = Command::Type::Slot;
    c.a = slot;
    c.fx = effect.get();
    if (!commands_.tryPush(c)) return false;
    auto& o = owned_[static_cast<std::size_t>(slot)];
    if (o) leaving_.push_back(std::move(o));
    o = std::move(effect);
    on_[static_cast<std::size_t>(slot)].store(true, std::memory_order_relaxed);
    return true;
}

bool RackProcessor::moveSlot(int from, int to) {
    if (from < 0 || to < 0 || from >= kMaxSlots || to >= kMaxSlots || from == to) return false;
    Command c;
    c.type = Command::Type::Move;
    c.a = from;
    c.b = to;
    if (!commands_.tryPush(c)) return false;
    std::swap(owned_[static_cast<std::size_t>(from)], owned_[static_cast<std::size_t>(to)]);
    const bool a = on_[static_cast<std::size_t>(from)].load(std::memory_order_relaxed);
    on_[static_cast<std::size_t>(from)].store(on_[static_cast<std::size_t>(to)].load(std::memory_order_relaxed), std::memory_order_relaxed);
    on_[static_cast<std::size_t>(to)].store(a, std::memory_order_relaxed);
    return true;
}

void RackProcessor::setSlotOn(int slot, bool on) noexcept {
    if (slot >= 0 && slot < kMaxSlots) on_[static_cast<std::size_t>(slot)].store(on, std::memory_order_relaxed);
}
bool RackProcessor::slotOn(int slot) const noexcept {
    return slot >= 0 && slot < kMaxSlots && on_[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed);
}
hc::fx::Effect* RackProcessor::effect(int slot) const noexcept {
    return slot >= 0 && slot < kMaxSlots ? owned_[static_cast<std::size_t>(slot)].get() : nullptr;
}
int RackProcessor::usedSlots() const noexcept {
    int n = 0;
    for (const auto& o : owned_) n += o ? 1 : 0;
    return n;
}

void RackProcessor::collectRetired() noexcept {
    Retired r;
    while (retired_.tryPop(r)) {
        if (r.fx)
            std::erase_if(leaving_, [&](const std::unique_ptr<hc::fx::Effect>& e) { return e.get() == r.fx; });
        if (r.song)
            std::erase_if(songsLeaving_, [&](const std::shared_ptr<const Song>& s) { return s.get() == r.song; });
    }
}

// ---- audio thread -------------------------------------------------------------------------------
void RackProcessor::prepare(const hc::io::StreamConfig&) {
    // Everything is allocated in the constructor; plug-ins are prepared at 48 kHz when they are made.
    fade_ = 0.0f;
}

void RackProcessor::applyCommands() noexcept {
    Command c;
    while (commands_.tryPop(c)) {
        switch (c.type) {
        case Command::Type::Song:
            if (song_) (void)retired_.tryPush(Retired{song_, nullptr});
            song_ = c.song;
            pos_ = 0;
            fade_ = 0.0f;
            pendingSeek_ = -1;
            break;
        case Command::Type::Slot: {
            SlotRt& s = rt_[static_cast<std::size_t>(c.a)];
            if (s.fx) (void)retired_.tryPush(Retired{nullptr, s.fx});
            s.fx = c.fx;
            s.notes = dynamic_cast<rack::NoteSink*>(c.fx);
            std::fill(s.dl.begin(), s.dl.end(), 0.0f);
            std::fill(s.dr.begin(), s.dr.end(), 0.0f);
            s.pos = 0;
            s.mix = on_[static_cast<std::size_t>(c.a)].load(std::memory_order_relaxed) ? 1.0f : 0.0f;
            break;
        }
        case Command::Type::Move:
            std::swap(rt_[static_cast<std::size_t>(c.a)], rt_[static_cast<std::size_t>(c.b)]);
            break;
        }
    }
}

void RackProcessor::startRecording() noexcept {
    if (rec_.load(std::memory_order_relaxed)) return;
    Frame f;
    while (recWetRing_->tryPop(f)) {}   // what an earlier take left (the audio thread is not writing now)
    while (recDryRing_->tryPop(f)) {}
    recOverflow_.store(0, std::memory_order_relaxed);
    rec_.store(true, std::memory_order_release);
}

std::size_t RackProcessor::takeRecorded(float* wet, float* dry, std::size_t maxFrames) noexcept {
    std::size_t n = 0;
    Frame a, b;
    while (n < maxFrames && recWetRing_->tryPop(a)) {
        if (!recDryRing_->tryPop(b)) b = Frame{};   // pushed in pairs: never empty here
        wet[2 * n] = a.l;
        wet[2 * n + 1] = a.r;
        dry[2 * n] = b.l;
        dry[2 * n + 1] = b.r;
        ++n;
    }
    return n;
}

void RackProcessor::midi(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) noexcept {
    while (midiLock_.test_and_set(std::memory_order_acquire)) {}
    (void)midi_.tryPush(Midi{status, d1, d2});   // full: the message is dropped
    midiLock_.clear(std::memory_order_release);
}

void RackProcessor::applyMidi() noexcept {
    Midi m;
    while (midi_.tryPop(m)) {
        const int type = m.status & 0xF0;
        for (SlotRt& s : rt_) {
            if (!s.notes || !s.fx) continue;
            if (type == 0x90 && m.d2 > 0) s.notes->noteOn(m.d1 & 0x7F, static_cast<float>(m.d2 & 0x7F) / 127.0f, 0);
            else if (type == 0x80 || type == 0x90) s.notes->noteOff(m.d1 & 0x7F, 0);
            else if (type == 0xB0 && m.d1 == 64 && s.notes->sustainParam() >= 0) s.fx->set(s.notes->sustainParam(), m.d2 >= 64 ? 1.0f : 0.0f);
            else if (type == 0xB0 && (m.d1 == 120 || m.d1 == 123)) s.notes->allNotesOff();
        }
    }
}

void RackProcessor::readSong(float* l, float* r, std::uint32_t n) noexcept {
    const std::int64_t want = seek_.exchange(-1, std::memory_order_relaxed);
    if (want >= 0) pendingSeek_ = want;
    const bool play = play_.load(std::memory_order_relaxed);
    const bool loop = loop_.load(std::memory_order_relaxed);
    const std::uint64_t frames = song_ ? song_->frames() : 0;
    constexpr float kStep = 1.0f / 240.0f;   // 5 ms de-click
    for (std::uint32_t i = 0; i < n; ++i) {
        const float target = play && frames > 0 && pendingSeek_ < 0 ? 1.0f : 0.0f;
        fade_ = target > fade_ ? std::min(target, fade_ + kStep) : std::max(target, fade_ - kStep);
        if (pendingSeek_ >= 0 && fade_ <= 0.0f) {
            pos_ = std::min<std::uint64_t>(static_cast<std::uint64_t>(pendingSeek_), frames);
            pendingSeek_ = -1;
        }
        if (fade_ > 0.0f && pos_ < frames) {
            l[i] = song_->left[pos_] * fade_;
            r[i] = song_->right[pos_] * fade_;
            ++pos_;
            if (pos_ >= frames) {
                if (loop) pos_ = 0;
                else play_.store(false, std::memory_order_relaxed);
            }
        } else {
            l[i] = 0.0f;
            r[i] = 0.0f;
        }
    }
    playhead_.store(pos_, std::memory_order_relaxed);
}

void RackProcessor::renderChunk(float* l, float* r, std::uint32_t n) noexcept {
    readSong(inL_.data(), inR_.data(), n);
    // LIVE INPUT: the chain gets the sound card's input; the song stays out of it
    const int first = inFirst_.load(std::memory_order_relaxed);
    const bool live = first >= 0 && hwIn_ && static_cast<std::uint32_t>(first) < hwInCount_;
    if (live) {
        const float* a = hwIn_[first] + hwInOffset_;
        const float* b = inStereo_.load(std::memory_order_relaxed) && static_cast<std::uint32_t>(first) + 1 < hwInCount_ ? hwIn_[first + 1] + hwInOffset_ : a;
        float pk = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            liveL_[i] = l[i] = a[i];
            liveR_[i] = r[i] = b[i];
            pk = std::max({pk, std::fabs(a[i]), std::fabs(b[i])});
        }
        if (pk > inPeak_.load(std::memory_order_relaxed)) inPeak_.store(pk, std::memory_order_relaxed);
    } else {
        std::memcpy(l, inL_.data(), n * sizeof(float));
        std::memcpy(r, inR_.data(), n * sizeof(float));
    }

    // The chain.
    std::uint32_t total = 0;
    for (std::size_t si = 0; si < rt_.size(); ++si) {
        SlotRt& s = rt_[si];
        if (!s.fx) continue;
        const std::uint32_t lat = std::min(s.fx->latency(), kMaxSlotDelay - 1);
        total += lat;
        std::memcpy(tmpL_.data(), l, n * sizeof(float));
        std::memcpy(tmpR_.data(), r, n * sizeof(float));
        s.fx->process(l, r, n);
        // the plug-in's input, delayed by its latency (what BYPASS plays)
        for (std::uint32_t i = 0; i < n; ++i) {
            s.dl[s.pos] = tmpL_[i];
            s.dr[s.pos] = tmpR_[i];
            const std::uint32_t rd = (s.pos + kMaxSlotDelay - lat) % kMaxSlotDelay;
            tmpL_[i] = s.dl[rd];
            tmpR_[i] = s.dr[rd];
            s.pos = (s.pos + 1) % kMaxSlotDelay;
        }
        const float target = on_[si].load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        if (s.mix == target && target == 1.0f) continue;
        constexpr float kStep = 1.0f / 480.0f;   // 10 ms
        for (std::uint32_t i = 0; i < n; ++i) {
            s.mix = target > s.mix ? std::min(target, s.mix + kStep) : std::max(target, s.mix - kStep);
            l[i] = tmpL_[i] + s.mix * (l[i] - tmpL_[i]);
            r[i] = tmpR_[i] + s.mix * (r[i] - tmpR_[i]);
        }
    }
    latency_.store(total, std::memory_order_relaxed);

    if (live) {
        // the takes: out of the chain, and what went in
        if (rec_.load(std::memory_order_acquire)) {
            for (std::uint32_t i = 0; i < n; ++i) {
                if (!recWetRing_->tryPush(Frame{l[i], r[i]})) { recOverflow_.fetch_add(1, std::memory_order_relaxed); break; }
                if (!recDryRing_->tryPush(Frame{liveL_[i], liveR_[i]})) { recOverflow_.fetch_add(1, std::memory_order_relaxed); break; }
            }
        }
        // what you hear: the processed input (MONITOR) over the song, as it is
        const bool mon = monitor_.load(std::memory_order_relaxed);
        float pk0 = 0.0f, pk1 = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            l[i] = (mon ? l[i] : 0.0f) + inL_[i];
            r[i] = (mon ? r[i] : 0.0f) + inR_[i];
            pk0 = std::max(pk0, std::fabs(l[i]));
            pk1 = std::max(pk1, std::fabs(r[i]));
        }
        outMeter_.add(l, r, n, kL_.data(), kR_.data());
        if (pk0 > peak_[0].load(std::memory_order_relaxed)) peak_[0].store(pk0, std::memory_order_relaxed);
        if (pk1 > peak_[1].load(std::memory_order_relaxed)) peak_[1].store(pk1, std::memory_order_relaxed);
        outLufs_.store(static_cast<float>(outMeter_.lufs()), std::memory_order_relaxed);
        return;
    }

    // B: the untouched song, delayed by the chain's latency so it lines up with A.
    for (std::uint32_t i = 0; i < n; ++i) {
        dryL_[dryPos_] = inL_[i];
        dryR_[dryPos_] = inR_[i];
        const std::uint32_t rd = (dryPos_ + kMaxChainDelay - total) % kMaxChainDelay;
        bL_[i] = dryL_[rd];
        bR_[i] = dryR_[rd];
        dryPos_ = (dryPos_ + 1) % kMaxChainDelay;
    }

    wetMeter_.add(l, r, n, kL_.data(), kR_.data());
    dryMeter_.add(bL_.data(), bR_.data(), n, kL_.data(), kR_.data());
    const double wet = wetMeter_.lufs(), dry = dryMeter_.lufs();
    double targetDb = 0.0;
    if (match_.load(std::memory_order_relaxed) && wet > -100.0 && dry > -100.0) targetDb = std::clamp(wet - dry, -18.0, 18.0);
    const float targetGain = static_cast<float>(std::pow(10.0, targetDb / 20.0));
    const float g0 = matchGain_;
    matchGain_ += (targetGain - matchGain_) * static_cast<float>(1.0 - std::exp(-static_cast<double>(n) / (kRate * 0.4)));

    // A / B with a 20 ms crossfade.
    const float abTarget = original_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    constexpr float kAb = 1.0f / 960.0f;
    float pk0 = 0.0f, pk1 = 0.0f;
    for (std::uint32_t i = 0; i < n; ++i) {
        abMix_ = abTarget > abMix_ ? std::min(abTarget, abMix_ + kAb) : std::max(abTarget, abMix_ - kAb);
        const float g = g0 + (matchGain_ - g0) * static_cast<float>(i + 1) / static_cast<float>(n);
        if (abMix_ > 0.0f) {
            l[i] += abMix_ * (bL_[i] * g - l[i]);
            r[i] += abMix_ * (bR_[i] * g - r[i]);
        }
        pk0 = std::max(pk0, std::fabs(l[i]));
        pk1 = std::max(pk1, std::fabs(r[i]));
    }
    outMeter_.add(l, r, n, kL_.data(), kR_.data());
    if (pk0 > peak_[0].load(std::memory_order_relaxed)) peak_[0].store(pk0, std::memory_order_relaxed);
    if (pk1 > peak_[1].load(std::memory_order_relaxed)) peak_[1].store(pk1, std::memory_order_relaxed);
    outLufs_.store(static_cast<float>(outMeter_.lufs()), std::memory_order_relaxed);
    wetLufs_.store(static_cast<float>(wet), std::memory_order_relaxed);
    dryLufs_.store(static_cast<float>(dry), std::memory_order_relaxed);
    matchDb_.store(20.0f * std::log10(std::max(matchGain_, 1e-6f)), std::memory_order_relaxed);
}

void RackProcessor::render(float* left, float* right, std::uint32_t frames) noexcept {
    applyCommands();
    applyMidi();
    for (std::uint32_t done = 0; done < frames;) {
        const std::uint32_t n = std::min(kChunk, frames - done);
        renderChunk(left + done, right + done, n);
        done += n;
    }
}

void RackProcessor::process(const hc::engine::ProcessContext& ctx) noexcept {
    if (ctx.numOutputs == 0 || ctx.frames == 0) { applyCommands(); return; }
    float* l = ctx.outputs[0];
    float* r = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
    applyCommands();
    applyMidi();
    hwIn_ = ctx.inputs;
    hwInCount_ = ctx.inputs ? ctx.numInputs : 0;
    for (std::uint32_t done = 0; done < ctx.frames;) {
        const std::uint32_t n = std::min(kChunk, ctx.frames - done);
        hwInOffset_ = done;
        if (r) {
            renderChunk(l + done, r + done, n);
        } else {   // a mono output: both sides summed
            renderChunk(tmpMonoL_.data(), tmpMonoR_.data(), n);
            for (std::uint32_t i = 0; i < n; ++i) l[done + i] = 0.5f * (tmpMonoL_[i] + tmpMonoR_[i]);
        }
        done += n;
    }
    hwIn_ = nullptr;
    hwInCount_ = 0;
}

void RackProcessor::renderWithInputs(const float* const* inputs, std::uint32_t numInputs, float* left, float* right, std::uint32_t frames) noexcept {
    applyCommands();
    applyMidi();
    hwIn_ = inputs;
    hwInCount_ = inputs ? numInputs : 0;
    for (std::uint32_t done = 0; done < frames;) {
        const std::uint32_t n = std::min(kChunk, frames - done);
        hwInOffset_ = done;
        renderChunk(left + done, right + done, n);
        done += n;
    }
    hwIn_ = nullptr;
    hwInCount_ = 0;
}

} // namespace hem::app
