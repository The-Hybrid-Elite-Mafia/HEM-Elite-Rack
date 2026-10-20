#pragma once
// =============================================================================
//  RackHost — the engine, the rack processor and the sound card: an ASIO driver (Steinberg's ASIO SDK under its
//  GPLv3 option), WASAPI exclusive, or the offline clock (no sound card). Always 48 kHz. Message thread only.
// =============================================================================
#include "RackEngine.hpp"

#include "hybrid/engine/Engine.hpp"
#include "hybrid/io/OfflineStream.hpp"
#include "hybrid/io/WasapiExclusiveOutput.hpp"
#if defined(HC_HAS_ASIO_SDK)
#include "hybrid/io/AsioDuplex.hpp"
#endif

#include <memory>
#include <string>
#include <vector>

namespace hem::app {

struct Device {
    bool         offline = false;
    bool         asio    = false;   // id is then the ASIO driver's name
    std::wstring id;                // WASAPI endpoint id ("" = Windows' default output)
    std::string  name;              // for the UI
    [[nodiscard]] std::string key() const;   // what the settings file remembers
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
    [[nodiscard]] double inputLatencyMs() const noexcept { return inMs_; }
    [[nodiscard]] int inputs() const noexcept { return running_ ? inputs_ : 0; }   // the sound card's inputs (ASIO only)

private:
    hc::engine::Engine engine_;
    RackProcessor* rack_ = nullptr;                       // owned by engine_ once installed
    std::unique_ptr<hc::engine::AudioProcessor> pending_; // until the first stream is open
    std::unique_ptr<hc::io::WasapiExclusiveOutput> wasapi_;
    std::unique_ptr<hc::io::OfflineStream> offline_;
#if defined(HC_HAS_ASIO_SDK)
    std::unique_ptr<hc::io::AsioDuplex> asio_;
#endif
    Device current_;
    bool running_ = false;
    std::string deviceName_, format_, problem_;
    float cpu_ = 0.0f;
    double outMs_ = 0.0, inMs_ = 0.0;
    int inputs_ = 0;
};

[[nodiscard]] std::string toUtf8(const std::wstring& w);
[[nodiscard]] std::wstring fromUtf8(const std::string& s);

} // namespace hem::app
