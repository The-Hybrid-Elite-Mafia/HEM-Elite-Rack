// =============================================================================
//  38 DRUMKEY — retunes the ring of a drum without touching the rest of the hit. A FOCUS band (FOCUS
//  LOW .. FOCUS HIGH, 98..988 Hz, dragged on the graph) is split out; while that band is over the
//  THRESHOLD the band is pitch-shifted by FORMANT SHIFT (-1200 .. +1200 cents), easing in and out at
//  SPEED (15..50 ms). PROCESSED TRIM sets the shifted band's level, FULL OUTPUT the overall level.
//  MONITOR plays only the (processed) focus band. MODE: QUALITY (40 ms grains, smoother on low toms
//  and kicks) or LOW LATENCY (16 ms grains, tighter on fast hits). The host wrapper cannot announce
//  a latency change, so both modes report the same fixed 20 ms (LOW LATENCY is padded).
//  Meters: input and output peak with clip LEDs (each with its reset), DETECTION (weak .. strong:
//  how far the band is over the threshold) and the focus centre.
//  Dead flat on load: THRESHOLD 0 dBFS = no processing (the dry signal, delayed by the latency),
//  SHIFT 0 cents, TRIM 0 dB, FULL OUTPUT 0 dB.
//  ASSUMED: focus split = Linkwitz-Riley 24 dB/oct at FOCUS LOW and FOCUS HIGH (while shifting, the rest of
//  the signal is an all-pass copy of the input: same level, slight phase turn); band level = peak follower 1 / 50 ms;
//  DETECTION shows 0..24 dB over the threshold; two-grain shifter with sin^2 windows.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kThr, kShift, kSpeed, kTrim, kFull, kFocusLo, kFocusHi, kMonitor, kMode, kInClipReset, kOutClipReset, kBypass, kCount };
const char* const kModeN[] = {"QUALITY", "LOW LATENCY"};
const ParamInfo kParams[kCount] = {
    {"threshold", "THRESHOLD", "dBFS", -70.0f, 0.0f, 0.0f},
    {"formant_shift", "FORMANT SHIFT", "cents", -1200.0f, 1200.0f, 0.0f},
    {"speed", "SPEED", "ms", 15.0f, 50.0f, 25.0f},
    {"processed_trim", "PROCESSED TRIM", "dB", -6.0f, 6.0f, 0.0f},
    {"full_output", "FULL OUTPUT", "dB", -24.0f, 12.0f, 0.0f},
    {"focus_low", "FOCUS LOW", "Hz", 98.0f, 988.0f, 150.0f, Taper::Log},
    {"focus_high", "FOCUS HIGH", "Hz", 98.0f, 988.0f, 500.0f, Taper::Log},
    {"monitor", "MONITOR", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"mode", "MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModeN},
    {"input_clip_reset", "INPUT CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"output_clip_reset", "OUTPUT CLIP RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "dBFS", -60, 0}, {"INPUT CLIP", "", 0, 1}, {"OUTPUT", "dBFS", -60, 0}, {"OUTPUT CLIP", "", 0, 1},
    {"DETECTION", "", 0, 1}, {"FOCUS", "Hz", 98, 988}};

constexpr std::uint32_t kSub = 8;

class DrumKey final : public hc::fx::Effect {
public:
    DrumKey() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return lat_; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        lat_ = static_cast<std::uint32_t>(std::lround(fs * 0.020));
        const auto cap = static_cast<std::uint32_t>(fs * 0.045) + 8;
        for (auto& d : dry_) d.prepare(lat_ + 4);
        for (auto& d : bandD_) d.prepare(lat_ + 4);
        for (auto& d : restD_) d.prepare(lat_ + 4);
        for (auto& d : shift_) d.prepare(cap);
        for (auto& g : gl_) g.prepare(fs, kSub, 30.0, 0.0f);
        trim_.prepare(fs, 20.0, 1.0f); full_.prepare(fs, 20.0, 1.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : dry_) d.clear();
        for (auto& d : bandD_) d.clear();
        for (auto& d : restD_) d.clear();
        for (auto& c : lpLo_) for (auto& f : c) f.clear();
        for (auto& c : hpHi_) for (auto& f : c) f.clear();
        for (auto& f : apHi_) f.clear();
        for (auto& d : shift_) d.clear();
        for (auto& c : hp_) for (auto& f : c) f.clear();
        for (auto& c : lp_) for (auto& f : c) f.clear();
        env_ = 0; eng_ = 0; ph_ = {0.5, 0.0}; key_ = -1; inPk_ = outPk_ = 0; inClip_ = outClip_ = false; det_ = 0; snap_ = true; loaded_ = false;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kInClipReset) > 0.5f) inClip_ = false;
        if (p(kOutClipReset) > 0.5f) outClip_ = false;
        const float lat = static_cast<float>(lat_);
        if (p(kBypass) > 0.5f) {   // bypass keeps the reported delay so the host's timing holds
            for (std::uint32_t i = 0; i < n; ++i) { dry_[0].write(L[i]); dry_[1].write(R[i]); L[i] = dry_[0].read(lat); R[i] = dry_[1].read(lat); }
            publishAll(); return;
        }
        const float trT = kit::dbToGain(p(kTrim)), fuT = kit::dbToGain(p(kFull));
        if (!loaded_) { trim_.cur = trT; full_.cur = fuT; loaded_ = true; }
        const float thr = p(kThr);
        const bool active = thr < -0.05f;
        const float ratio = std::pow(2.0f, p(kShift) / 1200.0f);
        const float engC = hc::fx::onePoleCoef(fs_, p(kSpeed));
        const float eA = hc::fx::onePoleCoef(fs_, 1.0), eR = hc::fx::onePoleCoef(fs_, 50.0);
        const float G = static_cast<float>(fs_ * (p(kMode) > 0.5f ? 0.016 : 0.040));
        const bool mon = p(kMonitor) > 0.5f;
        float inPk = 0, outPk = 0, detM = 0;
        for (std::uint32_t at = 0; at < n; at += kSub) {
            setFocus();
            snap_ = false;
            const std::uint32_t end = std::min(n, at + kSub);
            for (std::uint32_t i = at; i < end; ++i) {
                const float x[2] = {L[i], R[i]};
                inPk = std::max(inPk, std::max(std::fabs(x[0]), std::fabs(x[1])));
                std::array<float, 2> band{}, rest{};
                for (std::size_t c = 0; c < 2; ++c) {
                    // a Linkwitz-Riley split at FOCUS LOW and FOCUS HIGH: rest + band add back to an all-pass of the input,
                    // so the band really leaves the rest when it is shifted (the filters always run: moving them never clicks)
                    const float hiPart = hp_[c][1].tick(hp_[c][0].tick(x[c]));
                    const float loPart = apHi_[c].tick(lpLo_[c][1].tick(lpLo_[c][0].tick(x[c])));
                    band[c] = lp_[c][1].tick(lp_[c][0].tick(hiPart));
                    rest[c] = loPart + hpHi_[c][1].tick(hpHi_[c][0].tick(hiPart));
                }
                const float bl = std::max(std::fabs(band[0]), std::fabs(band[1]));
                env_ = bl + (bl > env_ ? eA : eR) * (env_ - bl);
                const float over = kit::gainToDb(env_ + 1e-9f) - thr;
                const float gate = active && over > 0.0f ? 1.0f : 0.0f;
                eng_ = gate + engC * (eng_ - gate);
                detM = std::max(detM, active ? std::clamp(over / 24.0f, 0.0f, 1.0f) : 0.0f);
                // shifter: two grains reading the band at a sliding delay around the latency
                const float r = 1.0f + eng_ * (ratio - 1.0f);
                if (eng_ < 1e-5f) ph_ = {0.5, 0.0};   // at rest a single grain sits exactly at the latency
                else for (auto& ph : ph_) { ph += static_cast<double>((1.0f - r) / G); ph -= std::floor(ph); }
                const float tr = trim_.next(trT), fu = full_.next(fuT);
                for (std::size_t c = 0; c < 2; ++c) {
                    shift_[c].write(band[c]);
                    dry_[c].write(x[c]);
                    bandD_[c].write(band[c]);
                    restD_[c].write(rest[c]);
                    const float xd = dry_[c].read(lat), bd = bandD_[c].read(lat), rd = restD_[c].read(lat);
                    float sh = 0;
                    for (const double ph : ph_) {
                        const float u = static_cast<float>(ph);
                        const float w = std::sin(kit::kPi * u);
                        sh += w * w * shift_[c].read(std::max(0.0f, lat + (u - 0.5f) * G));
                    }
                    const float procBand = bd + eng_ * (tr * sh - bd);
                    float y = mon ? procBand : xd + eng_ * (rd + tr * sh - xd);   // at rest: exactly the dry signal
                    y *= fu;
                    if (std::fabs(x[c]) >= 1.0f) inClip_ = true;
                    if (std::fabs(y) >= 1.0f) outClip_ = true;
                    outPk = std::max(outPk, std::fabs(y));
                    (c == 0 ? L : R)[i] = kit::sane(y);
                }
            }
        }
        inPk_ = inPk; outPk_ = outPk; det_ = detM;
        publishAll();
    }

