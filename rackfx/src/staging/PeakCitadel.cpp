// =============================================================================
//  34 PEAKCITADEL — a loudness maximizer: broadband (BASIC / ADVANCED) or five-band (MULTIBAND)
//  lookahead peak limiting. THRESHOLD (L / R, link) pushes the level into the limiter; OUTPUT
//  CEILING (L / R, link) is the most the output can reach. DOMAIN: DIGITAL (sample peaks), ANALOG
//  (a soft knee in the top 3 dB, still never over the ceiling) or TRUE PEAK (the peaks between the
//  samples are caught too, read through a 4x interpolator). PROFILE: TRANSPARENT / PUNCH /
//  AGGRESSIVE (release speed, and a pre-shave of the very top in AGGRESSIVE). RELEASE: MANUAL, or
//  AUTO (short for spikes, longer when the limiting is sustained).
//  MULTIBAND: four crossovers (Linkwitz-Riley 24 dB/oct with all-pass matching), and per band GAIN,
//  SHARE (how much of the limiting this band takes), LINK (linked bands move together) and
//  RELEASE (scaled by MASTER RELEASE); ISOLATION 0 % = the bands move as one, 100 % = each on its own.
//  After the bands are summed, a final broadband stage holds the ceiling.
//  Output: DITHER (NONE / TYPE A = TPDF / TYPE B = high-passed TPDF), QUANTIZE (16 / 20 / 24 bit),
//  NOISE SHAPING (NONE / LIGHT / MEDIUM / STRONG). DITHER NONE = no word-length change at all.
//  Different from MASTER LIMITER (44): one band, oversampled, a set of limiter modes.
//  Fixed latency 2 x 1.5 ms in every mode (the band stage and the final stage each look ahead).
//  Dead flat on load: BASIC, THRESHOLD 0, CEILING 0 dBFS, DIGITAL, DITHER NONE (normal audio passes
//  untouched, delayed by the reported latency).
//  ASSUMED: SHARE 0..100 lowers the band's own ceiling by 0..6 dB; AUTO release 0.25..1.75 x;
//  PUNCH 0.5 x release, AGGRESSIVE 0.25 x and a soft shave above 90 % of the ceiling; noise-shaping
//  filters 1st / 2nd / 3rd order; MASTER RELEASE in ms is the broadband release and, divided by
//  100 ms, the multiplier for the band releases.
//  The panel's five-meter band strips need more than the 10 meter slots the effect interface has:
//  each band's gain reduction is published, the band input / output meters wait for the Phase 2 UI.
// =============================================================================
#include "hem/rack/Staging.hpp"
#include "Kit.hpp"

#include <array>
#include <vector>

