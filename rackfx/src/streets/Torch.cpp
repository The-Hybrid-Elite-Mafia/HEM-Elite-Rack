// TORCH — guitar amp with two cabinet paths (see Streets.hpp).
//
// In order: INPUT; the amp (its voicing, the preamp and power stages 4x oversampled with DRIVE setting the
// gain, the tone stack BASS / MID / TREBLE and PRESENCE, MASTER), or nothing when AMP is off; then the two
// cabinet paths A and B (each a cabinet and its mic, its VOLUME and PAN; B can be flipped), added together,
// OUTPUT. ROUTING STEREO sends the left side through A and the right through B instead of both into both.
#include "hem/rack/Streets.hpp"

#include "AmpKit.hpp"

namespace hem::rack {
namespace torch {
namespace {
using namespace amps;

const char* const kTypeN[] = {"DIRECT", "CLEAN", "SPARKLE", "EDGE", "DRIVE", "CRUNCH", "BRIT", "HOT", "LEAD", "MODERN"};
const char* const kCabN[] = {"NONE", "1X12 OPEN", "2X12 OPEN", "4X10 OPEN", "2X12 CLOSED", "4X12 VINTAGE", "4X12 MODERN", "1X15", "8X10"};
const char* const kMicN[] = {"DYNAMIC ON", "DYNAMIC OFF", "CONDENSER ON", "CONDENSER OFF", "RIBBON ON", "RIBBON OFF"};
const char* const kRouteN[] = {"DUAL", "STEREO"};
const ParamInfo kParams[kCount] = {
    {"amp", "Amp", "", 0, 1, 1, Taper::Toggle, kOnOff},
    {"type", "Type", "", 0, kAmps - 1, Clean, Taper::Choice, kTypeN},
    {"drive", "Drive", "", 0.0f, 10.0f, 5.0f},
    {"bass", "Bass", "", 0.0f, 10.0f, 5.0f},
    {"mid", "Mid", "", 0.0f, 10.0f, 5.0f},
    {"treble", "Treble", "", 0.0f, 10.0f, 5.0f},
    {"presence", "Presence", "", 0.0f, 10.0f, 5.0f},
    {"master", "Master", "", 0.0f, 10.0f, 5.0f},
    {"input", "Input", "dB", -12.0f, 12.0f, 0.0f},
    {"output", "Output", "dB", -12.0f, 12.0f, 0.0f},
    {"caba", "Cabinet A", "", 0, kCabs - 1, 2, Taper::Choice, kCabN},
    {"mica", "Mic A", "", 0, kMics - 1, DynamicOn, Taper::Choice, kMicN},
    {"vola", "Volume A", "", 0.0f, 10.0f, 5.0f},
    {"pana", "Pan A", "", -100.0f, 100.0f, 0.0f},
    {"cabb", "Cabinet B", "", 0, kCabs - 1, 5, Taper::Choice, kCabN},
    {"micb", "Mic B", "", 0, kMics - 1, RibbonOn, Taper::Choice, kMicN},
    {"volb", "Volume B", "", 0.0f, 10.0f, 0.0f},
    {"panb", "Pan B", "", -100.0f, 100.0f, 0.0f},
    {"phaseb", "Phase B", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"routing", "Routing", "", 0, 1, 0, Taper::Choice, kRouteN},
};
// The amps: voicing, gain range across DRIVE 0..10 (dB), where MID sits, where TREBLE and PRESENCE work.
struct AmpModel { Voice v; float gainLo, gainHi; double midHz, trebleHz, presHz; };
constexpr AmpModel kModels[kAmps] = {
    {{20.0, 1000.0, 0.0, 0.0f, 1}, -6.0f, 6.0f, 700.0, 3000.0, 5000.0},       // DIRECT: almost a clean line
    {{60.0, 800.0, 0.0, 0.05f, 1}, 0.0f, 18.0f, 600.0, 2800.0, 4500.0},      // CLEAN
    {{80.0, 2500.0, 3.0, 0.04f, 1}, 0.0f, 20.0f, 700.0, 3500.0, 5500.0},     // SPARKLE: a bright clean
    {{90.0, 1200.0, 2.0, 0.10f, 2}, 4.0f, 28.0f, 650.0, 3000.0, 4500.0},     // EDGE
    {{90.0, 900.0, 3.0, 0.12f, 2}, 8.0f, 32.0f, 650.0, 2800.0, 4500.0},      // DRIVE
    {{100.0, 900.0, 4.0, 0.12f, 2}, 10.0f, 36.0f, 700.0, 2800.0, 4500.0},    // CRUNCH
    {{100.0, 1000.0, 5.0, 0.14f, 3}, 10.0f, 38.0f, 800.0, 3000.0, 5000.0},   // BRIT
    {{110.0, 800.0, 6.0, 0.15f, 3}, 14.0f, 44.0f, 800.0, 3000.0, 4500.0},    // HOT
    {{110.0, 900.0, 6.0, 0.15f, 3}, 16.0f, 48.0f, 900.0, 3200.0, 5000.0},    // LEAD
    {{120.0, 700.0, 4.0, 0.10f, 3}, 18.0f, 50.0f, 500.0, 3500.0, 5500.0},    // MODERN: tight lows, scooped
};
constexpr CabSpec kCabSpecs[kCabs - 1] = {
    {75.0, 1.0, 120.0, 2.0, 2400.0, 3.0, 4000.0, -5.0, 5500.0},     // 1X12 OPEN
    {70.0, 1.0, 110.0, 2.5, 2200.0, 3.0, 3800.0, -4.0, 5200.0},     // 2X12 OPEN
    {80.0, 0.9, 140.0, 2.0, 2600.0, 4.0, 4200.0, -4.0, 6000.0},     // 4X10 OPEN
    {70.0, 1.2, 100.0, 3.0, 2000.0, 2.5, 3400.0, -4.0, 5000.0},     // 2X12 CLOSED
    {65.0, 1.2, 95.0, 3.5, 1800.0, 2.0, 3200.0, -5.0, 4600.0},      // 4X12 VINTAGE
    {60.0, 1.3, 90.0, 4.0, 2500.0, 3.0, 3600.0, -3.0, 5400.0},      // 4X12 MODERN
    {45.0, 0.9, 80.0, 3.0, 1500.0, 2.0, 2800.0, -4.0, 4000.0},      // 1X15
    {40.0, 0.9, 70.0, 3.0, 800.0, -3.0, 2200.0, -3.0, 3500.0},      // 8X10
};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return Preamp::latency(); }
    bool paramActive(int i) const noexcept override {
        if (i >= Type && i <= Master) return p(AmpOn) >= 0.5f;
        if (i == MicA) return p(CabA) >= 0.5f;
        if (i == MicB) return p(CabB) >= 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        pre_.prepare(fs);
        clean_.prepare(Preamp::latency());
        fade_ = static_cast<float>(1.0 / (0.02 * fs));
        reset();
    }
    void reset() noexcept override {
        pre_.clear();
        clean_.clear();
        for (auto& ch : in_) for (auto& b : ch) b.clear();
        for (auto& ch : stack_) for (auto& b : ch) b.clear();
        a_.clear();
        b_.clear();
        key_.fill(-1e9f);
        design();
        dc_[0] = dc_[1] = 0.0f;
        ampMix_ = p(AmpOn) >= 0.5f ? 1.0f : 0.0f;
        inG_.prepare(fs_, 20, hc::fx::dbToGain(p(Input)));
        outG_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        master_.prepare(fs_, 30, hc::fx::dbToGain(knobDb(p(Master), 3.6f)));
        volA_.prepare(fs_, 30, knobVol(p(VolA)));
        volB_.prepare(fs_, 30, knobVol(p(VolB)) * (p(PhaseB) >= 0.5f ? -1.0f : 1.0f));
        panA_.prepare(fs_, 30, p(PanA) * 0.01f);
        panB_.prepare(fs_, 30, p(PanB) * 0.01f);
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    static constexpr std::uint32_t kChunk = 64;

    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        design();
        const AmpModel& m = kModels[type_];
        const bool ampOn = p(AmpOn) >= 0.5f;
        const float g = hc::fx::dbToGain(m.gainLo + (m.gainHi - m.gainLo) * p(Drive) * 0.1f);
        const float makeup = m.v.stages == 1 ? 1.0f / std::sqrt(std::max(g, 1.0f)) : 0.35f;
        const float wIn = hc::fx::dbToGain(p(Input)), wOut = hc::fx::dbToGain(p(Output)), wMaster = hc::fx::dbToGain(knobDb(p(Master), 3.6f));
        const float wA = knobVol(p(VolA)), wB = knobVol(p(VolB)) * (p(PhaseB) >= 0.5f ? -1.0f : 1.0f), wPa = p(PanA) * 0.01f, wPb = p(PanB) * 0.01f;
        const bool stereo = p(Routing) >= 0.5f;
        float dry[2][kChunk], amp[2][kChunk];
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gi = inG_.next(wIn);
            for (int c = 0; c < 2; ++c) {
                float x = (c == 0 ? L[i] : R[i]) * gi;
                dry[c][i] = x;
                for (auto& b : in_[static_cast<std::size_t>(c)]) x = b.tick(x);
                amp[c][i] = x;
            }
        }
        pre_.run(amp[0], amp[1], n, m.v, g, makeup);
        for (std::uint32_t i = 0; i < n; ++i) {
            // the amp's tone stack and master (or the plain input, delayed to match, when AMP is off)
            ampMix_ = ampOn ? std::min(1.0f, ampMix_ + fade_) : std::max(0.0f, ampMix_ - fade_);
            const float mg = master_.next(wMaster);
            float x[2];
            for (int c = 0; c < 2; ++c) {
                float v = amp[c][i];
                dc_[c] += (v - dc_[c]) * 0.0005f;
                v -= dc_[c];
                for (auto& b : stack_[static_cast<std::size_t>(c)]) v = b.tick(v);
                const float d = c == 0 ? clean_.l.process(dry[0][i], Preamp::latency()) : clean_.r.process(dry[1][i], Preamp::latency());
                x[c] = d + ampMix_ * (v * mg - d);
            }
            // the two cabinet paths
            const float va = volA_.next(wA), vb = volB_.next(wB), pa = panA_.next(wPa), pb = panB_.next(wPb);
            const float inA[2] = {stereo ? x[0] : x[0], stereo ? x[0] : x[1]};
            const float inB[2] = {stereo ? x[1] : x[0], x[1]};
            float oa[2], ob[2];
            for (int c = 0; c < 2; ++c) {
                oa[c] = cabA_ ? a_.tick(c, inA[c]) : inA[c];
                ob[c] = cabB_ ? b_.tick(c, inB[c]) : inB[c];
            }
            // pan: -100 = left only .. +100 = right only (constant level in the middle)
            const float aL = va * std::min(1.0f, 1.0f - pa), aR = va * std::min(1.0f, 1.0f + pa);
            const float bL = vb * std::min(1.0f, 1.0f - pb), bR = vb * std::min(1.0f, 1.0f + pb);
            const float o = outG_.next(wOut);
            L[i] = (aL * oa[0] + bL * ob[0]) * o;
            R[i] = (aR * oa[1] + bR * ob[1]) * o;
        }
    }

