// KINGPIN — drum channel: one set of controls whose behaviour follows TYPE (see ToneShop.hpp).
//
// In order: the gate (KICK only), the compressor (SENS sets how hard it is hit, COMPRESS how much it
// squeezes; speed from TYPE; half of what it takes at full scale is made up), BASS and TREBLE shelves
// (eqkit::IirBands, eased), the FX ambience mixed in (a small comb-and-all-pass room per TYPE), OUTPUT,
// then the hiss of ANALOG. The VU meters read the input and the output.
#include "hem/rack/ToneShop.hpp"

#include "EqKit.hpp"
#include "ToneCommon.hpp"
#include "../arsenal/Vintage.hpp"

#include <vector>

namespace hem::rack {
namespace kingpin {
namespace {
using namespace arsenal;
using eqkit::Shape;

const char* const kTypeN[] = {"KICK", "SNARE", "HATS", "TOMS", "OVERHEAD", "ROOM"};
const char* const kMeterIoN[] = {"IN", "OUT"};
const ParamInfo kParams[kCount] = {
    {"type", "Type", "", 0, kDrums - 1, 0, Taper::Choice, kTypeN},
    {"sens", "Sens", "", -50.0f, 50.0f, 0.0f},
    {"compress", "Compress", "%", 0.0f, 100.0f, 0.0f},
    {"gate", "Gate", "", -50.0f, 50.0f, -50.0f},
    {"fx", "FX", "%", 0.0f, 100.0f, 0.0f},
    {"bass", "Bass", "", 0.0f, 5.0f, 0.0f},
    {"treble", "Treble", "", 0.0f, 5.0f, 0.0f},
    {"output", "Output", "", -50.0f, 50.0f, 0.0f},
    {"meter", "Meter", "", 0, 1, 1, Taper::Choice, kMeterIoN},
    {"analog", "Analog", "", 0, 1, 1, Taper::Toggle, kOnOff},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "VU", -20.0f, 3.0f}, {"OUTPUT", "VU", -20.0f, 3.0f}};
constexpr float kAttackMs[kDrums] = {10, 4, 1, 8, 15, 25};
constexpr float kReleaseMs[kDrums] = {90, 120, 60, 160, 220, 300};
constexpr float kMaxRatio[kDrums] = {8, 8, 6, 8, 5, 20};
// the FX rooms: comb lengths (ms), feedback, the tone of the send (low-pass Hz)
struct Room {
    float combMs[4];
    float feedback, toneHz;
};
constexpr Room kRooms[3] = {{{11.3f, 13.7f, 15.1f, 17.9f}, 0.62f, 260.0f},    // KICK: a short low boom
                            {{29.7f, 37.1f, 41.1f, 43.7f}, 0.80f, 9000.0f},   // SNARE: a bright plate
                            {{35.3f, 41.9f, 47.3f, 53.9f}, 0.72f, 4500.0f}};  // TOMS: a room
constexpr float kAllpassMs[2] = {5.0f, 1.7f};

inline bool hasFx(int t) noexcept { return t == Kick || t == Snare || t == Toms; }
inline int roomOf(int t) noexcept { return t == Kick ? 0 : t == Snare ? 1 : 2; }

// one channel of the ambience: four combs in parallel, two all-passes after
class Ambience {
public:
    void prepare(double fs, float spread) {
        fs_ = fs;
        spread_ = spread;
        const auto n = static_cast<std::size_t>(fs * 0.06) + 8;
        for (auto& c : comb_) c.assign(n, 0.0f);
        for (int a = 0; a < 2; ++a) ap_[a].assign(static_cast<std::size_t>(fs * kAllpassMs[a] * 0.001 * spread) + 2, 0.0f);
        clear();
    }
    void clear() noexcept {
        for (auto& c : comb_) std::fill(c.begin(), c.end(), 0.0f);
        for (auto& a : ap_) std::fill(a.begin(), a.end(), 0.0f);
        for (int k = 0; k < 4; ++k) { pos_[k] = 0; damp_[k] = 0.0f; }
        apPos_[0] = apPos_[1] = 0;
        tone_ = 0.0f;
    }
    void set(const Room& r) noexcept {
        for (int k = 0; k < 4; ++k)
            len_[k] = std::clamp(static_cast<std::size_t>(fs_ * r.combMs[k] * 0.001 * spread_), std::size_t{8}, comb_[k].size() - 1);
        fb_ = r.feedback;
        toneK_ = static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * r.toneHz / fs_));
    }
    float tick(float x) noexcept {
        tone_ += (x - tone_) * toneK_;
        float y = 0.0f;
        for (int k = 0; k < 4; ++k) {
            float& cell = comb_[k][pos_[k]];
            const float out = cell;
            damp_[k] += (out - damp_[k]) * 0.4f;   // the walls soak up some top
            cell = tone_ + fb_ * damp_[k];
            if (++pos_[k] >= len_[k]) pos_[k] = 0;
            y += out;
        }
        y *= 0.25f;
        for (int a = 0; a < 2; ++a) {
            float& cell = ap_[a][apPos_[a]];
            const float v = cell;
            const float w = y + 0.5f * v;
            cell = w;
            y = v - 0.5f * w;
            if (++apPos_[a] >= ap_[a].size()) apPos_[a] = 0;
        }
        return y;
    }

private:
    double fs_ = 48000.0;
    float spread_ = 1.0f, fb_ = 0.0f, toneK_ = 0.0f, tone_ = 0.0f, damp_[4]{};
    std::vector<float> comb_[4], ap_[2];
    std::size_t len_[4]{8, 8, 8, 8}, pos_[4]{}, apPos_[2]{};
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        const int t = type();
        if (i == Gate) return t == Kick;
        if (i == Fx) return hasFx(t);
        if (i == Bass) return t != Overhead;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        amb_[0].prepare(fs, 1.0f);
        amb_[1].prepare(fs, 1.013f);
        eq_[0].prepare(fs);
        eq_[1].prepare(fs);
        hum_.prepare(fs);
        vuIn_.prepare(fs);
        vuOut_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        amb_[0].clear();
        amb_[1].clear();
        load();
        eq_[0].clear();
        eq_[1].clear();
        gate_.clear();
        env_ = gr_ = 0.0f;
        hum_.clear();
        vuIn_.clear();
        vuOut_.clear();
        fx_.prepare(fs_, 30, fxAmount());
        out_.prepare(fs_, 30, outGain());
        room_ = -1;
        publishReduction(0.0f);
        publishMeter(0, -20.0f);
        publishMeter(1, -20.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int t = type();
        load();
        if (hasFx(t) && roomOf(t) != room_) {
            room_ = roomOf(t);
            amb_[0].set(kRooms[room_]);
            amb_[1].set(kRooms[room_]);
        }
        const float thr = -18.0f - 0.4f * p(Sens);
        const float c = p(Compress) * 0.01f, ratio = 1.0f + (kMaxRatio[t] - 1.0f) * c;
        const float makeup = std::min(0.5f * -downDb(0.0f, thr, ratio, 6.0f), 18.0f);
        const float att = coefMs(fs_, kAttackMs[t]), rel = coefMs(fs_, kReleaseMs[t]);
        const float envAtt = coefMs(fs_, 0.5), envRel = coefMs(fs_, 30.0);
        const bool gateOn = t == Kick && p(Gate) > -49.9f;
        const float gateDb = -60.0f + 50.0f * (p(Gate) + 50.0f) / 100.0f;
        const auto hold = static_cast<std::uint32_t>(fs_ * 0.02);
        const float gEnv = coefMs(fs_, 20.0), gAtt = coefMs(fs_, 0.5), gRel = coefMs(fs_, 60.0);
        const float wFx = fxAmount(), wOut = outGain(), mk = hc::fx::dbToGain(makeup);
        const int analog = p(Analog) >= 0.5f ? 3 : 0;
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            vuIn_.tick(l, r);
            const float x = std::max(std::fabs(l), std::fabs(r));
            const float gg = gate_.tick(x, gateOn, gateDb, -40.0f, hold, gEnv, gAtt, gRel);
            env_ = x + (x > env_ ? envAtt : envRel) * (env_ - x);
            const float target = c > 0.0f ? downDb(dbOf(env_), thr, ratio, 6.0f) : 0.0f;
            gr_ = follow(gr_, target, att, rel);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gg + gr_) * (c > 0.0f ? mk : 1.0f);
            l *= g;
            r *= g;
            eq_[0].step();
            eq_[1].step();
            l = eq_[0].run(0, l);
            r = eq_[1].run(0, r);
            const float f = fx_.next(wFx);
            if (f > 1e-5f) {
                const float send = 0.5f * (l + r);
                l += f * amb_[0].tick(send);
                r += f * amb_[1].tick(send);
            }
            const float o = out_.next(wOut);
            l *= o;
            r *= o;
            hum_.tick(analog, 1.0f, l, r);
            L[i] = l;
            R[i] = r;
            vuOut_.tick(l, r);
        }
        publishReduction(most);
        publishMeter(0, vuIn_.vu());
        publishMeter(1, vuOut_.vu());
    }

