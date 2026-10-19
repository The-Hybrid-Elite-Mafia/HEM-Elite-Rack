#include "hem/rack/EliteEq.hpp"

#include "hybrid/fx/Dsp.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace hem::rack {
namespace eq {

namespace {
const char* const kOnOff[]  = {"OFF", "ON"};
const char* const kTypes[]  = {"BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "NOTCH", "TILT"};
const char* const kSlopeN[] = {"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"};
const char* const kStereoN[] = {"STEREO", "MID", "SIDE", "LEFT", "RIGHT"};
const char* const kCharN[]  = {"CLEAN", "CONSOLE", "TUBE"};

using hc::fx::ParamInfo;
using hc::fx::Taper;

struct Table {
    std::array<std::string, kParamCount> ids, names;
    std::array<ParamInfo, kParamCount>   p{};
    Table() {
        for (int b = 0; b < kBands; ++b) {
            const double defHz = 30.0 * std::pow(16000.0 / 30.0, b / 15.0);
            const std::string pre = "b" + std::to_string(b + 1), nm = "Band " + std::to_string(b + 1) + " ";
            auto add = [&](int c, const char* id, const char* name, const char* unit, float mn, float mx, float def, Taper t = Taper::Linear,
                           const char* const* ch = nullptr) {
                const auto i = static_cast<std::size_t>(bandParam(b, c));
                ids[i] = pre + id;
                names[i] = nm + name;
                p[i] = ParamInfo{ids[i].c_str(), names[i].c_str(), unit, mn, mx, def, t, ch};
            };
            add(On, "on", "On", "", 0, 1, 0, Taper::Toggle, kOnOff);
            add(TypeC, "type", "Type", "", 0, static_cast<float>(static_cast<int>(Type::kCount) - 1), 0, Taper::Choice, kTypes);
            add(Freq, "freq", "Frequency", "Hz", 20.0f, 20000.0f, static_cast<float>(defHz), Taper::Log);
            add(Gain, "gain", "Gain", "dB", -30.0f, 30.0f, 0.0f);
            add(Q, "q", "Q", "", 0.1f, 18.0f, 1.0f, Taper::Log);
            add(Slope, "slope", "Slope", "", 0, 5, 3, Taper::Choice, kSlopeN);
            add(StereoC, "stereo", "Stereo", "", 0, static_cast<float>(static_cast<int>(Stereo::kCount) - 1), 0, Taper::Choice, kStereoN);
            add(Dyn, "dyn", "Dynamic", "", 0, 1, 0, Taper::Toggle, kOnOff);
            add(Threshold, "thr", "Threshold", "dB", -60.0f, 0.0f, -24.0f);
            add(Range, "range", "Range", "dB", -24.0f, 24.0f, -6.0f);
            add(Attack, "att", "Attack", "ms", 0.5f, 200.0f, 10.0f, Taper::Log);
            add(Release, "rel", "Release", "ms", 10.0f, 2000.0f, 150.0f, Taper::Log);
        }
        auto addG = [&](int i, const char* id, const char* name, const char* unit, float mn, float mx, float def, Taper t = Taper::Linear,
                        const char* const* ch = nullptr) {
            const auto u = static_cast<std::size_t>(i);
            ids[u] = id;
            names[u] = name;
            p[u] = ParamInfo{ids[u].c_str(), names[u].c_str(), unit, mn, mx, def, t, ch};
        };
        addG(Output, "output", "Output", "dB", -24.0f, 24.0f, 0.0f);
        addG(AutoGain, "autogain", "Auto Gain", "", 0, 1, 0, Taper::Toggle, kOnOff);
        addG(Character, "character", "Character", "", 0, 2, 0, Taper::Choice, kCharN);
        addG(Drive, "drive", "Drive", "%", 0.0f, 100.0f, 30.0f);
    }
};
const Table& table() {
    static const Table t;
    return t;
}

Section fromRbj(const hc::fx::BiquadCoefs& c) { return {c.b0, c.b1, c.b2, c.a1, c.a2}; }

Section firstOrder(bool highPass, double fs, double hz) {
    const double k = std::tan(std::numbers::pi * std::clamp(hz, 5.0, fs * 0.49) / fs);
    if (highPass) { const double b0 = 1.0 / (1.0 + k); return {b0, -b0, 0, (k - 1.0) / (k + 1.0), 0}; }
    const double b0 = k / (1.0 + k);
    return {b0, b0, 0, (k - 1.0) / (k + 1.0), 0};
}
} // namespace

const char* typeName(Type t) noexcept { return kTypes[std::clamp(static_cast<int>(t), 0, static_cast<int>(Type::kCount) - 1)]; }
bool typeHasGain(Type t) noexcept { return t == Type::Bell || t == Type::LowShelf || t == Type::HighShelf || t == Type::Tilt; }
bool typeHasSlope(Type t) noexcept { return t == Type::LowCut || t == Type::HighCut; }
bool typeCanBeDynamic(Type t) noexcept { return typeHasGain(t); }

Sections design(const Band& b, double fs) noexcept {
    using hc::fx::BiquadCoefs;
    using BT = BiquadCoefs::Type;
    Sections out;
    const double f = std::clamp(b.freq, 10.0, fs * 0.49), q = std::clamp(b.q, 0.05, 40.0);
    switch (b.type) {
    case Type::Bell:      out.s[0] = fromRbj(BiquadCoefs::make(BT::Bell, fs, f, q, b.gainDb)); out.count = 1; break;
    case Type::LowShelf:  out.s[0] = fromRbj(BiquadCoefs::make(BT::LowShelf, fs, f, q, b.gainDb)); out.count = 1; break;
    case Type::HighShelf: out.s[0] = fromRbj(BiquadCoefs::make(BT::HighShelf, fs, f, q, b.gainDb)); out.count = 1; break;
    case Type::Notch:     out.s[0] = fromRbj(BiquadCoefs::make(BT::Notch, fs, f, q)); out.count = 1; break;
    case Type::Tilt: {
        // First-order tilt around f: -gain/2 below, +gain/2 above.
        const double A = std::pow(10.0, b.gainDb / 40.0), t = std::tan(std::numbers::pi * f / fs);
        const double n0 = A + t, n1 = t - A, d0 = 1.0 + A * t, d1 = A * t - 1.0;
        out.s[0] = {n0 / d0, n1 / d0, 0, d1 / d0, 0};
        out.count = 1;
        break;
    }
    case Type::LowCut:
    case Type::HighCut: {
        const bool hp = b.type == Type::LowCut;
        const int n = std::clamp(b.slopeDb / 6, 1, 8);
        int idx = 0;
        if (n % 2 == 1) out.s[static_cast<std::size_t>(idx++)] = firstOrder(hp, fs, f);
        // Butterworth pole pairs; the most resonant pair takes the Q (0.707 = plain Butterworth at 12 dB/oct).
        const int pairs = n / 2;
        double maxQ = 0.0;
        int maxAt = -1;
        std::array<double, 4> qs{};
        for (int k = 1; k <= pairs; ++k) {
            const double ang = n % 2 == 0 ? (2.0 * k - 1.0) * std::numbers::pi / (2.0 * n) : k * std::numbers::pi / n;
            qs[static_cast<std::size_t>(k - 1)] = 1.0 / (2.0 * std::cos(ang));
            if (qs[static_cast<std::size_t>(k - 1)] > maxQ) { maxQ = qs[static_cast<std::size_t>(k - 1)]; maxAt = k - 1; }
        }
        for (int k = 0; k < pairs; ++k) {
            double qk = qs[static_cast<std::size_t>(k)];
            if (k == maxAt) qk *= q * std::numbers::sqrt2;   // = qk * q / 0.7071
            out.s[static_cast<std::size_t>(idx++)] = fromRbj(BiquadCoefs::make(hp ? BT::HighPass : BT::LowPass, fs, f, qk));
        }
        out.count = idx;
        break;
    }
    case Type::kCount: out.count = 0; break;
    }
    return out;
}

double magnitudeDb(const Sections& s, double fs, double hz) noexcept {
    const double w = 2.0 * std::numbers::pi * hz / fs;
    const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
    double db = 0.0;
    for (int i = 0; i < s.count; ++i) {
        const Section& c = s.s[static_cast<std::size_t>(i)];
        const double nr = c.b0 + c.b1 * cr + c.b2 * c2r, ni = c.b1 * ci + c.b2 * c2i;
        const double dr = 1 + c.a1 * cr + c.a2 * c2r, di = c.a1 * ci + c.a2 * c2i;
        db += 10.0 * std::log10(std::max(nr * nr + ni * ni, 1e-30) / std::max(dr * dr + di * di, 1e-30));
    }
    return db;
}

Band bandOf(const hc::fx::Effect& e, int b) noexcept {
    Band out;
    out.on = e.get(bandParam(b, On)) >= 0.5f;
    out.type = static_cast<Type>(std::clamp(static_cast<int>(e.get(bandParam(b, TypeC))), 0, static_cast<int>(Type::kCount) - 1));
    out.freq = e.get(bandParam(b, Freq));
    out.gainDb = e.get(bandParam(b, Gain));
    out.q = e.get(bandParam(b, Q));
    out.slopeDb = kSlopes[static_cast<std::size_t>(std::clamp(static_cast<int>(e.get(bandParam(b, Slope))), 0, 5))];
    out.stereo = static_cast<Stereo>(std::clamp(static_cast<int>(e.get(bandParam(b, StereoC))), 0, static_cast<int>(Stereo::kCount) - 1));
    return out;
}

} // namespace eq

// =============================================================================================
namespace {
using namespace eq;
constexpr std::uint32_t kChunk = 32;
constexpr double kFadeMs = 20.0;

struct State2 { double z1 = 0, z2 = 0; };

inline float runSection(const Section& c, State2& st, float xf) noexcept {
    const double x = xf, y = c.b0 * x + st.z1;
    st.z1 = c.b1 * x - c.a1 * y + st.z2;
    st.z2 = c.b2 * x - c.a2 * y;
    return static_cast<float>(y);
}

inline float softClip(float v) noexcept { return v / std::cbrt(1.0f + std::fabs(v) * v * v); }

struct BandState {
    Sections cur;
    std::array<std::array<State2, 4>, 2> st{};   // [channel or M/S slot][section]
    hc::fx::Biquad sc[2];                        // dynamic detector filters
    float  env = 0.0f;                           // detector peak envelope (linear)
    float  dynDb = 0.0f;                         // gain the dynamic part adds now
    double logF = std::log(1000.0), gain = 0.0, q = 1.0;
    float  mix = 0.0f;                           // 0 = band out of the signal, 1 = in (on / off fades)
    Type   type = Type::Bell;
    int    slope = 24;
    Stereo stereo = Stereo::Stereo;
    bool   fresh = true;                         // first chunk after reset: jump to the targets
    bool   onT = false, dyn = false;             // as read at the last 32-sample boundary
    float  att = 0.0f, rel = 0.0f;
    bool   scValid = false;
    Type   scType = Type::Bell;
    double scF = 0, scQ = 0;
    void clear() noexcept {
        for (auto& ch : st) for (auto& s : ch) s = {};
        sc[0].clear(); sc[1].clear();
        env = 0.0f; dynDb = 0.0f;
    }
};

class EliteEqImpl final : public hc::fx::Effect, public SpectrumSource, public EliteEqView {
public:
    EliteEqImpl() { setDefaults(); }
    std::span<const hc::fx::ParamInfo> params() const noexcept override { return eq::table().p; }
    std::uint32_t latency() const noexcept override { return hc::fx::Oversampler4::latency(); }
    SpectrumMeter& spectrumBefore() noexcept override { return before_; }
    SpectrumMeter& spectrumAfter() noexcept override { return after_; }
    float dynamicGainDb(int b) const noexcept override {
        return b >= 0 && b < kBands ? dynPub_[static_cast<std::size_t>(b)].load(std::memory_order_relaxed) : 0.0f;
    }
    float autoGainDb() const noexcept override { return autoPub_.load(std::memory_order_relaxed); }

