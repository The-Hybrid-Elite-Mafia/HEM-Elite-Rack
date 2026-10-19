// Reverbs and echoes: HALL, ROOM, PLATE, SPRING, TAPE ECHO, DIGITAL DELAY (see Space.hpp).
#include "hem/rack/Space.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace hem::rack {

float ddelay::delayMs(int division, float bpm, float freeMs) noexcept {
    const int d = std::clamp(division, 0, 7);
    if (d == 0) return freeMs;
    return 60000.0f / std::max(bpm, 1.0f) * kBeats[d];
}

namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using C = hc::fx::BiquadCoefs;
using T = C::Type;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
const char* const kOnOff[] = {"OFF", "ON"};
inline std::uint32_t samples(double fs, double ms) noexcept { return static_cast<std::uint32_t>(std::lround(fs * ms * 0.001)); }

// Schroeder all-pass (diffuser) with a fixed delay.
struct Diffuser {
    hc::fx::DelayLine d;
    std::uint32_t len = 1;
    void prepare(std::uint32_t maxLen) { d.prepare(maxLen + 1); }
    void clear() noexcept { d.clear(); }
    float tick(float x, float g) noexcept {
        const float z = d.read(static_cast<float>(len - 1));
        const float v = x + g * z;
        d.write(v);
        return z - g * v;
    }
};

// ---- the FDN reverb ---------------------------------------------------------------------------------
namespace rv {
using namespace verb;
constexpr int kLines = 8;
constexpr double kLineMs[kLines] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1};
constexpr double kLfoHz[kLines] = {0.31, 0.47, 0.59, 0.71, 0.83, 0.97, 1.07, 1.19};
constexpr double kDiffMs[4] = {4.7, 3.6, 12.7, 9.3};
struct Tap { double ms; float g; };
constexpr Tap kEarly[8] = {{4.3, 0.84f}, {8.9, 0.63f}, {13.1, 0.50f}, {17.9, -0.42f}, {23.3, 0.35f}, {29.1, -0.30f}, {36.7, 0.25f}, {44.3, 0.20f}};
constexpr double kMaxScale = 2.2;
constexpr float kModMax = 12.0f;   // samples of movement at MOD 100 %

const ParamInfo kHall[kCount] = {
    {"predelay", "Pre-Delay", "ms", 0.0f, 200.0f, 20.0f},  {"size", "Size", "%", 0.0f, 100.0f, 60.0f},
    {"decay", "Decay", "s", 0.5f, 12.0f, 2.5f, Taper::Log}, {"damping", "Damping", "%", 0.0f, 100.0f, 40.0f},
    {"early", "Early", "%", 0.0f, 100.0f, 40.0f},          {"mod", "Mod", "%", 0.0f, 100.0f, 30.0f},
    {"width", "Width", "%", 0.0f, 150.0f, 100.0f},          {"lowcut", "Low Cut", "Hz", 20.0f, 500.0f, 80.0f, Taper::Log},
    {"highcut", "High Cut", "Hz", 1000.0f, 20000.0f, 9000.0f, Taper::Log}, {"mix", "Mix", "%", 0.0f, 100.0f, 25.0f},
};
const ParamInfo kPlate[kCount] = {
    {"predelay", "Pre-Delay", "ms", 0.0f, 200.0f, 10.0f},  {"size", "Size", "%", 0.0f, 100.0f, 50.0f},
    {"decay", "Decay", "s", 0.4f, 8.0f, 1.8f, Taper::Log},  {"damping", "Damping", "%", 0.0f, 100.0f, 20.0f},
    {"diffusion", "Diffusion", "%", 0.0f, 100.0f, 80.0f},  {"mod", "Mod", "%", 0.0f, 100.0f, 25.0f},
    {"width", "Width", "%", 0.0f, 150.0f, 100.0f},          {"lowcut", "Low Cut", "Hz", 20.0f, 500.0f, 120.0f, Taper::Log},
    {"highcut", "High Cut", "Hz", 1000.0f, 20000.0f, 12000.0f, Taper::Log}, {"mix", "Mix", "%", 0.0f, 100.0f, 25.0f},
};

