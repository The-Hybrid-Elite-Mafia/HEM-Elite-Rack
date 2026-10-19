// Wave 7 dynamics: VCA COMP, PUNCH 160, THRUST BUS, BRIT BUS, GATE, MULTIBAND (see MoreDynamics.hpp).
#include "hem/rack/MoreDynamics.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {

// Gain change for `over` dB above the threshold with a given slope (dB of change per dB over) and
// a quadratic knee of kneeDb around the threshold.
static float slopeKneeDb(float over, float slope, float kneeDb) noexcept {
    if (kneeDb <= 1e-3f) return over > 0.0f ? slope * over : 0.0f;
    if (over <= -kneeDb * 0.5f) return 0.0f;
    if (over >= kneeDb * 0.5f) return slope * over;
    const float k = over + kneeDb * 0.5f;
    return slope * k * k / (2.0f * kneeDb);
}

float kneeGainDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept {
    return slopeKneeDb(levelDb - thresholdDb, 1.0f / std::max(ratio, 1.0f) - 1.0f, kneeDb);
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;
using OS = hc::fx::Oversampler4;

inline float dbOf(float x) noexcept { return 20.0f * std::log10(std::max(x, 1e-9f)); }
inline float coefFor(double fs, double ms) noexcept { return static_cast<float>(std::exp(-1.0 / (fs * std::max(ms, 0.001) * 0.001))); }
// one step of a dB-domain gain smoother: attack when more reduction is wanted, release otherwise
inline float follow(float gr, float target, float att, float rel) noexcept {
    return target + (target < gr ? att : rel) * (gr - target);
}
const char* const kOnOff[] = {"OFF", "ON"};

// The "work" memory used by the program-dependent releases: charges while it compresses harder
// than 1 dB (2 s), drains when it rests (3 s). 0..1.
struct Work {
    float v = 0, charge = 0, drain = 0;
    void set(double fs) noexcept { charge = 1.0f - coefFor(fs, 2000); drain = 1.0f - coefFor(fs, 3000); }
    void tick(float gr) noexcept { v += gr < -1.0f ? (1.0f - v) * charge : -v * drain; }
};

// ---- VCA COMP -------------------------------------------------------------------------------------
namespace vc {
using namespace vca;
const char* const kScN[] = {"OFF", "60 Hz", "90 Hz", "150 Hz", "250 Hz"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -60.0f, 0.0f, -20.0f},
    {"ratio", "Ratio", ":1", 1.0f, 20.0f, 4.0f, Taper::Log},
    {"knee", "Knee", "dB", 0.0f, 24.0f, 6.0f},
    {"attack", "Attack", "ms", 0.1f, 100.0f, 10.0f, Taper::Log},
    {"release", "Release", "ms", 10.0f, 2000.0f, 150.0f, Taper::Log},
    {"autorelease", "Auto Release", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f},
    {"automakeup", "Auto Makeup", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"schpf", "Sidechain HPF", "", 0, 4, 0, Taper::Choice, kScN},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        if (i == Release) return p(AutoRelease) < 0.5f;
        if (i == Makeup) return p(AutoMakeup) < 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; work_.set(fs); reset(); }
    void reset() noexcept override {
        sc_[0].clear();
        sc_[1].clear();
        scIdx_ = -1;
        gr_ = 0.0f;
        env_ = 0.0f;
        work_.v = 0.0f;
        makeup_.prepare(fs_, 30, makeupDb());
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int sc = std::clamp(static_cast<int>(std::lround(p(ScHpf))), 0, 4);
        if (sc != scIdx_) {
            scIdx_ = sc;
            if (sc > 0) sc_[0].c = sc_[1].c = C::make(T::HighPass, fs_, kScHz[sc], 0.7071);
        }
        const float thr = p(Threshold), ratio = p(Ratio), knee = p(Knee);
        const float att = coefFor(fs_, p(Attack));
        const bool autoRel = p(AutoRelease) >= 0.5f;
        const float relManual = coefFor(fs_, p(Release));
        const float envRel = coefFor(fs_, 5.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float dl = L[i], dr = R[i];
            if (sc > 0) { dl = static_cast<float>(sc_[0].tick(dl)); dr = static_cast<float>(sc_[1].tick(dr)); }
            const float x = std::max(std::fabs(dl), std::fabs(dr));
            env_ = x > env_ ? x : x + envRel * (env_ - x);   // peak detector: holds the tops of the waves
            const float target = kneeGainDb(dbOf(env_), thr, ratio, knee);
            // AUTO: lets go in 40 ms after a short hit, up to 640 ms after it has been working a while
            const float rel = autoRel ? coefFor(fs_, 40.0 + 600.0 * work_.v) : relManual;
            gr_ = follow(gr_, target, att, rel);
            work_.tick(gr_);
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_ + makeup_.next(makeupDb()));
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (L[i] * g - L[i])) * o;
            R[i] = (R[i] + m * (R[i] * g - R[i])) * o;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }
private:
    // AUTO MAKEUP gives back half of what the curve takes off a full-scale signal.
    float makeupDb() const noexcept { return p(AutoMakeup) >= 0.5f ? -0.5f * kneeGainDb(0.0f, p(Threshold), p(Ratio), p(Knee)) : p(Makeup); }
    double fs_ = 48000;
    hc::fx::Biquad sc_[2];
    int scIdx_ = -1;
    float gr_ = 0, env_ = 0;
    Work work_;
    hc::fx::Smoothed makeup_, mix_, out_;
};
} // namespace vc

// ---- PUNCH 160 ------------------------------------------------------------------------------------
namespace pu {
using namespace punch;
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -40.0f, 0.0f, -18.0f},
    {"compression", "Compression", ":1", 1.0f, 60.0f, 4.0f, Taper::Log},
    {"overeasy", "Over Easy", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -20.0f, 20.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        ms_ = 0.0;
        gr_ = 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold), ratio = p(Compression), knee = p(OverEasy) >= 0.5f ? 10.0f : 0.0f;
        const double rmsK = 1.0 - std::exp(-1.0 / (fs_ * 0.005));   // ~5 ms true RMS
        const float relStep = static_cast<float>(120.0 / fs_);       // 120 dB per second
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            ms_ += (0.5 * (static_cast<double>(L[i]) * L[i] + static_cast<double>(R[i]) * R[i]) - ms_) * rmsK;
            // RMS, read on a sine scale (+3 dB): a -10 dBFS sine reads -10
            const float level = ms_ > 1e-18 ? static_cast<float>(10.0 * std::log10(ms_) + 3.0103) : -180.0f;
            const float target = kneeGainDb(level, thr, ratio, knee);
            if (target < gr_) {   // the further it has to go, the faster it gets there (15 ms for 10 dB)
                const float need = gr_ - target;
                const double tau = std::clamp(15.0 * 10.0 / std::max(need, 1.0f), 1.0, 15.0);
                gr_ = target + coefFor(fs_, tau) * (gr_ - target);
            } else {
                gr_ = std::min(target, gr_ + relStep);
            }
            most = std::min(most, gr_);
            const float g = hc::fx::dbToGain(gr_);
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (L[i] + m * (L[i] * g - L[i])) * o;
            R[i] = (R[i] + m * (R[i] * g - R[i])) * o;
        }
        publishReduction(most);
    }
private:
    double fs_ = 48000, ms_ = 0;
    float gr_ = 0;
    hc::fx::Smoothed out_, mix_;
};
} // namespace pu

