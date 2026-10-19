// =============================================================================
//  HEM-Elite-Rack.vst3 — ELITE EQ and RESTORER as VST3 plug-ins (one bundle, one plug-in each),
//  with the same teal / gunmetal faceplates as the rack app (hem::ui::FaceWindow, OpenGL, inside
//  the host's window).
//
//  Per plug-in: stereo in / out; its parameters (ids 0..n-1, same ranges and text as the app);
//  a Bypass parameter (latency-matched, 10 ms crossfade, no click). What the faceplate shows
//  besides the knobs (before / after spectrum, RESTORER's noise curve, ELITE EQ's dynamic gains
//  and auto gain) travels from the processor to the editor as hidden read-only output
//  parameters, about 30 times a second, so it works the VST3 way in every host.
//  Steinberg VST3 SDK: MIT licence (verified from the SDK's LICENSE.txt, v3.8.1). No ASIO.
// =============================================================================
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Spectrum.hpp"
#include "hem/ui/Face.hpp"
#include "hem/ui/FaceWindow.hpp"
#include "hybrid/fx/Dsp.hpp"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/common/pluginview.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef HEM_RACK_VERSION_STR
#define HEM_RACK_VERSION_STR "0.0.0"
#endif

using namespace Steinberg;

namespace hem::vst {

// Parameter ids beyond the plug-in's own (0..n-1).
constexpr Vst::ParamID kBypassId   = 1000;
constexpr Vst::ParamID kRateId     = 1001;   // sample rate / 384000 (the EQ curve is drawn for it)
constexpr Vst::ParamID kAutoGainId = 1002;   // ELITE EQ auto gain, -24..24 dB
constexpr Vst::ParamID kRemovedId  = 1003;   // RESTORER: dB taken out, -60..0
constexpr Vst::ParamID kDynBase    = 1100;   // + band: ELITE EQ dynamic gain, -30..30 dB
constexpr Vst::ParamID kBeforeBase = 1200;   // + i: spectrum before (kSent bins)
constexpr Vst::ParamID kAfterBase  = 1300;   // + i: spectrum after
constexpr Vst::ParamID kNoiseBase  = 1400;   // + i: RESTORER noise curve
constexpr int kSent = rack::kSpectrumBins / 2;   // every other display bin; the editor interpolates
static_assert(kSent <= 100);

float dbToNorm(float db, float lo, float hi) { return std::clamp((db - lo) / (hi - lo), 0.0f, 1.0f); }
float normToDb(double n, float lo, float hi) { return lo + static_cast<float>(n) * (hi - lo); }
constexpr float kSpecLo = -120.0f, kSpecHi = 0.0f;

// <bundle>/Contents/x86_64-win/<dll> -> <bundle>/Contents/Resources (fonts/, faceplates/, brand/).
std::filesystem::path bundleResources() {
    HMODULE m = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&bundleResources), &m);
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = ::GetModuleFileNameW(m, buf, static_cast<DWORD>(std::size(buf)));
    return std::filesystem::path(std::wstring(buf, n)).parent_path().parent_path() / "Resources";
}

FUID toFuid(const std::uint32_t (&u)[4]) { return FUID(u[0], u[1], u[2], u[3]); }
std::u16string toU16(const std::string& s) { return std::u16string(s.begin(), s.end()); }   // ASCII names

// ---- state: "HEMRACK1\n" then id=value lines (plain values), bypass=0/1 ---------------------
constexpr char kStateMagic[] = "HEMRACK1\n";
std::string stateText(const hc::fx::Effect& e, bool bypass) {
    std::string out = kStateMagic;
    const auto ps = e.params();
    char b[64];
    for (std::size_t i = 0; i < ps.size(); ++i) {
        std::snprintf(b, sizeof b, "%.9g", static_cast<double>(e.get(static_cast<int>(i))));
        out += std::string(ps[i].id) + "=" + b + "\n";
    }
    out += std::string("bypass=") + (bypass ? "1" : "0") + "\n";
    return out;
}
bool writeText(IBStream* s, const std::string& t) {
    if (!s) return false;
    int32 w = 0;
    return s->write(const_cast<char*>(t.data()), static_cast<int32>(t.size()), &w) == kResultOk && w == static_cast<int32>(t.size());
}
bool readState(IBStream* s, hc::fx::Effect& e, bool& bypass) {
    if (!s) return false;
    std::string text;
    char buf[4096];
    for (;;) {
        int32 got = 0;
        if (s->read(buf, sizeof buf, &got) != kResultOk || got <= 0) break;
        text.append(buf, static_cast<std::size_t>(got));
        if (text.size() > (1u << 18)) return false;   // ELITE EQ's state is ~4 KB; refuse junk
    }
    if (text.compare(0, sizeof(kStateMagic) - 1, kStateMagic) != 0) return false;
    std::istringstream in(text.substr(sizeof(kStateMagic) - 1));
    std::string line;
    const auto ps = e.params();
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq);
        const float v = std::strtof(line.c_str() + eq + 1, nullptr);
        if (k == "bypass") { bypass = v >= 0.5f; continue; }
        for (std::size_t i = 0; i < ps.size(); ++i)
            if (k == ps[i].id) { e.set(static_cast<int>(i), v); break; }   // unknown ids (other versions) are skipped
    }
    return true;
}

