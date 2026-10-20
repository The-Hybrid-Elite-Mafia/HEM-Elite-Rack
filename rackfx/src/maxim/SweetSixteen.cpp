// SWEET SIXTEEN — sixteen-band maximizer with six EQ / priority bands (see Maximizers.hpp).
//
// The sound runs through the six-band EQ (GAIN; ordinary filters, no added delay), is split into
// sixteen bands (linear phase, crossovers fixed from 45 Hz to 14 kHz; SEPARATION picks how sharp:
// LOW 1023, MEDIUM 2047, HIGH 4095 taps — all three are built in prepare and switched with a
// one-block crossfade), limited with the reduction shared over the sixteen bands by their energy and
// by PRIORITY (the six bands' curve, read at each of the sixteen), then caught at the ceiling.
// The RELEASE CHARACTER sets how long each of the sixteen bands takes to let go and how
// program-dependent that is; RELEASE scales them all.
#include "hem/rack/Maximizers.hpp"

#include "LimitKit.hpp"

namespace hem::rack {
namespace sixteen {
namespace {
using namespace maxim;

const char* const kCharN[kCharacters] = {"DEFAULT", "WARM TAPE", "ORCHESTRA", "ALL PUNCH", "KICK SNARE", "VOCAL UP 1", "VOCAL UP 2", "BASS UP 1",
                                         "BASS UP 2", "HARSH DOWN 1", "HARSH DOWN 2", "MUD DOWN", "SUB DOWN", "STANDARD", "OLD SCHOOL",
                                         "HALF SPLIT", "SINGLE AUTO", "SINGLE FIXED", "VELVET", "HEAVY", "STRIPPED", "QUIET LIFT"};
// a release character: the lowest band's and the highest band's release (the ones between on a log
// curve), how program-dependent, and one region whose releases are scaled (shorter = that region
// comes back up quicker and sounds more forward; longer = it stays down longer and sits back)
struct CharDef {
    float lowMs, highMs;
    int behave;
    float regionLo, regionHi, regionMul;
};
const CharDef kChar[kCharacters] = {
    {600, 150, Auto, 0, 0, 1},          {900, 300, AutoLows, 0, 0, 1},   {1200, 500, Auto, 0, 0, 1},       {300, 80, AutoHighs, 0, 0, 1},
    {250, 100, Hard, 40, 300, 0.5f},    {600, 150, Auto, 250, 4000, 0.6f}, {600, 150, Auto, 250, 4000, 0.35f}, {600, 150, Auto, 40, 250, 0.6f},
    {600, 150, Auto, 40, 250, 0.35f},   {600, 150, Auto, 2000, 6000, 1.8f}, {600, 150, Auto, 2000, 6000, 3.0f}, {600, 150, Auto, 200, 600, 2.0f},
    {600, 150, Auto, 20, 80, 2.5f},     {598, 200, Auto, 0, 0, 1},       {950, 320, AutoLows, 0, 0, 1},    {598, 200, Auto, 0, 0, 1},
    {300, 300, Auto, 0, 0, 1},          {300, 300, Manual, 0, 0, 1},     {840, 280, AutoLows, 0, 0, 1},    {360, 120, Hard, 0, 0, 1},
    {300, 100, AutoHighs, 0, 0, 1},     {1200, 400, Auto, 0, 0, 1},
};
const char* const kSepN[] = {"LOW", "MEDIUM", "HIGH"};
constexpr std::uint32_t kSepTaps[] = {1023, 2047, 4095};
const char* const kTypeN[] = {"BELL", "LOW SHELF", "HIGH SHELF"};

#define HEM_BAND(n, hz, type)                                                                             \
    {"gain" #n, "Band " #n " Gain", "dB", -18.0f, 18.0f, 0.0f},                                           \
    {"priority" #n, "Band " #n " Priority", "dB", -18.0f, 18.0f, 0.0f},                                   \
    {"link" #n, "Band " #n " Link", "", 0, 1, 0, Taper::Toggle, kOnOff},                                  \
    {"freq" #n, "Band " #n " Freq", "Hz", 16.0f, 20000.0f, hz, Taper::Log},                               \
    {"q" #n, "Band " #n " Q", "", 0.5f, 2.0f, 1.0f},                                                      \
    {"type" #n, "Band " #n " Type", "", 0, 2, type, Taper::Choice, kTypeN},                               \
    {"in" #n, "Band " #n " In", "", 0, 1, 1, Taper::Toggle, kOnOff}

const ParamInfo kParams[kCount] = {
    {"threshold", "Threshold", "dB", -30.0f, 0.0f, 0.0f},
    {"ceiling", "Ceiling", "dB", -30.0f, 0.0f, 0.0f},
    {"link", "Link", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"release", "Release", "x", 0.1f, 10.0f, 1.0f, Taper::Log},
    {"character", "Release Character", "", 0, kCharacters - 1, 0, Taper::Choice, kCharN},
    {"separation", "Separation", "", 0, 2, 1, Taper::Choice, kSepN},
    {"quantize", "Quantize", "", 0, 4, 0, Taper::Choice, kQuantN},
    {"dither", "Dither", "", 0, 2, 0, Taper::Choice, kDitherN},
    {"shaping", "Shaping", "", 0, 3, 1, Taper::Choice, kShapeN},
    HEM_BAND(1, 100.0f, 1), HEM_BAND(2, 200.0f, 0), HEM_BAND(3, 400.0f, 0), HEM_BAND(4, 800.0f, 0), HEM_BAND(5, 1600.0f, 0), HEM_BAND(6, 3200.0f, 2),
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {{"BAND 1", "dB", -30.0f, 0.0f}, {"BAND 2", "dB", -30.0f, 0.0f}, {"BAND 3", "dB", -30.0f, 0.0f},
                                     {"BAND 4", "dB", -30.0f, 0.0f}, {"BAND 5", "dB", -30.0f, 0.0f}, {"BAND 6", "dB", -30.0f, 0.0f}};
constexpr std::uint32_t kLook = 72;
constexpr T kTypes[] = {T::Bell, T::LowShelf, T::HighShelf};

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return kLatency; }
    bool paramActive(int i) const noexcept override {
        if (i >= Band0) {
            const int b = (i - Band0) / kPerBand, k = (i - Band0) % kPerBand;
            if (k != In) return p(band(b, In)) >= 0.5f;
        }
        return true;
    }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (int k = 0; k < kBands - 1; ++k) xo_[k] = 45.0f * std::pow(14000.0f / 45.0f, static_cast<float>(k) / 14.0f);
        for (int b = 0; b < kBands; ++b) {
            const float lo = b == 0 ? 20.0f : xo_[b - 1], hi = b == kBands - 1 ? 20000.0f : xo_[b];
            centre_[b] = std::sqrt(lo * hi);
        }
        split_.prepare(fs, kBands - 1, 3);
        for (int s = 0; s < 3; ++s) split_.designSet(s, xo_, kSepTaps[s]);
        shared_.prepare(fs, kLook);
        catch_.prepare(fs);
        idr_.prepare(fs);
        reset();
    }
    void reset() noexcept override {
        sep_ = sepNow();
        split_.clear();
        split_.useSet(sep_);
        shared_.clear();
        catch_.clear();
        idr_.clear();
        for (auto& ch : eq_) for (auto& f : ch) f.clear();
        designed_.fill(-1e9f);
        eqDesign();
        drive_.prepare(fs_, 30, gainOfDb(-p(Threshold)));
        ceil_.prepare(fs_, 30, gainOfDb(ceilingDb()));
        publishReduction(0.0f);
        for (int j = 0; j < kEqBands; ++j) publishMeter(j, 0.0f);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const int sep = sepNow();
        if (sep != sep_ && !split_.fading()) {
            split_.fadeToSet(sep);
            sep_ = sep;
        }
        eqDesign();
        const float cT = gainOfDb(ceilingDb());
        idr_.set(kQuantBits[std::clamp(static_cast<int>(std::lround(p(Quantize))), 0, 4)], static_cast<int>(std::lround(p(Dither))),
                 static_cast<int>(std::lround(p(Shaping))), cT);
        const CharDef& ch = kChar[std::clamp(static_cast<int>(std::lround(p(Character))), 0, kCharacters - 1)];
        Shared<kBands>::Set set;
        set.bands = kBands;
        set.separation = 1.0f;
        set.hard = ch.behave == Hard;
        for (int b = 0; b < kBands; ++b) {
            float ms = ch.lowMs * std::pow(ch.highMs / ch.lowMs, static_cast<float>(b) / static_cast<float>(kBands - 1));
            if (centre_[b] >= ch.regionLo && centre_[b] <= ch.regionHi) ms *= ch.regionMul;
            set.releaseS[b] = ms * p(Release) * 0.001f;
            set.adapt[b] = adaptivity(ch.behave, b, kBands);
            set.prioW[b] = prioW_[b];
        }
        float most = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            float l = L[i], r = R[i];
            for (int j = 0; j < kEqBands; ++j)
                if (eqOn_[j]) { l = eq_[0][j].tick(l); r = eq_[1][j].tick(r); }
            float bl[kBands], br[kBands];
            split_.tick(l, r, bl, br);
            const float d = drive_.next(gainOfDb(-p(Threshold)));
            set.ceiling = ceil_.next(cT);
            for (int b = 0; b < kBands; ++b) { bl[b] *= d; br[b] *= d; }
            most = std::min(most, shared_.tick(set, bl, br));
            float yl = 0, yr = 0;
            for (int b = 0; b < kBands; ++b) { yl += bl[b]; yr += br[b]; }
            catch_.tick(yl, yr, set.ceiling);
            L[i] = idr_.tick(0, std::clamp(yl, -set.ceiling, set.ceiling));
            R[i] = idr_.tick(1, std::clamp(yr, -set.ceiling, set.ceiling));
        }
        float bandDb[kBands];
        for (int b = 0; b < kBands; ++b) bandDb[b] = shared_.takeBandDb(b);
        publishReduction(most);
        for (int j = 0; j < kEqBands; ++j) {   // the reduction of the band each EQ band sits in
            const float f = p(band(j, Freq));
            int b = 0;
            while (b < kBands - 1 && f > xo_[b]) ++b;
            publishMeter(j, bandDb[b]);
        }
    }

private:
    int sepNow() const noexcept { return std::clamp(static_cast<int>(std::lround(p(Separation))), 0, 2); }
    float ceilingDb() const noexcept { return p(Ceiling) + (p(Link) >= 0.5f ? p(Threshold) : 0.0f); }
    // the EQ filters and the priority curve, when a band setting changed
    void eqDesign() noexcept {
        std::array<float, kEqBands * kPerBand> now{};
        for (int i = 0; i < kEqBands * kPerBand; ++i) now[static_cast<std::size_t>(i)] = p(Band0 + i);
        if (now == designed_) return;
        designed_ = now;
        float prioDb[kBands]{};
        for (int j = 0; j < kEqBands; ++j) {
            eqOn_[j] = p(band(j, In)) >= 0.5f;
            const T t = kTypes[std::clamp(static_cast<int>(std::lround(p(band(j, Type)))), 0, 2)];
            const double hz = p(band(j, Freq)), q = p(band(j, Q));
            const C g = C::make(t, fs_, hz, q, p(band(j, Gain)));
            eq_[0][j].c = eq_[1][j].c = g;
            if (p(band(j, In)) < 0.5f) continue;
            const float pr = p(band(j, Priority)) + (p(band(j, BandLink)) >= 0.5f ? p(band(j, Gain)) : 0.0f);
            if (pr == 0.0f) continue;
            const C pc = C::make(t, fs_, hz, q, pr);
            for (int b = 0; b < kBands; ++b) prioDb[b] += static_cast<float>(biquadDb(pc, fs_, std::min<double>(centre_[b], fs_ * 0.45)));
        }
        for (int b = 0; b < kBands; ++b) prioW_[b] = gainOfDb(-prioDb[b]);
    }

    double fs_ = 48000.0;
    float xo_[kBands - 1]{}, centre_[kBands]{}, prioW_[kBands]{};
    LinearSplit split_;
    Shared<kBands> shared_;
    Catch catch_;
    Idr idr_;
    hc::fx::Biquad eq_[2][kEqBands];
    bool eqOn_[kEqBands]{};
    std::array<float, kEqBands * kPerBand> designed_{};
    int sep_ = 1;
    hc::fx::Smoothed drive_, ceil_;
};

} // namespace
} // namespace sixteen

std::unique_ptr<hc::fx::Effect> makeSweetSixteen() { return std::make_unique<sixteen::Impl>(); }

} // namespace hem::rack
