#include "hem/rack/ConsoleEq.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace hem::rack {
namespace console {

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
const char* const kOnOff[] = {"OUT", "IN"};
const char* const kShape[] = {"SHELF", "BELL"};
const char* const kCurveN[] = {"CLASSIC", "MODERN"};

const ParamInfo kParams[kCount] = {
    {"hpf", "HPF In", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"hpfhz", "HPF Frequency", "Hz", 16.0f, 350.0f, 80.0f, Taper::Log},
    {"lpf", "LPF In", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"lpfhz", "LPF Frequency", "Hz", 3000.0f, 22000.0f, 16000.0f, Taper::Log},
    {"lfgain", "LF Gain", "dB", -15.0f, 15.0f, 0.0f},
    {"lfhz", "LF Frequency", "Hz", 30.0f, 450.0f, 100.0f, Taper::Log},
    {"lfbell", "LF Bell", "", 0, 1, 0, Taper::Toggle, kShape},
    {"lmfgain", "LMF Gain", "dB", -15.0f, 15.0f, 0.0f},
    {"lmfhz", "LMF Frequency", "Hz", 200.0f, 2500.0f, 800.0f, Taper::Log},
    {"lmfq", "LMF Q", "", 0.5f, 2.5f, 1.0f, Taper::Log},
    {"hmfgain", "HMF Gain", "dB", -15.0f, 15.0f, 0.0f},
    {"hmfhz", "HMF Frequency", "Hz", 600.0f, 7000.0f, 3000.0f, Taper::Log},
    {"hmfq", "HMF Q", "", 0.5f, 2.5f, 1.0f, Taper::Log},
    {"hfgain", "HF Gain", "dB", -15.0f, 15.0f, 0.0f},
    {"hfhz", "HF Frequency", "Hz", 1500.0f, 16000.0f, 8000.0f, Taper::Log},
    {"hfbell", "HF Bell", "", 0, 1, 0, Taper::Toggle, kShape},
    {"curve", "Curve", "", 0, 1, 0, Taper::Choice, kCurveN},
    {"drive", "Drive", "%", 0.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};

eq::Section fromRbj(const hc::fx::BiquadCoefs& c) { return {c.b0, c.b1, c.b2, c.a1, c.a2}; }
hc::fx::BiquadCoefs toRbj(const eq::Section& s) {
    hc::fx::BiquadCoefs c;
    c.b0 = s.b0; c.b1 = s.b1; c.b2 = s.b2; c.a1 = s.a1; c.a2 = s.a2;
    return c;
}
eq::Section firstOrderHp(double fs, double hz) {
    const double k = std::tan(std::numbers::pi * std::clamp(hz, 2.0, fs * 0.49) / fs), b0 = 1.0 / (1.0 + k);
    return {b0, -b0, 0, (k - 1.0) / (k + 1.0), 0};
}
double sectionDb(const eq::Section& c, double fs, double hz) {
    const double w = 2.0 * std::numbers::pi * hz / fs;
    const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
    const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
    const double dr = 1 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
    return 10.0 * std::log10(std::max(nr * nr + ni * ni, 1e-30) / std::max(dr * dr + di * di, 1e-30));
}

constexpr double kShelfQClassic = 0.7071, kShelfQModern = 1.15, kEdgeBellQ = 0.8;

eq::Section lfSection(const Settings& s, double fs, bool bell) {
    using T = hc::fx::BiquadCoefs::Type;
    return bell ? fromRbj(hc::fx::BiquadCoefs::make(T::Bell, fs, s.lfHz, effectiveQ(kEdgeBellQ, s.lfGain, s.mode), s.lfGain))
                : fromRbj(hc::fx::BiquadCoefs::make(T::LowShelf, fs, s.lfHz, s.mode == CurveMode::Modern ? kShelfQModern : kShelfQClassic, s.lfGain));
}
eq::Section hfSection(const Settings& s, double fs, bool bell) {
    using T = hc::fx::BiquadCoefs::Type;
    return bell ? fromRbj(hc::fx::BiquadCoefs::make(T::Bell, fs, s.hfHz, effectiveQ(kEdgeBellQ, s.hfGain, s.mode), s.hfGain))
                : fromRbj(hc::fx::BiquadCoefs::make(T::HighShelf, fs, s.hfHz, s.mode == CurveMode::Modern ? kShelfQModern : kShelfQClassic, s.hfGain));
}
} // namespace

double effectiveQ(double q, double gainDb, CurveMode mode) noexcept {
    if (mode == CurveMode::Classic) return q;
    return q * (0.5 + std::min(std::fabs(gainDb), 15.0) / 15.0);   // 0.5 × q flat .. 1.5 × q at ±15 dB
}

Settings settingsFrom(const float* v) noexcept {
    Settings s;
    s.hpf = v[HpfOn] >= 0.5f;
    s.hpfHz = v[HpfHz];
    s.lpf = v[LpfOn] >= 0.5f;
    s.lpfHz = v[LpfHz];
    s.lfGain = v[LfGain];
    s.lfHz = v[LfHz];
    s.lfBell = v[LfBell] >= 0.5f;
    s.lmfGain = v[LmfGain];
    s.lmfHz = v[LmfHz];
    s.lmfQ = v[LmfQ];
    s.hmfGain = v[HmfGain];
    s.hmfHz = v[HmfHz];
    s.hmfQ = v[HmfQ];
    s.hfGain = v[HfGain];
    s.hfHz = v[HfHz];
    s.hfBell = v[HfBell] >= 0.5f;
    s.mode = v[Curve] >= 0.5f ? CurveMode::Modern : CurveMode::Classic;
    return s;
}

Design design(const Settings& s, double fs) noexcept {
    using T = hc::fx::BiquadCoefs::Type;
    Design d;
    d.hpfOn = s.hpf;
    d.lpfOn = s.lpf;
    // 18 dB/oct high-pass: one first-order and one second-order section (third-order Butterworth).
    d.hpf1 = firstOrderHp(fs, s.hpfHz);
    d.hpf2 = fromRbj(hc::fx::BiquadCoefs::make(T::HighPass, fs, s.hpfHz, 1.0));
    d.lf = lfSection(s, fs, s.lfBell);
    d.lmf = fromRbj(hc::fx::BiquadCoefs::make(T::Bell, fs, s.lmfHz, effectiveQ(s.lmfQ, s.lmfGain, s.mode), s.lmfGain));
    d.hmf = fromRbj(hc::fx::BiquadCoefs::make(T::Bell, fs, s.hmfHz, effectiveQ(s.hmfQ, s.hmfGain, s.mode), s.hmfGain));
    d.hf = hfSection(s, fs, s.hfBell);
    d.lpf = fromRbj(hc::fx::BiquadCoefs::make(T::LowPass, fs, std::min(s.lpfHz, fs * 0.45), 0.7071));
    return d;
}

double curveDb(const Settings& s, double fs, double hz) noexcept {
    const Design d = design(s, fs);
    double db = sectionDb(d.lf, fs, hz) + sectionDb(d.lmf, fs, hz) + sectionDb(d.hmf, fs, hz) + sectionDb(d.hf, fs, hz);
    if (d.hpfOn) db += sectionDb(d.hpf1, fs, hz) + sectionDb(d.hpf2, fs, hz);
    if (d.lpfOn) db += sectionDb(d.lpf, fs, hz);
    return db;
}

} // namespace console

