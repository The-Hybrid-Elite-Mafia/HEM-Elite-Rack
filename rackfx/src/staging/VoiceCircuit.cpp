// =============================================================================
//  04 VOICECIRCUIT — vocal resynthesis: the voice's formants (a 20-band vocoder per engine)
//  imprinted on two synth engines that play the sung note (AUTO), held MIDI notes (MIDI) or
//  an external carrier (EXTERNAL, from the sidechain). Mixer of VOICE / SYNTH / PROCESSED,
//  vocal utility, and an FX rack (chorus, delay, reverb, drive).
//  Its own engine: none of the rack's vocoder / reverb code is used.
//  Dead flat on load: SYNTH and PROCESSED at -60 dB (off), VOICE 0 dB, utility MIX 0 % (dry),
//  every FX off, output 0 dB.
//  Not in a plug-in (greyed on the faceplate): MIDI OUT (an effect cannot send MIDI here).
// =============================================================================
#include "hem/rack/Notes.hpp"
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

const char* const kVsN[] = {"MIC", "SIDECHAIN"};
const char* const kCarN[] = {"INTERNAL", "EXTERNAL"};
const char* const kNoteN[] = {"MIDI", "AUTO"};
const char* const kDirN[] = {"UP", "DOWN", "UP-DOWN", "RANDOM"};
const char* const kRateN[] = {"1/4", "1/8", "1/16", "1/32"};
const char* const kKeys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kScales[] = {"CHROMATIC", "MAJOR", "MINOR", "DORIAN", "MAJOR PENTATONIC", "MINOR PENTATONIC"};
const char* const kRouteN[] = {"PARALLEL", "SERIES"};
constexpr std::array<std::uint16_t, 6> kScaleMask{
    0xFFF,
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),
};

// ---- parameter layout -----------------------------------------------------------------------
enum Top {
    kVsMode, kVsGate, kVsGain, kCarMode, kCarDrive, kCarGain,
    kNoteMode, kMinNote, kNoteTol,
    kArpOn, kArpRange, kArpDir, kArpRate, kArpSync,
    kRoot, kScale, kLock,
    kTopCount
};
constexpr int kEngA = kTopCount;   // engine blocks start here
enum Eng { eOn, eX, eY, eChar, eHarm, eCoarse, eFine, eUnison, eSpread, eFilter, eReso, eGain,
           fFocus, fShift, fSpeed, fDyn, fPan, fGain, kEngCount };
constexpr int kEngB = kEngA + static_cast<int>(kEngCount);
enum Tail {
    kAMix = kEngB + static_cast<int>(kEngCount), kBMix, kRoute,
    kVoiceLvl, kSynthLvl, kProcLvl, kVoiceTone, kSynthTone, kProcTone, kLimiter,
    kBreath, kClarity, kCorrect, kHarmonics, kMix, kOutput,
    kChorusOn, kChorusMix, kDelayOn, kDelayFilter, kDelayTime, kDelaySync, kDelayFb, kDelayMix,
    kVerbOn, kVerbSize, kVerbMix, kDriveOn, kDriveAmt, kBypass, kCount
};

#define HEM_ENG(L, on) \
    {"eng" L "_on", "ENGINE " L " ENABLE", "", 0.0f, 1.0f, on, Taper::Toggle}, \
    {"eng" L "_x", "ENGINE " L " OSC X", "", 0.0f, 1.0f, 0.5f}, \
    {"eng" L "_y", "ENGINE " L " OSC Y", "", 0.0f, 1.0f, 0.0f}, \
    {"eng" L "_character", "ENGINE " L " CHARACTER", "", 0.0f, 100.0f, 0.0f}, \
    {"eng" L "_harmonics", "ENGINE " L " HARMONICS", "", 0.0f, 100.0f, 50.0f}, \
    {"eng" L "_coarse", "ENGINE " L " COARSE", "st", -24.0f, 24.0f, 0.0f}, \
    {"eng" L "_fine", "ENGINE " L " FINE", "cents", -100.0f, 100.0f, 0.0f}, \
    {"eng" L "_unison", "ENGINE " L " UNISON", "", 1.0f, 8.0f, 1.0f}, \
    {"eng" L "_spread", "ENGINE " L " SPREAD", "", 0.0f, 100.0f, 0.0f}, \
    {"eng" L "_filter", "ENGINE " L " FILTER", "Hz", 50.0f, 20000.0f, 20000.0f, Taper::Log}, \
    {"eng" L "_reso", "ENGINE " L " RESO", "", 0.0f, 100.0f, 0.0f}, \
    {"eng" L "_gain", "ENGINE " L " GAIN", "dB", -24.0f, 24.0f, 0.0f}, \
    {"fmt" L "_focus", "FORMANT " L " FOCUS", "", 0.0f, 100.0f, 50.0f}, \
    {"fmt" L "_shift", "FORMANT " L " SHIFT", "st", -12.0f, 12.0f, 0.0f}, \
    {"fmt" L "_speed", "FORMANT " L " SPEED", "ms", 1.0f, 200.0f, 10.0f, Taper::Log}, \
    {"fmt" L "_dynamics", "FORMANT " L " DYNAMICS", "", 0.0f, 100.0f, 50.0f}, \
    {"fmt" L "_pan", "FORMANT " L " PAN", "", -100.0f, 100.0f, 0.0f}, \
    {"fmt" L "_gain", "FORMANT " L " GAIN", "dB", -24.0f, 24.0f, 0.0f}