class Reverb final : public hc::fx::Effect {
public:
    explicit Reverb(Kind k) : kind_(k) { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kind_ == Kind::Hall ? std::span<const ParamInfo>(kHall) : std::span<const ParamInfo>(kPlate); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        const std::uint32_t maxLine = samples(fs, kLineMs[kLines - 1] * kMaxScale) + static_cast<std::uint32_t>(kModMax) + 4;
        for (auto& l : line_) l.prepare(maxLine);
        pre_.prepare(samples(fs, 200.0) + 2);
        early_.prepare(samples(fs, 44.3 * kMaxScale * 1.1) + 2);
        for (auto& d : diff_) d.prepare(samples(fs, 12.7 * kMaxScale) + 2);
        reset();
    }
    void reset() noexcept override {
        for (auto& l : line_) l.clear();
        pre_.clear();
        early_.clear();
        for (auto& d : diff_) d.clear();
        for (auto& b : eq_) for (auto& x : b) x.clear();
        lp_.fill(0.0f);
        lfo_.fill(0.0);
        clock_ = 0;
        for (int i = 0; i < kLines; ++i) lfo_[static_cast<std::size_t>(i)] = kTwoPi * i / kLines;
        eqKey_ = -1.0f;
        scale_.prepare(fs_, 200, scaleFor(p(Size)));
        pd_.prepare(fs_, 100, static_cast<float>(p(PreDelay) * 0.001 * fs_));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        designEq();
        const float decay = p(Decay), damp = p(Damping) * 0.01f, mod = p(Mod) * 0.01f * kModMax;
        const float width = p(Width) * 0.01f;
        const float er = kind_ == Kind::Plate ? 0.0f : p(Early) * 0.01f;
        const float diffG = kind_ == Kind::Plate ? 0.45f + 0.3f * p(Early) * 0.01f : kind_ == Kind::Hall ? 0.6f : 0.5f;
        // damping: a one-pole low-pass in each line, from open (0 %) to dark (100 %)
        const float dampK = static_cast<float>(std::exp(-kTwoPi * (16000.0 * std::pow(0.08, damp)) / fs_));
        std::array<double, kLines> inc{};
        for (int i = 0; i < kLines; ++i) inc[static_cast<std::size_t>(i)] = kTwoPi * kLfoHz[i] / fs_;
        for (std::uint32_t s = 0; s < n; ++s) {
            const float scale = scale_.next(scaleFor(p(Size)));
            const float pdS = pd_.next(static_cast<float>(p(PreDelay) * 0.001 * fs_));
            const float mono = 0.5f * (L[s] + R[s]);
            pre_.write(mono);
            const float in = pre_.read(pdS);
            // early reflections: taps on the pre-delayed input
            early_.write(in);
            float eL = 0, eR = 0;
            if (er > 0.0f)
                for (const Tap& t : kEarly) {
                    eL += t.g * early_.read(static_cast<float>(t.ms * 0.001 * fs_ * scale));
                    eR += t.g * early_.read(static_cast<float>(t.ms * 1.07 * 0.001 * fs_ * scale));
                }
            // input diffusion
            float x = in;
            for (int d = 0; d < 4; ++d) {
                diff_[static_cast<std::size_t>(d)].len = std::max<std::uint32_t>(1, samples(fs_, kDiffMs[d] * std::min(scale, 1.4f)));
                x = diff_[static_cast<std::size_t>(d)].tick(x, diffG);
            }
            // the network: read, damp, mix (Householder), feed back
            std::array<float, kLines> o{};
            float sum = 0;
            for (int i = 0; i < kLines; ++i) {
                const auto u = static_cast<std::size_t>(i);
                lfo_[u] += inc[u];
                if (lfo_[u] > kTwoPi) lfo_[u] -= kTwoPi;
                const float len = static_cast<float>(kLineMs[i] * 0.001 * fs_) * scale;
                const float v = line_[u].read(len - 1.0f + mod * static_cast<float>(std::sin(lfo_[u])));
                lp_[u] = v + dampK * (lp_[u] - v);
                if ((clock_ & 31u) == 0) g_[u] = static_cast<float>(std::pow(10.0, -3.0 * len / (fs_ * decay)));   // -60 dB after DECAY seconds
                o[u] = lp_[u] * g_[u];
                sum += o[u];
            }
            ++clock_;
            const float h = sum * (2.0f / kLines);
            for (int i = 0; i < kLines; ++i) {
                const auto u = static_cast<std::size_t>(i);
                line_[u].write((i & 1 ? -x : x) * 0.5f + o[u] - h);
            }
            float wl = (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]) * 0.6f + er * eL * 0.5f;
            float wr = (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7]) * 0.6f + er * eR * 0.5f;
            // the reverb's EQ and width
            wl = static_cast<float>(eq_[0][1].tick(eq_[0][0].tick(wl)));
            wr = static_cast<float>(eq_[1][1].tick(eq_[1][0].tick(wr)));
            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * width;
            const float mx = mix_.next(p(Mix) * 0.01f);
            L[s] += mx * ((m + sd) - L[s]);
            R[s] += mx * ((m - sd) - R[s]);
        }
    }

