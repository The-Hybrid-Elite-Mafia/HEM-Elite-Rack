// THE MOLD and THE FORGER — the EQ cloner (see Clone.hpp).
//
// THE MOLD's test signal is a multisine: every FFT bin from 20 Hz to 20 kHz of an 8192-sample period, all at the
// same level, with random phases, so one period of the return, divided by one period of what was sent, is the
// loop's response (its delay included, as a phase slope). Periods are averaged (each new one counts half), the
// delay is read off the response's impulse, and the response's level is averaged into 120 bands, 25 Hz .. 20 kHz:
// that curve goes on a board every FORGER reads.
// THE FORGER turns a curve into a minimum-phase FIR (the way an analogue EQ plays it: log magnitude -> real
// cepstrum, folded -> exp), 4096 taps, and plays it by partitioned FFT convolution (256-sample blocks). A new
// FIR is made by a background thread into the spare of two slots; the audio thread crossfades to it over two
// blocks. The held curve is the plug-in's own 120 parameters, so it is saved with the session.
#include "hem/rack/Clone.hpp"

#include "ToneCommon.hpp"

#include <atomic>
#include <chrono>
#include <complex>
#include <thread>
#include <vector>

#include "hybrid/fx/Fft.hpp"

namespace hem::rack {
using cf = std::complex<float>;

// ---- the board ---------------------------------------------------------------------------------------------
namespace clone {
double pointHz(int i) noexcept { return kLoHz * std::pow(kHiHz / kLoHz, static_cast<double>(i) / (kPoints - 1)); }
namespace {
struct Board {
    std::atomic<std::uint32_t> seq{0};
    float db[2][kPoints]{};
    std::atomic<std::int64_t> stampMs{-1000000};
};
Board gBoard;
std::int64_t nowMs() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void publish(const float* db) noexcept {
    const std::uint32_t s = gBoard.seq.load(std::memory_order_relaxed) + 1;
    float* dst = gBoard.db[s & 1u];
    for (int i = 0; i < kPoints; ++i) dst[i] = db[i];
    gBoard.seq.store(s, std::memory_order_release);
    gBoard.stampMs.store(nowMs(), std::memory_order_relaxed);
}
} // namespace
bool latestCurve(float* db, std::uint32_t* serial) noexcept {
    for (int tries = 0; tries < 4; ++tries) {
        const std::uint32_t s = gBoard.seq.load(std::memory_order_acquire);
        if (s == 0) return false;
        const float* src = gBoard.db[s & 1u];
        for (int i = 0; i < kPoints; ++i) db[i] = src[i];
        if (gBoard.seq.load(std::memory_order_acquire) < s + 2) {   // the slot read was not rewritten meanwhile
            if (serial) *serial = s;
            return nowMs() - gBoard.stampMs.load(std::memory_order_relaxed) < 500;
        }
    }
    return false;
}
} // namespace clone

namespace {
using namespace tone;

// ---- THE MOLD -------------------------------------------------------------------------------------------------
namespace mo {
using namespace mold;
const char* const kOutN[] = {"OUT 3", "OUT 4", "OUT 5", "OUT 6", "OUT 7", "OUT 8", "OUT 9", "OUT 10", "OUT 11", "OUT 12", "OUT 13", "OUT 14", "OUT 15", "OUT 16"};
const char* const kInN[] = {"IN 1", "IN 2", "IN 3", "IN 4", "IN 5", "IN 6", "IN 7", "IN 8", "IN 9", "IN 10", "IN 11", "IN 12", "IN 13", "IN 14", "IN 15", "IN 16"};
const ParamInfo kParams[kCount] = {
    {"headroom", "Headroom", "dB", 0.0f, 30.0f, 14.0f},
    {"sendout", "Send Out", "", 0, 13, 0, Taper::Choice, kOutN},
    {"returnin", "Return In", "", 0, 15, 0, Taper::Choice, kInN},
    {"clear", "Clear Clips", "", 0, 1, 0, Taper::Toggle, kOnOff},
    {"transmit", "Transmit", "", 0, 1, 0, Taper::Toggle, kOnOff},
};
const hc::fx::MeterInfo kMeterInfo[kMeters] = {{"SEND", "dBFS", -60.0f, 0.0f}, {"RETURN", "dBFS", -60.0f, 0.0f}, {"CLIP SEND", "", 0.0f, 1.0f},
                                              {"CLIP RETURN", "", 0.0f, 1.0f}, {"LOOP", "ms", 0.0f, 170.0f}, {"RECEIVING", "", 0.0f, 1.0f}};
constexpr int kP = 8192;   // the test signal's period

class Impl final : public hc::fx::Effect, public HardwareLoop {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }
    int sendOutput() const noexcept override { return 2 + std::clamp(static_cast<int>(std::lround(p(SendOut))), 0, 13); }
    int returnInput() const noexcept override { return std::clamp(static_cast<int>(std::lround(p(ReturnIn))), 0, 15); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        fft_.prepare(kP);
        z_.assign(kP, cf{});
        x_.assign(kP, cf{});
        h_.assign(kP, cf{});
        test_.assign(kP, 0.0f);
        ret_.assign(kP, 0.0f);
        // the multisine: every bin 20 Hz .. 20 kHz at one level, random phases
        std::uint32_t rng = 0x5EEDu;
        const int lo = static_cast<int>(std::ceil(20.0 * kP / fs)), hi = std::min(kP / 2 - 1, static_cast<int>(20000.0 * kP / fs));
        for (int k = lo; k <= hi; ++k) {
            rng = rng * 1664525u + 1013904223u;
            const float ph = static_cast<float>(rng >> 8) / 16777216.0f * 2.0f * static_cast<float>(std::numbers::pi);
            x_[static_cast<std::size_t>(k)] = std::polar(1.0f, ph);
            x_[static_cast<std::size_t>(kP - k)] = std::conj(x_[static_cast<std::size_t>(k)]);
        }
        z_ = x_;
        fft_.inverse(z_.data());
        float pk = 0.0f;
        for (int i = 0; i < kP; ++i) pk = std::max(pk, std::fabs(z_[static_cast<std::size_t>(i)].real()));
        scale_ = pk > 0.0f ? 1.0f / pk : 0.0f;   // peaks at full scale before HEADROOM
        for (int i = 0; i < kP; ++i) test_[static_cast<std::size_t>(i)] = z_[static_cast<std::size_t>(i)].real() * scale_;
        loBin_ = lo;
        hiBin_ = hi;
        reset();
    }
    void reset() noexcept override {
        pos_ = 0;
        periods_ = 0;
        std::fill(h_.begin(), h_.end(), cf{});
        std::fill(ret_.begin(), ret_.end(), 0.0f);
        clipS_ = clipR_ = false;
        clearWas_ = p(ClearClips) >= 0.5f;
        routed_ = 0;
        retSq_ = 0.0;
        publishMeter(Receiving, 0.0f);
        publishMeter(LoopMs, 0.0f);
    }

