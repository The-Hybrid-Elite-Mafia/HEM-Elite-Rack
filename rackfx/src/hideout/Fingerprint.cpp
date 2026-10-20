// FINGERPRINT — convolution reverb (see Holdouts.hpp).
//
// The impulse responses are made here, not recorded: for a room, the direct sound, early reflections (a
// seeded pattern of taps per side) and a tail of noise in seven octave bands, each band dying at its own
// reverb time after a short build-up; for a plate, the dense tail alone; for a spring, the response of
// spring loops (as in COIL) to a click. Each response is scaled so its energy is 1 per side (so wet noise
// comes out as loud as it went in) before START, LENGTH, DECAY, DIRECT and CPU shape it.
//
// The convolution: uniformly partitioned, overlap-save, 512-sample blocks (FFT 1024; both sides in one complex
// FFT), a frequency-domain delay line of the input's spectra and the response's spectra per partition.
// A new response is made by a background thread (polling the settings every 10 ms) into the spare of two
// slots; the audio thread then crossfades from the old slot to the new one over four blocks (both run during
// the fade, on the same input spectra) and hands the old one back. The audio thread never waits, never
// allocates; the background thread never allocates after prepare().
#include "hem/rack/Holdouts.hpp"

#include "SpaceKit.hpp"

#include <atomic>
#include <chrono>
#include <complex>
#include <thread>
#include <vector>

#include "hybrid/fx/Fft.hpp"

