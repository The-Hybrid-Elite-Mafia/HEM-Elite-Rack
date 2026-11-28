// =============================================================================
//  01 DYNAMIC EQ 8 — eight-band EQ where any band can move by itself when its range of the
//  spectrum gets too loud (Down) or too quiet (Up). Simple sibling of BANDMOTION (16):
//  fixed eight slots, one Up/Down mode per band, no sidechain, no mid/side.
//  Dead flat on load: every gain 0 dB, every band's dynamics off, output 0 dB.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

constexpr int kBands = 8, kPer = 11;
enum BandParam { kFreq, kGain, kQ, kOn, kType, kDynOn, kThr, kRange, kAtt, kRel, kMode };
constexpr int kSelect = kBands * kPer, kOutput = kSelect + 1, kBypass = kSelect + 2, kCount = kSelect + 3;

const char* const kTypes[] = {"BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT"};
const char* const kModes[] = {"DOWN", "UP"};
const char* const kBandNames[] = {"BAND 1", "BAND 2", "BAND 3", "BAND 4", "BAND 5", "BAND 6", "BAND 7", "BAND 8"};
constexpr std::array<float, kBands> kDefHz{60, 150, 350, 800, 1600, 3500, 7000, 12000};   // ASSUMED spread

// ASSUMED (panel has no ratio): once over the threshold the band moves 2 dB for every 3 dB over, up to RANGE.
constexpr float kSlope = 2.0f / 3.0f;

