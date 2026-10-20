#include "RackHost.hpp"

#include "hybrid/io/DeviceProbe.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace hem::app {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

RackHost::RackHost() {
    auto p = std::make_unique<RackProcessor>();
    rack_ = p.get();
    pending_ = std::move(p);
}

RackHost::~RackHost() { close(); }

std::string Device::key() const {
    if (offline) return "offline";
    if (asio) return "asio:" + toUtf8(id);
    return id.empty() ? "default" : toUtf8(id);
}

// ASIO drivers first (the lowest delay), then Windows' outputs, then no sound.
std::vector<Device> RackHost::listDevices() {
    std::vector<Device> out;
#if defined(HC_HAS_ASIO_SDK)
    for (const auto& d : hc::io::enumerateAsioDrivers())
        if (!d.dllPath.empty()) out.push_back({false, true, d.name, "ASIO: " + toUtf8(d.name)});   // 32-bit-only drivers can't load here
#endif
    out.push_back({false, false, L"", "Windows default output"});
    if (auto eps = hc::io::enumerateWasapi()) {
        for (const auto& ep : *eps) {
            if (ep.flow != hc::io::Flow::Render) continue;
            if (!(ep.excl48kFloat32 || ep.excl48kInt24in32 || ep.excl48kInt16)) continue;
            out.push_back({false, false, ep.id, toUtf8(ep.name)});
        }
    }
    out.push_back({true, false, L"offline", "No sound (offline clock)"});
    return out;
}

void RackHost::close() noexcept {
    if (wasapi_) { wasapi_->close(); wasapi_.reset(); }
#if defined(HC_HAS_ASIO_SDK)
    if (asio_) { asio_->close(); asio_.reset(); }
#endif
    if (offline_) { offline_->close(); offline_.reset(); }
    engine_.collectGarbage();
    running_ = false;
}

bool RackHost::open(const Device& d) {
    close();
    current_ = d;
    problem_.clear();
    format_.clear();
    deviceName_ = d.name;
    char buf[256];
    hc::io::StreamConfig cfg{};
    if (d.offline) {
        offline_ = std::make_unique<hc::io::OfflineStream>();
        cfg = offline_->open({}, engine_);
    } else if (d.asio) {
#if defined(HC_HAS_ASIO_SDK)
        asio_ = std::make_unique<hc::io::AsioDuplex>();
        hc::io::AsioDuplex::Options opt;
        opt.driverName = d.id;
        opt.maxInputs  = 0;   // the rack plays songs: outputs only
        opt.maxOutputs = 2;   // the first two outputs of the interface
        auto r = asio_->open(opt, engine_);
        if (!r) {
            std::snprintf(buf, sizeof buf, "Could not open the ASIO driver (%s, error %ld) - is another app using it? Or pick another output",
                          r.error().stage, r.error().code);
            problem_ = buf;
            asio_.reset();
            return false;
        }
        if (r->numOutputs < 2) {
            problem_ = "This ASIO driver has fewer than two outputs - pick another output with the OUTPUT button";
            close();
            return false;
        }
        cfg = *r;
#else
        problem_ = "This build has no ASIO";
        return false;
#endif
    } else {
        auto tryOpen = [&](const std::wstring& id) {
            if (wasapi_) wasapi_->close();
            wasapi_ = std::make_unique<hc::io::WasapiExclusiveOutput>();
            hc::io::WasapiExclusiveOutput::Options opt;
            opt.deviceId = id;
            return wasapi_->open(opt, engine_);
        };
        auto r = tryOpen(d.id);
        if (!r && d.id.empty()) {
            // Windows' default can be an output without a 48 kHz exclusive mode (HDMI, Bluetooth):
            // fall back to the first one that has it, and say which.
            if (auto eps = hc::io::enumerateWasapi())
                for (const auto& ep : *eps) {
                    if (ep.flow != hc::io::Flow::Render || ep.id.empty()) continue;
                    if (!(ep.excl48kFloat32 || ep.excl48kInt24in32 || ep.excl48kInt16)) continue;
                    if (auto again = tryOpen(ep.id)) { r = std::move(again); deviceName_ = toUtf8(ep.name); break; }
                }
        }
        if (!r) {
            const auto code = static_cast<unsigned long>(r.error().code);
            if (code == 0x88890008ul)
                problem_ = "This output cannot play 48 kHz in exclusive mode - pick another one with the OUTPUT button";
            else if (code == 0x8889000Aul)
                problem_ = "Another app is using this output in exclusive mode - close it, or pick another output";
            else {
                std::snprintf(buf, sizeof buf, "Could not open the sound card (WASAPI %s, 0x%08lX) - try another output", r.error().stage, code);
                problem_ = buf;
            }
            wasapi_.reset();
            return false;
        }
        cfg = *r;
    }
    if (pending_ && !engine_.install(pending_)) {
        problem_ = "Could not start the audio engine";
        close();
        return false;
    }
#if defined(HC_HAS_ASIO_SDK)
    if (asio_) {
        if (auto s = asio_->start(); !s) {
            std::snprintf(buf, sizeof buf, "The ASIO driver did not start (%s, error %ld)", s.error().stage, s.error().code);
            problem_ = buf;
            close();
            return false;
        }
    } else
#endif
    if (wasapi_) {
        if (auto s = wasapi_->start(); !s) {
            std::snprintf(buf, sizeof buf, "The sound card did not start (WASAPI %s, 0x%08lX)", s.error().stage, static_cast<unsigned long>(s.error().code));
            problem_ = buf;
            close();
            return false;
        }
    } else {
        offline_->start();
    }
    outMs_ = cfg.outputLatencyMs;
    if (d.asio)
        std::snprintf(buf, sizeof buf, "ASIO - %u Hz - %u frames = %.1f ms, the driver reports %.1f ms out", cfg.sampleRate, cfg.blockFrames,
                      1000.0 * cfg.blockFrames / std::max(1u, cfg.sampleRate), cfg.outputLatencyMs);
    else
        std::snprintf(buf, sizeof buf, "%u Hz - %u frames = %.1f ms", cfg.sampleRate, cfg.blockFrames, 1000.0 * cfg.blockFrames / std::max(1u, cfg.sampleRate));
    format_ = buf;
    running_ = true;
    return true;
}

void RackHost::poll() {
    hc::engine::Telemetry t;
    while (engine_.popTelemetry(t)) cpu_ = t.loadAvg * 100.0f;
    engine_.collectGarbage();
    if (!running_) rack_->drainWhileStopped();   // no audio thread runs the rack: apply chain changes here
    rack_->collectRetired();
    std::uint32_t ev = 0;
    if (wasapi_) ev |= wasapi_->takeEvents();
#if defined(HC_HAS_ASIO_SDK)
    if (asio_) ev |= asio_->takeEvents();
#endif
    using namespace hc::io::StreamEvent;
    if (ev & (ResetRequest | SampleRateMoved | ResyncRequest)) {
        const Device again = current_;
        (void)open(again);
        return;
    }
    if (ev & (DeviceLost | ThreadFault)) {
        close();
        problem_ = (ev & DeviceLost) ? "The sound card was unplugged - pick an output with the OUTPUT button"
                                     : "The sound card stopped responding - pick an output with the OUTPUT button";
    }
}

} // namespace hem::app