namespace {
using namespace console;
using OS = hc::fx::Oversampler4;
// The console curve: clean at low levels, rounding off smoothly toward the top.
inline float softClip(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }

class ConsoleEqImpl final : public hc::fx::Effect, public SpectrumSource {
public:
    ConsoleEqImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }

    bool paramActive(int i) const noexcept override {
        switch (i) {
        case HpfHz: return p(HpfOn) >= 0.5f;
        case LpfHz: return p(LpfOn) >= 0.5f;
        default: return true;
        }
    }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * OS::kFactor, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        for (auto& d : delay_) d.prepare(OS::latency() + 1);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }

    void reset() noexcept override {
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        for (auto& o : os_) o.clear();
        for (auto& d : delay_) d.clear();
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        cur_ = settingsFrom(v);
        hpfMix_ = cur_.hpf ? 1.0f : 0.0f;
        lpfMix_ = cur_.lpf ? 1.0f : 0.0f;
        lfBellMix_ = cur_.lfBell ? 1.0f : 0.0f;
        hfBellMix_ = cur_.hfBell ? 1.0f : 0.0f;
        driveFade_ = p(Drive) > 0.0f ? 1.0f : 0.0f;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        clock_ = 0;
        applyDesign();
        before_.reset();
        after_.reset();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        for (std::uint32_t done = 0; done < n;) {
            if (clock_ == 0) updateTargets();
            const std::uint32_t m = std::min(n - done, kStep - clock_);
            runFilters(L + done, R + done, m);
            clock_ = (clock_ + m) % kStep;
            done += m;
        }
        drive(L, R, n);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float g = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] *= g;
            R[i] *= g;
        }
        after_.push(L, R, n);
    }