private:
    float scaleFor(float size) const noexcept {
        const float t = size * 0.01f;
        if (kind_ == Kind::Hall) return 1.0f + 1.2f * t;
        return 0.6f + 0.6f * t;
    }
    void designEq() noexcept {
        const float key = p(LowCut) * 100000.0f + p(HighCut);
        if (key == eqKey_) return;
        eqKey_ = key;
        const C hp = C::make(T::HighPass, fs_, p(LowCut), 0.7071), lp = C::make(T::LowPass, fs_, std::min<double>(p(HighCut), fs_ * 0.45), 0.7071);
        for (auto& b : eq_) { b[0].c = hp; b[1].c = lp; }
    }
    Kind kind_;
    double fs_ = 48000;
    std::array<hc::fx::DelayLine, kLines> line_;
    hc::fx::DelayLine pre_, early_;
    std::array<Diffuser, 4> diff_;
    std::array<std::array<hc::fx::Biquad, 2>, 2> eq_{};
    std::array<float, kLines> lp_{}, g_{};
    std::array<double, kLines> lfo_{};
    std::uint32_t clock_ = 0;
    float eqKey_ = -1;
    hc::fx::Smoothed scale_, pd_, mix_;
};
} // namespace rv

// ---- DIGITAL DELAY ----------------------------------------------------------------------------------
namespace dd {
using namespace ddelay;
const char* const kDivN[] = {"FREE", "1/4", "1/8", "1/8 D", "1/16", "1/4 T", "1/8 T", "1/2"};
const ParamInfo kParams[kCount] = {
    {"division", "Note", "", 0, 7, 2, Taper::Choice, kDivN},
    {"bpm", "BPM", "", 60.0f, 200.0f, 120.0f},
    {"time", "Time", "ms", 1.0f, 2000.0f, 375.0f, Taper::Log},
    {"feedback", "Feedback", "%", 0.0f, 98.0f, 35.0f},
    {"pingpong", "Ping-Pong", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"offset", "Stereo Offset", "ms", 0.0f, 30.0f, 0.0f},
    {"lowcut", "Low Cut", "Hz", 20.0f, 1000.0f, 60.0f, Taper::Log},
    {"highcut", "High Cut", "Hz", 1000.0f, 20000.0f, 12000.0f, Taper::Log},
    {"mix", "Mix", "%", 0.0f, 100.0f, 30.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        const bool free = std::lround(p(Division)) == 0;
        if (i == Time) return free;
        if (i == Bpm) return !free;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : line_) d.prepare(samples(fs, 2000.0 + 30.0) + 64);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : line_) d.clear();
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        eqKey_ = -1.0f;
        time_.prepare(fs_, 50, target());
        off_.prepare(fs_, 50, static_cast<float>(p(Offset) * 0.001 * fs_));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        fb_.prepare(fs_, 30, p(Feedback) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        designEq();
        const bool ping = p(PingPong) >= 0.5f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float t = time_.next(target()), off = off_.next(static_cast<float>(p(Offset) * 0.001 * fs_));
            const float fb = fb_.next(p(Feedback) * 0.01f), mx = mix_.next(p(Mix) * 0.01f);
            const float el = line_[0].read(t - 1.0f), er = line_[1].read(t + off - 1.0f);
            const float fl = static_cast<float>(f_[0][1].tick(f_[0][0].tick(el))), fr = static_cast<float>(f_[1][1].tick(f_[1][0].tick(er)));
            if (ping) {   // the input goes left; each repeat crosses to the other side
                line_[0].write(0.5f * (L[i] + R[i]) + fb * fr);
                line_[1].write(fb * fl);
            } else {
                line_[0].write(L[i] + fb * fl);
                line_[1].write(R[i] + fb * fr);
            }
            L[i] += mx * (el - L[i]);
            R[i] += mx * (er - R[i]);
        }
    }
private:
    float target() const noexcept {
        const float ms = delayMs(static_cast<int>(std::lround(p(Division))), p(Bpm), p(Time));
        return std::clamp(static_cast<float>(ms * 0.001 * fs_), 1.0f, static_cast<float>(2000.0 * 0.001 * fs_));
    }
    void designEq() noexcept {
        const float key = p(LowCut) * 100000.0f + p(HighCut);
        if (key == eqKey_) return;
        eqKey_ = key;
        const C hp = C::make(T::HighPass, fs_, p(LowCut), 0.7071), lp = C::make(T::LowPass, fs_, std::min<double>(p(HighCut), fs_ * 0.45), 0.7071);
        for (auto& ch : f_) { ch[0].c = hp; ch[1].c = lp; }
    }
    double fs_ = 48000;
    hc::fx::DelayLine line_[2];
    std::array<std::array<hc::fx::Biquad, 2>, 2> f_{};
    float eqKey_ = -1;
    hc::fx::Smoothed time_, off_, mix_, fb_;
};
} // namespace dd

} // namespace

std::unique_ptr<hc::fx::Effect> makeHall() { return std::make_unique<rv::Reverb>(verb::Kind::Hall); }
std::unique_ptr<hc::fx::Effect> makePlate() { return std::make_unique<rv::Reverb>(verb::Kind::Plate); }
std::unique_ptr<hc::fx::Effect> makeDigitalDelay() { return std::make_unique<dd::Impl>(); }

} // namespace hem::rack