// ---- processor -------------------------------------------------------------------------------
class Processor final : public Vst::AudioEffect {
public:
    explicit Processor(const rack::PluginInfo* info) : info_(info), fx_(info->create()) {
        setControllerClass(toFuid(info->controllerUid));
        spectrum_ = dynamic_cast<rack::SpectrumSource*>(fx_.get());
        noise_ = dynamic_cast<rack::NoiseProfileSource*>(fx_.get());
        eq_ = dynamic_cast<rack::EliteEqView*>(fx_.get());
    }
    static FUnknown* createInstance(void* ctx) { return static_cast<Vst::IAudioProcessor*>(new Processor(static_cast<const rack::PluginInfo*>(ctx))); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        const tresult r = AudioEffect::initialize(context);
        if (r != kResultOk) return r;
        addAudioInput(STR16("Stereo In"), Vst::SpeakerArr::kStereo);
        addAudioOutput(STR16("Stereo Out"), Vst::SpeakerArr::kStereo);
        return kResultOk;
    }
    tresult PLUGIN_API setBusArrangements(Vst::SpeakerArrangement* inputs, int32 numIns, Vst::SpeakerArrangement* outputs, int32 numOuts) SMTG_OVERRIDE {
        if (numIns != 1 || numOuts != 1 || inputs[0] != Vst::SpeakerArr::kStereo || outputs[0] != Vst::SpeakerArr::kStereo) return kResultFalse;
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) SMTG_OVERRIDE { return size == Vst::kSample32 ? kResultTrue : kResultFalse; }

    tresult PLUGIN_API setupProcessing(Vst::ProcessSetup& setup) SMTG_OVERRIDE {
        sampleRate_ = setup.sampleRate;
        maxBlock_ = static_cast<std::uint32_t>(std::clamp<int32>(setup.maxSamplesPerBlock, 32, static_cast<int32>(hc::fx::Effect::kMaxBlock)));
        fx_->prepare(sampleRate_, maxBlock_);
        for (auto& d : bypassDelay_) d.prepare(fx_->latency());
        for (auto& b : dry_) b.assign(maxBlock_, 0.0f);
        fadeStep_ = static_cast<float>(1.0 / (0.010 * sampleRate_));
        sendEvery_ = static_cast<std::uint32_t>(sampleRate_ / 30.0);
        return AudioEffect::setupProcessing(setup);
    }
    tresult PLUGIN_API setActive(TBool state) SMTG_OVERRIDE {
        if (state) {
            fx_->reset();
            for (auto& d : bypassDelay_) d.clear();
            mix_ = bypass_ ? 0.0f : 1.0f;
            if (spectrum_) { spectrum_->spectrumBefore().setActive(true); spectrum_->spectrumAfter().setActive(true); }
            sinceSend_ = sendEvery_;
        }
        return AudioEffect::setActive(state);
    }
    uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE { return fx_->latency(); }
    uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE { return fx_->latency(); }

