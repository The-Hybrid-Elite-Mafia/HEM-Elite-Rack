// HOT WIRE — guitar amp head with cabinet pairs (see Garage.hpp).
//
// In order: INPUT, the GATE, the AMP's voicing and FOCUS, the preamp (CHANNEL picks one, two or three clipping
// stages; GAIN how hard), the tone stack (BASS / MIDDLE / TREBLE), VOLUME, the power amp's DEPTH and
// PRESENCE, the CABINET pair (CAB TONE mixes the darker and the brighter one; CAB OFF skips it), ROOM,
// OUTPUT, the limiter.
#include "hem/rack/Garage.hpp"

#include "AmpKit.hpp"
#include "../maxim/LimitKit.hpp"

namespace hem::rack {
namespace hotwire {
namespace {
using namespace amps;

const char* const kAmpN[] = {"CLASSIC", "CROWN", "STEEL", "BLAZE", "BEAST"};
const char* const kChN[] = {"CLEAN", "DRIVE", "LEAD"};
const char* const kCabN[] = {"1X12 OPEN", "2X12 OPEN", "4X10 OPEN", "2X12 CLOSED", "4X12 OLD", "4X12 NEW"};
const char* const kPushN[] = {"A", "B"};
const ParamInfo kParams[kCount] = {
    {"input", "Input", "dB", -12.0f, 12.0f, 0.0f},
    {"amp", "Amp", "", 0, 4, 0, Taper::Choice, kAmpN},
    {"channel", "Channel", "", 0, 2, 1, Taper::Choice, kChN},
    {"focus", "Focus", "", 0.0f, 10.0f, 0.0f},
    {"gate", "Gate", "dB", -60.0f, 0.0f, -60.0f},
    {"gatespeed", "Gate Speed", "", 0.0f, 10.0f, 5.0f},
    {"gain", "Gain", "", 0.0f, 10.0f, 5.0f},
    {"bass", "Bass", "", 0.0f, 10.0f, 5.0f},
    {"middle", "Middle", "", 0.0f, 10.0f, 5.0f},
    {"treble", "Treble", "", 0.0f, 10.0f, 5.0f},
    {"volume", "Volume", "", 0.0f, 10.0f, 5.0f},
    {"depth", "Depth", "", 0.0f, 10.0f, 3.0f},
    {"presence", "Presence", "", 0.0f, 10.0f, 3.0f},
    {"cabinet", "Cabinet", "", 0, 5, 4, Taper::Choice, kCabN},
    {"cabtone", "Cab Tone", "", 0.0f, 100.0f, 50.0f},
    {"room", "Room", "", 0.0f, 100.0f, 0.0f},
    {"caboff", "Cab Off", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"output", "Output", "dB", -12.0f, 12.0f, 0.0f},
    {"clear", "Clear", "", 0, 1, 0, Taper::Toggle, kPushN},
};
const hc::fx::MeterInfo kMeters[] = {{"INPUT", "dBFS", -60.0f, 6.0f}, {"OUTPUT", "dBFS", -60.0f, 6.0f}, {"CLIPS", "", 0.0f, 999.0f}};

// The amps: voicing per channel (stages come from the channel), gain range (dB over GAIN 0..10), the tone
// stack's middle, the treble corner.
struct AmpModel { double inHp, preHz, preDb; float bias, gainLo, gainHi; double midHz, trebleHz; };
constexpr AmpModel kAmps[5] = {
    {70.0, 900.0, 2.0, 0.08f, 0.0f, 30.0f, 600.0, 2800.0},     // CLASSIC: round, American-style clean that breaks up late
    {90.0, 1100.0, 3.0, 0.12f, 6.0f, 38.0f, 750.0, 3000.0},    // CROWN: British midrange
    {100.0, 1400.0, 4.0, 0.10f, 8.0f, 42.0f, 800.0, 3300.0},   // STEEL: bright and cutting
    {110.0, 900.0, 5.0, 0.15f, 12.0f, 48.0f, 650.0, 3000.0},   // BLAZE: hot-rodded
    {130.0, 700.0, 4.0, 0.10f, 16.0f, 52.0f, 500.0, 3500.0},   // BEAST: tight, scooped, high gain
};
constexpr int kStagesFor[3] = {1, 2, 3};
// each cabinet pair: the darker (ribbon, off axis) and the brighter (dynamic, on axis) take of the same box
constexpr CabSpec kPairs[6] = {
    {75.0, 1.0, 120.0, 2.0, 2400.0, 3.0, 4000.0, -5.0, 5500.0},
    {70.0, 1.0, 110.0, 2.5, 2200.0, 3.0, 3800.0, -4.0, 5200.0},
    {80.0, 0.9, 140.0, 2.0, 2600.0, 4.0, 4200.0, -4.0, 6000.0},
    {70.0, 1.2, 100.0, 3.0, 2000.0, 2.5, 3400.0, -4.0, 5000.0},
    {65.0, 1.2, 95.0, 3.5, 1800.0, 2.0, 3200.0, -5.0, 4600.0},
    {60.0, 1.3, 90.0, 4.0, 2500.0, 3.0, 3600.0, -3.0, 5400.0},
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return Preamp::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i == GateSpeed) return p(Gate) > -59.95f;
        if (i == CabTone || i == Room) return p(CabOff) < 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        pre_.prepare(fs);
        room_.prepare(fs);
        lim_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        pre_.clear();
        room_.clear();
        lim_.clear();
        dark_.clear();
        bright_.clear();
        gate_.clear();
        for (auto& ch : in_) for (auto& b : ch) b.clear();
        for (auto& ch : post_) for (auto& b : ch) b.clear();
        key_.fill(-1e9f);
        design();
        dc_[0] = dc_[1] = 0.0f;
        inG_.prepare(fs_, 20, hc::fx::dbToGain(p(Input)));
        outG_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        vol_.prepare(fs_, 30, volume());
        mix_.prepare(fs_, 30, p(CabTone) * 0.01f);
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
    float volume() const noexcept { return hc::fx::dbToGain(knobDb(p(Volume), 3.6f)); }

    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        design();
        const AmpModel& a = kAmps[amp_];
        const int ch = std::clamp(static_cast<int>(std::lround(p(Channel))), 0, 2);
        const Voice v{a.inHp, a.preHz, a.preDb, a.bias, kStagesFor[ch]};
        const float chLift = ch == Clean ? 0.0f : ch == Drive ? 6.0f : 12.0f;
        const float g = hc::fx::dbToGain(a.gainLo + chLift + (a.gainHi - a.gainLo) * p(Gain) * 0.1f);
        const float makeup = v.stages == 1 ? 1.0f / std::sqrt(std::max(g, 1.0f)) : 0.35f;
        // the gate: GATE SPEED 0 = slow (300 ms let-go), 10 = fast (20 ms)
        const bool gateOn = p(Gate) > -59.95f;
        const float relMs = 300.0f * std::pow(20.0f / 300.0f, p(GateSpeed) * 0.1f);
        const float gEnv = coefMs(fs_, 8.0), gAtt = coefMs(fs_, 0.5), gRel = coefMs(fs_, relMs);
        const auto holdN = static_cast<std::uint32_t>(0.02 * fs_);
        const float wIn = hc::fx::dbToGain(p(Input));
        float pi = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = inG_.next(wIn);
            L[i] *= gi;
            R[i] *= gi;
            pi = std::max({pi, std::fabs(L[i]), std::fabs(R[i])});
            const float gg = hc::fx::dbToGain(gate_.tick(std::max(std::fabs(L[i]), std::fabs(R[i])), gateOn, p(Gate), -80.0f, holdN, gEnv, gAtt, gRel));
            for (int c = 0; c < 2; ++c) {
                float x = (c == 0 ? L[i] : R[i]) * gg;
                for (auto& b : in_[static_cast<std::size_t>(c)]) x = b.tick(x);
                (c == 0 ? L[i] : R[i]) = x;
            }
        }
        pkIn_ = std::max(pkIn_, pi);
        pre_.run(L, R, n, v, g, makeup);
        const bool cabOn = p(CabOff) < 0.5f;
        const float wVol = volume(), wMix = p(CabTone) * 0.01f, wRoom = p(Room) * 0.01f, wOut = hc::fx::dbToGain(p(Output));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float vg = vol_.next(wVol), mx = mix_.next(wMix), rm = roomAmt_.next(wRoom), og = outG_.next(wOut);
            for (int c = 0; c < 2; ++c) {
                float x = c == 0 ? L[i] : R[i];
                dc_[c] += (x - dc_[c]) * 0.0005f;
                x -= dc_[c];
                for (auto& b : post_[static_cast<std::size_t>(c)]) x = b.tick(x);
                x *= vg;
                if (cabOn) {
                    const float d = dark_.tick(c, x), b = bright_.tick(c, x);
                    x = d + mx * (b - d);
                    if (rm > 0.0f) x += rm * room_.tick(c, x);
                }
                (c == 0 ? L[i] : R[i]) = x * og;
            }
        }
        if (lim_.run(L, R, n)) clips_ += 1.0f;
        float po = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) po = std::max({po, std::fabs(L[i]), std::fabs(R[i])});
        pkOut_ = std::max(pkOut_, po);
    }

    void design() noexcept {
        const std::array<float, 8> k{p(Amp), p(Focus), p(Bass), p(Middle), p(Treble), p(Depth), p(Presence), p(Cabinet)};
        if (k == key_) return;
        key_ = k;
        amp_ = std::clamp(static_cast<int>(std::lround(p(Amp))), 0, 4);
        const AmpModel& a = kAmps[amp_];
        const double top = fs_ * 0.45;
        // the input: the amp's low cut, its pre-emphasis, and FOCUS (a lift that sweeps 250 Hz .. 4 kHz)
        const C inHp = C::make(T::HighPass, fs_, a.inHp, 0.7071);
        const C pre = C::make(T::Bell, fs_, a.preHz, 0.7, a.preDb);
        const float f = p(Focus);
        const C focus = f > 0.0f ? C::make(T::Bell, fs_, 250.0 * std::pow(16.0, (f - 0.0) / 10.0), 1.2, 3.0 + 0.3 * f) : C{};
        for (auto& ch : in_) { ch[0].c = inHp; ch[1].c = pre; ch[2].c = focus; }
        // the tone stack, then the power amp's DEPTH (low resonance) and PRESENCE
        const C st[5] = {C::make(T::LowShelf, fs_, 120.0, 0.7071, knobDb(p(Bass))), C::make(T::Bell, fs_, a.midHz, 0.7, knobDb(p(Middle))),
                         C::make(T::HighShelf, fs_, std::min(a.trebleHz, top), 0.7071, knobDb(p(Treble))),
                         C::make(T::Bell, fs_, 90.0, 1.0, p(Depth)), C::make(T::HighShelf, fs_, std::min(4500.0, top), 0.7071, p(Presence))};
        for (auto& ch : post_) for (int i = 0; i < 5; ++i) ch[static_cast<std::size_t>(i)].c = st[i];
        const CabSpec& cs = kPairs[std::clamp(static_cast<int>(std::lround(p(Cabinet))), 0, 5)];
        dark_.set(cabinet(cs, RibbonOff, fs_));
        bright_.set(cabinet(cs, DynamicOn, fs_));
    }

    double fs_ = 48000.0;
    Preamp pre_;
    EarlyRoom room_;
    OutLimit lim_;
    CabPath dark_, bright_;
    GateStage gate_;
    std::array<std::array<hc::fx::Biquad, 3>, 2> in_{};
    std::array<std::array<hc::fx::Biquad, 5>, 2> post_{};
    std::array<float, 8> key_{};
    int amp_ = 0;
    float dc_[2]{}, push_ = 0.0f, pkIn_ = 0.0f, pkOut_ = 0.0f, clips_ = 0.0f;
    maxim::PeakHold holdIn_, holdOut_;
    hc::fx::Smoothed inG_, outG_, vol_, mix_, roomAmt_;
};

} // namespace
} // namespace hotwire

std::unique_ptr<hc::fx::Effect> makeHotWire() { return std::make_unique<hotwire::Impl>(); }

} // namespace hem::rack