namespace hem::rack {
namespace fingerprint {
namespace {
using namespace hideout;
using cf = std::complex<float>;

const char* const kSpaceN[kSpaces] = {"VOCAL BOOTH", "WOOD ROOM", "LIVE ROOM", "DRUM ROOM", "CHAMBER", "STONE CHAMBER", "SMALL HALL", "CONCERT HALL",
                                      "BIG HALL", "CHURCH", "PLATE BRIGHT", "PLATE DARK", "PLATE LONG", "SPRING TWO", "SPRING THREE", "GARAGE"};
const char* const kCpuN[] = {"FULL", "LOW"};
const ParamInfo kParams[kCount] = {
    {"space", "Space", "", 0, kSpaces - 1, 7, Taper::Choice, kSpaceN},
    {"start", "Start", "s", 0.0f, 1.0f, 0.0f},
    {"length", "Length", "s", 0.0f, 6.0f, 6.0f},
    {"decay", "Decay", "x", 0.25f, 1.0f, 1.0f},
    {"mix", "Mix", "%", 0.0f, 100.0f, 100.0f},
    {"direct", "Direct", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"predelay", "Pre-Delay", "ms", 0.0f, 500.0f, 0.0f},
    {"drydelay", "Dry Delay", "ms", 0.0f, 30.0f, 0.0f},
    {"cpu", "CPU", "", 0, 1, 0, Taper::Choice, kCpuN},
    {"output", "Output", "dB", -50.0f, 0.0f, 0.0f},
    {"crosstalk", "Crosstalk", "%", 0.0f, 100.0f, 0.0f},
};
const hc::fx::MeterInfo kMeterInfo[kMeters] = {{"RT60", "s", 0.0f, 8.0f}, {"READY", "", 0.0f, 1.0f}};

// ---- the spaces ----------------------------------------------------------------------------------------
constexpr int kBands = 7;
constexpr double kBandHz[kBands] = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
enum class Kind { Room, Plate, Spring };
struct SpaceSpec {
    Kind kind;
    float rt[kBands];   // reverb time per octave band (s); a spring uses rt[3]
    float preMs;        // the first reflection after the direct sound
    float erMs;         // the span of the early reflections
    int taps;           // early reflections per side
    float erLevel;      // their level against the tail
    float buildMs;      // how long the tail takes to grow
    float width;        // 0 = the sides alike, 1 = unrelated
    int springs;        // a spring tank: 2 or 3
};
constexpr SpaceSpec kSpec[kSpaces] = {
    {Kind::Room, {0.25f, 0.22f, 0.20f, 0.18f, 0.16f, 0.14f, 0.12f}, 1, 12, 10, 0.70f, 2, 0.6f, 0},
    {Kind::Room, {0.60f, 0.55f, 0.50f, 0.45f, 0.40f, 0.35f, 0.30f}, 3, 25, 14, 0.60f, 5, 0.7f, 0},
    {Kind::Room, {1.00f, 0.95f, 0.90f, 0.85f, 0.75f, 0.60f, 0.50f}, 5, 35, 16, 0.55f, 8, 0.8f, 0},
    {Kind::Room, {0.80f, 0.80f, 0.75f, 0.70f, 0.65f, 0.55f, 0.45f}, 2, 20, 18, 0.70f, 4, 0.8f, 0},
    {Kind::Room, {1.60f, 1.50f, 1.40f, 1.30f, 1.10f, 0.90f, 0.70f}, 8, 30, 16, 0.40f, 15, 0.85f, 0},
    {Kind::Room, {2.20f, 2.10f, 2.00f, 1.90f, 1.70f, 1.40f, 1.10f}, 10, 40, 16, 0.45f, 18, 0.85f, 0},
    {Kind::Room, {1.80f, 1.70f, 1.60f, 1.50f, 1.30f, 1.00f, 0.80f}, 12, 45, 20, 0.40f, 25, 0.9f, 0},
    {Kind::Room, {2.80f, 2.60f, 2.30f, 2.10f, 1.80f, 1.40f, 1.00f}, 20, 60, 24, 0.35f, 40, 0.9f, 0},
    {Kind::Room, {3.80f, 3.50f, 3.10f, 2.80f, 2.40f, 1.90f, 1.40f}, 28, 80, 24, 0.30f, 60, 0.95f, 0},
    {Kind::Room, {5.00f, 4.60f, 4.20f, 3.80f, 3.20f, 2.50f, 1.80f}, 35, 90, 24, 0.30f, 80, 0.95f, 0},
    {Kind::Plate, {1.60f, 1.70f, 1.80f, 1.80f, 1.70f, 1.60f, 1.40f}, 0, 0, 0, 0.0f, 1, 0.9f, 0},
    {Kind::Plate, {2.00f, 2.00f, 1.90f, 1.70f, 1.30f, 0.90f, 0.60f}, 0, 0, 0, 0.0f, 1, 0.9f, 0},
    {Kind::Plate, {3.60f, 3.80f, 4.00f, 3.90f, 3.60f, 3.00f, 2.40f}, 0, 0, 0, 0.0f, 1, 0.9f, 0},
    {Kind::Spring, {2.4f, 2.4f, 2.4f, 2.4f, 2.4f, 2.4f, 2.4f}, 0, 0, 0, 0.0f, 0, 0.5f, 2},
    {Kind::Spring, {3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f}, 0, 0, 0, 0.0f, 0, 0.5f, 3},
    {Kind::Room, {2.60f, 2.80f, 3.00f, 3.00f, 2.70f, 2.20f, 1.60f}, 6, 50, 20, 0.60f, 10, 0.8f, 0},
};
constexpr float kDirect = 0.3f;     // the direct sound's level in a normalised response
constexpr double kMaxIrS = 6.0;     // the longest response
constexpr int kFadeBlocks = 4;

// a spring loop (as in COIL)
constexpr int kSpringStages = 16;
struct SpringLoop {
    std::vector<float> line;
    std::size_t pos = 0;
    std::array<float, kSpringStages> ap{};
    float lp = 0.0f, g = 0.0f;
    float tick(float x, float drip, float lpA) noexcept {
        const float back = line[pos];
        lp = back + lpA * (lp - back);
        float v = x + g * lp;
        for (auto& s : ap) {
            const float y = -drip * v + s;
            s = v + drip * y;
            v = y;
        }
        line[pos] = v;
        pos = (pos + 1) % line.size();
        return lp;
    }
};

// What a response is made from: when any of these move, a new one is made.
struct Key {
    float space = -1, start = 0, length = 0, decay = 0, direct = 0, cpu = 0;
    bool operator==(const Key&) const = default;
};

// One response, ready to convolve: its spectra per partition (left and right, real and imaginary apart).
struct Slot {
    std::vector<float> re[2], im[2];   // [partition * bins + bin]
    int parts = 0;
    float rt60 = 0.0f;
};

class Impl final : public hc::fx::Effect {
public:
    static constexpr int B = static_cast<int>(kBlock), N = 2 * B, kBins = B + 1;

