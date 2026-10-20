// TORCH — guitar / bass amp with cabinet (see Streets.hpp).
//
// Per channel: gate -> input filter -> 4x oversampled preamp stages (asymmetric soft clipping, a
// high-pass between stages) and a softer power stage -> tone stack (bass / mid / treble, presence)
// -> cabinet (a filter model of the speaker and box) -> a little room -> master.
#include "hem/rack/Streets.hpp"

#include "../tone/ToneCommon.hpp"

namespace hem::rack {
namespace torch {
namespace {
using namespace tone;

const char* const kModelN[] = {"CLEAN", "CRUNCH", "LEAD", "BASS"};
const char* const kCabN[] = {"1X12", "4X12", "8X10", "OFF"};
const ParamInfo kParams[kCount] = {
    {"model", "Model", "", 0, 3, 1, Taper::Choice, kModelN},
    {"gate", "Gate", "dB", -80.0f, -20.0f, -80.0f},
    {"gain", "Gain", "%", 0.0f, 100.0f, 40.0f},
    {"bass", "Bass", "dB", -12.0f, 12.0f, 0.0f},
    {"mid", "Mid", "dB", -12.0f, 12.0f, 0.0f},
    {"treble", "Treble", "dB", -12.0f, 12.0f, 0.0f},
    {"presence", "Presence", "%", 0.0f, 100.0f, 30.0f},
    {"cabinet", "Cabinet", "", 0, 3, 1, Taper::Choice, kCabN},
    {"room", "Room", "%", 0.0f, 100.0f, 15.0f},
    {"master", "Master", "dB", -24.0f, 12.0f, 0.0f},
};
struct AmpModel {
    double inHp, preHz, preDb, midHz;
    float gainLo, gainHi, bias;   // preamp gain range (dB) and how lopsided the clipping is
    int stages;
};
constexpr AmpModel kModels[4] = {
    {60.0, 800.0, 0.0, 600.0, 0.0f, 18.0f, 0.05f, 1},
    {90.0, 900.0, 3.0, 650.0, 8.0f, 34.0f, 0.12f, 2},
    {110.0, 800.0, 6.0, 800.0, 14.0f, 46.0f, 0.15f, 3},
    {30.0, 500.0, 0.0, 500.0, 0.0f, 24.0f, 0.08f, 1},
};
constexpr int kTaps = 4;
constexpr float kTapMs[kTaps] = {3.1f, 5.3f, 7.9f, 11.3f};
constexpr float kTapGain[kTaps] = {0.30f, 0.22f, 0.16f, 0.11f};

float stage(float x, float bias) noexcept { return softClip(x + bias) - softClip(bias); }

class Impl final : public hc::fx::Effect {
public:
    Impl() { setDefaults(); }
    std::span<const ParamInfo> params() const noexcept override { return kParams; }
    std::uint32_t latency() const noexcept override { return OS::latency(); }

    void prepare(double fs, std::uint32_t) override {
        fs_ = fs;
        for (auto& d : room_) d.prepare(static_cast<std::uint32_t>(0.015 * fs) + 8);
        // one-pole high-pass between the preamp stages, at the 4x rate
        interK_ = static_cast<float>(std::exp(-2.0 * std::numbers::pi * 140.0 / (fs * OS::kFactor)));
        reset();
    }
    void reset() noexcept override {
        for (auto& o : os_) o.clear();
        for (auto& ch : pre_) for (auto& b : ch) b.clear();
        for (auto& ch : post_) for (auto& b : ch) b.clear();
        for (auto& d : room_) d.clear();
        for (auto& ch : inter_) for (auto& v : ch) v = 0.0f;
        gate_.clear();
        key_.fill(-1e9f);
        dcL_ = dcR_ = 0.0f;
        master_.prepare(fs_, 30, hc::fx::dbToGain(p(Master)));
        roomAmt_.prepare(fs_, 30, p(Room) * 0.01f);
        design();
    }

    void process(float* L, float* R, std::uint32_t n) noexcept override {
        for (std::uint32_t off = 0; off < n; off += kChunk) chunk(L + off, R + off, std::min(kChunk, n - off));
    }

private:
    static constexpr std::uint32_t kChunk = 64;

