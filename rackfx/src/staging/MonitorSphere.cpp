// =============================================================================
//  42 MONITORSPHERE — hear how a mix will translate, on headphones or one pair of monitors.
//  MODE: BYPASS, NEARFIELD (small studio monitors), CLUB (big subs, a hard top), CAR (cabin boom and a
//  dip in the low mids), PHONE (a tiny mono speaker), MONO (the mix folded to one channel).
//  CROSSFEED lets each ear hear a little of the other side, late and darker, the way speakers reach both
//  ears (headphone fatigue goes down, hard-panned parts sit more naturally). ROOM_AMT adds the first
//  wall reflections of a small control room. DIM drops the level 20 dB. OUTPUT.
//  MONITORING ONLY: switch it off (or take it out) before you bounce or export.
//  The profiles are original HEM curves, not measurements of any product, car or room.
//  Different from S.I.P.A. (10: stereo imaging for the mix itself).
//  Dead flat on load: MODE BYPASS, CROSSFEED 0, ROOM_AMT 0, DIM OFF, OUTPUT 0 dB.
//  ASSUMED: crossfeed = the other side low-passed at 700 Hz, 0.3 ms later, up to -6 dB (each side then
//  scaled so a centred sound keeps its level); room = six reflections a side, 3..23 ms, low-passed at
//  6 kHz, up to -10 dB; the profile curves listed in setMode().
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

enum P { kMode, kCross, kRoom, kDim, kOut, kCount };
const char* const kModeN[] = {"BYPASS", "NEARFIELD", "CLUB", "CAR", "PHONE", "MONO"};
const char* const kDimN[] = {"OFF", "-20 dB"};
const ParamInfo kParams[kCount] = {
    {"mode", "MODE", "", 0.0f, 5.0f, 0.0f, Taper::Choice, kModeN},
    {"crossfeed", "CROSSFEED", "%", 0.0f, 100.0f, 0.0f},
    {"room_amt", "ROOM AMOUNT", "%", 0.0f, 100.0f, 0.0f},
    {"dim", "DIM", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kDimN},
    {"output", "OUTPUT", "dB", -12.0f, 12.0f, 0.0f},
};
const hc::fx::MeterInfo kMeters[] = {{"OUT L", "dB", -60, 6}, {"OUT R", "dB", -60, 6}};
constexpr int kStages = 4;

class MonitorSphere final : public hc::fx::Effect {
public:
    MonitorSphere() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : xd_) d.prepare(static_cast<std::uint32_t>(fs * 0.002) + 4);
        for (auto& d : room_) d.prepare(static_cast<std::uint32_t>(fs * 0.03) + 4);
        xlp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs, 700.0, 0.7071));
        rlp_.set(hc::fx::BiquadCoefs::make(BT::LowPass, fs, std::min(6000.0, fs * 0.45), 0.7071));
        cross_.prepare(fs, 20.0, 0.0f); roomG_.prepare(fs, 20.0, 0.0f); out_.prepare(fs, 20.0, 1.0f); prof_.prepare(fs, 20.0, 0.0f);
        modeKey_ = -1;
        reset();
    }
    void reset() noexcept override {
        for (auto& d : xd_) d.clear();
        for (auto& d : room_) d.clear();
        xlp_.clear(); rlp_.clear();
        for (auto& f : eq_) f.clear();
        loaded_ = false; pk_.fill(0);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int mode = static_cast<int>(p(kMode));
        setMode(mode);
        const float cT = p(kCross) * 0.005f, rT = p(kRoom) * 0.0032f, oT = kit::dbToGain(p(kOut) - (p(kDim) > 0.5f ? 20.0f : 0.0f));
        const float pT = mode == 0 ? 0.0f : 1.0f;
        if (!loaded_) { cross_.cur = cT; roomG_.cur = rT; out_.cur = oT; prof_.cur = pT; loaded_ = true; }
        const float xd = static_cast<float>(0.0003 * fs_);
        static constexpr std::array<float, 6> kRefMs{3.1f, 5.3f, 8.9f, 12.7f, 17.3f, 23.1f};
        static constexpr std::array<float, 6> kRefG{0.9f, -0.75f, 0.6f, -0.5f, 0.4f, -0.3f};
        pk_.fill(0);
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            // ---- translation profile (crossfaded in / out over 20 ms) ----
            const float pw = prof_.next(pT);
            if (pw > 1e-6f) {
                float pl = l, pr = r;
                if (mode == 4 || mode == 5) { const float m = 0.5f * (pl + pr); pl = pr = m; }   // PHONE and MONO are one speaker
                for (int s = 0; s < stages_; ++s) { pl = eq_[static_cast<std::size_t>(s)].l.tick(pl); pr = eq_[static_cast<std::size_t>(s)].r.tick(pr); }
                l += pw * (pl - l); r += pw * (pr - r);
            }
            // ---- crossfeed ----
            const float cf = cross_.next(cT);
            xd_[0].write(l); xd_[1].write(r);
            if (cf > 1e-6f) {
                const float fromR = xlp_.l.tick(xd_[1].read(xd)), fromL = xlp_.r.tick(xd_[0].read(xd));
                const float norm = 1.0f / (1.0f + cf);   // a centred sound keeps its level
                l = (l + cf * fromR) * norm; r = (r + cf * fromL) * norm;
            }
            // ---- room reflections ----
            const float rg = roomG_.next(rT);
            room_[0].write(l); room_[1].write(r);
            if (rg > 1e-6f) {
                float el = 0, er = 0;
                for (std::size_t k = 0; k < 6; ++k) {
                    const float t = kRefMs[k] * 0.001f * static_cast<float>(fs_);
                    el += kRefG[k] * room_[k % 2].read(t); er += kRefG[k] * room_[(k + 1) % 2].read(t * 1.07f);
                }
                l += rg * rlp_.l.tick(el); r += rg * rlp_.r.tick(er);
            }
            const float o = out_.next(oT);
            l *= o; r *= o;
            pk_[0] = std::max(pk_[0], std::fabs(l)); pk_[1] = std::max(pk_[1], std::fabs(r));
            L[i] = kit::sane(l); R[i] = kit::sane(r);
        }
        publishMeter(0, kit::gainToDb(pk_[0])); publishMeter(1, kit::gainToDb(pk_[1]));
    }

