// BOTTOM FEEDER — bass amp, DI and cabinet (see Garage.hpp).
//
// INPUT splits in two. The DI side: DI TONE (0 = untouched; up from there a growl lift and a softer top),
// DI LEVEL, DI FLIP. The amp side: the drive pedal (DRIVE TYPE with its TONE, GAIN and MIX), the AMP's
// preamp (GAIN), VOICE (a mid lift, narrow to wide), the EQ (BASS / MID / HI MID / HIGH), the cabinet (CAB
// TONE mixes a darker and a brighter take; CAB OFF skips it) and ROOM. BLEND mixes the two sides, then
// OUTPUT and the limiter. The DI side is delayed to match the amp side (both 23 samples late).
#include "hem/rack/Garage.hpp"

#include "AmpKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace bottomfeeder {
namespace {
using namespace amps;

const char* const kAmpN[] = {"WARM", "DARK"};
const char* const kDriveN[] = {"OFF", "FAT", "TIGHT", "SHARP"};
const char* const kVoiceN[] = {"1", "2", "3"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -12.0f, 12.0f, 0.0f},
    {"amp", "Amp", "", 0, 1, 0, Taper::Choice, kAmpN},
    {"ditone", "DI Tone", "", 0.0f, 10.0f, 0.0f},
    {"dilevel", "DI Level", "", 0.0f, 10.0f, 5.0f},
    {"blend", "Blend", "", 0.0f, 10.0f, 3.0f},
    {"diflip", "DI Flip", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"drivetype", "Drive Type", "", 0, 3, 0, Taper::Choice, kDriveN},
    {"drivetone", "Drive Tone", "", 0.0f, 10.0f, 5.0f},
    {"drivegain", "Drive Gain", "", 0.0f, 10.0f, 5.0f},
    {"drivemix", "Drive Mix", "", 0.0f, 10.0f, 5.0f},
    {"voice", "Voice", "", 0, 2, 1, Taper::Choice, kVoiceN},
    {"gain", "Gain", "", 0.0f, 10.0f, 5.0f},
    {"bass", "Bass", "", 0.0f, 10.0f, 5.0f},
    {"mid", "Mid", "", 0.0f, 10.0f, 5.0f},
    {"himid", "Hi Mid", "", 0.0f, 10.0f, 5.0f},
    {"high", "High", "", 0.0f, 10.0f, 5.0f},
    {"cabtone", "Cab Tone", "", 0.0f, 100.0f, 50.0f},
    {"room", "Room", "", 0.0f, 100.0f, 0.0f},
    {"caboff", "Cab Off", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -12.0f, 12.0f, 0.0f},
    {"clear", "Clear", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "dBFS", -60.0f, 6.0f}, {"OUTPUT", "dBFS", -60.0f, 6.0f}, {"CLIPS", "", 0.0f, 999.0f}};