    tresult PLUGIN_API process(Vst::ProcessData& data) SMTG_OVERRIDE {
        const auto ps = fx_->params();
        if (data.inputParameterChanges) {
            const int32 n = data.inputParameterChanges->getParameterCount();
            for (int32 i = 0; i < n; ++i) {
                Vst::IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
                if (!q) continue;
                const int32 points = q->getPointCount();
                Vst::ParamValue v = 0;
                int32 off = 0;
                if (points <= 0 || q->getPoint(points - 1, off, v) != kResultTrue) continue;
                const Vst::ParamID id = q->getParameterId();
                if (id == kBypassId) bypass_ = v >= 0.5;
                else if (id < ps.size()) fx_->set(static_cast<int>(id), hc::fx::toPlain(ps[id], static_cast<float>(v)));
            }
        }
        if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1 || maxBlock_ == 0) return kResultOk;
        Vst::AudioBusBuffers& in = data.inputs[0];
        Vst::AudioBusBuffers& out = data.outputs[0];
        if (!out.channelBuffers32 || out.numChannels < 2 || !in.channelBuffers32 || in.numChannels < 2) return kResultOk;
        float* oL = out.channelBuffers32[0];
        float* oR = out.channelBuffers32[1];
        const float* iL = in.channelBuffers32[0];
        const float* iR = in.channelBuffers32[1];
        const auto total = static_cast<std::uint32_t>(data.numSamples);
        if (oL != iL) std::memcpy(oL, iL, total * sizeof(float));
        if (oR != iR) std::memcpy(oR, iR, total * sizeof(float));

        const float target = bypass_ ? 0.0f : 1.0f;
        for (std::uint32_t done = 0; done < total;) {
            const std::uint32_t n = std::min(total - done, maxBlock_);
            float* L = oL + done;
            float* R = oR + done;
            for (std::uint32_t k = 0; k < n; ++k) {
                dry_[0][k] = bypassDelay_[0].process(L[k], fx_->latency());
                dry_[1][k] = bypassDelay_[1].process(R[k], fx_->latency());
            }
            fx_->process(L, R, n);
            if (mix_ != 1.0f || target != 1.0f) {
                for (std::uint32_t k = 0; k < n; ++k) {
                    mix_ = target > mix_ ? std::min(target, mix_ + fadeStep_) : std::max(target, mix_ - fadeStep_);
                    L[k] = mix_ * L[k] + (1.0f - mix_) * dry_[0][k];
                    R[k] = mix_ * R[k] + (1.0f - mix_) * dry_[1][k];
                }
            }
            done += n;
        }
        out.silenceFlags = 0;
        sinceSend_ += total;
        if (data.outputParameterChanges && sinceSend_ >= sendEvery_) {
            sinceSend_ = 0;
            sendDisplay(*data.outputParameterChanges);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) SMTG_OVERRIDE {
        bool b = bypass_;
        if (!readState(state, *fx_, b)) return kResultFalse;
        bypass_ = b;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) SMTG_OVERRIDE { return writeText(state, stateText(*fx_, bypass_)) ? kResultOk : kResultFalse; }

private:
    void send(Vst::IParameterChanges& c, Vst::ParamID id, double v) {
        int32 idx = 0;
        if (Vst::IParamValueQueue* q = c.addParameterData(id, idx)) {
            int32 pi = 0;
            q->addPoint(0, v, pi);
        }
    }
    // What the faceplate draws, as normalized values on hidden read-only parameters.
    void sendDisplay(Vst::IParameterChanges& c) {
        send(c, kRateId, std::clamp(sampleRate_ / 384000.0, 0.0, 1.0));
        if (spectrum_) {
            for (int i = 0; i < kSent; ++i) {
                const int b = 2 * i;
                const float before = std::max(spectrum_->spectrumBefore().level(b), spectrum_->spectrumBefore().level(b + 1));
                const float after = std::max(spectrum_->spectrumAfter().level(b), spectrum_->spectrumAfter().level(b + 1));
                send(c, kBeforeBase + static_cast<Vst::ParamID>(i), dbToNorm(before, kSpecLo, kSpecHi));
                send(c, kAfterBase + static_cast<Vst::ParamID>(i), dbToNorm(after, kSpecLo, kSpecHi));
            }
        }
        if (noise_) {
            for (int i = 0; i < kSent; ++i)
                send(c, kNoiseBase + static_cast<Vst::ParamID>(i), dbToNorm(0.5f * (noise_->noiseFloorDb(2 * i) + noise_->noiseFloorDb(2 * i + 1)), kSpecLo, kSpecHi));
            send(c, kRemovedId, dbToNorm(noise_->removedDb(), -60.0f, 0.0f));
        }
        if (eq_) {
            for (int b = 0; b < rack::eq::kBands; ++b) send(c, kDynBase + static_cast<Vst::ParamID>(b), dbToNorm(eq_->dynamicGainDb(b), -30.0f, 30.0f));
            send(c, kAutoGainId, dbToNorm(eq_->autoGainDb(), -24.0f, 24.0f));
        }
    }

