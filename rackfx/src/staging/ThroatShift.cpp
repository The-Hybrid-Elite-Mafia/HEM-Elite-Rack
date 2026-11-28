// =============================================================================
//  07 THROATSHIFT — creative pitch and formant shifting with movement (doubles, chipmunk,
//  monster, robot FLATTEN). An effect, not a tuner (NOTEFORGE corrects; this moves on purpose).
//  Its own engine: a grain cloud whose grains are placed at the new pitch period (PITCH) and
//  whose contents are read faster or slower (FORMANT), so the two move independently.
//  Four modulators: M1 / M2 (LFO or 16-step sequencer, tempo-synced), AM (envelope follower),
//  PT (pitch follower); each has its own depth on PITCH and on FORMANT.
//  Dead flat on load: MIX 0 % (the dry voice, delayed by the reported latency), every depth 0.
//  Latency: 3 periods of the lowest tracked pitch (65 Hz, C2) + 16 samples (about 46 ms at 48 kHz).
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

constexpr int kSteps = 16;
enum Top {
    kFlatOn, kFlatNote, kRange, kPitch, kPitchM1, kPitchM2, kPitchAm, kPitchPt,
    kCenter, kFine, kFormant, kFmtM1, kFmtM2, kFmtAm, kFmtPt, kMix,
    kM1Mode, kM1Rate, kM1Steps, kM1Phase, kM1Warp, kM1Smooth, kM1Level,
    kM2Mode, kM2Rate, kM2Steps, kM2Phase, kM2Warp, kM2Smooth, kM2Level,
    kAmLive, kAmAttack, kAmRelease, kAmLevel,
    kPtLive, kPtSmooth, kPtOffset, kPtLevel,
    kSeq1, kSeq2 = kSeq1 + kSteps, kBypass = kSeq2 + kSteps, kCount
};
const char* const kModeN[] = {"LFO", "SEQ"};
const char* const kRateN[] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
const char* const kRangeN[] = {"C2 - B3", "C3 - B4", "C4 - B5"};
const char* const kNotes[] = {"C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
                              "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
                              "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4", "C5"};

