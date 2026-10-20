// BACK ALLEY — lo-fi space: a worn echo and a spring or plate reverb, through the master filters (see Hideout.hpp).
#include "hem/rack/Hideout.hpp"

#include "SpaceKit.hpp"

namespace hem::rack {
namespace backalley {
namespace {
using namespace hideout;

const char* const kSyncN[] = {"BPM", "MS", "HOST"};
const char* const kNotesN[] = {"1/64 T", "1/64", "1/32 T", "1/32", "1/16 T", "1/16", "1/16 D", "1/8 T", "1/8", "1/8 D", "1/4 T", "1/4", "1/4 D",
                               "1/2 T", "1/2", "1/2 D", "1 BAR", "2 BAR"};
constexpr double kBeats[] = {1.0 / 24, 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 0.25, 0.375, 1.0 / 3, 0.5, 0.75, 2.0 / 3, 1.0, 1.5, 4.0 / 3, 2.0, 3.0, 4.0, 8.0};
constexpr int kNotes = 18;
const char* const kTypeN[] = {"SPRING", "PLATE"};
const ParamInfo kParams[kCount] = {
    {"echoon", "Echo", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"sync", "Sync", "", 0, 2, 2, Taper::Choice, kSyncN},
    {"bpm", "BPM", "", 40.0f, 300.0f, 120.0f},
    {"time", "Time", "ms", 1.0f, 3500.0f, 300.0f, Taper::Log},
    {"note", "Note", "", 0, kNotes - 1, 11, Taper::Choice, kNotesN},
    {"feedback", "Feedback", "%", 0.0f, 100.0f, 35.0f},
    {"pingpong", "Ping Pong", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"echolevel", "Echo Level", "dB", -60.0f, 0.0f, -6.0f},
    {"reverbon", "Reverb", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"length", "Reverb Length", "s", 0.5f, 20.0f, 2.0f, Taper::Log},
    {"type", "Reverb Type", "", 0, 1, 0, Taper::Choice, kTypeN},
    {"reverblevel", "Reverb Level", "dB", -60.0f, 0.0f, -6.0f},
    {"highpass", "Hi-Pass", "Hz", 30.0f, 1500.0f, 30.0f, Taper::Log},
    {"lowpass", "Lo-Pass", "Hz", 1500.0f, 21000.0f, 21000.0f, Taper::Log},
    {"mono", "Mono", "", 0.0f, 10.0f, 10.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 50.0f},
};
constexpr int kChirp = 8;   // all-passes that make the spring's "boing"
constexpr double kLineS = 3.6;

inline int pick(float v, int hi) noexcept { return std::clamp(static_cast<int>(std::lround(v)), 0, hi); }
inline float levelOrOff(float db) noexcept { return db <= -59.9f ? 0.0f : hc::fx::dbToGain(db); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    bool paramActive(int i) const noexcept override {
        const int s = pick(p(Sync), 2);
        if (i == Time) return s == 1 && p(EchoOn) >= 0.5f;
        if (i == Note) return s != 1 && p(EchoOn) >= 0.5f;
        if (i == Bpm) return s == 0 && p(EchoOn) >= 0.5f;
        if (i == Feedback || i == PingPong || i == EchoLevel || i == Sync) return p(EchoOn) >= 0.5f;
        if (i == Length || i == Type || i == ReverbLevel) return p(ReverbOn) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fdn_.prepare(fs, 1.2);
        for (auto& e : echo_) e.prepare(static_cast<std::uint32_t>(kLineS * fs) + 8);
        reset();
    }
    void reset() noexcept override {
        fdn_.clear();
        for (auto& e : echo_) e.clear();
        for (auto& ch : chirp_) for (auto& b : ch) b.clear();
        for (auto& ch : master_) for (auto& b : ch) b.clear();
        for (auto& b : wear_) b.clear();
        fb_[0] = fb_[1] = 0.0f;
        wow_ = 0.0;
        key_.fill(-1.0f);
        design();
        time_.prepare(fs_, 150, echoSamples());
        echoG_.prepare(fs_, 20, p(EchoOn) >= 0.5f ? levelOrOff(p(EchoLevel)) : 0.0f);
        revG_.prepare(fs_, 20, p(ReverbOn) >= 0.5f ? levelOrOff(p(ReverbLevel)) : 0.0f);
        mix_.prepare(fs_, 30, p(Mix) * 0.01f);
        width_.prepare(fs_, 30, p(Mono) * 0.1f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        design();
        const bool spring = p(Type) < 0.5f, ping = p(PingPong) >= 0.5f;
        const float fb = std::min(p(Feedback) * 0.01f, 1.0f);
        const float tTarget = echoSamples();
        const float wE = p(EchoOn) >= 0.5f ? levelOrOff(p(EchoLevel)) : 0.0f, wR = p(ReverbOn) >= 0.5f ? levelOrOff(p(ReverbLevel)) : 0.0f;
        const float wMix = p(Mix) * 0.01f, wW = p(Mono) * 0.1f;
        const double wowStep = 0.6 / fs_;
        const float maxD = static_cast<float>(kLineS * fs_) - 8.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float dl = L[i], dr = R[i];
            // the reverb (the spring first runs through a chain of all-passes: its chirp)
            float il = dl, ir = dr;
            if (spring)
                for (int k = 0; k < kChirp; ++k) { il = chirp_[0][k].tick(il); ir = chirp_[1][k].tick(ir); }
            float rl = 0.0f, rr = 0.0f;
            fdn_.tick(il, ir, rl, rr);
            // the echo: a worn tape path with a slow wobble; each repeat a little more worn
            wow_ += wowStep;
            wow_ -= std::floor(wow_);
            const float t = time_.next(tTarget) + 0.0015f * static_cast<float>(fs_) * static_cast<float>(std::sin(2.0 * std::numbers::pi * wow_));
            const float el = echo_[0].read(std::clamp(t, 2.0f, maxD)), er = echo_[1].read(std::clamp(t, 2.0f, maxD));
            if (ping) {
                echo_[0].write(0.5f * (dl + dr) + fb * fb_[1]);
                echo_[1].write(fb * fb_[0]);
            } else {
                echo_[0].write(dl + fb * fb_[0]);
                echo_[1].write(dr + fb * fb_[1]);
            }
            fb_[0] = softClip(1.6f * wear_[1].tick(wear_[0].tick(el))) / 1.6f;
            fb_[1] = softClip(1.6f * wear_[3].tick(wear_[2].tick(er))) / 1.6f;
            // the wet side through the master filters
            float wl = revG_.next(wR) * rl + echoG_.next(wE) * el, wr = revG_.cur * rr + echoG_.cur * er;
            wl = master_[0][1].tick(master_[0][0].tick(wl));
            wr = master_[1][1].tick(master_[1][0].tick(wr));
            const float m = mix_.next(wMix), w = width_.next(wW);
            const float yl = dl + m * (wl - dl), yr = dr + m * (wr - dr);
            const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * w;
            L[i] = mid + side;
            R[i] = mid - side;
        }
    }

private:
    float echoSamples() const noexcept {
        const int s = pick(p(Sync), 2);
        double ms = p(Time);
        if (s != 1) ms = kBeats[pick(p(Note), kNotes - 1)] * 60000.0 / (s == 0 ? p(Bpm) : tempo());
        return static_cast<float>(std::clamp(ms, 1.0, 3500.0) * 0.001 * fs_);
    }
    void design() noexcept {
        const std::array<float, 4> k{p(Type), p(Length), p(HighPass), p(LowPass)};
        if (k == key_) return;
        key_ = k;
        const bool spring = p(Type) < 0.5f;
        fdn_.set(spring ? 0.55 : 1.0, p(Length), spring ? 4500.0 : 9000.0);
        const C ap = C::make(T::AllPass, fs_, 1800.0, 0.55);
        for (auto& ch : chirp_) for (auto& b : ch) b.c = ap;
        const C hp = C::make(T::HighPass, fs_, p(HighPass), 0.7071), lp = C::make(T::LowPass, fs_, std::min<double>(p(LowPass), fs_ * 0.45), 0.7071);
        for (auto& ch : master_) { ch[0].c = hp; ch[1].c = lp; }
        const C whp = C::make(T::HighPass, fs_, 150.0, 0.7071), wlp = C::make(T::LowPass, fs_, std::min(6000.0, fs_ * 0.45), 0.7071);
        wear_[0].c = wear_[2].c = whp;
        wear_[1].c = wear_[3].c = wlp;
    }

    double fs_ = 48000.0, wow_ = 0.0;
    Fdn8 fdn_;
    hc::fx::DelayLine echo_[2];
    hc::fx::Biquad chirp_[2][kChirp], master_[2][2], wear_[4];
    float fb_[2]{};
    std::array<float, 4> key_{};
    hc::fx::Smoothed time_, echoG_, revG_, mix_, width_;
};

} // namespace
} // namespace backalley

std::unique_ptr<hc::fx::Effect> makeBackAlley() { return std::make_unique<backalley::Impl>(); }

} // namespace hem::rack
