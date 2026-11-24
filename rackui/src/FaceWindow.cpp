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
    auto frame = hc::ui::loadImageFile(assetDir / "brand" / "HEM-Board-4K.jpg", nullptr);
    const hc::ui::HemPartImages hem = hc::ui::loadHemParts(assetDir / "parts");
    const bool hemOk = hem.complete();
    art = SharedFaceArt{std::move(*fonts), hc::ui::buildPartsAtlas(plate->master, hemOk ? &hem : nullptr), std::move(*brand),
                        frame ? std::move(*frame) : hc::ui::Image{}, hemOk};
    dir = assetDir;
    return &*art;
}

void FaceWindow::sizeFor(float scale, int& width, int& height) noexcept {
    // The HEM frame look: the window is the frame (16:9), the plate body sits in its glass bed.
    width = std::max(320, static_cast<int>(std::lround(rackframe::kW * scale)));
    height = std::max(180, static_cast<int>(std::lround(rackframe::kH * scale)));
}

float FaceWindow::defaultScale() noexcept {
    const UINT dpi = ::GetDpiForSystem();
    float scale = 0.45f * static_cast<float>(dpi ? dpi : 96) / 96.0f;
    RECT work{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        const float fitW = 0.9f * static_cast<float>(work.right - work.left) / rackframe::kW;
        const float fitH = 0.85f * static_cast<float>(work.bottom - work.top) / rackframe::kH;
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
    view_->setFramed(true);
    view_->setHemParts(art->hemParts);
    controller_ = std::make_unique<FaceController>(*layout_, params, host, feed, state_);
    if (!renderer_.init(4, error)) return false;
    if (!renderer_.setParts(art->parts, error)) return false;
    w_ = w;
    h_ = h;
    art_ = art;
    placeCanvas();
    if (!bakeFont(error)) return false;
    if (!buildPlate(error)) return false;
    if (!renderer_.setTargetSize(w_, h_, error)) return false;
    renderer_.beginStatic(canvas_);
    renderer_.panel({0, 0, rackframe::kW, rackframe::kH});
    renderer_.endStatic();
    feed_->setSpectrumActive(true);
    open_ = true;
    return true;
}

void FaceWindow::placeCanvas() {
    canvas_ = hc::ui::Canvas{};
    canvas_.scale = std::min(static_cast<float>(w_) / rackframe::kW, static_cast<float>(h_) / rackframe::kH);   // design units = frame pixels
    canvas_.offsetX = std::round((static_cast<float>(w_) - rackframe::kW * canvas_.scale) * 0.5f);
    canvas_.offsetY = std::round((static_cast<float>(h_) - rackframe::kH * canvas_.scale) * 0.5f);
    canvas_.pixelW = static_cast<float>(w_);
    canvas_.pixelH = static_cast<float>(h_);
}

bool FaceWindow::bakeFont(std::string* error) {
    if (!font_.bake(art_->fonts, canvas_.scale)) { if (error) *error = "could not bake the Poppins atlas"; return false; }
    fontScale_ = canvas_.scale;
    return renderer_.setFont(font_, error);
}

// The HEM board with this plate's glass plates, at the window's own pixel density (capped at the 4096 px texture limit).
bool FaceWindow::buildPlate(std::string* error) {
    const float ppu = std::min({canvas_.scale, 4096.0f / rackframe::kW, 4096.0f / rackframe::kH});
    if (!renderer_.setPanel(hc::ui::buildMips(hc::ui::premultiply(renderRackPlateFramed(*layout_, ppu, art_->frame))), error))
        return false;
    plateScale_ = canvas_.scale;
    plateStale_ = false;
    return true;
}

void FaceWindow::resize(int w, int h) {
    w = std::max(64, w);
    h = std::max(36, h);
    if (!open_ || !child_ || (w == w_ && h == h_)) return;
    window_.makeCurrent();
    window_.resizeChild(w, h);
    w_ = w;
    h_ = h;
    placeCanvas();
    std::string err;
    if (canvas_.scale > fontScale_ * 1.06f || canvas_.scale < fontScale_ * 0.8f) (void)bakeFont(&err);
    (void)renderer_.setTargetSize(w_, h_, &err);
    renderer_.beginStatic(canvas_);
    renderer_.panel({0, 0, rackframe::kW, rackframe::kH});
    renderer_.endStatic();
    plateStale_ = std::fabs(canvas_.scale / std::max(1e-6f, plateScale_) - 1.0f) > 0.15f;
    resizedAt_ = std::chrono::steady_clock::now();
    window_.restoreContext();
}

bool FaceWindow::openChild(void* parentHwnd, const rack::PluginInfo& info, ParamTable params, PanelHost& host, FaceFeed& feed,
                           const std::filesystem::path& assetDir, float scale, std::string* error) {
    close();
    int w = 0, h = 0;
    sizeFor(scale, w, h);
    if (!window_.createChild(parentHwnd, w, h, error)) return false;
    child_ = true;
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
    child_ = false;
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
        const RackFrameMap m = rackFrameMap();   // frame pixels -> plate units (the controller works in plate units)
        const float x = m.plateX(canvas_.toDesignX(e.x)), y = m.plateY(canvas_.toDesignY(e.y));
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
    const RackFrameMap m = rackFrameMap();
    view_->draw(renderer_, *host_, *feed_, state_, m.ox, m.oy, m.s, false);
}

void FaceWindow::frame() {
    if (!open_) return;
    window_.makeCurrent();
    if (plateStale_ && std::chrono::steady_clock::now() - resizedAt_ > std::chrono::milliseconds(300)) {
        std::string err;   // resizing has settled: redraw the faceplate sharp at the new size
        if (buildPlate(&err)) {
            renderer_.beginStatic(canvas_);
            renderer_.panel({0, 0, rackframe::kW, rackframe::kH});
            renderer_.endStatic();
        }
        plateStale_ = false;
    }
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