    Impl() { setDefaults(); }
    ~Impl() override { stopWorker(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }
    std::uint32_t latency() const noexcept override { return kBlock; }

    void prepare(double fs, std::uint32_t) override {
        stopWorker();
        fs_ = fs;
        maxParts_ = static_cast<int>(std::ceil(kMaxIrS * fs / B));
        const auto spec = static_cast<std::size_t>(maxParts_) * kBins;
        for (auto& s : slot_)
            for (int c = 0; c < 2; ++c) {
                s.re[c].assign(spec, 0.0f);
                s.im[c].assign(spec, 0.0f);
            }
        for (int c = 0; c < 2; ++c) {
            fdlRe_[c].assign(spec, 0.0f);
            fdlIm_[c].assign(spec, 0.0f);
            accRe_[c].assign(kBins, 0.0f);
            accIm_[c].assign(kBins, 0.0f);
            accRe2_[c].assign(kBins, 0.0f);
            accIm2_[c].assign(kBins, 0.0f);
            in_[c].assign(N, 0.0f);
            out_[c].assign(B, 0.0f);
            outNew_[c].assign(B, 0.0f);
            pre_[c].prepare(static_cast<std::uint32_t>(0.5 * fs) + 8);
            dry_[c].prepare(static_cast<std::uint32_t>(0.031 * fs) + kBlock + 8);
        }
        fftA_.prepare(N);
        fftW_.prepare(N);
        z_.assign(N, cf{});
        zw_.assign(N, cf{});
        const auto irLen = static_cast<std::size_t>((kMaxIrS + 1.0) * fs) + 16;
        for (auto& v : ir_) v.assign(irLen, 0.0f);
        edc_.assign(irLen, 0.0);
        for (auto& sp : springs_)
            for (auto& s : sp) s.line.assign(static_cast<std::size_t>(0.07 * fs) + 8, 0.0f);
        // the first response, made now, so the sound is right from the start
        const Key k = keyNow();
        build(slot_[0], k);
        active_.store(0, std::memory_order_relaxed);
        state_.store(kIdle, std::memory_order_relaxed);
        lastKey_ = k;
        publishMeter(Rt60, slot_[0].rt60);
        publishMeter(Ready, 1.0f);
        reset();
        startWorker();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            std::fill(fdlRe_[c].begin(), fdlRe_[c].end(), 0.0f);
            std::fill(fdlIm_[c].begin(), fdlIm_[c].end(), 0.0f);
            std::fill(in_[c].begin(), in_[c].end(), 0.0f);
            std::fill(out_[c].begin(), out_[c].end(), 0.0f);
            std::fill(outNew_[c].begin(), outNew_[c].end(), 0.0f);
            pre_[c].clear();
            dry_[c].clear();
        }
        head_ = 0;
        pos_ = 0;
        fade_ = 0;
        mix_.prepare(fs_, 20, p(Mix) * 0.01f);
        out_g_.prepare(fs_, 20, hc::fx::dbToGain(p(Output)));
        preS_.prepare(fs_, 80, static_cast<float>(p(PreDelay) * 0.001 * fs_));
        dryS_.prepare(fs_, 80, static_cast<float>(p(DryDelay) * 0.001 * fs_));
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        const float wMix = p(Mix) * 0.01f, wOut = hc::fx::dbToGain(p(Output));
        const float wPre = static_cast<float>(p(PreDelay) * 0.001 * fs_), wDry = static_cast<float>(p(DryDelay) * 0.001 * fs_);
        const float x = p(Crosstalk) * 0.005f;   // 100 = each side half its own, half the other
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = L[i], r = R[i];
            in_[0][static_cast<std::size_t>(B + pos_)] = (1.0f - x) * l + x * r;
            in_[1][static_cast<std::size_t>(B + pos_)] = (1.0f - x) * r + x * l;
            float wl = out_[0][static_cast<std::size_t>(pos_)], wr = out_[1][static_cast<std::size_t>(pos_)];
            if (fade_ > 0) {   // crossfading to the new response
                const float t = (static_cast<float>((kFadeBlocks - fade_) * B + pos_) + 0.5f) / static_cast<float>(kFadeBlocks * B);
                wl += t * (outNew_[0][static_cast<std::size_t>(pos_)] - wl);
                wr += t * (outNew_[1][static_cast<std::size_t>(pos_)] - wr);
            }
            // pre-delay on the reverb, the dry sound held back by the latency (+ DRY DELAY)
            const float pd = preS_.next(wPre), dd = static_cast<float>(B) + dryS_.next(wDry);
            pre_[0].write(wl);
            pre_[1].write(wr);
            dry_[0].write(l);
            dry_[1].write(r);
            const float m = mix_.next(wMix), g = out_g_.next(wOut);
            L[i] = g * ((1.0f - m) * dry_[0].read(dd) + m * pre_[0].read(pd));
            R[i] = g * ((1.0f - m) * dry_[1].read(dd) + m * pre_[1].read(pd));
            if (++pos_ == B) {
                pos_ = 0;
                block();
            }
        }
    }

private:
    enum State : int { kIdle, kReady, kFading };