// ---- THRUST BUS -----------------------------------------------------------------------------------
namespace th {
using namespace thrust;
const char* const kRatioN[] = {"1.5:1", "2:1", "3:1", "4:1", "6:1", "10:1"};
const char* const kAttN[] = {"0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms"};
const char* const kRelN[] = {"0.05 s", "0.1 s", "0.2 s", "0.5 s", "1 s", "2 s", "3 s"};
const char* const kKneeN[] = {"HARD", "MED", "SOFT"};
const char* const kThrustN[] = {"NORM", "MED", "LOUD"};
const char* const kStyleN[] = {"NEW", "OLD"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -40.0f, 0.0f, -14.0f},
    {"ratio", "Ratio", "", 0, 5, 3, Taper::Choice, kRatioN},
    {"attack", "Attack", "", 0, 6, 4, Taper::Choice, kAttN},
    {"release", "Release", "", 0, 6, 2, Taper::Choice, kRelN},
    {"knee", "Knee", "", 0, 2, 1, Taper::Choice, kKneeN},
    {"thrust", "Thrust", "", 0, 2, 0, Taper::Choice, kThrustN},
    {"style", "Style", "", 0, 1, 0, Taper::Choice, kStyleN},
    {"link", "Link", "%", 0.0f, 100.0f, 100.0f},
    {"makeup", "Makeup", "dB", 0.0f, 20.0f, 0.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& ch : sc_) for (auto& b : ch) b.clear();
        thrustIdx_ = -1;
        gr_[0] = gr_[1] = 0.0f;
        last_[0] = last_[1] = 0.0f;
        makeup_.prepare(fs_, 30, hc::fx::dbToGain(p(Makeup)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int tIdx = std::clamp(static_cast<int>(std::lround(p(Thrust))), 0, 2);
        if (tIdx != thrustIdx_) {   // what the detector hears: NORM flat, MED less low end, LOUD much less, more top
            thrustIdx_ = tIdx;
            C a = {1, 0, 0, 0, 0}, b = {1, 0, 0, 0, 0};
            if (tIdx == 1) a = C::make(T::LowShelf, fs_, 200, 0.5, -6.0);
            if (tIdx == 2) { a = C::make(T::HighPass, fs_, 150, 0.7071); b = C::make(T::HighShelf, fs_, 4000, 0.5, 4.0); }
            for (auto& ch : sc_) { ch[0].c = a; ch[1].c = b; }
        }
        const float thr = p(Threshold), ratio = kRatios[std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 5)];
        const float att = coefFor(fs_, kAttackMs[std::clamp(static_cast<int>(std::lround(p(Attack))), 0, 6)]);
        const float rel = coefFor(fs_, kReleaseMs[std::clamp(static_cast<int>(std::lround(p(Release))), 0, 6)]);
        const float knee = kKneeDb[std::clamp(static_cast<int>(std::lround(p(Knee))), 0, 2)];
        const bool old = p(Style) >= 0.5f;
        // OLD (feedback) listens to its own output: a slope of 1 - ratio there gives the same ratio
        const float slope = old ? 1.0f - ratio : 1.0f / ratio - 1.0f;
        const float link = p(Link) * 0.01f;
        float most = 0.0f;
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            float lv[2];
            for (int c = 0; c < 2; ++c) {
                const float x = old ? last_[c] : io[c][i];
                auto& f = sc_[static_cast<std::size_t>(c)];
                lv[c] = dbOf(std::fabs(static_cast<float>(f[1].tick(f[0].tick(x)))));
            }
            const float both = std::max(lv[0], lv[1]);
            const float mk = makeup_.next(hc::fx::dbToGain(p(Makeup)));
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            for (int c = 0; c < 2; ++c) {
                const float lev = link * both + (1.0f - link) * lv[c];
                const float target = std::max(slopeKneeDb(lev - thr, slope, knee), -40.0f);
                gr_[c] = follow(gr_[c], target, att, rel);
                const float wet = io[c][i] * hc::fx::dbToGain(gr_[c]);
                last_[c] = wet;
                io[c][i] = (io[c][i] + m * (wet * mk - io[c][i])) * o;
            }
            most = std::min({most, gr_[0], gr_[1]});
        }
        publishReduction(most);
    }
private:
    double fs_ = 48000;
    std::array<std::array<hc::fx::Biquad, 2>, 2> sc_{};
    int thrustIdx_ = -1;
    float gr_[2] = {0, 0}, last_[2] = {0, 0};
    hc::fx::Smoothed makeup_, mix_, out_;
};
} // namespace th

