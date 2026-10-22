// GRAND LARCENY — electric grand piano (see Holdouts.hpp).
//
// A voice is a struck string pair and its pickup, as partials: for partial k of a note at f0, its frequency
// k f0 sqrt(1 + B k^2) (B, the strings' stiffness, grows toward the top), its level from where the hammer hits
// (1/8.5 of the string: a dip near the 8th and 17th partials; a struck string's partials fall as 1/k), from how hard (a harder blow reaches higher
// partials) and from the body of the tone (FORMANT moves it; a pickup bump near 1.1 kHz, rolled off under
// 80 Hz and over 5 kHz). Each partial is two oscillators a hair apart: a loud one dying fast and a quiet one
// dying slowly (the two stages of a piano's decay, and the slow beat of a string pair). Higher partials and
// higher notes die sooner. Letting go of a key drops the damper (about 60 ms), except on the top notes,
// which have none; with the pedal down nothing is damped until it comes up.
// Around the voices: the action's thump, the damper landing, the pedal's own sound, the open strings ringing
// along while the pedal is down (24 resonators, C2..B3), then TONE, SQUEEZE (parallel), the effects (tremolo,
// auto pan, phaser, chorus, reverb), VOLUME and the limiter. All our own design; nothing sampled.
// Whatever comes in (a rack slot's song) passes untouched, held back the same 48 samples, with the piano on top.
#include "hem/rack/Holdouts.hpp"
#include "hem/rack/Notes.hpp"

