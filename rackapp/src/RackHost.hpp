#pragma once
// =============================================================================
//  RackHost — the engine, the rack processor and the sound card (WASAPI exclusive, 48 kHz)
//  or the offline clock (no sound card). No ASIO: this is the public build. Message thread only.
// =============================================================================
#include "RackEngine.hpp"

#include "hybrid/engine/Engine.hpp"
#include "hybrid/io/OfflineStream.hpp"
#include "hybrid/io/WasapiExclusiveOutput.hpp"

#include <memory>
#include <string>
#include <vector>

namespace hem::app {

struct Device {
    bool         offline = false;
    std::wstring id;          // WASAPI endpoint id ("" = Windows' default output)
    std::string  name;        // for the UI
};

class RackHost {
public:
    RackHost();
    ~RackHost();
    RackHost(const RackHost&)            = delete;
    RackHost& operator=(const RackHost&) = delete;

    [[nodiscard]] RackProcessor& rack() noexcept { return *rack_; }
    [[nodiscard]] static std::vector<Device> listDevices();
    bool open(const Device& d);   // closes the current stream first; false = see problem()
    void close() noexcept;
    void poll();                  // every UI frame: telemetry, device loss, freeing what the audio thread let go

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] const std::string& deviceName() const noexcept { return deviceName_; }
    [[nodiscard]] const std::string& format() const noexcept { return format_; }   // "48000 Hz - 144 frames = 3.0 ms"
    [[nodiscard]] const std::string& problem() const noexcept { return problem_; }
    [[nodiscard]] float cpuAvg() const noexcept { return cpu_; }
    [[nodiscard]] double outputLatencyMs() const noexcept { return outMs_; }

private:
    hc::engine::Engine engine_;
    RackProcessor* rack_ = nullptr;                       // owned by engine_ once installed
    std::unique_ptr<hc::engine::AudioProcessor> pending_; // until the first stream is open
    std::unique_ptr<hc::io::WasapiExclusiveOutput> wasapi_;
    std::unique_ptr<hc::io::OfflineStream> offline_;
    Device current_;
    bool running_ = false;
    std::string deviceName_, format_, problem_;
    float cpu_ = 0.0f;
    double outMs_ = 0.0;
};

[[nodiscard]] std::string toUtf8(const std::wstring& w);
[[nodiscard]] std::wstring fromUtf8(const std::string& s);

} // namespace hem::app