private:
    int type() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Type))), 0, kDrums - 1); }
    float fxAmount() const noexcept { return hasFx(type()) ? p(Fx) * 0.01f : 0.0f; }
    float outGain() const noexcept { return hc::fx::dbToGain(0.2f * p(Output)); }
    void load() noexcept {
        const int t = type();
        eqkit::BandSet b[2];
        if (t != Overhead && p(Bass) > 0.0f) b[0] = {Shape::LowShelf, kBassHz[t], 1.5f * p(Bass), 0.7071f};
        if (p(Treble) > 0.0f) b[1] = {Shape::HighShelf, kTrebleHz[t], 1.5f * p(Treble), 0.7071f};
        for (auto& e : eq_) { e.set(0, b[0]); e.set(1, b[1]); }
    }

    double fs_ = 48000.0;
    Ambience amb_[2];
    eqkit::IirBands<2> eq_[2];
    tone::GateStage gate_;
    float env_ = 0.0f, gr_ = 0.0f;
    int room_ = -1;
    Hum hum_;
    Vu vuIn_, vuOut_;
    hc::fx::Smoothed fx_, out_;
};

} // namespace
} // namespace kingpin

std::unique_ptr<hc::fx::Effect> makeKingpin() { return std::make_unique<kingpin::Impl>(); }

} // namespace hem::rack
