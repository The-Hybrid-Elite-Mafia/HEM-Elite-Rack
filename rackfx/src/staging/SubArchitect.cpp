// =============================================================================
//  03 SUB-BASS & 808 ARCHITECT — builds the low end of an 808 or bass: a real sub one octave
//  BELOW the note (a tracked sine, or an octave divider), punch, tightness, a duck when the kick
//  clicks, phase rotation, cleanup and a mono-maker, then a trim / clip stage with oversampling.
//  Different from BASSBEACON (40), which adds harmonics ABOVE the bass for small speakers.
//  Signal path: input -> [wet chain] -> dry/wet -> mono-maker -> output trim -> clip (oversampled).
//  Dead flat on load: DRY / WET 0 % (wet chain unheard), MONO-MAKER 0 %, trim 0 dB, clip off, 1x.
//  Not a plug-in feature (greyed on the faceplate): LOAD AUDIO, play / pause, MASTER / STEM EXPORT.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P {
    kSubDrive, kPunch, kTight, kTrim, kSoftClip, kHardClip, kCharacter,
    kDuckOn, kDuckAmt, kDuckAtt, kDuckRel,
    kPhaseOn, kInvert, kLink, kPhaseAngle,
    kTrackOn, kTrackMode, kTrackSens, kManualHz,
    kCutOn, kCutFreq, kHarm2, kHarmWeight,
    kMix, kMonoAmt, kMonoCut, kOversample, kQuality, kCount
};
const char* const kCharN[] = {"CLEAN 808", "SUB BOOM", "ANALOG WEIGHT", "CHEST PUNCH", "MONO-LOCK 120HZ", "CUSTOM"};
const char* const kTrackN[] = {"AUTO", "MANUAL"};
const char* const kOsN[] = {"1X", "2X", "4X", "8X"};
const char* const kQualN[] = {"ECO", "NORMAL", "HIGH", "ULTRA"};