    // the rack app: the sound card's own channels
    void hardware(const float* in, float* out, std::uint32_t n) noexcept override {
        run(in, out, nullptr, n);
        routed_ = n;
    }
    void process(float* L, float* R, std::uint32_t n) noexcept override {
        if (routed_ > 0) { routed_ = 0; return; }   // the music in the slot passes untouched
        // a DAW: the return comes in on the left, the test signal goes out on both sides
        run(L, L, R, n);
    }

private:
    void run(const float* in, float* out, float* out2, std::uint32_t n) noexcept {
        const bool clr = p(ClearClips) >= 0.5f;
        if (clr != clearWas_) { clearWas_ = clr; clipS_ = clipR_ = false; }
        const float g = p(Transmit) >= 0.5f ? hc::fx::dbToGain(-p(Headroom)) : 0.0f;
        float pkS = 0.0f, pkR = 0.0f;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float r = in ? in[i] : 0.0f;
            const float s = test_[static_cast<std::size_t>(pos_)] * g;
            ret_[static_cast<std::size_t>(pos_)] = r;
            retSq_ += static_cast<double>(r) * r;
            if (out) out[i] = s;
            if (out2) out2[i] = s;
            pkS = std::max(pkS, std::fabs(s));
            pkR = std::max(pkR, std::fabs(r));
            if (++pos_ == kP) {
                pos_ = 0;
                period(g);
            }
        }
        if (pkS >= 0.989f) clipS_ = true;
        if (pkR >= 0.989f) clipR_ = true;
        publishMeter(SendDb, std::max(-100.0f, dbOf(pkS)));
        publishMeter(ReturnDb, std::max(-100.0f, dbOf(pkR)));
        publishMeter(ClipSend, clipS_ ? 1.0f : 0.0f);
        publishMeter(ClipReturn, clipR_ ? 1.0f : 0.0f);
    }
    // one whole period came back: the response, the delay, the curve
    void period(float g) noexcept {
        const double rms = std::sqrt(retSq_ / kP);
        retSq_ = 0.0;
        ++periods_;
        if (periods_ < 2 || rms < 1e-4 || g <= 0.0f) {   // the first period is the loop filling up; silence is no answer
            publishMeter(Receiving, 0.0f);
            if (rms < 1e-4) periods_ = 0;
            return;
        }
        for (int i = 0; i < kP; ++i) z_[static_cast<std::size_t>(i)] = cf(ret_[static_cast<std::size_t>(i)], 0.0f);
        fft_.forward(z_.data());
        const float sent = scale_ * g;
        const float w = periods_ == 2 ? 1.0f : 0.5f;
        for (int k = loBin_; k <= hiBin_; ++k) {
            const cf hk = z_[static_cast<std::size_t>(k)] / (x_[static_cast<std::size_t>(k)] * sent);
            cf& a = h_[static_cast<std::size_t>(k)];
            a += w * (hk - a);
        }
        // the delay: where the response's impulse peaks
        for (int k = 0; k < kP; ++k) z_[static_cast<std::size_t>(k)] = cf{};
        for (int k = loBin_; k <= hiBin_; ++k) {
            z_[static_cast<std::size_t>(k)] = h_[static_cast<std::size_t>(k)];
            z_[static_cast<std::size_t>(kP - k)] = std::conj(h_[static_cast<std::size_t>(k)]);
        }
        fft_.inverse(z_.data());
        int at = 0;
        float best = 0.0f, total = 0.0f;
        for (int i = 0; i < kP; ++i) {
            const float v = z_[static_cast<std::size_t>(i)].real() * z_[static_cast<std::size_t>(i)].real();
            total += v;
            if (v > best) { best = v; at = i; }
        }
        float near = 0.0f;
        for (int d = -64; d <= 2048; ++d) {
            const float v = z_[static_cast<std::size_t>(((at + d) % kP + kP) % kP)].real();
            near += v * v;
        }
        const bool clean = total > 0.0f && near / total > 0.5f;   // it is our own signal coming back
        publishMeter(LoopMs, static_cast<float>(at * 1000.0 / fs_));
        publishMeter(Receiving, clean ? 1.0f : 0.0f);
        if (!clean) return;
        // the curve: the level in 120 bands
        float db[clone::kPoints];
        for (int i = 0; i < clone::kPoints; ++i) {
            const double f = clone::pointHz(i), half = std::pow(clone::kHiHz / clone::kLoHz, 0.5 / (clone::kPoints - 1));
            int k0 = static_cast<int>(std::floor(f / half * kP / fs_)), k1 = static_cast<int>(std::ceil(f * half * kP / fs_));
            k0 = std::clamp(k0, loBin_, hiBin_);
            k1 = std::clamp(std::max(k1, k0), loBin_, hiBin_);
            double e = 0.0;
            for (int k = k0; k <= k1; ++k) e += std::norm(h_[static_cast<std::size_t>(k)]);
            e /= (k1 - k0 + 1);
            db[i] = static_cast<float>(std::clamp(10.0 * std::log10(std::max(e, 1e-12)), -clone::kCurveDb * 1.0, clone::kCurveDb * 1.0));
        }
        clone::publish(db);
    }