private:
    void setFocus() noexcept {
        auto glide = [this](std::size_t k, float t) { auto& g = gl_[k]; const float v = std::log(t); if (snap_) g.cur = v; return std::exp(g.step(v)); };
        float lo = glide(0, p(kFocusLo)), hi = glide(1, p(kFocusHi));
        if (hi < lo * 1.1f) hi = lo * 1.1f;   // keep the band open
        const float key = lo * 1e4f + hi;
        if (key == key_) return;
        key_ = key;
        const auto h = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, lo, 0.7071), l = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hi, 0.7071);
        const auto ll = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, lo, 0.7071), hh = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hi, 0.7071);
        const auto ap = hc::fx::BiquadCoefs::make(BT::AllPass, fs_, hi, 0.7071);
        for (auto& c : hp_) for (auto& f : c) f.c = h;
        for (auto& c : lp_) for (auto& f : c) f.c = l;
        for (auto& c : lpLo_) for (auto& f : c) f.c = ll;
        for (auto& c : hpHi_) for (auto& f : c) f.c = hh;
        for (auto& f : apHi_) f.c = ap;
        focus_ = std::sqrt(lo * hi);
    }
    void publishAll() noexcept {
        publishMeter(0, kit::gainToDb(inPk_)); publishMeter(1, inClip_ ? 1.0f : 0.0f);
        publishMeter(2, kit::gainToDb(outPk_)); publishMeter(3, outClip_ ? 1.0f : 0.0f);
        publishMeter(4, det_); publishMeter(5, focus_);
    }
    double fs_ = 48000;
    std::uint32_t lat_ = 960;
    std::array<hc::fx::DelayLine, 2> dry_{}, bandD_{}, restD_{}, shift_{};
    std::array<std::array<hc::fx::Biquad, 2>, 2> hp_{}, lp_{}, lpLo_{}, hpHi_{};
    std::array<hc::fx::Biquad, 2> apHi_{};
    std::array<kit::BlockGlide, 2> gl_{};
    std::array<double, 2> ph_{0.5, 0.0};
    hc::fx::Smoothed trim_, full_;
    float env_ = 0, eng_ = 0, key_ = -1, inPk_ = 0, outPk_ = 0, det_ = 0, focus_ = 387;
    bool inClip_ = false, outClip_ = false, snap_ = true, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeDrumKey() { return std::make_unique<DrumKey>(); }

} // namespace hem::staging