    void prepare(double fs, std::uint32_t maxBlock) override {
        fs_ = fs;
        const std::uint32_t mb = std::min(std::max<std::uint32_t>(maxBlock, 1), kMaxBlock);
        for (auto& v : up_) v.assign(static_cast<std::size_t>(mb) * hc::fx::Oversampler4::kFactor, 0.0f);
        for (auto& v : sat_) v.assign(mb, 0.0f);
        for (auto& d : delay_) d.prepare(hc::fx::Oversampler4::latency() + 1);
        kw_[0].set(hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighShelf, fs, 1500.0, 0.7071, 4.0));
        kw_[1].set(hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, fs, 38.0, 0.5));
        kw_[2].set(kw_[0].l.c);
        kw_[3].set(kw_[1].l.c);
        before_.prepare(fs);
        after_.prepare(fs);
        reset();
    }

    void reset() noexcept override {
        for (auto& b : bands_) { b.clear(); b.fresh = true; b.mix = 0.0f; }
        for (auto& o : os_) o.clear();
        for (auto& d : delay_) d.clear();
        for (auto& k : kw_) k.clear();
        dcL_ = dcR_ = 0.0;
        inMs_ = outMs_ = 0.0;
        autoDb_ = 0.0f;
        clock_ = 0;
        agTick_ = 0;
        out_.prepare(fs_, 30, hc::fx::dbToGain(p(Output)));
        charMode_ = static_cast<int>(p(Character));
        charFade_ = charMode_ == 0 ? 0.0f : 1.0f;
        before_.reset();
        after_.reset();
        for (auto& d : dynPub_) d.store(0.0f, std::memory_order_relaxed);
        autoPub_.store(0.0f, std::memory_order_relaxed);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        before_.push(L, R, n);
        // Auto gain measures the input before anything changes it.
        const bool autoOn = p(AutoGain) >= 0.5f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = kw_[1].l.tick(kw_[0].l.tick(L[i])), b = kw_[1].r.tick(kw_[0].r.tick(R[i]));
            inMs_ += (0.5 * (static_cast<double>(a) * a + static_cast<double>(b) * b) - inMs_) * msCoef();
        }
        // Bands update their filters every 32 samples, counted from the start of the stream, so the
        // result is the same whatever block sizes the host uses.
        for (std::uint32_t done = 0; done < n;) {
            const auto m = static_cast<std::uint32_t>(std::min<std::uint64_t>(kChunk - (clock_ % kChunk), n - done));
            if (clock_ % kChunk == 0) for (int b = 0; b < kBands; ++b) updateBand(b);
            for (int b = 0; b < kBands; ++b) runBand(b, L + done, R + done, m);
            done += m;
            clock_ += m;
        }
        character(L, R, n);
        // Loudness after the EQ (before OUTPUT): AUTO GAIN brings it back to the input's.
        const float oT = hc::fx::dbToGain(p(Output));
        for (std::uint32_t i = 0; i < n; ++i) {
            const float a = kw_[3].l.tick(kw_[2].l.tick(L[i])), b = kw_[3].r.tick(kw_[2].r.tick(R[i]));
            outMs_ += (0.5 * (static_cast<double>(a) * a + static_cast<double>(b) * b) - outMs_) * msCoef();
            if ((agTick_++ & 31u) == 0) {
                float target = 0.0f;
                if (autoOn && inMs_ > 1e-8 && outMs_ > 1e-10) target = std::clamp(static_cast<float>(10.0 * std::log10(inMs_ / outMs_)), -24.0f, 24.0f);
                if (!autoOn) target = 0.0f;
                autoDb_ += (target - autoDb_) * agCoef();
            }
            const float g = out_.next(oT) * hc::fx::dbToGain(autoDb_);
            L[i] *= g;
            R[i] *= g;
        }
        autoPub_.store(autoDb_, std::memory_order_relaxed);
        after_.push(L, R, n);
    }

