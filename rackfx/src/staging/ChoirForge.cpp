// =============================================================================
//  12 CHOIRFORGE — eight harmony voices from one vocal. Each voice: its own interval (SEMITONES),
//  scale steps (SCALE) or chord tone (CHORD) — or a FIXED NOTE — plus fine tune, formant, delay
//  with feedback, a filter, pan and level. The sung note is read once; every voice is its own
//  grain stream placed at its own period, so voices stay in tune with each other and the key.
//  SOURCE CORRECTION can tune the lead itself (OFF / NATURAL / HARD / NOTES).
//  Modulation: four LFOs, two envelopes, LOUD (level follower) and TRACK (pitch) into four slots
//  (source -> PITCH / FORMANT / PAN / LEVEL / DELAY of every voice, with depth).
//  Different from THROATSHIFT (07, bends one voice) and NOTEFORGE (05, tunes one voice).
//  Dead flat on load: every voice OFF, SOURCE 0 dB, correction OFF, output 0 dB — the lead comes out
//  unchanged, delayed by the reported latency (3 periods of 65 Hz + 16 samples).
//  Snapshots, mapping edit and scale edit are faceplate screens over these same controls.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

constexpr int kVoices = 8, kVPer = 15, kSlots = 4, kLfos = 4;
enum V { vOn, vSolo, vPitch, vFine, vFixed, vFixedNote, vFormant, vDelay, vSync, vFeedback, vFiltType, vCutoff, vPan, vLevel, vGlide };
enum G {
    kKey = kVoices * kVPer, kScale, kMode, kChord, kLock, kTimeTol, kCentsTol, kGlide, kFmtComp, kGPitch, kGFormant, kSpread,
    kSource, kHarmony, kOutput, kCorrection, kLfo0, kEnvA = kLfo0 + kLfos, kEnvR, kSlot0
};
enum T { kBypass = kSlot0 + kSlots * 3, kCount };
const char* const kKeys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kScales[] = {"CHROMATIC", "MAJOR", "MINOR", "HARMONIC MINOR", "DORIAN"};
const char* const kModes[] = {"SEMITONES", "SCALE", "CHORD"};
const char* const kChords[] = {"MAJOR", "MINOR", "SUS4", "SEVENTH"};
const char* const kFilt[] = {"LP", "HP"};
const char* const kCorr[] = {"OFF", "NATURAL", "HARD", "NOTES"};
const char* const kSrcN[] = {"LFO 1", "LFO 2", "LFO 3", "LFO 4", "ENV 1", "ENV 2", "LOUD", "TRACK"};
const char* const kTgtN[] = {"PITCH", "FORMANT", "PAN", "LEVEL", "DELAY"};
const char* const kNotes[] = {"C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
                              "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
                              "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
                              "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5", "C6"};
constexpr std::array<std::uint16_t, 5> kScaleMask{0xFFF,
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10)};
constexpr std::array<std::array<int, 4>, 4> kChordTones{{{0, 4, 7, 12}, {0, 3, 7, 12}, {0, 5, 7, 12}, {0, 4, 7, 10}}};
constexpr std::array<float, kVoices> kDefPan{-40, 40, -70, 70, -20, 20, -90, 90};

