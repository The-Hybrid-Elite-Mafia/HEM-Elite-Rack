// SAFE HOUSE — speakers in your headphones: crossfeed with head delay and shadow, plus a room (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace safehouse {
namespace {
using namespace hideout;

const char* const kRoomN[] = {"DRY", "STUDIO", "LIVE"};
const ParamInfo kParams[kCount] = {
    {"room", "Room", "", 0, 2, 1, Taper::Choice, kRoomN},
    {"angle", "Angle", "deg", 20.0f, 60.0f, 30.0f},
    {"crossfeed", "Crossfeed", "%", 0.0f, 100.0f, 70.0f},
    {"roomlevel", "Room Level", "%", 0.0f, 100.0f, 40.0f},
    {"bass", "Bass", "dB", -6.0f, 6.0f, 0.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kHeadM = 0.0875, kC = 343.0;
// early reflections of a small studio: delay (ms), level, which side
constexpr int kTaps = 6;
constexpr float kTapMs[kTaps] = {6.1f, 8.3f, 11.7f, 14.2f, 19.9f, 23.4f};
constexpr float kTapGain[kTaps] = {0.32f, 0.28f, 0.22f, 0.2f, 0.15f, 0.12f};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override { return i != RoomLevel || p(Room) >= 0.5f; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : cross_) d.prepare(static_cast<std::uint32_t>(0.002 * fs) + 8);
        for (auto& d : early_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        tail_.prepare(fs, 0.6);
        tail_.set(0.5, 0.6, 6000.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& d : cross_) d.clear();
        for (auto& d : early_) d.clear();
        for (auto& b : shadow_) b.clear();
        for (auto& b : bass_) b.clear();
        for (auto& b : earlyTone_) b.clear();
        tail_.clear();
        key_.fill(-1e9f);
        cf_.prepare(fs_, 30, p(Crossfeed) * 0.01f);
        room_.prepare(fs_, 30, roomTarget());
        live_.prepare(fs_, 30, p(Room) >= 1.5f ? 1.0f : 0.0f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float itd = itdSamples();
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i], r = R[i];
            // the far speaker reaches each ear later and darker
            cross_[0].write(l);
            cross_[1].write(r);
            const float lFar = shadow_[0].tick(cross_[0].read(itd)), rFar = shadow_[1].tick(cross_[1].read(itd));
            const float cf = cf_.next(p(Crossfeed) * 0.01f);
            const float norm = 1.0f / (1.0f + 0.5f * cf);   // mono stays the same level
            float yl = (l + cf * rFar) * norm, yr = (r + cf * lFar) * norm;
            // the room: early reflections, and (LIVE) a short tail
            const float rm = room_.next(roomTarget());
            const float lv = live_.next(p(Room) >= 1.5f ? 1.0f : 0.0f);
            early_[0].write(l);
            early_[1].write(r);
            float el = 0.0f, er = 0.0f;
            for (int k = 0; k < kTaps; ++k) {
                const float d = kTapMs[k] * 0.001f * static_cast<float>(fs_);
                const float a = early_[0].read(d), b = early_[1].read(d);
                if (k % 2 == 0) { el += kTapGain[k] * a; er += kTapGain[k] * 0.6f * b; }
                else { er += kTapGain[k] * b; el += kTapGain[k] * 0.6f * a; }
            }
            el = earlyTone_[0].tick(el);
            er = earlyTone_[1].tick(er);
            float tl = 0.0f, tr = 0.0f;
            tail_.tick(l, r, tl, tr);
            yl += rm * (el + lv * 0.3f * tl);
            yr += rm * (er + lv * 0.3f * tr);
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = bass_[0].tick(yl) * o;
            R[i] = bass_[1].tick(yr) * o;
        }
    }

private:
    float roomTarget() const noexcept { return p(Room) >= 0.5f ? p(RoomLevel) * 0.01f : 0.0f; }
    // Woodworth's head delay for a speaker at +-ANGLE
    float itdSamples() const noexcept {
        const double th = p(Angle) * std::numbers::pi / 180.0;
        return static_cast<float>(kHeadM / kC * (th + std::sin(th)) * fs_);
    }
    void design() noexcept {
        const std::array<float, 2> k{p(Angle), p(Bass)};
        if (k == key_) return;
        key_ = k;
        // the head's shadow: more high-frequency loss the wider the speakers sit
        const double th = p(Angle) * std::numbers::pi / 180.0;
        const C sh = C::make(T::HighShelf, fs_, 1500.0, 0.7071, -12.0 * std::sin(th) - 3.0);
        shadow_[0].c = shadow_[1].c = sh;
        bass_[0].c = bass_[1].c = C::make(T::LowShelf, fs_, 100.0, 0.7071, p(Bass));
        earlyTone_[0].c = earlyTone_[1].c = C::make(T::LowPass, fs_, std::min(7000.0, fs_ * 0.45), 0.7071);
    }

    double fs_ = 48000.0;
    hc::fx::DelayLine cross_[2], early_[2];
    hc::fx::Biquad shadow_[2], bass_[2], earlyTone_[2];
    Fdn8 tail_;
    std::array<float, 2> key_{};
    hc::fx::Smoothed cf_, room_, live_, out_;
};

} // namespace
} // namespace safehouse

std::unique_ptr<hc::fx::Effect> makeSafeHouse() { return std::make_unique<safehouse::Impl>(); }

} // namespace hem::rack