#include "RoomKit.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace hem::rack {
namespace grandlarceny {
namespace {
using namespace hideout;

const char* const kSyncN[] = {"FREE", "SYNC"};
const char* const kNoteN[] = {"2/1", "1/1", "1/2 D", "1/2", "1/2 T", "1/4 D", "1/4", "1/4 T", "1/8 D", "1/8", "1/8 T",
                              "1/16 D", "1/16", "1/16 T", "1/32 D", "1/32", "1/32 T"};
constexpr int kNotes = 17;
constexpr double kNoteBeats[kNotes] = {8.0, 4.0, 3.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0,
                                       0.375, 0.25, 1.0 / 6.0, 0.1875, 0.125, 1.0 / 12.0};
#define HEM_KEY(n) {"key" #n, "Key " #n, "", 0, 1, 0, Taper::Toggle, kOnOff}
#define HEM_KEYS10(a) HEM_KEY(a##0), HEM_KEY(a##1), HEM_KEY(a##2), HEM_KEY(a##3), HEM_KEY(a##4), HEM_KEY(a##5), HEM_KEY(a##6), HEM_KEY(a##7), HEM_KEY(a##8), HEM_KEY(a##9)
const ParamInfo kParams[kCount] = {
    {"velcurve", "Velocity Curve", "", -50.0f, 50.0f, 0.0f},
    {"formant", "Formant", "st", -12.0f, 12.0f, 0.0f},
    {"tune", "Tune", "cents", -100.0f, 100.0f, 0.0f},
    {"main", "Main", "%", 0.0f, 100.0f, 100.0f},
    {"keyup", "Key Up", "%", 0.0f, 100.0f, 25.0f},
    {"mechanics", "Mechanics", "%", 0.0f, 100.0f, 25.0f},
    {"pedalnoise", "Pedal Sound", "%", 0.0f, 100.0f, 35.0f},
    {"squeeze", "Squeeze", "%", 0.0f, 100.0f, 50.0f},
    {"sympathy", "Sympathy", "%", 0.0f, 100.0f, 35.0f},
    {"bass", "Bass", "", -50.0f, 50.0f, 0.0f},
    {"mid", "Mid", "", -50.0f, 50.0f, 0.0f},
    {"treble", "Treble", "", -50.0f, 50.0f, 0.0f},
    {"volume", "Volume", "", 0.0f, 10.0f, 7.0f},
    {"limit", "Limit", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"fxon", "Effects", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"tremon", "Tremolo", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"tremsync", "Tremolo Sync", "", 0, 1, 1, Taper::Choice, kSyncN},
    {"tremnote", "Tremolo Note", "", 0, kNotes - 1, 11, Taper::Choice, kNoteN},
    {"tremhz", "Tremolo Rate", "Hz", 0.25f, 28.0f, 4.0f, Taper::Log},
    {"tremdepth", "Tremolo Depth", "", 0.0f, 100.0f, 60.0f},
    {"panon", "Auto Pan", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"pansync", "Auto Pan Sync", "", 0, 1, 1, Taper::Choice, kSyncN},
    {"pannote", "Auto Pan Note", "", 0, kNotes - 1, 3, Taper::Choice, kNoteN},
    {"panhz", "Auto Pan Rate", "Hz", 0.25f, 28.0f, 1.0f, Taper::Log},
    {"pandepth", "Auto Pan Depth", "", 0.0f, 100.0f, 33.0f},
    {"phon", "Phaser", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"phmix", "Phaser Mix", "%", 0.0f, 100.0f, 25.0f},
    {"phsync", "Phaser Sync", "", 0, 1, 0, Taper::Choice, kSyncN},
    {"phnote", "Phaser Note", "", 0, kNotes - 1, 1, Taper::Choice, kNoteN},
    {"phhz", "Phaser Rate", "Hz", 0.01f, 22.0f, 0.03f, Taper::Log},
    {"phdepth", "Phaser Depth", "", 0.0f, 100.0f, 53.5f},
    {"chon", "Chorus", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"chdepth", "Chorus Depth", "", 0.0f, 100.0f, 25.0f},
    {"rvmix", "Reverb Mix", "%", 0.0f, 100.0f, 30.0f},
    {"rvpre", "Reverb Pre-Delay", "ms", 20.0f, 180.0f, 55.0f},
    {"rvtime", "Reverb Time", "s", 0.4f, 6.0f, 2.0f, Taper::Log},
    {"rvdamp", "Reverb Damp", "", 0.1f, 2.0f, 0.8f},
    {"pedal", "Sustain Pedal", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"latch", "Latch Keys", "", 0, 1, 0, Taper::Toggle, kOnOff},
    HEM_KEYS10(0), HEM_KEYS10(1), HEM_KEYS10(2), HEM_KEYS10(3), HEM_KEYS10(4), HEM_KEYS10(5), HEM_KEY(60),
};
#undef HEM_KEY
#undef HEM_KEYS10
const hc::fx::MeterInfo kMeterInfo[kMeters] = {{"OUT L", "dBFS", -60.0f, 0.0f}, {"OUT R", "dBFS", -60.0f, 0.0f}, {"VOICES", "", 0.0f, 32.0f}};

constexpr int kPartials = 24, kOsc = 2 * kPartials;
constexpr int kSym = 24;          // sympathetic strings, C2 .. B3
constexpr int kMaxEvents = 512;
constexpr float kCeiling = 0.9661f;   // -0.3 dBFS

inline float dbOfF(float x) noexcept { return 20.0f * std::log10(std::max(x, 1e-9f)); }

struct Voice {
    // oscillators: state (re, im) and the per-sample step (rotation and decay together)
    alignas(32) float re[kOsc]{}, im[kOsc]{}, cr[kOsc]{}, ci[kOsc]{};
    int osc = 0, note = -1;
    bool held = false, sustained = false, releasing = false;
    float gain = 0.0f, attack = 0.0f, attackStep = 1.0f, rel = 1.0f, panL = 1.0f, panR = 1.0f;
    std::uint32_t age = 0, quiet = 0;
    [[nodiscard]] bool active() const noexcept { return note >= 0; }
};

struct Burst {   // a short noise burst through a two-pole low-pass
    float level = 0.0f, decay = 0.0f, a = 0.0f, z1 = 0.0f, z2 = 0.0f;
    std::uint32_t rng = 0x1234567u;
    float tick() noexcept {
        if (level < 1e-6f) return 0.0f;
        rng = rng * 1664525u + 1013904223u;
        const float n = (static_cast<float>(rng >> 8) / 8388608.0f - 1.0f) * level;
        level *= decay;
        z1 += a * (n - z1);
        z2 += a * (z1 - z2);
        return z2;
    }
    void fire(float lvl, float hz, float ms, double fs) noexcept {
        level = std::max(level, lvl);
        a = 1.0f - std::exp(-2.0f * static_cast<float>(std::numbers::pi) * hz / static_cast<float>(fs));
        decay = std::exp(-1.0f / (ms * 0.001f * static_cast<float>(fs)));
    }
};

class Impl final : public hc::fx::Effect, public NoteSink {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        const bool fx = p(FxOn) >= 0.5f;
        auto on = [&](int s) { return fx && p(s) >= 0.5f; };
        auto sync = [&](int s) { return p(s) >= 0.5f; };
        switch (i) {
        case TremOn: case PanOn: case PhOn: case ChOn: case RvMix: case RvPre: case RvTime: case RvDamp: return fx;
        case TremSync: case TremDepth: return on(TremOn);
        case TremNote: return on(TremOn) && sync(TremSync);
        case TremHz: return on(TremOn) && !sync(TremSync);
        case PanSync: case PanDepth: return on(PanOn);
        case PanNote: return on(PanOn) && sync(PanSync);
        case PanHz: return on(PanOn) && !sync(PanSync);
        case PhMix: case PhSync: case PhDepth: return on(PhOn);
        case PhNote: return on(PhOn) && sync(PhSync);
        case PhHz: return on(PhOn) && !sync(PhSync);
        case ChDepth: return on(ChOn);
        default: return true;
        }
    }
    int sustainParam() const noexcept override { return Pedal; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : look_) d.prepare(kLatency + 4);
        for (auto& d : thru_) d.prepare(kLatency + 4);
        for (auto& b : in_) b.assign(hc::fx::Effect::kMaxBlock, 0.0f);
        for (auto& d : chorus_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(0.2 * fs) + 8);
        tail_.prepare(fs, 2.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& v : voice_) v = Voice{};
        nEvents_ = 0;
        for (auto& b : keyWas_) b = false;
        pedalWas_ = p(Pedal) >= 0.5f;
        thump_ = keyUp_ = pedalLow_ = pedalHigh_ = Burst{};
        pedalHigh_.rng = 0xBEEF1u;
        keyUp_.rng = 0xC0FFEEu;
        for (auto& r : sym_) r = {};
        symGain_ = 0.0f;
        for (auto& ch : eq_)
            for (auto& b : ch) b.clear();
        for (auto& ch : ph_) ch.fill(0.0f);
        phFb_[0] = phFb_[1] = 0.0f;
        for (auto& d : look_) d.clear();
        for (auto& d : thru_) d.clear();
        for (auto& d : chorus_) d.clear();
        for (auto& d : pre_) d.clear();
        tail_.clear();
        env_ = 0.0f;
        limGain_ = 1.0f;
        for (auto& w : win_) w.fill(0.0f);
        winPos_ = 0;
        trem_ = pan_ = phase_ = chor_ = 0.0;
        key_.fill(-1e9f);
        design();
        vol_.prepare(fs_, 20, volumeGain());
    }