    const rack::PluginInfo* info_;
    std::unique_ptr<hc::fx::Effect> fx_;
    rack::SpectrumSource* spectrum_ = nullptr;
    rack::NoiseProfileSource* noise_ = nullptr;
    rack::EliteEqView* eq_ = nullptr;
    hc::fx::DelayLine bypassDelay_[2];
    std::vector<float> dry_[2];
    double sampleRate_ = 48000.0;
    std::uint32_t maxBlock_ = 0, sendEvery_ = 1600, sinceSend_ = 0;
    bool bypass_ = false;
    float mix_ = 1.0f, fadeStep_ = 0.002f;
};

// ---- controller ---------------------------------------------------------------------------
class FxParameter final : public Vst::Parameter {
public:
    FxParameter(const hc::fx::ParamInfo& p, Vst::ParamID id)
        : Vst::Parameter(toU16(p.name).c_str(), id, unitFor(p), hc::fx::toNormalized(p, p.def),
                         (p.taper == hc::fx::Taper::Choice || p.taper == hc::fx::Taper::Toggle) ? static_cast<int32>(p.max - p.min) : 0,
                         Vst::ParameterInfo::kCanAutomate | (p.taper == hc::fx::Taper::Choice ? Vst::ParameterInfo::kIsList : 0)),
          p_(p) {}
    void toString(Vst::ParamValue norm, Vst::String128 out) const SMTG_OVERRIDE {
        const std::string s = hc::fx::formatValue(p_, hc::fx::toPlain(p_, static_cast<float>(norm)));
        const std::size_t n = std::min<std::size_t>(s.size(), 127);
        for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<Vst::TChar>(static_cast<unsigned char>(s[i]));
        out[n] = 0;
    }
    bool fromString(const Vst::TChar* text, Vst::ParamValue& norm) const SMTG_OVERRIDE {
        std::string s;
        for (const Vst::TChar* t = text; t && *t && s.size() < 64; ++t) s.push_back(static_cast<char>(*t & 0x7F));
        float plain = 0;
        if (!hc::fx::parseValue(p_, s, plain)) return false;
        norm = hc::fx::toNormalized(p_, plain);
        return true;
    }
    Vst::ParamValue toPlain(Vst::ParamValue norm) const SMTG_OVERRIDE { return hc::fx::toPlain(p_, static_cast<float>(norm)); }
    Vst::ParamValue toNormalized(Vst::ParamValue plain) const SMTG_OVERRIDE { return hc::fx::toNormalized(p_, static_cast<float>(plain)); }

private:
    static const Vst::TChar* unitFor(const hc::fx::ParamInfo& p) {
        const std::string u = p.unit ? p.unit : "";
        if (u == "dB" || u == "dBr") return STR16("dB");
        if (u == "Hz") return STR16("Hz");
        if (u == "ms") return STR16("ms");
        if (u == "%") return STR16("%");
        return STR16("");
    }
    const hc::fx::ParamInfo& p_;
};

// The faceplate edits parameters the VST3 way: begin / perform / end edit through the controller.
class ControllerHost final : public hc::ui::PanelHost {
public:
    ControllerHost(Vst::EditController& c, hc::fx::Effect& e) : c_(c), e_(e) {}
    float normalized(int i) const override { return static_cast<float>(c_.getParamNormalized(static_cast<Vst::ParamID>(i))); }
    void beginEdit(int i) override { c_.beginEdit(static_cast<Vst::ParamID>(i)); }
    void setNormalized(int i, float v) override {
        v = std::clamp(v, 0.0f, 1.0f);
        c_.setParamNormalized(static_cast<Vst::ParamID>(i), v);
        c_.performEdit(static_cast<Vst::ParamID>(i), v);
    }
    void endEdit(int i) override { c_.endEdit(static_cast<Vst::ParamID>(i)); }
    std::string text(int i) const override {
        const auto ps = e_.params();
        if (i < 0 || i >= static_cast<int>(ps.size())) return {};
        const auto& p = ps[static_cast<std::size_t>(i)];
        return hc::fx::formatValue(p, hc::fx::toPlain(p, normalized(i)));
    }
    // Whether a control is in use depends on the others: ask the controller's own copy of the
    // plug-in (it never processes audio) with the current values.
    bool active(int i) const override {
        const auto ps = e_.params();
        for (std::size_t j = 0; j < ps.size(); ++j) e_.set(static_cast<int>(j), hc::fx::toPlain(ps[j], normalized(static_cast<int>(j))));
        return e_.paramActive(i);
    }
    bool bypassed() const override { return c_.getParamNormalized(kBypassId) >= 0.5; }
    void setBypassed(bool on) override {
        c_.beginEdit(kBypassId);
        c_.setParamNormalized(kBypassId, on ? 1.0 : 0.0);
        c_.performEdit(kBypassId, on ? 1.0 : 0.0);
        c_.endEdit(kBypassId);
    }

private:
    Vst::EditController& c_;
    hc::fx::Effect&      e_;
};