const ParamInfo kParams[kCount] = {
    {"sub_drive", "SUB-HARMONIC DRIVE", "dB", -12.0f, 24.0f, 0.0f},
    {"punch", "808 PUNCH / ATTACK", "%", 0.0f, 100.0f, 0.0f},
    {"tightness", "SUB TIGHTNESS / DECAY", "%", 0.0f, 100.0f, 0.0f},
    {"trim", "OUTPUT TRIM", "dB", -12.0f, 12.0f, 0.0f},
    {"soft_clip", "SOFT CLIP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"hard_clip", "HARD CLIP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"character", "CHARACTER", "", 0.0f, 5.0f, 0.0f, Taper::Choice, kCharN},
    {"duck_on", "KICK / 808 DUCK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"duck_amount", "DUCK AMOUNT", "dB", 0.0f, 12.0f, 0.0f},
    {"duck_attack", "DUCK ATTACK", "ms", 0.1f, 30.0f, 5.0f, Taper::Log},
    {"duck_release", "DUCK RELEASE", "ms", 10.0f, 500.0f, 100.0f, Taper::Log},
    {"phase_on", "SUB PHASE ALIGN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"invert", "INVERT", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"link_lr", "LINK L+R", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"phase_angle", "PHASE ANGLE", "deg", 0.0f, 180.0f, 0.0f},
    {"track_on", "FUNDAMENTAL TRACKING", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"track_mode", "TRACKING MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kTrackN},
    {"track_sens", "TRACKING SENSITIVITY", "", 0.0f, 100.0f, 50.0f},
    {"manual_hz", "MANUAL NOTE", "Hz", 25.0f, 150.0f, 50.0f, Taper::Log},
    {"lowcut_on", "LOW-CUT CLEANUP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"lowcut_freq", "LOW-CUT FREQ", "Hz", 20.0f, 500.0f, 20.0f, Taper::Log},
    {"harm2", "2ND ORDER HARMONICS", "%", 0.0f, 100.0f, 0.0f},
    {"harm_weight", "HARMONICS WEIGHT", "", 0.0f, 100.0f, 0.0f},
    {"mix", "DRY / WET MIX", "%", 0.0f, 100.0f, 0.0f},
    {"mono_maker", "MONO-MAKER", "%", 0.0f, 100.0f, 0.0f},
    {"mono_cutoff", "MONO-MAKER CUTOFF", "Hz", 60.0f, 180.0f, 120.0f, Taper::Log},
    {"oversampling", "OVERSAMPLING", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kOsN},
    {"quality", "PROCESS QUALITY", "", 0.0f, 3.0f, 1.0f, Taper::Choice, kQualN},
};

const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "dB", -60, 0}, {"OUTPUT", "dB", -60, 0}, {"PEAK", "dB", -60, 0}, {"HEADROOM", "dB", 0, 60},
    {"FUNDAMENTAL", "Hz", 0, 250}, {"SUB LEVEL", "dB", -60, 0}, {"DUCK", "dB", -12, 0},
    {"MONO-LOCK", "", 0, 1}, {"PHASE ALIGNED", "", 0, 1}, {"CLIPPING", "dB", -12, 0},
};

constexpr float kLowSplit = 150.0f;   // the "low band" every low-end tool works on (ASSUMED)

class SubArchitect final : public hc::fx::Effect {
public:
    SubArchitect() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i >= kDuckAmt && i <= kDuckRel) return p(kDuckOn) > 0.5f;
        if (i >= kInvert && i <= kPhaseAngle) return p(kPhaseOn) > 0.5f;
        if (i == kTrackMode || i == kTrackSens) return p(kTrackOn) > 0.5f;
        if (i == kManualHz) return p(kTrackOn) > 0.5f && p(kTrackMode) > 0.5f;
        if (i >= kCutFreq && i <= kHarmWeight) return p(kCutOn) > 0.5f;
        if (i == kMonoCut) return p(kMonoAmt) > 0.0f;
        return true;
    }
    // constant: the 8x round trip; lower settings are padded so the DAW's compensation is always right
    std::uint32_t latency() const noexcept override { return osLatency(3); }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        det_.prepare(fs, 25.0f, 400.0f, 512);
        wetL_.assign(std::max<std::uint32_t>(maxBlock, kMaxBlock), 0.0f);
        wetR_.assign(wetL_.size(), 0.0f);
        for (auto* m : {&inM_, &outM_}) m->prepare(fs);
        padL_.prepare(64); padR_.prepare(64);
        trimG_.prepare(fs, 20.0, kit::dbToGain(p(kTrim)));
        reset();
    }
    void reset() noexcept override {
        det_.reset();
        for (auto& b : lowL_) b.clear();
        for (auto& b : lowR_) b.clear();
        for (auto& b : divLp_) b.clear();
        subLp_.clear(); clickHp_.clear(); clickLp_.clear(); voice_.clear(); cut_.clear(); harmLp_.clear();
        monoLp_.clear(); hilL_.clear(); hilR_.clear();
        for (auto& s : osL_) s.clear();
        for (auto& s : osR_) s.clear();
        lowEnv_ = {}; fastEnv_ = {}; slowEnv_ = {}; clickEnv_ = {}; clickSlow_ = {};
        flip_ = 1.0f; lastSign_ = 0.0f; phase_ = 0.0; f0_ = 0.0f; subLevel_ = 0.0f; duckDb_ = 0.0f; clipDb_ = 0.0f;
        keys_.fill(1e9f);
        padL_.clear(); padR_.clear(); wDc_.fill(0); hDc_.fill(0);
        lowSplitSet();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inM_.feed(L, n);
        det_.setHop(std::array<std::uint32_t, 4>{1024, 512, 256, 128}[static_cast<std::size_t>(p(kQuality))]);
        const float mix = p(kMix) * 0.01f;
        if (mix > 0.0f) {
            std::copy(L, L + n, wetL_.data()); std::copy(R, R + n, wetR_.data());
            wetChain(wetL_.data(), wetR_.data(), n);
            for (std::uint32_t i = 0; i < n; ++i) { L[i] += mix * (wetL_[i] - L[i]); R[i] += mix * (wetR_[i] - R[i]); }
        } else {
            // the detector keeps listening so the readout is live and a mix move starts in tune
            for (std::uint32_t i = 0; i < n; ++i) det_.push(lowOnly(0.5f * (L[i] + R[i])));
            updateF0();
        }
        monoMaker(L, R, n);
        output(L, R, n);
        outM_.feed(L, n);
        publish();
    }