#define HEM_B(n, idp, namep) \
    {"b" #n "_freq", namep " FREQ", "Hz", 20.0f, 20000.0f, kDefHz[n - 1], Taper::Log}, \
    {"b" #n "_gain", namep " GAIN", "dB", -18.0f, 18.0f, 0.0f}, \
    {"b" #n "_q", namep " Q", "", 0.3f, 10.0f, 1.0f, Taper::Log}, \
    {"b" #n "_on", namep " ON", "", 0.0f, 1.0f, 1.0f, Taper::Toggle}, \
    {"b" #n "_type", namep " TYPE", "", 0.0f, 4.0f, 0.0f, Taper::Choice, kTypes}, \
    {"b" #n "_dyn", namep " DYNAMIC", "", 0.0f, 1.0f, 0.0f, Taper::Toggle}, \
    {"b" #n "_thr", namep " THRESHOLD", "dB", -60.0f, 0.0f, 0.0f}, \
    {"b" #n "_range", namep " MAX RANGE", "dB", -18.0f, 18.0f, 0.0f}, \
    {"b" #n "_att", namep " ATTACK", "ms", 0.5f, 500.0f, 10.0f, Taper::Log}, \
    {"b" #n "_rel", namep " RELEASE", "ms", 5.0f, 5000.0f, 120.0f, Taper::Log}, \
    {"b" #n "_mode", namep " MODE", "", 0.0f, 1.0f, 0.0f, Taper::Choice, kModes}

const ParamInfo kParams[kCount] = {
    HEM_B(1, 1, "BAND 1"), HEM_B(2, 2, "BAND 2"), HEM_B(3, 3, "BAND 3"), HEM_B(4, 4, "BAND 4"),
    HEM_B(5, 5, "BAND 5"), HEM_B(6, 6, "BAND 6"), HEM_B(7, 7, "BAND 7"), HEM_B(8, 8, "BAND 8"),
    {"select", "SELECTED BAND", "", 0.0f, 7.0f, 0.0f, Taper::Choice, kBandNames},
    {"output", "OUTPUT", "dB", -18.0f, 18.0f, 0.0f},
    {"bypass", "BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_B

const hc::fx::MeterInfo kMeters[] = {
    {"INPUT", "dB", -60, 0}, {"OUTPUT", "dB", -60, 0},
    {"BAND 1 MOVE", "dB", -18, 18, true}, {"BAND 2 MOVE", "dB", -18, 18, true}, {"BAND 3 MOVE", "dB", -18, 18, true},
    {"BAND 4 MOVE", "dB", -18, 18, true}, {"BAND 5 MOVE", "dB", -18, 18, true}, {"BAND 6 MOVE", "dB", -18, 18, true},
    {"BAND 7 MOVE", "dB", -18, 18, true}, {"BAND 8 MOVE", "dB", -18, 18, true},
};

class DynamicEq8 final : public hc::fx::Effect {
public:
    DynamicEq8() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }

    bool paramActive(int i) const noexcept override {
        if (i >= kSelect) return true;
        const int b = i / kPer, k = i % kPer;
        const int type = static_cast<int>(p(b * kPer + kType));
        const bool cut = type >= 3;
        if (k == kGain) return !cut;
        if (k >= kDynOn) return !cut && (k == kDynOn || p(b * kPer + kDynOn) > 0.5f);
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs; outGain_.prepare(fs, 20.0, kit::dbToGain(p(kOutput))); inM_.prepare(fs); outM_.prepare(fs);
        for (int b = 0; b < kBands; ++b) bands_[static_cast<std::size_t>(b)].gain.prepare(fs, 32, 20.0, p(b * kPer + kGain));
        reset();
    }
    void reset() noexcept override {
        for (auto& b : bands_) { b.eq.clear(); b.det.clear(); b.env.env = 0; b.dynDb = 0; b.lastKey = {}; }
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        inM_.feed(L, n);
        if (p(kBypass) > 0.5f) { outM_.feed(L, n); publish(); return; }
        constexpr std::uint32_t kSub = 32;
        for (std::uint32_t at = 0; at < n; at += kSub) {
            const std::uint32_t m = std::min(kSub, n - at);
            float* l = L + at; float* r = R + at;
            for (int b = 0; b < kBands; ++b) runBand(b, l, r, m);
            for (std::uint32_t i = 0; i < m; ++i) {
                const float g = outGain_.next(kit::dbToGain(p(kOutput)));
                l[i] = kit::sane(l[i] * g); r[i] = kit::sane(r[i] * g);
            }
        }
        outM_.feed(L, n);
        publish();
    }

private:
    struct Key { int type = -1; float hz = -1, q = -1, db = 1e9f; bool operator==(const Key&) const = default; };
    struct Band {
        hc::fx::Biquad2 eq;
        kit::Svf det;
        hc::fx::Envelope env;
        float dynDb = 0;
        kit::BlockGlide gain;
        Key lastKey{};
        bool active = false;
    };

    void runBand(int b, float* l, float* r, std::uint32_t m) noexcept {
        Band& bd = bands_[static_cast<std::size_t>(b)];
        const int o = b * kPer;
        const bool on = p(o + kOn) > 0.5f;
        const int type = static_cast<int>(p(o + kType));
        const float hz = p(o + kFreq), q = p(o + kQ), gain = bd.gain.step(p(o + kGain));   // glides: no click on a jump
        const bool cut = type >= 3;
        const bool dyn = on && !cut && p(o + kDynOn) > 0.5f && !kit::isFlat(p(o + kRange));

        // detector: always tracks so a switch-on starts from the right level
        bd.det.set(fs_, hz, std::max(0.5f, q));
        bd.env.set(fs_, p(o + kAtt), p(o + kRel));
        for (std::uint32_t i = 0; i < m; ++i) {
            const float mono = 0.5f * (l[i] + r[i]);
            float s = 0.0f;
            if (type == 1) s = bd.det.low(mono);
            else if (type == 2) s = bd.det.high(mono);
            else s = bd.det.band(mono);
            (void)bd.env.tick(std::fabs(s));
        }
        float target = 0.0f;
        if (dyn) {
            const float lvl = kit::gainToDb(bd.env.env * 1.4142f);   // peak-ish of a sine
            const float thr = p(o + kThr), range = p(o + kRange);
            const bool up = p(o + kMode) > 0.5f;
            const float over = up ? (thr - lvl) : (lvl - thr);
            if (over > 0.0f) target = std::copysign(std::min(std::fabs(range), over * kSlope), range);
        }
        bd.dynDb = target;   // the envelope already carries attack / release

        if (!on) { if (bd.active) { bd.eq.clear(); bd.active = false; bd.lastKey = {}; } return; }
        if (!bd.active && !cut && kit::isFlat(gain) && kit::isFlat(bd.dynDb)) return;   // never used yet: skip (once used it keeps running; exact at 0 dB)
        const Key key{type, hz, q, gain + bd.dynDb};
        if (!(key == bd.lastKey)) {
            BT t = BT::Bell; double qq = q;
            switch (type) {
            case 1: t = BT::LowShelf; qq = std::min<double>(q, 2.0); break;
            case 2: t = BT::HighShelf; qq = std::min<double>(q, 2.0); break;
            case 3: t = BT::HighPass; break;
            case 4: t = BT::LowPass; break;
            default: break;
            }
            bd.eq.set(hc::fx::BiquadCoefs::make(t, fs_, hz, qq, gain + bd.dynDb));
            bd.lastKey = key;
        }
        bd.active = true;
        bd.eq.run(l, r, m);
    }

    void publish() noexcept {
        publishMeter(0, inM_.db());
        publishMeter(1, outM_.db());
        float most = 0.0f;
        for (int b = 0; b < kBands; ++b) {
            publishMeter(2 + b, bands_[static_cast<std::size_t>(b)].dynDb);
            most = std::min(most, bands_[static_cast<std::size_t>(b)].dynDb);
        }
        publishReduction(most);
    }

    double fs_ = 48000;
    std::array<Band, kBands> bands_{};
    hc::fx::Smoothed outGain_;
    kit::PeakMeter inM_, outM_;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makeDynamicEq8() { return std::make_unique<DynamicEq8>(); }

} // namespace hem::staging