// The faceplate's live data, from the processor's hidden output parameters.
class ControllerFeed final : public ui::FaceFeed {
public:
    ControllerFeed(Vst::EditController& c, const rack::PluginInfo& info) : c_(c), restorer_(info.face == rack::Face::Restorer) {}
    bool hasSpectrum() const override { return true; }
    float before(int b) const override { return bin(kBeforeBase, b); }
    float after(int b) const override { return bin(kAfterBase, b); }
    bool hasNoiseFloor() const override { return restorer_; }
    float noiseFloor(int b) const override { return bin(kNoiseBase, b); }
    float removedDb() const override { return normToDb(c_.getParamNormalized(kRemovedId), -60.0f, 0.0f); }
    float dynamicGainDb(int band) const override {
        return normToDb(c_.getParamNormalized(kDynBase + static_cast<Vst::ParamID>(std::clamp(band, 0, rack::eq::kBands - 1))), -30.0f, 30.0f);
    }
    float autoGainDb() const override { return normToDb(c_.getParamNormalized(kAutoGainId), -24.0f, 24.0f); }
    double sampleRate() const override {
        const double r = c_.getParamNormalized(kRateId) * 384000.0;
        return r > 8000.0 ? r : 48000.0;
    }

private:
    float bin(Vst::ParamID base, int b) const {
        const float x = static_cast<float>(b) * 0.5f;
        const int i0 = std::clamp(static_cast<int>(x), 0, kSent - 1), i1 = std::min(i0 + 1, kSent - 1);
        const float f = x - static_cast<float>(i0);
        const float a = normToDb(c_.getParamNormalized(base + static_cast<Vst::ParamID>(i0)), kSpecLo, kSpecHi);
        const float c = normToDb(c_.getParamNormalized(base + static_cast<Vst::ParamID>(i1)), kSpecLo, kSpecHi);
        return a + (c - a) * f;
    }
    Vst::EditController& c_;
    bool restorer_;
};

class FaceView final : public CPluginView {
public:
    FaceView(const rack::PluginInfo& info, hc::fx::Effect& fx, hc::ui::PanelHost& host, ui::FaceFeed& feed)
        : info_(info), fx_(fx), host_(host), feed_(feed) {
        scale_ = ui::FaceWindow::defaultScale();
        int w = 0, h = 0;
        ui::FaceWindow::sizeFor(scale_, w, h);
        ViewRect r(0, 0, w, h);
        setRect(r);
    }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE { return std::strcmp(type, kPlatformTypeHWND) == 0 ? kResultTrue : kResultFalse; }
    void attachedToParent() SMTG_OVERRIDE {
        std::string err;
        if (!window_.openChild(systemWindow, info_, fx_.params(), host_, feed_, bundleResources(), scale_, &err))
            ::OutputDebugStringA(("HEM-Elite-Rack: faceplate failed: " + err + "\n").c_str());
    }
    void removedFromParent() SMTG_OVERRIDE { window_.close(); }

private:
    const rack::PluginInfo& info_;
    hc::fx::Effect&         fx_;
    hc::ui::PanelHost&      host_;
    ui::FaceFeed&           feed_;
    ui::FaceWindow          window_;
    float                   scale_ = 0.72f;
};

class Controller final : public Vst::EditController {
public:
    explicit Controller(const rack::PluginInfo* info) : info_(info), fx_(info->create()), host_(*this, *fx_), feed_(*this, *info) {}
    static FUnknown* createInstance(void* ctx) { return static_cast<Vst::IEditController*>(new Controller(static_cast<const rack::PluginInfo*>(ctx))); }

