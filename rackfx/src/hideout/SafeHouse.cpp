// SAFE HOUSE — two speakers in a room, heard through your head (see Hideout.hpp).
//
// Each speaker sits at an angle from where you face (FRONT spreads them, ROTATE turns your head). For each speaker
// and each ear: the far ear hears it later (Woodworth's head delay from the head's size) and darker (head shadow);
// a speaker behind you is darker in both ears. The room (AMOUNT) is early reflections from the side walls and a
// short tail, fed by both speakers.
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace safehouse {
namespace {
using namespace hideout;

const ParamInfo kParams[kCount] = {
    {"amount", "Amount", "%", 0.0f, 100.0f, 17.0f},
    {"front", "Front", "deg", 0.0f, 180.0f, 60.0f},
    {"rotate", "Rotate", "deg", -180.0f, 180.0f, 0.0f},
    {"head", "Head Circumference", "cm", 35.0f, 75.0f, 55.0f},
    {"arc", "Inter-aural Arc", "cm", 15.0f, 40.0f, 25.0f},
    {"mutel", "Mute L", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"muter", "Mute R", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -24.0f, 12.0f, 0.0f},
};
constexpr double kC = 343.0, kPi = std::numbers::pi;
constexpr double kMaxItdS = 0.0016;   // the largest head at 90°, with room to spare
// early reflections: delay (ms), level, which ear hears it first (0 left, 1 right)
constexpr int kTaps = 8;
constexpr float kTapMs[kTaps] = {5.3f, 7.9f, 10.4f, 13.1f, 16.6f, 19.2f, 23.7f, 28.1f};
constexpr float kTapGain[kTaps] = {0.42f, 0.38f, 0.31f, 0.28f, 0.22f, 0.19f, 0.15f, 0.12f};

inline double wrap180(double d) noexcept {
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

// What one speaker sounds like at one ear.
struct Path {
    hc::fx::Biquad shade, rear;
    hc::fx::Smoothed delay, gain;
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : spk_) d.prepare(static_cast<std::uint32_t>(kMaxItdS * fs) + 8);
        for (auto& d : early_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        tail_.prepare(fs, 0.8);
        tail_.set(0.55, 0.45, 6500.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : spk_) d.clear();
        for (auto& d : early_) d.clear();
        for (auto& b : earlyTone_) b.clear();
        tail_.clear();
        key_.fill(-1e9f);
        design();
        for (int s = 0; s < 2; ++s)
            for (int e = 0; e < 2; ++e) {
                Path& pa = path_[s][e];
                pa.shade.clear();
                pa.rear.clear();
                pa.delay.prepare(fs_, 40, want_[s][e].delay);
                pa.gain.prepare(fs_, 40, want_[s][e].gain);
            }
        amt_.prepare(fs_, 30, p(Amount) * 0.01f);
        mute_[0].prepare(fs_, 10, p(MuteL) >= 0.5f ? 0.0f : 1.0f);
        mute_[1].prepare(fs_, 10, p(MuteR) >= 0.5f ? 0.0f : 1.0f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float wAmt = p(Amount) * 0.01f, wOut = hc::fx::dbToGain(p(Output));
        const float wMute[2] = {p(MuteL) >= 0.5f ? 0.0f : 1.0f, p(MuteR) >= 0.5f ? 0.0f : 1.0f};
        for (std::uint32_t i = 0; i < n; ++i) {
            const float in[2] = {L[i] * mute_[0].next(wMute[0]), R[i] * mute_[1].next(wMute[1])};
            float ear[2] = {0.0f, 0.0f};
            for (int s = 0; s < 2; ++s) {
                spk_[s].write(in[s]);
                for (int e = 0; e < 2; ++e) {
                    Path& pa = path_[s][e];
                    const float d = pa.delay.next(want_[s][e].delay), g = pa.gain.next(want_[s][e].gain);
                    ear[e] += g * pa.rear.tick(pa.shade.tick(d > 0.0f ? spk_[s].read(d) : in[s]));
                }
            }
            // the room: side-wall reflections (each reaching one ear first) and a short tail
            const float a = amt_.next(wAmt);
            const float mono = 0.5f * (in[0] + in[1]);
            early_[0].write(in[0]);
            early_[1].write(in[1]);
            float el = 0.0f, er = 0.0f;
            for (int k = 0; k < kTaps; ++k) {
                const float d = kTapMs[k] * 0.001f * static_cast<float>(fs_);
                const float x = early_[k & 1].read(d);
                if (k & 1) { er += kTapGain[k] * x; el += kTapGain[k] * 0.55f * x; }
                else { el += kTapGain[k] * x; er += kTapGain[k] * 0.55f * x; }
            }
            float tl = 0.0f, tr = 0.0f;
            tail_.tick(mono, mono, tl, tr);
            el = earlyTone_[0].tick(el) + 0.35f * tl;
            er = earlyTone_[1].tick(er) + 0.35f * tr;
            const float o = out_.next(wOut);
            L[i] = (ear[0] + a * el) * o;
            R[i] = (ear[1] + a * er) * o;
        }
    }

private:
    struct Want { float delay = 0.0f, gain = 1.0f; };

    void design() noexcept {
        const std::array<float, 4> now{p(Front), p(Rotate), p(Head), p(Arc)};
        if (now == key_) return;
        key_ = now;
        // the head: its radius from the circumference; the arc over the top scales how far round the far ear is
        const double radius = p(Head) * 0.01 / (2.0 * kPi), arcScale = p(Arc) / 25.0;
        double far[2]{};
        for (int s = 0; s < 2; ++s) {
            const double az = wrap180((s == 0 ? -0.5 : 0.5) * p(Front) - p(Rotate));   // + = to the right
            const double azr = az * kPi / 180.0;
            const double side = std::sin(azr);                     // -1 hard left .. +1 hard right
            const double lat = std::asin(std::fabs(side));          // 0..pi/2 from the middle
            const double behind = std::max(0.0, -std::cos(azr));   // 0 in front .. 1 straight behind
            const double itd = std::min(radius / kC * (lat + std::sin(lat)) * arcScale, kMaxItdS - 0.0001) * fs_;
            const int nearEar = side < 0.0 ? 0 : 1;
            for (int e = 0; e < 2; ++e) {
                const bool isNear = e == nearEar || std::fabs(side) < 1e-6;
                const double shadeDb = isNear ? 2.0 * std::fabs(side) : -(3.0 + 13.0 * std::fabs(side)) * std::sqrt(arcScale);
                want_[s][e].delay = isNear ? 0.0f : static_cast<float>(itd);
                want_[s][e].gain = static_cast<float>(hc::fx::dbToGain(static_cast<float>(isNear ? 0.0 : -2.0 * std::fabs(side))));
                if (!isNear) far[s] = want_[s][e].gain;
                path_[s][e].shade.c = C::make(T::HighShelf, fs_, std::min(1600.0, fs_ * 0.45), 0.7071, isNear && std::fabs(side) < 1e-6 ? 0.0 : shadeDb);
                path_[s][e].rear.c = C::make(T::HighShelf, fs_, std::min(4500.0, fs_ * 0.45), 0.7071, -6.0 * behind);
            }
            if (std::fabs(side) < 1e-6) far[s] = 1.0;
        }
        // keep a centred sound about as loud as it was: each speaker's two ears together
        for (int s = 0; s < 2; ++s) {
            const double g = 1.0 / (0.5 * (1.0 + far[s]) + 0.5 * std::sqrt(1.0 + far[s] * far[s]));
            for (int e = 0; e < 2; ++e) want_[s][e].gain *= static_cast<float>(g);
        }
        earlyTone_[0].c = earlyTone_[1].c = C::make(T::LowPass, fs_, std::min(7000.0, fs_ * 0.45), 0.7071);
    }

    double fs_ = 48000.0;
    hc::fx::DelayLine spk_[2], early_[2];
    Path path_[2][2];
    Want want_[2][2];
    hc::fx::Biquad earlyTone_[2];
    Fdn8 tail_;
    std::array<float, 4> key_{};
    hc::fx::Smoothed amt_, mute_[2], out_;
};

} // namespace
} // namespace safehouse

std::unique_ptr<hc::fx::Effect> makeSafeHouse() { return std::make_unique<safehouse::Impl>(); }

} // namespace hem::rack
