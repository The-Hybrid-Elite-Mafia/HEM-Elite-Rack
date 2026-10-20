// BIG HOUSE — reverb with twelve kinds of space (see Hideout.hpp).
//
// Input -> PRE-DELAY -> early reflections; the tail is fed the input and (DIFFUSION) the early reflections, decays per
// band (TIME x the damping ratios), goes through its EQ, and for the shaped kinds through an envelope that restarts at
// every new hit (BACKWARDS swells, GATED holds then shuts, DECAY under 3.5 shortens any tail).
#include "hem/rack/Hideout.hpp"

#include "RoomKit.hpp"

namespace hem::rack {
namespace bighouse {
namespace {
using namespace hideout;

const char* const kTypeN[] = {"HALL A", "HALL B", "ROOM", "CHAMBER", "SANCTUARY", "PLATE A", "PLATE B", "BACKWARDS", "GATED", "SHAPED",
                              "ECHO ROOM", "RINGING"};
const char* const kVarN[] = {"0", "1", "2", "3", "4", "5", "6"};
const char* const kPushN[] = {"A", "B"};   // any change = a push
const ParamInfo kParams[kCount] = {
    {"type", "Type", "", 0, 11, 0, Taper::Choice, kTypeN},
    {"variation", "Variation", "", 0, 6, 0, Taper::Choice, kVarN},
    {"predelay", "Pre-Delay", "ms", -160.0f, 160.0f, 0.0f},
    {"time", "Time", "s", 0.1f, 20.0f, 2.5f, Taper::Log},
    {"size", "Size", "%", 1.0f, 100.0f, 60.0f},
    {"diffusion", "Diffusion", "%", 1.0f, 100.0f, 60.0f},
    {"decay", "Decay", "", 0.04f, 3.5f, 3.5f, Taper::Log},
    {"early", "Early", "dB", -40.0f, 0.0f, -6.0f},
    {"reverb", "Reverb", "dB", -40.0f, 0.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
    {"lowdamp", "Low Damp", "Hz", 16.0f, 1600.0f, 200.0f, Taper::Log},
    {"lowratio", "Low Ratio", "x", 0.1f, 2.0f, 1.0f},
    {"highdamp", "High Damp", "Hz", 1000.0f, 21000.0f, 6000.0f, Taper::Log},
    {"highratio", "High Ratio", "x", 0.1f, 2.0f, 0.6f},
    {"eqlow", "EQ Low", "Hz", 16.0f, 1600.0f, 100.0f, Taper::Log},
    {"eqlowgain", "EQ Low Gain", "dB", -24.0f, 12.0f, 0.0f},
    {"eqhigh", "EQ High", "Hz", 1000.0f, 21000.0f, 8000.0f, Taper::Log},
    {"eqhighgain", "EQ High Gain", "dB", -24.0f, 12.0f, 0.0f},
    {"gain", "Gain", "dB", -24.0f, 0.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}};

// what each kind of space is: tail size, early span (ms at SIZE 100), early spread / count, input diffusion, shape
enum Shape : int { Natural, Backwards, Gated, Shaped, Echo, Ringing };
struct Kind { float size, earlyMs, spread, density; int taps; Shape shape; };
constexpr Kind kKinds[12] = {
    {1.6f, 90.0f, 1.3f, 0.85f, 20, Natural},   // HALL A
    {2.0f, 110.0f, 1.5f, 0.75f, 22, Natural},  // HALL B
    {0.8f, 40.0f, 1.0f, 0.70f, 14, Natural},   // ROOM
    {1.1f, 60.0f, 1.1f, 0.80f, 16, Natural},   // CHAMBER
    {2.5f, 150.0f, 1.6f, 0.90f, 24, Natural},  // SANCTUARY
    {0.9f, 10.0f, 1.0f, 0.95f, 6, Natural},    // PLATE A
    {1.2f, 14.0f, 1.0f, 0.98f, 8, Natural},    // PLATE B
    {1.4f, 60.0f, 1.2f, 0.85f, 16, Backwards}, // BACKWARDS
    {1.2f, 50.0f, 1.0f, 0.90f, 18, Gated},     // GATED
    {1.3f, 60.0f, 1.1f, 0.85f, 16, Shaped},    // SHAPED
    {1.3f, 80.0f, 1.2f, 0.75f, 16, Echo},      // ECHO ROOM
    {1.0f, 40.0f, 1.0f, 0.70f, 12, Ringing},   // RINGING
};
constexpr double kMaxSize = 2.6, kMaxPreMs = 161.0, kMaxEarlyMs = 160.0, kEchoMs = 500.0;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == Decay) { const int t = pick(Type, 11); return kKinds[t].shape != Backwards && kKinds[t].shape != Gated; }
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        tail_.prepare(fs, kMaxSize);
        early_.prepare(fs, kMaxEarlyMs);
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(kMaxPreMs * 0.001 * fs) + 8);
        for (auto& d : dryD_) d.prepare(static_cast<std::uint32_t>(kMaxPreMs * 0.001 * fs) + 8);
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(kEchoMs * 0.001 * fs) + 8);
        for (auto& d : ring_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        tail_.clear();
        early_.clear();
        for (auto& d : pre_) d.clear();
        for (auto& d : dryD_) d.clear();
        for (auto& d : echo_) d.clear();
        for (auto& d : ring_) d.clear();
        for (auto& ch : eq_) for (auto& f : ch) f.clear();
        key_.fill(-1e9f);
        design();
        env_ = 0.0f;
        since_ = 1e9f;
        echoFb_[0] = echoFb_[1] = 0.0f;
        push_ = p(Reset);
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        erG_.prepare(fs_, 20, levelOf(p(EarlyLevel)));
        rvG_.prepare(fs_, 20, levelOf(p(ReverbLevel)));
        gain_.prepare(fs_, 20, hc::fx::dbToGain(p(Gain)));
        pre_ms_.prepare(fs_, 60, p(PreDelay));
        pk_[0] = pk_[1] = 0.0f;
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        if (p(Reset) != push_) {
            push_ = p(Reset);
            pk_[0] = pk_[1] = 0.0f;
        }
        const Kind& k = kKinds[pick(Type, 11)];
        const float diff = p(Diffusion) * 0.01f, wMix = p(Mix) * 0.01f;
        const float wEr = levelOf(p(EarlyLevel)), wRv = levelOf(p(ReverbLevel)), wGain = hc::fx::dbToGain(p(Gain));
        const float fsF = static_cast<float>(fs_), msToS = 0.001f * fsF;
        const float envRel = coefMs(fs_, 30.0), decay = p(Decay), time = p(Time);
        const bool shaped = k.shape == Shaped || decay < 3.49f;
        const float echoAt = std::clamp(80.0f + 2.2f * p(Size), 80.0f, 300.0f) * msToS;
        const float ringAt = fsF / 220.0f;   // the ring: a 220 Hz comb (and its harmonics)
        float io[2]{};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x[2] = {L[i], R[i]};
            // a new hit restarts the shaped tail's clock
            const float a = std::max(std::fabs(x[0]), std::fabs(x[1]));
            if (a > env_ * 2.0f && a > 1e-3f) since_ = 0.0f;
            env_ = a > env_ ? a : a + envRel * (env_ - a);
            since_ += 1.0f / fsF;
            const float pdMs = pre_ms_.next(p(PreDelay));
            const float wetDelay = std::max(0.0f, pdMs) * msToS, dryDelay = std::max(0.0f, -pdMs) * msToS;
            float pd[2], dry[2];
            for (int c = 0; c < 2; ++c) {
                pre_[c].write(x[c]);
                pd[c] = pre_[c].read(wetDelay);
                dryD_[c].write(x[c]);
                dry[c] = dryD_[c].read(dryDelay);
            }
            float el = 0.0f, er = 0.0f;
            early_.tick(pd[0], pd[1], el, er);
            float tin[2] = {(1.0f - diff) * pd[0] + diff * el, (1.0f - diff) * pd[1] + diff * er};
            if (k.shape == Echo) {   // echoes into the room
                for (int c = 0; c < 2; ++c) {
                    echo_[c].write(tin[c] + 0.45f * echoFb_[1 - c]);
                    echoFb_[c] = echo_[c].read(echoAt * (c == 0 ? 1.0f : 1.13f));
                    tin[c] += 0.7f * echoFb_[c];
                }
            } else if (k.shape == Ringing) {   // a tuned ring in the tail
                for (int c = 0; c < 2; ++c) {
                    const float r = ring_[c].read(ringAt * (c == 0 ? 1.0f : 1.003f));
                    ring_[c].write(tin[c] + 0.92f * r);
                    tin[c] = 0.5f * tin[c] + 0.35f * r;
                }
            }
            float tl = 0.0f, tr = 0.0f;
            tail_.tick(tin[0], tin[1], tl, tr);
            for (auto& f : eq_[0]) tl = f.tick(tl);
            for (auto& f : eq_[1]) tr = f.tick(tr);
            // the envelope of the shaped kinds
            float shape = 1.0f;
            if (k.shape == Backwards) {
                const float T = std::max(0.1f, 0.5f * time);
                // the swell undoes the tail's own decay, so it really grows (to about where the tail began) and then stops
                shape = since_ < T ? (since_ / T) * (since_ / T) * std::exp(6.91f * since_ / std::max(time, 0.1f)) : 0.0f;
            } else if (k.shape == Gated) {
                const float hold = std::max(0.05f, 0.25f * time);
                shape = since_ < hold ? 1.0f : std::exp(-(since_ - hold) / 0.012f);
            } else if (shaped) {
                shape = std::exp(-since_ / std::max(decay, 0.04f) * (k.shape == Shaped ? 1.5f : 1.0f));
            }
            const float m = mix_.next(wMix), eg = erG_.next(wEr), rg = rvG_.next(wRv), g = gain_.next(wGain);
            const float wl = eg * el + rg * shape * tl, wr = eg * er + rg * shape * tr;
            const float ol = ((1.0f - m) * dry[0] + m * wl) * g, orr = ((1.0f - m) * dry[1] + m * wr) * g;
            L[i] = ol;
            R[i] = orr;
            io[0] = std::max(io[0], std::fabs(ol));
            io[1] = std::max(io[1], std::fabs(orr));
        }
        pk_[0] = std::max(pk_[0], io[0]);
        pk_[1] = std::max(pk_[1], io[1]);
        publishMeter(0, std::max(dbOf(pk_[0]), -100.0f));
        publishMeter(1, std::max(dbOf(pk_[1]), -100.0f));
    }