#define HEM_VOICE(n) \
    {"v" #n "_on", "VOICE " #n " ON", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {"v" #n "_solo", "VOICE " #n " SOLO", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {"v" #n "_pitch", "VOICE " #n " PITCH", "st", -24.0f, 24.0f, 0.0f}, \
    {"v" #n "_fine", "VOICE " #n " FINE", "cents", -50.0f, 50.0f, 0.0f}, \
    {"v" #n "_fixed", "VOICE " #n " FIXED NOTE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {"v" #n "_note", "VOICE " #n " NOTE", "", 0.0f, 48.0f, 24.0f, Taper::Choice, kNotes}, \
    {"v" #n "_formant", "VOICE " #n " FORMANT", "st", -12.0f, 12.0f, 0.0f}, \
    {"v" #n "_delay", "VOICE " #n " DELAY", "ms", 0.0f, 500.0f, 0.0f}, \
    {"v" #n "_sync", "VOICE " #n " DELAY SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {"v" #n "_feedback", "VOICE " #n " FEEDBACK", "%", 0.0f, 90.0f, 0.0f}, \
    {"v" #n "_filter", "VOICE " #n " FILTER", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kFilt}, \
    {"v" #n "_cutoff", "VOICE " #n " CUTOFF", "Hz", 20.0f, 20000.0f, 20000.0f, Taper::Log}, \
    {"v" #n "_pan", "VOICE " #n " PAN", "", -100.0f, 100.0f, kDefPan[n - 1]}, \
    {"v" #n "_level", "VOICE " #n " LEVEL", "dB", -60.0f, 6.0f, 0.0f}, \
    {"v" #n "_glide", "VOICE " #n " GLIDE SCALE", "%", 0.0f, 200.0f, 100.0f}
#define HEM_SLOT(n) \
    {"slot" #n "_source", "MOD " #n " SOURCE", "", 0.0f, 7.0f, 0.0f, Taper::Choice, kSrcN}, \
    {"slot" #n "_target", "MOD " #n " TARGET", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kTgtN}, \
    {"slot" #n "_depth", "MOD " #n " DEPTH", "%", -100.0f, 100.0f, 0.0f}

const ParamInfo kParams[kCount] = {
    HEM_VOICE(1), HEM_VOICE(2), HEM_VOICE(3), HEM_VOICE(4), HEM_VOICE(5), HEM_VOICE(6), HEM_VOICE(7), HEM_VOICE(8),
    {"key", "KEY", "", 0.0f, 11.0f, 0.0f, Taper::Choice, kKeys},
    {"scale", "SCALE", "", 0.0f, 4.0f, 1.0f, Taper::Choice, kScales},
    {"harmony_mode", "HARMONY MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModes},
    {"chord", "CHORD", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kChords},
    {"lock", "LOCK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"time_tolerance", "TIME TOLERANCE", "ms", 0.0f, 100.0f, 25.0f},
    {"cents_tolerance", "CENTS TOLERANCE", "cents", 0.0f, 50.0f, 15.0f},
    {"glide", "GLIDE", "ms", 0.0f, 500.0f, 80.0f},
    {"formant_comp", "FORMANT COMP", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"global_pitch", "GLOBAL PITCH", "st", -12.0f, 12.0f, 0.0f},
    {"global_formant", "GLOBAL FORMANT", "st", -12.0f, 12.0f, 0.0f},
    {"spread", "SPREAD", "%", 0.0f, 100.0f, 100.0f},
    {"source_level", "SOURCE", "dB", -60.0f, 6.0f, 0.0f},
    {"harmony_level", "HARMONY", "dB", -60.0f, 6.0f, 0.0f},
    {"output", "OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
    {"correction", "SOURCE CORRECTION", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kCorr},
    {"lfo1_rate", "LFO 1 RATE", "Hz", 0.05f, 10.0f, 0.3f, Taper::Log}, {"lfo2_rate", "LFO 2 RATE", "Hz", 0.05f, 10.0f, 1.0f, Taper::Log},
    {"lfo3_rate", "LFO 3 RATE", "Hz", 0.05f, 10.0f, 5.0f, Taper::Log}, {"lfo4_rate", "LFO 4 RATE", "Hz", 0.05f, 10.0f, 0.1f, Taper::Log},
    {"env_attack", "ENV ATTACK", "ms", 1.0f, 500.0f, 10.0f, Taper::Log},
    {"env_release", "ENV RELEASE", "ms", 10.0f, 2000.0f, 200.0f, Taper::Log},
    HEM_SLOT(1), HEM_SLOT(2), HEM_SLOT(3), HEM_SLOT(4),
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_VOICE
#undef HEM_SLOT

const hc::fx::MeterInfo kMeters[] = {
    {"SOURCE", "dBFS", -60, 0}, {"HARMONY", "dBFS", -60, 0}, {"OUTPUT", "dBFS", -60, 0}, {"SUNG NOTE", "Hz", 0, 1000}, {"VOICES ON", "", 0, 8},
};

constexpr std::uint32_t kRing = 16384;
constexpr float kLowHz = 65.0f;
constexpr std::size_t kCtl = 512;

class ChoirForge final : public hc::fx::Effect {
public:
    ChoirForge() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_; }
    bool paramActive(int i) const noexcept override {
        if (i < kKey) { const int k = i % kVPer, o = i - k; if (k == vOn) return true; if (p(o + vOn) < 0.5f) return false; if (k == vFixedNote) return p(o + vFixed) > 0.5f; return true; }
        if (i == kChord) return static_cast<int>(p(kMode)) == 2;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        det_.prepare(fs, kLowHz, 1100.0f, 256);
        maxT_ = static_cast<std::uint32_t>(std::ceil(fs / kLowHz));
        lat_ = 3 * maxT_ + 16;
        in_.assign(kRing, 0.0f); inR_.assign(kRing, 0.0f);
        for (auto& v : voice_) { v.acc.assign(kRing, 0.0f); v.nrm.assign(kRing, 0.0f); v.dly.prepare(static_cast<std::uint32_t>(fs * 0.6)); }
        src_.acc.assign(kRing, 0.0f); src_.nrm.assign(kRing, 0.0f);
        srcG_.prepare(fs, 20.0, kit::dbToGain(p(kSource))); harmG_.prepare(fs, 20.0, kit::dbToGain(p(kHarmony))); outG_.prepare(fs, 20.0, kit::dbToGain(p(kOutput)));
        srcM_.prepare(fs); harmM_.prepare(fs); outM_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        det_.reset();
        std::fill(in_.begin(), in_.end(), 0.0f); std::fill(inR_.begin(), inR_.end(), 0.0f);
        for (auto& v : voice_) { std::fill(v.acc.begin(), v.acc.end(), 0.0f); std::fill(v.nrm.begin(), v.nrm.end(), 0.0f); v.nextSyn = -1; v.ana = 0; v.note = -1; v.dly.clear(); v.filt.clear(); v.lvl = 0; v.fb = 0; }
        std::fill(src_.acc.begin(), src_.acc.end(), 0.0f); std::fill(src_.nrm.begin(), src_.nrm.end(), 0.0f); src_.nextSyn = -1; src_.note = -1;
        t_ = 0; sung_ = -1; pending_ = -1; pendFor_ = 0; locked_ = -1; ctl_.fill({}); lfoPh_.fill(0.0); env1_ = env2_ = 0; srcWet_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        bool anySolo = false; int on = 0;
        for (int v = 0; v < kVoices; ++v) { anySolo |= p(v * kVPer + vOn) > 0.5f && p(v * kVPer + vSolo) > 0.5f; on += p(v * kVPer + vOn) > 0.5f ? 1 : 0; }
        const float fadeC = hc::fx::onePoleCoef(fs_, 20.0);
        const bool correcting = static_cast<int>(p(kCorrection)) > 0;
        const float e1a = hc::fx::onePoleCoef(fs_, p(kEnvA)), e1r = hc::fx::onePoleCoef(fs_, p(kEnvR));
        const float e2a = hc::fx::onePoleCoef(fs_, 50.0), e2r = hc::fx::onePoleCoef(fs_, 600.0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::int64_t now = t_++;
            in_[static_cast<std::size_t>(now) & (kRing - 1)] = L[i];
            inR_[static_cast<std::size_t>(now) & (kRing - 1)] = R[i];
            const float mono = 0.5f * (L[i] + R[i]);
            det_.push(mono);
            const float am = std::fabs(mono);
            env1_ = am + (am > env1_ ? e1a : e1r) * (env1_ - am);
            env2_ = am + (am > env2_ ? e2a : e2r) * (env2_ - am);
            if ((now & 31) == 0) { control(); ctl_[static_cast<std::size_t>(now >> 5) & (kCtl - 1)] = cur_; }
            const std::int64_t d = now - static_cast<std::int64_t>(lat_);
            const std::int64_t ci = (now - static_cast<std::int64_t>(lat_ - maxT_)) >> 5;
            const Ctl& c = ci >= 0 ? ctl_[static_cast<std::size_t>(ci) & (kCtl - 1)] : ctl_[0];
            float dl = d >= 0 ? in_[static_cast<std::size_t>(d) & (kRing - 1)] : 0.0f, dr = d >= 0 ? inR_[static_cast<std::size_t>(d) & (kRing - 1)] : 0.0f;
            // the lead: corrected (crossfaded in) or as sung
            const float swT = correcting && c.voiced ? 1.0f : 0.0f;
            srcWet_ = swT + fadeC * (srcWet_ - swT);
            if (d >= 0 && srcWet_ > 1e-6f) {
                const float y = grain(src_, d, c.T, c.voiced, c.srcRatio, 1.0f);
                const float mid = 0.5f * (dl + dr);
                dl += srcWet_ * (y - mid); dr += srcWet_ * (y - mid);
            } else src_.nextSyn = -1;
            const float sg = srcG_.next(kit::dbToGain(p(kSource) <= -59.5f ? -200.0f : p(kSource)));
            float outL = anySolo ? 0.0f : dl * sg, outR = anySolo ? 0.0f : dr * sg;
            const float srcAbs = std::fabs(dl * sg);
            // the harmony voices
            float hl = 0, hr = 0;
            if (on > 0 && d >= 0) {
                for (int v = 0; v < kVoices; ++v) {
                    const int o = v * kVPer;
                    Voice& vc = voice_[static_cast<std::size_t>(v)];
                    const float lvT = p(o + vOn) > 0.5f && (!anySolo || p(o + vSolo) > 0.5f) && c.voiced ? kit::dbToGain(p(o + vLevel) <= -59.5f ? -200.0f : p(o + vLevel)) * c.modLevel : 0.0f;
                    vc.lvl = lvT + fadeC * (vc.lvl - lvT);
                    if (vc.lvl < 1e-6f) { vc.nextSyn = -1; continue; }
                    float y = grain(vc, d, c.T, c.voiced, c.ratio[static_cast<std::size_t>(v)], c.fmt[static_cast<std::size_t>(v)]);
                    // delay with feedback
                    float dMs = p(o + vDelay) + c.modDelay;
                    if (p(o + vSync) > 0.5f) { const float six = 15000.0f / std::max(tempo(), 20.0f); dMs = std::round(dMs / six) * six; }
                    if (dMs > 0.5f) {
                        const float ds = std::clamp(static_cast<float>(fs_) * dMs * 0.001f, 1.0f, static_cast<float>(fs_ * 0.55));
                        const float back = vc.dly.read(ds);
                        vc.dly.write(y + p(o + vFeedback) * 0.01f * back);
                        y = back;
                    }
                    // filter (LP at 20 kHz / HP at 20 Hz = open)
                    const bool hp = p(o + vFiltType) > 0.5f;
                    if ((hp && p(o + vCutoff) > 20.5f) || (!hp && p(o + vCutoff) < 19900.0f)) {
                        vc.filt.set(fs_, p(o + vCutoff), 0.7071);
                        y = hp ? vc.filt.high(y) : vc.filt.low(y);
                    }
                    const float pan = std::clamp(p(o + vPan) * p(kSpread) * 0.01f + c.modPan * (v % 2 ? 1.0f : -1.0f), -100.0f, 100.0f) * 0.01f;
                    const float a = (pan + 1.0f) * 0.25f * kit::kPi;
                    hl += y * vc.lvl * std::cos(a) * 1.41421356f; hr += y * vc.lvl * std::sin(a) * 1.41421356f;
                }
            }
            const float hg = harmG_.next(kit::dbToGain(p(kHarmony) <= -59.5f ? -200.0f : p(kHarmony)));
            outL += hl * hg; outR += hr * hg;
            const float og = outG_.next(kit::dbToGain(p(kOutput)));
            L[i] = kit::sane(outL * og); R[i] = kit::sane(outR * og);
            srcPk_ = std::max(srcPk_ * 0.9999f, srcAbs); harmPk_ = std::max(harmPk_ * 0.9999f, std::fabs(hl * hg));
        }
        outM_.feed(L, n);
        publishMeter(0, kit::gainToDb(srcPk_)); publishMeter(1, kit::gainToDb(harmPk_)); publishMeter(2, outM_.db());
        publishMeter(3, sung_ > 0.0f ? kit::midiToHz(sung_) : 0.0f); publishMeter(4, static_cast<float>(on));
    }

private:
    struct Ctl { float T = 480; bool voiced = false; float srcRatio = 1; std::array<float, kVoices> ratio{}, fmt{}; float modLevel = 1, modPan = 0, modDelay = 0; };
    struct Voice {
        std::vector<float> acc, nrm;
        double nextSyn = -1, ana = 0;
        float note = -1, lvl = 0, fb = 0;
        hc::fx::DelayLine dly;
        kit::Svf filt;
    };

    [[nodiscard]] bool inScale(int m) const noexcept {
        const int rel = (((m - static_cast<int>(p(kKey))) % 12) + 12) % 12;
        return (kScaleMask[static_cast<std::size_t>(p(kScale))] >> rel) & 1;
    }
    [[nodiscard]] int snap(float m) const noexcept {
        const int near = static_cast<int>(std::lround(m));
        for (int d = 0; d <= 6; ++d) { if (inScale(near + d)) return near + d; if (inScale(near - d)) return near - d; }
        return near;
    }
    [[nodiscard]] int scaleStep(int from, int steps) const noexcept {
        int m = from;
        for (int s = 0; s < std::abs(steps);) { m += steps > 0 ? 1 : -1; if (inScale(m)) ++s; }
        return m;
    }
    // the chord's tones around the sung note: step k (0 = the chord tone nearest the note)
    [[nodiscard]] float chordStep(float sung, int steps) const noexcept {
        const auto& ct = kChordTones[static_cast<std::size_t>(p(kChord))];
        const int root = static_cast<int>(p(kKey));
        std::array<int, 40> tones{}; int n = 0;
        for (int oct = 1; oct <= 8 && n < 36; ++oct) for (int k = 0; k < 3; ++k) tones[static_cast<std::size_t>(n++)] = 12 * oct + root + ct[static_cast<std::size_t>(k)];
        int best = 0; float bd = 1e9f;
        for (int k = 0; k < n; ++k) if (std::fabs(static_cast<float>(tones[static_cast<std::size_t>(k)]) - sung) < bd) { bd = std::fabs(static_cast<float>(tones[static_cast<std::size_t>(k)]) - sung); best = k; }
        return static_cast<float>(tones[static_cast<std::size_t>(std::clamp(best + steps, 0, n - 1))]);
    }

    float modSource(int s) noexcept {
        switch (s) {
        case 0: case 1: case 2: case 3: return std::sin(kit::kTau * static_cast<float>(lfoPh_[static_cast<std::size_t>(s)]));
        case 4: return std::min(1.0f, env1_ * 4.0f);
        case 5: return std::min(1.0f, env2_ * 4.0f);
        case 6: return std::clamp((kit::gainToDb(env1_) + 40.0f) / 40.0f, 0.0f, 1.0f);
        default: return sung_ > 0.0f ? std::clamp((sung_ - 60.0f) / 24.0f, -1.0f, 1.0f) : 0.0f;
        }
    }

    void control() noexcept {
        for (int l = 0; l < kLfos; ++l) { auto& ph = lfoPh_[static_cast<std::size_t>(l)]; ph += 32.0 * p(kLfo0 + l) / fs_; ph -= std::floor(ph); }
        const float hz = det_.hz();
        cur_.voiced = hz > 0.0f;
        if (!cur_.voiced) return;
        cur_.T = static_cast<float>(fs_ / hz);
        const float m = kit::hzToMidi(hz);
        // the sung note, with cents and time tolerance
        if (sung_ < 0.0f || std::fabs(m - sung_) * 100.0f > p(kCentsTol)) {
            const int cand = static_cast<int>(std::lround(m * 100.0f));
            if (cand / 100 != pending_ / 100 || pending_ < 0) { pending_ = cand; pendFor_ = 0; }
            pendFor_ += 32;
            if (sung_ < 0.0f || static_cast<float>(pendFor_) >= p(kTimeTol) * 0.001f * static_cast<float>(fs_)) sung_ = m;
        }
        if (p(kLock) > 0.5f) { if (locked_ < 0.0f) locked_ = sung_; } else locked_ = -1.0f;
        const float base = locked_ >= 0.0f ? locked_ : sung_;
        // modulation slots
        float mp = 0, mf = 0, mpan = 0, mlvl = 0, mdly = 0;
        for (int s = 0; s < kSlots; ++s) {
            const float dep = p(kSlot0 + 3 * s + 2) * 0.01f;
            if (dep == 0.0f) continue;
            const float v = modSource(static_cast<int>(p(kSlot0 + 3 * s))) * dep;
            switch (static_cast<int>(p(kSlot0 + 3 * s + 1))) {
            case 0: mp += v * 1.0f; break;                 // +-1 semitone at 100 %
            case 1: mf += v * 6.0f; break;                 // +-6 semitones of formant
            case 2: mpan += v * 50.0f; break;              // +-50 pan, opposite on odd / even voices
            case 3: mlvl += v * 12.0f; break;              // +-12 dB
            default: mdly += v * 50.0f; break;             // +-50 ms
            }
        }
        cur_.modLevel = kit::dbToGain(mlvl); cur_.modPan = mpan; cur_.modDelay = mdly;
        const float glideC = std::exp(-32.0f / std::max(1.0f, static_cast<float>(fs_) * p(kGlide) * 0.001f));
        const bool comp = p(kFmtComp) > 0.5f;
        for (int v = 0; v < kVoices; ++v) {
            const int o = v * kVPer;
            Voice& vc = voice_[static_cast<std::size_t>(v)];
            float target;
            if (p(o + vFixed) > 0.5f) target = 36.0f + p(o + vFixedNote);
            else switch (static_cast<int>(p(kMode))) {
                case 1: target = static_cast<float>(scaleStep(snap(base), static_cast<int>(std::lround(p(o + vPitch))))); break;
                case 2: target = chordStep(base, static_cast<int>(std::lround(p(o + vPitch)))); break;
                default: target = base + p(o + vPitch); break;
            }
            target += p(o + vFine) * 0.01f + p(kGPitch) + mp;
            const float gc = std::pow(glideC, p(o + vGlide) * 0.01f);
            vc.note = vc.note < 0.0f ? target : target + gc * (vc.note - target);
            const float ratio = std::exp2((vc.note - m) / 12.0f);
            cur_.ratio[static_cast<std::size_t>(v)] = ratio;
            // formant: kept (COMP on) or moving with the pitch, plus its own and the global formant
            const float fst = p(o + vFormant) + p(kGFormant) + mf + (comp ? 0.0f : (vc.note - m));
            cur_.fmt[static_cast<std::size_t>(v)] = std::exp2(std::clamp(fst, -12.0f, 12.0f) / 12.0f);
        }
        // source correction: NATURAL halfway / 120 ms, HARD all the way at once, NOTES all the way with glide
        const int corr = static_cast<int>(p(kCorrection));
        if (corr > 0) {
            const float tgt = static_cast<float>(snap(m));
            const float amount = corr == 1 ? 0.5f : 1.0f;
            const float want = m + (tgt - m) * amount;
            const float sc = corr == 2 ? 0.0f : std::exp(-32.0f / (static_cast<float>(fs_) * (corr == 1 ? 0.12f : std::max(0.001f, p(kGlide) * 0.001f))));
            src_.note = src_.note < 0.0f ? want : want + sc * (src_.note - want);
            cur_.srcRatio = std::exp2((src_.note - m) / 12.0f);
        } else cur_.srcRatio = 1.0f;
    }

    // one voice's grain stream: grains two periods long, laid one new period apart, contents read at the formant rate
    float grain(Voice& v, std::int64_t d, float T0, bool voiced, float ratio, float fr) noexcept {
        const float T = voiced ? T0 : 480.0f;
        const float rr = voiced ? ratio : 1.0f;
        if (v.nextSyn < 0.0) { v.nextSyn = static_cast<double>(d); v.ana = static_cast<double>(d); }
        const int g = static_cast<int>(std::min(T, static_cast<float>(maxT_)));
        while (v.nextSyn - g <= static_cast<double>(d)) {
            while (v.ana + T <= v.nextSyn) v.ana += T;
            while (v.ana > v.nextSyn) v.ana -= T;
            const auto s = static_cast<std::int64_t>(std::llround(v.nextSyn));
            for (int k = -g; k <= g; ++k) {
                const float w = 0.5f + 0.5f * std::cos(kit::kPi * static_cast<float>(k) / static_cast<float>(g));
                const double src = v.ana + static_cast<double>(k) * fr;
                const auto i0 = static_cast<std::int64_t>(std::floor(src));
                const float f = static_cast<float>(src - static_cast<double>(i0));
                const auto a = static_cast<std::size_t>(i0) & (kRing - 1), b = static_cast<std::size_t>(i0 + 1) & (kRing - 1);
                const float x = 0.5f * (in_[a] + inR_[a]) + f * (0.5f * (in_[b] + inR_[b]) - 0.5f * (in_[a] + inR_[a]));
                const auto dst = static_cast<std::size_t>(s + k) & (kRing - 1);
                v.acc[dst] += w * x; v.nrm[dst] += w;
            }
            v.nextSyn += static_cast<double>(T / rr);
        }
        const auto at = static_cast<std::size_t>(d) & (kRing - 1);
        const float y = v.acc[at] / std::max(v.nrm[at], 0.25f);
        v.acc[at] = 0.0f; v.nrm[at] = 0.0f;
        return y;
    }

    double fs_ = 48000;
    kit::PitchDetector det_;
    std::uint32_t maxT_ = 739, lat_ = 2233;
    std::vector<float> in_, inR_;
    std::array<Voice, kVoices> voice_{};
    Voice src_{};
    std::int64_t t_ = 0;
    float sung_ = -1, locked_ = -1, env1_ = 0, env2_ = 0, srcWet_ = 0, srcPk_ = 0, harmPk_ = 0;
    int pending_ = -1; std::uint32_t pendFor_ = 0;
    std::array<double, kLfos> lfoPh_{};
    Ctl cur_{};
    std::array<Ctl, kCtl> ctl_{};
    hc::fx::Smoothed srcG_, harmG_, outG_;
    kit::PeakMeter srcM_, harmM_, outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeChoirForge() { return std::make_unique<ChoirForge>(); }

} // namespace hem::staging
