// =============================================================================
//  24 CHAMBERPLATE — two vintage ambience engines on one panel, run in series:
//    CHAMBER: an echo-chamber model (a speaker playing into a hard room, picked up by mics) plus a
//      tape delay. Input faders -> CHAMBER INPUT FILTER (top cut, bass cut, bell) -> two parallel
//      paths: TAPE DELAY (left / right times, link, host sync, feedback with its own filter, tape
//      drive and wow / flutter) and the CHAMBER (ROOM A / B / C, SPEAKER TYPE, FACING, MIC TYPE,
//      MIC POSITION, TIME MULTIPLIER). CHAMBER / TAPE BALANCE blends the two, WET / DRY blends the
//      result with the dry signal, then OUTPUT.
//    PLATE: a steel-plate model (a cross-coupled diffusion tank, stereo in, stereo out). Input
//      faders (link) -> CROSSTALK, DRIVE, BASS CUT, PREDELAY -> plate (CHARACTER A..D, DAMPER) ->
//      TREBLE, ANALOG NOISE -> WET / DRY -> output faders (link).
//  Both reverbs are written for this plugin only (no shared rack reverb engine).
//  Dead flat on load: both WET / DRY at 0 / 100 and every fader at 0 dB.
//  ASSUMED: tape and chamber run in parallel inside the CHAMBER engine (the balance knob blends
//  them); room decay A 0.7 s / B 1.4 s / C 2.4 s; plate decay A 1.3 / B 2.2 / C 3.2 / D 4.8 s;
//  DAMPER 0..10 shortens the plate down to about 12 % of its decay; both LINK switches on.
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
    kCInL, kCInR, kTopCut, kBassCut, kBellFreq, kBellGain, kTapeL, kTapeR, kTapeLink, kTapeSync,
    kFeedback, kDrive, kMod, kFbTop, kFbBass, kFbMid, kRoom, kSpeaker, kFacing, kMicType, kMicPos,
    kTimeMult, kTapeOn, kChamberOn, kBalance, kCMix, kCOut,
    kPInL, kPInR, kPInLink, kCharacter, kPOutL, kPOutR, kPOutLink, kDamper, kPBass, kPDrive, kPNoise,
    kTreble, kPredelay, kPMix, kCrosstalk, kBypass, kCount
};
const char* const kRoomN[] = {"A", "B", "C"};
const char* const kSpeakerN[] = {"SMALL CONE", "LARGE CONE", "HORN"};
const char* const kFacingN[] = {"TO MICS", "TO WALL"};
const char* const kMicN[] = {"RIBBON", "CONDENSER", "DYNAMIC"};
const char* const kCharN[] = {"A", "B", "C", "D"};
const ParamInfo kParams[kCount] = {
    {"chamber_in_l", "CHAMBER INPUT L", "dB", -24.0f, 12.0f, 0.0f},
    {"chamber_in_r", "CHAMBER INPUT R", "dB", -24.0f, 12.0f, 0.0f},
    {"top_cut", "INPUT TOP CUT", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"bass_cut", "INPUT BASS CUT", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"bell_freq", "INPUT BELL FREQ", "Hz", 200.0f, 8000.0f, 1000.0f, Taper::Log},
    {"bell_gain", "INPUT BELL GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"tape_left", "TAPE DELAY LEFT", "ms", 1.0f, 2000.0f, 120.0f, Taper::Log},
    {"tape_right", "TAPE DELAY RIGHT", "ms", 1.0f, 2000.0f, 180.0f, Taper::Log},
    {"tape_link", "TAPE LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"tape_sync", "TAPE SYNC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
    {"feedback", "FEEDBACK", "%", 0.0f, 95.0f, 0.0f},
    {"drive", "DRIVE", "", 0.0f, 100.0f, 0.0f},
    {"modulation", "MODULATION", "", 0.0f, 100.0f, 0.0f},
    {"fb_top_cut", "FEEDBACK TOP CUT", "Hz", 1000.0f, 20000.0f, 20000.0f, Taper::Log},
    {"fb_bass_cut", "FEEDBACK BASS CUT", "Hz", 20.0f, 1000.0f, 20.0f, Taper::Log},
    {"fb_mid_gain", "FEEDBACK MID GAIN", "dB", -12.0f, 12.0f, 0.0f},
    {"room", "ROOM", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kRoomN},
    {"speaker", "SPEAKER TYPE", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kSpeakerN},
    {"facing", "FACING", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kFacingN},
    {"mic_type", "MIC TYPE", "", 0.0f, 2.0f, 1.0f, Taper::Choice, kMicN},
    {"mic_position", "MIC POSITION", "%", 0.0f, 100.0f, 50.0f},
    {"time_mult", "TIME MULTIPLIER", "x", 0.25f, 4.0f, 1.0f, Taper::Log},
    {"tape_on", "TAPE ON", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"chamber_on", "CHAMBER ON", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"balance", "CHAMBER / TAPE BALANCE", "%", 0.0f, 100.0f, 50.0f},
    {"chamber_mix", "CHAMBER WET / DRY", "%", 0.0f, 100.0f, 0.0f},
    {"chamber_out", "CHAMBER OUTPUT", "dB", -24.0f, 12.0f, 0.0f},
    {"plate_in_l", "PLATE INPUT L", "dB", -24.0f, 12.0f, 0.0f},
    {"plate_in_r", "PLATE INPUT R", "dB", -24.0f, 12.0f, 0.0f},
    {"plate_in_link", "PLATE INPUT LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"character", "PLATE CHARACTER", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kCharN},
    {"plate_out_l", "PLATE OUTPUT L", "dB", -24.0f, 12.0f, 0.0f},
    {"plate_out_r", "PLATE OUTPUT R", "dB", -24.0f, 12.0f, 0.0f},
    {"plate_out_link", "PLATE OUTPUT LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"damper", "DAMPER", "", 0.0f, 10.0f, 0.0f},
    {"plate_bass_cut", "PLATE BASS CUT", "", 0.0f, 100.0f, 0.0f},
    {"plate_drive", "PLATE DRIVE", "", 0.0f, 100.0f, 0.0f},
    {"analog_noise", "ANALOG NOISE", "", 0.0f, 100.0f, 0.0f},
    {"treble", "TREBLE", "dB", -12.0f, 12.0f, 0.0f},
    {"predelay", "PREDELAY", "ms", 0.0f, 250.0f, 0.0f},
    {"plate_mix", "PLATE WET / DRY", "%", 0.0f, 100.0f, 0.0f},
    {"crosstalk", "CROSSTALK", "%", 0.0f, 100.0f, 0.0f},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
const hc::fx::MeterInfo kMeters[] = {{"CHAMBER OUT", "dBFS", -60, 0}, {"CHAMBER CLIP", "", 0, 1}, {"PLATE OUT", "dBFS", -60, 0}, {"PLATE CLIP", "", 0, 1}};

// Schroeder allpass on its own line; read-before-write, so `len` is the exact loop delay.
struct Ap {
    hc::fx::DelayLine d;
    void prepare(std::uint32_t maxLen) { d.prepare(maxLen + 4); }
    void clear() noexcept { d.clear(); }
    float tick(float x, float g, float len) noexcept {
        const float v = d.read(std::max(0.0f, len - 1.0f));
        const float w = x + g * v;
        d.write(w);
        return v - g * w;
    }
    [[nodiscard]] float tap(float k) const noexcept { return d.read(k); }
};
// damped feedback comb (the chamber's late field)
struct Comb {
    hc::fx::DelayLine d;
    float lp = 0;
    void prepare(std::uint32_t maxLen) { d.prepare(maxLen + 4); }
    void clear() noexcept { d.clear(); lp = 0; }
    float tick(float x, float len, float g, float damp) noexcept {
        const float y = d.read(std::max(0.0f, len - 1.0f));
        lp = y + damp * (lp - y);
        d.write(x + g * lp);
        return y;
    }
};

class ChamberPlate final : public hc::fx::Effect {
public:
    ChamberPlate() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == kTapeR) return p(kTapeLink) < 0.5f;
        if (i == kPInR) return p(kPInLink) < 0.5f;
        if (i == kPOutR) return p(kPOutLink) < 0.5f;
        if (i >= kTapeL && i <= kFbMid) return p(kTapeOn) > 0.5f;
        if (i >= kRoom && i <= kTimeMult) return p(kChamberOn) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const double s44 = fs / 44100.0, sD = fs / 29761.0;
        for (auto& t : tape_) t.prepare(static_cast<std::uint32_t>(fs * 2.1));
        erLine_[0].prepare(static_cast<std::uint32_t>(fs * 0.25)); erLine_[1].prepare(static_cast<std::uint32_t>(fs * 0.25));
        lateDly_[0].prepare(static_cast<std::uint32_t>(fs * 0.12)); lateDly_[1].prepare(static_cast<std::uint32_t>(fs * 0.12));
        for (auto& ch : comb_) for (auto& c : ch) c.prepare(static_cast<std::uint32_t>(1700.0 * 1.6 * s44) + 40);
        for (auto& ch : cAp_) for (auto& a : ch) a.prepare(static_cast<std::uint32_t>(600.0 * s44) + 40);
        // plate
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(fs * 0.26));
        for (auto& ch : inAp_) for (auto& a : ch) a.prepare(static_cast<std::uint32_t>(400.0 * sD) + 8);
        modAp_[0].prepare(static_cast<std::uint32_t>(700.0 * sD) + 64); modAp_[1].prepare(static_cast<std::uint32_t>(940.0 * sD) + 64);
        tAp_[0].prepare(static_cast<std::uint32_t>(1800.0 * sD) + 8); tAp_[1].prepare(static_cast<std::uint32_t>(2656.0 * sD) + 8);
        tD1_[0].prepare(static_cast<std::uint32_t>(4453.0 * sD) + 8); tD1_[1].prepare(static_cast<std::uint32_t>(4217.0 * sD) + 8);
        tD2_[0].prepare(static_cast<std::uint32_t>(3720.0 * sD) + 8); tD2_[1].prepare(static_cast<std::uint32_t>(3163.0 * sD) + 8);
        for (auto* s : {&cInL_, &cInR_, &cOut_, &pInL_, &pInR_, &pOutL_, &pOutR_}) s->prepare(fs, 20.0, 1.0f);
        cMix_.prepare(fs, 20.0, 0.0f); pMix_.prepare(fs, 20.0, 0.0f);
        tl_.prepare(fs, 80.0, tapeSamp(p(kTapeL))); tr_.prepare(fs, 80.0, tapeSamp(p(kTapeR)));
        preS_.prepare(fs, 80.0, static_cast<float>(p(kPredelay) * 0.001 * fs));
        cM_.prepare(fs); pM_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& t : tape_) t.clear();
        for (auto& d : erLine_) d.clear();
        for (auto& d : lateDly_) d.clear();
        for (auto& ch : comb_) for (auto& c : ch) c.clear();
        for (auto& ch : cAp_) for (auto& a : ch) a.clear();
        for (auto& d : pre_) d.clear();
        for (auto& ch : inAp_) for (auto& a : ch) a.clear();
        for (auto& a : modAp_) a.clear();
        for (auto& a : tAp_) a.clear();
        for (auto& d : tD1_) d.clear();
        for (auto& d : tD2_) d.clear();
        for (auto* f : {&inTop_, &inBass_, &inBell_, &fbTop_, &fbBass_, &fbMid_, &spk1_, &spk2_, &mic1_, &mic2_, &pBass_, &pTreb_}) f->clear();
        bw_.fill(0); damp_.fill(0);
        wow_ = flut_ = 0; plMod_ = 0; rng_ = 4242u; cPrimed_ = pPrimed_ = inLpOn_ = fbLpOn_ = loaded_ = false; clipC_ = clipP_ = 0; key1_ = key2_ = -1;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) return;
        if (!loaded_) {   // a loaded session starts where it was saved (no 20 ms fade-in)
            cInL_.cur = gainOf(p(kCInL)); cInR_.cur = gainOf(p(kCInR)); cOut_.cur = gainOf(p(kCOut)); cMix_.cur = p(kCMix) * 0.01f;
            const bool il = p(kPInLink) > 0.5f, ol = p(kPOutLink) > 0.5f;
            pInL_.cur = gainOf(p(kPInL)); pInR_.cur = gainOf(il ? p(kPInL) : p(kPInR));
            pOutL_.cur = gainOf(p(kPOutL)); pOutR_.cur = gainOf(ol ? p(kPOutL) : p(kPOutR)); pMix_.cur = p(kPMix) * 0.01f;
            loaded_ = true;
        }
        chamber(L, R, n);
        plate(L, R, n);
    }

private:
    [[nodiscard]] float tapeSamp(float ms) const noexcept {
        if (p(kTapeSync) > 0.5f) {   // SYNC: snap to the nearest 1/16 of the host beat
            const float six = 15000.0f / std::max(tempo(), 20.0f);
            ms = std::max(six, std::round(ms / six) * six);
        }
        return std::clamp(static_cast<float>(fs_) * ms * 0.001f, 1.0f, static_cast<float>(fs_ * 2.0));
    }
    static float gainOf(float db) noexcept { return kit::dbToGain(db); }

    void setChamberFilters() noexcept {
        const float k1 = p(kTopCut) + p(kBassCut) * 31.0f + p(kBellFreq) * 0.37f + p(kBellGain) * 1013.0f
                       + p(kFbTop) * 0.11f + p(kFbBass) * 7.0f + p(kFbMid) * 977.0f + p(kSpeaker) * 50000.0f + p(kMicType) * 90000.0f;
        if (k1 == key1_) return;
        key1_ = k1;
        const double ny = fs_ * 0.45;
        if (p(kTopCut) < 19999.0f) inLpOn_ = true;   // 20 kHz = off; once moved, keep running (no click on return)
        if (p(kFbTop) < 19999.0f) fbLpOn_ = true;
        inTop_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(p(kTopCut), ny), 0.7071));
        inBass_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kBassCut), 0.7071));
        inBell_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, p(kBellFreq), 0.9, p(kBellGain)));
        fbTop_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min<double>(p(kFbTop), ny), 0.7071));
        fbBass_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, p(kFbBass), 0.7071));
        fbMid_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 1200.0, 0.8, p(kFbMid)));
        switch (static_cast<int>(p(kSpeaker))) {   // the speaker playing into the room
            case 0: spk1_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 160.0, 0.7)); spk2_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(8000.0, ny), 0.7)); break;
            case 1: spk1_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 60.0, 0.7)); spk2_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(12000.0, ny), 0.7)); break;
            default: spk1_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 320.0, 0.7)); spk2_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 2500.0, 1.0, 4.0)); break;
        }
        switch (static_cast<int>(p(kMicType))) {   // the mics picking it up
            case 0: mic1_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(9000.0, ny), 0.6)); mic2_.set(hc::fx::BiquadCoefs::make(BT::LowShelf, fs_, 200.0, 0.7, 2.0)); break;
            case 1: mic1_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, 8000.0, 0.7, 2.0)); mic2_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, 30.0, 0.7)); break;
            default: mic1_.set(hc::fx::BiquadCoefs::make(BT::Bell, fs_, 4000.0, 1.0, 3.0)); mic2_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs_, std::min(12000.0, ny), 0.7)); break;
        }
    }

    void chamber(float* L, float* R, std::uint32_t n) noexcept {
        const float inLT = gainOf(p(kCInL)), inRT = gainOf(p(kCInR)), outT = gainOf(p(kCOut)), mixT = p(kCMix) * 0.01f;
        const bool wet = mixT > 0.0f || cMix_.cur > 1e-6f;
        if (!wet) {
            const bool flat = kit::isFlat(inLT, 1.0f) && kit::isFlat(inRT, 1.0f) && kit::isFlat(outT, 1.0f) && kit::isFlat(cInL_.cur, 1.0f) && kit::isFlat(cInR_.cur, 1.0f) && kit::isFlat(cOut_.cur, 1.0f);
            if (!flat) for (std::uint32_t i = 0; i < n; ++i) { const float o = cOut_.next(outT); L[i] *= cInL_.next(inLT) * o; R[i] *= cInR_.next(inRT) * o; }
            meterC(L, R, n); return;
        }
        setChamberFilters();
        const float fs = static_cast<float>(fs_), s44 = fs / 44100.0f;
        const bool tapeOn = p(kTapeOn) > 0.5f, chOn = p(kChamberOn) > 0.5f;
        const float tLT = tapeSamp(p(kTapeL)), tRT = tapeSamp(p(kTapeLink) > 0.5f ? p(kTapeL) : p(kTapeR));
        const float fb = p(kFeedback) * 0.01f, drv = 1.0f + p(kDrive) * 0.07f, modA = p(kMod) * 0.01f;
        const int room = static_cast<int>(p(kRoom));
        static constexpr std::array<float, 3> kScale{0.6f, 1.0f, 1.6f}, kRt{0.7f, 1.4f, 2.4f}, kDamp{0.22f, 0.3f, 0.38f};
        const float scale = kScale[static_cast<std::size_t>(room)];
        const float rt = kRt[static_cast<std::size_t>(room)] * p(kTimeMult);
        const float damp = kDamp[static_cast<std::size_t>(room)];
        const float pos = p(kMicPos) * 0.01f;
        const bool toWall = p(kFacing) > 0.5f;
        const float erG = (toWall ? 0.35f : 1.0f) * (1.0f - 0.5f * pos), lateG = (toWall ? 1.0f : 0.8f) * (0.7f + 0.3f * pos);
        const float micPre = pos * 0.030f * fs;   // a far mic hears everything later
        static constexpr std::array<float, 8> kErL{7.1f, 11.3f, 15.9f, 19.7f, 23.3f, 29.1f, 33.7f, 41.3f}, kErR{8.3f, 12.7f, 17.1f, 21.1f, 26.9f, 31.3f, 37.9f, 44.7f};
        static constexpr std::array<float, 8> kComb{1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116};
        std::array<std::array<float, 8>, 2> len{}, g{};
        for (std::size_t c = 0; c < 8; ++c)
            for (std::size_t s = 0; s < 2; ++s) {
                len[s][c] = (kComb[c] + (s ? 23.0f : 0.0f)) * scale * s44;
                g[s][c] = std::min(0.985f, std::pow(10.0f, -3.0f * len[s][c] / (fs * rt)));
            }
        const float ap1 = 556.0f * s44, ap2 = 441.0f * s44;
        const float lateDelay = std::min(0.11f * fs, 0.012f * scale * fs + micPre);
        const float bal = p(kBalance) * 0.01f;
        const float wowRate = 0.7f / fs, flRate = 7.0f / fs;
        if (!cPrimed_) { tl_.cur = tLT; tr_.cur = tRT; cPrimed_ = true; }   // start at the set times
        const bool inLp = inLpOn_, fbLp = fbLpOn_;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float o = cOut_.next(outT);
            const float xl = L[i] * cInL_.next(inLT), xr = R[i] * cInR_.next(inRT);
            float sl = xl, sr = xr;
            if (inLp) { sl = inTop_.l.tick(sl); sr = inTop_.r.tick(sr); }
            sl = inBell_.l.tick(inBass_.l.tick(sl));
            sr = inBell_.r.tick(inBass_.r.tick(sr));
            // ---- tape delay ----
            float tpl = 0, tpr = 0;
            if (tapeOn) {
                wow_ += wowRate; wow_ -= std::floor(wow_); flut_ += flRate; flut_ -= std::floor(flut_);
                const float m = modA * fs * (0.002f * std::sin(kit::kTau * wow_) + 0.0003f * std::sin(kit::kTau * flut_));
                const float dl = tl_.next(tLT), dr = tr_.next(tRT);
                float yl = tape_[0].read(std::max(0.0f, dl - 1.0f + m)), yr = tape_[1].read(std::max(0.0f, dr - 1.0f - m));
                if (fbLp) { yl = fbTop_.l.tick(yl); yr = fbTop_.r.tick(yr); }
                yl = fbMid_.l.tick(fbBass_.l.tick(yl));
                yr = fbMid_.r.tick(fbBass_.r.tick(yr));
                auto sat = [drv](float v) { return std::tanh(v * drv) / drv; };
                tape_[0].write(sat(sl + fb * yl)); tape_[1].write(sat(sr + fb * yr));
                tpl = yl; tpr = yr;
            }
            // ---- chamber ----
            float chl = 0, chr = 0;
            if (chOn) {
                const float spkIn = 0.5f * (sl + sr);   // one speaker plays the mono send into the room
                const float sp = static_cast<float>(spk2_.l.tick(static_cast<float>(spk1_.l.tick(spkIn))));
                const float sd = sl - sr;   // a little of the side signal keeps the two mics different
                erLine_[0].write(sp + 0.15f * sd); erLine_[1].write(sp - 0.15f * sd);
                float el = 0, er = 0, sign = 1.0f, gk = 1.0f;
                for (std::size_t k = 0; k < 8; ++k) {
                    el += sign * gk * erLine_[0].read(kErL[k] * 0.001f * fs * scale + micPre);
                    er += sign * gk * erLine_[1].read(kErR[k] * 0.001f * fs * scale + micPre);
                    sign = -sign; gk *= 0.85f;
                }
                lateDly_[0].write(sp + 0.15f * sd); lateDly_[1].write(sp - 0.15f * sd);
                const float li = lateDly_[0].read(lateDelay), ri = lateDly_[1].read(lateDelay);
                float cl = 0, cr = 0;
                for (std::size_t c = 0; c < 8; ++c) {
                    cl += comb_[0][c].tick(li, len[0][c], g[0][c], damp);
                    cr += comb_[1][c].tick(ri, len[1][c], g[1][c], damp);
                }
                cl = cAp_[0][1].tick(cAp_[0][0].tick(cl * 0.12f, 0.5f, ap1), 0.5f, ap2);
                cr = cAp_[1][1].tick(cAp_[1][0].tick(cr * 0.12f, 0.5f, ap1 + 23.0f * s44), 0.5f, ap2 + 23.0f * s44);
                chl = erG * 0.25f * el + lateG * cl;
                chr = erG * 0.25f * er + lateG * cr;
                chl = static_cast<float>(mic2_.l.tick(static_cast<float>(mic1_.l.tick(chl))));
                chr = static_cast<float>(mic2_.r.tick(static_cast<float>(mic1_.r.tick(chr))));
            }
            float wl = 0, wr = 0;
            if (tapeOn && chOn) { wl = chl + bal * (tpl - chl); wr = chr + bal * (tpr - chr); }
            else if (tapeOn) { wl = tpl; wr = tpr; }
            else if (chOn) { wl = chl; wr = chr; }
            const float mx = cMix_.next(mixT);
            L[i] = kit::sane((xl + mx * (wl - xl)) * o);
            R[i] = kit::sane((xr + mx * (wr - xr)) * o);
        }
        meterC(L, R, n);
    }

    void plate(float* L, float* R, std::uint32_t n) noexcept {
        const bool inLink = p(kPInLink) > 0.5f, outLink = p(kPOutLink) > 0.5f;
        const float inLT = gainOf(p(kPInL)), inRT = gainOf(inLink ? p(kPInL) : p(kPInR));
        const float outLT = gainOf(p(kPOutL)), outRT = gainOf(outLink ? p(kPOutL) : p(kPOutR));
        const float mixT = p(kPMix) * 0.01f;
        const bool wet = mixT > 0.0f || pMix_.cur > 1e-6f;
        if (!wet) {
            const bool flat = kit::isFlat(inLT, 1.0f) && kit::isFlat(inRT, 1.0f) && kit::isFlat(outLT, 1.0f) && kit::isFlat(outRT, 1.0f)
                           && kit::isFlat(pInL_.cur, 1.0f) && kit::isFlat(pInR_.cur, 1.0f) && kit::isFlat(pOutL_.cur, 1.0f) && kit::isFlat(pOutR_.cur, 1.0f);
            if (!flat) for (std::uint32_t i = 0; i < n; ++i) { L[i] *= pInL_.next(inLT) * pOutL_.next(outLT); R[i] *= pInR_.next(inRT) * pOutR_.next(outRT); }
            meterP(L, R, n); return;
        }
        const float bass = p(kPBass), treb = p(kTreble);
        const float k2 = bass * 3.0f + treb * 1000.0f;
        if (k2 != key2_) {
            key2_ = k2;
            const double hz = bass > 0.0f ? 30.0 * std::pow(400.0 / 30.0, bass * 0.01) : 10.0;   // 0 = off (10 Hz is below hearing)
            pBass_.set(hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hz, 0.7071));
            pTreb_.set(hc::fx::BiquadCoefs::make(BT::HighShelf, fs_, std::min(5000.0, fs_ * 0.4), 0.7, treb));
        }
        const int chs = static_cast<int>(p(kCharacter));
        static constexpr std::array<float, 4> kRt{1.3f, 2.2f, 3.2f, 4.8f}, kBw{0.9999f, 0.9f, 0.75f, 0.6f}, kDampC{0.05f, 0.15f, 0.25f, 0.35f}, kModHz{0.9f, 0.7f, 0.5f, 0.35f};
        const auto ci = static_cast<std::size_t>(chs);
        const float rt = kRt[ci] * std::max(0.12f, 1.0f - 0.088f * p(kDamper));
        static constexpr std::array<float, 4> kSize{0.5f, 0.55f, 0.65f, 0.75f};   // tank size per character (ASSUMED)
        const float sD = static_cast<float>(fs_ / 29761.0) * kSize[ci];
        const float sIn = static_cast<float>(fs_ / 29761.0);
        const float half = (672.0f + 4453.0f + 1800.0f + 3720.0f) * sD / static_cast<float>(fs_);
        const float decay = std::min(0.985f, std::pow(10.0f, -1.5f * half / rt));   // applied twice per half loop (after the damper and on the cross-feed)
        const float bw = kBw[ci], dmp = kDampC[ci];
        const float drv = 1.0f + p(kPDrive) * 0.09f, noiseA = p(kPNoise) * 1e-5f, ct = p(kCrosstalk) * 0.005f;
        const float exc = 16.0f * sD, modInc = kModHz[ci] / static_cast<float>(fs_);
        static constexpr std::array<float, 4> kInL{142, 107, 379, 277}, kInR{151, 113, 367, 289};
        static constexpr std::array<float, 4> kInG{0.75f, 0.75f, 0.625f, 0.625f};
        const float preT = p(kPredelay) * 0.001f * static_cast<float>(fs_);
        if (!pPrimed_) { preS_.cur = preT; pPrimed_ = true; }
        for (std::uint32_t i = 0; i < n; ++i) {
            const float xl = L[i] * pInL_.next(inLT), xr = R[i] * pInR_.next(inRT);
            float sl = xl + ct * (xr - xl), sr = xr + ct * (xl - xr);
            if (drv > 1.0f) { sl = std::tanh(sl * drv) / drv; sr = std::tanh(sr * drv) / drv; }
            sl = static_cast<float>(pBass_.l.tick(sl)); sr = static_cast<float>(pBass_.r.tick(sr));
            const float pd = preS_.next(preT);
            pre_[0].write(sl); pre_[1].write(sr);
            float al = pre_[0].read(pd), ar = pre_[1].read(pd);
            bw_[0] = bw * al + (1.0f - bw) * bw_[0]; bw_[1] = bw * ar + (1.0f - bw) * bw_[1];
            al = bw_[0]; ar = bw_[1];
            for (std::size_t k = 0; k < 4; ++k) { al = inAp_[0][k].tick(al, kInG[k], kInL[k] * sIn); ar = inAp_[1][k].tick(ar, kInG[k], kInR[k] * sIn); }
            // the tank: two branches, each feeding the other
            plMod_ += modInc; plMod_ -= std::floor(plMod_);
            const float mo = exc * std::sin(kit::kTau * plMod_);
            const float endL = tD2_[0].read(3720.0f * sD - 1.0f), endR = tD2_[1].read(3163.0f * sD - 1.0f);
            float a = modAp_[0].tick(al + decay * endR, -0.7f, 672.0f * sD + mo);
            float b = tD1_[0].read(4453.0f * sD - 1.0f); tD1_[0].write(a);
            damp_[0] = b + dmp * (damp_[0] - b);
            float c = tAp_[0].tick(damp_[0] * decay, 0.5f, 1800.0f * sD);
            tD2_[0].write(c);
            a = modAp_[1].tick(ar + decay * endL, -0.7f, 908.0f * sD - mo);
            b = tD1_[1].read(4217.0f * sD - 1.0f); tD1_[1].write(a);
            damp_[1] = b + dmp * (damp_[1] - b);
            c = tAp_[1].tick(damp_[1] * decay, 0.5f, 2656.0f * sD);
            tD2_[1].write(c);
            // output taps spread across both branches
            float wl = tD1_[1].read(266.0f * sD) + tD1_[1].read(2974.0f * sD) - tAp_[1].tap(1913.0f * sD) + tD2_[1].read(1996.0f * sD)
                     - tD1_[0].read(1990.0f * sD) - tAp_[0].tap(187.0f * sD) - tD2_[0].read(1066.0f * sD);
            float wr = tD1_[0].read(353.0f * sD) + tD1_[0].read(3627.0f * sD) - tAp_[0].tap(1228.0f * sD) + tD2_[0].read(2673.0f * sD)
                     - tD1_[1].read(2111.0f * sD) - tAp_[1].tap(335.0f * sD) - tD2_[1].read(121.0f * sD);
            wl *= 0.6f; wr *= 0.6f;
            wl = static_cast<float>(pTreb_.l.tick(wl)); wr = static_cast<float>(pTreb_.r.tick(wr));
            if (noiseA > 0.0f) {
                rng_ = rng_ * 1664525u + 1013904223u; wl += (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f) * noiseA;
                rng_ = rng_ * 1664525u + 1013904223u; wr += (static_cast<float>(rng_ >> 8) / 8388608.0f - 1.0f) * noiseA;
            }
            const float mx = pMix_.next(mixT);
            L[i] = kit::sane((xl + mx * (wl - xl)) * pOutL_.next(outLT));
            R[i] = kit::sane((xr + mx * (wr - xr)) * pOutR_.next(outRT));
        }
        meterP(L, R, n);
    }

    void meterC(const float* L, const float* R, std::uint32_t n) noexcept {
        cM_.feed(L, n); cM_.feed(R, n);
        clipC_ = std::max(0.0f, clipC_ - static_cast<float>(n));
        for (std::uint32_t i = 0; i < n; ++i) if (std::fabs(L[i]) >= 1.0f || std::fabs(R[i]) >= 1.0f) clipC_ = static_cast<float>(fs_);
        publishMeter(0, cM_.db()); publishMeter(1, clipC_ > 0 ? 1.0f : 0.0f);
    }
    void meterP(const float* L, const float* R, std::uint32_t n) noexcept {
        pM_.feed(L, n); pM_.feed(R, n);
        clipP_ = std::max(0.0f, clipP_ - static_cast<float>(n));
        for (std::uint32_t i = 0; i < n; ++i) if (std::fabs(L[i]) >= 1.0f || std::fabs(R[i]) >= 1.0f) clipP_ = static_cast<float>(fs_);
        publishMeter(2, pM_.db()); publishMeter(3, clipP_ > 0 ? 1.0f : 0.0f);
    }

    double fs_ = 48000;
    // chamber
    std::array<hc::fx::DelayLine, 2> tape_{}, erLine_{}, lateDly_{};
    std::array<std::array<Comb, 8>, 2> comb_{};
    std::array<std::array<Ap, 2>, 2> cAp_{};
    hc::fx::Biquad2 inTop_, inBass_, inBell_, fbTop_, fbBass_, fbMid_, spk1_, spk2_, mic1_, mic2_;
    // plate
    std::array<hc::fx::DelayLine, 2> pre_{}, tD1_{}, tD2_{};
    std::array<std::array<Ap, 4>, 2> inAp_{};
    std::array<Ap, 2> modAp_{}, tAp_{};
    hc::fx::Biquad2 pBass_, pTreb_;
    std::array<float, 2> bw_{}, damp_{};
    hc::fx::Smoothed cInL_, cInR_, cOut_, cMix_, pInL_, pInR_, pOutL_, pOutR_, pMix_, tl_, tr_, preS_;
    float wow_ = 0, flut_ = 0, plMod_ = 0, clipC_ = 0, clipP_ = 0, key1_ = -1, key2_ = -1;
    std::uint32_t rng_ = 4242u;
    bool cPrimed_ = false, pPrimed_ = false, inLpOn_ = false, fbLpOn_ = false, loaded_ = false;
    kit::PeakMeter cM_, pM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeChamberPlate() { return std::make_unique<ChamberPlate>(); }

} // namespace hem::staging