private:
    static constexpr std::uint32_t kStep = 32;   // settings glide and filters are redesigned every 32 samples
    enum F : int { Hp1, Hp2, LfShelf, LfBellF, Lmf, Hmf, HfShelf, HfBellF, Lp, kF };

    void updateTargets() noexcept {
        float v[kCount];
        for (int i = 0; i < kCount; ++i) v[i] = p(i);
        const Settings t = settingsFrom(v);
        const double k = 1.0 - std::exp(-static_cast<double>(kStep) / (fs_ * 0.03));
        auto glide = [k](double& c, double target) { c += (target - c) * k; if (std::fabs(target - c) < 1e-6) c = target; };
        auto glideLog = [k](double& c, double target) {
            const double lc = std::log(c), lt = std::log(target);
            c = std::exp(lc + (lt - lc) * k);
            if (std::fabs(lt - lc) < 1e-6) c = target;
        };
        glideLog(cur_.hpfHz, t.hpfHz);
        glideLog(cur_.lpfHz, t.lpfHz);
        glide(cur_.lfGain, t.lfGain);
        glideLog(cur_.lfHz, t.lfHz);
        glide(cur_.lmfGain, t.lmfGain);
        glideLog(cur_.lmfHz, t.lmfHz);
        glideLog(cur_.lmfQ, t.lmfQ);
        glide(cur_.hmfGain, t.hmfGain);
        glideLog(cur_.hmfHz, t.hmfHz);
        glideLog(cur_.hmfQ, t.hmfQ);
        glide(cur_.hfGain, t.hfGain);
        glideLog(cur_.hfHz, t.hfHz);
        cur_.hpf = t.hpf;
        cur_.lpf = t.lpf;
        cur_.lfBell = t.lfBell;
        cur_.hfBell = t.hfBell;
        cur_.mode = t.mode;
        applyDesign();
    }

    void applyDesign() noexcept {
        const Design d = design(cur_, fs_);
        Settings shelf = cur_, bell = cur_;
        shelf.lfBell = shelf.hfBell = false;
        bell.lfBell = bell.hfBell = true;
        const Design ds = design(shelf, fs_), db = design(bell, fs_);
        const eq::Section sec[kF] = {d.hpf1, d.hpf2, ds.lf, db.lf, d.lmf, d.hmf, ds.hf, db.hf, d.lpf};
        for (auto& ch : f_)
            for (int i = 0; i < kF; ++i) ch[static_cast<std::size_t>(i)].c = toRbj(sec[i]);
    }

    void runFilters(float* L, float* R, std::uint32_t n) noexcept {
        const float step = static_cast<float>(1.0 / (fs_ * 0.010));   // switches cross-fade over 10 ms
        auto toward = [step](float& m, bool on) { m = on ? std::min(1.0f, m + step) : std::max(0.0f, m - step); };
        for (std::uint32_t i = 0; i < n; ++i) {
            toward(hpfMix_, cur_.hpf);
            toward(lpfMix_, cur_.lpf);
            toward(lfBellMix_, cur_.lfBell);
            toward(hfBellMix_, cur_.hfBell);
            float* io[2] = {L + i, R + i};
            for (int c = 0; c < 2; ++c) {
                auto& f = f_[static_cast<std::size_t>(c)];
                float x = *io[c];
                const float h = f[Hp2].tick(f[Hp1].tick(x));
                x += hpfMix_ * (h - x);
                const float ls = f[LfShelf].tick(x), lb = f[LfBellF].tick(x);
                x = ls + lfBellMix_ * (lb - ls);
                x = f[Hmf].tick(f[Lmf].tick(x));
                const float hs = f[HfShelf].tick(x), hb = f[HfBellF].tick(x);
                x = hs + hfBellMix_ * (hb - hs);
                const float lp = f[Lp].tick(x);
                x += lpfMix_ * (lp - x);
                *io[c] = x;
            }
        }
    }

    // DRIVE: the console's soft saturation, 4x oversampled. At 0 % the path is clean (only delayed
    // like the oversampler, so the latency never changes).
    void drive(float* L, float* R, std::uint32_t n) noexcept {
        const float drive = p(Drive) * 0.01f;
        for (std::uint32_t i = 0; i < n; ++i) {
            sat_[0][i] = L[i];
            sat_[1][i] = R[i];
            L[i] = delay_[0].process(L[i], OS::latency());
            R[i] = delay_[1].process(R[i], OS::latency());
        }
        if (drive <= 0.0f && driveFade_ <= 0.0f) {
            primeOversampler(n);
            return;
        }
        const float g = 1.0f + 3.0f * drive;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(sat_[static_cast<std::size_t>(c)].data(), x4, n);
            for (std::uint32_t i = 0; i < n * OS::kFactor; ++i) x4[i] = softClip(g * x4[i]) / g;
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        const float fadeStep = static_cast<float>(1.0 / (fs_ * 0.010));
        for (std::uint32_t i = 0; i < n; ++i) {
            driveFade_ = drive > 0.0f ? std::min(1.0f, driveFade_ + fadeStep) : std::max(0.0f, driveFade_ - fadeStep);
            L[i] += driveFade_ * (sat_[0][i] - L[i]);
            R[i] += driveFade_ * (sat_[1][i] - R[i]);
        }
    }
    // With DRIVE at 0 the oversampler still sees the signal (cheap compared with the EQ), so its
    // output is already in step the moment DRIVE comes up.
    void primeOversampler(std::uint32_t n) noexcept {
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(sat_[static_cast<std::size_t>(c)].data(), x4, n);
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
    }

    double fs_ = 48000.0;
    Settings cur_;
    std::array<std::array<hc::fx::Biquad, kF>, 2> f_{};
    float hpfMix_ = 0, lpfMix_ = 0, lfBellMix_ = 0, hfBellMix_ = 0, driveFade_ = 0;
    std::uint32_t clock_ = 0;
    hc::fx::Smoothed out_;
    OS os_[2];
    hc::fx::DelayLine delay_[2];
    std::array<std::vector<float>, 2> up_, sat_;
    SpectrumMeter before_, after_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeConsoleEq() { return std::make_unique<ConsoleEqImpl>(); }

} // namespace hem::rack
