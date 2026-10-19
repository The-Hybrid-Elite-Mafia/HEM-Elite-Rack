#include "hem/ui/FaceWindow.hpp"

#include "hybrid/ui/Faceplate.hpp"
#include "hybrid/ui/Parts.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>

namespace hem::ui {

const SharedFaceArt* sharedFaceArt(const std::filesystem::path& assetDir, std::string* error) {
    static std::mutex mutex;
    static std::optional<SharedFaceArt> art;
    static std::filesystem::path dir;
    std::lock_guard lock(mutex);
    if (art && dir == assetDir) return &*art;
    auto fonts = hc::ui::FontSet::load(assetDir / "fonts", error);
    if (!fonts) return nullptr;
    auto plate = hc::ui::loadFaceplateArt(assetDir / "faceplates" / "Main-Faceplate.png", error);
    if (!plate) return nullptr;
    auto brand = BrandArt::load(assetDir, error);
    if (!brand) return nullptr;
    art = SharedFaceArt{std::move(*fonts), hc::ui::buildPartsAtlas(plate->master), std::move(*brand)};
    dir = assetDir;
    return &*art;
}

void FaceWindow::sizeFor(float scale, int& width, int& height) noexcept {
    width = std::max(320, static_cast<int>(std::lround(kPlateW * scale)));
    height = std::max(180, static_cast<int>(std::lround(kPlateH * scale)));
}

float FaceWindow::defaultScale() noexcept {
    const UINT dpi = ::GetDpiForSystem();
    float scale = 0.72f * static_cast<float>(dpi ? dpi : 96) / 96.0f;
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const float fitW = 0.9f * static_cast<float>(work.right - work.left) / kPlateW;
        const float fitH = 0.85f * static_cast<float>(work.bottom - work.top) / kPlateH;
        scale = std::max(0.3f, std::min({scale, fitW, fitH}));
    }
    return scale;
}

bool FaceWindow::setUp(const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
                       const std::filesystem::path& assetDir, int w, int h, std::string* error) {
    const SharedFaceArt* art = sharedFaceArt(assetDir, error);
    if (!art) return false;
    host_ = &host;
    feed_ = &feed;
    state_ = FaceState{};
    layout_ = std::make_unique<FaceLayout>(faceLayout(info));
    view_ = std::make_unique<FaceView>(*layout_, params);
    controller_ = std::make_unique<FaceController>(*layout_, params, host, feed, state_);
    if (!renderer_.init(4, error)) return false;
    if (!renderer_.setParts(art->parts, error)) return false;
    w_ = w;
    h_ = h;
    canvas_ = hc::ui::Canvas{};
    canvas_.scale = std::min(static_cast<float>(w_) / kPlateW, static_cast<float>(h_) / kPlateH);
    canvas_.offsetX = std::round((static_cast<float>(w_) - kPlateW * canvas_.scale) * 0.5f);
    canvas_.offsetY = std::round((static_cast<float>(h_) - kPlateH * canvas_.scale) * 0.5f);
    canvas_.pixelW = static_cast<float>(w_);
    canvas_.pixelH = static_cast<float>(h_);
    if (!font_.bake(art->fonts, canvas_.scale)) { if (error) *error = "could not bake the Poppins atlas"; return false; }
    if (!renderer_.setFont(font_, error)) return false;
    // The gunmetal plate at the window's own pixel density (capped at the 4096 px texture limit).
    const float ppu = std::min({canvas_.scale, 4096.0f / kPlateW, 4096.0f / kPlateH});
    if (!renderer_.setPanel(hc::ui::buildMips(hc::ui::premultiply(renderRackPlate(*layout_, art->brand, ppu))), error)) return false;
    if (!renderer_.setTargetSize(w_, h_, error)) return false;
    renderer_.beginStatic(canvas_);
    renderer_.panel({0, 0, kPlateW, kPlateH});
    renderer_.endStatic();
    feed_->setSpectrumActive(true);
    open_ = true;
    return true;
}

bool FaceWindow::openChild(void* parentHwnd, const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
                           const std::filesystem::path& assetDir, float scale, std::string* error) {
    close();
    int w = 0, h = 0;
    sizeFor(scale, w, h);
    if (!window_.createChild(parentHwnd, w, h, error)) return false;
    const bool ok = setUp(info, params, host, feed, assetDir, w, h, error);
    window_.restoreContext();
    if (!ok) { close(); return false; }
    window_.setTimer(33, [this] { frame(); });
    return true;
}

bool FaceWindow::openHidden(const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
                            const std::filesystem::path& assetDir, float scale, std::string* error) {
    close();
    int w = 0, h = 0;
    sizeFor(scale, w, h);
    if (!window_.create(L"HEM-Elite-Rack snapshot", 64, 64, false, error)) return false;
    if (!setUp(info, params, host, feed, assetDir, w, h, error)) { close(); return false; }
    return true;
}

void FaceWindow::close() noexcept {
    if (!open_ && !window_.nativeHandle()) return;
    if (feed_ && open_) feed_->setSpectrumActive(false);
    if (window_.nativeHandle()) {
        window_.makeCurrent();
        renderer_.shutdown();
        window_.restoreContext();
    }
    window_.destroy();
    controller_.reset();
    view_.reset();
    layout_.reset();
    open_ = false;
}

void FaceWindow::handleInput() {
    for (const hc::ui::InputEvent& e : window_.takeEvents()) {
        const float x = canvas_.toDesignX(e.x), y = canvas_.toDesignY(e.y);
        switch (e.type) {
        case hc::ui::InputEvent::Type::MouseDown: controller_->mouseDown(x, y, e.doubleClick, e.fine); break;
        case hc::ui::InputEvent::Type::MouseMove: controller_->mouseMove(x, y, e.fine); break;
        case hc::ui::InputEvent::Type::MouseUp:   controller_->mouseUp(); break;
        case hc::ui::InputEvent::Type::Wheel:     controller_->wheel(x, y, e.wheel, e.fine); break;
        default: break;
        }
    }
}

void FaceWindow::draw() {
    renderer_.beginFrame(canvas_);
    view_->draw(renderer_, *host_, *feed_, state_, 0, 0, 1.0f, false);
}

void FaceWindow::frame() {
    if (!open_) return;
    window_.makeCurrent();
    handleInput();
    draw();
    renderer_.endFrame(0);
    window_.swap();
    window_.restoreContext();
}

hc::ui::Image FaceWindow::snapshot() {
    if (!open_) return {};
    window_.makeCurrent();
    draw();
    renderer_.endFrameToCapture();
    hc::ui::Image img = renderer_.capture();
    window_.restoreContext();
    return img;
}

} // namespace hem::ui
