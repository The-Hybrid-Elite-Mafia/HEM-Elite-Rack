// PAWN SHOP — lo-fi box: DEVICE, SPACE, NOISE, MECHANICS and the master section (see Garage.hpp).
//
// In order: INPUT; DEVICE (the band of the SOUND morphed by TONE, SQUASH, RINGER, the DECADE's band, limiting
// and distortion; MIX with what came in); NOISE when its PLACE is PRE; SPACE (echo and reverb in parallel,
// SPACE MIX); NOISE when POST; MECHANICS (a moving delay for WOW, a moving level for WOBBLE, sets A and B);
// LOW CUT, HIGH CUT, MONO, OUTPUT. A module that is off passes the sound through untouched.
// The latency is always kPawnLatency (the moving delay's centre), whether MECHANICS is on or not.
#include "hem/rack/Garage.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace pawnshop {
namespace {
using namespace hideout;

const char* const kDeviceN[] = {"AIRY", "VINTAGE", "DEEP", "FLAT"};
const char* const kDecadeN[] = {"50s", "60s", "70s", "80s"};
const char* const kSyncN[] = {"BPM", "HOST", "MS"};
const char* const kNoteN[] = {"1/1", "1/2", "1/2 D", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 T", "1/32"};
const char* const kVerbN[] = {"SPRING", "PLATE"};
const char* const kNoiseN[] = {"TAPE HISS", "WHITE", "PINK", "RUMBLE", "CRACKLE", "DUST", "HUM 50", "HUM 60", "RADIO", "RAIN", "FIRE", "WIND"};
const char* const kModeN[] = {"DUCK", "GATE"};
const char* const kPlaceN[] = {"PRE", "POST"};
const ParamInfo kParams[kCount] = {
    {"deviceon", "Device", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"device", "Sound", "", 0, 3, 1, Taper::Choice, kDeviceN},
    {"tone", "Tone", "", 0.0f, 100.0f, 50.0f},
    {"squash", "Squash", "", -10.0f, 10.0f, 0.0f},
    {"ringer", "Ringer", "", 0.0f, 100.0f, 0.0f},
    {"styler", "Decade", "", 0, 3, 1, Taper::Choice, kDecadeN},
    {"devicemix", "Device Mix", "%", 0.0f, 100.0f, 100.0f},
    {"spaceon", "Space", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"sync", "Sync", "", 0, 2, 2, Taper::Choice, kSyncN},
    {"time", "Time", "ms", 1.0f, 3500.0f, 250.0f, Taper::Log},
    {"note", "Note", "", 0, 11, 6, Taper::Choice, kNoteN},
    {"bpm", "BPM", "", 40.0f, 240.0f, 120.0f},
    {"echolevel", "Echo", "", 0.0f, 100.0f, 50.0f},
    {"pingpong", "Ping Pong", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"feedback", "Feedback", "", 0.0f, 100.0f, 30.0f},
    {"reverblevel", "Reverb Level", "", 0.0f, 100.0f, 40.0f},
    {"reverblength", "Reverb Length", "", 0.0f, 100.0f, 40.0f},
    {"reverbtype", "Reverb", "", 0, 1, 0, Taper::Choice, kVerbN},
    {"spacemix", "Space Mix", "%", 0.0f, 100.0f, 30.0f},
    {"noiseon", "Noise", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"noisetype", "Noise Type", "", 0, kNoises - 1, 0, Taper::Choice, kNoiseN},
    {"noisemode", "Noise Follows", "", 0, 1, 0, Taper::Choice, kModeN},
    {"threshold", "Threshold", "dBFS", -60.0f, 0.0f, -30.0f},
    {"noiseplace", "Place", "", 0, 1, 1, Taper::Choice, kPlaceN},
    {"noiselevel", "Noise Level", "dB", -60.0f, 0.0f, -30.0f},
    {"mechon", "Mechanics", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"wowa", "Wow A", "", 0.0f, 100.0f, 30.0f},
    {"wobblea", "Wobble A", "", 0.0f, 100.0f, 0.0f},
    {"speeda", "Speed A", "", 0.0f, 100.0f, 30.0f},
    {"wowb", "Wow B", "", 0.0f, 100.0f, 0.0f},
    {"wobbleb", "Wobble B", "", 0.0f, 100.0f, 0.0f},
    {"speedb", "Speed B", "", 0.0f, 100.0f, 30.0f},
    {"input", "Input", "dB", -18.0f, 18.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
    {"lowcut", "Low Cut", "Hz", 30.0f, 1500.0f, 30.0f, Taper::Log},
    {"highcut", "High Cut", "Hz", 15000.0f, 21000.0f, 21000.0f},
    {"mono", "Mono", "", 0.0f, 10.0f, 10.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dBFS", -60.0f, 6.0f}, {"OUT", "dBFS", -60.0f, 6.0f}};
constexpr double kMaxEchoMs = 3500.0;

// The sounds of the old devices: a band (low and high corner, each 12 dB / oct) and a bump, at TONE 0
// (the darker side) and TONE 100 (the brighter side). TONE morphs between them.
struct Band { float lowHz, highHz, bumpHz, bumpDb; };
constexpr Band kSound[4][2] = {
    {{220.0f, 5000.0f, 2500.0f, 3.0f}, {400.0f, 11000.0f, 4000.0f, 5.0f}},     // AIRY
    {{120.0f, 3800.0f, 1200.0f, 3.0f}, {200.0f, 7000.0f, 2000.0f, 4.0f}},      // VINTAGE
    {{60.0f, 1800.0f, 300.0f, 4.0f}, {90.0f, 3500.0f, 600.0f, 3.0f}},          // DEEP
    {{20.0f, 22000.0f, 1000.0f, 0.0f}, {20.0f, 22000.0f, 1000.0f, 0.0f}},      // FLAT: no colour (for SQUASH and RINGER alone)
};
// The decades: their band, how hard they limit (dB of gain reduction at full scale) and how much they distort.
struct Era { float lowHz, highHz, limitDb, drive; int bits; };
constexpr Era kEra[4] = {
    {200.0f, 5000.0f, 12.0f, 3.0f, 0},
    {100.0f, 8000.0f, 8.0f, 2.0f, 0},
    {50.0f, 12000.0f, 4.0f, 1.3f, 0},
    {30.0f, 16000.0f, 3.0f, 1.0f, 12},
};

inline float rnd(std::uint32_t& s) noexcept {
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
}

// Twelve noises, made here (no recordings). Each side has its own random stream.
class NoiseBank {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (int c = 0; c < 2; ++c) {
            auto& f = f_[c];
            f[0].c = C::make(T::HighPass, fs, 2000.0, 0.7071);    // tape hiss
            f[1].c = C::make(T::LowPass, fs, 60.0, 0.7071);       // rumble
            f[2].c = C::make(T::BandPass, fs, 1200.0, 0.8);       // radio
            f[3].c = C::make(T::HighPass, fs, 1500.0, 0.7071);    // rain drops
            f[4].c = C::make(T::LowPass, fs, 400.0, 0.7071);      // fire's body
        }
        clear();
    }
    void clear() noexcept {
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        for (auto& ch : pink_) ch.fill(0.0f);
        for (int c = 0; c < 2; ++c) { burst_[c] = 0.0f; burstEnv_[c] = 0.0f; wind_[c] = 0.0f; brown_[c] = 0.0f; }
        ph_ = fade_ = sweep_ = 0.0;
        s_[0] = 0x9E3779B9u;
        s_[1] = 0x7F4A7C15u;
        windBq_[0].clear();
        windBq_[1].clear();
    }
    float tick(int c, int type) noexcept {
        std::uint32_t& s = s_[c];
        const float w = rnd(s);
        switch (type) {
        case TapeHiss: return 1.4f * f_[c][0].tick(w);
        case White: return 0.5f * w;
        case Pink: return 0.25f * pink(c, w);
        case Rumble: brown_[c] = 0.995f * brown_[c] + 0.05f * w; return 3.0f * f_[c][1].tick(brown_[c]);
        case Crackle: return clicks(c, 25.0f, 0.6f, 0.0004f);
        case Dust: return clicks(c, 6.0f, 0.15f, 0.0002f) + 0.02f * f_[c][0].tick(w);
        case Hum50:
        case Hum60: {
            if (c == 0) {
                ph_ += (type == Hum50 ? 50.0 : 60.0) / fs_;
                if (ph_ >= 1.0) ph_ -= 1.0;
            }
            const double x = 2.0 * std::numbers::pi * ph_;
            return static_cast<float>(0.5 * std::sin(x) + 0.25 * std::sin(2 * x) + 0.15 * std::sin(3 * x) + 0.08 * std::sin(5 * x));
        }
        case Radio: {
            if (c == 0) {
                fade_ += 0.3 / fs_;
                if (fade_ >= 1.0) fade_ -= 1.0;
            }
            const float flicker = 0.6f + 0.4f * static_cast<float>(std::sin(2.0 * std::numbers::pi * fade_) * std::sin(2.0 * std::numbers::pi * 3.1 * fade_));
            return 2.0f * flicker * f_[c][2].tick(w);
        }
        case Rain: return clicks(c, 400.0f, 0.08f, 0.00015f, true) + 0.05f * pink(c, w);
        case Fire: return clicks(c, 12.0f, 0.5f, 0.002f) + 1.5f * f_[c][4].tick(brownOf(c, w));
        case Wind: {
            if (c == 0) {
                sweep_ += 0.07 / fs_;
                if (sweep_ >= 1.0) sweep_ -= 1.0;
            }
            const double centre = 400.0 * std::pow(4.0, 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * (sweep_ + 0.13 * c)));
            if ((tick_[c]++ & 63u) == 0) windBq_[c].c = C::make(T::BandPass, fs_, centre, 2.5);
            wind_[c] += (0.5f + 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * (sweep_ * 3.0 + c))) - wind_[c]) * 0.0005f;
            return 3.0f * wind_[c] * windBq_[c].tick(w);
        }
        default: return 0.0f;
        }
    }

private:
    float pink(int c, float w) noexcept {   // Paul Kellet's filter
        auto& b = pink_[c];
        b[0] = 0.99886f * b[0] + w * 0.0555179f;
        b[1] = 0.99332f * b[1] + w * 0.0750759f;
        b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f;
        b[4] = 0.55000f * b[4] + w * 0.5329522f;
        b[5] = -0.7616f * b[5] - w * 0.0168980f;
        const float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
        b[6] = w * 0.115926f;
        return out;
    }
    float brownOf(int c, float w) noexcept {
        brown_[c] = 0.997f * brown_[c] + 0.04f * w;
        return brown_[c];
    }
    // random clicks: `perSecond` on average, up to `size` high, each fading in `decayS`
    float clicks(int c, float perSecond, float size, float decayS, bool bright = false) noexcept {
        std::uint32_t& s = s_[c];
        if (0.5f * (rnd(s) + 1.0f) < perSecond / static_cast<float>(fs_)) {
            const float u = 0.5f * (rnd(s) + 1.0f);
            burst_[c] = (rnd(s) > 0.0f ? 1.0f : -1.0f) * size * u * u;
            burstEnv_[c] = 1.0f;
        }
        if (burstEnv_[c] < 1e-4f) return 0.0f;
        const float v = burst_[c] * burstEnv_[c] * (0.5f + 0.5f * rnd(s));
        burstEnv_[c] *= static_cast<float>(std::exp(-1.0 / (fs_ * decayS)));
        return bright ? f_[c][3].tick(v) * 2.0f : v;
    }

    double fs_ = 48000.0, ph_ = 0.0, fade_ = 0.0, sweep_ = 0.0;
    std::array<std::array<hc::fx::Biquad, 5>, 2> f_{};
    std::array<std::array<float, 7>, 2> pink_{};
    hc::fx::Biquad windBq_[2];
    float burst_[2]{}, burstEnv_[2]{}, wind_[2]{}, brown_[2]{};
    std::uint32_t s_[2]{}, tick_[2]{};
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kPawnLatency; }
    bool paramActive(int i) const noexcept override {
        if (i >= Device && i <= DeviceMix) return p(DeviceOn) >= 0.5f;
        if (i >= Sync && i <= SpaceMix) {
            if (p(SpaceOn) < 0.5f) return false;
            const int s = sync();
            if (i == TimeMs) return s == 2;
            if (i == Note) return s != 2;
            if (i == Bpm) return s == 0;
            return true;
        }
        if (i >= NoiseType && i <= NoiseLevel) return p(NoiseOn) >= 0.5f;
        if (i >= WowA && i <= SpeedB) return p(MechOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(kMaxEchoMs * 0.001 * fs) + 8);
        for (auto& d : mech_) d.prepare(2 * kPawnLatency + 8);
        verb_.prepare(fs, 1.2);
        noise_.prepare(fs);
        fade_ = static_cast<float>(1.0 / (0.02 * fs));
        reset();
    }
    void reset() noexcept override {
        for (auto& d : echo_) d.clear();
        for (auto& d : mech_) d.clear();
        verb_.clear();
        noise_.clear();
        for (auto& ch : dev_) for (auto& b : ch) b.clear();
        for (auto& ch : era_) for (auto& b : ch) b.clear();
        for (auto& ch : master_) for (auto& b : ch) b.clear();
        for (auto& ch : spring_) ch.fill(0.0f);
        for (auto& b : echoTone_) b.clear();
        key_.fill(-1e9f);
        design();
        env_ = lim_ = sq_ = denv_ = 0.0f;
        ringPh_ = phA_ = phB_ = 0.0;
        driftA_ = driftT_ = 0.0f;
        rng_ = 0x2468ACE1u;
        devMix_ = p(DeviceOn) >= 0.5f ? 1.0f : 0.0f;
        spaceOn_ = p(SpaceOn) >= 0.5f ? 1.0f : 0.0f;
        noiseOn_ = p(NoiseOn) >= 0.5f ? 1.0f : 0.0f;
        mechOn_ = p(MechOn) >= 0.5f ? 1.0f : 0.0f;
        in_.prepare(fs_, 20, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        dmix_.prepare(fs_, 30, p(DeviceMix) * 0.01f);
        smix_.prepare(fs_, 30, p(SpaceMix) * 0.01f);
        echoT_.prepare(fs_, 80, echoSamples());
        nlev_.prepare(fs_, 30, noiseGain());
        ring_.prepare(fs_, 20, p(Ringer) * 0.01f);
        width_.prepare(fs_, 30, p(Mono) * 0.1f);
        pkIn_ = pkOut_ = 0.0f;
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const bool devOn = p(DeviceOn) >= 0.5f, spOn = p(SpaceOn) >= 0.5f, nzOn = p(NoiseOn) >= 0.5f, mcOn = p(MechOn) >= 0.5f;
        const Era& era = kEra[pick(p(Styler), 3)];
        const float wIn = hc::fx::dbToGain(p(Input)), wOut = hc::fx::dbToGain(p(Output)), wDmix = p(DeviceMix) * 0.01f, wSmix = p(SpaceMix) * 0.01f;
        const float wEcho = echoSamples(), wNoise = noiseGain(), wRing = p(Ringer) * 0.01f, wWidth = p(Mono) * 0.1f;
        const float echoLv = p(EchoLevel) * 0.01f, fb = std::min(p(Feedback) * 0.01f, 1.0f), revLv = p(ReverbLevel) * 0.01f;
        const bool ping = p(PingPong) >= 0.5f, spring = p(ReverbType) < 0.5f, post = p(NoisePlace) >= 0.5f, gate = p(NoiseMode) >= 0.5f;
        const float thr = p(Threshold), squash = p(Squash);
        const int ntype = pick(p(NoiseType), kNoises - 1);
        const float att = coefMs(fs_, 5.0), rel = coefMs(fs_, 150.0), limAtt = coefMs(fs_, 1.0), limRel = coefMs(fs_, 120.0);
        const float steps = era.bits > 0 ? static_cast<float>(1 << (era.bits - 1)) : 0.0f;
        // the mechanics: A drifts like tape, B turns like a record
        const float wowA = p(WowA) * 0.01f, wobA = p(WobbleA) * 0.01f, wowB = p(WowB) * 0.01f, wobB = p(WobbleB) * 0.01f;
        const double incA = 2.0 * std::numbers::pi * (0.1 + 4.0 * std::pow(p(SpeedA) * 0.01, 2.0)) / fs_;
        const double incB = 2.0 * std::numbers::pi * (0.2 + 2.0 * p(SpeedB) * 0.01) / fs_;
        float pi = 0.0f, po = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn);
            float x[2] = {L[i] * gi, R[i] * gi};
            pi = std::max({pi, std::fabs(x[0]), std::fabs(x[1])});
            const float level = std::max(std::fabs(x[0]), std::fabs(x[1]));
            denv_ = level > denv_ ? level + att * (denv_ - level) : level + rel * (denv_ - level);   // for the noise's duck / gate
            // ---- DEVICE
            devMix_ = devOn ? std::min(1.0f, devMix_ + fade_) : std::max(0.0f, devMix_ - fade_);
            if (devMix_ > 0.0f) {
                float d[2] = {x[0], x[1]};
                for (int c = 0; c < 2; ++c) {
                    auto& f = dev_[static_cast<std::size_t>(c)];
                    d[c] = f[2].tick(f[1].tick(f[0].tick(d[c])));
                }
                // SQUASH: minus = a compressor (up to 6:1 over -24 dBFS, made up), plus = the loud parts pushed out
                const float lv = std::max(std::fabs(d[0]), std::fabs(d[1]));
                env_ = lv > env_ ? lv + att * (env_ - lv) : lv + rel * (env_ - lv);
                const float over = std::max(dbOf(env_) + 24.0f, 0.0f);
                float sqDb = 0.0f;
                if (squash < 0.0f) sqDb = -over * (1.0f - 1.0f / (1.0f + 0.5f * -squash)) + 0.4f * -squash;
                else if (squash > 0.0f) sqDb = std::min(over * 0.05f * squash, 12.0f);
                sq_ += (sqDb - sq_) * 0.01f;
                const float sg = hc::fx::dbToGain(sq_);
                // RINGER: ring modulation and digital clipping, more of both as it goes up
                const float rg = ring_.next(wRing);
                ringPh_ += 830.0 / fs_;
                if (ringPh_ >= 1.0) ringPh_ -= 1.0;
                const float car = static_cast<float>(std::sin(2.0 * std::numbers::pi * ringPh_));
                const float clip = hc::fx::dbToGain(-18.0f * rg);
                // the DECADE: its limiter, its distortion, its band (and the 80s' converter)
                const float elv = std::max(std::fabs(d[0]), std::fabs(d[1])) * sg;
                const float want = era.limitDb * std::clamp(dbOf(std::max(elv, 1e-6f)) / 24.0f + 1.0f, 0.0f, 1.0f);
                lim_ = want > lim_ ? want + limAtt * (lim_ - want) : want + limRel * (lim_ - want);
                const float lg = hc::fx::dbToGain(-lim_);
                for (int c = 0; c < 2; ++c) {
                    float v = d[c] * sg;
                    if (rg > 0.0f) {   // the ring, then a hard (digital) clip, half made up
                        v = v * (1.0f - 0.5f * rg + 0.5f * rg * car);
                        v = std::clamp(v, -clip, clip) / std::sqrt(clip);
                    }
                    v *= lg;
                    v = std::tanh(era.drive * v) / era.drive;
                    if (steps > 0.0f) v = std::round(v * steps) / steps;
                    auto& e = era_[static_cast<std::size_t>(c)];
                    v = e[1].tick(e[0].tick(v));
                    d[c] = v;
                }
                const float m = dmix_.next(wDmix) * devMix_;
                x[0] += m * (d[0] - x[0]);
                x[1] += m * (d[1] - x[1]);
            }
            // ---- NOISE (its level follows the music: DUCK = down while it plays, GATE = only while it plays)
            noiseOn_ = nzOn ? std::min(1.0f, noiseOn_ + fade_) : std::max(0.0f, noiseOn_ - fade_);
            float nz[2] = {0.0f, 0.0f};
            const float nl = nlev_.next(wNoise) * noiseOn_;
            if (nl > 0.0f) {
                float follow = 1.0f;
                if (thr > -59.95f) {
                    const float above = std::clamp((dbOf(std::max(denv_, 1e-9f)) - thr) / 6.0f, 0.0f, 1.0f);
                    follow = gate ? above : 1.0f - 0.94f * above;
                }
                nz[0] = nl * follow * noise_.tick(0, ntype);
                nz[1] = nl * follow * noise_.tick(1, ntype);
            }
            if (!post) {
                x[0] += nz[0];
                x[1] += nz[1];
            }
            // ---- SPACE: the echo and the reverb side by side
            spaceOn_ = spOn ? std::min(1.0f, spaceOn_ + fade_) : std::max(0.0f, spaceOn_ - fade_);
            const float et = echoT_.next(wEcho), sm = smix_.next(wSmix);
            if (spaceOn_ > 0.0f) {
                float back[2] = {echoTone_[0].tick(echo_[0].read(et)), echoTone_[1].tick(echo_[1].read(et))};
                // FEEDBACK 100 repeats forever: kept from running away by a soft limit in the loop
                if (ping) {
                    const float mono = 0.5f * (x[0] + x[1]);
                    echo_[0].write(softLimit(mono + fb * back[1]));
                    echo_[1].write(softLimit(fb * back[0]));
                } else {
                    echo_[0].write(softLimit(x[0] + fb * back[0]));
                    echo_[1].write(softLimit(x[1] + fb * back[1]));
                }
                float vin[2] = {x[0], x[1]};
                if (spring)
                    for (int c = 0; c < 2; ++c) {   // a dispersive chain in front: the spring's chirp
                        auto& st = spring_[static_cast<std::size_t>(c)];
                        float v = vin[c];
                        for (int k = 0; k < kChirp; ++k) {
                            const float y = -0.6f * v + st[static_cast<std::size_t>(k)];
                            st[static_cast<std::size_t>(k)] = v + 0.6f * y;
                            v = y;
                        }
                        vin[c] = v;
                    }
                float vl = 0.0f, vr = 0.0f;
                verb_.tick(vin[0], vin[1], vl, vr);
                const float wet[2] = {echoLv * back[0] + revLv * vl, echoLv * back[1] + revLv * vr};
                const float k = sm * spaceOn_;
                x[0] += k * (wet[0] - x[0]);
                x[1] += k * (wet[1] - x[1]);
            }
            if (post) {
                x[0] += nz[0];
                x[1] += nz[1];
            }
            // ---- MECHANICS: the moving delay (WOW) and the moving level (WOBBLE); centred at kPawnLatency
            mechOn_ = mcOn ? std::min(1.0f, mechOn_ + fade_) : std::max(0.0f, mechOn_ - fade_);
            phA_ += incA;
            if (phA_ > 2.0 * std::numbers::pi) phA_ -= 2.0 * std::numbers::pi;
            phB_ += incB;
            if (phB_ > 2.0 * std::numbers::pi) phB_ -= 2.0 * std::numbers::pi;
            if ((++tick_ & 2047u) == 0) driftT_ = rnd(rng_);
            driftA_ += (driftT_ - driftA_) * 1e-4f;
            const float modA = static_cast<float>(std::sin(phA_)) * 0.6f + 0.4f * driftA_, modB = static_cast<float>(std::sin(phB_));
            const float shift = mechOn_ * (60.0f * wowA * modA + 60.0f * wowB * modB);
            const float lvl = 1.0f - mechOn_ * (0.25f * wobA * (0.5f + 0.5f * modA) + 0.25f * wobB * (0.5f + 0.5f * static_cast<float>(std::sin(phB_ * 2.0))));
            for (int c = 0; c < 2; ++c) {
                mech_[c].write(x[c]);
                x[c] = mech_[c].read(static_cast<float>(kPawnLatency) + shift) * lvl;
            }
            // ---- MASTER
            for (int c = 0; c < 2; ++c) {
                auto& f = master_[static_cast<std::size_t>(c)];
                x[c] = f[1].tick(f[0].tick(x[c]));
            }
            const float wdt = width_.next(wWidth);
            const float mid = 0.5f * (x[0] + x[1]), side = 0.5f * (x[0] - x[1]) * wdt;
            const float o = out_.next(wOut);
            L[i] = (mid + side) * o;
            R[i] = (mid - side) * o;
            po = std::max({po, std::fabs(L[i]), std::fabs(R[i])});
        }
        const float fall = static_cast<float>(std::exp(-static_cast<double>(n) / (0.3 * fs_)));
        pkIn_ = std::max(pi, pkIn_ * fall);
        pkOut_ = std::max(po, pkOut_ * fall);
        publishMeter(0, std::max(dbOf(pkIn_), -100.0f));
        publishMeter(1, std::max(dbOf(pkOut_), -100.0f));
    }

