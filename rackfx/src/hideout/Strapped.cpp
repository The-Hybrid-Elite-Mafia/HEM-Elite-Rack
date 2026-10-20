// STRAPPED — guitar mix strip: tight, compress, three-band tone, chorus, delay, room, width (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace strapped {
namespace {
using namespace hideout;

const ParamInfo kParams[kCount] = {
    {"tight", "Tight", "Hz", 20.0f, 300.0f, 80.0f, Taper::Log},
    {"compress", "Compress", "%", 0.0f, 100.0f, 0.0f},
    {"bass", "Bass", "dB", -12.0f, 12.0f, 0.0f},
    {"mids", "Mids", "dB", -12.0f, 12.0f, 0.0f},
    {"treble", "Treble", "dB", -12.0f, 12.0f, 0.0f},
    {"chorus", "Chorus", "%", 0.0f, 100.0f, 0.0f},
    {"delay", "Delay", "%", 0.0f, 100.0f, 0.0f},
    {"note", "Delay Note", "", 0, 7, 3, Taper::Choice, kNoteN},
    {"room", "Room", "%", 0.0f, 100.0f, 0.0f},
    {"width", "Width", "%", 0.0f, 200.0f, 100.0f},
    {"output", "Output", "dB", -18.0f, 18.0f, 0.0f},
};
constexpr double kMaxDelayS = 2.1;   // 1 bar at 115 BPM; slower tempos get the longest that fits

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override { return i != Note || p(Delay) > 0.0f; }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : chorus_) d.prepare(static_cast<std::uint32_t>(0.03 * fs) + 8);
        for (auto& d : echo_) d.prepare(static_cast<std::uint32_t>(kMaxDelayS * fs) + 8);
        room_.prepare(fs, 0.5);
        room_.set(0.4, 0.9, 7000.0);
        reset();
    }
    void reset() noexcept override {
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        for (auto& d : chorus_) d.clear();
        for (auto& d : echo_) d.clear();
        for (auto& b : echoTone_) b.clear();
        room_.clear();
        key_.fill(-1e9f);
        env_ = gr_ = 0.0f;
        lfo_ = 0.0;
        fb_[0] = fb_[1] = 0.0f;
        chorus_amt_.prepare(fs_, 30, p(Chorus) * 0.01f);
        delay_amt_.prepare(fs_, 30, p(Delay) * 0.01f);
        room_amt_.prepare(fs_, 30, p(Room) * 0.01f);
        time_.prepare(fs_, 150, delaySamples());
        width_.prepare(fs_, 30, p(Width) * 0.01f);
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        design();
        publishReduction(0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const float c = p(Compress) * 0.01f;
        const float thr = -8.0f - 22.0f * c, ratio = 1.0f + 5.0f * c, makeup = -0.5f * downDb(-8.0f, thr, ratio, 8.0f);
        const float att = coefMs(fs_, 6.0), rel = coefMs(fs_, 150.0), envRel = coefMs(fs_, 10.0);
        const float tTarget = delaySamples();
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            for (auto& b : eq_[0]) l = b.tick(l);
            for (auto& b : eq_[1]) r = b.tick(r);
            if (c > 0.0f) {
                const float x = std::max(std::fabs(l), std::fabs(r));
                env_ = x > env_ ? x : x + envRel * (env_ - x);
                gr_ = follow(gr_, downDb(dbOf(env_), thr, ratio, 8.0f), att, rel);
                most = std::min(most, gr_);
                const float g = hc::fx::dbToGain(gr_ + makeup);
                l *= g;
                r *= g;
            } else {
                gr_ = 0.0f;
            }
            // chorus: one slowly moving copy per side, opposite phases
            lfo_ += 0.7 / fs_;
            if (lfo_ >= 1.0) lfo_ -= 1.0;
            const float sw = static_cast<float>(std::sin(2.0 * std::numbers::pi * lfo_));
            chorus_[0].write(l);
            chorus_[1].write(r);
            const float base = 0.008f * static_cast<float>(fs_), depth = 0.003f * static_cast<float>(fs_);
            const float ch = chorus_amt_.next(p(Chorus) * 0.01f) * 0.7f;
            l += ch * chorus_[0].read(base + depth * sw);
            r += ch * chorus_[1].read(base - depth * sw);
            // delay: a note of the song's tempo, ping-ponging, darker each time
            const float t = time_.next(tTarget);
            echo_[0].write(l + 0.35f * fb_[1]);
            echo_[1].write(r + 0.35f * fb_[0]);
            fb_[0] = echoTone_[0].tick(echo_[0].read(t));
            fb_[1] = echoTone_[1].tick(echo_[1].read(t));
            const float dl = delay_amt_.next(p(Delay) * 0.01f) * 0.6f;
            float yl = l + dl * fb_[0], yr = r + dl * fb_[1];
            // room
            float rl = 0.0f, rr = 0.0f;
            room_.tick(l, r, rl, rr);
            const float rm = room_amt_.next(p(Room) * 0.01f) * 0.6f;
            yl += rm * rl;
            yr += rm * rr;
            // width
            const float w = width_.next(p(Width) * 0.01f);
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * w;
            const float o = out_.next(hc::fx::dbToGain(p(Output)));
            L[i] = (m + s) * o;
            R[i] = (m - s) * o;
        }
        publishReduction(most);
    }

private:
    float delaySamples() const noexcept {
        const int note = std::clamp(static_cast<int>(std::lround(p(Note))), 0, 7);
        return static_cast<float>(std::min(beatsMs(kNoteBeats[note], tempo()) * 0.001, kMaxDelayS - 0.01) * fs_);
    }
    void design() noexcept {
        const std::array<float, 4> k{p(Tight), p(Bass), p(Mids), p(Treble)};
        if (k == key_) return;
        key_ = k;
        const C hp = C::make(T::HighPass, fs_, p(Tight), 0.7071);
        const C bass = C::make(T::LowShelf, fs_, 120.0, 0.7071, p(Bass));
        const C mids = C::make(T::Bell, fs_, 800.0, 0.8, p(Mids));
        const C treble = C::make(T::HighShelf, fs_, std::min(3500.0, fs_ * 0.4), 0.7071, p(Treble));
        for (auto& ch : eq_) { ch[0].c = hp; ch[1].c = bass; ch[2].c = mids; ch[3].c = treble; }
        echoTone_[0].c = echoTone_[1].c = C::make(T::LowPass, fs_, std::min(3500.0, fs_ * 0.4), 0.7071);
    }

    double fs_ = 48000.0;
    std::array<std::array<hc::fx::Biquad, 4>, 2> eq_{};
    hc::fx::DelayLine chorus_[2], echo_[2];
    hc::fx::Biquad echoTone_[2];
    Fdn8 room_;
    std::array<float, 4> key_{};
    float env_ = 0, gr_ = 0, fb_[2]{};
    double lfo_ = 0;
    hc::fx::Smoothed chorus_amt_, delay_amt_, room_amt_, time_, width_, out_;
};

} // namespace
} // namespace strapped

std::unique_ptr<hc::fx::Effect> makeStrapped() { return std::make_unique<strapped::Impl>(); }

} // namespace hem::rack
