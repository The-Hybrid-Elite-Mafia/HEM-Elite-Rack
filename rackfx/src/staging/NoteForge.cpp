// =============================================================================
//  05 NOTEFORGE — real-time vocal pitch correction, natural to robotic.
//  Its own engine (not the rack's MADE MAN / PitchKit): a McLeod NSDF detector and a
//  pitch-synchronous overlap-add shifter written for NoteForge. FORMANT = CORRECTED keeps the
//  voice's formants (grains placed at the new period); NON-CORRECTED shifts them with the pitch
//  (a two-tap resampling shifter).
//  How the target is chosen: the nearest LEGAL note of KEY / SCALE (or the per-note states, or a
//  held MIDI note when MIDI TARGET is on), TOLERANCE cents of slack, HOLD ms before a new note
//  is accepted, TRANSITION ms to glide between targets, SPEED ms to reach the target inside a note,
//  VIBRATO % of the singer's own vibrato kept, CORRECTION % of the move applied.
//  Dead flat on load: CORRECTION 0 % (the voice passes through, delayed by the reported latency),
//  REFERENCE TONE off. Latency: 2 periods of the lowest pitch it tracks (50 Hz) plus 16 samples.
// =============================================================================
#include "hem/rack/Notes.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P {
    kSpeed, kTransition, kTolerance, kHold, kVibrato, kCorrection,
    kKey, kScale, kRange, kReference, kFormant, kMidiTarget, kRefTone, kRefLevel, kGroupOct,
    kNote0, // 12 note states C .. B
    kActive = kNote0 + 12, kBypass, kCount
};
const char* const kKeys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kScales[] = {"CHROMATIC", "MAJOR", "MINOR", "HARMONIC MINOR", "MAJOR PENTATONIC", "MINOR PENTATONIC", "BLUES", "DORIAN"};
const char* const kRanges[] = {"BASS", "BARITONE", "TENOR", "ALTO", "MEZZO SOPRANO", "SOPRANO"};
const char* const kStates[] = {"SCALE", "LEGAL", "ILLEGAL", "PITCH UP", "PITCH DOWN", "BYPASS"};
const char* const kFormantN[] = {"NON-CORRECTED", "CORRECTED"};
constexpr std::array<std::uint16_t, 8> kScaleMask{
    0xFFF,                                                   // chromatic
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),
};
constexpr std::array<std::array<float, 2>, 6> kRangeHz{{{50, 400}, {70, 500}, {90, 700}, {130, 900}, {150, 1000}, {180, 1400}}};

