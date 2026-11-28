// =============================================================================
//  25 ECHOFORGE — modulated character delay: two delay times (1..5000 ms; HOST, BPM or MS, with
//  TAP), DUAL LINK, PING PONG with DIRECTION, per-side POLARITY, FEEDBACK 0..200 % (past 100 % the
//  repeats build until a soft saturator in the loop holds them), MODULATION up to audio rate
//  (0.1 Hz .. 6 kHz: chorus-like wobble to FM grit), HP / LP in the loop, CHARACTER (OFF, 1..4:
//  progressively darker, grittier, noisier analog echoes) and LOFI (bit and rate reduction).
//  Different from STEREO DELAY (48): clean digital, feedback capped at 100 %, diffusion.
//  Dead flat on load: WET / DRY 0 %, OUTPUT 0 dB (the dry signal passes exactly).
//  MIDI LEARN is a host / faceplate feature, not part of the sound.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;

enum P {
    kTimeL, kTimeR, kSyncMode, kBpm, kTap, kLink, kPingPong, kDirection, kPolL, kPolR, kFeedback,
    kModDepth, kModRate, kHp, kLp, kFiltLink, kFiltOff, kCharacter, kLofi, kMix, kOutput, kPeakReset, kBypass, kCount
};
const char* const kSyncN[] = {"HOST", "BPM", "MS"};
const char* const kDirN[] = {"L -> R", "R -> L"};
const char* const kCharN[] = {"OFF", "1", "2", "3", "4"};
const ParamInfo kParams[kCount] = {
    {"time_l", "LEFT DELAY", "ms", 1.0f, 5000.0f, 250.0f, Taper::Log},
    {"time_r", "RIGHT DELAY", "ms", 1.0f, 5000.0f, 375.0f, Taper::Log},
    {"sync_mode", "SYNC MODE", "", 0.0f, 2.0f, 2.0f, Taper::Choice, kSyncN},
    {"bpm", "BPM", "", 40.0f, 300.0f, 120.0f},
    {"tap", "TAP", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"dual_link", "DUAL LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"ping_pong", "PING PONG", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"direction", "DIRECTION", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kDirN},
    {"polarity_l", "POLARITY L", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"polarity_r", "POLARITY R", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"feedback", "FEEDBACK", "%", 0.0f, 200.0f, 0.0f},
    {"mod_depth", "MODULATION DEPTH", "%", 0.0f, 100.0f, 0.0f},
    {"mod_rate", "MODULATION RATE", "Hz", 0.1f, 6000.0f, 0.5f, Taper::Log},
    {"high_pass", "HIGH PASS", "Hz", 20.0f, 2000.0f, 20.0f, Taper::Log},
    {"low_pass", "LOW PASS", "Hz", 500.0f, 20000.0f, 20000.0f, Taper::Log},
    {"filter_link", "FILTER LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"filter_off", "FILTER OFF", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"character", "CHARACTER", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kCharN},
    {"lofi", "LOFI", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"mix", "WET / DRY", "%", 0.0f, 100.0f, 0.0f},
    {"output", "OUTPUT", "dB", -18.0f, 18.0f, 0.0f},
    {"peak_reset", "PEAK RESET", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"OUTPUT L", "dB", -36, 0}, {"OUTPUT R", "dB", -36, 0}, {"PEAK L", "dB", -36, 0}, {"PEAK R", "dB", -36, 0}, {"TEMPO", "bpm", 40, 300}};

class EchoForge final : public hc::fx::Effect {
public:
    EchoForge() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kTimeR) return p(kLink) < 0.5f;
        if (i == kBpm || i == kTap) return static_cast<int>(p(kSyncMode)) == 1;
        if (i == kDirection) return p(kPingPong) > 0.5f;
        if (i == kHp || i == kLp || i == kFiltLink) return p(kFiltOff) < 0.5f;
        if (i == kModRate) return p(kModDepth) > 0.0f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : line_) d.prepare(static_cast<std::uint32_t>(fs * 5.2));
        mix_.prepare(fs, 20.0, 0.0f); out_.prepare(fs, 20.0, 1.0f);
        tl_.prepare(fs, 80.0, msToSamp(p(kTimeL))); tr_.prepare(fs, 80.0, msToSamp(p(kTimeR)));
        mL_.prepare(fs); mR_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : line_) d.clear();
        for (auto& f : lp_) f.clear();
        for (auto& f : hp_) f.clear();
        modPh_ = 0; primed_ = false; loaded_ = false; filtOn_ = false; tapBpm_ = 120; tapPrev_ = false; sinceTap_ = 0; hold_.fill(0); holdN_ = 0; rng_ = 99u; pk_.fill(0); fKey_ = -1;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        if (p(kPeakReset) > 0.5f) pk_.fill(0);
        // TAP: two presses set the BPM (between 40 and 300)
        const bool tap = p(kTap) > 0.5f;
        sinceTap_ += n;
        if (tap && !tapPrev_) {
            const double sec = static_cast<double>(sinceTap_) / fs_;
            if (sec > 0.2 && sec < 1.5) tapBpm_ = static_cast<float>(60.0 / sec);
            sinceTap_ = 0;
        }
        tapPrev_ = tap;
        const float mixT = p(kMix) * 0.01f, oT = kit::dbToGain(p(kOutput));
        if (!loaded_) { mix_.cur = mixT; out_.cur = oT; loaded_ = true; }   // a loaded session starts where it was saved
        const bool wet = mixT > 0.0f || mix_.cur > 1e-6f;
        if (!wet) {
            if (!(kit::isFlat(oT, 1.0f) && kit::isFlat(out_.cur, 1.0f))) for (std::uint32_t i = 0; i < n; ++i) { const float o = out_.next(oT); L[i] *= o; R[i] *= o; }
            meters(L, R, n); return;
        }
        setFilters();
        const float tLT = msToSamp(p(kTimeL)), tRT = msToSamp(p(kLink) > 0.5f ? p(kTimeL) : p(kTimeR));
        const float fb = p(kFeedback) * 0.01f;
        const int ch = static_cast<int>(p(kCharacter));
        const bool pp = p(kPingPong) > 0.5f, r2l = p(kDirection) > 0.5f, lofi = p(kLofi) > 0.5f, filt = p(kFiltOff) < 0.5f;
        const float polL = p(kPolL) > 0.5f ? -1.0f : 1.0f, polR = p(kPolR) > 0.5f ? -1.0f : 1.0f;
        const float depth = p(kModDepth) * 0.01f, rate = p(kModRate);
        // CHARACTER 1..4: more drive, less top, more noise in the loop (ASSUMED steps)
        static constexpr std::array<float, 5> kDrive{0.0f, 1.2f, 2.0f, 3.0f, 4.5f}, kNoise{0.0f, 2e-5f, 5e-5f, 1e-4f, 2e-4f};
        if (!primed_) { tl_.cur = tLT; tr_.cur = tRT; primed_ = true; }   // start at the set times, no glide from the old ones
        const int holdEvery = lofi ? std::max(1, static_cast<int>(fs_ / 11025.0)) : 1;   // LOFI: about 11 kHz, 8-bit
        for (std::uint32_t i = 0; i < n; ++i) {
            const float tl = tl_.next(tLT), tr = tr_.next(tRT);
            modPh_ += static_cast<double>(rate) / fs_; modPh_ -= std::floor(modPh_);
            const float mod = std::sin(kit::kTau * static_cast<float>(modPh_));
            // depth: up to +-5 ms of wobble at low rates, shrinking as the rate rises into FM territory
            const float dev = depth * static_cast<float>(fs_) * 0.005f / (1.0f + rate / 50.0f);
            float yl = line_[0].read(std::max(0.0f, tl - 1.0f + dev * mod));   // read before this sample is written: d - 1 lands d samples late
            float yr = line_[1].read(std::max(0.0f, tr - 1.0f - dev * mod));
            if (filt && filtOn_) {
                yl = static_cast<float>(lp_[0].tick(static_cast<float>(hp_[0].tick(yl))));
                yr = static_cast<float>(lp_[1].tick(static_cast<float>(hp_[1].tick(yr))));
            }
            if (ch > 0) {
                const float d = kDrive[static_cast<std::size_t>(ch)];
                yl = std::tanh(yl * d) / d; yr = std::tanh(yr * d) / d;
                rng_ = rng_ * 1664525u + 1013904223u;
                const float nz = (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f) * kNoise[static_cast<std::size_t>(ch)];
                yl += nz; yr -= nz;
            }
            if (lofi) {
                if (holdN_++ % holdEvery == 0) { hold_[0] = std::round(yl * 127.0f) / 127.0f; hold_[1] = std::round(yr * 127.0f) / 127.0f; }
                yl = hold_[0]; yr = hold_[1];
            }
            // feedback above 100 % grows; a soft saturator in the loop keeps it bounded
            auto loop = [&](float v) { return fb > 1.0f ? std::tanh(v) : v; };
            const float inL = L[i], inR = R[i];
            if (pp) {
                const float mono = 0.5f * (inL + inR);
                if (!r2l) { line_[0].write(loop(mono + fb * yr)); line_[1].write(loop(fb * yl)); }
                else { line_[1].write(loop(mono + fb * yl)); line_[0].write(loop(fb * yr)); }
            } else {
                line_[0].write(loop(inL + fb * yl)); line_[1].write(loop(inR + fb * yr));
            }
            const float mx = mix_.next(mixT), o = out_.next(oT);
            L[i] = kit::sane((inL + mx * (polL * yl - inL)) * o);
            R[i] = kit::sane((inR + mx * (polR * yr - inR)) * o);
        }
        meters(L, R, n);
    }

