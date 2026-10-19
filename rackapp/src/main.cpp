// =============================================================================
//  HEMEliteRack.exe — load a song, stack HEM plug-ins, hear them live, compare A / B at equal
//  loudness, export a WAV.
//  Threads: this UI / message thread (GPU drawing, files), the audio thread (WASAPI or the offline
//  clock, inside RackHost) and short-lived worker threads for loading and exporting. The audio
//  thread only shares atomics and wait-free rings with the others.
// =============================================================================
#include "Chain.hpp"
#include "RackEngine.hpp"
#include "RackHost.hpp"
#include "RackView.hpp"

#include "hem/rack/Plugins.hpp"
#include "hem/ui/Face.hpp"
#include "hem/ui/FaceWindow.hpp"
#include "hybrid/core/RealtimeThread.hpp"
#include "hybrid/io/DeviceProbe.hpp"
#include "hybrid/media/AudioFile.hpp"
#include "hybrid/media/Loudness.hpp"
#include "hybrid/media/Resampler.hpp"
#include "hybrid/params/Settings.hpp"
#include "hybrid/ui/GlWindow.hpp"
#include "hybrid/ui/Renderer.hpp"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <numbers>
#include <thread>

using namespace hem;
using namespace hem::app;
namespace fs = std::filesystem;

namespace {

void fatal(const std::string& msg) { ::MessageBoxW(nullptr, fromUtf8(msg).c_str(), L"HEM-Elite-Rack", MB_ICONERROR | MB_OK); }

fs::path exeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n)).parent_path();
}

// %APPDATA%\HEM-Elite-Rack (settings.ini). Never touched by the installer, so settings survive upgrades.
fs::path dataDir() {
    fs::path base;
    PWSTR p = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) { base = p; ::CoTaskMemFree(p); }
    if (base.empty()) base = fs::temp_directory_path();
    fs::path dir = base / "HEM-Elite-Rack";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

fs::path musicDir() {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &p))) { out = p; ::CoTaskMemFree(p); }
    return out;
}

// Command line (CI and support): --exit-after <s> --screenshot <png> --device offline --demo | --demo2 --select <slot 1..8> [song]
struct Options {
    double   exitAfter = 0.0;
    fs::path screenshot, song;
    bool     offline = false, demo = false, demo2 = false;   // --demo2: the vintage chain (TAPE .. FET)
    int      select = 0;
};
Options parseOptions() {
    Options o;
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        const std::wstring k = argv[i];
        if (k == L"--exit-after" && i + 1 < argc) o.exitAfter = std::wcstod(argv[++i], nullptr);
        else if (k == L"--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (k == L"--device" && i + 1 < argc) o.offline = std::wstring(argv[++i]) == L"offline";
        else if (k == L"--demo") o.demo = true;
        else if (k == L"--demo2") o.demo = o.demo2 = true;
        else if (k == L"--select" && i + 1 < argc) o.select = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        else if (!k.empty() && k[0] != L'-') o.song = k;
    }
    if (argv) ::LocalFree(argv);
    return o;
}

void setFolder(IFileDialog* dlg, const fs::path& folder) {
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return;
    IShellItem* f = nullptr;
    if (SUCCEEDED(::SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&f)))) { dlg->SetFolder(f); f->Release(); }
}

fs::path askOpenSong(HWND owner, const fs::path& folder) {
    fs::path out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    const COMDLG_FILTERSPEC filters[] = {{L"Songs (WAV, MP3, FLAC)", L"*.wav;*.wave;*.mp3;*.flac"}, {L"All files", L"*.*"}};
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(L"Open a song");
    FILEOPENDIALOGOPTIONS o = 0;
    dlg->GetOptions(&o);
    dlg->SetOptions(o | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    setFolder(dlg, folder);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* it = nullptr;
        PWSTR path = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path))) { out = path; ::CoTaskMemFree(path); }
        if (it) it->Release();
    }
    dlg->Release();
    return out;
}

fs::path askSaveWav(HWND owner, const fs::path& suggested) {
    fs::path out;
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    const COMDLG_FILTERSPEC filters[] = {{L"WAV 24-bit, 48 kHz", L"*.wav"}};
    dlg->SetFileTypes(1, filters);
    dlg->SetDefaultExtension(L"wav");
    dlg->SetTitle(L"Export the song through the chain");
    FILEOPENDIALOGOPTIONS o = 0;
    dlg->GetOptions(&o);
    dlg->SetOptions(o | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM);
    if (!suggested.filename().empty()) dlg->SetFileName(suggested.filename().c_str());
    setFolder(dlg, suggested.parent_path());
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* it = nullptr;
        PWSTR path = nullptr;
        if (SUCCEEDED(dlg->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path))) { out = path; ::CoTaskMemFree(path); }
        if (it) it->Release();
    }
    dlg->Release();
    return out;
}