#define HEM_MOD(n, rate) \
    {"m" #n "_mode", "M" #n " MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN}, \
    {"m" #n "_rate", "M" #n " RATE", "", 0.0f, 5.0f, rate, Taper::Choice, kRateN}, \
    {"m" #n "_steps", "M" #n " STEPS", "", 1.0f, 16.0f, 8.0f}, \
    {"m" #n "_phase", "M" #n " PHASE", "deg", 0.0f, 360.0f, 0.0f}, \
    {"m" #n "_warp", "M" #n " WARP", "", 0.0f, 1.0f, 0.0f}, \
    {"m" #n "_smooth", "M" #n " SMOOTH", "", 0.0f, 100.0f, 0.0f}, \
    {"m" #n "_level", "M" #n " LEVEL", "", 0.0f, 1.0f, 1.0f}
#define HEM_STEP(m, i) {"m" #m "_step" #i, "M" #m " STEP " #i, "", -1.0f, 1.0f, 0.0f}
#define HEM_SEQ(m) HEM_STEP(m, 1), HEM_STEP(m, 2), HEM_STEP(m, 3), HEM_STEP(m, 4), HEM_STEP(m, 5), HEM_STEP(m, 6), \
    HEM_STEP(m, 7), HEM_STEP(m, 8), HEM_STEP(m, 9), HEM_STEP(m, 10), HEM_STEP(m, 11), HEM_STEP(m, 12), \
    HEM_STEP(m, 13), HEM_STEP(m, 14), HEM_STEP(m, 15), HEM_STEP(m, 16)

const ParamInfo kParams[kCount] = {
    {"flatten", "FLATTEN", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"flatten_note", "FLATTEN NOTE", "", 0.0f, 36.0f, 9.0f, Taper::Choice, kNotes},
    {"range", "RANGE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kRangeN},
    {"pitch", "PITCH", "st", -24.0f, 24.0f, 0.0f},
    {"pitch_m1", "PITCH M1", "%", -100.0f, 100.0f, 0.0f},
    {"pitch_m2", "PITCH M2", "%", -100.0f, 100.0f, 0.0f},
    {"pitch_am", "PITCH AM", "%", -100.0f, 100.0f, 0.0f},
    {"pitch_pt", "PITCH PT", "%", -100.0f, 100.0f, 0.0f},
    {"center", "CENTER LINK OFFSET", "st", -24.0f, 24.0f, 0.0f},
    {"fine", "FINE", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"formant", "FORMANT", "st", -12.0f, 12.0f, 0.0f},
    {"formant_m1", "FORMANT M1", "%", -100.0f, 100.0f, 0.0f},
    {"formant_m2", "FORMANT M2", "%", -100.0f, 100.0f, 0.0f},
    {"formant_am", "FORMANT AM", "%", -100.0f, 100.0f, 0.0f},
    {"formant_pt", "FORMANT PT", "%", -100.0f, 100.0f, 0.0f},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    HEM_MOD(1, 2.0f), HEM_MOD(2, 3.0f),
    {"am_live", "AM LIVE INPUT", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"am_attack", "AM ATTACK", "ms", 1.0f, 200.0f, 10.0f, Taper::Log},
    {"am_release", "AM RELEASE", "ms", 10.0f, 1000.0f, 120.0f, Taper::Log},
    {"am_level", "AM LEVEL", "", 0.0f, 1.0f, 1.0f},
    {"pt_live", "PT LIVE INPUT", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"pt_smooth", "PT SMOOTH", "", 0.0f, 100.0f, 0.0f},
    {"pt_offset", "PT OFFSET", "", -1.0f, 1.0f, 0.0f},
    {"pt_level", "PT LEVEL", "", 0.0f, 1.0f, 1.0f},
    HEM_SEQ(1), HEM_SEQ(2),
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_MOD
#undef HEM_STEP
#undef HEM_SEQ

const hc::fx::MeterInfo kMeters[] = {
    {"M1", "", -1, 1, true}, {"M2", "", -1, 1, true}, {"AM", "", 0, 1}, {"PT", "", -1, 1, true},
    {"PITCH NOW", "st", -24, 24, true}, {"FORMANT NOW", "st", -12, 12, true}, {"DETECTED", "Hz", 0, 1000},
};

constexpr std::uint32_t kRing = 16384;
constexpr float kLowHz = 65.0f;   // C2
constexpr float kModRange = 12.0f;   // a modulator at 100 % depth moves +-12 semitones (ASSUMED)

class ThroatShift final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    ThroatShift() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override {
        if (i == kFlatNote) return p(kFlatOn) > 0.5f;
        if (i == kM1Steps || (i >= kSeq1 && i < kSeq2)) return p(kM1Mode) > 0.5f;
        if (i == kM2Steps || (i >= kSeq2 && i < kBypass)) return p(kM2Mode) > 0.5f;
        if (i == kM1Warp) return p(kM1Mode) < 0.5f;
        if (i == kM2Warp) return p(kM2Mode) < 0.5f;
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        det_.prepare(fs, kLowHz, 1100.0f, 256);
        maxT_ = static_cast<std::uint32_t>(std::ceil(fs / kLowHz));
        lat_ = 3 * maxT_ + 16;
        inL_.assign(kRing, 0.0f); inR_.assign(kRing, 0.0f);
        accL_.assign(kRing, 0.0f); accR_.assign(kRing, 0.0f); nrm_.assign(kRing, 0.0f);
        mixG_.prepare(fs, 20.0, p(kMix) * 0.01f);
        reset();
    }
    void reset() noexcept override {
        det_.reset();
        for (auto* v : {&inL_, &inR_, &accL_, &accR_, &nrm_}) std::fill(v->begin(), v->end(), 0.0f);
        t_ = 0; nextSyn_ = -1; ana_ = 0; amEnv_ = 0; ptSm_ = 0; m1_ = m2_ = 0; m1Sm_ = m2Sm_ = 0; beat_ = 0; detHz_ = 0; T_ = 480;
        ctl_.fill({});
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; return; }
        static constexpr std::array<float, 3> kRangeLo{65.4f, 130.8f, 261.6f};
        det_.setRange(kRangeLo[static_cast<std::size_t>(p(kRange))], kRangeLo[static_cast<std::size_t>(p(kRange))] * 4.0f);
        const float amA = hc::fx::onePoleCoef(fs_, p(kAmAttack)), amR = hc::fx::onePoleCoef(fs_, p(kAmRelease));
        const double beatsPerSample = static_cast<double>(tempo()) / 60.0 / fs_;
        // MIX 0 (and faded out) = the delayed dry: the detector keeps its window filled but skips its analysis
        det_.setHop(p(kMix) <= 0.0f && mixG_.cur < 1e-5f ? (1u << 30) : 256);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::int64_t now = t_++;
            inL_[static_cast<std::size_t>(now) & (kRing - 1)] = L[i];
            inR_[static_cast<std::size_t>(now) & (kRing - 1)] = R[i];
            const float liveIn = 0.5f * (L[i] + R[i]);
            const float scIn = scL_ ? 0.5f * (scL_[i] + scR_[i]) : liveIn;
            det_.push(p(kPtLive) > 0.5f ? liveIn : scIn);
            const float amIn = std::fabs(p(kAmLive) > 0.5f ? liveIn : scIn);
            amEnv_ = amIn + (amIn > amEnv_ ? amA : amR) * (amEnv_ - amIn);
            beat_ += beatsPerSample;
            if ((now & 31) == 0) {
                control();
                Ctl& c = ctl_[static_cast<std::size_t>(now >> 5) & (kCtl - 1)];
                c = {pitchSt_, fmtSt_, T_, detHz_ > 0.0f};
            }
            const std::int64_t d = now - static_cast<std::int64_t>(lat_);
            const std::int64_t ci = (now - static_cast<std::int64_t>(lat_ - maxT_)) >> 5;
            const Ctl& cur = ci >= 0 ? ctl_[static_cast<std::size_t>(ci) & (kCtl - 1)] : ctl_[0];
            const float dryL = d >= 0 ? inL_[static_cast<std::size_t>(d) & (kRing - 1)] : 0.0f;
            const float dryR = d >= 0 ? inR_[static_cast<std::size_t>(d) & (kRing - 1)] : 0.0f;
            const float mix = mixG_.next(p(kMix) * 0.01f);
            float wl = dryL, wr = dryR;
            if (d >= 0 && (mix > 1e-5f)) grains(d, cur, wl, wr);
            else nextSyn_ = -1;
            L[i] = kit::sane(dryL + mix * (wl - dryL));
            R[i] = kit::sane(dryR + mix * (wr - dryR));
        }
        publishMeter(0, m1_); publishMeter(1, m2_); publishMeter(2, std::min(1.0f, amEnv_ * 4.0f)); publishMeter(3, ptNorm_);
        publishMeter(4, pitchSt_); publishMeter(5, fmtSt_); publishMeter(6, detHz_);
        scL_ = scR_ = nullptr;
    }

private:
    struct Ctl { float pitch = 0, fmt = 0, T = 480; bool voiced = false; };
    static constexpr std::size_t kCtl = 512;

    // LFO: a sine whose rise / fall balance is WARPed; SEQ: steps held, SMOOTH glides between them
    float modulator(int base, int seq, float& sm) const noexcept {
        static constexpr std::array<double, 6> kBeats{4.0, 2.0, 1.0, 0.5, 0.25, 0.125};
        const double len = kBeats[static_cast<std::size_t>(p(base + 1))];
        double ph = beat_ / len + static_cast<double>(p(base + 3)) / 360.0;
        ph -= std::floor(ph);
        float v = 0.0f;
        if (p(base) < 0.5f) {
            const float w = std::clamp(p(base + 4), 0.0f, 0.95f);       // warp: 0 = symmetric
            const float up = 0.5f * (1.0f - w);
            const float x = static_cast<float>(ph);
            const float tri = x < up ? x / up : 1.0f - (x - up) / (1.0f - up);
            v = -std::cos(kit::kPi * tri);
        } else {
            const int steps = std::clamp(static_cast<int>(p(base + 2)), 1, kSteps);
            const int s = std::min(steps - 1, static_cast<int>(ph * steps));
            v = p(seq + s);
        }
        const float c = p(base + 5) * 0.0099f;                          // SMOOTH 0 .. 100: how slowly it follows
        sm = v + c * (sm - v);
        return sm * p(base + 6);
    }

    void control() noexcept {
        m1_ = modulator(kM1Mode, kSeq1, m1Sm_);
        m2_ = modulator(kM2Mode, kSeq2, m2Sm_);
        const float am = std::min(1.0f, amEnv_ * 4.0f) * p(kAmLevel);
        detHz_ = det_.hz();
        if (detHz_ > 0.0f) T_ = static_cast<float>(fs_ / detHz_);
        // PT: where the sung pitch sits in the range, -1 .. +1, smoothed and offset
        const float lo = std::array<float, 3>{65.4f, 130.8f, 261.6f}[static_cast<std::size_t>(p(kRange))];
        const float pt = detHz_ > 0.0f ? std::clamp(std::log2(detHz_ / lo) - 1.0f, -1.0f, 1.0f) : ptSm_;
        const float k = p(kPtSmooth) * 0.0099f;
        ptSm_ = pt + k * (ptSm_ - pt);
        ptNorm_ = std::clamp(ptSm_ + p(kPtOffset), -1.0f, 1.0f) * p(kPtLevel);
        const float unit = p(kFine) > 0.5f ? 0.01f : 1.0f;              // FINE: the knobs move in cents
        auto mods = [&](int m1, int m2, int a, int t) {
            return kModRange * 0.01f * (p(m1) * m1_ + p(m2) * m2_ + p(a) * am + p(t) * ptNorm_);
        };
        float pitch = p(kPitch) * unit + p(kCenter) * unit + mods(kPitchM1, kPitchM2, kPitchAm, kPitchPt);
        if (p(kFlatOn) > 0.5f && detHz_ > 0.0f) {                       // FLATTEN: every note lands on one pitch
            const float note = 36.0f + p(kFlatNote);
            pitch += note - kit::hzToMidi(detHz_);
        }
        pitchSt_ = std::clamp(pitch, -36.0f, 36.0f);
        fmtSt_ = std::clamp(p(kFormant) * unit + p(kCenter) * unit + mods(kFmtM1, kFmtM2, kFmtAm, kFmtPt), -12.0f, 12.0f);
    }

    // grains two periods long, laid one new period apart; each grain's contents read at the formant rate
    void grains(std::int64_t d, const Ctl& c, float& yl, float& yr) noexcept {
        const float T = c.voiced ? c.T : 480.0f;
        const float ratio = c.voiced ? std::exp2(c.pitch / 12.0f) : 1.0f;
        const float fr = std::exp2(c.fmt / 12.0f);
        if (nextSyn_ < 0.0) { nextSyn_ = static_cast<double>(d); ana_ = static_cast<double>(d); }
        const int g = static_cast<int>(std::min(T, static_cast<float>(maxT_)));
        while (nextSyn_ - g <= static_cast<double>(d)) {
            while (ana_ + T <= nextSyn_) ana_ += T;
            while (ana_ > nextSyn_) ana_ -= T;
            const auto s = static_cast<std::int64_t>(std::llround(nextSyn_));
            for (int k = -g; k <= g; ++k) {
                const float w = 0.5f + 0.5f * std::cos(kit::kPi * static_cast<float>(k) / static_cast<float>(g));
                const double src = ana_ + static_cast<double>(k) * fr;   // formant: the grain's contents squeezed / stretched
                const auto i0 = static_cast<std::int64_t>(std::floor(src));
                const float f = static_cast<float>(src - static_cast<double>(i0));
                const auto a = static_cast<std::size_t>(i0) & (kRing - 1), b = static_cast<std::size_t>(i0 + 1) & (kRing - 1);
                const auto dst = static_cast<std::size_t>(s + k) & (kRing - 1);
                accL_[dst] += w * (inL_[a] + f * (inL_[b] - inL_[a]));
                accR_[dst] += w * (inR_[a] + f * (inR_[b] - inR_[a]));
                nrm_[dst] += w;
            }
            nextSyn_ += static_cast<double>(T / ratio);
        }
        const auto at = static_cast<std::size_t>(d) & (kRing - 1);
        const float nn = std::max(nrm_[at], 0.25f);
        yl = accL_[at] / nn; yr = accR_[at] / nn;
        accL_[at] = accR_[at] = nrm_[at] = 0.0f;
    }

    double fs_ = 48000;
    kit::PitchDetector det_;
    std::uint32_t maxT_ = 739, lat_ = 2233;
    std::vector<float> inL_, inR_, accL_, accR_, nrm_;
    std::int64_t t_ = 0;
    double nextSyn_ = -1, ana_ = 0, beat_ = 0;
    float amEnv_ = 0, ptSm_ = 0, ptNorm_ = 0, m1_ = 0, m2_ = 0, m1Sm_ = 0, m2Sm_ = 0, detHz_ = 0, T_ = 480, pitchSt_ = 0, fmtSt_ = 0;
    std::array<Ctl, kCtl> ctl_{};
    hc::fx::Smoothed mixG_;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeThroatShift() { return std::make_unique<ThroatShift>(); }

} // namespace hem::staging