namespace hem::staging {
namespace {
using hc::fx::ParamInfo;
using hc::fx::Taper;
using BT = hc::fx::BiquadCoefs::Type;

constexpr int kBands = 5, kPer = 4;   // per band: gain, share, link, release
enum P {
    kMode, kThrL, kThrR, kThrLink, kCeilL, kCeilR, kCeilLink,
    kB0, kX1 = kB0 + kBands * kPer, kX2, kX3, kX4, kIso, kMRel, kRelMode, kProfile, kDomain, kDither, kBits, kShaping, kBypass, kCount
};
const char* const kModeN[] = {"BASIC", "ADVANCED", "MULTIBAND"};
const char* const kRelN[] = {"MANUAL", "AUTO"};
const char* const kProfN[] = {"TRANSPARENT", "PUNCH", "AGGRESSIVE"};
const char* const kDomN[] = {"DIGITAL", "ANALOG", "TRUE PEAK"};
const char* const kDitN[] = {"NONE", "TYPE A", "TYPE B"};
const char* const kBitsN[] = {"16-BIT", "20-BIT", "24-BIT"};
const char* const kShapeN[] = {"NONE", "LIGHT", "MEDIUM", "STRONG"};
#define HEM_BAND(id, nm)                                                    \
    {id "_gain", nm " GAIN", "dB", -12.0f, 12.0f, 0.0f},                   \
    {id "_share", nm " SHARE", "", 0.0f, 100.0f, 50.0f},                   \
    {id "_link", nm " LINK", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},          \
    {id "_release", nm " RELEASE", "ms", 1.0f, 1000.0f, 100.0f, Taper::Log}
const ParamInfo kParams[kCount] = {
    {"mode", "MODE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kModeN},
    {"threshold_l", "THRESHOLD L", "dB", -30.0f, 0.0f, 0.0f},
    {"threshold_r", "THRESHOLD R", "dB", -30.0f, 0.0f, 0.0f},
    {"threshold_link", "THRESHOLD LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    {"ceiling_l", "OUTPUT CEILING L", "dBFS", -12.0f, 0.0f, 0.0f},
    {"ceiling_r", "OUTPUT CEILING R", "dBFS", -12.0f, 0.0f, 0.0f},
    {"ceiling_link", "OUTPUT CEILING LINK", "", 0.0f, 1.0f, 1.0f, Taper::Toggle},
    HEM_BAND("band1", "BAND 1"), HEM_BAND("band2", "BAND 2"), HEM_BAND("band3", "BAND 3"), HEM_BAND("band4", "BAND 4"), HEM_BAND("band5", "BAND 5"),
    {"xover1", "XOVER 1", "Hz", 20.0f, 20000.0f, 80.0f, Taper::Log},
    {"xover2", "XOVER 2", "Hz", 20.0f, 20000.0f, 500.0f, Taper::Log},
    {"xover3", "XOVER 3", "Hz", 20.0f, 20000.0f, 2500.0f, Taper::Log},
    {"xover4", "XOVER 4", "Hz", 20.0f, 20000.0f, 8000.0f, Taper::Log},
    {"isolation", "ISOLATION", "%", 0.0f, 100.0f, 0.0f},
    {"master_release", "MASTER RELEASE", "ms", 1.0f, 1000.0f, 100.0f, Taper::Log},
    {"release_mode", "RELEASE BEHAVIOR", "", 0.0f, 1.0f, 1.0f, Taper::Choice, kRelN},
    {"profile", "PROFILE", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kProfN},
    {"domain", "DOMAIN", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kDomN},
    {"dither", "DITHER", "", 0.0f, 2.0f, 0.0f, Taper::Choice, kDitN},
    {"quantize", "QUANTIZE", "", 0.0f, 2.0f, 2.0f, Taper::Choice, kBitsN},
    {"noise_shaping", "NOISE SHAPING", "", 0.0f, 3.0f, 0.0f, Taper::Choice, kShapeN},
    {"bypass", "MASTER BYPASS", "", 0.0f, 1.0f, 0.0f, Taper::Toggle},
};
#undef HEM_BAND
const hc::fx::MeterInfo kMeters[] = {
    {"GAIN REDUCTION", "dB", -30, 0}, {"INPUT PEAK", "dBFS", -60, 0}, {"OUTPUT PEAK", "dBFS", -60, 0},
    {"BAND 1 GR", "dB", -30, 0}, {"BAND 2 GR", "dB", -30, 0}, {"BAND 3 GR", "dB", -30, 0}, {"BAND 4 GR", "dB", -30, 0}, {"BAND 5 GR", "dB", -30, 0}};

// Lookahead gain: the needed gain, held at its lowest over the lookahead, released, then averaged
// over the lookahead. The average never rises above the need at the sample it protects.
struct Lookahead {
    std::uint32_t w = 1;
    std::vector<float> dqV;
    std::vector<std::uint32_t> dqT;
    std::uint32_t head = 0, tail = 0, t = 0;
    std::vector<float> box;
    std::uint32_t bPos = 0;
    double sum = 0;
    float rel = 1, gr = 0;
    void prepare(std::uint32_t lookahead) {
        w = std::max<std::uint32_t>(1, lookahead);
        dqV.assign(w + 2, 1.0f); dqT.assign(w + 2, 0); box.assign(w, 1.0f);
        clear();
    }
    void clear() noexcept { head = tail = t = 0; std::fill(box.begin(), box.end(), 1.0f); bPos = 0; sum = static_cast<double>(w); rel = 1; gr = 0; }
    // need = the gain the newest sample asks for; relC = release coefficient; returns the gain for the sample w behind
    float tick(float need, float relC) noexcept {
        const auto cap = static_cast<std::uint32_t>(dqV.size());
        while (head != tail && dqV[(tail + cap - 1) % cap] >= need) tail = (tail + cap - 1) % cap;
        dqV[tail] = need; dqT[tail] = t; tail = (tail + 1) % cap;
        while (t - dqT[head] > w) head = (head + 1) % cap;
        const float m = dqV[head];
        ++t;
        rel = m < rel ? m : m + relC * (rel - m);
        sum += static_cast<double>(rel) - static_cast<double>(box[bPos]);
        box[bPos] = rel; bPos = (bPos + 1) % w;
        const float g = static_cast<float>(sum / static_cast<double>(w));
        gr = g;
        return std::min(1.0f, g);
    }
};

// 4x interpolation for the true-peak read (windowed sinc, 8 taps a phase)
struct TruePeak {
    std::array<float, 8> z{};
    int pos = 0;
    std::array<std::array<float, 8>, 3> h{};
    void prepare() noexcept {
        for (int ph = 1; ph <= 3; ++ph)
            for (int k = 0; k < 8; ++k) {
                const double x = (k - 3.5) - (ph / 4.0 - 0.5);
                const double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
                const double win = 0.5 + 0.5 * std::cos(std::numbers::pi * x / 4.5);
                h[static_cast<std::size_t>(ph - 1)][static_cast<std::size_t>(k)] = static_cast<float>(sinc * win);
            }
    }
    void clear() noexcept { z.fill(0); pos = 0; }
    float peak(float x) noexcept {
        z[static_cast<std::size_t>(pos)] = x; pos = (pos + 1) & 7;
        float m = std::fabs(z[static_cast<std::size_t>((pos + 3) & 7)]);
        for (const auto& hp : h) {
            float acc = 0;
            for (int k = 0; k < 8; ++k) acc += hp[static_cast<std::size_t>(k)] * z[static_cast<std::size_t>((pos + k) & 7)];
            m = std::max(m, std::fabs(acc));
        }
        return std::max(m, std::fabs(x));
    }
};

class PeakCitadel final : public hc::fx::Effect {
public:
    PeakCitadel() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeters; }
    std::uint32_t latency() const noexcept override { return 2 * look_; }
    bool paramActive(int i) const noexcept override {
        const bool mb = static_cast<int>(p(kMode)) == 2;
        if ((i >= kB0 && i < kX1) || (i >= kX1 && i <= kIso)) return mb;
        if (i == kThrR) return p(kThrLink) < 0.5f;
        if (i == kCeilR) return p(kCeilLink) < 0.5f;
        if (i == kBits || i == kShaping) return p(kDither) > 0.5f;
        return true;
    }
    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        look_ = static_cast<std::uint32_t>(std::lround(fs * 0.0015));
        for (auto& b : bandLa_) b.prepare(look_);
        final_.prepare(look_);
        for (auto& d : dA_) d.prepare(look_ + 4);
        for (auto& d : dB_) d.prepare(look_ + 4);
        for (auto& ch : dBand_) for (auto& d : ch) d.prepare(look_ + 4);
        for (auto& t : tp_) t.prepare();
        drvL_.prepare(fs, 20.0, 1.0f); drvR_.prepare(fs, 20.0, 1.0f); ceilL_.prepare(fs, 20.0, 1.0f); ceilR_.prepare(fs, 20.0, 1.0f); mb_.prepare(fs, 20.0, 0.0f);
        for (auto& s : bGain_) s.prepare(fs, 20.0, 1.0f);
        reset();
    }
    void reset() noexcept override {
        for (auto& b : bandLa_) b.clear();
        final_.clear();
        for (auto& d : dA_) d.clear();
        for (auto& d : dB_) d.clear();
        for (auto& ch : dBand_) for (auto& d : ch) d.clear();
        for (auto& t : tp_) t.clear();
        for (auto& c : xo_) for (auto& f : c) f.clear();
        sus_ = 0; err_.fill({}); rng_ = 1357u; prevR_.fill(0); key_ = -1; loaded_ = false; grM_ = 0; inPk_ = outPk_ = 0; bandGrM_.fill(0);
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (p(kBypass) > 0.5f) {   // bypass keeps the reported delay so the host's timing holds
            for (std::uint32_t i = 0; i < n; ++i) {
                dA_[0].write(L[i]); dA_[1].write(R[i]);
                dB_[0].write(dA_[0].read(static_cast<float>(look_))); dB_[1].write(dA_[1].read(static_cast<float>(look_)));
                L[i] = dB_[0].read(static_cast<float>(look_)); R[i] = dB_[1].read(static_cast<float>(look_));
            }
            return;
        }
        const bool tl = p(kThrLink) > 0.5f, cl = p(kCeilLink) > 0.5f;
        const float dLT = kit::dbToGain(-p(kThrL)), dRT = kit::dbToGain(-(tl ? p(kThrL) : p(kThrR)));
        const float cLT = kit::dbToGain(p(kCeilL)), cRT = kit::dbToGain(cl ? p(kCeilL) : p(kCeilR));
        const bool mbOn = static_cast<int>(p(kMode)) == 2;
        const float mbT = mbOn ? 1.0f : 0.0f;
        std::array<float, kBands> bgT{}, shareDb{};
        for (int b = 0; b < kBands; ++b) { bgT[static_cast<std::size_t>(b)] = kit::dbToGain(p(kB0 + b * kPer)); shareDb[static_cast<std::size_t>(b)] = -6.0f * p(kB0 + b * kPer + 1) * 0.01f; }
        if (!loaded_) { drvL_.cur = dLT; drvR_.cur = dRT; ceilL_.cur = cLT; ceilR_.cur = cRT; mb_.cur = mbT; for (int b = 0; b < kBands; ++b) bGain_[static_cast<std::size_t>(b)].cur = bgT[static_cast<std::size_t>(b)]; loaded_ = true; }
        setXover();
        const int prof = static_cast<int>(p(kProfile)), dom = static_cast<int>(p(kDomain));
        const float profMul = prof == 0 ? 1.0f : prof == 1 ? 0.5f : 0.25f;
        const bool autoRel = p(kRelMode) > 0.5f;
        const float mrel = p(kMRel);
        const float iso = p(kIso) * 0.01f;
        std::array<float, kBands> bRelC{};
        for (int b = 0; b < kBands; ++b) bRelC[static_cast<std::size_t>(b)] = hc::fx::onePoleCoef(fs_, p(kB0 + b * kPer + 3) * (mrel / 100.0f) * profMul);
        const int dither = static_cast<int>(p(kDither)), bitsI = static_cast<int>(p(kBits)), shaping = static_cast<int>(p(kShaping));
        const float q = std::pow(2.0f, static_cast<float>((bitsI == 0 ? 16 : bitsI == 1 ? 20 : 24) - 1));
        const float invFs = 1.0f / static_cast<float>(fs_);
        const float look = static_cast<float>(look_);
        grM_ = 0; bandGrM_.fill(0);
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gl = drvL_.next(dLT), gr = drvR_.next(dRT), cL = ceilL_.next(cLT), cR = ceilR_.next(cRT);
            const float c = std::min(cL, cR);
            inPk_ = std::max(inPk_, std::max(std::fabs(L[i]), std::fabs(R[i])));
            float xl = L[i] * gl, xr = R[i] * gr;
            if (prof == 2) {   // AGGRESSIVE: shave the very top softly before the limiter
                auto shave = [c](float v) { const float k = 0.9f * c, a = std::fabs(v); return a <= k ? v : std::copysign(k + (c - k) * std::tanh((a - k) / (c - k)), v); };
                xl = shave(xl); xr = shave(xr);
            }
            // ---- stage A: the bands (or a plain lookahead delay) ----
            dA_[0].write(xl); dA_[1].write(xr);
            float al = dA_[0].read(look), ar = dA_[1].read(look);
            const float mbx = mb_.next(mbT);
            if (mbx > 1e-6f) {
                std::array<std::array<float, kBands>, 2> band{};
                split(xl, band[0], 0); split(xr, band[1], 1);
                std::array<float, kBands> need{}, g{};
                for (std::size_t b = 0; b < kBands; ++b) {
                    const float bg = bGain_[b].next(bgT[b]);
                    band[0][b] *= bg; band[1][b] *= bg;
                    const float cb = c * kit::dbToGain(shareDb[b]);
                    const float pk = std::max(std::fabs(band[0][b]), std::fabs(band[1][b]));
                    need[b] = pk > cb ? cb / pk : 1.0f;
                    g[b] = bandLa_[b].tick(need[b], bRelC[b]);
                }
                // LINK: linked bands share the deepest cut; ISOLATION: 0 % all bands share it
                float gMin = 1.0f, gLink = 1.0f;
                for (std::size_t b = 0; b < kBands; ++b) { gMin = std::min(gMin, g[b]); if (p(kB0 + static_cast<int>(b) * kPer + 2) > 0.5f) gLink = std::min(gLink, g[b]); }
                float sl = 0, sr = 0;
                for (std::size_t b = 0; b < kBands; ++b) {
                    float gb = p(kB0 + static_cast<int>(b) * kPer + 2) > 0.5f ? gLink : g[b];
                    gb = std::exp(iso * std::log(std::max(gb, 1e-6f)) + (1.0f - iso) * std::log(std::max(gMin, 1e-6f)));
                    dBand_[0][b].write(band[0][b]); dBand_[1][b].write(band[1][b]);
                    sl += dBand_[0][b].read(look) * gb; sr += dBand_[1][b].read(look) * gb;
                    bandGrM_[b] = std::max(bandGrM_[b], -kit::gainToDb(gb));
                }
                al += mbx * (sl - al); ar += mbx * (sr - ar);
            }
            // ---- stage B: the broadband ceiling ----
            float pkl = std::fabs(al), pkr = std::fabs(ar);
            if (dom == 2) { pkl = tp_[0].peak(al); pkr = tp_[1].peak(ar); }
            const float needL = need1(pkl, cL, dom), needR = need1(pkr, cR, dom);
            const float needB = std::min(needL, needR);
            float relMs = mrel * profMul;
            if (autoRel) { sus_ = std::clamp(sus_ + invFs * (needB < 0.89f ? 0.7f : -0.35f), 0.0f, 1.0f); relMs *= 0.25f + 1.5f * sus_; }
            const float gB = final_.tick(needB, hc::fx::onePoleCoef(fs_, relMs));
            dB_[0].write(al); dB_[1].write(ar);
            float yl = dB_[0].read(look) * gB, yr = dB_[1].read(look) * gB;
            grM_ = std::max(grM_, -kit::gainToDb(gB));
            if (dither > 0) { yl = quant(yl, q, dither, shaping, 0); yr = quant(yr, q, dither, shaping, 1); }
            outPk_ = std::max(outPk_, std::max(std::fabs(yl), std::fabs(yr)));
            L[i] = kit::sane(yl); R[i] = kit::sane(yr);
        }
        publishMeter(0, -grM_); publishMeter(1, kit::gainToDb(inPk_)); publishMeter(2, kit::gainToDb(outPk_));
        for (int b = 0; b < kBands; ++b) publishMeter(3 + b, mbOn ? -bandGrM_[static_cast<std::size_t>(b)] : 0.0f);
        publishReduction(-grM_);
        inPk_ *= 0.9f; outPk_ *= 0.9f;
    }

private:
    // the gain one peak asks for, per DOMAIN (ANALOG: a soft knee over the top 3 dB, still under the ceiling)
    static float need1(float pk, float ceil, int dom) noexcept {
        if (pk <= 1e-9f) return 1.0f;
        if (dom != 1) return pk > ceil ? ceil / pk : 1.0f;
        const float l = kit::gainToDb(pk), cdb = kit::gainToDb(ceil);
        if (l < cdb - 3.0f) return 1.0f;
        const float f = cdb - 3.0f * std::exp(-(l - cdb + 3.0f) / 3.0f);
        return std::min(1.0f, kit::dbToGain(f - l));
    }
    float quant(float x, float q, int dither, int shaping, std::size_t c) noexcept {
        static constexpr std::array<std::array<float, 3>, 4> kH{{{0, 0, 0}, {0.5f, 0, 0}, {1.0f, -0.5f, 0}, {1.623f, -0.982f, 0.109f}}};
        auto& e = err_[c];
        const auto& h = kH[static_cast<std::size_t>(shaping)];
        const float v = x - (h[0] * e[0] + h[1] * e[1] + h[2] * e[2]);
        auto rnd = [this]() { rng_ = rng_ * 1664525u + 1013904223u; return static_cast<float>(rng_ >> 8) / 16777216.0f; };
        float d = rnd() - rnd();   // TPDF, +-1 LSB
        if (dither == 2) { const float r0 = rnd() - 0.5f; d = r0 - prevR_[c]; prevR_[c] = r0; }   // high-passed TPDF
        const float yq = std::round(v * q + d) / q;
        e[2] = e[1]; e[1] = e[0]; e[0] = yq - v;
        return yq;
    }
    void setXover() noexcept {
        std::array<float, 4> f{p(kX1), p(kX2), p(kX3), p(kX4)};
        for (std::size_t k = 1; k < 4; ++k) f[k] = std::max(f[k], f[k - 1] * 1.2f);   // keep them in order
        const float key = f[0] + f[1] * 3.0f + f[2] * 7.0f + f[3] * 11.0f;
        if (key == key_) return;
        key_ = key;
        const double ny = fs_ * 0.45;
        for (std::size_t k = 0; k < 4; ++k) {
            const double hz = std::min<double>(f[k], ny);
            xc_[k][0] = hc::fx::BiquadCoefs::make(BT::LowPass, fs_, hz, 0.7071);
            xc_[k][1] = hc::fx::BiquadCoefs::make(BT::HighPass, fs_, hz, 0.7071);
            xc_[k][2] = hc::fx::BiquadCoefs::make(BT::AllPass, fs_, hz, 0.7071);
        }
        for (auto& ch : xo_) {
            // per channel: for split k: LP x2, HP x2; plus all-pass matches for the bands below
            for (std::size_t k = 0; k < 4; ++k) { ch[k * 4 + 0].c = xc_[k][0]; ch[k * 4 + 1].c = xc_[k][0]; ch[k * 4 + 2].c = xc_[k][1]; ch[k * 4 + 3].c = xc_[k][1]; }
            // all-passes: band 1 through splits 2,3,4; band 2 through 3,4; band 3 through 4
            ch[16].c = xc_[1][2]; ch[17].c = xc_[2][2]; ch[18].c = xc_[3][2];
            ch[19].c = xc_[2][2]; ch[20].c = xc_[3][2];
            ch[21].c = xc_[3][2];
        }
    }
    void split(float x, std::array<float, kBands>& out, std::size_t c) noexcept {
        auto& f = xo_[c];
        float rem = x;
        for (std::size_t k = 0; k < 4; ++k) {
            out[k] = f[k * 4 + 1].tick(f[k * 4 + 0].tick(rem));
            rem = f[k * 4 + 3].tick(f[k * 4 + 2].tick(rem));
        }
        out[4] = rem;
        out[0] = f[18].tick(f[17].tick(f[16].tick(out[0])));
        out[1] = f[20].tick(f[19].tick(out[1]));
        out[2] = f[21].tick(out[2]);
    }

    double fs_ = 48000;
    std::uint32_t look_ = 72;
    std::array<Lookahead, kBands> bandLa_{};
    Lookahead final_;
    std::array<hc::fx::DelayLine, 2> dA_{}, dB_{};
    std::array<std::array<hc::fx::DelayLine, kBands>, 2> dBand_{};
    std::array<TruePeak, 2> tp_{};
    std::array<std::array<hc::fx::BiquadCoefs, 3>, 4> xc_{};
    std::array<std::array<hc::fx::Biquad, 22>, 2> xo_{};
    hc::fx::Smoothed drvL_, drvR_, ceilL_, ceilR_, mb_;
    std::array<hc::fx::Smoothed, kBands> bGain_{};
    std::array<std::array<float, 3>, 2> err_{};
    std::array<float, 2> prevR_{};
    std::array<float, kBands> bandGrM_{};
    float sus_ = 0, key_ = -1, grM_ = 0, inPk_ = 0, outPk_ = 0;
    std::uint32_t rng_ = 1357u;
    bool loaded_ = false;
};
} // namespace

std::unique_ptr<hc::fx::Effect> makePeakCitadel() { return std::make_unique<PeakCitadel>(); }

} // namespace hem::staging