// The faceplate's view of one slot: knobs turn the live plug-in's atomics; ON / BYPASS is the slot's ON.
class SlotHost final : public hc::ui::PanelHost {
public:
    SlotHost(hc::fx::Effect& e, RackProcessor& rack, int slot) : e_(e), rack_(rack), slot_(slot) {}
    float normalized(int i) const override { return hc::fx::toNormalized(e_.params()[static_cast<std::size_t>(i)], e_.get(i)); }
    void beginEdit(int) override {}
    void setNormalized(int i, float v) override { e_.set(i, hc::fx::toPlain(e_.params()[static_cast<std::size_t>(i)], std::clamp(v, 0.0f, 1.0f))); }
    void endEdit(int) override {}
    std::string text(int i) const override { return hc::fx::formatValue(e_.params()[static_cast<std::size_t>(i)], e_.get(i)); }
    bool active(int i) const override { return e_.paramActive(i); }
    bool bypassed() const override { return !rack_.slotOn(slot_); }
    void setBypassed(bool b) override { rack_.setSlotOn(slot_, !b); }

private:
    hc::fx::Effect& e_;
    RackProcessor&  rack_;
    int             slot_;
};

// The faceplate of the selected slot.
struct FaceUi {
    int slot = -1;
    hc::fx::Effect* fx = nullptr;
    const rack::PluginInfo* info = nullptr;
    std::unique_ptr<ui::FaceLayout> layout;
    std::unique_ptr<ui::FaceView> view;
    std::unique_ptr<SlotHost> host;
    std::unique_ptr<ui::EffectFeed> feed;
    std::unique_ptr<ui::FaceController> controller;
    ui::FaceState state;
};

// A made-up song for demos and CI screenshots: kick, bass, chords, hats, a little hiss (20 s).
std::shared_ptr<Song> demoSong() {
    auto s = std::make_shared<Song>();
    const std::size_t n = static_cast<std::size_t>(20.0 * kRate);
    s->left.assign(n, 0.0f);
    s->right.assign(n, 0.0f);
    std::uint32_t rng = 7;
    const double bpm = 92.0, beat = 60.0 / bpm;
    const double chord[4][3] = {{220.0, 261.6, 329.6}, {174.6, 220.0, 261.6}, {196.0, 246.9, 293.7}, {164.8, 207.7, 246.9}};
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate, b = std::fmod(t, beat), bar = t / (4 * beat);
        const int ci = static_cast<int>(bar) % 4;
        rng = rng * 1664525u + 1013904223u;
        const float nz = static_cast<float>(rng >> 8) / 8388608.0f - 1.0f;
        const float kick = 0.55f * static_cast<float>(std::exp(-b * 14.0) * std::sin(2 * std::numbers::pi * (50.0 + 90.0 * std::exp(-b * 30.0)) * b));
        const float bass = 0.22f * static_cast<float>(std::sin(2 * std::numbers::pi * chord[ci][0] * 0.25 * t));
        float pad = 0.0f;
        for (double f : chord[ci]) pad += 0.05f * static_cast<float>(std::sin(2 * std::numbers::pi * f * t) + 0.3 * std::sin(4 * std::numbers::pi * f * t));
        const double hb = std::fmod(t, beat * 0.5);
        const float hat = (hb < 0.03 ? 0.10f * static_cast<float>(1.0 - hb / 0.03) : 0.0f) * nz;
        const float hiss = 0.006f * nz;
        s->left[i] = kick + bass + pad + hat + hiss;
        s->right[i] = kick + bass + 0.8f * pad + hat - hiss;
    }
    s->name = "HEM demo groove (made up for testing)";
    return s;
}

std::string mmss(double sec) {
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(sec / 60.0), static_cast<int>(std::fmod(sec, 60.0)));
    return b;
}

struct App {
    RackHost host;
    hc::params::Settings settings;
    fs::path settingsFile;
    std::vector<Device> devices;
    int deviceIndex = 0;
    std::string notice;
    std::chrono::steady_clock::time_point noticeAt{};
    bool noticeAlert = false;

    // song
    std::string songName, songInfo;
    std::vector<float> overview;
    fs::path songPath;
    std::thread loader;
    std::atomic<float> loadProgress{0.0f};
    std::atomic<bool> loading{false}, loadDone{false};
    std::mutex loadMutex;
    std::shared_ptr<Song> loadedSong;
    std::vector<float> loadedOverview;
    std::string loadedInfo, loadError;

    // export
    std::thread exporter;
    std::atomic<float> exportProgress{-1.0f};
    std::atomic<bool> exportDone{false};
    std::mutex exportMutex;
    std::string exportMessage;
    bool exportOk = false;

    // chain + faceplate
    int selected = -1;
    bool picker = false;
    int pickerAt = -1;
    FaceUi face;
    std::string savedChain;

    RackProcessor& rack() { return host.rack(); }

    void say(const std::string& s, bool alert = false) {
        notice = s;
        noticeAlert = alert;
        noticeAt = std::chrono::steady_clock::now();
    }

