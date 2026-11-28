// =============================================================================
//  21 BUSFOUNDRY — flexible bus dynamics (stepped VCA style): THRESHOLD, stepped ATTACK / RATIO /
//  RELEASE (or a VARIABLE release), KNEE, DETECTOR WEIGHT (how much the lows are ignored by the
//  detector: NORMAL flat, MEDIUM, STRONG), TOPOLOGY (FEEDFORWARD reads the input, FEEDBACK reads its
//  own output — softer, more "glue"), STEREO LINK (IND .. 100 %) with a LINK FILTER on the shared
//  part, ANALOG colour and an external sidechain (ON, or FEEDFORWARD ONLY).
//  Different from MIXBOND (32, fixed 2/4/10 glue) and VCA COMPRESSOR (52, smooth knobs).
//  Dead flat on load: COMP IN = OUT, ANALOG OFF, OUTPUT MAKEUP 0 dB, MANUAL makeup (verified spec,
//  COMP IN default chosen OUT so the plug-in loads flat).
// =============================================================================
#include "hem/rack/Sidechain.hpp"
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kThreshold, kMakeup, kAuto, kAttack, kRatio, kRelease, kVarRel, kKnee, kWeight, kTopology, kLink, kLinkFilt, kAnalog, kCompIn, kExtSc, kMeterSel, kClipReset, kBypass, kCount };
const char* const kAutoN[] = {"AUTO", "MANUAL"};
const char* const kAttN[] = {"0.03 MS", "0.1 MS", "0.3 MS", "1 MS", "3 MS", "10 MS", "30 MS"};
const char* const kRatioN[] = {"1.5", "2", "3", "4", "6", "10", "INF"};
const char* const kRelN[] = {"0.05 S", "0.1 S", "0.2 S", "0.5 S", "1 S", "2 S", "VAR"};
const char* const kKneeN[] = {"HARD", "MEDIUM", "SOFT"};
const char* const kWeightN[] = {"NORMAL", "MEDIUM", "STRONG"};
const char* const kTopoN[] = {"FEEDBACK", "FEEDFORWARD"};
const char* const kLinkN[] = {"IND", "50", "60", "70", "80", "90", "100"};
const char* const kLfN[] = {"OFF", "HP", "LP", "BP"};
const char* const kInN[] = {"OUT", "IN"};
const char* const kScN[] = {"OFF", "ON", "FEEDFORWARD ONLY"};
const char* const kMeterN[] = {"IN", "GR", "OUT"};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dB", -40.0f, 10.0f, 0.0f},
    {"makeup", "OUTPUT MAKEUP", "dB", -24.0f, 24.0f, 0.0f},
    {"auto_makeup", "MAKEUP MODE", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kAutoN},
    {"attack", "ATTACK", "", 0.0f, 6.0f, 3.0f, Taper::Choice, kAttN},
    {"ratio", "RATIO", "", 0.0f, 6.0f, 3.0f, Taper::Choice, kRatioN},
    {"release", "RELEASE", "", 0.0f, 6.0f, 3.0f, Taper::Choice, kRelN},
    {"var_release", "VARIABLE RELEASE", "s", 0.05f, 3.0f, 0.5f, Taper::Log},
    {"knee", "KNEE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kKneeN},
    {"weight", "DETECTOR WEIGHT", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kWeightN},
    {"topology", "TOPOLOGY", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kTopoN},
    {"link", "STEREO LINK", "", 0.0f, 6.0f, 6.0f, Taper::Choice, kLinkN},
    {"link_filter", "LINK FILTER", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kLfN},
    {"analog", "ANALOG", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"comp_in", "COMP IN", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kInN},
    {"ext_sc", "EXT SIDECHAIN", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kScN},
    {"meter", "METER", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kMeterN},
    {"clip_reset", "CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"LEFT VU", "dB", -20, 6, true}, {"RIGHT VU", "dB", -20, 6, true}, {"GR LEFT", "dB", -30, 0}, {"GR RIGHT", "dB", -30, 0}, {"CLIP", "", 0, 1}};
constexpr std::array<float, 7> kAttMs{0.03f, 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
constexpr std::array<float, 7> kRatioV{1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 1000.0f};
constexpr std::array<float, 6> kRelS{0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f};
constexpr std::array<float, 3> kKneeDb{0.0f, 6.0f, 12.0f};

class BusFoundry final : public hc::fx::Effect, public hem::rack::SidechainSink {
public:
    BusFoundry() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void setSidechain(const float* l, const float* r) noexcept override { scL_ = l; scR_ = r; }
    bool paramActive(int i) const noexcept override { return i != kVarRel || static_cast<int>(p(kRelease)) == 6; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs; in_.prepare(fs, 20.0, 0.0f); mk_.prepare(fs, 20.0, 1.0f); an_.prepare(fs, 20.0, 0.0f); thr_.prepare(fs, 20.0, p(kThreshold));
        reset();
    }
    void reset() noexcept override { for (auto& f : wf_) f.clear(); for (auto& f : lf_) f.clear(); env_.fill(0); gr_.fill(0); fbOut_.fill(0); vu_.fill(0); dc_.fill(0); clip_ = false; wKey_ = lKey_ = -1; }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kClipReset) > 0.5f) clip_ = false;
        if (p(kBypass) > 0.5f) { scL_ = scR_ = nullptr; publishAll(); return; }
        setFilters();
        const float inT = p(kCompIn) > 0.5f ? 1.0f : 0.0f, anT = p(kAnalog) > 0.5f ? 1.0f : 0.0f;
        const float ratio = kRatioV[static_cast<std::size_t>(p(kRatio))], knee = kKneeDb[static_cast<std::size_t>(p(kKnee))];
        const float ca = hc::fx::onePoleCoef(fs_, kAttMs[static_cast<std::size_t>(p(kAttack))]);
        const int ri = static_cast<int>(p(kRelease));
        const float cr = hc::fx::onePoleCoef(fs_, 1000.0 * (ri == 6 ? p(kVarRel) : kRelS[static_cast<std::size_t>(ri)]));
        const bool ff = p(kTopology) > 0.5f;
        const int ext = static_cast<int>(p(kExtSc));
        const bool useSc = scL_ && (ext == 1 || (ext == 2 && ff));
        static constexpr std::array<float, 7> kLinkV{0.0f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};
        const float link = kLinkV[static_cast<std::size_t>(p(kLink))];
        const bool lfOn = static_cast<int>(p(kLinkFilt)) > 0;
        const float mkT = kit::dbToGain(p(kMakeup) + (p(kAuto) < 0.5f ? autoMakeup(ratio) : 0.0f));
        const bool active = inT > 0.0f || in_.cur > 1e-6f || anT > 0.0f || an_.cur > 1e-6f || !kit::isFlat(mkT, 1.0f) || !kit::isFlat(mk_.cur, 1.0f);
        if (!active) { vuFeed(L, R, n); publishAll(); scL_ = scR_ = nullptr; return; }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float thr = thr_.next(p(kThreshold));
            // detector input per side: input (FF), own last output (FB) or the sidechain; WEIGHT filters it
            std::array<float, 2> d{useSc ? scL_[i] : ff ? L[i] : fbOut_[0], useSc ? scR_[i] : ff ? R[i] : fbOut_[1]};
            for (int c = 0; c < 2; ++c) if (wKey_ > 0.0f) d[static_cast<std::size_t>(c)] = static_cast<float>(wf_[static_cast<std::size_t>(c)].tick(d[static_cast<std::size_t>(c)]));
            // STEREO LINK: each side's level blended with the shared (optionally filtered) level
            float shared = std::max(std::fabs(d[0]), std::fabs(d[1]));
            if (lfOn) { const float m = 0.5f * (d[0] + d[1]); shared = std::fabs(static_cast<float>(lf_[1].tick(static_cast<float>(lf_[0].tick(m))))) * 1.41f; }
            std::array<float, 2> gDb{};
            for (int c = 0; c < 2; ++c) {
                const auto ci = static_cast<std::size_t>(c);
                const float x = (1.0f - link) * std::fabs(d[ci]) + link * shared;
                env_[ci] = x + (x > env_[ci] ? ca : cr) * (env_[ci] - x);
                const float over = kit::gainToDb(env_[ci]) - thr;
                float red = 0.0f;
                if (knee <= 0.0f) { if (over > 0.0f) red = -over * (1.0f - 1.0f / ratio); }
                else if (over > knee * 0.5f) red = -over * (1.0f - 1.0f / ratio);
                else if (over > -knee * 0.5f) red = -(1.0f - 1.0f / ratio) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                gr_[ci] = red;
                gDb[ci] = red;
            }
            const float k = in_.next(inT), mg = mk_.next(mkT), ak = an_.next(anT);
            float l = L[i] * kit::dbToGain(k * gDb[0]), r = R[i] * kit::dbToGain(k * gDb[1]);
            fbOut_[0] = l; fbOut_[1] = r;
            l *= mg; r *= mg;
            if (ak > 1e-6f) {   // ANALOG: a soft transformer curve and a whisper of 2nd harmonic
                float al = std::tanh(l * 1.2f) / 1.2f + 0.01f * l * l, ar = std::tanh(r * 1.2f) / 1.2f + 0.01f * r * r;   // x^2: the 2nd harmonic
                dc_[0] += 0.0005f * (al - l - dc_[0]); dc_[1] += 0.0005f * (ar - r - dc_[1]);
                al -= dc_[0]; ar -= dc_[1];
                l += ak * (al - l); r += ak * (ar - r);
            }
            if (std::fabs(l) >= 1.0f || std::fabs(r) >= 1.0f) clip_ = true;
            L[i] = kit::sane(l); R[i] = kit::sane(r);
        }
        vuFeed(L, R, n);
        publishAll();
        scL_ = scR_ = nullptr;
    }

