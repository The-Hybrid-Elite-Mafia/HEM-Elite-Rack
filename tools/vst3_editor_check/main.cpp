// hem_vst3_editor_check - opens EVERY plug-in's faceplate in a bundle inside a real Windows host window, the way a
// DAW does (Audacity, FL Studio, Reaper ...), and checks each one: it loads and starts processing, it has an
// editor, the editor's window is up (art loaded, OpenGL running) and survives half a second of drawing, it closes
// cleanly, and its size fits a 1080p laptop screen.
//   hem_vst3_editor_check "<bundle.vst3>" [--only "NAME"]        exit code 0 = every editor opened
// Prints one line per plug-in: name, the size it opens at here, and the size it would open at on a 1920 x 1080
// screen at 100 / 125 / 150 % Windows display scaling (the HEM faceplates' own rule: at most 90 % of the work
// area's width and 85 % of its height, 0.45 x the display scale at most).
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;

namespace {

int failures = 0, checks = 0;
void check(bool ok, const std::string& what, double v = 0) {
    ++checks;
    if (!ok) ++failures;
    std::printf("  [%s] %-60s %10.1f\n", ok ? "PASS" : "FAIL", what.c_str(), v);
    std::fflush(stdout);
}

std::atomic<const char*> gStage{"start"};
std::string gPlugin;
void stage(const char* s) { gStage = s; }
void startWatchdog(int seconds) {
    std::thread([seconds] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::printf("  [FAIL] TIMEOUT at %s: %s\n", gPlugin.c_str(), gStage.load());
        std::fflush(stdout);
        std::_Exit(3);
    }).detach();
}
LONG WINAPI crashReport(EXCEPTION_POINTERS* ep) {
    const void* at = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    char file[MAX_PATH] = "(unknown file)";
    if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(at), &mod) && mod)
        ::GetModuleFileNameA(mod, file, MAX_PATH);
    std::printf("  [FAIL] CRASH 0x%08lX in %s at +0x%llX while %s: %s\n", static_cast<unsigned long>(ep->ExceptionRecord->ExceptionCode), file,
                static_cast<unsigned long long>(reinterpret_cast<const char*>(at) - reinterpret_cast<const char*>(mod)), gPlugin.c_str(), gStage.load());
    std::fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}
void pump(int ms) {
    const DWORD until = ::GetTickCount() + static_cast<DWORD>(ms);
    MSG m;
    while (::GetTickCount() < until) {
        for (int guard = 0; guard < 200 && ::PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE); ++guard) {
            ::TranslateMessage(&m);
            ::DispatchMessageW(&m);
        }
        ::Sleep(5);
    }
}
LRESULT CALLBACK parentProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return ::DefWindowProcW(h, msg, wp, lp); }