private:
    static constexpr int kChirp = 10;
    static int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }
    static float softLimit(float v) noexcept { return v / std::sqrt(1.0f + 0.25f * v * v); }
    int sync() const noexcept { return pick(p(Sync), 2); }
    float echoSamples() const noexcept {
        double ms = p(TimeMs);
        const int s = sync();
        if (s != 2) ms = kNoteBeats[pick(p(Note), 11)] * 60000.0 / std::max(1.0, s == 0 ? static_cast<double>(p(Bpm)) : static_cast<double>(tempo()));
        return static_cast<float>(std::clamp(ms, 1.0, kMaxEchoMs) * 0.001 * fs_);
    }
    float noiseGain() const noexcept { return p(NoiseLevel) <= -59.95f ? 0.0f : hc::fx::dbToGain(p(NoiseLevel)); }
    void design() noexcept {
        const std::array<float, 7> now{p(Device), p(Tone), p(Styler), p(ReverbLength), p(ReverbType), p(LowCut), p(HighCut)};
        if (now == key_) return;
        key_ = now;
        const int dv = pick(p(Device), 3);
        const float t = p(Tone) * 0.01f;
        const Band& a = kSound[dv][0];
        const Band& b = kSound[dv][1];
        auto lg = [t](float x, float y) { return std::exp(std::log(x) + t * (std::log(y) - std::log(x))); };
        const double top = fs_ * 0.45;
        const C lo = dv == 3 ? C{} : C::make(T::HighPass, fs_, lg(a.lowHz, b.lowHz), 0.7071);
        const C hi = dv == 3 ? C{} : C::make(T::LowPass, fs_, std::min<double>(lg(a.highHz, b.highHz), top), 0.7071);
        const C bump = C::make(T::Bell, fs_, lg(a.bumpHz, b.bumpHz), 1.0, a.bumpDb + t * (b.bumpDb - a.bumpDb));
        for (auto& f : dev_) { f[0].c = lo; f[1].c = hi; f[2].c = bump; }
        const Era& e = kEra[pick(p(Styler), 3)];
        for (auto& f : era_) {
            f[0].c = C::make(T::HighPass, fs_, e.lowHz, 0.7071);
            f[1].c = C::make(T::LowPass, fs_, std::min<double>(e.highHz, top), 0.7071);
        }
        for (auto& f : master_) {
            f[0].c = C::make(T::HighPass, fs_, p(LowCut), 0.7071);
            f[1].c = C::make(T::LowPass, fs_, std::min<double>(p(HighCut), top), 0.7071);
        }
        const bool spring = p(ReverbType) < 0.5f;
        const double t60 = 0.3 * std::pow(20.0, p(ReverbLength) * 0.01);   // 0.3 .. 6 s
        verb_.set(spring ? 0.55 : 1.1, t60, spring ? 4500.0 : 9000.0);
        const C et = C::make(T::LowPass, fs_, 6000.0, 0.7071);
        for (auto& b2 : echoTone_) b2.c = et;
    }

    double fs_ = 48000.0, ringPh_ = 0.0, phA_ = 0.0, phB_ = 0.0;
    hc::fx::DelayLine echo_[2], mech_[2];
    Fdn8 verb_;
    NoiseBank noise_;
    std::array<std::array<hc::fx::Biquad, 3>, 2> dev_{};
    std::array<std::array<hc::fx::Biquad, 2>, 2> era_{}, master_{};
    std::array<std::array<float, kChirp>, 2> spring_{};
    hc::fx::Biquad echoTone_[2];
    std::array<float, 7> key_{};
    float env_ = 0.0f, lim_ = 0.0f, sq_ = 0.0f, denv_ = 0.0f, driftA_ = 0.0f, driftT_ = 0.0f, fade_ = 0.001f;
    float devMix_ = 1.0f, spaceOn_ = 0.0f, noiseOn_ = 0.0f, mechOn_ = 0.0f, pkIn_ = 0.0f, pkOut_ = 0.0f;
    std::uint32_t rng_ = 0x2468ACE1u, tick_ = 0;
    hc::fx::Smoothed in_, out_, dmix_, smix_, echoT_, nlev_, ring_, width_;
};

} // namespace
} // namespace pawnshop

std::unique_ptr<hc::fx::Effect> makePawnShop() { return std::make_unique<pawnshop::Impl>(); }

} // namespace hem::rack