    Key keyNow() const noexcept {
        return {static_cast<float>(std::clamp(static_cast<int>(std::lround(p(Space))), 0, kSpaces - 1)), p(Start), p(Length), p(Decay),
                p(Direct) >= 0.5f ? 1.0f : 0.0f, p(Cpu) >= 0.5f ? 1.0f : 0.0f};
    }

    // ---- the audio thread: one block of convolution ----------------------------------------------------
    void block() noexcept {
        // a new response waiting: start the fade
        if (fade_ == 0 && state_.load(std::memory_order_acquire) == kReady) {
            state_.store(kFading, std::memory_order_relaxed);
            fade_ = kFadeBlocks + 1;   // the first block of the fade is computed now, heard next
        }
        // the input's spectrum (both sides in one FFT), into the delay line
        for (int k = 0; k < N; ++k) z_[static_cast<std::size_t>(k)] = cf(in_[0][static_cast<std::size_t>(k)], in_[1][static_cast<std::size_t>(k)]);
        fftA_.forward(z_.data());
        head_ = (head_ + maxParts_ - 1) % maxParts_;
        const std::size_t h0 = static_cast<std::size_t>(head_) * kBins;
        for (int k = 0; k < kBins; ++k) {
            const cf a = z_[static_cast<std::size_t>(k)], b = std::conj(z_[static_cast<std::size_t>((N - k) % N)]);
            const cf xl = 0.5f * (a + b), xr = cf(0.0f, -0.5f) * (a - b);
            fdlRe_[0][h0 + static_cast<std::size_t>(k)] = xl.real();
            fdlIm_[0][h0 + static_cast<std::size_t>(k)] = xl.imag();
            fdlRe_[1][h0 + static_cast<std::size_t>(k)] = xr.real();
            fdlIm_[1][h0 + static_cast<std::size_t>(k)] = xr.imag();
        }
        for (int c = 0; c < 2; ++c) std::copy(in_[c].begin() + B, in_[c].end(), in_[c].begin());
        const int act = active_.load(std::memory_order_relaxed);
        convolve(slot_[act], accRe_, accIm_, out_);
        if (fade_ > 0) {
            convolve(slot_[1 - act], accRe2_, accIm2_, outNew_);
            if (--fade_ == 0) {   // the new one is all that is heard now
                active_.store(1 - act, std::memory_order_release);
                for (int c = 0; c < 2; ++c) std::copy(outNew_[c].begin(), outNew_[c].end(), out_[c].begin());
                publishMeter(Rt60, slot_[1 - act].rt60);
                state_.store(kIdle, std::memory_order_release);
            }
        }
    }
    // sum over the partitions of input spectrum x response spectrum, then back to time (the block's output)
    void convolve(const Slot& s, std::vector<float>* aRe, std::vector<float>* aIm, std::vector<float>* out) noexcept {
        for (int c = 0; c < 2; ++c) {
            float* ar = aRe[c].data();
            float* ai = aIm[c].data();
            std::fill(ar, ar + kBins, 0.0f);
            std::fill(ai, ai + kBins, 0.0f);
            for (int j = 0; j < s.parts; ++j) {
                const std::size_t xo = static_cast<std::size_t>((head_ + j) % maxParts_) * kBins, ho = static_cast<std::size_t>(j) * kBins;
                const float* xr = fdlRe_[c].data() + xo;
                const float* xi = fdlIm_[c].data() + xo;
                const float* hr = s.re[c].data() + ho;
                const float* hi = s.im[c].data() + ho;
                for (int k = 0; k < kBins; ++k) {
                    ar[k] += xr[k] * hr[k] - xi[k] * hi[k];
                    ai[k] += xr[k] * hi[k] + xi[k] * hr[k];
                }
            }
        }
        // left + i right, mirrored for the upper half, back to time: the real part is left, the imaginary right
        for (int k = 0; k < kBins; ++k) {
            const cf yl(aRe[0][static_cast<std::size_t>(k)], aIm[0][static_cast<std::size_t>(k)]);
            const cf yr(aRe[1][static_cast<std::size_t>(k)], aIm[1][static_cast<std::size_t>(k)]);
            z_[static_cast<std::size_t>(k)] = yl + cf(0.0f, 1.0f) * yr;
            if (k > 0 && k < B) z_[static_cast<std::size_t>(N - k)] = std::conj(yl) + cf(0.0f, 1.0f) * std::conj(yr);
        }
        fftA_.inverse(z_.data());
        for (int k = 0; k < B; ++k) {
            out[0][static_cast<std::size_t>(k)] = z_[static_cast<std::size_t>(B + k)].real();
            out[1][static_cast<std::size_t>(k)] = z_[static_cast<std::size_t>(B + k)].imag();
        }
    }

