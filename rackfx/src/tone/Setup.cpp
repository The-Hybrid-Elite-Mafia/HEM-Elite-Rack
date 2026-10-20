// THE SETUP — a filter played by an LFO, a step sequencer and an envelope follower, with a delay (see ToneShop.hpp).
//
// Per sample: INPUT; the movers (LFO, SEQUENCER, FOLLOWER) give -1..1 (the follower 0..1), each target
// adds them up by their DEPTHs and SMOOTH rounds the result off. The delay (ROUTE 0) runs first; then
// DRIVE, the filter (a zero-delay state-variable filter, one or two of them for 12 or 24 dB, its
// frequency set every sample; or the comb, or the gain) with SPREAD between left and right; then CRUSH
// (sample-and-hold). With ROUTE 1 the filter sits inside the delay's feedback instead. MIX, OUTPUT.
#include "hem/rack/ToneShop.hpp"

#include "hem/rack/Spectrum.hpp"

#include "ToneCommon.hpp"

#include <vector>

namespace hem::rack {
namespace thesetup {
namespace {
using namespace tone;

const char* const kTypeN[] = {"LO PASS", "HI PASS", "BAND PASS", "NOTCH", "COMB", "LEVEL"};
const char* const kSlopeN[] = {"12 dB", "24 dB"};
const char* const kLfoNoteN[] = {"4/1", "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
constexpr double kLfoBeats[] = {16, 8, 4, 2, 1, 0.5, 0.25, 0.125};
const char* const kSeqNoteN[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
constexpr double kSeqBeats[] = {4, 2, 1, 0.5, 0.25, 0.125};
const char* const kDelayNoteN[] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
constexpr double kDelayBeats[] = {0.125, 0.25, 0.5, 1, 2, 4};
const char* const kWaveN[] = {"SINE", "TRIANGLE", "PULSE", "SAW", "RANDOM"};
const char* const kRightN[] = {"100 %", "150 %", "200 %"};
constexpr float kRight[] = {1.0f, 1.5f, 2.0f};
const char* const kAnalogN[] = {"CLEAN", "WARM"};
const char* const kRouteN[] = {"PRE FILTER", "IN FEEDBACK"};

#define HEM_STEP(n) {"step" #n, "Step " #n, "%", -100.0f, 100.0f, 0.0f}
#define HEM_DEPTH(id, name) {id, name, "%", -100.0f, 100.0f, 0.0f}
const ParamInfo kParams[kCount] = {
    {"type", "Type", "", 0, kTypes - 1, 0, Taper::Choice, kTypeN},
    {"freq", "Freq", "Hz", 30.0f, 18000.0f, 18000.0f, Taper::Log},
    {"combdelay", "Comb Delay", "ms", 11.0f, 28.0f, 15.0f},
    {"gain", "Gain", "dB", -80.0f, 0.0f, 0.0f},
    {"res", "Res", "%", 0.0f, 100.0f, 0.0f},
    {"combfeedback", "Comb Feedback", "%", 0.0f, 100.0f, 0.0f},
    {"slope", "Slope", "", 0, 1, 0, Taper::Choice, kSlopeN},
    {"spread", "Spread", "%", -50.0f, 50.0f, 0.0f},
    {"smooth", "Glide", "%", 0.0f, 100.0f, 0.0f},
    {"drive", "Drive", "%", 0.0f, 100.0f, 0.0f},
    {"crush", "Crush", "%", 0.0f, 100.0f, 0.0f},
    {"lforate", "LFO Rate", "Hz", 0.1f, 100.0f, 1.0f, Taper::Log},
    {"lfonote", "LFO Note", "", 0, 7, 6, Taper::Choice, kLfoNoteN},
    {"lfosync", "LFO Sync", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"lfowave", "LFO Wave", "", 0, 4, 0, Taper::Choice, kWaveN},
    {"seqrate", "Seq Rate", "Hz", 0.1f, 50.0f, 8.0f, Taper::Log},
    {"seqnote", "Seq Note", "", 0, 5, 5, Taper::Choice, kSeqNoteN},
    {"seqsync", "Seq Sync", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"swing", "Swing", "%", 0.0f, 75.0f, 0.0f},
    {"last", "Last Step", "", 1.0f, 16.0f, 16.0f},
    HEM_STEP(1), HEM_STEP(2), HEM_STEP(3), HEM_STEP(4), HEM_STEP(5), HEM_STEP(6), HEM_STEP(7), HEM_STEP(8),
    HEM_STEP(9), HEM_STEP(10), HEM_STEP(11), HEM_STEP(12), HEM_STEP(13), HEM_STEP(14), HEM_STEP(15), HEM_STEP(16),
    {"delaytime", "Delay Time", "ms", 50.0f, 1000.0f, 250.0f, Taper::Log},
    {"delaynote", "Delay Note", "", 0, 5, 2, Taper::Choice, kDelayNoteN},
    {"delaysync", "Delay Sync", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"delayright", "Delay Right", "", 0, 2, 0, Taper::Choice, kRightN},
    {"delayfeedback", "Delay Feedback", "%", 0.0f, 100.0f, 0.0f},
    {"delaymix", "Delay Dry/Wet", "%", 0.0f, 100.0f, 0.0f},
    {"delayanalog", "Delay Style", "", 0, 1, 0, Taper::Choice, kAnalogN},
    {"delayroute", "Delay Route", "", 0, 1, 0, Taper::Choice, kRouteN},
    HEM_DEPTH("lfofreq", "LFO > Freq"), HEM_DEPTH("seqfreq", "Seq > Freq"), HEM_DEPTH("followfreq", "Follow > Freq"),
    HEM_DEPTH("lfores", "LFO > Res"), HEM_DEPTH("seqres", "Seq > Res"), HEM_DEPTH("followres", "Follow > Res"),
    HEM_DEPTH("lfotime", "LFO > Time"), HEM_DEPTH("seqtime", "Seq > Time"), HEM_DEPTH("followtime", "Follow > Time"),
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"input", "Input", "dB", -40.0f, 10.0f, 0.0f},
    {"output", "Output", "dB", -40.0f, 10.0f, 0.0f},
};
#undef HEM_STEP
#undef HEM_DEPTH
const hc::fx::MeterInfo kMeters[] = {{"LFO", "", -1.0f, 1.0f, true}, {"STEP", "", 1.0f, 16.0f}, {"FOLLOW", "", 0.0f, 1.0f}};
constexpr double kMaxDelayS = 2.2;   // 1 s, x 2 (RIGHT 200 %) and modulated up to x 2

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }
// resonance -> Q (12 dB: from 0.71; 24 dB: the second stage from 1.31)
inline float qOf(float res, bool steep) noexcept {
    const float r = std::clamp(res, 0.0f, 100.0f) * 0.01f;
    return steep ? 1.3066f * (1.0f + 12.0f * r * r) : 0.7071f * (1.0f + 24.0f * r * r);
}

// A zero-delay state-variable filter (one channel).
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    void clear() noexcept { ic1 = ic2 = 0.0f; }
    // g = tan(pi f / fs), k = 1 / Q; returns the chosen output
    float tick(float v0, float g, float k, int type) noexcept {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        switch (type) {
        case LowPass: return v2;
        case HighPass: return v0 - k * v1 - v2;
        case BandPass: return k * v1;   // 0 dB at the centre
        default: return v0 - k * v1;    // notch
        }
    }
};

class Impl final : public hc::fx::Effect, public SpectrumSource {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    bool paramActive(int i) const noexcept override {
        const int t = pick(p(Kind), kTypes - 1);
        if (i == Freq || i == Res || i == Slope) return t < Comb;
        if (i == CombDelay || i == CombFeedback) return t == Comb;
        if (i == Gain) return t == Amp;
        if (i == LfoRate) return p(LfoSync) < 0.5f;
        if (i == LfoNote) return p(LfoSync) >= 0.5f;
        if (i == SeqRate) return p(SeqSync) < 0.5f;
        if (i == SeqNote) return p(SeqSync) >= 0.5f;
        if (i == DelayTime) return p(DelaySync) < 0.5f;
        if (i == DelayNote) return p(DelaySync) >= 0.5f;
        if (i >= Step1 && i < Step1 + kSteps) return i - Step1 < static_cast<int>(std::lround(p(Last)));
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const auto n = static_cast<std::uint32_t>(kMaxDelayS * fs) + 8;
        for (auto& d : dly_) d.prepare(n);
        for (auto& c : comb_) c.prepare(static_cast<std::uint32_t>(0.06 * fs) + 8);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& c : svf_) for (auto& s : c) s.clear();
        for (auto& d : dly_) d.clear();
        for (auto& c : comb_) c.clear();
        lfoPh_ = 0.0;
        lfoRand_ = 0.0f;
        seqPos_ = 0.0;
        step_ = 0;
        env_ = 0.0f;
        for (float& m : mod_) m = 0.0f;
        dTime_[0] = dTime_[1] = -1.0f;
        hold_[0] = hold_[1] = 0.0f;
        holdN_ = 0;
        rng_ = 0x9E3779B9u;
        in_.prepare(fs_, 20, hc::fx::dbToGain(p(Input)));
        out_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        dmix_.prepare(fs_, 20, p(DelayMix) * 0.01f);
        before_.reset();
        after_.reset();
        publishMeter(0, 0.0f);
        publishMeter(1, 1.0f);
        publishMeter(2, 0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        const double bpm = tempo();
        const int type = pick(p(Kind), kTypes - 1);
        const bool steep = p(Slope) >= 0.5f;
        // the movers' speeds
        const double lfoHz = p(LfoSync) >= 0.5f ? bpm / 60.0 / kLfoBeats[pick(p(LfoNote), 7)] : p(LfoRate);
        const double stepS = p(SeqSync) >= 0.5f ? kSeqBeats[pick(p(SeqNote), 5)] * 60.0 / bpm : 1.0 / p(SeqRate);
        const double swing = p(Swing) * 0.01;
        const int last = std::clamp(static_cast<int>(std::lround(p(Last))), 1, kSteps);
        const int wave = pick(p(LfoWave), 4);
        float depth[kMovers][kTargets];
        for (int m = 0; m < kMovers; ++m)
            for (int t = 0; t < kTargets; ++t) depth[m][t] = p(thesetup::depth(m, t)) * 0.01f;
        const float smoothK = p(Smooth) > 0.0f ? coefMs(fs_, p(Smooth) * 2.0) : 0.0f;
        const float envAtt = coefMs(fs_, 5.0), envRel = coefMs(fs_, 150.0);
        // the filter
        const float baseHz = p(Freq), baseRes = p(Res), spreadR = std::pow(2.0f, p(Spread) / 100.0f);
        const float combMs = p(CombDelay), combFb = p(CombFeedback), gainDb = p(Gain);
        const float drive = 1.0f + 0.09f * p(Drive);
        const int crushN = 1 + static_cast<int>(p(Crush) * 0.4f);
        // the delay
        const double dBase = p(DelaySync) >= 0.5f ? kDelayBeats[pick(p(DelayNote), 5)] * 60000.0 / bpm : p(DelayTime);
        const float right = kRight[pick(p(DelayRight), 2)], dFb = p(DelayFeedback) * 0.009f;
        const bool analog = p(DelayAnalog) >= 0.5f, inLoop = p(DelayRoute) >= 0.5f;
        const float glide = analog ? coefMs(fs_, 150.0) : 0.0f;
        const float wIn = hc::fx::dbToGain(p(Input)), wOut = hc::fx::dbToGain(p(Output)), wMix = p(Mix) * 0.01f, wDMix = p(DelayMix) * 0.01f;
        const float nyq = static_cast<float>(fs_ * 0.45);
        float lfoOut = 0.0f, follow = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = in_.next(wIn);
            const float xl = L[i] * gi, xr = R[i] * gi;
            // the movers
            const double ph = lfoPh_;
            float lfo;
            switch (wave) {
            case 0: lfo = static_cast<float>(std::sin(2.0 * std::numbers::pi * ph)); break;
            case 1: lfo = static_cast<float>(ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph); break;
            case 2: lfo = ph < 0.5 ? 1.0f : -1.0f; break;
            case 3: lfo = static_cast<float>(2.0 * ph - 1.0); break;
            default: lfo = lfoRand_; break;
            }
            lfoPh_ += lfoHz / fs_;
            if (lfoPh_ >= 1.0) {
                lfoPh_ -= std::floor(lfoPh_);
                rng_ = rng_ * 1664525u + 1013904223u;
                lfoRand_ = static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f;
            }
            const double thisStep = stepS * (step_ % 2 == 0 ? 1.0 + swing : 1.0 - swing);
            seqPos_ += 1.0 / fs_;
            if (seqPos_ >= thisStep) {
                seqPos_ -= thisStep;
                step_ = (step_ + 1) % last;
            }
            const float seq = p(Step1 + std::min(step_, kSteps - 1)) * 0.01f;
            const float a = std::max(std::fabs(xl), std::fabs(xr));
            env_ = a + (a > env_ ? envAtt : envRel) * (env_ - a);
            const float fol = std::clamp((dbOf(env_) + 60.0f) / 60.0f, 0.0f, 1.0f);
            const float src[kMovers] = {lfo, seq, fol};
            for (int t = 0; t < kTargets; ++t) {
                float m = 0.0f;
                for (int s = 0; s < kMovers; ++s) m += depth[s][t] * src[s];
                mod_[t] = m + smoothK * (mod_[t] - m);
            }
            lfoOut = lfo;
            follow = fol;
            // the delay times (TIME target: up to an octave either way)
            const float dMs = static_cast<float>(std::clamp(dBase * std::pow(2.0, static_cast<double>(mod_[ToTime])), 50.0, 1000.0));
            const float want[2] = {dMs * 0.001f * static_cast<float>(fs_), dMs * right * 0.001f * static_cast<float>(fs_)};
            for (int c = 0; c < 2; ++c) dTime_[c] = dTime_[c] < 0.0f ? want[c] : want[c] + glide * (dTime_[c] - want[c]);
            const float dm = dmix_.next(wDMix);
            // the filter's settings now
            const float res = std::clamp(baseRes + 100.0f * mod_[ToRes], 0.0f, 100.0f);
            float yl, yr;
            auto filter = [&](float l, float r) {
                if (drive > 1.0f) {   // DRIVE: pushed into a soft clip, level kept for small signals
                    l = std::tanh(drive * l) / drive;
                    r = std::tanh(drive * r) / drive;
                }
                if (type < Comb) {
                    const float f = std::clamp(baseHz * std::exp2(5.0f * mod_[ToFreq]), 20.0f, nyq);
                    const float fl = std::clamp(f / spreadR, 20.0f, nyq), fr = std::clamp(f * spreadR, 20.0f, nyq);
                    const float gl = std::tan(static_cast<float>(std::numbers::pi) * fl / static_cast<float>(fs_));
                    const float gr = std::tan(static_cast<float>(std::numbers::pi) * fr / static_cast<float>(fs_));
                    if (steep) {
                        const float k2 = 1.0f / qOf(res, true), k1 = 1.0f / 0.5412f;
                        l = svf_[0][1].tick(svf_[0][0].tick(l, gl, k1, type), gl, k2, type);
                        r = svf_[1][1].tick(svf_[1][0].tick(r, gr, k1, type), gr, k2, type);
                    } else {
                        const float k = 1.0f / qOf(res, false);
                        l = svf_[0][0].tick(l, gl, k, type);
                        r = svf_[1][0].tick(r, gr, k, type);
                    }
                } else if (type == Comb) {
                    const float ms = std::clamp(combMs * std::exp2(mod_[ToFreq]), 11.0f, 28.0f);
                    const float fb = std::clamp(combFb + 100.0f * mod_[ToRes], 0.0f, 100.0f) * 0.0095f;
                    const float dl = ms / spreadR * 0.001f * static_cast<float>(fs_), dr = ms * spreadR * 0.001f * static_cast<float>(fs_);
                    const float cl = l + fb * comb_[0].read(dl), cr = r + fb * comb_[1].read(dr);
                    comb_[0].write(cl);
                    comb_[1].write(cr);
                    l = cl * (1.0f - fb * 0.5f);
                    r = cr * (1.0f - fb * 0.5f);
                } else {
                    const float g = hc::fx::dbToGain(std::clamp(gainDb + 40.0f * mod_[ToFreq], -80.0f, 0.0f));
                    l *= g;
                    r *= g;
                }
                // CRUSH: hold every crushN-th sample
                if (crushN > 1) {
                    if (holdN_ == 0) { hold_[0] = l; hold_[1] = r; }
                    holdN_ = (holdN_ + 1) % crushN;
                    l = hold_[0];
                    r = hold_[1];
                }
                return std::array<float, 2>{l, r};
            };
            const float outL = dly_[0].read(dTime_[0]), outR = dly_[1].read(dTime_[1]);
            if (!inLoop) {   // the delay first, then the filter
                dly_[0].write(xl + dFb * outL);
                dly_[1].write(xr + dFb * outR);
                const auto y = filter(xl + dm * (outL - xl), xr + dm * (outR - xr));
                yl = y[0];
                yr = y[1];
            } else {         // the filter inside the delay's feedback
                const auto y = filter(xl + dFb * outL, xr + dFb * outR);
                dly_[0].write(y[0]);
                dly_[1].write(y[1]);
                yl = y[0] + dm * outL;
                yr = y[1] + dm * outR;
            }
            const float m = mix_.next(wMix), o = out_.next(wOut);
            L[i] = (xl + m * (yl - xl)) * o;
            R[i] = (xr + m * (yr - xr)) * o;
        }
        after_.push(L, R, n);
        publishMeter(0, lfoOut);
        publishMeter(1, static_cast<float>(step_ + 1));
        publishMeter(2, follow);
    }

private:
    double fs_ = 48000.0;
    Svf svf_[2][2];
    hc::fx::DelayLine dly_[2], comb_[2];
    double lfoPh_ = 0.0, seqPos_ = 0.0;
    float lfoRand_ = 0.0f, env_ = 0.0f, mod_[kTargets]{}, dTime_[2]{-1.0f, -1.0f}, hold_[2]{};
    int step_ = 0, holdN_ = 0;
    std::uint32_t rng_ = 0x9E3779B9u;
    hc::fx::Smoothed in_, out_, mix_, dmix_;
    SpectrumMeter before_, after_;
};

int dots(const float* v, double* hz, int max) noexcept {
    if (max < 1 || pick(v[Kind], kTypes - 1) >= Comb) return 0;
    hz[0] = v[Freq];
    return 1;
}

} // namespace

