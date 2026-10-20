// MIXTAPE (cassette deck), CRATE DIGGER (record player), SHREDDER (bits and sample rate down): see
// Comeback.hpp. These three are our own designs (not built after one original); they came back from
// retirement as they were in 0.13.0, with the cassette's drive moved onto the stage that only oversamples
// the harmonics (the top octave is no longer dulled by the oversampler).
#include "hem/rack/Comeback.hpp"

#include "ToneCommon.hpp"

namespace hem::rack {
namespace {
using namespace arsenal;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
inline float soft(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }
// A slow DC blocker (5 Hz) per channel, for the lopsided curves.
struct DcBlock {
    double s[2] = {0, 0}, k = 0;
    void set(double fs) noexcept { k = 1.0 - std::exp(-kTwoPi * 5.0 / fs); }
    float tick(int c, float x) noexcept { s[c] += (x - s[c]) * k; return x - static_cast<float>(s[c]); }
};
// Deterministic white noise (-1..1).
struct Rng {
    std::uint32_t s = 12345;
    float next() noexcept { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 8388608.0f - 1.0f; }
};
struct Transport {
    hc::fx::DelayLine d[2];
    std::uint32_t base = 96;
    void prepare(std::uint32_t baseDelay) { base = baseDelay; for (auto& x : d) x.prepare(2 * baseDelay + 4); }
    void clear() noexcept { for (auto& x : d) x.clear(); }
    // mod: samples of displacement (|mod| < base). Exactly `base` behind when mod == 0.
    void tick(float& l, float& r, float mod) noexcept {
        (void)d[0].process(l, 0);
        (void)d[1].process(r, 0);
        if (mod == 0.0f) { l = d[0].read(static_cast<float>(base)); r = d[1].read(static_cast<float>(base)); return; }
        const float at = static_cast<float>(base) + mod;
        l = d[0].read(at);
        r = d[1].read(at);
    }
};

// ---- MIXTAPE
namespace cs {
using namespace mixtape;
const char* const kTypeN[] = {"TYPE I", "TYPE II", "TYPE IV"};
const ParamInfo kParams[kCount] = {
    {"drive", "Drive", "%", 0.0f, 100.0f, 35.0f},
    {"type", "Tape Type", "", 0, 2, 1, Taper::Choice, kTypeN},
    {"wow", "Wow", "%", 0.0f, 100.0f, 20.0f},
    {"flutter", "Flutter", "%", 0.0f, 100.0f, 20.0f},
    {"hiss", "Hiss", "%", 0.0f, 100.0f, 0.0f},
    {"age", "Age", "%", 0.0f, 100.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr std::uint32_t kBase = 96;   // room for ±0.3 % wow at 0.5 Hz (about ±46 samples)
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kMixtapeLatency; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        st_.prepare();
        tr_.prepare(kBase);
        dc_.set(fs);
        reset();
    }
    void reset() noexcept override {
        st_.clear(true);
        tr_.clear();
        for (auto& ch : f_) for (auto& b : ch) b.clear();
        dc_.s[0] = dc_.s[1] = 0;
        wowPh_ = 0.0;
        flPh_ = 0.0;
        hissLp_[0] = hissLp_[1] = 0.0f;
        rng_.s = 12345;
        filterKey_ = -1.0f;
        wow_.prepare(fs_, 50, p(Wow) * 0.01f);
        fl_.prepare(fs_, 50, p(Flutter) * 0.01f);
        hiss_.prepare(fs_, 50, p(Hiss) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        work(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        const int type = std::clamp(static_cast<int>(std::lround(p(Type))), 0, 2);
        // the tape: TYPE I saturates earliest, metal latest; DRIVE pushes it
        const float head[3] = {1.6f, 1.25f, 1.0f};
        const float g = head[type] * std::pow(8.0f, p(Drive) * 0.01f);
        st_.run(L, R, n, true, 1.0f, [g](float x) { const float v = g * x; return soft(v + 0.08f * v * v) / g; });
        designFilters(type);
        const double wowInc = kTwoPi * 0.5 / fs_, flInc = kTwoPi * 6.0 / fs_;
        const float wowDepth = static_cast<float>(0.003 * fs_ / (kTwoPi * 0.5)), flDepth = static_cast<float>(0.0015 * fs_ / (kTwoPi * 6.0));
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = dc_.tick(0, L[i]), r = dc_.tick(1, R[i]);
            // the head and the tape lose the top end (TYPE, AGE)
            l = static_cast<float>(f_[0][1].tick(f_[0][0].tick(l)));
            r = static_cast<float>(f_[1][1].tick(f_[1][0].tick(r)));
            const float w = wow_.next(p(Wow) * 0.01f), fl = fl_.next(p(Flutter) * 0.01f);
            wowPh_ += wowInc; if (wowPh_ > kTwoPi) wowPh_ -= kTwoPi;
            flPh_ += flInc; if (flPh_ > kTwoPi) flPh_ -= kTwoPi;
            const float mod = w * wowDepth * static_cast<float>(std::sin(wowPh_)) + fl * flDepth * static_cast<float>(std::sin(flPh_) + 0.3 * std::sin(2.7 * flPh_));
            tr_.tick(l, r, mod);
            const float h = hiss_.next(p(Hiss) * 0.01f);
            if (h > 0.0f) {   // up to about -40 dBFS of hiss, brightened
                const float level = 0.01f * h * h;
                for (int c = 0; c < 2; ++c) {
                    const float wn = rng_.next();
                    const float bright = wn - 0.6f * hissLp_[c];
                    hissLp_[c] = wn;
                    (c == 0 ? l : r) += level * bright;
                }
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = l * o;
            R[i] = r * o;
        }
    }
private:
    void designFilters(int type) noexcept {
        const float key = static_cast<float>(type) * 1000.0f + p(Age);
        if (key == filterKey_) return;
        filterKey_ = key;
        const double age = p(Age) * 0.01;
        const double top = kTopHz[type] * std::pow(0.45, age);   // AGE: down to under half the bandwidth
        const C a = C::make(T::LowPass, fs_, std::min(top, fs_ * 0.45), 0.6);
        const C b = C::make(T::HighShelf, fs_, 3000, 0.5, -6.0 * age);
        for (auto& ch : f_) { ch[0].c = a; ch[1].c = b; }
    }
    double fs_ = 48000, wowPh_ = 0, flPh_ = 0;
    tone::Drive4 st_;
    Transport tr_;
    std::array<std::array<hc::fx::Biquad, 2>, 2> f_{};
    DcBlock dc_;
    Rng rng_;
    float hissLp_[2] = {0, 0}, filterKey_ = -1;
    hc::fx::Smoothed wow_, fl_, hiss_, out_;
};
} // namespace cs

// ---- CRATE DIGGER
namespace vn {
using namespace cratedigger;
const ParamInfo kParams[kCount] = {
    {"crackle", "Crackle", "%", 0.0f, 100.0f, 0.0f},
    {"noise", "Noise", "%", 0.0f, 100.0f, 0.0f},
    {"wear", "Wear", "%", 0.0f, 100.0f, 30.0f},
    {"warp", "Warp", "%", 0.0f, 100.0f, 20.0f},
    {"monolows", "Mono Lows", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr std::uint32_t kBase = 96;
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return kCrateDiggerLatency; }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        tr_.prepare(kBase);
        const C lp = C::make(T::LowPass, fs, 150, 0.7071), rum = C::make(T::LowPass, fs, 40, 0.7071);
        for (auto& b : mono_) b.c = lp;
        for (auto& b : rumble_) b.c = rum;
        reset();
    }
    void reset() noexcept override {
        tr_.clear();
        for (auto& b : mono_) b.clear();
        for (auto& b : rumble_) b.clear();
        for (auto& ch : wear_) for (auto& b : ch) b.clear();
        ph_ = 0.0;
        rng_.s = 777;
        click_ = 0.0f;
        clickDecay_ = 0.0f;
        clickSide_ = 0;
        wearKey_ = -1.0f;
        monoMix_ = p(MonoLows) >= 0.5f ? 1.0f : 0.0f;
        warp_.prepare(fs_, 50, p(Warp) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        work(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        designWear();
        const double inc = kTwoPi * (33.333 / 60.0) / fs_;   // a 33 rpm record turns 0.56 times a second
        const float depth = static_cast<float>(0.004 * fs_ / (kTwoPi * 33.333 / 60.0));
        const float crackle = p(Crackle) * 0.01f, noise = p(Noise) * 0.01f, wear = p(Wear) * 0.01f;
        const bool mono = p(MonoLows) >= 0.5f;
        const float step = static_cast<float>(1.0 / (fs_ * 0.02));
        const float clicksPerSecond = 40.0f * crackle * crackle;
        const float chance = static_cast<float>(clicksPerSecond / fs_);
        const float decay = static_cast<float>(std::exp(-1.0 / (fs_ * 0.0004)));
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            // MONO LOWS: below ~150 Hz both sides carry the same (a cutting lathe does that)
            monoMix_ = mono ? std::min(1.0f, monoMix_ + step) : std::max(0.0f, monoMix_ - step);
            {
                const float side = 0.5f * (l - r);
                const float lowSide = static_cast<float>(mono_[0].tick(side));
                const float s2 = side - monoMix_ * lowSide;
                const float mid = 0.5f * (l + r);
                l = mid + s2;
                r = mid - s2;
            }
            // WEAR: duller, and a touch of grit on the loud parts
            for (int c = 0; c < 2; ++c) {
                float& x = c == 0 ? l : r;
                auto& f = wear_[static_cast<std::size_t>(c)];
                x = static_cast<float>(f[1].tick(f[0].tick(x)));
                if (wear > 0.0f) { const float k = 1.0f + 2.0f * wear; x = soft(k * x) / k; }
            }
            // WARP
            const float w = warp_.next(p(Warp) * 0.01f);
            ph_ += inc; if (ph_ > kTwoPi) ph_ -= kTwoPi;
            tr_.tick(l, r, w * depth * static_cast<float>(std::sin(ph_)));
            // NOISE: surface hiss and rumble
            if (noise > 0.0f) {
                const float rum = static_cast<float>(rumble_[0].tick(rng_.next())) * 0.03f * noise;
                const float h0 = rng_.next() * 0.0015f * noise, h1 = rng_.next() * 0.0015f * noise;
                l += rum + h0;
                r += rum + h1;
            }
            // CRACKLE: short random clicks of random size, mostly on one side
            if (crackle > 0.0f) {
                const float u = 0.5f * (rng_.next() + 1.0f);
                if (u < chance) {
                    const float size = 0.5f * (rng_.next() + 1.0f);
                    click_ = (rng_.next() > 0 ? 1.0f : -1.0f) * (0.02f + 0.25f * size * size * size) * crackle;
                    clickSide_ = rng_.next() > 0 ? 0 : 1;
                    clickDecay_ = 1.0f;
                }
                if (clickDecay_ > 1e-4f) {
                    const float v = click_ * clickDecay_ * (rng_.next() * 0.5f + 0.5f);
                    l += clickSide_ == 0 ? v : 0.4f * v;
                    r += clickSide_ == 1 ? v : 0.4f * v;
                    clickDecay_ *= decay;
                }
            }
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = l * o;
            R[i] = r * o;
        }
    }
private:
    void designWear() noexcept {
        const float wear = p(Wear);
        if (wear == wearKey_) return;
        wearKey_ = wear;
        const double w = wear * 0.01;
        const C id{1, 0, 0, 0, 0};   // WEAR 0: untouched
        const C a = w <= 0.0 ? id : C::make(T::LowPass, fs_, std::min(20000.0 * std::pow(0.3, w), fs_ * 0.45), 0.7071);
        const C b = w <= 0.0 ? id : C::make(T::HighPass, fs_, 10.0 + 40.0 * w, 0.7071);
        for (auto& ch : wear_) { ch[0].c = a; ch[1].c = b; }
    }
    double fs_ = 48000, ph_ = 0;
    Transport tr_;
    hc::fx::Biquad mono_[1], rumble_[1];
    std::array<std::array<hc::fx::Biquad, 2>, 2> wear_{};
    Rng rng_;
    float click_ = 0, clickDecay_ = 0, wearKey_ = -1, monoMix_ = 0;
    int clickSide_ = 0;
    hc::fx::Smoothed warp_, out_;
};
} // namespace vn

// ---- SHREDDER
namespace bc {
using namespace shredder;
const ParamInfo kParams[kCount] = {
    {"bits", "Bits", "", 1.0f, 24.0f, 8.0f},
    {"downsample", "Downsample", "x", 1.0f, 50.0f, 4.0f, Taper::Log},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    void prepare(double fs, std::uint32_t) override { fs_ = fs; reset(); }
    void reset() noexcept override {
        phase_ = 0.0;   // the first sample is taken at once
        held_[0] = held_[1] = 0.0f;
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        work(L, R, n);
    }
    void work(float* L, float* R, std::uint32_t n) noexcept {
        // BITS: 2^(bits-1) steps each side of zero (fractional bits blend smoothly between depths)
        const float steps = std::pow(2.0f, p(Bits) - 1.0f);
        const double hold = std::max(1.0f, p(Downsample));
        float* io[2] = {L, R};
        for (std::uint32_t i = 0; i < n; ++i) {
            const bool take = phase_ <= 0.0;   // a new sample every DOWNSAMPLE samples (fractional allowed)
            if (take) phase_ += hold;
            phase_ -= 1.0;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            for (int c = 0; c < 2; ++c) {
                const float x = io[c][i];
                if (take) held_[c] = std::round(std::clamp(x, -1.0f, 1.0f) * steps) / steps;
                io[c][i] = (x + m * (held_[c] - x)) * o;
            }
        }
    }
private:
    double fs_ = 48000, phase_ = 0.0;
    float held_[2] = {0, 0};
    hc::fx::Smoothed mix_, out_;
};
} // namespace bc

} // namespace

std::unique_ptr<hc::fx::Effect> makeMixtape() { return std::make_unique<cs::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeCrateDigger() { return std::make_unique<vn::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeShredder() { return std::make_unique<bc::Impl>(); }

} // namespace hem::rack