const ParamInfo kParams[kCount] = {
    {"voice_source", "VOICE SOURCE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kVsN},
    {"voice_gate", "VOICE GATE", "dB", -80.0f, 0.0f, -80.0f},
    {"voice_gain", "VOICE GAIN", "dB", -24.0f, 24.0f, 0.0f},
    {"carrier_source", "CARRIER SOURCE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kCarN},
    {"carrier_drive", "CARRIER DRIVE", "", 0.0f, 100.0f, 0.0f},
    {"carrier_gain", "CARRIER GAIN", "dB", -24.0f, 24.0f, 0.0f},
    {"note_source", "NOTE SOURCE", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kNoteN},
    {"min_note", "MIN NOTE", "", 24.0f, 72.0f, 36.0f},
    {"note_tolerance", "NOTE TOLERANCE", "cents", 0.0f, 100.0f, 30.0f},
    {"arp_on", "ARPEGGIATOR", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"arp_range", "ARP RANGE", "oct", 1.0f, 3.0f, 1.0f},
    {"arp_direction", "ARP DIRECTION", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kDirN},
    {"arp_rate", "ARP RATE", "", 0.0f, 3.0f, 2.0f, Taper::Choice, kRateN},
    {"arp_sync", "ARP SYNC", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"root", "ROOT", "", 0.0f, 11.0f, 0.0f, Taper::Choice, kKeys},
    {"scale", "SCALE", "", 0.0f, 5.0f, 0.0f, Taper::Choice, kScales},
    {"lock", "LOCK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    HEM_ENG("A", 1.0f),
    HEM_ENG("B", 0.0f),
    {"a_to_mix", "A -> MIX", "%", 0.0f, 100.0f, 100.0f},
    {"b_to_mix", "B -> MIX", "%", 0.0f, 100.0f, 100.0f},
    {"routing", "ROUTING", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kRouteN},
    {"voice_level", "VOICE LEVEL", "dB", -60.0f, 6.0f, 0.0f},
    {"synth_level", "SYNTH LEVEL", "dB", -60.0f, 6.0f, -60.0f},
    {"processed_level", "PROCESSED LEVEL", "dB", -60.0f, 6.0f, -60.0f},
    {"voice_tone", "VOICE TONE", "", -100.0f, 100.0f, 0.0f},
    {"synth_tone", "SYNTH TONE", "", -100.0f, 100.0f, 0.0f},
    {"processed_tone", "PROCESSED TONE", "", -100.0f, 100.0f, 0.0f},
    {"limiter", "LIMITER", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"breath", "BREATH", "", 0.0f, 100.0f, 0.0f},
    {"clarity", "CLARITY", "", 0.0f, 100.0f, 0.0f},
    {"correction", "CORRECTION", "%", 0.0f, 100.0f, 0.0f},
    {"harmonics", "HARMONICS", "", 0.0f, 100.0f, 0.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"output", "OUTPUT", "dB", -24.0f, 24.0f, 0.0f},
    {"chorus_on", "CHORUS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"chorus_mix", "CHORUS MIX", "%", 0.0f, 100.0f, 0.0f},
    {"delay_on", "DELAY", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"delay_filter", "DELAY FILTER", "Hz", 200.0f, 20000.0f, 20000.0f, Taper::Log},
    {"delay_time", "DELAY TIME", "ms", 1.0f, 2000.0f, 250.0f, Taper::Log},
    {"delay_sync", "DELAY SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"delay_feedback", "DELAY FEEDBACK", "%", 0.0f, 95.0f, 30.0f},
    {"delay_mix", "DELAY MIX", "%", 0.0f, 100.0f, 0.0f},
    {"reverb_on", "REVERB", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"reverb_size", "REVERB SIZE", "", 0.0f, 100.0f, 50.0f},
    {"reverb_mix", "REVERB MIX", "%", 0.0f, 100.0f, 0.0f},
    {"drive_on", "DRIVE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"drive_amount", "DRIVE AMOUNT", "", 0.0f, 100.0f, 0.0f},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_ENG

const hc::fx::MeterInfo kMeters[] = {
    {"VOICE", "dB", -60, 0}, {"SYNTH", "dB", -60, 0}, {"PROCESSED", "dB", -60, 0}, {"PEAK", "dB", -60, 0},
    {"NOTE", "Hz", 0, 2000}, {"LIMITER", "dB", -24, 0},
};

constexpr int kBandsN = 20, kMaxUnison = 8, kMaxNotes = 4;
constexpr float kLoBand = 100.0f, kHiBand = 8000.0f;

// ---- one synth engine: unison PolyBLEP oscillators -> filter, per playing note ----------------
struct Osc {
    double ph = 0;
    static float blep(double t, double dt) noexcept {
        if (t < dt) { const double x = t / dt; return static_cast<float>(x + x - x * x - 1.0); }
        if (t > 1.0 - dt) { const double x = (t - 1.0) / dt; return static_cast<float>(x * x + x + x + 1.0); }
        return 0.0f;
    }
    // shape 0 saw .. 0.5 square .. 1 narrow pulse; harm blends from a pure sine (0) to the full wave (1)
    float tick(double dt, float shape, float harm) noexcept {
        ph += dt; if (ph >= 1.0) ph -= 1.0;
        const float saw = static_cast<float>(2.0 * ph - 1.0) - blep(ph, dt);
        const double pw = shape <= 0.5f ? 0.5 : 0.5 - 0.45 * (shape - 0.5f) * 2.0;
        float sq = ph < pw ? 1.0f : -1.0f;
        sq += blep(ph, dt);
        double t2 = ph - pw; if (t2 < 0) t2 += 1.0;
        sq -= blep(t2, dt);
        const float mixSq = std::clamp(shape * 2.0f, 0.0f, 1.0f);
        const float wave = saw + mixSq * (sq - saw);
        const float sine = std::sin(kit::kTau * static_cast<float>(ph));
        return sine + harm * (wave - sine);
    }
};

class VoiceCircuit final : public hc::fx::Effect, public hem::rack::NoteSink, public hem::rack::SidechainSink {
public:
    VoiceCircuit() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kMinNote || i == kNoteTol) return p(kNoteMode) > 0.5f;
        if (i >= kArpRange && i <= kArpSync) return p(kArpOn) > 0.5f;
        if (i >= kEngB && i < kEngB + kEngCount) return p(kEngB + eOn) > 0.5f || i == kEngB + eOn;
        if (i == kChorusMix) return p(kChorusOn) > 0.5f;
        if (i >= kDelayFilter && i <= kDelayMix) return p(kDelayOn) > 0.5f && (i != kDelayTime || p(kDelaySync) < 0.5f);
        if (i == kVerbSize || i == kVerbMix) return p(kVerbOn) > 0.5f;
        if (i == kDriveAmt) return p(kDriveOn) > 0.5f;
        return true;
    }

    // NoteSink: up to four held notes play the engines in MIDI mode
    void noteOn(int note, float, int) noexcept override {
        for (auto& h : held_) if (h == note) return;
        for (auto& h : held_) if (h < 0) { h = note; return; }
        held_[0] = note;
    }
    void noteOff(int note, int) noexcept override { for (auto& h : held_) if (h == note) h = -1; }
    void allNotesOff() noexcept override { held_.fill(-1); }
    // SidechainSink: VOICE SOURCE = SIDECHAIN or CARRIER = EXTERNAL read this
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        det_.prepare(fs, 60.0f, 1200.0f, 256);
        const std::size_t mb = std::max<std::uint32_t>(maxBlock, kMaxBlock);
        for (auto* v : {&modL_, &carA_, &carB_, &synA_, &synB_, &procL_, &procR_, &voiceL_, &voiceR_, &dryL_, &dryR_}) v->assign(mb, 0.0f);
        chorusL_.prepare(static_cast<std::uint32_t>(fs * 0.05)); chorusR_.prepare(static_cast<std::uint32_t>(fs * 0.05));
        delayL_.prepare(static_cast<std::uint32_t>(fs * 2.1)); delayR_.prepare(static_cast<std::uint32_t>(fs * 2.1));
        for (int i = 0; i < 4; ++i) {
            combL_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(fs * 0.06));
            combR_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(fs * 0.06));
        }
        for (int i = 0; i < 2; ++i) {
            apL_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(fs * 0.01));
            apR_[static_cast<std::size_t>(i)].prepare(static_cast<std::uint32_t>(fs * 0.01));
        }
        vM_.prepare(fs); sM_.prepare(fs); pM_.prepare(fs); outM_.prepare(fs);
        outG_.prepare(fs, 20.0, kit::dbToGain(p(kOutput)));
        reset();
    }
    void reset() noexcept override {
        det_.reset();
        for (auto& e : eng_) { for (auto& n : e.osc) for (auto& o : n) o = {}; e.filt.clear(); for (auto& b : e.mod) b.clear(); for (auto& b : e.car) b.clear(); e.env.fill(0.0f); }
        for (auto& f : tone_) f = 0.0f;
        chorusL_.clear(); chorusR_.clear(); delayL_.clear(); delayR_.clear();
        for (auto& c : combL_) c.clear();
        for (auto& c : combR_) c.clear();
        for (auto& c : apL_) c.clear();
        for (auto& c : apR_) c.clear();
        combLp_.fill(0.0f); dlLp_[0] = dlLp_[1] = 0.0f;
        curNote_ = -1.0f; lockNote_ = -1.0f; arpStep_ = 0; arpClock_ = 0.0; arpUp_ = true; rng_ = 12345u;
        chorusPh_ = 0.0; limGain_ = 1.0f; limDb_ = 0.0f; clarLp_[0] = clarLp_[1] = 0.0f; gateEnv_ = 0.0f;
        keysA_.fill(1e9f); keysB_.fill(1e9f);
        held_.fill(-1);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        const float* vsL = (p(kVsMode) > 0.5f && scL_) ? scL_ : L;
        const float* vsR = (p(kVsMode) > 0.5f && scR_) ? scR_ : R;
        const bool external = p(kCarMode) > 0.5f && scL_ && !(p(kVsMode) > 0.5f);
        std::copy(L, L + n, dryL_.data()); std::copy(R, R + n, dryR_.data());

        // ---- voice: gain, gate, detector ----------------------------------------------------
        const float vg = kit::dbToGain(p(kVsGain));
        const float gateThr = kit::dbToGain(p(kVsGate));
        const float gateC = hc::fx::onePoleCoef(fs_, 20.0);
        for (std::uint32_t i = 0; i < n; ++i) {
            float m = 0.5f * (vsL[i] + vsR[i]) * vg;
            gateEnv_ = std::fabs(m) + gateC * (gateEnv_ - std::fabs(m));
            if (p(kVsGate) > -79.5f && gateEnv_ < gateThr) m = 0.0f;
            modL_[i] = m;
            det_.push(m);
        }
        updateNotes(n);

        // ---- carriers ------------------------------------------------------------------------
        const float cDrive = 1.0f + p(kCarDrive) * 0.09f;
        const float cGain = kit::dbToGain(p(kCarGain));
        for (int e = 0; e < 2; ++e) {
            auto& syn = e == 0 ? synA_ : synB_;
            auto& car = e == 0 ? carA_ : carB_;
            synth(e, syn.data(), n);
            for (std::uint32_t i = 0; i < n; ++i) {
                float c = external ? 0.5f * (scL_[i] + scR_[i]) : syn[i];
                if (p(kBreath) > 0.0f) c += p(kBreath) * 0.004f * noise();   // breath: noise in the carrier for consonants
                if (cDrive > 1.0f) c = std::tanh(c * cDrive) / std::tanh(cDrive);
                car[i] = c * cGain;
            }
        }

        // ---- vocoders (parallel, or A feeding B in series) -----------------------------------
        std::fill(procL_.begin(), procL_.begin() + n, 0.0f); std::fill(procR_.begin(), procR_.begin() + n, 0.0f);
        const bool series = p(kRoute) > 0.5f;
        const bool aOn = p(kEngA + eOn) > 0.5f, bOn = p(kEngB + eOn) > 0.5f;
        if (aOn) vocode(0, carA_.data(), n, series && bOn ? 0.0f : p(kAMix) * 0.01f);
        if (bOn) {
            if (series && aOn) std::copy(vocodedA_.begin(), vocodedA_.begin() + n, carB_.begin());
            vocode(1, carB_.data(), n, p(kBMix) * 0.01f);
        }

        // ---- mixer: voice / synth / processed, each with level and tone -------------------
        const float vl = kit::dbToGain(p(kVoiceLvl) <= -59.5f ? -200.0f : p(kVoiceLvl));
        const float sl = kit::dbToGain(p(kSynthLvl) <= -59.5f ? -200.0f : p(kSynthLvl));
        const float pl = kit::dbToGain(p(kProcLvl) <= -59.5f ? -200.0f : p(kProcLvl));
        for (std::uint32_t i = 0; i < n; ++i) {
            float voL = tilt(0, L[i], p(kVoiceTone)), voR = tilt(1, R[i], p(kVoiceTone));
            const float synMono = 0.5f * ((aOn ? synA_[i] : 0.0f) + (bOn ? synB_[i] : 0.0f));
            const float sy = tilt(2, synMono, p(kSynthTone));
            float prL = tilt(3, procL_[i], p(kProcTone)), prR = tilt(4, procR_[i], p(kProcTone));
            // clarity: the voice's own top end (above 4 kHz) laid over the processed sound
            if (p(kClarity) > 0.0f) {
                const float c = hc::fx::onePoleCoef(fs_, 0.04);
                clarLp_[0] = L[i] + c * (clarLp_[0] - L[i]); clarLp_[1] = R[i] + c * (clarLp_[1] - R[i]);
                prL += p(kClarity) * 0.01f * (L[i] - clarLp_[0]); prR += p(kClarity) * 0.01f * (R[i] - clarLp_[1]);
            }
            if (p(kHarmonics) > 0.0f) {   // harmonics: saturation on the processed sound
                const float d = 1.0f + p(kHarmonics) * 0.05f;
                prL = std::tanh(prL * d) / std::tanh(d); prR = std::tanh(prR * d) / std::tanh(d);
            }
            voiceL_[i] = voL * vl; voiceR_[i] = voR * vl;
            L[i] = voL * vl + sy * sl + prL * pl;
            R[i] = voR * vl + sy * sl + prR * pl;
        }
        vM_.feed(voiceL_.data(), n); sM_.feed(synA_.data(), n); pM_.feed(procL_.data(), n);

        fxRack(L, R, n);
        if (p(kLimiter) > 0.5f) limiter(L, R, n);

        // ---- utility mix and output ----------------------------------------------------------
        const float mix = p(kMix) * 0.01f, ogT = kit::dbToGain(p(kOutput));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float og = outG_.next(ogT);
            L[i] = kit::sane((dryL_[i] + mix * (L[i] - dryL_[i])) * og);
            R[i] = kit::sane((dryR_[i] + mix * (R[i] - dryR_[i])) * og);
        }
        outM_.feed(L, n);
        publishMeter(0, vM_.db()); publishMeter(1, sM_.db()); publishMeter(2, pM_.db()); publishMeter(3, outM_.db());
        publishMeter(4, curNote_ > 0.0f ? kit::midiToHz(curNote_) : 0.0f); publishMeter(5, limDb_);
        publishReduction(limDb_);
        scL_ = scR_ = nullptr;
    }

private:
    struct Engine {
        std::array<std::array<Osc, kMaxUnison>, kMaxNotes> osc{};
        kit::Svf filt;
        std::array<kit::Svf, kBandsN> mod{}, car{};
        std::array<float, kBandsN> env{};
    };

    float noise() noexcept { rng_ = rng_ * 1664525u + 1013904223u; return static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f; }

    // ---- which notes play: AUTO (the sung pitch, snapped by CORRECTION to ROOT / SCALE), MIDI, LOCK, ARP
    void updateNotes(std::uint32_t n) noexcept {
        if (p(kLock) > 0.5f) { if (lockNote_ < 0.0f) lockNote_ = curNote_; }
        else lockNote_ = -1.0f;
        float base = -1.0f;
        if (lockNote_ >= 0.0f) base = lockNote_;
        else if (p(kNoteMode) < 0.5f) {
            base = static_cast<float>(*std::max_element(held_.begin(), held_.end()));
        } else if (det_.hz() > 0.0f) {
            float m = kit::hzToMidi(det_.hz());
            while (m < p(kMinNote)) m += 12.0f;
            const float snapped = snap(m);
            const float target = m + (snapped - m) * p(kCorrect) * 0.01f;
            // NOTE TOLERANCE: stay on the current note until the voice moves this many cents away
            if (curNote_ < 0.0f || std::fabs(target - curNote_) * 100.0f > p(kNoteTol)) base = target;
            else base = curNote_;
        }
        curNote_ = base;
        // arpeggiator over the chord of the current note (1-3-5 of the scale, RANGE octaves)
        arpNote_ = base;
        if (p(kArpOn) > 0.5f && base > 0.0f) {
            const std::array<float, 4> div{1.0f, 0.5f, 0.25f, 0.125f};
            const double beat = 60.0 / std::max(tempo(), 20.0f);
            const double stepSec = p(kArpSync) > 0.5f ? beat * div[static_cast<std::size_t>(p(kArpRate))] : 0.5 * div[static_cast<std::size_t>(p(kArpRate))];
            arpClock_ += n / fs_;
            if (arpClock_ >= stepSec) { arpClock_ -= stepSec; advanceArp(); }
            const int oct = static_cast<int>(p(kArpRange));
            const int count = 3 * oct;
            const int s = arpStep_ % std::max(count, 1);
            const std::array<int, 3> deg{0, 2, 4};
            arpNote_ = scaleDegree(snap(base), deg[static_cast<std::size_t>(s % 3)]) + 12.0f * static_cast<float>(s / 3);
        }
    }
    void advanceArp() noexcept {
        const int count = 3 * static_cast<int>(p(kArpRange));
        switch (static_cast<int>(p(kArpDir))) {
        case 1: arpStep_ = (arpStep_ + count - 1) % count; break;
        case 2:
            if (arpUp_) { if (++arpStep_ >= count - 1) arpUp_ = false; }
            else { if (--arpStep_ <= 0) arpUp_ = true; }
            arpStep_ = std::clamp(arpStep_, 0, count - 1); break;
        case 3: arpStep_ = static_cast<int>(noise() * 0.5f * static_cast<float>(count) + static_cast<float>(count) * 0.5f) % count; break;
        default: arpStep_ = (arpStep_ + 1) % count; break;
        }
    }
    [[nodiscard]] bool inScale(int midi) const noexcept {
        const int rel = (((midi - static_cast<int>(p(kRoot))) % 12) + 12) % 12;
        return (kScaleMask[static_cast<std::size_t>(p(kScale))] >> rel) & 1;
    }
    [[nodiscard]] float snap(float m) const noexcept {
        const int near = static_cast<int>(std::lround(m));
        for (int d = 0; d <= 6; ++d) { if (inScale(near + d)) return static_cast<float>(near + d); if (inScale(near - d)) return static_cast<float>(near - d); }
        return static_cast<float>(near);
    }
    [[nodiscard]] float scaleDegree(float root, int steps) const noexcept {
        int m = static_cast<int>(root);
        for (int s = 0; s < steps;) { ++m; if (inScale(m)) ++s; }
        return static_cast<float>(m);
    }

    // ---- an engine's oscillators for every playing note -> its filter ------------------------
    void synth(int e, float* out, std::uint32_t n) noexcept {
        Engine& en = eng_[static_cast<std::size_t>(e)];
        const int o = e == 0 ? kEngA : kEngB;
        std::fill(out, out + n, 0.0f);
        if (p(o + eOn) < 0.5f) return;
        std::array<float, kMaxNotes> notes{-1, -1, -1, -1};
        if (p(kNoteMode) < 0.5f && lockNote_ < 0.0f && p(kArpOn) < 0.5f) for (std::size_t k = 0; k < kMaxNotes; ++k) notes[k] = static_cast<float>(held_[k]);
        else notes[0] = arpNote_;
        const int uni = std::clamp(static_cast<int>(p(o + eUnison)), 1, kMaxUnison);
        const float spread = p(o + eSpread) * 0.5f;                      // up to +-50 cents across the unison
        const float shape = p(o + eChar) * 0.01f, harm = p(o + eHarm) * 0.01f;
        const float pwm = (p(o + eX) - 0.5f) * 0.4f;                     // OSC X: shape offset
        const float air = p(o + eY);                                     // OSC Y: noise blended into the tone
        const float tr = p(o + eCoarse) + p(o + eFine) * 0.01f;
        const float g = kit::dbToGain(p(o + eGain)) / std::sqrt(static_cast<float>(uni));
        int playing = 0;
        for (std::size_t k = 0; k < kMaxNotes; ++k) {
            if (notes[k] <= 0.0f) continue;
            ++playing;
            for (int u = 0; u < uni; ++u) {
                const float det = uni == 1 ? 0.0f : spread * (2.0f * static_cast<float>(u) / static_cast<float>(uni - 1) - 1.0f) * 0.01f;
                const double dt = kit::midiToHz(notes[k] + tr + det) / fs_;
                Osc& os = en.osc[k][static_cast<std::size_t>(u)];
                for (std::uint32_t i = 0; i < n; ++i) out[i] += g * os.tick(dt, std::clamp(shape + pwm, 0.0f, 1.0f), harm);
            }
        }
        if (playing == 0) return;
        if (air > 0.0f) for (std::uint32_t i = 0; i < n; ++i) out[i] += air * 0.3f * noise();
        if (p(o + eFilter) < 19900.0f || p(o + eReso) > 0.0f) {
            en.filt.set(fs_, p(o + eFilter), 0.5f + p(o + eReso) * 0.15f);
            for (std::uint32_t i = 0; i < n; ++i) out[i] = en.filt.low(out[i]);
        }
    }

    // ---- 20-band vocoder: the voice's band envelopes shape the carrier's bands ---------------
    void vocode(int e, const float* car, std::uint32_t n, float toMix) noexcept {
        Engine& en = eng_[static_cast<std::size_t>(e)];
        const int o = e == 0 ? kEngA : kEngB;
        auto& keys = e == 0 ? keysA_ : keysB_;
        const float shift = p(o + fShift), focus = p(o + fFocus);
        if (keys[0] != shift || keys[1] != focus) {
            keys[0] = shift; keys[1] = focus;
            const float q = 2.0f + focus * 0.1f;                         // FOCUS: band sharpness (Q 2 .. 12)
            for (int b = 0; b < kBandsN; ++b) {
                const float hz = kLoBand * std::pow(kHiBand / kLoBand, static_cast<float>(b) / (kBandsN - 1));
                en.car[static_cast<std::size_t>(b)].set(fs_, hz, q);
                en.mod[static_cast<std::size_t>(b)].set(fs_, hz * std::exp2(-shift / 12.0f), q);   // SHIFT: read the voice lower / higher
            }
        }
        const float rel = hc::fx::onePoleCoef(fs_, p(o + fSpeed)), att = hc::fx::onePoleCoef(fs_, 1.0);
        const float gamma = std::exp2((p(o + fDyn) - 50.0f) / 25.0f);    // DYNAMICS: 0.25 .. 4 (50 = as sung)
        const float g = kit::dbToGain(p(o + fGain)) * 4.0f;             // band-sum makeup (ASSUMED)
        const float pan = p(o + fPan) * 0.01f;
        const float gl = std::cos((pan + 1.0f) * 0.25f * kit::kPi) * 1.41421356f, gr = std::sin((pan + 1.0f) * 0.25f * kit::kPi) * 1.41421356f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float y = 0.0f;
            for (int b = 0; b < kBandsN; ++b) {
                const auto bi = static_cast<std::size_t>(b);
                const float mv = std::fabs(en.mod[bi].band(modL_[i]));
                float& ev = en.env[bi];
                ev = mv + (mv > ev ? att : rel) * (ev - mv);
                const float shaped = gamma == 1.0f ? ev : std::pow(ev, gamma);
                y += en.car[bi].band(car[i]) * shaped;
            }
            y *= g;
            if (e == 0) vocodedA_[i] = y;
            procL_[i] += toMix * y * gl; procR_[i] += toMix * y * gr;
        }
    }

    float tilt(int k, float x, float t) noexcept {   // TONE: -100 dark .. +100 bright, pivot ~800 Hz
        if (t == 0.0f) return x;
        const float c = hc::fx::onePoleCoef(fs_, 0.2);
        float& lp = tone_[static_cast<std::size_t>(k)];
        lp = x + c * (lp - x);
        const float a = t * 0.01f;
        return lp * (1.0f - 0.5f * a) + (x - lp) * (1.0f + 0.5f * a);
    }

    // ---- FX rack: 1 chorus, 2 delay, 3 reverb, 4 drive ---------------------------------------
    void fxRack(float* L, float* R, std::uint32_t n) noexcept {
        if (p(kChorusOn) > 0.5f && p(kChorusMix) > 0.0f) {
            const float mix = p(kChorusMix) * 0.01f;
            for (std::uint32_t i = 0; i < n; ++i) {
                chorusPh_ += 0.8 / fs_; if (chorusPh_ >= 1.0) chorusPh_ -= 1.0;
                const float lfo = std::sin(kit::kTau * static_cast<float>(chorusPh_));
                const float base = static_cast<float>(fs_ * 0.015), depth = static_cast<float>(fs_ * 0.004);
                chorusL_.write(L[i]); chorusR_.write(R[i]);
                const float wl = chorusL_.read(base + depth * lfo), wr = chorusR_.read(base - depth * lfo);
                L[i] += mix * (wl - L[i]); R[i] += mix * (wr - R[i]);
            }
        }
        if (p(kDelayOn) > 0.5f && p(kDelayMix) > 0.0f) {
            float ms = p(kDelayTime);
            if (p(kDelaySync) > 0.5f) {   // snapped to the nearest 1/16 of the song's beat
                const float sixteenth = 15000.0f / std::max(tempo(), 20.0f);
                ms = std::max(sixteenth, std::round(ms / sixteenth) * sixteenth);
            }
            const float d = std::min(static_cast<float>(fs_) * ms * 0.001f, static_cast<float>(fs_ * 2.0));
            const float fb = p(kDelayFb) * 0.01f, mix = p(kDelayMix) * 0.01f;
            const float c = static_cast<float>(std::exp(-2.0 * std::numbers::pi * p(kDelayFilter) / fs_));
            for (std::uint32_t i = 0; i < n; ++i) {
                const float tl = delayL_.read(d), tr = delayR_.read(d);
                dlLp_[0] = tl + c * (dlLp_[0] - tl); dlLp_[1] = tr + c * (dlLp_[1] - tr);
                delayL_.write(L[i] + fb * dlLp_[0]); delayR_.write(R[i] + fb * dlLp_[1]);
                L[i] += mix * dlLp_[0]; R[i] += mix * dlLp_[1];
            }
        }
        if (p(kVerbOn) > 0.5f && p(kVerbMix) > 0.0f) {
            // a small Schroeder room of its own: four damped combs and two allpasses per side
            static constexpr std::array<float, 4> kCombMs{29.7f, 37.1f, 41.1f, 43.7f};
            static constexpr std::array<float, 2> kApMs{5.0f, 1.7f};
            const float size = p(kVerbSize) * 0.01f, fb = 0.7f + 0.27f * size, mix = p(kVerbMix) * 0.01f;
            for (std::uint32_t i = 0; i < n; ++i) {
                float sl = 0, sr = 0;
                for (std::size_t c = 0; c < 4; ++c) {
                    const float dl = static_cast<float>(fs_) * kCombMs[c] * 0.001f, dr = dl * 1.023f;
                    const float yl = combL_[c].read(dl), yr = combR_[c].read(dr);
                    combLp_[c] = yl + 0.3f * (combLp_[c] - yl);
                    combL_[c].write(L[i] + fb * combLp_[c]); combR_[c].write(R[i] + fb * yr);
                    sl += yl; sr += yr;
                }
                for (std::size_t a = 0; a < 2; ++a) {
                    const float d = static_cast<float>(fs_) * kApMs[a] * 0.001f;
                    const float bl = apL_[a].read(d), br = apR_[a].read(d);
                    apL_[a].write(sl + 0.5f * bl); apR_[a].write(sr + 0.5f * br);
                    sl = bl - 0.5f * sl; sr = br - 0.5f * sr;
                }
                L[i] += mix * 0.25f * sl; R[i] += mix * 0.25f * sr;
            }
        }
        if (p(kDriveOn) > 0.5f && p(kDriveAmt) > 0.0f) {
            const float d = 1.0f + p(kDriveAmt) * 0.15f, norm = 1.0f / std::tanh(d);
            for (std::uint32_t i = 0; i < n; ++i) { L[i] = std::tanh(L[i] * d) * norm; R[i] = std::tanh(R[i] * d) * norm; }
        }
    }

    void limiter(float* L, float* R, std::uint32_t n) noexcept {
        const float ceil = kit::dbToGain(-0.3f), rel = hc::fx::onePoleCoef(fs_, 100.0);
        float worst = 1.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float pk = std::max(std::fabs(L[i]), std::fabs(R[i]));
            const float need = pk * limGain_ > ceil ? ceil / pk : 1.0f;
            limGain_ = need < limGain_ ? need : 1.0f + rel * (limGain_ - 1.0f);
            limGain_ = std::min(limGain_, need);
            L[i] *= limGain_; R[i] *= limGain_;
            worst = std::min(worst, limGain_);
        }
        limDb_ = kit::gainToDb(worst);
    }

    double fs_ = 48000;
    kit::PitchDetector det_;
    std::array<Engine, 2> eng_{};
    std::vector<float> modL_, carA_, carB_, synA_, synB_, procL_, procR_, voiceL_, voiceR_, dryL_, dryR_;
    std::array<float, hc::fx::Effect::kMaxBlock> vocodedA_{};
    std::array<int, kMaxNotes> held_{-1, -1, -1, -1};
    const float* scL_ = nullptr; const float* scR_ = nullptr;
    float curNote_ = -1, lockNote_ = -1, arpNote_ = -1, gateEnv_ = 0, limGain_ = 1, limDb_ = 0;
    int arpStep_ = 0; bool arpUp_ = true; double arpClock_ = 0, chorusPh_ = 0;
    std::uint32_t rng_ = 12345u;
    std::array<float, 5> tone_{};
    std::array<float, 2> keysA_{}, keysB_{}, clarLp_{}, dlLp_{};
    hc::fx::DelayLine chorusL_, chorusR_, delayL_, delayR_;
    std::array<hc::fx::DelayLine, 4> combL_, combR_;
    std::array<hc::fx::DelayLine, 2> apL_, apR_;
    std::array<float, 4> combLp_{};
    kit::PeakMeter vM_, sM_, pM_, outM_;
    hc::fx::Smoothed outG_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeVoiceCircuit() { return std::make_unique<VoiceCircuit>(); }

} // namespace hem::staging