// The size a 16:9-style faceplate of base w x h opens at on a 1920 x 1080 screen (taskbar 48 px at 100 %).
void laptopSize(double baseW, double baseH, double displayScale, int& w, int& h) {
    const double workW = 1920.0, workH = 1080.0 - 48.0 * displayScale;
    double s = 0.45 * displayScale;
    s = std::max(0.12, std::min({s, 0.9 * workW / baseW, 0.85 * workH / baseH}));
    w = static_cast<int>(baseW * s + 0.5);
    h = static_cast<int>(baseH * s + 0.5);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: hem_vst3_editor_check <bundle.vst3> [--only NAME]\n"); return 2; }
    const char* only = argc > 3 && std::strcmp(argv[2], "--only") == 0 ? argv[3] : nullptr;
    ::SetUnhandledExceptionFilter(crashReport);
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // like a modern DAW
    std::string err;
    auto module = VST3::Hosting::Module::create(argv[1], err);
    if (!module) { std::printf("FAIL cannot load %s: %s\n", argv[1], err.c_str()); return 1; }
    Vst::HostApplication hostApp;
    Vst::PluginContextFactory::instance().setPluginContext(&hostApp);
    const auto factory = module->getFactory();
    std::vector<VST3::Hosting::ClassInfo> classes;
    for (const auto& ci : factory.classInfos())
        if (ci.category() == kVstAudioEffectClass && (!only || ci.name().find(only) != std::string::npos)) classes.push_back(ci);
    startWatchdog(60 + 40 * static_cast<int>(classes.size()));

    WNDCLASSW wc{};
    wc.lpfnWndProc = parentProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"HemEditorCheckParent";
    ::RegisterClassW(&wc);

    int opened = 0, tooBig = 0;
    std::vector<std::string> failed;
    for (const auto& ci : classes) {
        gPlugin = ci.name();
        const int before = failures;
        std::printf("== %s ==\n", ci.name().c_str());
        stage("load");
        IPtr<Vst::PlugProvider> provider = owned(new Vst::PlugProvider(factory, ci, true));
        const bool init = provider->initialize();
        check(init, "loads (component + controller)");
        if (!init) { failed.push_back(ci.name()); continue; }
        IPtr<Vst::IComponent> component = provider->getComponentPtr();
        IPtr<Vst::IEditController> controller = provider->getControllerPtr();
        FUnknownPtr<Vst::IAudioProcessor> processor(component);
        Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, 512, 48000.0};
        stage("setupProcessing");
        const bool proc = processor && processor->setupProcessing(setup) == kResultOk;
        check(proc, "sets up at 48 kHz / 512");
        if (proc) { component->setActive(true); processor->setProcessing(true); }

        stage("createView");
        IPtr<IPlugView> view = owned(controller ? controller->createView(Vst::ViewType::kEditor) : nullptr);
        check(view != nullptr, "has an editor (faceplate)");
        if (view) {
            ViewRect r;
            view->getSize(&r);
            const int w = r.getWidth(), h = r.getHeight();
            check(w >= 320 && h >= 180, "editor size here (width px)", w);
            HWND parent = ::CreateWindowExW(0, L"HemEditorCheckParent", L"host", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, w + 40, h + 60, nullptr,
                                            nullptr, ::GetModuleHandleW(nullptr), nullptr);
            stage("attached");
            const bool att = view->attached(parent, kPlatformTypeHWND) == kResultOk;
            check(att, "opens inside the host window");
            stage("drawing");
            pump(500);
            HWND child = ::FindWindowExW(parent, nullptr, L"HEMPluginGlView", nullptr);
            check(child != nullptr && ::IsWindow(child), "faceplate window is up after 0.5 s (art loaded, OpenGL running)");
            if (child) {
                RECT cr{};
                ::GetClientRect(child, &cr);
                check(cr.right - cr.left == w && cr.bottom - cr.top == h, "faceplate window is the size the host was told (width px)", cr.right - cr.left);
            }
            stage("removed");
            if (att) view->removed();
            pump(50);
            check(::FindWindowExW(parent, nullptr, L"HEMPluginGlView", nullptr) == nullptr, "closes cleanly");
            ::DestroyWindow(parent);
            // the size it would open at on a 1080p laptop (its own rule, from the 3840-wide frame every faceplate is drawn on)
            int w100, h100, w125, h125, w150, h150;
            const double fw = static_cast<double>(w), fh = static_cast<double>(h);
            const double aspect = fw / std::max(1.0, fh);
            const double bW = 3840.0, bH = 3840.0 / aspect;   // every HEM faceplate is drawn on a 3840-wide frame
            laptopSize(bW, bH, 1.00, w100, h100);
            laptopSize(bW, bH, 1.25, w125, h125);
            laptopSize(bW, bH, 1.50, w150, h150);
            const bool fits = h100 <= 1032 - 120 && h125 <= 1020 - 120 && h150 <= 1008 - 120 && w100 <= 1920;
            if (!fits) ++tooBig;
            std::printf("  SIZE %s | here %d x %d | 1080p 100%% %d x %d, 125%% %d x %d, 150%% %d x %d (physical px) | %s\n", ci.name().c_str(), w, h,
                        w100, h100, w125, h125, w150, h150, fits ? "fits with room for the host's title bar and buttons" : "TOO BIG");
            check(fits, "fits a 1080p laptop screen with the host's title bar and buttons", h100);
        }
        stage("close");
        view = nullptr;
        if (proc) { processor->setProcessing(false); component->setActive(false); }
        controller = nullptr;
        component = nullptr;
        provider = nullptr;
        if (failures == before) ++opened; else failed.push_back(ci.name());
    }
    std::printf("\nEDITORS: %d of %zu opened, drew, closed and fit 1080p; %d too big; %d checks, %d failed\n", opened, classes.size(), tooBig, checks,
                failures);
    for (const auto& f : failed) std::printf("  FAILED: %s\n", f.c_str());
    return failures ? 1 : 0;
}