double curveDb(const float* v, double fs, double hz) noexcept {
    const int type = pick(v[Kind], kTypes - 1);
    double db = 0.0;
    if (type < Comb) {
        const bool steep = v[Slope] >= 0.5f;
        const T t = type == LowPass ? T::LowPass : type == HighPass ? T::HighPass : type == BandPass ? T::BandPass : T::Notch;
        const double f = std::min(static_cast<double>(v[Freq]), fs * 0.45);
        if (steep) db = C::make(t, fs, f, 0.5412).magnitudeDb(fs, hz) + C::make(t, fs, f, qOf(v[Res], true)).magnitudeDb(fs, hz);
        else db = C::make(t, fs, f, qOf(v[Res], false)).magnitudeDb(fs, hz);
    } else if (type == Comb) {
        const double fb = v[CombFeedback] * 0.0095, w = 2.0 * std::numbers::pi * hz * v[CombDelay] * 0.001;
        const double re = 1.0 - fb * std::cos(w), im = fb * std::sin(w);
        db = 20.0 * std::log10((1.0 - fb * 0.5) / std::sqrt(re * re + im * im));
    } else {
        db = v[Gain];
    }
    const double m = std::clamp(static_cast<double>(v[Mix]) * 0.01, 0.0, 1.0);   // the dry part (drawn as if in phase)
    return 20.0 * std::log10(std::max(1.0 - m + m * std::pow(10.0, db / 20.0), 1e-9)) + v[Output];
}
const CurveInfo kCurve{&curveDb, &dots, 30.0f};

} // namespace thesetup

std::unique_ptr<hc::fx::Effect> makeTheSetup() { return std::make_unique<thesetup::Impl>(); }

} // namespace hem::rack
