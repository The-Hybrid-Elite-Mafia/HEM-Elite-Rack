#pragma once
// =============================================================================
//  FaceWindow — a HEM-Elite-Rack faceplate in its own OpenGL window:
//    openChild()  : inside a host's window (VST3 editor); redraws itself ~30× a second
//    openHidden() : off-screen, for snapshots (CI screenshots of every faceplate)
//  Fonts, knob parts and the crown / ghost art are loaded once per process and shared.
//  Message / UI thread only. While open, the feed's spectrum analysers run; closed, they stop.
// =============================================================================
#include "hem/ui/Face.hpp"
#include "hybrid/ui/FontAtlas.hpp"
#include "hybrid/ui/GlWindow.hpp"
#include "hybrid/ui/Renderer.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace hem::ui {

// Fonts, knob parts and brand art for every faceplate in this process (loaded on first use).
struct SharedFaceArt {
    hc::ui::FontSet    fonts;
    hc::ui::PartsAtlas parts;
    BrandArt           brand;
};
// `assetDir` holds fonts/, faceplates/Main-Faceplate.png and brand/.
[[nodiscard]] const SharedFaceArt* sharedFaceArt(const std::filesystem::path& assetDir, std::string* error);

class FaceWindow {
public:
    FaceWindow() = default;
    ~FaceWindow() { close(); }
    FaceWindow(const FaceWindow&)            = delete;
    FaceWindow& operator=(const FaceWindow&) = delete;

    static void sizeFor(float scale, int& width, int& height) noexcept;
    // ~0.72 px per plate unit at 100 % Windows scaling, smaller if it would not fit the screen.
    [[nodiscard]] static float defaultScale() noexcept;

    // `params`, `host` and `feed` must outlive the window.
    bool openChild(void* parentHwnd, const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
                   const std::filesystem::path& assetDir, float scale, std::string* error);
    bool openHidden(const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
                    const std::filesystem::path& assetDir, float scale, std::string* error);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    void frame();                          // input + draw + present (child windows: called by the timer)
    [[nodiscard]] hc::ui::Image snapshot(); // draw once into the capture buffer and read it back
    [[nodiscard]] FaceState& state() noexcept { return state_; }
    // Parts drawn as light sources (blurred into a glow) in the last frame. The rack's faceplates draw
    // none: lights are solid and sharp, with no glow around them (Steven, Oct 20). The snapshot tool
    // fails a plate that draws any.
    [[nodiscard]] std::uint32_t glowingParts() const noexcept { return renderer_.stats().bloomQuads; }
    [[nodiscard]] int width() const noexcept { return w_; }
    [[nodiscard]] int height() const noexcept { return h_; }

private:
    bool setUp(const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
               const std::filesystem::path& assetDir, int w, int h, std::string* error);
    void handleInput();
    void draw();

    hc::ui::GlWindow  window_;
    hc::ui::Renderer  renderer_;
    hc::ui::FontAtlas font_;
    std::unique_ptr<FaceLayout>     layout_;
    std::unique_ptr<FaceView>       view_;
    std::unique_ptr<FaceController> controller_;
    FaceState  state_;
    PanelHost* host_ = nullptr;
    FaceFeed*  feed_ = nullptr;
    hc::ui::Canvas canvas_;
    int  w_ = 0, h_ = 0;
    bool open_ = false;
};

} // namespace hem::ui