struct AmpModel { amps::Voice v; float gainLo, gainHi; };
constexpr AmpModel kAmps[2] = {
    {{30.0, 400.0, 2.0, 0.10f, 1}, 0.0f, 24.0f},    // WARM: a tube head, round and full
    {{40.0, 900.0, 4.0, 0.08f, 2}, 6.0f, 36.0f},    // DARK: a modern head that growls
};
// the drive pedal's flavours: where its tone filter sits, how lopsided, a lift before the clipping
struct DriveModel { double toneLo, toneHi, liftHz, liftDb; float bias; };
constexpr DriveModel kDrives[4] = {
    {0, 0, 0, 0, 0},
    {600.0, 4000.0, 150.0, 4.0, 0.15f},     // FAT: low and thick
    {1200.0, 6000.0, 700.0, 4.0, 0.05f},    // TIGHT: mids, the lows kept clean
    {2000.0, 9000.0, 1800.0, 5.0, 0.10f},   // SHARP: grind on top
};
constexpr double kVoiceQ[3] = {3.0, 1.5, 0.7};
constexpr CabSpec kBassCab{45.0, 0.9, 80.0, 3.0, 1500.0, 2.0, 2800.0, -4.0, 4000.0};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return Preamp::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i == DriveTone || i == DriveGain || i == DriveMix) return p(DriveType) >= 0.5f;
        if (i == CabTone || i == Room) return p(CabOff) < 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        pre_.prepare(fs);
        room_.prepare(fs);
        lim_.prepare(fs);
        di_.prepare(Preamp::latency());
        reset();
    }
    void reset() noexcept override {
        pre_.clear();
        room_.clear();
        lim_.clear();
        di_.clear();
        dark_.clear();
        bright_.clear();
        for (auto& ch : diF_) for (auto& b : ch) b.clear();
        for (auto& ch : drvF_) for (auto& b : ch) b.clear();
        for (auto& ch : in_) for (auto& b : ch) b.clear();
        for (auto& ch : eq_) for (auto& b : ch) b.clear();
        key_.fill(-1e9f);
        design();
        dc_[0] = dc_[1] = 0.0f;
        inG_.prepare(fs_, 20, hc::fx::dbToGain(p(Input)));
        outG_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        blend_.prepare(fs_, 30, p(Blend) * 0.1f);
        diLv_.prepare(fs_, 30, diLevel());
        dMix_.prepare(fs_, 30, driveMix());
        cabMix_.prepare(fs_, 30, p(CabTone) * 0.01f);
        roomAmt_.prepare(fs_, 30, p(Room) * 0.01f);
        push_ = p(Clear);
        pkIn_ = pkOut_ = clips_ = 0.0f;
        holdIn_.clear();
        holdOut_.clear();
        publishMeter(0, -100.0f);
        publishMeter(1, -100.0f);
        publishMeter(2, 0.0f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(Clear) != push_) {
            push_ = p(Clear);
            clips_ = 0.0f;
        }
        pkIn_ = pkOut_ = 0.0f;
        for (std::uint32_t off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
        publishMeter(0, holdIn_.push(std::max(dbOf(pkIn_), -100.0f), n, fs_));
        publishMeter(1, holdOut_.push(std::max(dbOf(pkOut_), -100.0f), n, fs_));
        publishMeter(2, clips_);
    }

private:
    static constexpr std::uint32_t kChunk = 64;
    float diLevel() const noexcept { return knobVol(p(DiLevel)) * (p(DiFlip) >= 0.5f ? -1.0f : 1.0f); }
    float driveMix() const noexcept { return p(DriveType) >= 0.5f ? p(DriveMix) * 0.1f : 0.0f; }

    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        design();
        const AmpModel& a = kAmps[amp_];
        const int dt = std::clamp(static_cast<int>(std::lround(p(DriveType))), 0, 3);
        const DriveModel& dm = kDrives[dt];
        const float dGain = hc::fx::dbToGain(6.0f + 3.6f * p(DriveGain));   // +6 .. +42 dB into the pedal
        const float g = hc::fx::dbToGain(a.gainLo + (a.gainHi - a.gainLo) * p(Gain) * 0.1f);
        const float makeup = a.v.stages == 1 ? 1.0f / std::sqrt(std::max(g, 1.0f)) : 0.35f;
        const float wIn = hc::fx::dbToGain(p(Input)), wDmix = driveMix();
        float di[2][kChunk];
        float pi = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = inG_.next(wIn), dmx = dMix_.next(wDmix);
            for (int c = 0; c < 2; ++c) {
                float x = (c == 0 ? L[i] : R[i]) * gi;
                pi = std::max(pi, std::fabs(x));
                // the DI side: its tone, delayed to line up with the amp
                float d = x;
                for (auto& b : diF_[static_cast<std::size_t>(c)]) d = b.tick(d);
                di[c][i] = c == 0 ? di_.l.process(d, Preamp::latency()) : di_.r.process(d, Preamp::latency());
                // the drive pedal, blended in by DRIVE MIX
                if (dt > 0) {
                    auto& f = drvF_[static_cast<std::size_t>(c)];
                    float u = f[0].tick(x) * dGain;
                    u = softClip(u + dm.bias) - softClip(dm.bias);
                    u = f[1].tick(u) * 0.5f;
                    x += dmx * (u - x);
                }
                for (auto& b : in_[static_cast<std::size_t>(c)]) x = b.tick(x);
                (c == 0 ? L[i] : R[i]) = x;
            }
        }
        pkIn_ = std::max(pkIn_, pi);
        pre_.run(L, R, n, a.v, g, makeup);
        const bool cabOn = p(CabOff) < 0.5f;
        const float wBlend = p(Blend) * 0.1f, wDi = diLevel(), wCab = p(CabTone) * 0.01f, wRoom = p(Room) * 0.01f, wOut = hc::fx::dbToGain(p(Output));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float bl = blend_.next(wBlend), dl = diLv_.next(wDi), cm = cabMix_.next(wCab), rm = roomAmt_.next(wRoom), og = outG_.next(wOut);
            for (int c = 0; c < 2; ++c) {
                float x = c == 0 ? L[i] : R[i];
                dc_[c] += (x - dc_[c]) * 0.0005f;
                x -= dc_[c];
                for (auto& b : eq_[static_cast<std::size_t>(c)]) x = b.tick(x);
                if (cabOn) {
                    const float dk = dark_.tick(c, x), br = bright_.tick(c, x);
                    x = dk + cm * (br - dk);
                    if (rm > 0.0f) x += rm * room_.tick(c, x);
                }
                (c == 0 ? L[i] : R[i]) = (x + bl * (dl * di[c][i] - x)) * og;
            }
        }
        if (lim_.run(L, R, n)) clips_ += 1.0f;
        float po = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) po = std::max({po, std::fabs(L[i]), std::fabs(R[i])});
        pkOut_ = std::max(pkOut_, po);
    }

    void design() noexcept {
        const std::array<float, 10> k{p(Amp), p(DiTone), p(DriveType), p(DriveTone), p(Voice), p(Bass), p(Mid), p(HiMid), p(High), 0.0f};
        if (k == key_) return;
        key_ = k;
        amp_ = std::clamp(static_cast<int>(std::lround(p(Amp))), 0, 1);
        const AmpModel& a = kAmps[amp_];
        const double top = fs_ * 0.45;
        // DI TONE: 0 = untouched; up from there a growl lift around 800 Hz and a softer top
        const float t = p(DiTone);
        const C g1 = t > 0.0f ? C::make(T::Bell, fs_, 800.0, 0.9, 0.6 * t) : C{};
        const C g2 = t > 0.0f ? C::make(T::HighShelf, fs_, std::min(5000.0, top), 0.7071, -0.6 * t) : C{};
        for (auto& ch : diF_) { ch[0].c = g1; ch[1].c = g2; }
        // the drive pedal: a lift in, its tone filter out (DRIVE TONE sweeps it)
        const int dt = std::clamp(static_cast<int>(std::lround(p(DriveType))), 0, 3);
        if (dt > 0) {
            const DriveModel& d = kDrives[dt];
            const double toneHz = d.toneLo * std::pow(d.toneHi / d.toneLo, p(DriveTone) * 0.1);
            for (auto& ch : drvF_) {
                ch[0].c = C::make(T::Bell, fs_, d.liftHz, 0.8, d.liftDb);
                ch[1].c = C::make(T::LowPass, fs_, std::min(toneHz, top), 0.7071);
            }
        }
        const C inHp = C::make(T::HighPass, fs_, a.v.inHp, 0.7071);
        const C pre = C::make(T::Bell, fs_, a.v.preHz, 0.7, a.v.preDb);
        for (auto& ch : in_) { ch[0].c = inHp; ch[1].c = pre; }
        // VOICE (a mid lift, narrow to wide) and the EQ
        const int vc = std::clamp(static_cast<int>(std::lround(p(Voice))), 0, 2);
        const C eq[5] = {C::make(T::Bell, fs_, 500.0, kVoiceQ[vc], 4.0), C::make(T::LowShelf, fs_, 80.0, 0.7071, knobDb(p(Bass))),
                         C::make(T::Bell, fs_, 400.0, 0.8, knobDb(p(Mid))), C::make(T::Bell, fs_, 1500.0, 0.9, knobDb(p(HiMid))),
                         C::make(T::HighShelf, fs_, std::min(4000.0, top), 0.7071, knobDb(p(High)))};
        for (auto& ch : eq_) for (int i = 0; i < 5; ++i) ch[static_cast<std::size_t>(i)].c = eq[i];
        dark_.set(cabinet(kBassCab, RibbonOn, fs_));
        bright_.set(cabinet(kBassCab, CondenserOn, fs_));
    }

    double fs_ = 48000.0;
    Preamp pre_;
    EarlyRoom room_;
    OutLimit lim_;
    Delay2 di_;
    CabPath dark_, bright_;
    std::array<std::array<hc::fx::Biquad, 2>, 2> diF_{}, drvF_{}, in_{};
    std::array<std::array<hc::fx::Biquad, 5>, 2> eq_{};
    std::array<float, 10> key_{};
    int amp_ = 0;
    float dc_[2]{}, push_ = 0.0f, pkIn_ = 0.0f, pkOut_ = 0.0f, clips_ = 0.0f;
    maxim::PeakHold holdIn_, holdOut_;
    hc::fx::Smoothed inG_, outG_, blend_, diLv_, dMix_, cabMix_, roomAmt_;
};

} // namespace
} // namespace bottomfeeder

std::unique_ptr<hc::fx::Effect> makeBottomFeeder() { return std::make_unique<bottomfeeder::Impl>(); }

} // namespace hem::rack