private:
    void setMode(int mode) noexcept {
        if (static_cast<float>(mode) == modeKey_ || mode == 0) { if (mode == 0) modeKey_ = 0; return; }
        modeKey_ = static_cast<float>(mode);
        for (auto& f : eq_) f.clear();
        const double ny = fs_ * 0.45;
        auto mk = [&](BT t, double hz, double q, double g = 0.0) { return hc::fx::BiquadCoefs::make(t, fs_, std::min(hz, ny), q, g); };
        std::array<hc::fx::BiquadCoefs, kStages> c{};
        switch (mode) {
            case 1: c = {mk(BT::HighPass, 45.0, 0.7071), mk(BT::Bell, 2000.0, 1.0, 1.0), mk(BT::HighShelf, 12000.0, 0.7071, -1.0), mk(BT::Bell, 120.0, 1.0, 1.0)}; stages_ = 4; break;   // NEARFIELD
            case 2: c = {mk(BT::HighPass, 30.0, 0.7071), mk(BT::LowShelf, 60.0, 0.7071, 6.0), mk(BT::Bell, 3000.0, 1.0, -2.0), mk(BT::LowPass, 14000.0, 0.7071)}; stages_ = 4; break;   // CLUB
            case 3: c = {mk(BT::LowShelf, 80.0, 0.7071, 5.0), mk(BT::Bell, 400.0, 1.2, -3.0), mk(BT::Bell, 4000.0, 1.0, 2.0), mk(BT::LowPass, 12000.0, 0.7071)}; stages_ = 4; break;   // CAR
            case 4: c = {mk(BT::HighPass, 400.0, 0.5412), mk(BT::HighPass, 400.0, 1.3066), mk(BT::Bell, 2000.0, 1.0, 6.0), mk(BT::LowPass, 6000.0, 0.7071)}; stages_ = 4; break;   // PHONE
            default: stages_ = 0; break;   // MONO: the fold only
        }
        for (int s = 0; s < kStages; ++s) eq_[static_cast<std::size_t>(s)].set(c[static_cast<std::size_t>(s)]);
    }
    double fs_ = 48000;
    std::array<hc::fx::DelayLine, 2> xd_{}, room_{};
    hc::fx::Biquad2 xlp_, rlp_;
    std::array<hc::fx::Biquad2, kStages> eq_{};
    hc::fx::Smoothed cross_, roomG_, out_, prof_;
    std::array<float, 2> pk_{};
    float modeKey_ = -1;
    int stages_ = 0;
    bool loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeMonitorSphere() { return std::make_unique<MonitorSphere>(); }

} // namespace hem::staging
