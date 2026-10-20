// SPEAKEASY — a room and a distance (see Hideout.hpp).
//
// From ROOM SIZE (volume) the room's edge l = cbrt(V): the early reflections arrive from (the extra path round a wall at
// DISTANCE) up to about 2 l / c later. With REAL ROOM on, the levels follow a real room: the direct sound falls as 1 / DISTANCE,
// the reverb stays where the critical distance (0.057 sqrt(V / T60) m) puts it, and the reverb starts as the last early
// reflection arrives. BALANCE moves level from direct + early to the reverb while keeping the loudness.
#include "hem/rack/Hideout.hpp"

#include "RoomKit.hpp"

namespace hem::rack {
namespace speakeasy {
namespace {
using namespace hideout;

const char* const kVarN[] = {"0", "1", "2", "3", "4", "5", "6"};
const char* const kPushN[] = {"A", "B"};   // any change = a push
const ParamInfo kParams[kCount] = {
    {"dimension", "Room Shape", "", 1.0f, 3.99f, 3.0f},
    {"roomsize", "Room Size", "m3", 50.0f, 20000.0f, 8000.0f, Taper::Log},
    {"distance", "Distance", "m", 0.5f, 50.0f, 5.0f, Taper::Log},
    {"link", "Real Room", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"balance", "Balance", "dB", -18.0f, 18.0f, 0.0f},
    {"decay", "Decay", "s", 0.1f, 20.0f, 2.0f, Taper::Log},
    {"predelay", "Pre-Delay", "ms", 10.0f, 500.0f, 30.0f, Taper::Log},
    {"density", "Density", "", 0.0f, 1.0f, 0.85f},
    {"revshelf", "Tail Dull", "dB", -24.0f, 0.0f, 0.0f},
    {"erabsorb", "Early Dull", "dB", -24.0f, 0.0f, -4.0f},
    {"freq", "Dull Above", "Hz", 1000.0f, 20000.0f, 6000.0f, Taper::Log},
    {"lfdamp", "Low Hz", "Hz", 20.0f, 1000.0f, 300.0f, Taper::Log},
    {"lfratio", "Low X", "x", 0.1f, 2.0f, 1.0f},
    {"hfratio", "High X", "x", 0.1f, 2.0f, 0.5f},
    {"hfdamp", "High Hz", "Hz", 1000.0f, 20000.0f, 7000.0f, Taper::Log},
    {"evar", "Early Take", "", 0, 6, 0, Taper::Choice, kVarN},
    {"rvar", "Tail Take", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"input", "Input", "dB", -60.0f, 0.0f, 0.0f},
    {"direct", "Direct", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"directgain", "Direct Gain", "dB", -24.0f, 6.0f, 0.0f},
    {"early", "Early", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"earlygain", "Early Gain", "dB", -24.0f, 6.0f, 0.0f},
    {"reverb", "Reverb", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"reverbgain", "Reverb Gain", "dB", -24.0f, 6.0f, 0.0f},
    {"reset", "Reset Peaks", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"PEAK L", "dBFS", -60.0f, 6.0f}, {"PEAK R", "dBFS", -60.0f, 6.0f}};
constexpr double kC = 343.0, kMaxSize = 2.6, kMaxEarlyMs = 200.0, kMaxPreMs = 501.0;

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    bool paramActive(int i) const noexcept override {
        if (i == PreDelay) return p(Link) < 0.5f;
        if (i == DirectGain) return p(DirectOn) >= 0.5f;
        if (i == EarlyGain) return p(EarlyOn) >= 0.5f;
        if (i == ReverbGain) return p(ReverbOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        tail_.prepare(fs, kMaxSize);
        early_.prepare(fs, kMaxEarlyMs);
        for (auto& d : pre_) d.prepare(static_cast<std::uint32_t>(kMaxPreMs * 0.001 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        tail_.clear();
        early_.clear();
        for (auto& d : pre_) d.clear();
        for (auto& f : revShelf_) f.clear();
        for (auto& f : erShelf_) f.clear();
        key_.fill(-1e9f);
        design();
        dG_.prepare(fs_, 30, want_.direct);
        eG_.prepare(fs_, 30, want_.early);
        rG_.prepare(fs_, 30, want_.reverb);
        preS_.prepare(fs_, 60, want_.preSamples);
        inG_.prepare(fs_, 20, inGain());
        push_ = p(Reset);
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
        const float wIn = inGain();
        float io[2]{};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = inG_.next(wIn);
            const float x[2] = {L[i] * gi, R[i] * gi};
            float el = 0.0f, er = 0.0f;
            early_.tick(x[0], x[1], el, er);
            el = erShelf_[0].tick(el);
            er = erShelf_[1].tick(er);
            const float pd = preS_.next(want_.preSamples);
            float t[2];
            for (int c = 0; c < 2; ++c) {
                pre_[c].write(revShelf_[c].tick(x[c]));
                t[c] = pre_[c].read(pd);
            }
            float tl = 0.0f, tr = 0.0f;
            tail_.tick(t[0], t[1], tl, tr);
            const float dg = dG_.next(want_.direct), eg = eG_.next(want_.early), rg = rG_.next(want_.reverb);
            const float ol = dg * x[0] + eg * el + rg * tl, orr = dg * x[1] + eg * er + rg * tr;
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
    struct Want { float direct = 1.0f, early = 0.5f, reverb = 0.5f, preSamples = 0.0f; };
    float inGain() const noexcept { return p(Input) <= -59.95f ? 0.0f : hc::fx::dbToGain(p(Input)); }
    void design() noexcept {
        std::array<float, 24> now{};
        for (int i = 0; i < 24; ++i) now[static_cast<std::size_t>(i)] = p(i);
        if (now == key_) return;
        key_ = now;
        const double V = p(RoomSize), l = std::cbrt(V), d = p(Distance), t60 = p(DecayTime);
        // the early reflections: from the extra path round a wall at half the room, to about 2 l / c
        const double firstMs = std::max(0.5, (std::sqrt(d * d + 0.25 * l * l) - d) / kC * 1000.0);
        const double spanMs = std::clamp(firstMs + 2.0 * l / kC * 1000.0, 2.0, kMaxEarlyMs - 1.0);
        const double dim = p(Dimension);
        early_.set(spanMs, static_cast<int>(std::lround(p(EVar))), 1.6 - 0.25 * dim, static_cast<int>(std::lround(4.0 + 5.0 * dim)), fs_ * 0.45, firstMs);
        tail_.set(std::clamp(l / 20.0, 0.1, kMaxSize) * (p(RVar) >= 0.5f ? 1.07 : 1.0), t60, p(LfRatio), p(HfRatio), p(LfDamp), p(HfDamp),
                  p(Density));
        const double top = fs_ * 0.45, f = std::min<double>(p(ShelfFreq), top);
        revShelf_[0].c = revShelf_[1].c = C::make(T::HighShelf, fs_, f, 0.7071, p(RevShelf));
        erShelf_[0].c = erShelf_[1].c = C::make(T::HighShelf, fs_, f, 0.7071, p(ErAbsorb));
        // the levels
        const bool link = p(Link) >= 0.5f;
        double direct = 1.0, early = 0.5, reverb = 0.5;
        if (link) {
            const double rc = 0.057 * std::sqrt(V / std::max(t60, 0.1));   // where the reverb is as loud as the direct
            direct = 1.0 / std::max(d, 1.0);
            early = 0.6 / std::max(std::sqrt(d), 1.0);
            reverb = 1.0 / std::max(rc, 0.3);
        } else {
            early = 0.6 / std::max(std::sqrt(d), 1.0);   // without LINK, DISTANCE sets the room's (early) level
        }
        const double b = std::pow(10.0, p(Balance) / 40.0);
        direct /= b;
        early /= b;
        reverb *= b;
        const auto lvl = [&](int on, int gain, double v) { return p(on) >= 0.5f ? static_cast<float>(v * hc::fx::dbToGain(p(gain))) : 0.0f; };
        want_.direct = lvl(DirectOn, DirectGain, direct);
        want_.early = lvl(EarlyOn, EarlyGain, early);
        want_.reverb = lvl(ReverbOn, ReverbGain, reverb);
        const double preMs = link ? std::max(10.0, firstMs + 0.8 * (spanMs - firstMs)) : p(PreDelay);
        want_.preSamples = static_cast<float>(std::min(preMs, kMaxPreMs - 1.0) * 0.001 * fs_);
    }

    double fs_ = 48000.0;
    Tail tail_;
    Early early_;
    hc::fx::DelayLine pre_[2];
    hc::fx::Biquad revShelf_[2], erShelf_[2];
    std::array<float, 24> key_{};
    Want want_;
    float pk_[2]{}, push_ = 0.0f;
    hc::fx::Smoothed dG_, eG_, rG_, preS_, inG_;
};

} // namespace
} // namespace speakeasy

std::unique_ptr<hc::fx::Effect> makeSpeakeasy() { return std::make_unique<speakeasy::Impl>(); }

} // namespace hem::rack