private:
    // AUTO makeup: what a full-scale signal loses at this threshold and ratio, halved (ASSUMED)
    [[nodiscard]] float autoMakeup(float ratio) const noexcept { return std::max(0.0f, -p(kThreshold)) * (1.0f - 1.0f / ratio) * 0.5f; }
    void setFilters() noexcept {
        const int w = static_cast<int>(p(kWeight));
        const float wk = w == 0 ? -1.0f : w == 1 ? 90.0f : 180.0f;   // detector high-pass: NORMAL none, MEDIUM 90 Hz, STRONG 180 Hz
        if (wk != wKey_) { wKey_ = wk; if (wk > 0.0f) for (auto& f : wf_) f.c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, wk, 0.7071); }
        const int lf = static_cast<int>(p(kLinkFilt));
        if (lf != lKey_) {
            lKey_ = lf;
            if (lf == 1) { lf_[0].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 150.0, 0.7071); lf_[1].c = lf_[0].c; }
            else if (lf == 2) { lf_[0].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, 3000.0, 0.7071); lf_[1].c = lf_[0].c; }
            else if (lf == 3) { lf_[0].c = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 150.0, 0.7071); lf_[1].c = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, 3000.0, 0.7071); }
        }
    }
    void vuFeed(const float* L, const float* R, std::uint32_t n) noexcept {
        for (std::uint32_t i = 0; i < n; ++i) {
            vu_[0] = std::fabs(L[i]) + 0.9995f * (vu_[0] - std::fabs(L[i]));
            vu_[1] = std::fabs(R[i]) + 0.9995f * (vu_[1] - std::fabs(R[i]));
        }
    }
    void publishAll() noexcept {
        const bool gr = static_cast<int>(p(kMeterSel)) == 1;
        for (int c = 0; c < 2; ++c) publishMeter(c, std::clamp(gr ? gr_[static_cast<std::size_t>(c)] : kit::gainToDb(vu_[static_cast<std::size_t>(c)] * 1.11f) + 18.0f, -20.0f, 6.0f));
        publishMeter(2, gr_[0]); publishMeter(3, gr_[1]); publishMeter(4, clip_ ? 1.0f : 0.0f);
        publishReduction(std::min(gr_[0], gr_[1]));
    }
    double fs_ = 48000;
    std::array<hc::fx::Biquad, 2> wf_{}, lf_{};
    std::array<float, 2> env_{}, gr_{}, fbOut_{}, vu_{}, dc_{};
    float wKey_ = -1; int lKey_ = -1;
    bool clip_ = false;
    hc::fx::Smoothed in_, mk_, an_, thr_;
    const float* scL_ = nullptr; const float* scR_ = nullptr;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeBusFoundry() { return std::make_unique<BusFoundry>(); }

} // namespace hem::staging