// ---- BRIT BUS -------------------------------------------------------------------------------------
namespace bb {
using namespace britbus;
const char* const kRatioN[] = {"1.5:1", "2:1", "3:1", "4:1", "6:1"};
const char* const kAttN[] = {"FAST", "SLOW"};
const char* const kRecN[] = {"0.1 s", "0.4 s", "0.8 s", "1.5 s", "AUTO1", "AUTO2"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -30.0f, 0.0f, -12.0f},
    {"ratio", "Ratio", "", 0, 4, 1, Taper::Choice, kRatioN},
    {"attack", "Attack", "", 0, 1, 1, Taper::Choice, kAttN},
    {"recovery", "Recovery", "", 0, 5, 4, Taper::Choice, kRecN},
    {"limit", "Limiter", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"limitlevel", "Limit Level", "dB", -12.0f, 0.0f, -1.0f},
    {"makeup", "Makeup", "dB", 0.0f, 20.0f, 0.0f},
    {"drive", "Iron Drive", "%", 0.0f, 100.0f, 25.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
};
inline float iron(float v) noexcept { return v / std::sqrt(1.0f + 0.5f * v * v); }
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    bool paramActive(int i) const noexcept override { return i != LimitLevel || p(Limit) >= 0.5f; }
    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        up_.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : wet_) v.assign(mb, 0.0f);
        for (auto& v : pre_) v.assign(mb, 0.0f);
        for (auto& d : dry_) d.prepare(OS::latency() + 1);
        for (auto& d : plainWet_) d.prepare(OS::latency() + 1);
        work_.set(fs);
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& d : dry_) d.clear();
        for (auto& d : plainWet_) d.clear();
        gr_ = lim_ = 0.0f;
        env_ = 0.0f;
        work_.v = 0.0f;
        dc_[0] = dc_[1] = 0.0;
        driveFade_ = p(Drive) > 0.0f ? 1.0f : 0.0f;
        makeup_.prepare(fs_, 30, hc::fx::dbToGain(p(Makeup)));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float thr = p(Threshold), ratio = kRatios[std::clamp(static_cast<int>(std::lround(p(Ratio))), 0, 4)];
        const bool slow = p(Attack) >= 0.5f;
        const float att = coefFor(fs_, slow ? 10.0 : 2.0);
        const int rec = std::clamp(static_cast<int>(std::lround(p(Recovery))), 0, 5);
        const bool limit = p(Limit) >= 0.5f;
        const float limLevel = p(LimitLevel);
        const float limAtt = coefFor(fs_, 0.5), limRel = coefFor(fs_, 80.0);
        const float envRel = coefFor(fs_, 5.0);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(L[i]), std::fabs(R[i]));
            env_ = x > env_ ? x : x + envRel * (env_ - x);   // the bridge's peak-ish detector
            const float target = kneeGainDb(dbOf(env_), thr, ratio, 4.0f);
            float relMs = 0;
            if (rec < 4) relMs = kRecoveryMs[rec];
            else relMs = rec == 4 ? 100.0f + 1400.0f * work_.v : 200.0f + 4800.0f * work_.v;   // AUTO 1 / AUTO 2
            gr_ = follow(gr_, target, att, coefFor(fs_, relMs));
            work_.tick(gr_);
            float total = gr_;
            if (limit) {   // the limiter section after the compressor
                const float after = dbOf(x) + gr_;
                const float lt = after > limLevel ? limLevel - after : 0.0f;
                lim_ = follow(lim_, lt, limAtt, limRel);
                total += lim_;
            } else {
                lim_ = 0.0f;
            }
            most = std::min(most, total);
            const float g = hc::fx::dbToGain(total) * makeup_.next(hc::fx::dbToGain(p(Makeup)));
            wet_[0][i] = pre_[0][i] = L[i] * g;
            wet_[1][i] = pre_[1][i] = R[i] * g;
        }
        // the output iron (4x oversampled), then MIX against the delayed dry
        const float drive = p(Drive) * 0.01f, dg = 0.6f + 2.4f * drive;
        for (int c = 0; c < 2; ++c) {
            auto& w = wet_[static_cast<std::size_t>(c)];
            os_[c].up(w.data(), up_.data(), n);
            if (drive > 0.0f || driveFade_ > 0.0f)
                for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) up_[i] = iron(dg * up_[i]) / dg;
            os_[c].down(up_.data(), w.data(), n);
        }
        const float step = static_cast<float>(1.0 / (fs_ * 0.010));
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            driveFade_ = drive > 0.0f ? std::min(1.0f, driveFade_ + step) : std::max(0.0f, driveFade_ - step);
            const float m = mix_.next(p(Mix) * 0.01f);
            for (int c = 0; c < 2; ++c) {
                // OS down already delayed the wet by the latency; without drive the plain (linear)
                // path through the oversampler is used, with drive the fade blends in the iron
                const float d = dry_[c].process(io[c][i], OS::latency());
                const float plain = plainWet_[c].process(pre_[static_cast<std::size_t>(c)][i], OS::latency());   // no iron: just delayed
                dc_[c] += (wet_[static_cast<std::size_t>(c)][i] - dc_[c]) * dcK;
                const float ironed = wet_[static_cast<std::size_t>(c)][i] - static_cast<float>(dc_[c]);
                const float w = plain + driveFade_ * (ironed - plain);
                io[c][i] = d + m * (w - d);
            }
        }
        publishReduction(most);
    }