    double fs_ = 48000.0, retSq_ = 0.0;
    hc::fx::Fft fft_;
    std::vector<cf> z_, x_, h_;
    std::vector<float> test_, ret_;
    float scale_ = 1.0f;
    int pos_ = 0, periods_ = 0, loBin_ = 1, hiBin_ = 1;
    bool clipS_ = false, clipR_ = false, clearWas_ = false;
    std::uint32_t routed_ = 0;
};
} // namespace mo

// ---- THE FORGER -----------------------------------------------------------------------------------------------
namespace fo {
using namespace forger;
const char* const kModeN[] = {"CAPTURE", "HOLD", "ADD"};
const char* const kScaleN[] = {"16 DB", "30 DB"};
#define HEM_C(n) {"c" #n, "Curve " #n, "dB", -30.0f, 30.0f, 0.0f}
#define HEM_C10(a) HEM_C(a##0), HEM_C(a##1), HEM_C(a##2), HEM_C(a##3), HEM_C(a##4), HEM_C(a##5), HEM_C(a##6), HEM_C(a##7), HEM_C(a##8), HEM_C(a##9)
const ParamInfo kParams[kCount] = {
    {"mode", "Mode", "", 0, 2, 1, Taper::Choice, kModeN},
    {"gain", "Gain", "dB", -48.0f, 12.0f, 0.0f},
    {"scale", "Scale", "", 0, 1, 0, Taper::Choice, kScaleN},
    {"clear", "Clear", "", 0, 1, 0, Taper::Toggle, kOnOff},
    HEM_C10(00), HEM_C10(01), HEM_C10(02), HEM_C10(03), HEM_C10(04), HEM_C10(05),
    HEM_C10(06), HEM_C10(07), HEM_C10(08), HEM_C10(09), HEM_C10(10), HEM_C10(11),
};
#undef HEM_C
#undef HEM_C10
const hc::fx::MeterInfo kMeterInfo[kMeters] = {{"TRIM", "dB", 0.0f, 48.0f}, {"RECEIVING", "", 0.0f, 1.0f}};

constexpr int kB = 256, kN = 2 * kB, kTaps = 4096, kParts = kTaps / kB, kM = 8192, kFade = 2;

struct Slot { std::vector<float> re, im; };

class Impl final : public hc::fx::Effect, public CloneSource {
public:
    Impl() {
        setDefaults();
        for (auto& v : shown_) v.store(0.0f, std::memory_order_relaxed);
    }
    ~Impl() override { stopWorker(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::span<const hc::fx::MeterInfo> meters() const noexcept override { return kMeterInfo; }
    std::uint32_t latency() const noexcept override { return kB; }
    bool paramActive(int i) const noexcept override { return i < Curve0; }
    float cloneDb(int i) const noexcept override { return i >= 0 && i < clone::kPoints ? shown_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed) : 0.0f; }

    void prepare(double fs, std::uint32_t) override {
        stopWorker();
        fs_ = fs;
        for (auto& s : slot_) { s.re.assign(static_cast<std::size_t>(kParts) * kN, 0.0f); s.im = s.re; }
        fdlRe_.assign(static_cast<std::size_t>(kParts) * kN, 0.0f);
        fdlIm_ = fdlRe_;
        in_.assign(kN, cf{});
        hist_.assign(kN, cf{});
        out_.assign(kB, cf{});
        outNew_.assign(kB, cf{});
        fftA_.prepare(kN);
        fftW_.prepare(kN);
        fftM_.prepare(kM);
        wz_.assign(kM, cf{});
        wn_.assign(kN, cf{});
        // the first FIR now: the curve as the settings say
        float c[clone::kPoints];
        wantCurve(c, true);
        build(slot_[0], c);
        lastBuilt_.assign(c, c + clone::kPoints);
        active_.store(0, std::memory_order_relaxed);
        state_.store(kIdle, std::memory_order_relaxed);
        reset();
        startWorker();
    }
    void reset() noexcept override {
        std::fill(fdlRe_.begin(), fdlRe_.end(), 0.0f);
        std::fill(fdlIm_.begin(), fdlIm_.end(), 0.0f);
        std::fill(hist_.begin(), hist_.end(), cf{});
        std::fill(out_.begin(), out_.end(), cf{});
        std::fill(outNew_.begin(), outNew_.end(), cf{});
        head_ = pos_ = fade_ = 0;
        modeWas_ = pick(Mode);
        clearWas_ = p(Clear) >= 0.5f;
        gain_.prepare(fs_, 20, hc::fx::dbToGain(p(Gain)));
        peak_ = 0.0f;
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        modes();
        const float wGain = hc::fx::dbToGain(p(Gain));
        const float fall = std::exp(-1.0f / (3.0f * static_cast<float>(fs_)));   // the trim reading's memory: 3 s
        for (std::uint32_t i = 0; i < n; ++i) {
            hist_[static_cast<std::size_t>(kB + pos_)] = cf(L[i], R[i]);
            cf y = out_[static_cast<std::size_t>(pos_)];
            if (fade_ > 0) {
                const float t = (static_cast<float>((kFade - fade_) * kB + pos_) + 0.5f) / static_cast<float>(kFade * kB);
                y += t * (outNew_[static_cast<std::size_t>(pos_)] - y);
            }
            const float g = gain_.next(wGain);
            L[i] = y.real() * g;
            R[i] = y.imag() * g;
            peak_ = std::max(peak_ * fall, std::max(std::fabs(L[i]), std::fabs(R[i])));
            if (++pos_ == kB) {
                pos_ = 0;
                block();
            }
        }
        publishMeter(Trim, std::clamp(-dbOf(peak_), 0.0f, 48.0f));
        float tmp[clone::kPoints];
        publishMeter(Receiving, clone::latestCurve(tmp, nullptr) ? 1.0f : 0.0f);
    }

private:
    enum State : int { kIdle, kReady, kFading };
    int pick(int i) const noexcept { return std::clamp(static_cast<int>(std::lround(p(i))), 0, 2); }

    // MODE changes and CLEAR (audio thread): what HOLD keeps is written into the curve parameters
    void modes() noexcept {
        const int mode = pick(Mode);
        const bool clr = p(Clear) >= 0.5f;
        if (clr != clearWas_) {
            clearWas_ = clr;
            for (int i = 0; i < clone::kPoints; ++i) set(Curve0 + i, 0.0f);
        }
        if (mode == modeWas_) return;
        float board[clone::kPoints];
        const bool have = clone::latestCurve(board, nullptr) || haveBoard_;
        if (!have) for (float& v : board) v = 0.0f;
        if (modeWas_ == Capture && mode != Capture && have) {   // keep what was being captured
            for (int i = 0; i < clone::kPoints; ++i) set(Curve0 + i, std::clamp(board[i], -clone::kCurveDb, clone::kCurveDb));
        } else if (modeWas_ == Add && mode != Add && have) {    // keep the sum
            for (int i = 0; i < clone::kPoints; ++i) set(Curve0 + i, std::clamp(p(Curve0 + i) + board[i], -clone::kCurveDb, clone::kCurveDb));
        }
        modeWas_ = mode;
    }

    // the curve the settings ask for now
    void wantCurve(float* c, bool initial) noexcept {
        const int mode = pick(Mode);
        float board[clone::kPoints];
        const bool fresh = clone::latestCurve(board, nullptr);
        if (fresh) {
            std::copy(board, board + clone::kPoints, lastBoard_);
            haveBoard_ = true;
        }
        for (int i = 0; i < clone::kPoints; ++i) {
            const float held = p(Curve0 + i);
            if (mode == Capture && haveBoard_ && !initial) c[i] = lastBoard_[i];
            else if (mode == Add && haveBoard_ && !initial) c[i] = std::clamp(held + lastBoard_[i], -2.0f * clone::kCurveDb, 2.0f * clone::kCurveDb);
            else c[i] = held;
        }
    }

    // ---- audio thread: one block -----------------------------------------------------------------------------
    void block() noexcept {
        if (fade_ == 0 && state_.load(std::memory_order_acquire) == kReady) {
            state_.store(kFading, std::memory_order_relaxed);
            fade_ = kFade + 1;
        }
        std::copy(hist_.begin(), hist_.end(), in_.begin());
        fftA_.forward(in_.data());   // left + i right: both sides in one FFT (the FIR is the same, and real)
        head_ = (head_ + kParts - 1) % kParts;
        const std::size_t h0 = static_cast<std::size_t>(head_) * kN;
        for (int k = 0; k < kN; ++k) {
            fdlRe_[h0 + static_cast<std::size_t>(k)] = in_[static_cast<std::size_t>(k)].real();
            fdlIm_[h0 + static_cast<std::size_t>(k)] = in_[static_cast<std::size_t>(k)].imag();
        }
        std::copy(hist_.begin() + kB, hist_.end(), hist_.begin());
        const int act = active_.load(std::memory_order_relaxed);
        convolve(slot_[act], out_);
        if (fade_ > 0) {
            convolve(slot_[1 - act], outNew_);
            if (--fade_ == 0) {
                active_.store(1 - act, std::memory_order_release);
                std::copy(outNew_.begin(), outNew_.end(), out_.begin());
                state_.store(kIdle, std::memory_order_release);
            }
        }
    }
    void convolve(const Slot& s, std::vector<cf>& out) noexcept {
        std::fill(accRe_.begin(), accRe_.end(), 0.0f);
        std::fill(accIm_.begin(), accIm_.end(), 0.0f);
        float* ar = accRe_.data();
        float* ai = accIm_.data();
        for (int j = 0; j < kParts; ++j) {
            const std::size_t xo = static_cast<std::size_t>((head_ + j) % kParts) * kN, ho = static_cast<std::size_t>(j) * kN;
            const float* xr = fdlRe_.data() + xo;
            const float* xi = fdlIm_.data() + xo;
            const float* hr = s.re.data() + ho;
            const float* hi = s.im.data() + ho;
            for (int k = 0; k < kN; ++k) {
                ar[k] += xr[k] * hr[k] - xi[k] * hi[k];
                ai[k] += xr[k] * hi[k] + xi[k] * hr[k];
            }
        }
        for (int k = 0; k < kN; ++k) wa_[static_cast<std::size_t>(k)] = cf(ar[k], ai[k]);
        fftA_.inverse(wa_.data());
        for (int k = 0; k < kB; ++k) out[static_cast<std::size_t>(k)] = wa_[static_cast<std::size_t>(kB + k)];
    }

    // ---- the background thread: a new FIR when the curve moves -------------------------------------------------
    void startWorker() {
        worker_ = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                float c[clone::kPoints];
                wantCurve(c, false);
                for (int i = 0; i < clone::kPoints; ++i) shown_[static_cast<std::size_t>(i)].store(c[i], std::memory_order_relaxed);
                if (state_.load(std::memory_order_acquire) != kIdle) continue;
                bool moved = false;
                for (int i = 0; i < clone::kPoints; ++i) moved = moved || std::fabs(c[i] - lastBuilt_[static_cast<std::size_t>(i)]) > 0.01f;
                if (!moved) continue;
                const int spare = 1 - active_.load(std::memory_order_acquire);
                build(slot_[spare], c);
                std::copy(c, c + clone::kPoints, lastBuilt_.begin());
                state_.store(kReady, std::memory_order_release);
            }
        });
    }
    void stopWorker() {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
    }
    // the curve -> a minimum-phase FIR -> its partition spectra
    void build(Slot& s, const float* curve) noexcept {
        const double lnPerDb = std::log(10.0) / 20.0;
        const double l0 = std::log(clone::kLoHz), step = (std::log(clone::kHiHz) - l0) / (clone::kPoints - 1);
        for (int k = 0; k <= kM / 2; ++k) {
            const double f = std::max(1.0, k * fs_ / kM);
            const double x = std::clamp((std::log(f) - l0) / step, 0.0, static_cast<double>(clone::kPoints - 1));
            const int i0 = std::min(static_cast<int>(x), clone::kPoints - 2);
            const double t = x - i0, db = curve[i0] + t * (curve[i0 + 1] - curve[i0]);
            wz_[static_cast<std::size_t>(k)] = cf(static_cast<float>(db * lnPerDb), 0.0f);
            if (k > 0 && k < kM / 2) wz_[static_cast<std::size_t>(kM - k)] = wz_[static_cast<std::size_t>(k)];
        }
        fftM_.inverse(wz_.data());   // the real cepstrum
        for (int i = 1; i < kM / 2; ++i) wz_[static_cast<std::size_t>(i)] *= 2.0f;
        for (int i = kM / 2 + 1; i < kM; ++i) wz_[static_cast<std::size_t>(i)] = cf{};
        fftM_.forward(wz_.data());
        for (auto& v : wz_) v = std::exp(v);
        fftM_.inverse(wz_.data());   // the minimum-phase impulse
        for (int j = 0; j < kParts; ++j) {
            for (int i = 0; i < kN; ++i) {
                const int at = j * kB + i;
                float h = 0.0f;
                if (i < kB) {
                    h = wz_[static_cast<std::size_t>(at)].real();
                    if (at >= kTaps - 512) h *= 0.5f + 0.5f * std::cos(static_cast<float>(std::numbers::pi) * static_cast<float>(at - (kTaps - 512)) / 512.0f);
                }
                wn_[static_cast<std::size_t>(i)] = cf(h, 0.0f);
            }
            fftW_.forward(wn_.data());
            for (int k = 0; k < kN; ++k) {
                s.re[static_cast<std::size_t>(j) * kN + static_cast<std::size_t>(k)] = wn_[static_cast<std::size_t>(k)].real();
                s.im[static_cast<std::size_t>(j) * kN + static_cast<std::size_t>(k)] = wn_[static_cast<std::size_t>(k)].imag();
            }
        }
    }

    double fs_ = 48000.0;
    Slot slot_[2];
    std::atomic<int> active_{0}, state_{kIdle};
    std::vector<float> fdlRe_, fdlIm_;
    std::vector<float> accRe_ = std::vector<float>(kN), accIm_ = std::vector<float>(kN);
    std::vector<cf> in_, hist_, out_, outNew_, wz_, wn_;
    std::vector<cf> wa_ = std::vector<cf>(kN);
    hc::fx::Fft fftA_, fftW_, fftM_;
    int head_ = 0, pos_ = 0, fade_ = 0, modeWas_ = Hold;
    bool clearWas_ = false;
    std::atomic<bool> haveBoard_{false};
    float lastBoard_[clone::kPoints]{};
    std::vector<float> lastBuilt_ = std::vector<float>(clone::kPoints, 0.0f);
    std::array<std::atomic<float>, clone::kPoints> shown_;
    hc::fx::Smoothed gain_;
    float peak_ = 0.0f;
    std::jthread worker_;
};
} // namespace fo

} // namespace

std::unique_ptr<hc::fx::Effect> makeMold() { return std::make_unique<mo::Impl>(); }
std::unique_ptr<hc::fx::Effect> makeForger() { return std::make_unique<fo::Impl>(); }

} // namespace hem::rack
