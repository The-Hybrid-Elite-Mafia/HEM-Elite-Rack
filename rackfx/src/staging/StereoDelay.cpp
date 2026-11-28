// =============================================================================
//  48 STEREO DELAY — a clean, precise stereo delay. TIME L / TIME R: with SYNC on, note values from
//  1/64 to 4 bars (straight, dotted, triplet) at the host tempo (TEMPO SOURCE SYNC) or a set BPM;
//  with SYNC off, 1..4000 ms. FEEDBACK L / R up to 100 % (capped: the repeats never build up).
//  DELAY MODE: NORMAL (both sides on TIME L / FEEDBACK L), PING PONG (the echoes bounce L -> R -> L),
//  DUAL (each side its own time and feedback), WIDE (as NORMAL, the right side 15 ms later and its
//  modulation turned the other way). The STEREO / PING PONG buttons are shortcuts for the mode.
//  In the loop: HPF and LPF, DAMPING (a gentle top roll-off per repeat), DIFFUSION (smears the
//  repeats into a wash), MOD DEPTH / RATE. WIDTH on the echoes, MIX.
//  Different from ECHOFORGE (25: analog colour, feedback past 100 %) and REELAURA (33: tape echo).
//  Dead flat on load: MIX 0 %.
//  ASSUMED: MOD DEPTH 100 % = +-3 ms; DIFFUSION = two all-passes in the loop, gain up to 0.6;
//  DAMPING 100 % = a one-pole low-pass at about 1.5 kHz in the loop.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kTimeLDiv, kTimeLMs, kFbL, kFbR, kTimeRDiv, kTimeRMs, kMode, kSync, kTempoSrc, kBpm, kHpf, kLpf, kMix, kWidth, kModDepth, kModRate, kDiff, kDamp, kBypass, kCount };
const char* const kDivN[] = {"1/64", "1/32 T", "1/32", "1/16 T", "1/16", "1/16 D", "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D", "1/2", "1/2 D", "1 BAR", "2 BARS", "4 BARS"};
constexpr std::array<float, 17> kDivBeats{1.0f / 16, 1.0f / 12, 1.0f / 8, 1.0f / 6, 1.0f / 4, 3.0f / 8, 1.0f / 3, 1.0f / 2, 3.0f / 4, 2.0f / 3, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f};
const char* const kModeN[] = {"NORMAL", "PING PONG", "DUAL", "WIDE"};
const char* const kSrcN[] = {"SYNC", "BPM"};
const ParamInfo kParams[kCount] = {
    {"time_l_div", "TIME L (NOTE)", "", 0.0f, 16.0f, 7.0f, Taper::Choice, kDivN},
    {"time_l_ms", "TIME L (MS)", "ms", 1.0f, 4000.0f, 250.0f, Taper::Log},
    {"feedback_l", "FEEDBACK L", "%", 0.0f, 100.0f, 0.0f},
    {"feedback_r", "FEEDBACK R", "%", 0.0f, 100.0f, 0.0f},
    {"time_r_div", "TIME R (NOTE)", "", 0.0f, 16.0f, 7.0f, Taper::Choice, kDivN},
    {"time_r_ms", "TIME R (MS)", "ms", 1.0f, 4000.0f, 375.0f, Taper::Log},
    {"delay_mode", "DELAY MODE", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kModeN},
    {"sync", "SYNC", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"tempo_source", "TEMPO / SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kSrcN},
    {"bpm", "BPM", "", 20.0f, 300.0f, 120.0f},
    {"hpf", "HPF", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"lpf", "LPF", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"mix", "MIX", "%", 0.0f, 100.0f, 0.0f},
    {"width", "WIDTH", "%", 0.0f, 100.0f, 100.0f},
    {"mod_depth", "MOD DEPTH", "%", 0.0f, 100.0f, 0.0f},
    {"mod_rate", "MOD RATE", "Hz", 0.01f, 10.0f, 0.5f, Taper::Log},
    {"diffusion", "DIFFUSION", "%", 0.0f, 100.0f, 0.0f},
    {"damping", "DAMPING", "%", 0.0f, 100.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"IN", "dB", -60, 6}, {"OUT", "dB", -60, 6}, {"TEMPO", "bpm", 20, 300}};

struct Ap {
    hc::fx::DelayLine d;
    void prepare(std::uint32_t m) { d.prepare(m + 4); }
    void clear() noexcept { d.clear(); }
    float tick(float x, float g, float len) noexcept { const float v = d.read(std::max(0.0f, len - 1.0f)); const float w = x + g * v; d.write(w); return v - g * w; }
};

class StereoDelay final : public hc::fx::Effect {
public:
    StereoDelay() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        const bool sync = p(kSync) > 0.5f;
        const int mode = static_cast<int>(p(kMode));
        if (i == kTimeLDiv || i == kTimeRDiv) return sync && (i == kTimeLDiv || (mode != 0 && mode != 3));
        if (i == kTimeLMs || i == kTimeRMs) return !sync && (i == kTimeLMs || (mode != 0 && mode != 3));
        if (i == kFbR) return mode == 1 || mode == 2;
        if (i == kBpm) return sync && p(kTempoSrc) > 0.5f;
        if (i == kTempoSrc) return sync;
        if (i == kModRate) return p(kModDepth) > 0.0f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : line_) d.prepare(static_cast<std::uint32_t>(fs * 8.2));   // 4 bars at 120 bpm, plus WIDE and modulation
        for (auto& c : ap_) for (auto& a : c) a.prepare(static_cast<std::uint32_t>(fs * 0.02));
        mix_.prepare(fs, 20.0, 0.0f); tl_.prepare(fs, 80.0, 0.0f); tr_.prepare(fs, 80.0, 0.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : line_) d.clear();
        for (auto& c : ap_) for (auto& a : c) a.clear();
        hp_.clear(); lp_.clear(); damp_.fill(0); ph_ = 0; fKey_ = -1; hpOn_ = lpOn_ = false; loaded_ = false; pkI_ = pkO_ = 0;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        const float mixT = p(kMix) * 0.01f;
        const int mode = static_cast<int>(p(kMode));
        const float bpm = p(kTempoSrc) > 0.5f ? p(kBpm) : std::max(tempo(), 20.0f);
        const float tLT = timeSamp(true, bpm), tRT0 = timeSamp(mode == 1 || mode == 2 ? false : true, bpm);
        const float tRT = mode == 3 ? tRT0 + 0.015f * static_cast<float>(fs_) : tRT0;
        if (!loaded_) { mix_.cur = mixT; tl_.cur = tLT; tr_.cur = tRT; loaded_ = true; }
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        float pkI = 0, pkO = 0;
        if (!wet) { for (std::uint32_t i = 0; i < n; ++i) pkI = std::max(pkI, std::max(std::fabs(L[i]), std::fabs(R[i]))); pkI_ = pkO_ = pkI; publish(bpm); return; }
        setFilters();
        const float fbL = std::min(1.0f, p(kFbL) * 0.01f), fbR = mode == 1 || mode == 2 ? std::min(1.0f, p(kFbR) * 0.01f) : fbL;
        const float dev = p(kModDepth) * 0.01f * 0.003f * static_cast<float>(fs_);
        const float inc = p(kModRate) / static_cast<float>(fs_);
        const float diff = 0.6f * p(kDiff) * 0.01f;
        const float dampC = p(kDamp) > 0.0f ? std::exp(-kit::kTau * (20000.0f * std::pow(1500.0f / 20000.0f, p(kDamp) * 0.01f)) / static_cast<float>(fs_)) : 0.0f;
        const float w = p(kWidth) * 0.01f;
        const float fs = static_cast<float>(fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i], xr = R[i];
            ph_ += static_cast<double>(inc); ph_ -= std::floor(ph_);
            const float m = dev * std::sin(kit::kTau * static_cast<float>(ph_));
            const float tl = tl_.next(tLT), tr = tr_.next(tRT);
            float yl = line_[0].read(std::max(0.0f, tl - 1.0f + m));
            float yr = line_[1].read(std::max(0.0f, tr - 1.0f + (mode == 3 ? -m : m)));
            // the loop: filters, damping, diffusion (all at most unity gain, so 100 % holds steady)
            if (hpOn_) { yl = hp_.l.tick(yl); yr = hp_.r.tick(yr); }
            if (lpOn_) { yl = lp_.l.tick(yl); yr = lp_.r.tick(yr); }
            if (dampC > 0.0f) { damp_[0] = yl + dampC * (damp_[0] - yl); damp_[1] = yr + dampC * (damp_[1] - yr); yl = damp_[0]; yr = damp_[1]; }
            if (diff > 0.0f) {
                yl = ap_[0][1].tick(ap_[0][0].tick(yl, diff, 0.0071f * fs), diff, 0.0113f * fs);
                yr = ap_[1][1].tick(ap_[1][0].tick(yr, diff, 0.0079f * fs), diff, 0.0127f * fs);
            }
            if (mode == 1) { line_[0].write(0.5f * (xl + xr) + fbR * yr); line_[1].write(fbL * yl); }
            else { line_[0].write(xl + fbL * yl); line_[1].write(xr + fbR * yr); }
            const float mm = 0.5f * (yl + yr), ss = 0.5f * (yl - yr) * w;
            const float el = mm + ss, er = mm - ss;
            const float mx = mix_.next(mixT);
            L[i] = kit::sane(xl + mx * (el - xl)); R[i] = kit::sane(xr + mx * (er - xr));
            pkI = std::max(pkI, std::max(std::fabs(xl), std::fabs(xr))); pkO = std::max(pkO, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
        pkI_ = pkI; pkO_ = pkO;
        publish(bpm);
    }

private:
    [[nodiscard]] float timeSamp(bool left, float bpm) const noexcept {
        float ms;
        if (p(kSync) > 0.5f) ms = kDivBeats[static_cast<std::size_t>(p(left ? kTimeLDiv : kTimeRDiv))] * 60000.0f / std::max(bpm, 20.0f);
        else ms = p(left ? kTimeLMs : kTimeRMs);
        return std::clamp(static_cast<float>(fs_) * ms * 0.001f, 1.0f, static_cast<float>(fs_ * 8.0));
    }
    void setFilters() noexcept {
        const float hp = p(kHpf), lp = p(kLpf), key = hp * 1e5f + lp;
        if (key == fKey_) return;
        fKey_ = key;
        if (hp > 20.01f) hpOn_ = true;
        if (lp < 19999.0f) lpOn_ = true;
        hp_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hp, 0.7071));
        lp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(lp, fs_ * 0.45), 0.7071));
    }
    void publish(float bpm) noexcept { publishMeter(0, kit::gainToDb(pkI_)); publishMeter(1, kit::gainToDb(pkO_)); publishMeter(2, bpm); }
    double fs_ = 48000;
    std::array<hc::fx::DelayLine, 2> line_{};
    std::array<std::array<Ap, 2>, 2> ap_{};
    hc::fx::Biquad2 hp_, lp_;
    hc::fx::Smoothed mix_, tl_, tr_;
    std::array<float, 2> damp_{};
    double ph_ = 0;
    float fKey_ = -1, pkI_ = 0, pkO_ = 0;
    bool hpOn_ = false, lpOn_ = false, loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeStereoDelay() { return std::make_unique<StereoDelay>(); }

} // namespace hem::staging