    ~App() {
        if (loader.joinable()) loader.join();
        if (exporter.joinable()) exporter.join();
    }

    // ---- faceplate ----
    void rebuildFace() {
        if (face.feed) face.feed->setSpectrumActive(false);
        face = FaceUi{};
        hc::fx::Effect* fx = selected >= 0 ? rack().effect(selected) : nullptr;
        if (!fx) return;
        face.info = rack::findPlugin(pluginName(*fx));
        if (!face.info) return;
        face.slot = selected;
        face.fx = fx;
        face.layout = std::make_unique<ui::FaceLayout>(ui::faceLayout(*face.info));
        face.view = std::make_unique<ui::FaceView>(*face.layout, fx->params());
        face.host = std::make_unique<SlotHost>(*fx, rack(), selected);
        face.feed = std::make_unique<ui::EffectFeed>(*fx);
        face.feed->setSampleRate(kRate);
        face.feed->setSpectrumActive(true);
        face.controller = std::make_unique<ui::FaceController>(*face.layout, fx->params(), *face.host, *face.feed, face.state);
    }
    void select(int slot) {
        selected = slot;
        rebuildFace();
    }

    // ---- chain ----
    void addPlugin(const char* name, int at) {
        int slot = -1;
        if (at >= 0 && at < kMaxSlots && !rack().effect(at)) slot = at;
        for (int s = std::max(0, selected + 1); slot < 0 && s < kMaxSlots; ++s) if (!rack().effect(s)) slot = s;
        for (int s = 0; slot < 0 && s < kMaxSlots; ++s) if (!rack().effect(s)) slot = s;
        if (slot < 0) { say("All 8 slots are full - remove one first (DEL)", true); return; }
        auto fx = makePlugin(name);
        if (!fx) return;
        if (!rack().setSlot(slot, std::move(fx))) { say("Busy - try again", true); return; }
        select(slot);
        say(std::string(name) + " added to slot " + std::to_string(slot + 1));
    }
    void removeSlot(int slot) {
        const std::string name = rack().effect(slot) ? pluginName(*rack().effect(slot)) : "";
        if (face.slot == slot) { if (face.feed) face.feed->setSpectrumActive(false); face = FaceUi{}; }
        (void)rack().setSlot(slot, nullptr);
        if (selected == slot) {
            int next = -1;
            for (int s = 0; s < kMaxSlots && next < 0; ++s) if (rack().effect(s)) next = s;
            select(next);
        }
        say(name + " removed");
    }
    void moveSlot(int slot, int dir) {
        const int to = slot + dir;
        if (to < 0 || to >= kMaxSlots) return;
        if (!rack().moveSlot(slot, to)) return;
        if (selected == slot) selected = to;
        else if (selected == to) selected = slot;
        rebuildFace();
    }
    void restoreChain(const std::string& text) {
        std::string note;
        for (const SlotState& s : chainFromText(text, &note)) {
            if (auto fx = makePlugin(s)) {
                (void)rack().setSlot(s.slot, std::move(fx));
                rack().setSlotOn(s.slot, s.on);
            }
        }
        if (!note.empty()) say("Some of the saved chain could not be restored: " + note, true);
    }

    // ---- song loading (worker thread) ----
    void loadSong(const fs::path& path) {
        if (loading.load()) { say("Still loading the last song - one moment", true); return; }
        if (loader.joinable()) loader.join();
        loading = true;
        loadDone = false;
        loadProgress = 0.0f;
        songPath = path;
        loader = std::thread([this, path] {
            std::shared_ptr<Song> song;
            std::vector<float> ov;
            std::string info, error;
            auto dec = hc::media::decodeFile(path, [this](float f) { loadProgress = 0.6f * f; return true; });
            if (!dec) {
                error = dec.error();
            } else {
                const std::uint32_t inRate = dec->sampleRate;
                const double seconds = static_cast<double>(dec->frames()) / std::max(1u, inRate);
                if (!hc::media::resampleTo(*dec, static_cast<std::uint32_t>(kRate), [this](float f) { loadProgress = 0.6f + 0.35f * f; return true; })) {
                    error = "could not convert the sample rate";
                } else {
                    song = std::make_shared<Song>();
                    song->left = std::move(dec->left);
                    song->right = std::move(dec->right);
                    song->name = toUtf8(path.stem().wstring());
                    ov = makeOverview(song->left, song->right);
                    char b[160];
                    if (inRate == static_cast<std::uint32_t>(kRate))
                        std::snprintf(b, sizeof b, "%s - 48 kHz - %s", dec->format.c_str(), mmss(seconds).c_str());
                    else
                        std::snprintf(b, sizeof b, "%s - %.1f kHz played at 48 kHz - %s", dec->format.c_str(), inRate / 1000.0, mmss(seconds).c_str());
                    info = b;
                }
            }
            std::lock_guard lock(loadMutex);
            loadedSong = std::move(song);
            loadedOverview = std::move(ov);
            loadedInfo = std::move(info);
            loadError = std::move(error);
            loadProgress = 1.0f;
            loadDone = true;
        });
    }
    void setSong(std::shared_ptr<Song> song, std::vector<float> ov, std::string info) {
        songName = song->name;
        songInfo = std::move(info);
        overview = std::move(ov);
        if (!rack().submitSong(std::move(song))) { say("Busy - open the song again", true); return; }
        say("Loaded " + songName + "  (" + songInfo + ")  -  SPACE plays / pauses");
    }
    void pollLoad() {
        if (!loadDone.load()) return;
        loadDone = false;
        loading = false;
        if (loader.joinable()) loader.join();
        std::lock_guard lock(loadMutex);
        if (!loadedSong) { say("Could not open " + toUtf8(songPath.filename().wstring()) + ": " + loadError, true); return; }
        settings.set("app", "folder", toUtf8(songPath.parent_path().wstring()));
        setSong(std::move(loadedSong), std::move(loadedOverview), std::move(loadedInfo));
    }