#define HEM_NOTE(i) {"note_" #i, "NOTE " #i, "", 0.0f, 5.0f, 0.0f, Taper::Choice, kStates}
const ParamInfo kParams[kCount] = {
    {"speed", "SPEED", "ms", 0.1f, 800.0f, 15.0f, Taper::Log},
    {"transition", "TRANSITION", "ms", 0.1f, 800.0f, 120.0f, Taper::Log},
    {"tolerance", "TOLERANCE", "cents", 0.0f, 40.0f, 0.0f},
    {"hold", "HOLD", "ms", 0.0f, 300.0f, 0.0f},
    {"vibrato", "VIBRATO", "%", 0.0f, 200.0f, 100.0f},
    {"correction", "CORRECTION", "%", 0.0f, 100.0f, 0.0f},
    {"key", "KEY", "", 0.0f, 11.0f, 0.0f, Taper::Choice, kKeys},
    {"scale", "SCALE", "", 0.0f, 7.0f, 0.0f, Taper::Choice, kScales},
    {"range", "RANGE", "", 0.0f, 5.0f, 2.0f, Taper::Choice, kRanges},
    {"reference", "REFERENCE", "Hz", 415.3f, 466.16f, 440.0f},
    {"formant", "FORMANT", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kFormantN},
    {"midi_target", "MIDI TARGET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"ref_tone", "REFERENCE TONE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"ref_level", "REFERENCE TONE LEVEL", "dB", -18.0f, 18.0f, 0.0f},
    {"group_octaves", "GROUP OCTAVES", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    HEM_NOTE(0), HEM_NOTE(1), HEM_NOTE(2), HEM_NOTE(3), HEM_NOTE(4), HEM_NOTE(5),
    HEM_NOTE(6), HEM_NOTE(7), HEM_NOTE(8), HEM_NOTE(9), HEM_NOTE(10), HEM_NOTE(11),
    {"active", "ACTIVE", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_NOTE

const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "dB", -48, 0}, {"OUTPUT", "dB", -48, 0}, {"DETECTED", "Hz", 0, 1400}, {"TARGET", "Hz", 0, 1400},
    {"PITCH OFFSET", "cents", -50, 50, true},
};

constexpr std::uint32_t kRing = 16384;   // power of two, holds latency + a grain
constexpr float kLowestHz = 50.0f;
constexpr std::size_t kCtl = 512;       // control history, one entry per 32 samples (covers the latency)

class NoteForge final : public hc::fx::Effect, public hem::rack::NoteSink {
public:
    NoteForge() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_; }
    bool paramActive(int i) const noexcept override {
        if (i == kRefLevel) return p(kRefTone) > 0.5f;
        if (i == kKey || i == kScale) return p(kMidiTarget) < 0.5f || held_ < 0;
        return true;
    }

    // MIDI TARGET: the last held note is the target
    void noteOn(int note, float, int) noexcept override { held_ = note; }
    void noteOff(int note, int) noexcept override { if (note == held_) held_ = -1; }
    void allNotesOff() noexcept override { held_ = -1; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        det_.prepare(fs, kLowestHz, 1400.0f, 128);
        maxT_ = static_cast<std::uint32_t>(std::ceil(fs / kLowestHz));
        lat_ = 2 * maxT_ + 16;
        in_.assign(kRing, 0.0f); inR_.assign(kRing, 0.0f);
        acc_.assign(kRing, 0.0f); accR_.assign(kRing, 0.0f); nrm_.assign(kRing, 0.0f);
        inM_.prepare(fs); outM_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        det_.reset();
        std::fill(in_.begin(), in_.end(), 0.0f); std::fill(inR_.begin(), inR_.end(), 0.0f);
        std::fill(acc_.begin(), acc_.end(), 0.0f); std::fill(accR_.begin(), accR_.end(), 0.0f); std::fill(nrm_.begin(), nrm_.end(), 0.0f);
        t_ = 0; nextSyn_ = -1.0; ana_ = 0.0; shift_ = 0.0f; center_ = -1.0f; target_ = -1.0f; glide_ = -1.0f;
        pending_ = -1; pendingFor_ = 0; wet_ = 0.0f; ctl_.fill(Ctl{}); rsW_ = static_cast<float>(maxT_); rsPhase_ = 0.0; refPhase_ = 0.0; detHz_ = 0; lastT_ = 480.0f;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inM_.feed(L, n);
        if (p(kBypass) > 0.5f) { outM_.feed(L, n); publishAll(); return; }   // host bypass is instant; no delay compensation needed
        const auto& rg = kRangeHz[static_cast<std::size_t>(p(kRange))];
        det_.setRange(rg[0], rg[1]);
        const float speedC = hc::fx::onePoleCoef(fs_, p(kSpeed));
        const float glideC = hc::fx::onePoleCoef(fs_, p(kTransition));
        const float centerC = hc::fx::onePoleCoef(fs_, 150.0);   // the note's centre without its vibrato (ASSUMED 150 ms)
        const float fadeC = hc::fx::onePoleCoef(fs_, 10.0);
        const bool correcting = p(kActive) > 0.5f && p(kCorrection) > 0.0f;
        const bool psola = p(kFormant) > 0.5f;
        const float refG = p(kRefTone) > 0.5f ? kit::dbToGain(p(kRefLevel)) * 0.25f : 0.0f;

        for (std::uint32_t i = 0; i < n; ++i) {
            const std::int64_t now = t_++;
            in_[static_cast<std::size_t>(now) & (kRing - 1)] = L[i];
            inR_[static_cast<std::size_t>(now) & (kRing - 1)] = R[i];
            det_.push(0.5f * (L[i] + R[i]));
            if ((now & 31) == 0) {
                steer(speedC, glideC, centerC, 32);
                Ctl& c = ctl_[static_cast<std::size_t>(now >> 5) & (kCtl - 1)];
                c.shift = shift_; c.T = lastT_; c.voiced = detHz_ > 0.0f;
            }
            // the detector's estimate describes the middle of its window (maxT_ samples ago); the audio we
            // output is lat_ samples old, so read the control written (lat_ - maxT_) samples ago
            const std::int64_t back = static_cast<std::int64_t>(lat_ - maxT_);
            const std::int64_t ci = (now - back) >> 5;
            const Ctl& cur = ci >= 0 ? ctl_[static_cast<std::size_t>(ci) & (kCtl - 1)] : ctl_[0];

            const std::int64_t d = now - static_cast<std::int64_t>(lat_);   // the delayed time we output
            const float ratio = std::exp2(cur.shift / 12.0f);
            const float wetTarget = correcting && cur.voiced ? 1.0f : 0.0f;
            wet_ = wetTarget + fadeC * (wet_ - wetTarget);

            float yl = d >= 0 ? in_[static_cast<std::size_t>(d) & (kRing - 1)] : 0.0f;
            float yr = d >= 0 ? inR_[static_cast<std::size_t>(d) & (kRing - 1)] : 0.0f;
            if (d >= 0 && (wet_ > 1e-4f || wetTarget > 0.0f)) {
                float wl = 0, wr = 0;
                if (psola) psolaTick(d, ratio, cur.T, cur.voiced, wl, wr);
                else resampleTick(d, ratio, cur.T, cur.voiced, wl, wr);
                yl += wet_ * (wl - yl); yr += wet_ * (wr - yr);
            } else {
                nextSyn_ = -1.0;   // restart the grain clock cleanly next time
            }
            if (refG > 0.0f && glide_ > 0.0f) {   // the target note as a sine, for the singer's ear
                refPhase_ += static_cast<double>(noteHz(glide_)) / fs_;
                if (refPhase_ >= 1.0) refPhase_ -= 1.0;
                const float s = refG * std::sin(kit::kTau * static_cast<float>(refPhase_));
                yl += s; yr += s;
            }
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        outM_.feed(L, n);
        publishAll();
    }

private:
    [[nodiscard]] float noteHz(float midi) const noexcept { return p(kReference) * std::exp2((midi - 69.0f) / 12.0f); }
    [[nodiscard]] float hzToNote(float hz) const noexcept { return 69.0f + 12.0f * std::log2(hz / p(kReference)); }

    // 0 scale-defined, 1 legal, 2 illegal, 3 pitch up, 4 pitch down, 5 bypass
    [[nodiscard]] int stateOf(int pc) const noexcept { return static_cast<int>(p(kNote0 + pc)); }
    [[nodiscard]] bool legal(int midi) const noexcept {
        const int pc = ((midi % 12) + 12) % 12;
        const int st = stateOf(pc);
        if (st == 1 || st == 5) return true;
        if (st >= 2) return false;
        const int rel = ((pc - static_cast<int>(p(kKey))) % 12 + 12) % 12;
        return (kScaleMask[static_cast<std::size_t>(p(kScale))] >> rel) & 1;
    }

    // picks the note to aim at for a sung pitch m (MIDI units); -1 = leave it alone
    [[nodiscard]] float chooseTarget(float m) const noexcept {
        if (p(kMidiTarget) > 0.5f && held_ >= 0) {
            float t = static_cast<float>(held_);
            if (p(kGroupOct) > 0.5f) while (t - m > 6.0f) t -= 12.0f;   // the held note in the singer's octave
            if (p(kGroupOct) > 0.5f) while (m - t > 6.0f) t += 12.0f;
            return t;
        }
        const int near = static_cast<int>(std::lround(m));
        const int pc = ((near % 12) + 12) % 12;
        const int st = stateOf(pc);
        if (st == 5) return -1.0f;                                       // BYPASS note: no correction near it
        if (st == 3) { int t = near + 1; while (!legal(t) && t < near + 12) ++t; return static_cast<float>(t); }
        if (st == 4) { int t = near - 1; while (!legal(t) && t > near - 12) --t; return static_cast<float>(t); }
        int best = near; float bd = 1e9f;
        for (int t = near - 6; t <= near + 6; ++t)
            if (legal(t) && std::fabs(static_cast<float>(t) - m) < bd) { bd = std::fabs(static_cast<float>(t) - m); best = t; }
        return static_cast<float>(best);
    }

    // runs every 32 samples: detector -> target -> the pitch move (semitones) the shifter applies
    void steer(float speedC, float glideC, float centerC, std::uint32_t k) noexcept {
        const float hz = det_.hz();
        detHz_ = hz;
        if (hz <= 0.0f) { shift_ *= std::pow(speedC, static_cast<float>(k)); return; }
        lastT_ = static_cast<float>(fs_ / hz);
        const float m = hzToNote(hz);
        if (center_ < 0.0f) center_ = m;
        center_ = m + std::pow(centerC, static_cast<float>(k)) * (center_ - m);

        float want = chooseTarget(center_);
        // TOLERANCE: inside this many cents of a legal note, no correction
        if (want > 0.0f && std::fabs(center_ - want) * 100.0f < p(kTolerance)) want = -1.0f;
        // HOLD: a new target must persist this long before it is taken
        if (want != target_) {
            if (static_cast<int>(want * 100) != pending_) { pending_ = static_cast<int>(want * 100); pendingFor_ = 0; }
            pendingFor_ += k;
            if (static_cast<float>(pendingFor_) >= p(kHold) * 0.001f * static_cast<float>(fs_)) target_ = want;
        }
        if (target_ < 0.0f) { shift_ *= std::pow(speedC, static_cast<float>(k)); return; }
        if (glide_ < 0.0f) glide_ = target_;
        glide_ = target_ + std::pow(glideC, static_cast<float>(k)) * (glide_ - target_);

        const float vib = p(kVibrato) * 0.01f;
        const float desired = glide_ + (m - center_) * vib;               // keep this much of the singer's vibrato
        const float move = (desired - m) * p(kCorrection) * 0.01f;
        shift_ = move + std::pow(speedC, static_cast<float>(k)) * (shift_ - move);
        shift_ = std::clamp(shift_, -12.0f, 12.0f);
    }

    // pitch-synchronous overlap-add: grains two periods long, taken one period apart from the input,
    // laid down one period / ratio apart; their window sum normalises the output
    void psolaTick(std::int64_t d, float ratio, float period, bool voiced, float& yl, float& yr) noexcept {
        const float T = voiced ? period : 480.0f;                      // unpitched: 10 ms grains at ratio 1
        const float rr = voiced ? ratio : 1.0f;
        if (nextSyn_ < 0.0) { nextSyn_ = static_cast<double>(d); ana_ = static_cast<double>(d); }
        const int g = static_cast<int>(std::min(T, static_cast<float>(maxT_)));
        while (nextSyn_ - g <= static_cast<double>(d)) {
            while (ana_ + T <= nextSyn_) ana_ += T;                    // the analysis grid follows the synthesis point,
            while (ana_ > nextSyn_) ana_ -= T;                         // never ahead of it (keeps every read inside the latency)
            const auto a = static_cast<std::int64_t>(std::llround(ana_));
            const auto s = static_cast<std::int64_t>(std::llround(nextSyn_));
            for (int k = -g; k <= g; ++k) {
                const float w = 0.5f + 0.5f * std::cos(kit::kPi * static_cast<float>(k) / static_cast<float>(g));
                const auto src = static_cast<std::size_t>(a + k) & (kRing - 1);
                const auto dst = static_cast<std::size_t>(s + k) & (kRing - 1);
                acc_[dst] += w * in_[src];
                accR_[dst] += w * inR_[src];
                nrm_[dst] += w;
            }
            nextSyn_ += static_cast<double>(T / rr);
        }
        const auto at = static_cast<std::size_t>(d) & (kRing - 1);
        const float nn = std::max(nrm_[at], 0.25f);
        yl = acc_[at] / nn; yr = accR_[at] / nn;
        acc_[at] = 0; accR_[at] = 0; nrm_[at] = 0;
    }

    // formants move with the pitch: two read heads sweeping a window, crossfaded (a tape-style shifter).
    // The window is an even number of the voice's periods, so a head's jump back lands in phase and
    // the two heads (half a window apart) never cancel.
    void resampleTick(std::int64_t d, float ratio, float period, bool voiced, float& yl, float& yr) noexcept {
        const float T = voiced ? period : static_cast<float>(maxT_);
        const float wNew = 2.0f * T * std::max(1.0f, std::floor(static_cast<float>(maxT_) / (2.0f * T)));   // even count: the two heads sit in phase
        if (std::fabs(wNew - rsW_) > 0.5f) {                           // keep head 1's position when the window resizes
            rsPhase_ = std::clamp(rsPhase_ * static_cast<double>(rsW_ / wNew), 0.0, 0.999999);
            rsW_ = wNew;
        }
        const float W = rsW_;
        rsPhase_ += static_cast<double>((1.0f - ratio) / W);
        rsPhase_ -= std::floor(rsPhase_);
        for (int h = 0; h < 2; ++h) {
            double ph = rsPhase_ + 0.5 * h; ph -= std::floor(ph);
            const double back = ph * W;                                // samples behind the delayed point (0 .. W)
            const double pos = static_cast<double>(d) + 0.5 * W - back;   // centred on the delayed point, never past `now`
            const auto i0 = static_cast<std::int64_t>(std::floor(pos));
            const float f = static_cast<float>(pos - static_cast<double>(i0));
            const auto a = static_cast<std::size_t>(i0) & (kRing - 1), b = static_cast<std::size_t>(i0 + 1) & (kRing - 1);
            const float w = std::sin(kit::kPi * static_cast<float>(ph));
            const float gw = w * w;                                    // the two heads' windows sum to 1
            yl += gw * (in_[a] + f * (in_[b] - in_[a]));
            yr += gw * (inR_[a] + f * (inR_[b] - inR_[a]));
        }
    }

    void publishAll() noexcept {
        publishMeter(0, inM_.db()); publishMeter(1, outM_.db());
        publishMeter(2, detHz_);
        publishMeter(3, target_ > 0.0f ? noteHz(glide_) : 0.0f);
        publishMeter(4, detHz_ > 0.0f && target_ > 0.0f ? std::clamp((hzToNote(detHz_) - target_) * 100.0f, -50.0f, 50.0f) : 0.0f);
    }

    double fs_ = 48000;
    kit::PitchDetector det_;
    std::uint32_t maxT_ = 960, lat_ = 1936;
    std::vector<float> in_, inR_, acc_, accR_, nrm_;
    std::int64_t t_ = 0;
    double nextSyn_ = -1, ana_ = 0, rsPhase_ = 0, refPhase_ = 0;
    float shift_ = 0, center_ = -1, target_ = -1, glide_ = -1, wet_ = 0, detHz_ = 0, lastT_ = 480;
    int pending_ = -1; std::uint32_t pendingFor_ = 0;
    int held_ = -1;
    float rsW_ = 960.0f;
    struct Ctl { float shift = 0, T = 480; bool voiced = false; };
    std::array<Ctl, kCtl> ctl_{};
    kit::PeakMeter inM_, outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeNoteForge() { return std::make_unique<NoteForge>(); }

} // namespace hem::staging