private:
    [[nodiscard]] float msToSamp(float ms) const noexcept {
        const int mode = static_cast<int>(p(kSyncMode));
        if (mode != 2) {   // HOST or BPM: the knob's ms snap to the nearest 1/16 of the beat
            const float bpm = mode == 0 ? std::max(tempo(), 20.0f) : (tapBpm_ != 120.0f ? tapBpm_ : p(kBpm));
            const float six = 15000.0f / bpm;
            ms = std::max(six, std::round(ms / six) * six);
        }
        return std::clamp(static_cast<float>(fs_) * ms * 0.001f, 1.0f, static_cast<float>(fs_ * 5.0));
    }
    void setFilters() noexcept {
        const float hp = p(kHp), lp = p(kFiltLink) > 0.5f ? std::clamp(p(kHp) * 16.0f, 500.0f, 20000.0f) : p(kLp);   // LINK: the band stays four octaves wide
        const int ch = static_cast<int>(p(kCharacter));
        const float lpc = std::min(lp, ch == 0 ? 20000.0f : 12000.0f / static_cast<float>(ch));   // character darkens the repeats
        const float key = hp * 100000.0f + lpc;
        if (key == fKey_) return;
        fKey_ = key;
        if (hp > 20.01f || lpc < 19999.0f) filtOn_ = true;   // at 20 Hz / 20 kHz the loop is left untouched; once moved, keep running (no click)
        for (auto& f : hp_) f.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, fs_, hp, 0.7071);
        for (auto& f : lp_) f.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::LowPass, fs_, std::min<double>(lpc, fs_ * 0.45), 0.7071);
    }
    void meters(const float* L, const float* R, std::uint32_t n) noexcept {
        mL_.feed(L, n); mR_.feed(R, n);
        pk_[0] = std::max(pk_[0], mL_.env); pk_[1] = std::max(pk_[1], mR_.env);
        publishMeter(0, mL_.db()); publishMeter(1, mR_.db()); publishMeter(2, kit::gainToDb(pk_[0])); publishMeter(3, kit::gainToDb(pk_[1]));
        publishMeter(4, static_cast<int>(p(kSyncMode)) == 0 ? tempo() : tapBpm_ != 120.0f ? tapBpm_ : p(kBpm));
    }
    double fs_ = 48000;
    std::array<hc::fx::DelayLine, 2> line_{};
    std::array<hc::fx::Biquad, 2> lp_{}, hp_{};
    hc::fx::Smoothed mix_, out_, tl_, tr_;
    double modPh_ = 0;
    float tapBpm_ = 120, fKey_ = -1;
    bool tapPrev_ = false, primed_ = false, filtOn_ = false, loaded_ = false;
    std::uint64_t sinceTap_ = 0;
    std::array<float, 2> hold_{}, pk_{};
    int holdN_ = 0;
    std::uint32_t rng_ = 99u;
    kit::PeakMeter mL_, mR_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeEchoForge() { return std::make_unique<EchoForge>(); }

} // namespace hem::staging