    // ---- export (worker thread) ----
    void startExport(HWND owner) {
        auto song = rack().song();
        if (!song) { say("Open a song first", true); return; }
        if (exportProgress.load() >= 0.0f) { say("Already exporting", true); return; }
        fs::path folder = fromUtf8(settings.get("app", "exportFolder"));
        if (folder.empty()) folder = songPath.empty() ? musicDir() : songPath.parent_path();
        const fs::path out = askSaveWav(owner, folder / (fromUtf8(song->name) + L" (HEM-Elite-Rack).wav"));
        if (out.empty()) return;
        settings.set("app", "exportFolder", toUtf8(out.parent_path().wstring()));
        if (exporter.joinable()) exporter.join();
        const std::vector<SlotState> chain = snapshotChain(rack());
        exportProgress = 0.0f;
        exportDone = false;
        exporter = std::thread([this, song, chain, out] {
            std::vector<float> l, r;
            std::string err, msg;
            bool ok = renderOffline(*song, chain, l, r, [this](float f) { exportProgress = 0.95f * f; return true; }, &err);
            if (ok) ok = hc::media::writeWav(out, l.data(), r.data(), l.size(), static_cast<std::uint32_t>(kRate), hc::media::WavEncoding::Pcm24);
            if (ok) {
                float pk = 0.0f;
                for (std::size_t i = 0; i < l.size(); ++i) pk = std::max({pk, std::fabs(l[i]), std::fabs(r[i])});
                const double lufs = hc::media::integratedLufs(l.data(), r.data(), l.size(), kRate);
                char b[200];
                std::snprintf(b, sizeof b, "Exported %s  -  %.1f LUFS integrated, peak %.1f dBFS%s", toUtf8(out.filename().wstring()).c_str(), lufs,
                              20.0 * std::log10(std::max(pk, 1e-9f)), pk > 1.0f ? "  (OVER 0 dBFS: the file clips - turn OUTPUT down)" : "");
                msg = b;
            } else {
                msg = "Export failed: " + (err.empty() ? std::string("could not write the file") : err);
            }
            std::lock_guard lock(exportMutex);
            exportMessage = msg;
            exportOk = ok;
            exportDone = true;
        });
    }
    void pollExport() {
        if (!exportDone.load()) return;
        exportDone = false;
        if (exporter.joinable()) exporter.join();
        exportProgress = -1.0f;
        std::lock_guard lock(exportMutex);
        say(exportMessage, !exportOk);
    }

    void openDevice(int index) {
        deviceIndex = std::clamp(index, 0, static_cast<int>(devices.size()) - 1);
        const Device& d = devices[static_cast<std::size_t>(deviceIndex)];
        if (host.open(d)) {
            say("Playing through " + host.deviceName() + "  (" + host.format() + ")");
            settings.set("app", "device", d.offline ? "offline" : toUtf8(d.id));
        } else {
            say(host.problem(), true);
        }
    }