    void chunk(float* L, float* R, std::uint32_t n) noexcept {
        design();
        const AmpModel& m = kModels[model_];
        const float g1 = hc::fx::dbToGain(m.gainLo + (m.gainHi - m.gainLo) * p(Gain) * 0.01f);
        // one clean stage follows its gain; stacked stages clip to about the same level whatever the gain
        const float makeup = m.stages == 1 ? 1.0f / std::sqrt(g1) : 0.35f;
        const bool gateOn = p(Gate) > -79.5f;
        const float gEnvRel = coefMs(fs_, 8.0), gAtt = coefMs(fs_, 0.5), gRel = coefMs(fs_, 80.0);
        const auto holdN = static_cast<std::uint32_t>(0.03 * fs_);
        float* io[2] = {L, R};
        // gate (both sides together) and the input filters, at the normal rate
        for (std::uint32_t i = 0; i < n; ++i) {
            const float gg = hc::fx::dbToGain(gate_.tick(std::max(std::fabs(L[i]), std::fabs(R[i])), gateOn, p(Gate), -70.0f, holdN, gEnvRel, gAtt, gRel));
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i] * gg;
                for (auto& b : pre_[c]) x = b.tick(x);
                io[c][i] = x;
            }
        }
        // the preamp and power stages at 4x
        float x4[kChunk * OS::kFactor];
        for (int c = 0; c < 2; ++c) {
            os_[c].up(io[c], x4, n);
            float* hp = inter_[c];
            for (std::uint32_t j = 0; j < n * OS::kFactor; ++j) {
                float v = stage(x4[j] * g1, m.bias);
                for (int s = 1; s < m.stages; ++s) {
                    // the coupling between stages takes the lows out, then the next stage clips again
                    const float lp = v + interK_ * (hp[s] - v);
                    hp[s] = lp;
                    v = stage((v - lp) * 2.0f, m.bias * 0.7f);
                }
                x4[j] = softClip(v * 1.4f) / 1.4f;   // the power stage: softer, rounder
            }
            os_[c].down(x4, io[c], n);
        }
        // tone stack, presence, cabinet, room, master
        for (std::uint32_t i = 0; i < n; ++i) {
            const float rm = roomAmt_.next(p(Room) * 0.01f), mg = master_.next(hc::fx::dbToGain(p(Master)));
            for (int c = 0; c < 2; ++c) {
                float x = io[c][i] * makeup;
                for (auto& b : post_[c]) x = b.tick(x);
                room_[c].write(x);
                float r = 0.0f;
                for (int k = 0; k < kTaps; ++k) r += kTapGain[k] * room_[c].read(kTapMs[k] * 0.001f * static_cast<float>(fs_) * (c == 0 ? 1.0f : 1.13f));
                float& dc = c == 0 ? dcL_ : dcR_;
                x += rm * r;
                dc += (x - dc) * 0.0005f;
                io[c][i] = (x - dc) * mg;
            }
        }
    }

    void design() noexcept {
        const std::array<float, 6> k{p(Model), p(Bass), p(Mid), p(Treble), p(Presence), p(Cabinet)};
        if (k == key_) return;
        key_ = k;
        model_ = std::clamp(static_cast<int>(std::lround(p(Model))), 0, 3);
        const AmpModel& m = kModels[model_];
        const double top = fs_ * 0.45;
        const C id{};
        const C inHp = C::make(T::HighPass, fs_, m.inHp, 0.7071);
        const C pre = m.preDb != 0.0 ? C::make(T::Bell, fs_, m.preHz, 0.7, m.preDb) : id;
        for (auto& ch : pre_) { ch[0].c = inHp; ch[1].c = pre; }
        // tone stack and presence
        std::array<C, kPost> post{};
        post[0] = C::make(T::LowShelf, fs_, 120.0, 0.7071, p(Bass));
        post[1] = C::make(T::Bell, fs_, m.midHz, 0.7, p(Mid));
        post[2] = C::make(T::HighShelf, fs_, std::min(2800.0, top), 0.7071, p(Treble));
        post[3] = C::make(T::HighShelf, fs_, std::min(4500.0, top), 0.7071, 8.0 * p(Presence) * 0.01);
        // the cabinet
        const int cab = std::clamp(static_cast<int>(std::lround(p(Cabinet))), 0, 3);
        for (int i = 4; i < kPost; ++i) post[static_cast<std::size_t>(i)] = id;
        if (cab == 0) {   // open back 1x12: looser lows, a bright edge
            post[4] = C::make(T::HighPass, fs_, 75.0, 1.0);
            post[5] = C::make(T::Bell, fs_, 2400.0, 1.2, 3.0);
            post[6] = C::make(T::Bell, fs_, std::min(4000.0, top), 2.0, -5.0);
            post[7] = C::make(T::LowPass, fs_, std::min(5500.0, top), 0.6);
            post[8] = C::make(T::LowPass, fs_, std::min(5500.0, top), 1.0);
        } else if (cab == 1) {   // closed 4x12: tight thump, darker top
            post[4] = C::make(T::HighPass, fs_, 70.0, 1.2);
            post[5] = C::make(T::Bell, fs_, 100.0, 1.0, 3.0);
            post[6] = C::make(T::Bell, fs_, std::min(3200.0, top), 1.5, -4.0);
            post[7] = C::make(T::LowPass, fs_, std::min(4800.0, top), 0.6);
            post[8] = C::make(T::LowPass, fs_, std::min(4800.0, top), 1.0);
        } else if (cab == 2) {   // bass 8x10: deep, no fizz
            post[4] = C::make(T::HighPass, fs_, 40.0, 0.9);
            post[5] = C::make(T::Bell, fs_, 70.0, 1.0, 3.0);
            post[6] = C::make(T::Bell, fs_, 800.0, 1.0, -3.0);
            post[7] = C::make(T::LowPass, fs_, std::min(3500.0, top), 0.6);
            post[8] = C::make(T::LowPass, fs_, std::min(3500.0, top), 1.0);
        }
        for (auto& ch : post_) for (int i = 0; i < kPost; ++i) ch[static_cast<std::size_t>(i)].c = post[static_cast<std::size_t>(i)];
    }

    static constexpr int kPost = 9;
    double fs_ = 48000.0;
    OS os_[2];
    std::array<std::array<hc::fx::Biquad, 2>, 2> pre_{};
    std::array<std::array<hc::fx::Biquad, kPost>, 2> post_{};
    hc::fx::DelayLine room_[2];
    float inter_[2][4]{};
    float interK_ = 0.0f, dcL_ = 0.0f, dcR_ = 0.0f;
    GateStage gate_;
    int model_ = 1;
    std::array<float, 6> key_{};
    hc::fx::Smoothed master_, roomAmt_;
};

} // namespace
} // namespace torch

std::unique_ptr<hc::fx::Effect> makeTorch() { return std::make_unique<torch::Impl>(); }

} // namespace hem::rack