    void design() noexcept {
        const std::array<float, 9> k{p(Type), p(Bass), p(Mid), p(Treble), p(Presence), p(CabA), p(MicA), p(CabB), p(MicB)};
        if (k == key_) return;
        key_ = k;
        type_ = std::clamp(static_cast<int>(std::lround(p(Type))), 0, kAmps - 1);
        const AmpModel& m = kModels[type_];
        const double top = fs_ * 0.45;
        const C inHp = C::make(T::HighPass, fs_, m.v.inHp, 0.7071);
        const C pre = m.v.preDb != 0.0 ? C::make(T::Bell, fs_, m.v.preHz, 0.7, m.v.preDb) : C{};
        for (auto& ch : in_) { ch[0].c = inHp; ch[1].c = pre; }
        const C st[4] = {C::make(T::LowShelf, fs_, 120.0, 0.7071, knobDb(p(Bass))), C::make(T::Bell, fs_, m.midHz, 0.7, knobDb(p(Mid))),
                         C::make(T::HighShelf, fs_, std::min(m.trebleHz, top), 0.7071, knobDb(p(Treble))),
                         C::make(T::HighShelf, fs_, std::min(m.presHz, top), 0.7071, knobDb(p(Presence), 1.6f))};
        for (auto& ch : stack_) for (int i = 0; i < 4; ++i) ch[static_cast<std::size_t>(i)].c = st[i];
        const int ca = std::clamp(static_cast<int>(std::lround(p(CabA))), 0, kCabs - 1), cb = std::clamp(static_cast<int>(std::lround(p(CabB))), 0, kCabs - 1);
        const int ma = std::clamp(static_cast<int>(std::lround(p(MicA))), 0, kMics - 1), mb = std::clamp(static_cast<int>(std::lround(p(MicB))), 0, kMics - 1);
        cabA_ = ca > 0;
        cabB_ = cb > 0;
        if (cabA_) a_.set(cabinet(kCabSpecs[ca - 1], ma, fs_));
        if (cabB_) b_.set(cabinet(kCabSpecs[cb - 1], mb, fs_));
    }

    double fs_ = 48000.0;
    Preamp pre_;
    Delay2 clean_;
    std::array<std::array<hc::fx::Biquad, 2>, 2> in_{};
    std::array<std::array<hc::fx::Biquad, 4>, 2> stack_{};
    CabPath a_, b_;
    bool cabA_ = true, cabB_ = true;
    int type_ = Clean;
    std::array<float, 9> key_{};
    float dc_[2]{}, ampMix_ = 1.0f, fade_ = 0.001f;
    hc::fx::Smoothed inG_, outG_, master_, volA_, volB_, panA_, panB_;
};

} // namespace
} // namespace torch

std::unique_ptr<hc::fx::Effect> makeTorch() { return std::make_unique<torch::Impl>(); }

} // namespace hem::rack