    // ---- notes (audio thread, before the block's process) ----------------------------------------------
    void noteOn(int note, float velocity, int offset) noexcept override { push(Ev::On, note, velocity, offset); }
    void noteOff(int note, int offset) noexcept override { push(Ev::Off, note, 0.0f, offset); }
    void allNotesOff() noexcept override {
        nEvents_ = 0;
        for (auto& v : voice_)
            if (v.active()) release(v, 0.01f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        n = std::min<std::uint32_t>(n, static_cast<std::uint32_t>(in_[0].size()));
        std::copy(L, L + n, in_[0].begin());
        std::copy(R, R + n, in_[1].begin());
        std::fill(L, L + n, 0.0f);
        std::fill(R, R + n, 0.0f);
        design();
        // the plate's keys
        for (int k = 0; k < kKeys; ++k) {
            const bool down = p(Key0 + k) >= 0.5f;
            if (down != keyWas_[k]) {
                keyWas_[k] = down;
                push(down ? Ev::On : Ev::Off, kFirstKeyNote + k, 0.7f, 0);
            }
        }
        // the pedal
        const bool pedal = p(Pedal) >= 0.5f;
        if (pedal != pedalWas_) {
            pedalWas_ = pedal;
            const float lvl = 0.25f * p(PedalNoise) * 0.01f;
            pedalLow_.fire(lvl, 140.0f, 60.0f, fs_);
            pedalHigh_.fire(0.25f * lvl, 3000.0f, 35.0f, fs_);
            if (!pedal)
                for (auto& v : voice_)
                    if (v.active() && v.sustained) damp(v);
        }
        // the voices, between the events
        std::uint32_t at = 0;
        int e = 0;
        // events in time order (a host gives them sorted; the plate's keys come at 0)
        std::sort(events_.begin(), events_.begin() + nEvents_, [](const Event& a, const Event& b) { return a.offset < b.offset; });
        while (at < n) {
            while (e < nEvents_ && static_cast<std::uint32_t>(std::max(0, events_[static_cast<std::size_t>(e)].offset)) <= at) handle(events_[static_cast<std::size_t>(e++)]);
            std::uint32_t end = n;
            if (e < nEvents_) end = std::min<std::uint32_t>(n, static_cast<std::uint32_t>(std::max(events_[static_cast<std::size_t>(e)].offset, static_cast<int>(at) + 1)));
            render(L + at, R + at, end - at);
            at = end;
        }
        while (e < nEvents_) handle(events_[static_cast<std::size_t>(e++)]);   // past the block's end: now
        nEvents_ = 0;
        post(L, R, n);
        // OUT L / R: everything that leaves the plug-in (the piano and the sound passing through)
        float pkL = 0.0f, pkR = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            L[i] += thru_[0].process(in_[0][i], kLatency);
            R[i] += thru_[1].process(in_[1][i], kLatency);
            pkL = std::max(pkL, std::fabs(L[i]));
            pkR = std::max(pkR, std::fabs(R[i]));
        }
        publishMeter(OutL, std::max(-100.0f, dbOfF(pkL)));
        publishMeter(OutR, std::max(-100.0f, dbOfF(pkR)));
    }

private:
    enum class Ev : std::uint8_t { On, Off };
    struct Event { Ev type; int note; float vel; int offset; };
    struct Res { float y1 = 0.0f, y2 = 0.0f, c = 0.0f, r2 = 0.0f; };

