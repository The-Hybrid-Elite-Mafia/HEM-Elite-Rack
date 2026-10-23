// THE INFORMANT — vocoder with its own eight-voice synth, noise, morph EQ and a four-way mixer (see Hideout.hpp).
//
// The modulator (after PRESSURE) runs through 32 analysis bands, placed FORMANT times lower or higher than the
// synthesis bands. The carrier (stereo) and the noise run through the synthesis bands; each band is turned to the
// level the modulator has there (the band's own level divided out first, so the voice's shape comes through
// whatever the carrier is). When the modulator stops, the last shape is held for RELEASE.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace informant {
namespace {
using namespace hideout;

const char* const kModN[] = {"L+R", "LEFT", "RIGHT"};
const char* const kCarN[] = {"SYNTH", "LEFT", "RIGHT", "STEREO"};
const char* const kNoiseN[] = {"OFF", "HI-PASS", "LO-PASS"};
const char* const kSoundN[] = {"WARM PAD", "BRIGHT SAW", "HOLLOW", "REED", "BUZZ", "GLASS", "CHOIR", "SUB BASS", "AIR", "GRIT"};
const char* const kSustainN[] = {"HOLD", "LATCH"};

#define HEM_EQ(n, hz, q)                                                       \
    {"eq" #n "on", "EQ " #n " On", "", 0, 1, 1, Taper::Toggle, kOnOff},        \
    {"eq" #n "freq", "EQ " #n " Freq", "Hz", 86.0f, 16020.0f, hz, Taper::Log}, \
    {"eq" #n "gain", "EQ " #n " Gain", "dB", -30.0f, 30.0f, 0.0f},             \
    {"eq" #n "q", "EQ " #n " Q", "", 0.26f, 6.5f, q, Taper::Log}
#define HEM_KEY(id, name) {id, name, "", 0, 1, 0, Taper::Toggle, kOnOff}
const ParamInfo kParams[kCount] = {
    {"modulator", "Modulator", "", 0, 2, 0, Taper::Choice, kModN},
    {"carrier", "Carrier", "", 0, 3, 0, Taper::Choice, kCarN},
    {"noisefilter", "Noise Filter", "", 0, 2, 0, Taper::Choice, kNoiseN},
    {"noisefreq", "Noise Freq", "Hz", 16.0f, 16000.0f, 250.0f, Taper::Log},
    {"sound", "Sound", "", 0, 9, 0, Taper::Choice, kSoundN},
    {"tune", "Tune", "Hz", 220.0f, 880.0f, 440.0f},
    {"spread", "Spread", "", 0.0f, 1.0f, 0.0f},
    HEM_EQ(1, 258.0f, 2.0f), HEM_EQ(2, 689.0f, 1.0f), HEM_EQ(3, 1804.0f, 1.0f), HEM_EQ(4, 4478.0f, 1.0f), HEM_EQ(5, 11046.0f, 2.0f),
    {"pressure", "Squeeze", "dB", 0.0f, 50.0f, 20.0f},
    {"formant", "Formant", "x", 0.5f, 2.0f, 1.0f, Taper::Log},
    {"smoothing", "Lag", "", 60.0f, 800.0f, 60.0f, Taper::Log},
    {"release", "Hold", "ms", 50.0f, 5000.0f, 100.0f, Taper::Log},
    {"carrierlevel", "Carrier Level", "dB", -60.0f, 0.0f, -60.0f},
    {"modulatorlevel", "Modulator Level", "dB", -60.0f, 0.0f, -60.0f},
    {"noiselevel", "Noise Level", "dB", -60.0f, 0.0f, -14.0f},
    {"morphlevel", "Vocoded Level", "dB", -60.0f, 6.0f, 0.0f},
    {"carriersolo", "Carrier Solo", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"modulatorsolo", "Modulator Solo", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"noisesolo", "Noise Solo", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"morphsolo", "Vocoded Solo", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"carriermute", "Carrier Mute", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"modulatormute", "Modulator Mute", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"noisemute", "Noise Mute", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"morphmute", "Vocoded Mute", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"sustain", "Latch", "", 0, 1, 0, Taper::Toggle, kSustainN},
    HEM_KEY("key0", "Key C2"), HEM_KEY("key1", "Key C#2"), HEM_KEY("key2", "Key D2"), HEM_KEY("key3", "Key D#2"),
    HEM_KEY("key4", "Key E2"), HEM_KEY("key5", "Key F2"), HEM_KEY("key6", "Key F#2"), HEM_KEY("key7", "Key G2"),
    HEM_KEY("key8", "Key G#2"), HEM_KEY("key9", "Key A2"), HEM_KEY("key10", "Key A#2"), HEM_KEY("key11", "Key B2"),
    HEM_KEY("key12", "Key C3"), HEM_KEY("key13", "Key C#3"), HEM_KEY("key14", "Key D3"), HEM_KEY("key15", "Key D#3"),
    HEM_KEY("key16", "Key E3"), HEM_KEY("key17", "Key F3"), HEM_KEY("key18", "Key F#3"), HEM_KEY("key19", "Key G3"),
    HEM_KEY("key20", "Key G#3"), HEM_KEY("key21", "Key A3"), HEM_KEY("key22", "Key A#3"), HEM_KEY("key23", "Key B3"),
    HEM_KEY("key24", "Key C4"), HEM_KEY("key25", "Key C#4"), HEM_KEY("key26", "Key D4"), HEM_KEY("key27", "Key D#4"),
    HEM_KEY("key28", "Key E4"), HEM_KEY("key29", "Key F4"), HEM_KEY("key30", "Key F#4"), HEM_KEY("key31", "Key G4"),
    HEM_KEY("key32", "Key G#4"), HEM_KEY("key33", "Key A4"), HEM_KEY("key34", "Key A#4"), HEM_KEY("key35", "Key B4"),
    HEM_KEY("key36", "Key C5"), HEM_KEY("key37", "Key C#5"), HEM_KEY("key38", "Key D5"), HEM_KEY("key39", "Key D#5"),
    HEM_KEY("key40", "Key E5"), HEM_KEY("key41", "Key F5"), HEM_KEY("key42", "Key F#5"), HEM_KEY("key43", "Key G5"),
    HEM_KEY("key44", "Key G#5"), HEM_KEY("key45", "Key A5"), HEM_KEY("key46", "Key A#5"), HEM_KEY("key47", "Key B5"),
};
#undef HEM_EQ
#undef HEM_KEY
const hc::fx::MeterInfo kMeters[] = {{"OUT L", "dBFS", -60.0f, 3.0f}, {"OUT R", "dBFS", -60.0f, 3.0f}};

constexpr int kBands = 32, kVoices = 8;
constexpr double kLowHz = 80.0, kHighHz = 12000.0;
constexpr float kMakeup = 4.0f;   // the narrow bands leave gaps between them: brings a voice back to about its own level

// The ten sounds: oscillator levels, detune, pulse width, sub octave, noise, drive, brightness, envelope.
struct SoundDef { float saw, saw2, saw3, pulse, pw, detune, sub, noise, drive, lowpass, attMs, relMs; };
constexpr SoundDef kSounds[10] = {
    {0.6f, 0.6f, 0.0f, 0.0f, 0.5f, 12.0f, 0.2f, 0.0f, 0.0f, 3000.0f, 80.0f, 300.0f},    // WARM PAD
    {1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 12000.0f, 5.0f, 80.0f},      // BRIGHT SAW
    {0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 6000.0f, 5.0f, 100.0f},      // HOLLOW
    {0.0f, 0.0f, 0.0f, 0.9f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 5000.0f, 10.0f, 120.0f},     // REED
    {0.7f, 0.0f, 0.0f, 0.5f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 16000.0f, 2.0f, 80.0f},      // BUZZ
    {0.3f, 0.3f, 0.0f, 0.6f, 0.35f, 7.0f, 0.0f, 0.0f, 0.0f, 9000.0f, 2.0f, 400.0f},     // GLASS
    {0.5f, 0.5f, 0.5f, 0.0f, 0.5f, 15.0f, 0.0f, 0.05f, 0.0f, 4500.0f, 120.0f, 400.0f},  // CHOIR
    {0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.9f, 0.0f, 0.0f, 1800.0f, 5.0f, 120.0f},      // SUB BASS
    {0.3f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 10000.0f, 40.0f, 300.0f},    // AIR
    {1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 3.0f, 14000.0f, 2.0f, 80.0f},      // GRIT
};

inline float blep(float t, float dt) noexcept {
    if (t < dt) { const float x = t / dt; return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}
inline float sawAt(float t, float dt) noexcept { return 2.0f * t - 1.0f - blep(t, dt); }
inline void advance(float& t, float dt) noexcept {
    t += dt;
    if (t >= 1.0f) t -= 1.0f;
}

struct Voice {
    int key = -1;            // -1: free
    bool held = false;       // false: releasing
    float level = 0.0f, ph[4]{}, panL = 0.7071f, panR = 0.7071f;
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == NoiseFreq) return pick(NoiseFilter, 2) > 0;
        if (i == Sound || i == Tune || i == Spread || i == Sustain || i >= Key0) return pick(Carrier, 3) == 0;
        if (i >= Eq0 && i < Pressure && (i - Eq0) % kEqStride != EqOn) return p(i - (i - Eq0) % kEqStride) >= 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        reset();
    }
    void reset() noexcept override {
        for (auto& b : ana_) for (auto& f : b) f.clear();
        for (auto& c : syn_) for (auto& b : c) for (auto& f : b) f.clear();
        for (auto& c : morphEq_) for (auto& f : c) f.clear();
        for (auto& f : synthTone_) f.clear();
        for (auto& f : noiseTone_) f.clear();
        for (int k = 0; k < kBands; ++k) {
            env_[k] = loudEnv_[k] = 0.0f;
            for (auto& c : cenv_) c[k] = 0.0f;
        }
        for (auto& v : voice_) v = Voice{};
        keyVoice_.fill(-1);
        pressEnv_ = modEnv_ = 0.0f;
        quiet_ = 0;
        noise_ = 22222u;
        key_.fill(-1e9f);
        design();
        for (int q = 0; q < kPaths; ++q) lvl_[q].prepare(fs_, 20, pathGain(q));
        meterHold_[0].clear();
        meterHold_[1].clear();
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        playKeys();
        const int mod = pick(Modulator, 2), car = pick(Carrier, 3), nf = pick(NoiseFilter, 2);
        const SoundDef& sd = kSounds[pick(Sound, 9)];
        const float pressG = hc::fx::dbToGain(p(Pressure));
        const float pAtt = coefMs(fs_, 1.0), pRel = coefMs(fs_, 80.0);
        const float sm = p(Smoothing);
        const float att = coefMs(fs_, std::max(0.5, sm / 60.0)), rel = coefMs(fs_, sm / 10.0), crel = coefMs(fs_, 10.0);
        const bool holdForever = p(Release) >= 4999.0f;
        const auto holdFor = static_cast<std::uint32_t>(p(Release) * 0.001f * static_cast<float>(fs_));
        const float qAtt = coefMs(fs_, 2.0), qRel = coefMs(fs_, 30.0), keep = coefMs(fs_, 300.0);
        float wL[kPaths];
        for (int q = 0; q < kPaths; ++q) wL[q] = pathGain(q);
        // the synth's oscillators
        const float tune = p(Tune);
        float dt[kVoices]{}, sub[kVoices]{};
        for (int v = 0; v < kVoices; ++v) {
            if (voice_[v].key < 0) continue;
            const float hz = tune * std::exp2((static_cast<float>(kLowKey + voice_[v].key) - 69.0f) / 12.0f);
            dt[v] = std::min(hz / static_cast<float>(fs_), 0.45f);
            sub[v] = dt[v] * 0.5f;
        }
        const float up = std::exp2(sd.detune / 1200.0f), down = 1.0f / up;
        const float aStep = 1.0f / std::max(1.0f, sd.attMs * 0.001f * static_cast<float>(fs_));
        const float rCoef = coefMs(fs_, sd.relMs / 6.9);   // to -60 dB in relMs
        const float oscNorm = 0.35f;
        float pkL = 0.0f, pkR = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float inL = L[i], inR = R[i];
            const float m = mod == 0 ? 0.5f * (inL + inR) : mod == 1 ? inL : inR;
            // PRESSURE: lift quiet words, hold the loud ones in (-20 dBFS, 4:1, then a soft ceiling)
            float x = m * pressG;
            const float ax = std::fabs(x);
            pressEnv_ = ax + (ax > pressEnv_ ? pAtt : pRel) * (pressEnv_ - ax);
            if (pressEnv_ > 0.1f) x *= std::pow(pressEnv_ / 0.1f, -0.75f);
            x = 0.9f * std::tanh(x / 0.9f);
            // the carrier
            float cL = 0.0f, cR = 0.0f;
            if (car == 0) {
                for (int v = 0; v < kVoices; ++v) {
                    Voice& vo = voice_[v];
                    if (vo.key < 0) continue;
                    if (vo.held) vo.level = std::min(1.0f, vo.level + aStep);
                    else {
                        vo.level *= rCoef;
                        if (vo.level < 1e-4f) { free(v); continue; }
                    }
                    float s = 0.0f;
                    if (sd.saw > 0.0f) s += sd.saw * sawAt(vo.ph[0], dt[v]);
                    if (sd.saw2 > 0.0f) s += sd.saw2 * sawAt(vo.ph[1], dt[v] * up);
                    if (sd.saw3 > 0.0f) s += sd.saw3 * sawAt(vo.ph[2], dt[v] * down);
                    if (sd.pulse > 0.0f) {
                        float t2 = vo.ph[0] + sd.pw;
                        if (t2 >= 1.0f) t2 -= 1.0f;
                        s += sd.pulse * 0.5f * (sawAt(vo.ph[0], dt[v]) - sawAt(t2, dt[v]));
                    }
                    if (sd.sub > 0.0f) {
                        float t2 = vo.ph[3] + 0.5f;
                        if (t2 >= 1.0f) t2 -= 1.0f;
                        s += sd.sub * 0.5f * (sawAt(vo.ph[3], sub[v]) - sawAt(t2, sub[v]));
                    }
                    if (sd.noise > 0.0f) s += sd.noise * white();
                    if (sd.drive > 0.0f) s = std::tanh(sd.drive * s) / std::tanh(sd.drive);
                    advance(vo.ph[0], dt[v]);
                    advance(vo.ph[1], dt[v] * up);
                    advance(vo.ph[2], dt[v] * down);
                    advance(vo.ph[3], sub[v]);
                    s *= vo.level * oscNorm;
                    cL += s * vo.panL;
                    cR += s * vo.panR;
                }
                cL = synthTone_[0].tick(cL);
                cR = synthTone_[1].tick(cR);
            } else {
                cL = car == 2 ? inR : inL;
                cR = car == 1 ? inL : inR;
            }
            // the noise
            float nz = white() * 0.5f;
            if (nf > 0) nz = noiseTone_[1].tick(noiseTone_[0].tick(nz));
            // is the modulator quiet? then hold the last shape for RELEASE
            const float aq = std::fabs(x);
            modEnv_ = aq + (aq > modEnv_ ? qAtt : qRel) * (modEnv_ - aq);
            if (modEnv_ < 0.003f) { if (quiet_ < 0xFFFFFFF0u) ++quiet_; }
            else quiet_ = 0;
            const bool hold = quiet_ > 0 && (holdForever || quiet_ < holdFor);
            // the bands
            float yL = 0.0f, yR = 0.0f, yN = 0.0f;
            for (int k = 0; k < kBands; ++k) {
                if (!hold) {
                    const float a = std::fabs(ana_[k][1].tick(ana_[k][0].tick(x)));
                    env_[k] = a + (a > env_[k] ? att : rel) * (env_[k] - a);
                    loudEnv_[k] = std::max(env_[k], loudEnv_[k] * keep);   // the recent shape a pause will hold
                } else {
                    (void)ana_[k][1].tick(ana_[k][0].tick(x));   // keep the filters running
                    env_[k] = loudEnv_[k];
                }
                const float sL = syn_[0][k][1].tick(syn_[0][k][0].tick(cL));
                const float sR = syn_[1][k][1].tick(syn_[1][k][0].tick(cR));
                const float sN = syn_[2][k][1].tick(syn_[2][k][0].tick(nz));
                const float in3[3] = {sL, sR, sN};
                float g3[3];
                for (int c = 0; c < 3; ++c) {
                    const float sx = std::fabs(in3[c]);
                    cenv_[c][k] = sx + (sx > cenv_[c][k] ? att : crel) * (cenv_[c][k] - sx);
                    g3[c] = std::min(env_[k] / std::max(cenv_[c][k], 1e-4f), 30.0f);
                }
                yL += sL * g3[0];
                yR += sR * g3[1];
                yN += sN * g3[2];
            }
            yL *= kMakeup;
            yR *= kMakeup;
            yN *= kMakeup;
            for (auto& f : morphEq_[0]) yL = f.tick(yL);
            for (auto& f : morphEq_[1]) yR = f.tick(yR);
            // the mixer
            const float gc = lvl_[CarrierPath].next(wL[CarrierPath]), gm = lvl_[ModPath].next(wL[ModPath]);
            const float gn = lvl_[NoisePath].next(wL[NoisePath]), gx = lvl_[MorphPath].next(wL[MorphPath]);
            const float mL = mod == 0 ? inL : m, mR = mod == 0 ? inR : m;
            const float oL = gc * cL + gm * mL + gn * yN + gx * yL;
            const float oR = gc * cR + gm * mR + gn * yN + gx * yR;
            L[i] = oL;
            R[i] = oR;
            pkL = std::max(pkL, std::fabs(oL));
            pkR = std::max(pkR, std::fabs(oR));
        }
        publishMeter(0, meterHold_[0].push(std::max(dbOf(pkL), -100.0f), n, fs_));
        publishMeter(1, meterHold_[1].push(std::max(dbOf(pkR), -100.0f), n, fs_));
    }

private:
    int pick(int i, int hi) const noexcept { return std::clamp(static_cast<int>(std::lround(p(i))), 0, hi); }
    float white() noexcept {
        noise_ = noise_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(noise_)) * (1.0f / 2147483648.0f);
    }
    // a path's level: off when muted, or when another path is soloed and this one isn't; -60 dB = off
    float pathGain(int q) const noexcept {
        bool anySolo = false;
        for (int k = 0; k < kPaths; ++k) anySolo = anySolo || p(Solo0 + k) >= 0.5f;
        if (p(Mute0 + q) >= 0.5f || (anySolo && p(Solo0 + q) < 0.5f)) return 0.0f;
        const float db = p(Level0 + q);
        return db <= -59.95f ? 0.0f : hc::fx::dbToGain(db);
    }
    void free(int v) noexcept {
        Voice& vo = voice_[v];
        if (vo.key >= 0 && keyVoice_[vo.key] == v) keyVoice_[vo.key] = -1;
        vo = Voice{};
    }
    // The keys: a newly held key takes a free voice (none free: it is ignored until let go and pressed again);
    // a let-go key's voice fades out. SPREAD places the held voices across the stereo, low to high.
    void playKeys() noexcept {
        const bool synth = pick(Carrier, 3) == 0;
        for (int k = 0; k < kKeys; ++k) {
            const bool on = synth && p(Key0 + k) >= 0.5f;
            const int v = keyVoice_[k];
            if (on && v == -1) {
                int freeV = -1;
                for (int i = 0; i < kVoices && freeV < 0; ++i)
                    if (voice_[i].key < 0) freeV = i;
                if (freeV >= 0) {
                    Voice& vo = voice_[freeV];
                    vo = Voice{};
                    vo.key = k;
                    vo.held = true;
                    keyVoice_[k] = freeV;
                } else {
                    keyVoice_[k] = -2;   // ignored while held
                }
            } else if (!on && v != -1) {
                if (v >= 0) voice_[v].held = false;
                keyVoice_[k] = -1;
            }
        }
        int held[kVoices], nh = 0;
        for (int k = 0; k < kKeys; ++k)
            if (keyVoice_[k] >= 0) held[nh++] = keyVoice_[k];
        const float spread = p(Spread);
        for (int j = 0; j < nh; ++j) {
            const float pos = nh > 1 ? spread * (2.0f * static_cast<float>(j) / static_cast<float>(nh - 1) - 1.0f) : 0.0f;
            const float th = (pos + 1.0f) * 0.25f * std::numbers::pi_v<float>;
            voice_[held[j]].panL = std::cos(th);
            voice_[held[j]].panR = std::sin(th);
        }
    }
    void design() noexcept {
        std::array<float, 32> now{};
        now[0] = p(Formant);
        now[1] = p(Sound);
        now[2] = p(NoiseFilter);
        now[3] = p(NoiseFreq);
        for (int i = 0; i < kEqBands * kEqStride; ++i) now[static_cast<std::size_t>(4 + i)] = p(Eq0 + i);
        if (now == key_) return;
        const bool bandsMoved = now[0] != key_[0];
        key_ = now;
        const double top = fs_ * 0.45;
        const double ratio = std::pow(kHighHz / kLowHz, 1.0 / (kBands - 1));
        const double q = std::sqrt(ratio) / (ratio - 1.0);
        const double formant = p(Formant);
        for (int b = 0; b < kBands; ++b) {
            const double f = kLowHz * std::pow(ratio, b);
            // FORMANT x2: the shape heard at f is the one the words had at f / 2 (moved up)
            const C a = C::make(T::BandPass, fs_, std::clamp(f / formant, 20.0, top), q), s = C::make(T::BandPass, fs_, std::min(f, top), q);
            ana_[b][0].c = ana_[b][1].c = a;
            for (auto& c : syn_) c[b][0].c = c[b][1].c = s;
        }
        if (bandsMoved) for (auto& b : ana_) for (auto& f : b) f.clear();
        const SoundDef& sd = kSounds[pick(Sound, 9)];
        synthTone_[0].c = synthTone_[1].c = C::make(T::LowPass, fs_, std::min<double>(sd.lowpass, top), 0.7071);
        const int nf = pick(NoiseFilter, 2);
        if (nf > 0) {
            const T t = nf == 1 ? T::HighPass : T::LowPass;
            noiseTone_[0].c = noiseTone_[1].c = C::make(t, fs_, std::min<double>(p(NoiseFreq), top), 0.7071);
        }
        for (int b = 0; b < kEqBands; ++b) {
            C c{};
            if (p(eq(b, EqOn)) >= 0.5f) {
                const double hz = std::min<double>(p(eq(b, EqFreq)), top), g = p(eq(b, EqGain)), qq = p(eq(b, EqQ));
                if (b == 0) c = C::make(T::LowShelf, fs_, hz, 0.7071 * std::sqrt(qq / 2.0), g);
                else if (b == kEqBands - 1) c = C::make(T::HighShelf, fs_, hz, 0.7071 * std::sqrt(qq / 2.0), g);
                else c = C::make(T::Bell, fs_, hz, qq, g);
            }
            morphEq_[0][b].c = morphEq_[1][b].c = c;
        }
    }

    double fs_ = 48000.0;
    hc::fx::Biquad ana_[kBands][2], syn_[3][kBands][2], morphEq_[2][kEqBands], synthTone_[2], noiseTone_[2];
    float env_[kBands]{}, loudEnv_[kBands]{}, cenv_[3][kBands]{};
    Voice voice_[kVoices];
    std::array<int, kKeys> keyVoice_{};
    float pressEnv_ = 0.0f, modEnv_ = 0.0f;
    maxim::PeakHold meterHold_[2];
    std::uint32_t quiet_ = 0, noise_ = 22222u;
    std::array<float, 32> key_{};
    hc::fx::Smoothed lvl_[kPaths];
};

} // namespace
} // namespace informant

std::unique_ptr<hc::fx::Effect> makeInformant() { return std::make_unique<informant::Impl>(); }

} // namespace hem::rack