private:
    int pick(int i, int hi) const noexcept { return std::clamp(static_cast<int>(std::lround(p(i))), 0, hi); }
    static float levelOf(float db) noexcept { return db <= -39.95f ? 0.0f : hc::fx::dbToGain(db); }
    void design() noexcept {
        const std::array<float, 12> now{p(Type), p(Variation), p(Time), p(Size), p(LowDampHz), p(LowRatio), p(HighDampHz), p(HighRatio),
                                        p(EqLowHz), p(EqLowGain), p(EqHighHz), p(EqHighGain)};
        if (now == key_) return;
        key_ = now;
        const Kind& k = kKinds[pick(Type, 11)];
        const double sz = p(Size) * 0.01;
        tail_.set(k.size * (0.4 + 0.6 * sz), p(Time), p(LowRatio), p(HighRatio), p(LowDampHz), p(HighDampHz), k.density);
        early_.set(k.earlyMs * (0.2 + 0.8 * sz), pick(Variation, 6), k.spread, k.taps, 9000.0);
        const double top = fs_ * 0.45;
        const double lg = p(EqLowGain), hg = p(EqHighGain);
        const C lo = lg <= -23.95 ? C::make(T::HighPass, fs_, std::min<double>(p(EqLowHz), top), 0.7071)
                                  : C::make(T::LowShelf, fs_, std::min<double>(p(EqLowHz), top), 0.7071, lg);
        const C hi = hg <= -23.95 ? C::make(T::LowPass, fs_, std::min<double>(p(EqHighHz), top), 0.7071)
                                  : C::make(T::HighShelf, fs_, std::min<double>(p(EqHighHz), top), 0.7071, hg);
        for (auto& ch : eq_) {
            ch[0].c = lo;
            ch[1].c = hi;
        }
    }

    double fs_ = 48000.0;
    Tail tail_;
    Early early_;
    hc::fx::DelayLine pre_[2], dryD_[2], echo_[2], ring_[2];
    hc::fx::Biquad eq_[2][2];
    std::array<float, 12> key_{};
    float env_ = 0.0f, since_ = 1e9f, echoFb_[2]{}, pk_[2]{}, push_ = 0.0f;
    hc::fx::Smoothed mix_, erG_, rvG_, gain_, pre_ms_;
};

} // namespace
} // namespace bighouse

std::unique_ptr<hc::fx::Effect> makeBigHouse() { return std::make_unique<bighouse::Impl>(); }

} // namespace hem::rack