    void saveSettings() {
        savedChain = chainToText(snapshotChain(rack()));
        settings.setSectionText("chain", savedChain);
        settings.set("app", "match", rack().match() ? "1" : "0");
        settings.set("app", "loop", rack().loop() ? "1" : "0");
        settings.set("app", "selected", std::to_string(selected));
        (void)settings.save(settingsFile);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const Options opts = parseOptions();
    hc::rt::configureProcessForAudio();
    hc::io::ComApartment com(hc::io::Apartment::SingleThreaded);   // the file dialogs want an STA

    const hc::io::CpuReport cpu = hc::io::probeCpu();
    if (!cpu.avx2 || !cpu.fma) {
        fatal("This computer's processor does not have AVX2 / FMA, which HEM-Elite-Rack needs (Intel 2013+ / AMD 2015+).");
        return 2;
    }

    App a;
    a.settingsFile = dataDir() / "settings.ini";
    a.settings.load(a.settingsFile);
    a.rack().setMatch(a.settings.get("app", "match", "1") != "0");
    a.rack().setLoop(a.settings.get("app", "loop", "0") == "1");

    if (opts.demo) {   // CI / showing it off: a made-up song and a chain with something to look at
        a.setSong(demoSong(), {}, "made-up test audio - 20 s");
        a.overview = makeOverview(a.rack().song()->left, a.rack().song()->right);
        if (opts.demo2) {   // the vintage chain: tape, tube EQ, three compressors
            a.addPlugin("TAPE", 0);
            if (auto* e = a.rack().effect(0)) { e->set(rack::tape::Input, 6); e->set(rack::tape::Wow, 15); }
            a.addPlugin("TUBE EQ", 1);
            if (auto* e = a.rack().effect(1)) { e->set(rack::tubeeq::LowBoost, 6); e->set(rack::tubeeq::LowAtten, 4); e->set(rack::tubeeq::HighBoost, 4); }
            a.addPlugin("VARI-MU", 2);
            if (auto* e = a.rack().effect(2)) e->set(rack::varimu::Threshold, -26);
            a.addPlugin("OPTO", 3);
            if (auto* e = a.rack().effect(3)) e->set(rack::opto::PeakReduction, 55);
            a.addPlugin("FET", 4);
            if (auto* e = a.rack().effect(4)) { e->set(rack::fet::Input, 20); e->set(rack::fet::Output, -10); }
            a.addPlugin("LIMITER", 5);
            if (auto* e = a.rack().effect(5)) e->set(rack::limiter::Input, 6);
        } else {
            a.addPlugin("RESTORER", 0);
            if (auto* e = a.rack().effect(0)) e->set(rack::restorer::Reduction, 6.0f);
            a.addPlugin("ELITE EQ", 1);
            if (auto* e = a.rack().effect(1)) {
                using namespace rack::eq;
                const struct { int t; float f, g, q; int st; } bands[] = {{static_cast<int>(Type::LowCut), 28, 0, 0.71f, 0},
                                                                          {static_cast<int>(Type::Bell), 220, -3.5f, 1.4f, 0},
                                                                          {static_cast<int>(Type::Bell), 3200, 2.5f, 0.9f, 1},
                                                                          {static_cast<int>(Type::HighShelf), 11000, 3.0f, 0.71f, 2}};
                for (int b = 0; b < 4; ++b) {
                    e->set(bandParam(b, On), 1);
                    e->set(bandParam(b, TypeC), static_cast<float>(bands[b].t));
                    e->set(bandParam(b, Freq), bands[b].f);
                    e->set(bandParam(b, Gain), bands[b].g);
                    e->set(bandParam(b, Q), bands[b].q);
                    e->set(bandParam(b, StereoC), static_cast<float>(bands[b].st));
                }
                e->set(bandParam(1, Dyn), 1);
                e->set(bandParam(1, Threshold), -30);
            }
            a.addPlugin("CONSOLE EQ", 2);
            if (auto* e = a.rack().effect(2)) {
                using namespace rack::console;
                e->set(HpfOn, 1); e->set(HpfHz, 60); e->set(LfGain, 3); e->set(LfHz, 90); e->set(LmfGain, -4); e->set(LmfHz, 350);
                e->set(LmfQ, 1.4f); e->set(HmfGain, 2.5f); e->set(HmfHz, 3500); e->set(HfGain, 4); e->set(HfHz, 10000); e->set(Curve, 1);
            }
            a.addPlugin("BUS GLUE", 3);
            if (auto* e = a.rack().effect(3)) { e->set(rack::glue::Threshold, -22); e->set(rack::glue::Makeup, 2); }
        }
        a.select(std::clamp(opts.select > 0 ? opts.select - 1 : 1, 0, kMaxSlots - 1));
    } else {
        a.restoreChain(a.settings.sectionText("chain"));
        const int sel = std::atoi(a.settings.get("app", "selected", "-1").c_str());
        int first = -1;
        for (int s = 0; s < kMaxSlots && first < 0; ++s) if (a.rack().effect(s)) first = s;
        a.select(sel >= 0 && sel < kMaxSlots && a.rack().effect(sel) ? sel : first);
    }
    a.savedChain = chainToText(snapshotChain(a.rack()));

    a.devices = RackHost::listDevices();
    {
        int idx = 0;
        const std::string saved = a.settings.get("app", "device");
        for (std::size_t i = 0; i < a.devices.size(); ++i) {
            const Device& d = a.devices[i];
            if (opts.offline ? d.offline : (!saved.empty() && (d.offline ? saved == "offline" : toUtf8(d.id) == saved))) idx = static_cast<int>(i);
        }
        a.openDevice(idx);
    }

    hc::ui::GlWindow window;
    std::string err;
    if (!window.create(L"HEM-Elite-Rack " + fromUtf8(HEM_RACK_VERSION_STR), 1600, 1000, true, &err)) {
        fatal("Could not start the display: " + err + "\nUpdate your graphics driver (OpenGL 3.3 is needed).");
        return 3;
    }
    const fs::path assets = exeDir() / "assets";
    const ui::SharedFaceArt* art = ui::sharedFaceArt(assets, &err);
    if (!art) {
        fatal("HEM-Elite-Rack could not load its artwork: " + err + "\nPlease reinstall (assets folder: " + toUtf8(assets.wstring()) + ").");
        return 3;
    }
    hc::ui::Renderer renderer;
    if (!renderer.init(4, &err) || !renderer.setParts(art->parts, &err)) { fatal("GPU error: " + err); return 3; }
    hc::ui::FontAtlas font;
    RackView view;

    if (!opts.song.empty()) a.loadSong(opts.song);
    else if (!opts.demo) a.say("Open a song (OPEN SONG, or drop a WAV / MP3 / FLAC on the window), then add plug-ins on the left");

    std::vector<std::pair<std::string, std::string>> pickerItems;
    for (const auto& p : rack::plugins()) pickerItems.emplace_back(p.name, std::string(p.suite) + "  -  " + p.blurb);

    hc::ui::Canvas canvas;
    int fbW = 0, fbH = 0;
    float bakedScale = 0.0f;
    std::map<std::pair<int, int>, hc::ui::TextureLevels> plates;   // (face, ppu × 1000) -> plate texture
    std::pair<int, int> plateKey{-1, -1}, wantPlateKey{-1, -1};
    auto plateChangedAt = std::chrono::steady_clock::now();
    Hit hover;
    bool faceDrag = false, seekDrag = false;
    float peakDb[2] = {-120.0f, -120.0f};
    const auto started = std::chrono::steady_clock::now();
    auto lastFrame = started, lastSave = started;
    bool shotTaken = false;
    const HWND hwnd = static_cast<HWND>(window.nativeHandle());

    auto toPlate = [&](float x, float y, float& px, float& py) {
        float ox, oy, z;
        RackView::facePlacement(ox, oy, z);
        px = (x - ox) / z;
        py = (y - oy) / z;
    };

    while (window.pump()) {
        // ---- size: canvas, render targets, fonts ----
        const int w = std::max(1, window.pixelWidth()), h = std::max(1, window.pixelHeight());
        if (w != fbW || h != fbH) {
            fbW = w;
            fbH = h;
            canvas = hc::ui::Canvas{};
            canvas.scale = std::min(static_cast<float>(w) / RackView::kW, static_cast<float>(h) / RackView::kH);
            canvas.offsetX = std::round((static_cast<float>(w) - RackView::kW * canvas.scale) * 0.5f);
            canvas.offsetY = std::round((static_cast<float>(h) - RackView::kH * canvas.scale) * 0.5f);
            canvas.pixelW = static_cast<float>(w);
            canvas.pixelH = static_cast<float>(h);
            if (!renderer.setTargetSize(w, h, &err)) { fatal("GPU error: " + err); break; }
            if (std::fabs(canvas.scale - bakedScale) > 1e-4f) {
                if (!font.bake(art->fonts, canvas.scale) || !renderer.setFont(font, &err)) { fatal("Could not prepare the fonts: " + err); break; }
                bakedScale = canvas.scale;
            }
            renderer.beginStatic(canvas);
            renderer.fillRound({-4000, -4000, 12000, 12000}, 0, hc::ui::rgb8(0x0A0D0F), hc::ui::Blend::Over);
            renderer.endStatic();
        }

        // ---- input ----
        for (const auto& e : window.takeEvents()) {
            const float x = canvas.toDesignX(e.x), y = canvas.toDesignY(e.y);
            RackViewState probe;
            probe.picker = a.picker;
            probe.pickerItems = pickerItems;
            for (int s = 0; s < kMaxSlots; ++s) probe.slots[static_cast<std::size_t>(s)].used = a.rack().effect(s) != nullptr;
            switch (e.type) {
            case hc::ui::InputEvent::Type::MouseDown: {
                const Hit hh = view.hit(x, y, probe);
                switch (hh.action) {
                case Action::OpenSong: {
                    fs::path folder = fromUtf8(a.settings.get("app", "folder"));
                    if (folder.empty()) folder = musicDir();
                    const fs::path p = askOpenSong(hwnd, folder);
                    if (!p.empty()) a.loadSong(p);
                    break;
                }
                case Action::Export: a.startExport(hwnd); break;
                case Action::Device: a.openDevice((a.deviceIndex + 1) % static_cast<int>(a.devices.size())); break;
                case Action::Play:
                    if (!a.rack().song()) { a.say("Open a song first", true); break; }
                    if (a.rack().wantsPlay()) a.rack().pause();
                    else {
                        if (a.rack().playhead() >= a.rack().songFrames()) a.rack().seek(0);
                        a.rack().play();
                    }
                    break;
                case Action::Stop: a.rack().stop(); break;
                case Action::Seek:
                    if (a.rack().songFrames() > 0) {
                        a.rack().seek(static_cast<std::uint64_t>(hh.value * static_cast<double>(a.rack().songFrames() - 1)));
                        seekDrag = true;
                    }
                    break;
                case Action::PlayA: a.rack().setOriginal(false); break;
                case Action::PlayB: a.rack().setOriginal(true); break;
                case Action::Match: a.rack().setMatch(!a.rack().match()); break;
                case Action::Loop: a.rack().setLoop(!a.rack().loop()); break;
                case Action::SlotSelect: a.select(hh.index); break;
                case Action::SlotOn: a.rack().setSlotOn(hh.index, !a.rack().slotOn(hh.index)); break;
                case Action::SlotUp: a.moveSlot(hh.index, -1); break;
                case Action::SlotDown: a.moveSlot(hh.index, +1); break;
                case Action::SlotRemove: a.removeSlot(hh.index); break;
                case Action::AddOpen: a.picker = true; a.pickerAt = hh.index; break;
                case Action::AddPlugin:
                    a.picker = false;
                    a.addPlugin(rack::plugins()[static_cast<std::size_t>(hh.index)].name, a.pickerAt);
                    break;
                case Action::PickerClose: a.picker = false; break;
                case Action::Face:
                    if (a.face.controller) {
                        float px, py;
                        toPlate(x, y, px, py);
                        a.face.controller->mouseDown(px, py, e.doubleClick, e.fine);
                        faceDrag = true;
                    }
                    break;
                case Action::None: break;
                }
                break;
            }
            case hc::ui::InputEvent::Type::MouseMove:
                if (faceDrag && a.face.controller) {
                    float px, py;
                    toPlate(x, y, px, py);
                    a.face.controller->mouseMove(px, py, e.fine);
                } else if (seekDrag) {
                    const Hit hh = view.hit(x, y, probe);
                    if (hh.action == Action::Seek && a.rack().songFrames() > 0)
                        a.rack().seek(static_cast<std::uint64_t>(hh.value * static_cast<double>(a.rack().songFrames() - 1)));
                } else {
                    hover = view.hit(x, y, probe);
                    if (a.face.controller) {
                        float px, py;
                        toPlate(x, y, px, py);
                        if (hover.action == Action::Face) a.face.controller->mouseMove(px, py, e.fine);
                        else a.face.state.hover = -1, a.face.state.hoverNode = -1;
                    }
                }
                break;
            case hc::ui::InputEvent::Type::MouseUp:
                if (faceDrag && a.face.controller) a.face.controller->mouseUp();
                faceDrag = false;
                seekDrag = false;
                break;
            case hc::ui::InputEvent::Type::Wheel:
                if (a.face.controller && view.hit(x, y, probe).action == Action::Face) {
                    float px, py;
                    toPlate(x, y, px, py);
                    a.face.controller->wheel(px, py, e.wheel, e.fine);
                }
                break;
            case hc::ui::InputEvent::Type::Key:
                if (e.key == VK_SPACE && a.rack().song()) {
                    if (a.rack().wantsPlay()) a.rack().pause();
                    else { if (a.rack().playhead() >= a.rack().songFrames()) a.rack().seek(0); a.rack().play(); }
                } else if ((e.key == VK_LEFT || e.key == VK_RIGHT) && a.rack().songFrames() > 0) {
                    const double step = (e.key == VK_LEFT ? -5.0 : 5.0) * kRate;
                    const double to = std::clamp(static_cast<double>(a.rack().playhead()) + step, 0.0, static_cast<double>(a.rack().songFrames() - 1));
                    a.rack().seek(static_cast<std::uint64_t>(to));
                } else if (e.key == 'A' && !e.ctrl) a.rack().setOriginal(false);
                else if (e.key == 'B' && !e.ctrl) a.rack().setOriginal(true);
                else if (e.key == VK_ESCAPE) a.picker = false;
                break;
            case hc::ui::InputEvent::Type::Drop:
            case hc::ui::InputEvent::Type::OpenFile:
                for (const auto& f : e.files)
                    if (hc::media::isAudioFileName(f)) { a.loadSong(f); break; }
                break;
            default: break;
            }
        }

        a.host.poll();
        a.pollLoad();
        a.pollExport();
        if (a.face.slot >= 0 && a.rack().effect(a.face.slot) != a.face.fx) a.rebuildFace();   // chain changed under it

        // ---- the selected plug-in's metal plate, rendered at the screen's pixel density ----
        if (a.face.info) {
            float ox, oy, z;
            RackView::facePlacement(ox, oy, z);
            const float ppu = std::min({canvas.scale * z, 4096.0f / ui::kPlateW, 4096.0f / ui::kPlateH});
            const std::pair<int, int> key{static_cast<int>(a.face.info->face), static_cast<int>(std::lround(ppu * 1000.0f))};
            if (key != wantPlateKey) { wantPlateKey = key; plateChangedAt = std::chrono::steady_clock::now(); }
            // While the window is being resized, wait until it settles (a plate takes a moment to render).
            const bool settled = std::chrono::steady_clock::now() - plateChangedAt > std::chrono::milliseconds(250) || plateKey.first != key.first;
            if (key != plateKey && settled) {
                auto it = plates.find(key);
                if (it == plates.end()) {
                    for (auto i = plates.begin(); i != plates.end();) i = i->first.first == key.first ? plates.erase(i) : std::next(i);
                    it = plates.emplace(key, hc::ui::buildMips(hc::ui::premultiply(ui::renderRackPlate(*a.face.layout, art->brand, ppu)))).first;
                }
                if (renderer.setPanel(it->second, &err)) plateKey = key;
                else a.say("GPU: " + err, true);
            }
        }

        // ---- persist (at most every 5 s, only when the chain changed) ----
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::min(0.1f, std::chrono::duration<float>(now - lastFrame).count());
        lastFrame = now;
        if (now - lastSave > std::chrono::seconds(5) && !opts.demo) {
            lastSave = now;
            if (chainToText(snapshotChain(a.rack())) != a.savedChain) a.saveSettings();
        }

        if (window.minimized()) { ::Sleep(50); continue; }

        // ---- view state ----
        RackViewState vs;
        vs.songName = a.songName;
        vs.songInfo = a.songInfo;
        vs.loading = a.loading.load();
        vs.loadProgress = a.loadProgress.load();
        vs.overview = a.overview.empty() ? nullptr : &a.overview;
        vs.position = static_cast<double>(a.rack().playhead()) / kRate;
        vs.length = static_cast<double>(a.rack().songFrames()) / kRate;
        vs.playing = a.rack().wantsPlay();
        vs.loop = a.rack().loop();
        vs.original = a.rack().original();
        vs.match = a.rack().match();
        for (int ch = 0; ch < 2; ++ch) {
            const float p = a.rack().peak(ch);
            const float pd = 20.0f * std::log10(std::max(p, 1e-6f));
            peakDb[ch] = std::max(pd, peakDb[ch] - 24.0f * dt);   // falls 24 dB a second
            vs.peakDb[ch] = peakDb[ch];
        }
        vs.outLufs = a.host.running() && a.rack().wantsPlay() ? a.rack().outLufs() : -144.0f;
        vs.matchDb = a.rack().matchGainDb();
        vs.latencyMs = static_cast<float>(a.rack().latency() / kRate * 1000.0);
        vs.cpuPct = a.host.cpuAvg();
        for (int s = 0; s < kMaxSlots; ++s) {
            SlotView& sv = vs.slots[static_cast<std::size_t>(s)];
            const hc::fx::Effect* e = a.rack().effect(s);
            sv.used = e != nullptr;
            if (!e) continue;
            sv.on = a.rack().slotOn(s);
            sv.name = pluginName(*e);
            if (const auto* info = rack::findPlugin(sv.name.c_str())) sv.suite = info->suite;
        }
        vs.selected = a.selected;
        vs.picker = a.picker;
        vs.pickerItems = pickerItems;
        vs.device = a.host.running() ? a.host.deviceName() : std::string("none - click to choose");
        const bool fresh = !a.notice.empty() && now - a.noticeAt < std::chrono::seconds(a.noticeAlert ? 15 : 8);
        vs.status = fresh ? a.notice : a.host.running() ? "Playing through " + a.host.deviceName() + "  (" + a.host.format() + ")" : a.host.problem();
        vs.alert = fresh ? a.noticeAlert : !a.host.running();
        vs.exportProgress = a.exportProgress.load();

        // ---- draw ----
        renderer.beginFrame(canvas);
        view.drawBack(renderer, vs, hover);
        if (a.face.view && plateKey.first == static_cast<int>(a.face.info->face)) {
            float ox, oy, z;
            RackView::facePlacement(ox, oy, z);
            a.face.view->draw(renderer, *a.face.host, *a.face.feed, a.face.state, ox, oy, z, true);
        }
        view.drawFront(renderer, vs, hover);

        const double alive = std::chrono::duration<double>(now - started).count();
        if (!opts.screenshot.empty() && !shotTaken && alive >= std::max(1.5, opts.exitAfter - 1.0)) {
            renderer.endFrameToCapture();
            (void)hc::ui::writePng(opts.screenshot, renderer.capture());
            shotTaken = true;
            continue;
        }
        renderer.endFrame(0);
        window.swap();
        if (opts.demo && alive > 0.3 && !a.rack().wantsPlay() && alive < 1.0) a.rack().play();
        if (opts.exitAfter > 0.0 && alive >= opts.exitAfter) break;
    }

    if (a.face.feed) a.face.feed->setSpectrumActive(false);
    a.host.close();
    if (!opts.demo) a.saveSettings();
    return 0;
}