private:
    static std::uint32_t osLatency(int os) noexcept {
        // halfband round trips at 2x, 4x, 8x rates expressed in base samples (rounded)
        const float l = os <= 0 ? 0.0f : os == 1 ? 15.0f : os == 2 ? 22.5f : 26.25f;
        return static_cast<std::uint32_t>(std::lround(l));
    }
    void lowSplitSet() noexcept {
        const auto lp = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, kLowSplit, 0.7071);
        detLp_.c = lp;
        for (auto& b : lowL_) b.c = lp;
        for (auto& b : lowR_) b.c = lp;
        const auto dl = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, 120.0, 0.7071);
        for (auto& b : divLp_) b.c = dl;
        subLp_.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, 90.0, 0.7071);
        clickHp_.c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 1200.0, 0.7071);
        clickLp_.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, 5000.0, 0.7071);
    }
    float lowOnly(float x) noexcept { return detLp_.tick(x); }

    void updateF0() noexcept {
        const float sens = p(kTrackSens) * 0.01f;
        const float need = 0.85f - 0.35f * sens;   // more sensitivity, lower confidence accepted (ASSUMED 0.5 .. 0.85)
        if (det_.hz() > 0.0f && det_.confidence() >= need) f0_ = det_.hz();
    }

    // ---------------------------------------------------------------- wet chain -------------
    void wetChain(float* l, float* r, std::uint32_t n) noexcept {
        const int ch = static_cast<int>(p(kCharacter));
        const float subG = kit::dbToGain(p(kSubDrive));
        const float punch = p(kPunch) * 0.01f, tight = p(kTight) * 0.01f;
        lowEnv_.set(fs_, 5, 80); fastEnv_.set(fs_, 0.5, 30); slowEnv_.set(fs_, 15, 250);
        clickEnv_.set(fs_, p(kDuckAtt), p(kDuckRel)); clickSlow_.set(fs_, 20, 200);
        const bool tracking = p(kTrackOn) > 0.5f;
        const bool manual = p(kTrackMode) > 0.5f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float mono = 0.5f * (l[i] + r[i]);
            const float low = lowOnly(mono);
            det_.push(low);
            if ((i & 63u) == 0) updateF0();
            const float env = lowEnv_.tick(std::fabs(low));

            // ---- the sub, one octave below the note ------------------------------------------
            float sub = 0.0f;
            if (tracking) {
                const float f = manual ? p(kManualHz) : f0_;
                if (f > 0.0f) {
                    phase_ += static_cast<double>(f * 0.5f) / fs_;
                    if (phase_ >= 1.0) phase_ -= 1.0;
                    sub = std::sin(kit::kTau * static_cast<float>(phase_)) * env * 1.4142f;
                }
            } else {
                // octave divider: a flip-flop on every downward zero crossing of the low band, smoothed to a sine-like tone
                float d = low;
                for (auto& b : divLp_) d = static_cast<float>(b.tick(d));
                const float sgn = d >= 0.0f ? 1.0f : -1.0f;
                if (sgn < 0.0f && lastSign_ > 0.0f) flip_ = -flip_;
                lastSign_ = sgn;
                sub = static_cast<float>(subLp_.tick(flip_ * env * 1.2f));
            }

            // ---- kick click -> duck the low end ---------------------------------------------
            float duck = 1.0f;
            if (p(kDuckOn) > 0.5f && p(kDuckAmt) > 0.0f) {
                const float click = static_cast<float>(clickLp_.tick(static_cast<float>(clickHp_.tick(mono))));
                const float ce = clickEnv_.tick(std::fabs(click)), cs = clickSlow_.tick(std::fabs(click));
                const float k = std::clamp((ce - 1.5f * cs) / std::max(cs, 1e-4f), 0.0f, 1.0f);
                duckDb_ = -p(kDuckAmt) * k;
                duck = kit::dbToGain(duckDb_);
            } else duckDb_ = 0.0f;

            // ---- punch (attack) and tightness (decay) on the low band -------------------------
            const float fe = fastEnv_.tick(std::fabs(low)), se = slowEnv_.tick(std::fabs(low));
            float lowGain = 1.0f;
            if (punch > 0.0f) lowGain *= 1.0f + punch * 3.0f * std::clamp((fe - se) / std::max(se, 1e-4f), 0.0f, 1.0f);   // up to +12 dB on the hit
            if (tight > 0.0f) lowGain *= 1.0f - tight * 0.875f * std::clamp((se - fe) / std::max(se, 1e-4f), 0.0f, 1.0f); // down to -18 dB on the tail

            const float lowL = static_cast<float>(lowL_[1].tick(static_cast<float>(lowL_[0].tick(l[i]))));
            const float lowR = static_cast<float>(lowR_[1].tick(static_cast<float>(lowR_[0].tick(r[i]))));
            const float addL = (lowGain - 1.0f) * lowL, addR = (lowGain - 1.0f) * lowR;
            const float subOut = sub * subG * duck;
            l[i] += addL + subOut + (duck - 1.0f) * lowL;
            r[i] += addR + subOut + (duck - 1.0f) * lowR;
            subLevel_ = std::max(subLevel_ * 0.9995f, std::fabs(subOut));
        }
        character(l, r, n, ch);
        phaseAlign(l, r, n);
        cleanup(l, r, n);
    }

    void character(float* l, float* r, std::uint32_t n, int ch) noexcept {
        switch (ch) {
        case 1:   // SUB BOOM: +3 dB shelf under 60 Hz
            if (setKey(0, 1)) voice_.set(hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, 60.0, 0.7, 3.0));
            voice_.run(l, r, n); break;
        case 2:   // ANALOG WEIGHT: gentle second-harmonic warmth on the lows (x^2, its DC removed)
            for (std::uint32_t i = 0; i < n; ++i) {
                const float al = 0.15f * l[i] * l[i], ar = 0.15f * r[i] * r[i];
                wDc_[0] += 0.0005f * (al - wDc_[0]); wDc_[1] += 0.0005f * (ar - wDc_[1]);
                l[i] += al - wDc_[0]; r[i] += ar - wDc_[1];
            }
            break;
        case 3:   // CHEST PUNCH: +4 dB at 100 Hz
            if (setKey(0, 3)) voice_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 100.0, 1.0, 4.0));
            voice_.run(l, r, n); break;
        case 4:   // MONO-LOCK 120 HZ: nothing below 120 Hz has width
            if (setKey(1, 120)) monoLp_.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, 120.0, 0.7071);
            for (std::uint32_t i = 0; i < n; ++i) {
                const float side = 0.5f * (l[i] - r[i]);
                const float sideLow = static_cast<float>(monoLp_.tick(side));
                l[i] -= sideLow; r[i] += sideLow;
            }
            break;
        default: break;   // CLEAN 808 and CUSTOM add no voicing
        }
    }

    void phaseAlign(float* l, float* r, std::uint32_t n) noexcept {
        if (p(kPhaseOn) < 0.5f) return;
        const float a = p(kPhaseAngle) * kit::kPi / 180.0f;
        const float pol = p(kInvert) > 0.5f ? -1.0f : 1.0f;
        const bool link = p(kLink) > 0.5f;
        const float cL = std::cos(a), sL = std::sin(a), cR = link ? cL : std::cos(-a), sR = link ? sL : std::sin(-a);
        for (std::uint32_t i = 0; i < n; ++i) {
            float re, im;
            hilL_.tick(l[i], re, im); l[i] = pol * (re * cL + im * sL);
            hilR_.tick(r[i], re, im); r[i] = pol * (re * cR + im * sR);
        }
    }

    void cleanup(float* l, float* r, std::uint32_t n) noexcept {
        if (p(kCutOn) < 0.5f) return;
        if (p(kCutFreq) > 20.5f) {
            if (setKey(2, p(kCutFreq))) cut_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kCutFreq), 0.7071));
            cut_.run(l, r, n);
        }
        const float h2 = p(kHarm2) * 0.01f;
        if (h2 > 0.0f) {
            // second-order harmonics of the lows; WEIGHT opens the harmonics' tone from 100 Hz to 2 kHz
            const float hz = 100.0f * std::pow(20.0f, p(kHarmWeight) * 0.01f);
            if (setKey(3, hz)) harmLp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hz, 0.7071));
            for (std::uint32_t i = 0; i < n; ++i) {
                hl_[0] = l[i] * l[i]; hr_[0] = r[i] * r[i];                 // x^2: the second harmonic
                harmLp_.run(hl_.data(), hr_.data(), 1);
                hDc_[0] += 0.0005f * (hl_[0] - hDc_[0]); hDc_[1] += 0.0005f * (hr_[0] - hDc_[1]);   // its DC removed
                l[i] += h2 * (hl_[0] - hDc_[0]); r[i] += h2 * (hr_[0] - hDc_[1]);
            }
        }
    }

    void monoMaker(float* l, float* r, std::uint32_t n) noexcept {
        const float amt = p(kMonoAmt) * 0.01f;
        if (amt <= 0.0f) return;
        if (setKey(4, p(kMonoCut))) monoMk_.c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, p(kMonoCut), 0.7071);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float side = 0.5f * (l[i] - r[i]);
            const float sideLow = static_cast<float>(monoMk_.tick(side)) * amt;
            l[i] -= sideLow; r[i] += sideLow;
        }
    }

    void output(float* l, float* r, std::uint32_t n) noexcept {
        const float gT = kit::dbToGain(p(kTrim));
        const std::uint32_t pad = osLatency(3) - osLatency(static_cast<int>(p(kOversample)));
        const bool hard = p(kHardClip) > 0.5f, soft = p(kSoftClip) > 0.5f;
        const int os = static_cast<int>(p(kOversample));
        float worst = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = trimG_.next(gT);
            l[i] *= g; r[i] *= g;
            if (os == 0) {
                if (hard || soft) { worst = std::max({worst, std::fabs(l[i]), std::fabs(r[i])}); l[i] = clip(l[i], hard); r[i] = clip(r[i], hard); }
            } else {
                l[i] = overs(osL_, l[i], os, hard, soft, worst);
                r[i] = overs(osR_, r[i], os, hard, soft, worst);
            }
            l[i] = kit::sane(padL_.process(l[i], pad)); r[i] = kit::sane(padR_.process(r[i], pad));
        }
        clipDb_ = worst > 1.0f ? -kit::gainToDb(worst) : 0.0f;
    }
    static float clip(float x, bool hard) noexcept { return hard ? kit::hardClip(x) : kit::softClip(x); }
    // oversampled clip: up through `stages` halfbands, clip at the high rate, back down
    static float overs(std::array<kit::Halfband2x, 3>& st, float x, int stages, bool hard, bool soft, float& worst) noexcept {
        std::array<float, 8> buf{}; int len = 1; buf[0] = x;
        for (int s = 0; s < stages; ++s) {
            std::array<float, 8> nb{};
            for (int k = 0; k < len; ++k) st[static_cast<std::size_t>(s)].up(buf[static_cast<std::size_t>(k)], nb[static_cast<std::size_t>(2 * k)], nb[static_cast<std::size_t>(2 * k + 1)]);
            buf = nb; len *= 2;
        }
        if (hard || soft)
            for (int k = 0; k < len; ++k) { worst = std::max(worst, std::fabs(buf[static_cast<std::size_t>(k)])); buf[static_cast<std::size_t>(k)] = clip(buf[static_cast<std::size_t>(k)], hard); }
        // each stage keeps separate histories for its up and down filters
        for (int s = stages - 1; s >= 0; --s) {
            len /= 2;
            for (int k = 0; k < len; ++k) buf[static_cast<std::size_t>(k)] = st[static_cast<std::size_t>(s)].down(buf[static_cast<std::size_t>(2 * k)], buf[static_cast<std::size_t>(2 * k + 1)]);
        }
        return buf[0];
    }

    bool setKey(int k, float v) noexcept { if (keys_[static_cast<std::size_t>(k)] == v) return false; keys_[static_cast<std::size_t>(k)] = v; return true; }

    void publish() noexcept {
        publishMeter(0, inM_.db()); publishMeter(1, outM_.db());
        publishMeter(2, outM_.db()); publishMeter(3, -std::min(0.0f, outM_.db()));
        publishMeter(4, f0_); publishMeter(5, kit::gainToDb(subLevel_)); publishMeter(6, duckDb_);
        publishMeter(7, (p(kMonoAmt) > 0.0f || static_cast<int>(p(kCharacter)) == 4) ? 1.0f : 0.0f);
        publishMeter(8, p(kPhaseOn) > 0.5f ? 1.0f : 0.0f);
        publishMeter(9, clipDb_);
        publishReduction(std::min(duckDb_, clipDb_));
    }

    double fs_ = 48000;
    kit::PitchDetector det_;
    hc::fx::Biquad detLp_;
    std::array<hc::fx::Biquad, 2> lowL_{}, lowR_{}, divLp_{};
    hc::fx::Biquad subLp_, clickHp_, clickLp_, monoLp_, monoMk_;
    hc::fx::Biquad2 voice_, cut_, harmLp_;
    kit::HilbertPair hilL_, hilR_;
    hc::fx::DelayLine padL_, padR_;
    hc::fx::Smoothed trimG_;
    std::array<kit::Halfband2x, 3> osL_{}, osR_{};
    hc::fx::Envelope lowEnv_, fastEnv_, slowEnv_, clickEnv_, clickSlow_;
    std::vector<float> wetL_, wetR_;
    std::array<float, 1> hl_{}, hr_{};
    std::array<float, 2> wDc_{}, hDc_{};
    std::array<float, 6> keys_{};
    float flip_ = 1, lastSign_ = 0, f0_ = 0, subLevel_ = 0, duckDb_ = 0, clipDb_ = 0;
    double phase_ = 0;
    kit::PeakMeter inM_, outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeSubArchitect() { return std::make_unique<SubArchitect>(); }

} // namespace hem::staging