private:
    double fs_ = 48000;
    float gr_ = 0, lim_ = 0, env_ = 0, driveFade_ = 0;
    double dc_[2] = {0, 0};
    Work work_;
    OS os_[2];
    std::vector<float> up_;
    std::array<std::vector<float>, 2> wet_, pre_;
    hc::fx::DelayLine dry_[2], plainWet_[2];
    hc::fx::Smoothed makeup_, mix_;
};
} // namespace bb

// ---- GATE -----------------------------------------------------------------------------------------
namespace ga {
using namespace gate;
const char* const kModeN[] = {"GATE", "EXPAND"};
const char* const kHpfN[] = {"OFF", "60 Hz", "120 Hz", "250 Hz", "500 Hz"};
const char* const kLpfN[] = {"OFF", "12 kHz", "8 kHz", "4 kHz", "2 kHz"};
const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -80.0f, 0.0f, -45.0f},
    {"range", "Range", "dB", 0.0f, 80.0f, 40.0f},
    {"attack", "Attack", "ms", 0.05f, 50.0f, 0.5f, Taper::Log},
    {"hold", "Hold", "ms", 0.0f, 500.0f, 30.0f},
    {"release", "Release", "ms", 5.0f, 2000.0f, 120.0f, Taper::Log},
    {"mode", "Mode", "", 0, 1, 0, Taper::Choice, kModeN},
    {"schpf", "Sidechain HPF", "", 0, 4, 0, Taper::Choice, kHpfN},
    {"sclpf", "Sidechain LPF", "", 0, 4, 0, Taper::Choice, kLpfN},
    {"listen", "Listen", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override { return i != Hold || p(Mode) < 0.5f; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        hp_ = lp_ = -1;
        env_ = 0.0f;
        open_ = false;
        holdLeft_ = 0;
        gr_ = 0.0f;
        publishReduction(0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int hp = std::clamp(static_cast<int>(std::lround(p(ScHpf))), 0, 4), lp = std::clamp(static_cast<int>(std::lround(p(ScLpf))), 0, 4);
        if (hp != hp_) { hp_ = hp; for (auto& ch : f_) ch[0].c = hp > 0 ? C::make(T::HighPass, fs_, kHpfHz[hp], 0.7071) : C{1, 0, 0, 0, 0}; }
        if (lp != lp_) { lp_ = lp; for (auto& ch : f_) ch[1].c = lp > 0 ? C::make(T::LowPass, fs_, std::min<double>(kLpfHz[lp], fs_ * 0.45), 0.7071) : C{1, 0, 0, 0, 0}; }
        const float thr = p(Threshold), range = p(Range);
        const bool expand = p(Mode) >= 0.5f, listen = p(Listen) >= 0.5f;
        const float openC = coefFor(fs_, p(Attack)), closeC = coefFor(fs_, p(Release)), envRel = coefFor(fs_, 10.0);
        const auto hold = static_cast<std::uint32_t>(p(Hold) * 0.001 * fs_);
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float sl = static_cast<float>(f_[0][1].tick(f_[0][0].tick(L[i]))), sr = static_cast<float>(f_[1][1].tick(f_[1][0].tick(R[i])));
            const float x = std::max(std::fabs(sl), std::fabs(sr));
            env_ = x > env_ ? x : x + envRel * (env_ - x);
            const float lev = dbOf(env_);
            float target;
            if (expand) {
                target = lev < thr ? std::max(-range, 2.0f * (lev - thr)) : 0.0f;   // 1:3 below the threshold
            } else {
                if (lev > thr) { open_ = true; holdLeft_ = hold; }
                else if (lev < thr - kHysteresisDb) {
                    if (holdLeft_ > 0) --holdLeft_;
                    else open_ = false;
                }
                target = open_ ? 0.0f : -range;
            }
            gr_ = target > gr_ ? target + openC * (gr_ - target) : target + closeC * (gr_ - target);
            most = std::min(most, gr_);
            if (listen) { L[i] = sl; R[i] = sr; continue; }
            const float g = hc::fx::dbToGain(gr_);
            L[i] *= g;
            R[i] *= g;
        }
        if (gr_ > -1e-6f) gr_ = 0.0f;
        publishReduction(most);
    }
private:
    double fs_ = 48000;
    std::array<std::array<hc::fx::Biquad, 2>, 2> f_{};
    int hp_ = -1, lp_ = -1;
    float env_ = 0, gr_ = 0;
    bool open_ = false;
    std::uint32_t holdLeft_ = 0;
};
} // namespace ga