    tresult PLUGIN_API initialize(FUnknown* context) SMTG_OVERRIDE {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk) return r;
        const auto ps = fx_->params();
        const int32 hidden = Vst::ParameterInfo::kIsReadOnly | Vst::ParameterInfo::kIsHidden;
        for (std::size_t i = 0; i < ps.size(); ++i) parameters.addParameter(new FxParameter(ps[i], static_cast<Vst::ParamID>(i)));
        parameters.addParameter(STR16("Bypass"), nullptr, 1, 0, Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsBypass, kBypassId);
        parameters.addParameter(STR16("Display Rate"), nullptr, 0, 48000.0 / 384000.0, hidden, kRateId);
        auto addHidden = [&](const std::string& name, Vst::ParamID id, double def) {
            parameters.addParameter(toU16(name).c_str(), nullptr, 0, def, hidden, id);
        };
        for (int i = 0; i < kSent; ++i) {
            addHidden("Display Before " + std::to_string(i + 1), kBeforeBase + static_cast<Vst::ParamID>(i), 0.0);
            addHidden("Display After " + std::to_string(i + 1), kAfterBase + static_cast<Vst::ParamID>(i), 0.0);
        }
        if (info_->face == rack::Face::Restorer) {
            for (int i = 0; i < kSent; ++i) addHidden("Display Noise " + std::to_string(i + 1), kNoiseBase + static_cast<Vst::ParamID>(i), 0.0);
            addHidden("Display Removed", kRemovedId, 1.0);
        } else {
            for (int b = 0; b < rack::eq::kBands; ++b) addHidden("Display Dynamic " + std::to_string(b + 1), kDynBase + static_cast<Vst::ParamID>(b), 0.5);
            addHidden("Display Auto Gain", kAutoGainId, 0.5);
        }
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream* state) SMTG_OVERRIDE {
        bool bypass = false;
        if (!readState(state, *fx_, bypass)) return kResultFalse;
        const auto ps = fx_->params();
        for (std::size_t i = 0; i < ps.size(); ++i) setParamNormalized(static_cast<Vst::ParamID>(i), hc::fx::toNormalized(ps[i], fx_->get(static_cast<int>(i))));
        setParamNormalized(kBypassId, bypass ? 1.0 : 0.0);
        return kResultOk;
    }

    IPlugView* PLUGIN_API createView(FIDString name) SMTG_OVERRIDE {
        if (name && std::strcmp(name, Vst::ViewType::kEditor) == 0) return new FaceView(*info_, *fx_, host_, feed_);
        return nullptr;
    }

private:
    const rack::PluginInfo*         info_;
    std::unique_ptr<hc::fx::Effect> fx_;   // parameter descriptions, text, paramActive (never processes audio)
    ControllerHost                  host_;
    ControllerFeed                  feed_;
};

} // namespace hem::vst

// ---- factory: two classes per plug-in, created with the plug-in's info as context -------------
SMTG_EXPORT_SYMBOL IPluginFactory* PLUGIN_API GetPluginFactory() {
    if (!gPluginFactory) {
        static PFactoryInfo factoryInfo("Hybrid Elite Mafia", "https://github.com/The-Hybrid-Elite-Mafia/HEM-Elite-Rack", "", PFactoryInfo::kUnicode);
        gPluginFactory = new CPluginFactory(factoryInfo);
        const auto list = hem::rack::plugins();
        static std::vector<PClassInfo2> infos;      // must outlive the registration
        static std::vector<std::string> names;
        infos.reserve(list.size() * 2);
        names.reserve(list.size() * 2);
        for (const hem::rack::PluginInfo& p : list) {
            TUID pid, cid;
            hem::vst::toFuid(p.processorUid).toTUID(pid);
            hem::vst::toFuid(p.controllerUid).toTUID(cid);
            names.push_back(std::string("HEM ") + p.name);
            infos.emplace_back(pid, PClassInfo::kManyInstances, kVstAudioEffectClass, names.back().c_str(), Vst::kDistributable, p.subCategory, nullptr,
                               HEM_RACK_VERSION_STR, kVstVersionString);
            gPluginFactory->registerClass(&infos.back(), hem::vst::Processor::createInstance, const_cast<hem::rack::PluginInfo*>(&p));
            names.push_back(std::string("HEM ") + p.name + " Controller");
            infos.emplace_back(cid, PClassInfo::kManyInstances, kVstComponentControllerClass, names.back().c_str(), 0, "", nullptr, HEM_RACK_VERSION_STR,
                               kVstVersionString);
            gPluginFactory->registerClass(&infos.back(), hem::vst::Controller::createInstance, const_cast<hem::rack::PluginInfo*>(&p));
        }
    } else {
        gPluginFactory->addRef();
    }
    return gPluginFactory;
}