    // ---- the background thread ----------------------------------------------------------------------------
    void startWorker() {
        worker_ = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const Key k = keyNow();
                if (state_.load(std::memory_order_acquire) == kIdle && !(k == lastKey_)) {
                    const int spare = 1 - active_.load(std::memory_order_acquire);
                    publishMeter(Ready, 0.0f);
                    build(slot_[spare], k);
                    lastKey_ = k;
                    state_.store(kReady, std::memory_order_release);
                }
                const bool ready = state_.load(std::memory_order_acquire) == kIdle && keyNow() == lastKey_;
                publishMeter(Ready, ready ? 1.0f : 0.0f);
            }
        });
    }
    void stopWorker() {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
    }

    // ---- making a response (background thread, or prepare) -----------------------------------------------
    void build(Slot& s, const Key& k) noexcept {
        const auto& sp = kSpec[static_cast<int>(k.space)];
        const std::size_t cap = ir_[0].size();
        std::size_t len = make(sp, static_cast<int>(k.space), cap);
        // DIRECT: the response's own direct sound
        if (k.direct >= 0.5f)
            for (auto& v : ir_) v[0] += kDirect;
        // DECAY: the reverb time x the ratio (an extra fall applied after the direct sound)
        const double rt = 0.5 * (sp.rt[2] + sp.rt[3]);
        if (k.decay < 0.999f) {
            const double perS = -3.0 * (1.0 / (static_cast<double>(k.decay) * rt) - 1.0 / rt);   // log10 of the extra gain per second
            const double step = std::pow(10.0, perS / fs_);
            for (auto& v : ir_) {
                double g = 1.0;
                for (std::size_t i = 1; i < len; ++i) {
                    g *= step;
                    v[i] *= static_cast<float>(g);
                }
            }
        }
        // START, then LENGTH (with a 10 ms fade where it is cut)
        const auto skip = std::min(len, static_cast<std::size_t>(static_cast<double>(k.start) * fs_));
        if (skip > 0)
            for (auto& v : ir_) {
                std::copy(v.begin() + static_cast<std::ptrdiff_t>(skip), v.begin() + static_cast<std::ptrdiff_t>(len), v.begin());
                std::fill(v.begin() + static_cast<std::ptrdiff_t>(len - skip), v.begin() + static_cast<std::ptrdiff_t>(len), 0.0f);
            }
        len -= skip;
        const auto want = static_cast<std::size_t>(static_cast<double>(k.length) * fs_);
        if (want < len) {
            const auto fade = std::min(want, static_cast<std::size_t>(0.01 * fs_));
            for (auto& v : ir_) {
                for (std::size_t i = 0; i < fade; ++i) v[want - fade + i] *= 1.0f - static_cast<float>(i + 1) / static_cast<float>(fade + 1);
                std::fill(v.begin() + static_cast<std::ptrdiff_t>(want), v.begin() + static_cast<std::ptrdiff_t>(len), 0.0f);
            }
            len = want;
        }
        // the reverb time as it is now (Schroeder: the energy still to come, -5 .. -35 dB), and CPU LOW's cut
        double total = 0.0;
        for (std::size_t i = len; i-- > 0;) {
            total += static_cast<double>(ir_[0][i]) * ir_[0][i] + static_cast<double>(ir_[1][i]) * ir_[1][i];
            edc_[i] = total;
        }
        s.rt60 = 0.0f;
        if (total > 0.0 && len > static_cast<std::size_t>(0.1 * fs_)) {
            std::size_t t5 = 0, t35 = 0, t25 = 0, t60 = len;
            for (std::size_t i = 0; i < len; ++i) {
                const double d = 10.0 * std::log10(std::max(edc_[i] / total, 1e-30));
                if (!t5 && d <= -5.0) t5 = i;
                if (!t25 && d <= -25.0) t25 = i;
                if (!t35 && d <= -35.0) t35 = i;
                if (t60 == len && d <= -60.0) t60 = i;
            }
            if (t35 > t5) s.rt60 = static_cast<float>(2.0 * static_cast<double>(t35 - t5) / fs_);
            else if (t25 > t5) s.rt60 = static_cast<float>(3.0 * static_cast<double>(t25 - t5) / fs_);
            if (k.cpu >= 0.5f && t60 < len) len = t60;
        }
        // the spectra, partition by partition (both sides in one FFT)
        s.parts = static_cast<int>(std::min<std::size_t>((len + B - 1) / B, static_cast<std::size_t>(maxParts_)));
        for (int j = 0; j < s.parts; ++j) {
            for (int i = 0; i < N; ++i) {
                const std::size_t at = static_cast<std::size_t>(j) * B + static_cast<std::size_t>(i);
                zw_[static_cast<std::size_t>(i)] = i < B && at < len ? cf(ir_[0][at], ir_[1][at]) : cf{};
            }
            fftW_.forward(zw_.data());
            const std::size_t o = static_cast<std::size_t>(j) * kBins;
            for (int b = 0; b < kBins; ++b) {
                const cf a = zw_[static_cast<std::size_t>(b)], c = std::conj(zw_[static_cast<std::size_t>((N - b) % N)]);
                const cf hl = 0.5f * (a + c), hr = cf(0.0f, -0.5f) * (a - c);
                s.re[0][o + static_cast<std::size_t>(b)] = hl.real();
                s.im[0][o + static_cast<std::size_t>(b)] = hl.imag();
                s.re[1][o + static_cast<std::size_t>(b)] = hr.real();
                s.im[1][o + static_cast<std::size_t>(b)] = hr.imag();
            }
        }
    }

    // the raw response of a space into ir_[0..1] (energy 1 per side, no direct sound); returns its length
    std::size_t make(const SpaceSpec& sp, int seed, std::size_t cap) noexcept {
        for (auto& v : ir_) std::fill(v.begin(), v.end(), 0.0f);
        const double maxRt = *std::max_element(std::begin(sp.rt), std::end(sp.rt));
        std::size_t len = std::min(cap, static_cast<std::size_t>(std::min(kMaxIrS, (sp.preMs + sp.erMs) * 0.001 + maxRt * 1.15) * fs_));
        std::uint32_t rng = 0x2545F491u * static_cast<std::uint32_t>(seed + 7);
        auto rnd = [&]() {   // -1 .. 1
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>(rng >> 8) / 8388608.0f - 1.0f;
        };
        if (sp.kind == Kind::Spring) {
            const float lpA = std::exp(-2.0f * static_cast<float>(std::numbers::pi) * 4200.0f / static_cast<float>(fs_));
            constexpr float kMs[2][3] = {{37.0f, 44.0f, 0.0f}, {31.0f, 37.0f, 43.0f}};
            const float drip = sp.springs == 2 ? 0.62f : 0.6f;
            for (int c = 0; c < 2; ++c) {
                for (int q = 0; q < sp.springs; ++q) {
                    auto& s = springs_[c][static_cast<std::size_t>(q)];
                    const double ms = kMs[sp.springs == 2 ? 0 : 1][q] * (c == 0 ? 1.0 : 1.07);
                    const auto n = static_cast<std::size_t>(ms * 0.001 * fs_);
                    std::fill(s.line.begin(), s.line.end(), 0.0f);
                    s.line.resize(std::min(n, s.line.capacity()));   // never grows: capacity set in prepare
                    s.pos = 0;
                    s.ap.fill(0.0f);
                    s.lp = 0.0f;
                    s.g = static_cast<float>(std::pow(10.0, -3.0 * ms * 0.001 / sp.rt[3]));
                }
                hc::fx::Biquad lowCut;
                lowCut.c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::HighPass, fs_, 110.0, 0.7071);
                for (std::size_t i = 0; i < len; ++i) {
                    const float in = i == 0 ? 1.0f : 0.0f;
                    float sum = 0.0f;
                    for (int q = 0; q < sp.springs; ++q) sum += springs_[c][static_cast<std::size_t>(q)].tick(in, drip, lpA) * (q & 1 ? -1.0f : 1.0f);
                    ir_[c][i] = lowCut.tick(sum);
                }
            }
        } else {
            const auto start = static_cast<std::size_t>((sp.preMs + (sp.kind == Kind::Room ? 0.3f * sp.erMs : 0.0f)) * 0.001 * fs_);
            const double build = std::max(0.0005, sp.buildMs * 0.001);
            // the tail: noise per side (the right one partly the left's, by WIDTH) in seven octave bands
            hc::fx::Biquad bp[2][kBands];
            double k[kBands];
            for (int b = 0; b < kBands; ++b) {
                const auto c = hc::fx::BiquadCoefs::make(hc::fx::BiquadCoefs::Type::BandPass, fs_, std::min(kBandHz[b], fs_ * 0.4), 1.41);
                bp[0][b].c = bp[1][b].c = c;
                k[b] = std::pow(10.0, -3.0 / (sp.rt[b] * fs_));   // per-sample fall: 60 dB in rt
            }
            double env[kBands];
            std::fill(std::begin(env), std::end(env), 1.0);
            const float w = sp.width;
            for (std::size_t i = start; i < len; ++i) {
                const float a = rnd(), b2 = rnd();
                const float nl = a, nr = (1.0f - w) * a + w * b2;
                const double t = static_cast<double>(i - start) / fs_;
                const double grow = 1.0 - std::exp(-t / build);
                float yl = 0.0f, yr = 0.0f;
                for (int b = 0; b < kBands; ++b) {
                    const float g = static_cast<float>(env[b] * grow);
                    yl += g * bp[0][b].tick(nl);
                    yr += g * bp[1][b].tick(nr);
                    env[b] *= k[b];
                }
                ir_[0][i] = yl;
                ir_[1][i] = yr;
            }
            // the early reflections: a pattern of taps per side over the span, falling, a little dulled
            if (sp.kind == Kind::Room && sp.taps > 0) {
                double tailE = 0.0;
                for (std::size_t i = 0; i < len; ++i) tailE += static_cast<double>(ir_[0][i]) * ir_[0][i];
                const float tapLevel = static_cast<float>(std::sqrt(tailE / sp.taps) * sp.erLevel * 2.0);
                for (int c = 0; c < 2; ++c)
                    for (int q = 0; q < sp.taps; ++q) {
                        const float pos = std::pow((static_cast<float>(q) + 0.5f + 0.4f * rnd()) / static_cast<float>(sp.taps), 1.3f);
                        const auto at = static_cast<std::size_t>((sp.preMs + pos * sp.erMs) * 0.001 * fs_);
                        const float g = tapLevel * (1.0f - 0.6f * pos) * (rnd() < 0.0f ? -1.0f : 1.0f);
                        if (at + 1 < len) {
                            ir_[c][at] += 0.6f * g;
                            ir_[c][at + 1] += 0.4f * g;
                        }
                    }
            }
        }
        // energy 1 per side
        for (auto& v : ir_) {
            double e = 0.0;
            for (std::size_t i = 0; i < len; ++i) e += static_cast<double>(v[i]) * v[i];
            const auto sc = static_cast<float>(e > 0.0 ? 1.0 / std::sqrt(e) : 0.0);
            for (std::size_t i = 0; i < len; ++i) v[i] *= sc;
        }
        return len;
    }

    double fs_ = 48000.0;
    int maxParts_ = 1, head_ = 0, pos_ = 0, fade_ = 0;
    Slot slot_[2];
    std::atomic<int> active_{0}, state_{kIdle};
    Key lastKey_{};
    std::vector<float> fdlRe_[2], fdlIm_[2], accRe_[2], accIm_[2], accRe2_[2], accIm2_[2], in_[2], out_[2], outNew_[2];
    std::vector<cf> z_, zw_;
    hc::fx::Fft fftA_, fftW_;   // the audio thread's, the background thread's
    std::vector<float> ir_[2];
    std::vector<double> edc_;
    std::array<std::array<SpringLoop, 3>, 2> springs_;
    hc::fx::DelayLine pre_[2], dry_[2];
    hc::fx::Smoothed mix_, out_g_, preS_, dryS_;
    std::jthread worker_;
};

} // namespace
} // namespace fingerprint

std::unique_ptr<hc::fx::Effect> makeFingerprint() { return std::make_unique<fingerprint::Impl>(); }

} // namespace hem::rack
