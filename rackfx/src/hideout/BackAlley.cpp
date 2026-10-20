// BACK ALLEY — lo-fi space: a spring or plate style reverb and a worn echo, through grit (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace backalley {
namespace {
using namespace hideout;

const char* const kSpaceN[] = {"SPRING", "PLATE"};
const ParamInfo kParams[kCount] = {
    {"space", "Space", "", 0, 1, 0, Taper::Choice, kSpaceN},
    {"reverb", "Reverb", "%", 0.0f, 100.0f, 50.0f},
    {"decay", "Decay", "s", 0.3f, 5.0f, 1.8f, Taper::Log},
    {"echo", "Echo", "%", 0.0f, 100.0f, 40.0f},
    {"time", "Time", "ms", 50.0f, 800.0f, 300.0f, Taper::Log},
    {"feedback", "Feedback", "%", 0.0f, 95.0f, 35.0f},
    {"wobble", "Wobble", "%", 0.0f, 100.0f, 30.0f},
    {"grit", "Grit", "%", 0.0f, 100.0f, 40.0f},
    {"narrow", "Narrow", "%", 0.0f, 100.0f, 30.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 35.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr int kChirp = 8;   // all-passes that make the spring's "boing"

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fdn_.prepare(fs, 1.2);
        echo_[0].prepare(static_cast<std::uint32_t>(0.9 * fs) + 8);
        echo_[1].prepare(static_cast<std::uint32_t>(0.9 * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        fdn_.clear();
        for (auto& e : echo_) e.clear();
        for (auto& ch : chirp_) for (auto& b : ch) b.clear();
        for (auto& ch : band_) for (auto& b : ch) b.clear();
        for (auto& b : fbBand_) b.clear();
        fbL_ = fbR_ = 0.0f;
        wow_ = 0.0;
        flutter_ = 0.0;
        key_.fill(-1.0f);
        time_.prepare(fs_, 150, p(Time) * 0.001f * static_cast<float>(fs_));
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const bool spring = p(Space) < 0.5f;
        const float rv = p(Reverb) * 0.01f, ec = p(Echo) * 0.01f, fb = p(Feedback) * 0.01f;
        const float wob = p(Wobble) * 0.01f, grit = p(Grit) * 0.01f, side = 1.0f - p(Narrow) * 0.01f;
        const float drive = 1.0f + 3.0f * grit;
        const float tTarget = p(Time) * 0.001f * static_cast<float>(fs_);
        const double wowStep = 0.55 / fs_, flutterStep = 6.5 / fs_;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = L[i], dr = R[i];
            // the reverb: (the spring first runs through a chain of all-passes: its chirp)
            float il = dl, ir = dr;
            if (spring)
                for (int k = 0; k < kChirp; ++k) { il = chirp_[0][k].tick(il); ir = chirp_[1][k].tick(ir); }
            float rl = 0.0f, rr = 0.0f;
            fdn_.tick(il, ir, rl, rr);
            // the echo: a worn tape path, its pitch drifting (wow) and trembling (flutter)
            wow_ += wowStep;
            if (wow_ >= 1.0) wow_ -= 1.0;
            flutter_ += flutterStep;
            if (flutter_ >= 1.0) flutter_ -= 1.0;
            const float t = time_.next(tTarget);
            const float mod = wob * (static_cast<float>(std::sin(2.0 * std::numbers::pi * wow_)) * 0.0025f +
                                     static_cast<float>(std::sin(2.0 * std::numbers::pi * flutter_)) * 0.0002f) * static_cast<float>(fs_);
            echo_[0].write(dl + fb * fbL_);
            echo_[1].write(dr + fb * fbR_);
            const float el = echo_[0].read(t + mod + 2.0f), er = echo_[1].read(t * 1.02f + mod + 2.0f);
            // each repeat gets a little more worn
            fbL_ = softClip(fbBand_[0].tick(fbBand_[1].tick(el)) * drive) / drive;
            fbR_ = softClip(fbBand_[2].tick(fbBand_[3].tick(er)) * drive) / drive;
            // the whole wet side through the grit: a narrower band, a little saturation
            float wl = rv * rl + ec * el, wr = rv * rr + ec * er;
            wl = softClip(band_[0][1].tick(band_[0][0].tick(wl)) * drive) / drive;
            wr = softClip(band_[1][1].tick(band_[1][0].tick(wr)) * drive) / drive;
            const float m = mix_.next(p(Mix) * 0.01f), o = out_.next(hc::fx::dbToGain(p(Output)));
            float yl = dl + m * (wl - dl), yr = dr + m * (wr - dr);
            const float mid = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * side;
            L[i] = (mid + s) * o;
            R[i] = (mid - s) * o;
        }
    }

private:
    void design() noexcept {
        const std::array<float, 3> k{p(Space), p(Decay), p(Grit)};
        if (k == key_) return;
        key_ = k;
        const bool spring = p(Space) < 0.5f;
        const float grit = p(Grit) * 0.01f;
        fdn_.set(spring ? 0.55 : 1.0, p(Decay), spring ? 4500.0 : 9000.0);
        const C ap = C::make(T::AllPass, fs_, 1800.0, 0.55);
        for (auto& ch : chirp_) for (auto& b : ch) b.c = ap;
        // grit: 60 Hz .. 14 kHz when clean, 400 Hz .. 3.5 kHz at full grit
        const double lo = 60.0 * std::pow(400.0 / 60.0, grit), hi = 14000.0 * std::pow(3500.0 / 14000.0, grit);
        const C hp = C::make(T::HighPass, fs_, lo, 0.7071), lp = C::make(T::LowPass, fs_, std::min(hi, fs_ * 0.45), 0.7071);
        for (auto& ch : band_) { ch[0].c = hp; ch[1].c = lp; }
        const C fhp = C::make(T::HighPass, fs_, 150.0, 0.7071), flp = C::make(T::LowPass, fs_, std::min(6000.0, fs_ * 0.45), 0.7071);
        fbBand_[0].c = fbBand_[2].c = fhp;
        fbBand_[1].c = fbBand_[3].c = flp;
    }

    double fs_ = 48000.0;
    Fdn8 fdn_;
    hc::fx::DelayLine echo_[2];
    hc::fx::Biquad chirp_[2][kChirp], band_[2][2], fbBand_[4];
    float fbL_ = 0, fbR_ = 0;
    double wow_ = 0, flutter_ = 0;
    std::array<float, 3> key_{};
    hc::fx::Smoothed time_, mix_, out_;
};

} // namespace
} // namespace backalley

std::unique_ptr<hc::fx::Effect> makeBackAlley() { return std::make_unique<backalley::Impl>(); }

} // namespace hem::rack