private:
    [[nodiscard]] double msCoef() const noexcept { return 1.0 / (fs_ * 3.0); }        // ~3 s loudness memory
    [[nodiscard]] float agCoef() const noexcept { return static_cast<float>(32.0 / (fs_ * 0.4)); }   // ~0.4 s glide, per 32 samples

    // Reads band bi's settings and redesigns its filter. Runs only at 32-sample boundaries counted
    // from the start of the stream, so the sound never depends on the host's block size.
    void updateBand(int bi) noexcept {
        BandState& s = bands_[static_cast<std::size_t>(bi)];
        s.onT = p(bandParam(bi, On)) >= 0.5f;
        if (!s.onT && s.mix <= 0.0f) {   // off and faded out: nothing to do, forget the state
            if (!s.fresh) { s.clear(); s.fresh = true; }
            s.dyn = false;
            dynPub_[static_cast<std::size_t>(bi)].store(0.0f, std::memory_order_relaxed);
            return;
        }
        const Band tb = eq::bandOf(*this, bi);
        s.dyn = p(bandParam(bi, Dyn)) >= 0.5f && typeCanBeDynamic(tb.type);
        if (s.fresh || tb.type != s.type || tb.slopeDb != s.slope || tb.stereo != s.stereo) {
            if (!s.fresh && (tb.type != s.type || tb.stereo != s.stereo)) s.clear();
            s.type = tb.type; s.slope = tb.slopeDb; s.stereo = tb.stereo;
            if (s.fresh) { s.logF = std::log(tb.freq); s.gain = tb.gainDb; s.q = tb.q; s.fresh = false; }
            s.scValid = false;
        }
        // Parameter glide (~30 ms) so moving a band never zips.
        const double k = 1.0 - std::exp(-static_cast<double>(kChunk) / (fs_ * 0.03));
        s.logF += (std::log(tb.freq) - s.logF) * k;
        s.gain += (tb.gainDb - s.gain) * k;
        s.q += (tb.q - s.q) * k;
        // Dynamic: how far the band's level is over the threshold (3:1), up to RANGE.
        float dynDb = 0.0f;
        if (s.dyn) {
            const float range = p(bandParam(bi, Range)), thr = p(bandParam(bi, Threshold));
            const float envDb = 20.0f * std::log10(std::max(s.env, 1e-9f));
            dynDb = std::clamp((envDb - thr) * (2.0f / 3.0f), 0.0f, std::fabs(range)) * (range < 0.0f ? -1.0f : 1.0f);
            s.att = hc::fx::onePoleCoef(fs_, p(bandParam(bi, Attack)));
            s.rel = hc::fx::onePoleCoef(fs_, p(bandParam(bi, Release)));
        } else {
            s.env = 0.0f;
        }
        s.dynDb = dynDb;
        dynPub_[static_cast<std::size_t>(bi)].store(dynDb, std::memory_order_relaxed);
        Band cur = tb;
        cur.freq = std::exp(s.logF);
        cur.gainDb = s.gain + dynDb;
        cur.q = s.q;
        s.cur = eq::design(cur, fs_);
        // Detector filter for dynamic bands (the part of the spectrum the band works on).
        if (s.dyn) {
            const double f = std::exp(s.logF);
            if (!s.scValid || std::fabs(f - s.scF) > s.scF * 0.002 || std::fabs(s.q - s.scQ) > 0.01 || s.scType != s.type) {
                using BT = hc::fx::BiquadCoefs::Type;
                const BT t = s.type == Type::LowShelf ? BT::LowPass : s.type == Type::HighShelf ? BT::HighPass
                           : s.type == Type::Tilt     ? BT::HighPass : BT::BandPass;
                const double q = s.type == Type::Bell ? s.q : 0.7071;
                const auto c = hc::fx::BiquadCoefs::make(t, fs_, f, q);
                s.sc[0].c = c; s.sc[1].c = c;
                s.scValid = true; s.scF = f; s.scQ = s.q; s.scType = s.type;
            }
        }
    }

    void runBand(int bi, float* L, float* R, std::uint32_t m) noexcept {
        BandState& s = bands_[static_cast<std::size_t>(bi)];
        if (!s.onT && s.mix <= 0.0f) return;
        const bool dyn = s.dyn;
        const float mixTarget = s.onT ? 1.0f : 0.0f;
        const float att = s.att, rel = s.rel;
        const int nSec = s.cur.count;
        auto filt = [&](int slot, float x) {
            for (int j = 0; j < nSec; ++j) x = runSection(s.cur.s[static_cast<std::size_t>(j)], s.st[static_cast<std::size_t>(slot)][static_cast<std::size_t>(j)], x);
            return x;
        };
        auto detect = [&](float a, float b) {
            const float lv = std::max(std::fabs(s.sc[0].tick(a)), std::fabs(s.sc[1].tick(b)));
            s.env = lv + (lv > s.env ? att : rel) * (s.env - lv);
        };
        float mix = s.mix;
        const auto mixStep = static_cast<float>(1.0 / (fs_ * kFadeMs * 0.001));
        for (std::uint32_t i = 0; i < m; ++i) {
            mix = mixTarget > mix ? std::min(mixTarget, mix + mixStep) : std::max(mixTarget, mix - mixStep);
            const float l = L[i], r = R[i];
            float yl = l, yr = r;
            switch (s.stereo) {
            case Stereo::Stereo:
                if (dyn) detect(l, r);
                yl = filt(0, l); yr = filt(1, r);
                break;
            case Stereo::Mid: {
                const float mm = 0.5f * (l + r), ss = 0.5f * (l - r);
                if (dyn) detect(mm, mm);
                const float y = filt(0, mm);
                yl = y + ss; yr = y - ss;
                break;
            }
            case Stereo::Side: {
                const float mm = 0.5f * (l + r), ss = 0.5f * (l - r);
                if (dyn) detect(ss, ss);
                const float y = filt(0, ss);
                yl = mm + y; yr = mm - y;
                break;
            }
            case Stereo::Left:  if (dyn) detect(l, l); yl = filt(0, l); break;
            case Stereo::Right: if (dyn) detect(r, r); yr = filt(1, r); break;
            case Stereo::kCount: break;
            }
            L[i] = l + mix * (yl - l);
            R[i] = r + mix * (yr - r);
        }
        s.mix = mix;
        if (s.env < 1e-12f) s.env = 0.0f;
    }

    void character(float* L, float* R, std::uint32_t n) noexcept {
        using OS = hc::fx::Oversampler4;
        const int target = std::clamp(static_cast<int>(p(Character)), 0, 2);
        const float drive = p(Drive) * 0.01f;
        const float fadeStep = static_cast<float>(1.0 / (fs_ * 0.010));
        const bool needSat = charMode_ != 0 || target != 0;
        for (std::uint32_t i = 0; i < n; ++i) {   // the clean path, delayed like the oversampler
            sat_[0][i] = L[i];
            sat_[1][i] = R[i];
            L[i] = delay_[0].process(L[i], OS::latency());
            R[i] = delay_[1].process(R[i], OS::latency());
        }
        if (!needSat && charFade_ <= 0.0f) return;
        const float g = 0.5f + 1.5f * drive, asym = 0.3f * drive;
        for (int c = 0; c < 2; ++c) {
            float* x4 = up_[static_cast<std::size_t>(c)].data();
            os_[c].up(sat_[static_cast<std::size_t>(c)].data(), x4, n);
            const std::uint32_t n4 = n * OS::kFactor;
            if (charMode_ == static_cast<int>(Char::Console)) {
                for (std::uint32_t i = 0; i < n4; ++i) x4[i] = softClip(g * x4[i]) / g;
            } else if (charMode_ == static_cast<int>(Char::Tube)) {
                for (std::uint32_t i = 0; i < n4; ++i) {
                    const float v = g * x4[i];
                    x4[i] = softClip(v + asym * v * v) / g;   // a little more on one side: even harmonics
                }
            }
            os_[c].down(x4, sat_[static_cast<std::size_t>(c)].data(), n);
        }
        // The tube curve leans to one side: take the DC it makes back out (5 Hz).
        const double dcK = 1.0 - std::exp(-2.0 * std::numbers::pi * 5.0 / fs_);
        for (std::uint32_t i = 0; i < n; ++i) {
            // Mode changes fade the coloured path out, switch, and fade it back in (10 ms each way).
            if (target != charMode_) {
                charFade_ = std::max(0.0f, charFade_ - fadeStep);
                if (charFade_ <= 0.0f) charMode_ = target;
            } else {
                charFade_ = charMode_ == 0 ? std::max(0.0f, charFade_ - fadeStep) : std::min(1.0f, charFade_ + fadeStep);
            }
            float sl = sat_[0][i], sr = sat_[1][i];
            if (charMode_ == static_cast<int>(Char::Tube)) {
                dcL_ += (sl - dcL_) * dcK;
                dcR_ += (sr - dcR_) * dcK;
                sl -= static_cast<float>(dcL_);
                sr -= static_cast<float>(dcR_);
            }
            L[i] += charFade_ * (sl - L[i]);
            R[i] += charFade_ * (sr - R[i]);
        }
    }

    double fs_ = 48000.0;
    std::array<BandState, kBands> bands_{};
    std::array<std::atomic<float>, kBands> dynPub_{};
    std::atomic<float> autoPub_{0.0f};
    hc::fx::Oversampler4 os_[2];
    std::array<std::vector<float>, 2> up_, sat_;
    hc::fx::DelayLine delay_[2];
    hc::fx::Biquad2 kw_[4];          // K-weighting: [0,1] input, [2,3] output
    double inMs_ = 0, outMs_ = 0, dcL_ = 0, dcR_ = 0;
    float autoDb_ = 0.0f;
    std::uint64_t clock_ = 0;
    std::uint32_t agTick_ = 0;
    hc::fx::Smoothed out_;
    int charMode_ = 0;
    float charFade_ = 0.0f;
    SpectrumMeter before_, after_;
};

} // namespace

std::unique_ptr<hc::fx::Effect> makeEliteEq() { return std::make_unique<EliteEqImpl>(); }

} // namespace hem::rack