    void push(Ev t, int note, float vel, int offset) noexcept {
        if (nEvents_ < kMaxEvents) events_[static_cast<std::size_t>(nEvents_++)] = {t, note, vel, offset};
    }
    void handle(const Event& ev) noexcept {
        if (ev.note < 0 || ev.note > 127) return;
        if (ev.type == Ev::On && ev.vel > 0.0f) strike(ev.note, ev.vel);
        else lift(ev.note);
    }
    void release(Voice& v, float seconds) noexcept {
        v.releasing = true;
        v.held = v.sustained = false;
        v.rel = std::exp(-1.0f / (seconds * static_cast<float>(fs_)));
    }
    // the damper lands (the top notes have none: they ring on)
    void damp(Voice& v) noexcept {
        v.sustained = false;
        if (v.note >= 89) { v.held = false; return; }
        release(v, 0.06f);
    }
    void lift(int note) noexcept {
        for (auto& v : voice_)
            if (v.active() && v.note == note && v.held) {
                v.held = false;
                keyUp_.fire(0.06f * p(KeyUp) * 0.01f, 260.0f, 22.0f, fs_);
                if (p(Pedal) >= 0.5f) v.sustained = true;
                else damp(v);
            }
    }
    Voice& freeVoice() noexcept {
        Voice* best = nullptr;
        for (auto& v : voice_)
            if (!v.active()) return v;
        // none free: the quietest released one, else the oldest
        for (auto& v : voice_)
            if (v.releasing && (!best || v.gain < best->gain)) best = &v;
        if (!best)
            for (auto& v : voice_)
                if (!best || v.age > best->age) best = &v;
        return *best;
    }
    void strike(int note, float vel) noexcept {
        for (auto& v : voice_)   // the same string struck again: the old sound gets out of the way
            if (v.active() && v.note == note) release(v, 0.02f);
        Voice& v = freeVoice();
        v = Voice{};
        const double curve = std::exp2(p(VelCurve) / 25.0);
        const double vv = std::pow(std::clamp(static_cast<double>(vel), 0.0, 1.0), curve);
        const double f0 = 440.0 * std::exp2((note - 69) / 12.0 + p(Tune) / 1200.0);
        const double stiff = 2e-4 * std::exp2((note - 60) / 24.0);
        const double t0 = std::clamp(14.0 * std::exp2(-(note - 21) / 24.0), 1.2, 14.0);
        const double fr = std::exp2(p(Formant) / 12.0);
        const double bright = 0.45 - 0.3 * vv;
        const double level = 0.5 * std::pow(vv, 1.5);
        const double top = std::min(18000.0, fs_ * 0.45);
        std::uint32_t rng = 0x9E3779B9u * static_cast<std::uint32_t>(note + 1);
        auto rnd = [&]() {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<double>(rng >> 8) / 8388608.0 - 1.0;
        };
        int o = 0;
        for (int k = 1; k <= kPartials; ++k) {
            const double fk = k * f0 * std::sqrt(1.0 + stiff * k * k);
            if (fk >= top) break;
            // the tone's body (FORMANT moves it): a pickup bump near 1.1 kHz, rolled off under 80 Hz and over 5 kHz
            const double lb = std::log2(fk / (1100.0 * fr));
            const double bodyDb = 6.0 * std::exp(-0.5 * lb * lb) - 12.0 * std::max(0.0, std::log2(fk / (5000.0 * fr))) -
                                  12.0 * std::max(0.0, std::log2(80.0 * fr / fk));
            const double ak = level * std::pow(k, -1.0) * std::fabs(std::sin(std::numbers::pi * k / 8.5)) * std::exp(-(k - 1) * bright) *
                              std::pow(10.0, bodyDb / 20.0);
            const double tk = t0 / (1.0 + 2.0 * std::pow(fk / 1500.0, 1.3));
            const double ph = std::numbers::pi * 2.0 * (0.5 + 0.5 * rnd());
            // the loud, fast-dying stage and the quiet, slow one a hair apart
            const double f[2] = {fk, fk * (1.0 + 0.0006 * rnd())};
            const double a[2] = {0.8 * ak, 0.2 * ak};
            const double t[2] = {0.6 * tk, 2.5 * tk};
            for (int s = 0; s < 2; ++s) {
                const double w = 2.0 * std::numbers::pi * f[s] / fs_, d = std::pow(10.0, -3.0 / (t[s] * fs_));
                v.re[o] = static_cast<float>(a[s] * std::cos(ph));
                v.im[o] = static_cast<float>(a[s] * std::sin(ph));
                v.cr[o] = static_cast<float>(d * std::cos(w));
                v.ci[o] = static_cast<float>(d * std::sin(w));
                ++o;
            }
        }
        v.osc = o;
        v.note = note;
        v.held = true;
        v.gain = 1.0f;
        v.attack = 0.0f;
        v.attackStep = static_cast<float>(1.0 / (0.0015 * fs_));   // the hammer's contact, 1.5 ms
        const float pan = std::clamp((static_cast<float>(note) - 66.0f) / 42.0f, -1.0f, 1.0f) * 0.6f;
        const float th = (pan + 1.0f) * 0.25f * static_cast<float>(std::numbers::pi);
        v.panL = std::cos(th) * std::numbers::sqrt2_v<float>;
        v.panR = std::sin(th) * std::numbers::sqrt2_v<float>;
        thump_.fire(static_cast<float>(0.35 * vv) * p(Mechanics) * 0.01f, 420.0f, 16.0f, fs_);
    }