// ---- MULTIBAND ------------------------------------------------------------------------------------
namespace mb {
using namespace multiband;
const hc::fx::MeterInfo kMeters[] = {{"LOW", "dB", -24.0f, 0.0f}, {"MID", "dB", -24.0f, 0.0f}, {"HIGH", "dB", -24.0f, 0.0f}};
const ParamInfo kParams[kCount] = {
    {"lowx", "Low / Mid", "Hz", 40.0f, 500.0f, 150.0f, Taper::Log},
    {"highx", "Mid / High", "Hz", 1000.0f, 10000.0f, 3000.0f, Taper::Log},
    {"lowthr", "Low Threshold", "dB", -60.0f, 0.0f, -18.0f},
    {"lowratio", "Low Ratio", ":1", 1.0f, 10.0f, 2.0f, Taper::Log},
    {"lowgain", "Low Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"midthr", "Mid Threshold", "dB", -60.0f, 0.0f, -18.0f},
    {"midratio", "Mid Ratio", ":1", 1.0f, 10.0f, 2.0f, Taper::Log},
    {"midgain", "Mid Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"highthr", "High Threshold", "dB", -60.0f, 0.0f, -18.0f},
    {"highratio", "High Ratio", ":1", 1.0f, 10.0f, 2.0f, Taper::Log},
    {"highgain", "High Gain", "dB", -12.0f, 12.0f, 0.0f},
    {"attack", "Attack", "ms", 0.5f, 100.0f, 10.0f, Taper::Log},
    {"release", "Release", "ms", 20.0f, 2000.0f, 150.0f, Taper::Log},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        for (auto& ch : x_) for (auto& b : ch) b.clear();
        f1_ = p(LowX);
        f2_ = std::max(p(HighX), f1_ * 2.0f);
        design();
        for (auto& g : gr_) g = 0.0f;
        for (auto& g : gain_) g.prepare(fs_, 30, 1.0f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        for (int b = 0; b < 3; ++b) publishMeter(b, 0.0f);
        publishReduction(0.0f);
        clock_ = 0;
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float att = coefFor(fs_, p(Attack)), rel = coefFor(fs_, p(Release));
        float most[3] = {0, 0, 0};
        const int thrP[3] = {LowThr, MidThr, HighThr}, ratP[3] = {LowRatio, MidRatio, HighRatio}, gainP[3] = {LowGain, MidGain, HighGain};
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            if (clock_++ % 32 == 0) glide();
            float band[2][3];
            for (int c = 0; c < 2; ++c) {
                auto& x = x_[static_cast<std::size_t>(c)];
                const double in = io[c][i];
                // LR4 split at f1, the upper part split again at f2; the low band goes through the
                // f2 all-pass so all three line up in phase and add back to a flat response
                const double lo = x[1].tick(x[0].tick(in)), up = x[3].tick(x[2].tick(in));
                band[c][0] = static_cast<float>(x[8].tick(lo));
                band[c][1] = static_cast<float>(x[5].tick(x[4].tick(up)));
                band[c][2] = static_cast<float>(x[7].tick(x[6].tick(up)));
            }
            float y[2] = {0, 0};
            for (int b = 0; b < 3; ++b) {
                const float lev = dbOf(std::max(std::fabs(band[0][b]), std::fabs(band[1][b])));
                gr_[b] = follow(gr_[b], kneeGainDb(lev, p(thrP[b]), p(ratP[b]), 6.0f), att, rel);
                most[b] = std::min(most[b], gr_[b]);
                const float g = hc::fx::dbToGain(gr_[b]) * gain_[b].next(hc::fx::dbToGain(p(gainP[b])));
                y[0] += band[0][b] * g;
                y[1] += band[1][b] * g;
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = y[0] * o;
            R[i] = y[1] * o;
        }
        for (int b = 0; b < 3; ++b) publishMeter(b, most[b]);
        publishReduction(std::min({most[0], most[1], most[2]}));
    }
private:
    void glide() noexcept {
        const float t1 = p(LowX), t2 = std::max(p(HighX), t1 * 2.0f);   // keep the mid band at least an octave wide
        if (t1 == f1_ && t2 == f2_) return;
        const float k = static_cast<float>(1.0 - std::exp(-32.0 / (fs_ * 0.03)));
        f1_ = std::fabs(t1 - f1_) < 0.01f ? t1 : std::exp(std::log(f1_) + (std::log(t1) - std::log(f1_)) * k);
        f2_ = std::fabs(t2 - f2_) < 0.01f ? t2 : std::exp(std::log(f2_) + (std::log(t2) - std::log(f2_)) * k);
        design();
    }
    void design() noexcept {
        const C lp1 = C::make(T::LowPass, fs_, f1_, 0.7071), hp1 = C::make(T::HighPass, fs_, f1_, 0.7071);
        const C lp2 = C::make(T::LowPass, fs_, f2_, 0.7071), hp2 = C::make(T::HighPass, fs_, f2_, 0.7071);
        const C ap2 = C::make(T::AllPass, fs_, f2_, 0.7071);
        for (auto& x : x_) {
            x[0].c = x[1].c = lp1;
            x[2].c = x[3].c = hp1;
            x[4].c = x[5].c = lp2;
            x[6].c = x[7].c = hp2;
            x[8].c = ap2;
        }
    }
    double fs_ = 48000;
    std::array<std::array<hc::fx::Biquad, 9>, 2> x_{};
    float f1_ = 150, f2_ = 3000;
    float gr_[3] = {0, 0, 0};
    hc::fx::Smoothed gain_[3], out_;
    std::uint32_t clock_ = 0;
};
} // namespace mb

} // namespace

std::unique_ptr<hc::fx::Effect> makeVcaComp() { return std::make_unique<vc::Impl>(); }
std::unique_ptr<hc::fx::Effect> makePunch160() { return std::make_unique<pu::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeThrustBus() { return std::make_unique<th::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeBritBus() { return std::make_unique<bb::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeGate() { return std::make_unique<ga::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeMultiband() { return std::make_unique<mb::Impl>(); }

} // namespace hem::rack