    // ---- the strings, the action and the open strings (in place: L / R get the piano) -----------------------
    void render(float* L, float* R, std::uint32_t n) noexcept {
        const float main = p(Main) * 0.01f;
        const bool pedal = p(Pedal) >= 0.5f;
        const float symWant = pedal ? p(Sympathy) * 0.01f : 0.0f, symStep = static_cast<float>(1.0 / (0.05 * fs_));
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = 0.0f, r = 0.0f;
            for (auto& v : voice_) {
                if (!v.active()) continue;
                float s = 0.0f;
                for (int j = 0; j < v.osc; ++j) {
                    const float x = v.re[j] * v.cr[j] - v.im[j] * v.ci[j];
                    v.im[j] = v.re[j] * v.ci[j] + v.im[j] * v.cr[j];
                    v.re[j] = x;
                    s += x;
                }
                if (v.attack < 1.0f) {
                    v.attack = std::min(1.0f, v.attack + v.attackStep);
                    s *= 0.5f - 0.5f * std::cos(static_cast<float>(std::numbers::pi) * v.attack);
                }
                if (v.releasing) v.gain *= v.rel;
                s *= v.gain;
                l += v.panL * s;
                r += v.panR * s;
                v.quiet = std::fabs(s) < 1e-6f ? v.quiet + 1 : 0;
            }
            l *= main;
            r *= main;
            // the open strings ring along while the pedal is down
            symGain_ = symWant > symGain_ ? std::min(symWant, symGain_ + symStep) : std::max(symWant, symGain_ - symStep);
            float sym = 0.0f;
            if (symGain_ > 0.0f || sym_[0].y1 != 0.0f) {
                const float x = 0.02f * (l + r);
                for (auto& q : sym_) {
                    const float y = x + q.c * q.y1 - q.r2 * q.y2;
                    q.y2 = q.y1;
                    q.y1 = y;
                    sym += y;
                }
                sym *= 0.02f * symGain_;
            }
            const float noise = thump_.tick() + keyUp_.tick() + pedalLow_.tick() + pedalHigh_.tick();
            L[i] = l + sym + noise;
            R[i] = r + sym + noise;
        }
        // voices that have died away come free
        int voices = 0;
        for (auto& v : voice_) {
            if (!v.active()) continue;
            v.age += n;
            const bool gone = (v.releasing && v.gain < 1e-4f) || v.quiet > static_cast<std::uint32_t>(0.05 * fs_);
            if (gone && !v.held) v.note = -1;
            else ++voices;
        }
        publishMeter(Voices, static_cast<float>(voices));
    }

    // ---- TONE, SQUEEZE, the effects, VOLUME, the limiter -----------------------------------------------------
    void post(float* L, float* R, std::uint32_t n) noexcept {
        const float sq = p(Squeeze) * 0.01f;
        const float att = std::exp(-1.0f / (0.003f * static_cast<float>(fs_))), rel = std::exp(-1.0f / (0.12f * static_cast<float>(fs_)));
        const bool fx = p(FxOn) >= 0.5f, trem = fx && p(TremOn) >= 0.5f, pan = fx && p(PanOn) >= 0.5f, ph = fx && p(PhOn) >= 0.5f,
                   ch = fx && p(ChOn) >= 0.5f;
        const double tremHz = rate(TremSync, TremNote, TremHz), panHz = rate(PanSync, PanNote, PanHz), phHz = rate(PhSync, PhNote, PhHz);
        const float tremD = p(TremDepth) * 0.01f, panD = p(PanDepth) * 0.01f, phMix = p(PhMix) * 0.01f, phFb = 0.9f * p(PhDepth) * 0.01f;
        const float chD = p(ChDepth) * 0.01f;
        const float rvM = fx ? p(RvMix) * 0.01f : 0.0f, preS = static_cast<float>(p(RvPre) * 0.001 * fs_);
        const bool limit = p(Limit) >= 0.5f;
        const float wVol = volumeGain();
        const float piOverFs = static_cast<float>(std::numbers::pi / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            float x[2] = {L[i], R[i]};
            for (int c = 0; c < 2; ++c) {
                auto& f = eq_[static_cast<std::size_t>(c)];
                x[c] = f[2].tick(f[1].tick(f[0].tick(x[c])));
            }
            // SQUEEZE: a compressor (-20 dBFS, 4:1, +8 dB back) mixed in
            const float a = std::max(std::fabs(x[0]), std::fabs(x[1]));
            env_ = a > env_ ? a + att * (env_ - a) : a + rel * (env_ - a);
            const float over = dbOfF(env_) + 20.0f;
            const float g = hc::fx::dbToGain((over > 0.0f ? -0.75f * over : 0.0f) + 8.0f);
            for (float& v : x) v += sq * (g * v - v);
            if (trem) {
                const float t = 1.0f - tremD * 0.5f * (1.0f - static_cast<float>(std::sin(2.0 * std::numbers::pi * trem_)));
                x[0] *= t;
                x[1] *= t;
            }
            if (pan) {
                const float th = (1.0f + panD * static_cast<float>(std::sin(2.0 * std::numbers::pi * pan_))) * 0.25f * static_cast<float>(std::numbers::pi);
                x[0] *= std::cos(th) * std::numbers::sqrt2_v<float>;
                x[1] *= std::sin(th) * std::numbers::sqrt2_v<float>;
            }
            if (ph) {
                const float fc = 700.0f * std::exp2(2.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_)));
                const float t = std::tan(std::min(fc, static_cast<float>(fs_) * 0.45f) * piOverFs), ac = (t - 1.0f) / (t + 1.0f);
                for (int c = 0; c < 2; ++c) {
                    float v = x[c] + phFb * phFb_[c];
                    for (float& z : ph_[static_cast<std::size_t>(c)]) {
                        const float y = ac * v + z;
                        z = v - ac * y;
                        v = y;
                    }
                    phFb_[c] = v;
                    x[c] = (x[c] + phMix * v) / (1.0f + phMix);
                }
            }
            if (ch) {   // four delays, each its own slow drift: two to each side
                for (int c = 0; c < 2; ++c) chorus_[c].write(x[c]);
                float w[2] = {0.0f, 0.0f};
                for (int k = 0; k < 4; ++k) {
                    const double ph2 = chor_ * (0.31 + 0.23 * k) + 0.25 * k;
                    const float d = static_cast<float>((7.0 + 1.6 * k + 2.0 * chD * std::sin(2.0 * std::numbers::pi * ph2)) * 0.001 * fs_);
                    w[k & 1] += 0.5f * chorus_[k & 1].read(d);
                }
                for (int c = 0; c < 2; ++c) x[c] += 0.5f * chD * (w[c] - x[c]);
            }
            if (fx || rvM > 0.0f) {
                pre_[0].write(x[0]);
                pre_[1].write(x[1]);
                float tl = 0.0f, tr = 0.0f;
                tail_.tick(pre_[0].read(preS), pre_[1].read(preS), tl, tr);
                const float dryG = std::min(1.0f, 2.0f * (1.0f - rvM)), wetG = std::min(1.0f, 2.0f * rvM);
                x[0] = dryG * x[0] + wetG * tl;
                x[1] = dryG * x[1] + wetG * tr;
            }
            trem_ = wrap(trem_ + tremHz / fs_);
            pan_ = wrap(pan_ + panHz / fs_);
            phase_ = wrap(phase_ + phHz / fs_);
            chor_ += 1.0 / fs_;
            if (chor_ > 1e6) chor_ = 0.0;
            // VOLUME, then the limiter (it always holds the sound back the same 48 samples)
            const float vg = vol_.next(wVol);
            x[0] *= vg;
            x[1] *= vg;
            const float need = std::min(1.0f, kCeiling / std::max({std::fabs(x[0]), std::fabs(x[1]), 1e-9f}));
            win_[0][static_cast<std::size_t>(winPos_)] = need;
            winPos_ = (winPos_ + 1) % static_cast<int>(kLatency);
            float y[2];
            for (int c = 0; c < 2; ++c) y[c] = look_[c].process(x[c], kLatency);
            if (limit) {
                float want = 1.0f;
                for (float w : win_[0]) want = std::min(want, w);
                limGain_ = want < limGain_ ? want + 0.6f * (limGain_ - want) : want + 0.9995f * (limGain_ - want);
                for (float& v : y) v = std::clamp(v * limGain_, -kCeiling, kCeiling);
            }
            L[i] = y[0];
            R[i] = y[1];
        }
    }
    static double wrap(double v) noexcept { return v >= 1.0 ? v - std::floor(v) : v; }
    double rate(int sync, int note, int hz) const noexcept {
        return p(sync) >= 0.5f ? tempo() / 60.0 / kNoteBeats[std::clamp(static_cast<int>(std::lround(p(note))), 0, kNotes - 1)] : p(hz);
    }
    float volumeGain() const noexcept {
        const float v = p(Volume);
        return v <= 0.001f ? 0.0f : hc::fx::dbToGain(4.0f * (v - 7.0f));
    }
    void design() noexcept {
        const std::array<float, 7> k{p(Bass), p(Mid), p(Treble), p(RvTime), p(RvDamp), p(Tune), p(Formant)};
        if (k == key_) return;
        key_ = k;
        constexpr float kPer = 18.0f / 50.0f;
        const C lo = C::make(T::LowShelf, fs_, 203.0, 0.7071, p(Bass) * kPer), mid = C::make(T::Bell, fs_, 900.0, 0.7, p(Mid) * kPer),
                hi = C::make(T::HighShelf, fs_, 1875.0, 0.7071, p(Treble) * kPer);
        for (auto& ch : eq_) { ch[0].c = lo; ch[1].c = mid; ch[2].c = hi; }
        tail_.set(std::clamp(0.7 + 0.2 * p(RvTime), 0.7, 2.0), p(RvTime), 1.0, p(RvDamp), 250.0, 3000.0, 0.85);
        // the open strings, C2 .. B3, ringing about 3 s
        for (int s = 0; s < kSym; ++s) {
            const double f = 440.0 * std::exp2((36 + s - 69) / 12.0 + p(Tune) / 1200.0), r = std::pow(10.0, -3.0 / (3.0 * fs_));
            sym_[static_cast<std::size_t>(s)].c = static_cast<float>(2.0 * r * std::cos(2.0 * std::numbers::pi * f / fs_));
            sym_[static_cast<std::size_t>(s)].r2 = static_cast<float>(r * r);
        }
    }

    double fs_ = 48000.0, trem_ = 0.0, pan_ = 0.0, phase_ = 0.0, chor_ = 0.0;
    std::array<Voice, kVoices> voice_{};
    std::array<Event, kMaxEvents> events_{};
    int nEvents_ = 0;
    bool keyWas_[kKeys]{};
    bool pedalWas_ = false;
    Burst thump_, keyUp_, pedalLow_, pedalHigh_;
    std::array<Res, kSym> sym_{};
    float symGain_ = 0.0f;
    std::array<std::array<hc::fx::Biquad, 3>, 2> eq_{};
    std::array<std::array<float, 6>, 2> ph_{};
    float phFb_[2]{};
    hc::fx::DelayLine look_[2], thru_[2], chorus_[2], pre_[2];
    std::vector<float> in_[2];
    Tail tail_;
    float env_ = 0.0f, limGain_ = 1.0f;
    std::array<std::array<float, kLatency>, 1> win_{};
    int winPos_ = 0;
    std::array<float, 7> key_{};
    hc::fx::Smoothed vol_;
};

} // namespace
} // namespace grandlarceny

std::unique_ptr<hc::fx::Effect> makeGrandLarceny() { return std::make_unique<grandlarceny::Impl>(); }

} // namespace hem::rack
