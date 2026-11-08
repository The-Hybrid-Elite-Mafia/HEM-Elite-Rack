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
#include "TakeWriter.hpp"

#include "hem/rack/ColorFx.hpp"
#include "hem/rack/MoreDynamics.hpp"
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
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <string_view>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <mutex>
#include <numbers>
#include <thread>
#include <vector>

using namespace hem;
using namespace hem::app;
namespace fs = std::filesystem;
static_assert(kStemSlots == kMaxStems, "the STEMS panel's rows are the engine's stems");

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

fs::path documentsDir() {
    PWSTR p = nullptr;
    fs::path out;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p))) { out = p; ::CoTaskMemFree(p); }
    return out;
}

// Command line (CI and support): --exit-after <s> --screenshot <png> --device offline --demo | --demo2 --select <slot 1..8> --closed (no plate open) --picker <tab 1..7> (the plug-in picker open on that tab) --stems (13 made-up stems, the STEMS panel open) [song or stems...]
struct Options {
    double   exitAfter = 0.0;
    fs::path screenshot;
    std::vector<fs::path> files;   // a song, or stems
    bool     offline = false, demo = false, demo2 = false;   // --demo2: the vintage chain (ARCHIVE .. LOCKDOWN)
    int      select = 0;
    bool     closed = false;
    int      picker = 0;   // > 0: open the picker on that tab
    bool     stems = false;  // with --demo: 13 made-up stems and the STEMS panel open
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
        else if (k == L"--closed") o.closed = true;
        else if (k == L"--picker" && i + 1 < argc) o.picker = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        else if (k == L"--stems") o.demo = o.stems = true;
        else if (k == L"--select" && i + 1 < argc) o.select = static_cast<int>(std::wcstol(argv[++i], nullptr, 10));
        else if (!k.empty() && k[0] != L'-') o.files.emplace_back(k);
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

// A song, or up to 13 stems at once (Ctrl+click / Shift+click / Ctrl+A to pick several).
std::vector<fs::path> askOpenAudio(HWND owner, const fs::path& folder, bool several) {
    std::vector<fs::path> out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    const COMDLG_FILTERSPEC filters[] = {{L"Audio (WAV, MP3, FLAC)", L"*.wav;*.wave;*.mp3;*.flac"}, {L"All files", L"*.*"}};
    dlg->SetFileTypes(2, filters);
    dlg->SetTitle(several ? L"Open a song, or up to 13 stems at once (Ctrl+click or Ctrl+A to pick several)" : L"Load a stem into this slot");
    FILEOPENDIALOGOPTIONS o = 0;
    dlg->GetOptions(&o);
    dlg->SetOptions(o | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | (several ? FOS_ALLOWMULTISELECT : 0));
    setFolder(dlg, folder);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items)) && items) {
            DWORD n = 0;
            items->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* it = nullptr;
                PWSTR path = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &path))) { out.emplace_back(path); ::CoTaskMemFree(path); }
                if (it) it->Release();
            }
            items->Release();
        }
    }
    dlg->Release();
    return out;
}

// Stems in name order ("01 Kick", "02 Snare", ... "10 Pad"): numbers inside names compare as numbers.
bool stemOrder(const fs::path& a, const fs::path& b) {
    const std::wstring x = a.filename().wstring(), y = b.filename().wstring();
    std::size_t i = 0, j = 0;
    while (i < x.size() && j < y.size()) {
        if (std::iswdigit(x[i]) && std::iswdigit(y[j])) {
            std::size_t i2 = i, j2 = j;
            while (i2 < x.size() && std::iswdigit(x[i2])) ++i2;
            while (j2 < y.size() && std::iswdigit(y[j2])) ++j2;
            const unsigned long long u = std::wcstoull(x.substr(i, i2 - i).c_str(), nullptr, 10), v = std::wcstoull(y.substr(j, j2 - j).c_str(), nullptr, 10);
            if (u != v) return u < v;
            i = i2;
            j = j2;
            continue;
        }
        const wchar_t c = static_cast<wchar_t>(std::towlower(x[i])), d = static_cast<wchar_t>(std::towlower(y[j]));
        if (c != d) return c < d;
        ++i;
        ++j;
    }
    return x.size() - i < y.size() - j;
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

// The faceplate's view of one slot: knobs turn the live plug-in's atomics; POWER (on / bypassed) is the slot's ON.
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

// 13 made-up stems for demos and CI screenshots (20 s each, mono where the part is mono).
StemSet demoStems() {
    StemSet set{};
    const std::size_t n = static_cast<std::size_t>(20.0 * kRate);
    const double bpm = 92.0, beat = 60.0 / bpm;
    const double chord[4][3] = {{220.0, 261.6, 329.6}, {174.6, 220.0, 261.6}, {196.0, 246.9, 293.7}, {164.8, 207.7, 246.9}};
    const char* names[kMaxStems] = {"01 Kick", "02 Snare", "03 Hats", "04 808 Bass", "05 Keys L", "06 Keys R", "07 Pad",
                                    "08 Lead Synth", "09 Lead Vocal", "10 Vocal Double", "11 Ad-libs", "12 Riser FX", "13 Room"};
    std::uint32_t rng = 11;
    for (int k = 0; k < kMaxStems; ++k) {
        auto s = std::make_shared<Song>();
        const bool stereo = k == 6 || k == 11 || k == 12;
        s->left.assign(n, 0.0f);
        if (stereo) s->right.assign(n, 0.0f);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate, b = std::fmod(t, beat), bar = t / (4 * beat);
            const int ci = static_cast<int>(bar) % 4;
            rng = rng * 1664525u + 1013904223u;
            const float nz = static_cast<float>(rng >> 8) / 8388608.0f - 1.0f;
            const double tw = 2 * std::numbers::pi * t;
            float v = 0.0f, w = 0.0f;
            switch (k) {
            case 0: v = 0.5f * static_cast<float>(std::exp(-b * 14.0) * std::sin(2 * std::numbers::pi * (50.0 + 90.0 * std::exp(-b * 30.0)) * b)); break;
            case 1: { const double sb = std::fmod(t + beat, 2 * beat); v = sb < 0.12 ? 0.25f * static_cast<float>(1.0 - sb / 0.12) * (nz + 0.5f * static_cast<float>(std::sin(tw * 190.0))) : 0.0f; break; }
            case 2: { const double hb = std::fmod(t, beat * 0.5); v = hb < 0.03 ? 0.12f * static_cast<float>(1.0 - hb / 0.03) * nz : 0.0f; break; }
            case 3: v = 0.3f * static_cast<float>(std::sin(tw * chord[ci][0] * 0.25) * std::exp(-std::fmod(t, 2 * beat) * 0.8)); break;
            case 4: v = 0.06f * static_cast<float>(std::sin(tw * chord[ci][1]) * std::exp(-b * 3.0)); break;
            case 5: v = 0.06f * static_cast<float>(std::sin(tw * chord[ci][2]) * std::exp(-b * 3.0)); break;
            case 6: for (double f : chord[ci]) { v += 0.03f * static_cast<float>(std::sin(tw * f * 1.003)); w += 0.03f * static_cast<float>(std::sin(tw * f * 0.997)); } break;
            case 7: v = 0.05f * static_cast<float>(std::sin(tw * chord[ci][2] * 2.0 + 2.0 * std::sin(tw * 5.0))); break;
            case 8: v = 0.09f * static_cast<float>(std::sin(tw * chord[ci][1] * 2.0) * (0.6 + 0.4 * std::sin(tw * 1.5))); break;
            case 9: v = 0.05f * static_cast<float>(std::sin(tw * chord[ci][1] * 2.01) * (0.6 + 0.4 * std::sin(tw * 1.5))); break;
            case 10: v = std::fmod(t, 4 * beat) > 3 * beat ? 0.06f * static_cast<float>(std::sin(tw * 660.0)) : 0.0f; break;
            case 11: { const float g = static_cast<float>(std::fmod(t, 8 * beat) / (8 * beat)); v = 0.05f * g * nz; w = -0.05f * g * nz; break; }
            case 12: v = 0.008f * nz; w = 0.008f * static_cast<float>((rng >> 3) % 1000) / 500.0f - 0.008f; break;
            default: break;
            }
            s->left[i] = v;
            if (stereo) s->right[i] = (k == 6 || k == 11 || k == 12) ? w : v;
        }
        s->name = names[k];
        set[static_cast<std::size_t>(k)] = std::move(s);
    }
    return set;
}

// ---- MIDI keyboards: every MIDI input Windows has, opened at the start and again whenever one is plugged in
// or out. Their messages go straight to the rack processor (any thread; it queues them for the audio thread).
class MidiInputs {
public:
    explicit MidiInputs(RackProcessor& rack) : rack_(rack) {}
    ~MidiInputs() { closeAll(); }
    MidiInputs(const MidiInputs&) = delete;
    MidiInputs& operator=(const MidiInputs&) = delete;
    // true when the inputs changed (they were opened again)
    bool refresh() {
        const UINT n = ::midiInGetNumDevs();
        if (!first_ && n == count_) return false;
        first_ = false;
        closeAll();
        count_ = n;
        for (UINT i = 0; i < n; ++i) {
            HMIDIIN h = nullptr;
            if (::midiInOpen(&h, i, reinterpret_cast<DWORD_PTR>(&MidiInputs::callback), reinterpret_cast<DWORD_PTR>(&rack_), CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
                continue;
            if (::midiInStart(h) == MMSYSERR_NOERROR) open_.push_back(h);
            else ::midiInClose(h);
        }
        return true;
    }
    [[nodiscard]] int opened() const noexcept { return static_cast<int>(open_.size()); }

private:
    static void CALLBACK callback(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR) {
        if (msg != MIM_DATA) return;
        const auto m = static_cast<DWORD>(p1);
        reinterpret_cast<RackProcessor*>(instance)->midi(static_cast<std::uint8_t>(m & 0xFF), static_cast<std::uint8_t>((m >> 8) & 0x7F),
                                                        static_cast<std::uint8_t>((m >> 16) & 0x7F));
    }
    void closeAll() noexcept {
        for (HMIDIIN h : open_) {
            ::midiInStop(h);
            ::midiInReset(h);
            ::midiInClose(h);
        }
        open_.clear();
    }
    RackProcessor& rack_;
    std::vector<HMIDIIN> open_;
    UINT count_ = 0;
    bool first_ = true;
};

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
    // what the loader made: a song (one file) or stems (several), or one stem for one slot
    StemSet loadedStems{};
    std::array<std::string, kMaxStems> loadedStemInfo{};
    int loadedSlot = -1;                 // >= 0: one stem for that slot; -1: a whole new set
    int loadedCount = 0;
    std::vector<float> loadedOverview;
    std::string loadedName, loadedInfo, loadError;
    std::array<std::string, kMaxStems> stemInfo{};   // per stem: format, rate, length
    bool stemPanel = false;
    std::array<float, kMaxStems> stemPeakDb{};

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
    int pickerTab = 0;
    bool pickerCards = true;   // faceplate cards, or the name list
    int pickerScroll = 0;
    FaceUi face;
    std::string savedChain;

    RackProcessor& rack() { return host.rack(); }

    void say(const std::string& s, bool alert = false) {
        notice = s;
        noticeAlert = alert;
        noticeAt = std::chrono::steady_clock::now();
    }

    // live input and the takes
    int inputOption = 0;   // 0 = off; then inputChoices()
    TakeWriter takeWet, takeDry;
    std::vector<float> recWet, recDry;
    std::string takeName;
    fs::path takeFolder;

    ~App() {
        if (takeWet.isOpen()) stopRecord();
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
    // Hides the plate only: the plug-in keeps running in its slot with its settings (its name in the chain opens it again).
    void closeFace() {
        if (selected < 0) return;
        const std::string name = rack().effect(selected) ? pluginName(*rack().effect(selected)) : "";
        select(-1);
        if (!name.empty()) say(name + " closed - still running in the chain. Click its name to open it again");
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

    // ---- song / stem loading (worker thread) ----
    // One file: a song (stem 1, the others emptied). Several: stems, one per slot in name order (up to 13).
    // slot >= 0: one file into that stem slot; the others keep playing.
    void loadAudio(std::vector<fs::path> files, int slot = -1) {
        if (loading.load()) { say("Still loading - one moment", true); return; }
        files.erase(std::remove_if(files.begin(), files.end(), [](const fs::path& f) { return !hc::media::isAudioFileName(f); }), files.end());
        if (files.empty()) { say("No WAV, MP3 or FLAC files there", true); return; }
        std::sort(files.begin(), files.end(), stemOrder);
        std::string note;
        if (slot >= 0 && files.size() > 1) files.resize(1);
        if (files.size() > static_cast<std::size_t>(kMaxStems)) {
            note = "Only " + std::to_string(kMaxStems) + " stems fit - loaded the first " + std::to_string(kMaxStems) + " by name, left out " +
                   std::to_string(files.size() - kMaxStems);
            files.resize(static_cast<std::size_t>(kMaxStems));
        }
        if (loader.joinable()) loader.join();
        loading = true;
        loadDone = false;
        loadProgress = 0.0f;
        songPath = files.front();
        const StemSet current = rack().stems();   // a one-slot load: the others, for the new overview
        loader = std::thread([this, files, slot, current, note] {
            const std::size_t count = files.size();
            std::vector<std::shared_ptr<Song>> songs(count);
            std::vector<std::string> infos(count), errors(count);
            std::vector<std::atomic<float>> prog(count);
            auto decodeOne = [&](std::size_t i) {
                const fs::path& path = files[i];
                auto dec = hc::media::decodeFile(path, [&](float f) { prog[i] = 0.6f * f; return true; });
                if (!dec) { errors[i] = dec.error(); return; }
                const std::uint32_t inRate = dec->sampleRate;
                const double seconds = static_cast<double>(dec->frames()) / std::max(1u, inRate);
                if (!hc::media::resampleTo(*dec, static_cast<std::uint32_t>(kRate), [&](float f) { prog[i] = 0.6f + 0.4f * f; return true; })) {
                    errors[i] = "could not convert the sample rate";
                    return;
                }
                auto s = std::make_shared<Song>();
                s->left = std::move(dec->left);
                // a mono file, or a stereo one with both sides the same: kept once (half the memory)
                const bool same = dec->channels == 1 || dec->right == s->left;
                if (!same) s->right = std::move(dec->right);
                s->name = toUtf8(path.stem().wstring());
                char b[160];
                if (inRate == static_cast<std::uint32_t>(kRate))
                    std::snprintf(b, sizeof b, "%s - 48 kHz - %s%s", dec->format.c_str(), mmss(seconds).c_str(), same ? " - mono" : "");
                else
                    std::snprintf(b, sizeof b, "%s - %.1f kHz played at 48 kHz - %s%s", dec->format.c_str(), inRate / 1000.0, mmss(seconds).c_str(), same ? " - mono" : "");
                infos[i] = b;
                songs[i] = std::move(s);
            };
            // several files decode at once, a thread each (up to the cores there are)
            const std::size_t workers = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, count);
            std::atomic<std::size_t> next{0}, finished{0};
            std::vector<std::thread> pool;
            for (std::size_t w = 0; w < workers; ++w)
                pool.emplace_back([&] {
                    for (std::size_t i; (i = next.fetch_add(1)) < count;) {
                        decodeOne(i);
                        prog[i] = 1.0f;
                        finished.fetch_add(1);
                    }
                });
            while (finished.load() < count) {
                float sum = 0.0f;
                for (auto& p : prog) sum += p.load();
                loadProgress = 0.95f * sum / static_cast<float>(count);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            for (auto& th : pool) th.join();

            StemSet set{};
            std::array<std::string, kMaxStems> stemInfos{};
            std::string error, name, info;
            int loaded = 0;
            std::uint64_t longest = 0;
            for (std::size_t i = 0; i < count; ++i) {
                if (!songs[i]) {
                    error += (error.empty() ? "" : "; ") + toUtf8(files[i].filename().wstring()) + ": " + errors[i];
                    continue;
                }
                const std::size_t at = slot >= 0 ? static_cast<std::size_t>(slot) : static_cast<std::size_t>(loaded);
                longest = std::max(longest, songs[i]->frames());
                set[at] = songs[i];
                stemInfos[at] = infos[i];
                ++loaded;
            }
            // the overview: what plays (the stems at 0 dB, centre)
            StemSet view = slot >= 0 ? current : set;
            if (slot >= 0) view[static_cast<std::size_t>(slot)] = set[static_cast<std::size_t>(slot)];
            std::vector<float> ov;
            if (loaded > 0) {
                const auto mix = mixStems(view, StemMixes{}, "");
                ov = makeOverview(mix->left, mix->right);
            }
            if (slot < 0 && loaded == 1 && count == 1) {
                name = set[0]->name;
                info = stemInfos[0];
            } else if (slot < 0 && loaded > 0) {
                const fs::path parent = files.front().parent_path().filename();
                name = parent.empty() ? set[0]->name : toUtf8(parent.wstring());
                info = std::to_string(loaded) + (loaded == 1 ? " STEM - " : " STEMS - ") + mmss(static_cast<double>(longest) / kRate);
            }
            if (!note.empty()) error = note + (error.empty() ? "" : " - " + error);
            std::lock_guard lock(loadMutex);
            loadedStems = std::move(set);
            loadedStemInfo = std::move(stemInfos);
            loadedSlot = slot;
            loadedCount = loaded;
            loadedOverview = std::move(ov);
            loadedName = std::move(name);
            loadedInfo = std::move(info);
            loadError = std::move(error);
            loadProgress = 1.0f;
            loadDone = true;
        });
    }
    void setSong(std::shared_ptr<Song> song, std::vector<float> ov, std::string info) {
        StemSet set{};
        const std::string name = song->name;
        set[0] = std::move(song);
        std::array<std::string, kMaxStems> infos{};
        infos[0] = info;
        setStems(set, infos, name, std::move(ov), std::move(info));
    }
    void setStems(const StemSet& set, const std::array<std::string, kMaxStems>& infos, std::string name, std::vector<float> ov, std::string info) {
        if (!rack().submitStems(set)) { say("Busy - open them again", true); return; }
        songName = std::move(name);
        songInfo = std::move(info);
        overview = std::move(ov);
        stemInfo = infos;
        for (float& p : stemPeakDb) p = -120.0f;
        say("Loaded " + songName + "  (" + songInfo + ")  -  SPACE plays / pauses" + (rack().stemCount() > 1 ? "  -  STEMS (above the plate) sets each one" : ""));
    }
    void pollLoad() {
        if (!loadDone.load()) return;
        loadDone = false;
        loading = false;
        if (loader.joinable()) loader.join();
        std::lock_guard lock(loadMutex);
        if (loadedCount == 0) { say("Could not open " + toUtf8(songPath.filename().wstring()) + ": " + loadError, true); return; }
        settings.set("app", "folder", toUtf8(songPath.parent_path().wstring()));
        if (loadedSlot >= 0) {
            const auto k = static_cast<std::size_t>(loadedSlot);
            if (!rack().setStem(loadedSlot, loadedStems[k])) { say("Busy - load it again", true); return; }
            stemInfo[k] = loadedStemInfo[k];
            overview = std::move(loadedOverview);
            if (songName.empty()) songName = loadedStems[k]->name;
            songInfo = std::to_string(rack().stemCount()) + (rack().stemCount() == 1 ? " STEM - " : " STEMS - ") + mmss(static_cast<double>(rack().songFrames()) / kRate);
            say("Stem " + std::to_string(loadedSlot + 1) + ": " + loadedStems[k]->name + "  (" + stemInfo[k] + ")");
            return;
        }
        setStems(loadedStems, loadedStemInfo, loadedName, std::move(loadedOverview), loadedInfo);
        if (loadedCount > 1) stemPanel = true;   // show what came in
        if (!loadError.empty()) say(loadError, true);
    }
    void removeStem(int k) {
        const auto s = rack().stem(k);
        if (!s) return;
        if (!rack().setStem(k, nullptr)) { say("Busy - try again", true); return; }
        stemInfo[static_cast<std::size_t>(k)].clear();
        if (rack().hasAudio()) {
            const auto mix = mixStems(rack().stems(), StemMixes{}, "");
            overview = makeOverview(mix->left, mix->right);
        } else {
            overview.clear();
        }
        if (!rack().hasAudio()) { songName.clear(); songInfo.clear(); }
        say("Stem " + std::to_string(k + 1) + " (" + s->name + ") removed");
    }
    void clearStems() {
        (void)rack().submitStems(StemSet{});
        songName.clear();
        songInfo.clear();
        overview.clear();
        for (auto& i : stemInfo) i.clear();
        say("All stems cleared");
    }

    // ---- export (worker thread) ----
    void startExport(HWND owner) {
        if (!rack().hasAudio()) { say("Open a song or stems first", true); return; }
        const StemSet stems = rack().stems();
        const StemMixes mixes = rack().stemMixes();
        const std::string name = songName.empty() ? std::string("HEM-Elite-Rack") : songName;
        if (exportProgress.load() >= 0.0f) { say("Already exporting", true); return; }
        fs::path folder = fromUtf8(settings.get("app", "exportFolder"));
        if (folder.empty()) folder = songPath.empty() ? musicDir() : songPath.parent_path();
        const fs::path out = askSaveWav(owner, folder / (fromUtf8(name) + L" (HEM-Elite-Rack).wav"));
        if (out.empty()) return;
        settings.set("app", "exportFolder", toUtf8(out.parent_path().wstring()));
        if (exporter.joinable()) exporter.join();
        const std::vector<SlotState> chain = snapshotChain(rack());
        exportProgress = 0.0f;
        exportDone = false;
        exporter = std::thread([this, stems, mixes, name, chain, out] {
            std::vector<float> l, r;
            std::string err, msg;
            const auto song = mixStems(stems, mixes, name);   // the stems at their levels, pans, mutes and solos
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
        if (takeWet.isOpen()) stopRecord();
        inputOption = 0;
        rack().setInput(-1, true);
        if (host.open(d)) {
            say("Playing through " + host.deviceName() + "  (" + host.format() + ")");
            settings.set("app", "device", d.key());
        } else {
            say(host.problem(), true);
        }
    }

    // ---- live input ----
    // OFF, then per pair of inputs: the pair (stereo), the first alone, the second alone
    std::vector<std::pair<int, bool>> inputChoices() const {
        std::vector<std::pair<int, bool>> c{{-1, true}};
        const int n = host.inputs();
        for (int ch = 0; ch < n; ch += 2) {
            if (ch + 1 < n) c.emplace_back(ch, true);
            c.emplace_back(ch, false);
            if (ch + 1 < n) c.emplace_back(ch + 1, false);
        }
        return c;
    }
    std::string inputLabel() {
        const int first = rack().inputFirst();
        if (first < 0) return "INPUT: OFF";
        return rack().inputStereo() ? "INPUT: IN " + std::to_string(first + 1) + "+" + std::to_string(first + 2)
                                                             : "INPUT: IN " + std::to_string(first + 1);
    }
    void cycleInput() {
        if (takeWet.isOpen()) { say("Stop the recording first", true); return; }
        if (host.inputs() == 0) {
            say("LIVE INPUT needs an ASIO sound card with inputs - choose it with the OUTPUT button", true);
            return;
        }
        const auto c = inputChoices();
        inputOption = (inputOption + 1) % static_cast<int>(c.size());
        rack().setInput(c[static_cast<std::size_t>(inputOption)].first, c[static_cast<std::size_t>(inputOption)].second);
        if (c[static_cast<std::size_t>(inputOption)].first >= 0)
            say(inputLabel() + "  -  the chain now works on your input; the song plays alongside, as it is");
        else
            say("LIVE INPUT off - the song goes through the chain again");
    }
    void startRecord() {
        if (rack().inputFirst() < 0) { say("Turn on LIVE INPUT first (the INPUT button)", true); return; }
        takeFolder = documentsDir() / "HEM-Elite-Rack Takes";
        std::error_code ec;
        fs::create_directories(takeFolder, ec);
        int number = std::atoi(settings.get("app", "takeNumber").c_str()) + 1;
        std::string name;
        char b[64];
        for (;; ++number) {   // never over an earlier take
            std::snprintf(b, sizeof b, "Take %03d", number);
            name = b;
            if (!songName.empty()) {
                const double from = static_cast<double>(rack().playhead()) / kRate;
                std::snprintf(b, sizeof b, " (from %dm%02ds)", static_cast<int>(from / 60.0), static_cast<int>(std::fmod(from, 60.0)));
                std::string song = songName;
                for (char& ch : song)
                    if (std::string_view("\\/:*?\"<>|").find(ch) != std::string_view::npos) ch = '-';
                name += " - " + song.substr(0, 60) + b;
            }
            if (!fs::exists(takeFolder / fromUtf8(name + ".wav"))) break;
        }
        // the round trip through the sound card, and the chain's delay for the processed take, are left out of
        // the front, so the takes line up with the song from where recording started
        const auto roundTrip = static_cast<std::uint64_t>(std::lround((host.inputLatencyMs() + host.outputLatencyMs()) * kRate / 1000.0));
        if (!takeWet.open(takeFolder / fromUtf8(name + ".wav"), static_cast<std::uint32_t>(kRate), roundTrip + rack().latency()) ||
            !takeDry.open(takeFolder / fromUtf8(name + " (dry).wav"), static_cast<std::uint32_t>(kRate), roundTrip)) {
            (void)takeWet.finish();
            (void)takeDry.finish();
            say("Could not create the take files in " + toUtf8(takeFolder.wstring()), true);
            return;
        }
        takeName = name;
        settings.set("app", "takeNumber", std::to_string(number));
        recWet.assign(2 * 8192, 0.0f);
        recDry.assign(2 * 8192, 0.0f);
        if (rack().hasAudio() && !rack().wantsPlay()) {
            if (rack().playhead() >= rack().songFrames()) rack().seek(0);
            rack().play();
        }
        rack().startRecording();
        say("RECORDING " + takeName);
    }
    void drainTakes() {
        for (;;) {
            const std::size_t n = rack().takeRecorded(recWet.data(), recDry.data(), 8192);
            if (n == 0) break;
            (void)takeWet.write(recWet.data(), n);
            (void)takeDry.write(recDry.data(), n);
        }
    }
    void stopRecord() {
        rack().stopRecording();
        drainTakes();
        const std::uint64_t frames = takeWet.finish();
        (void)takeDry.finish();
        char b[64];
        std::snprintf(b, sizeof b, "%d:%02d", static_cast<int>(frames / 48000 / 60), static_cast<int>(frames / 48000 % 60));
        std::string msg = "Saved " + takeName + " (" + b + ", with the chain and dry) in Documents\\HEM-Elite-Rack Takes";
        if (rack().recordOverflow() > 0) msg += " - some of it was lost (the computer fell behind writing)";
        say(msg, rack().recordOverflow() > 0);
        saveSettings();
    }

    void saveSettings() {
        savedChain = chainToText(snapshotChain(rack()));
        settings.setSectionText("chain", savedChain);
        settings.set("app", "match", rack().match() ? "1" : "0");
        settings.set("app", "loop", rack().loop() ? "1" : "0");
        settings.set("app", "pickercards", pickerCards ? "1" : "0");
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
    a.pickerCards = a.settings.get("app", "pickercards", "1") != "0";

    if (opts.demo) {   // CI / showing it off: a made-up song (or 13 stems) and a chain with something to look at
        if (opts.stems) {
            const StemSet set = demoStems();
            std::array<std::string, kMaxStems> infos{};
            for (auto& i : infos) i = "made up for testing - 20 s";
            const auto mix = mixStems(set, StemMixes{}, "");
            a.setStems(set, infos, "HEM demo session (made-up stems)", makeOverview(mix->left, mix->right), "13 STEMS - 0:20");
            // a few settings so the panel shows what it does
            a.rack().setStemMix(3, StemMix{-3.5f, 0.0f, false, false});
            a.rack().setStemMix(4, StemMix{0.0f, -0.6f, false, false});
            a.rack().setStemMix(5, StemMix{0.0f, 0.6f, false, false});
            a.rack().setStemMix(9, StemMix{-6.0f, 0.35f, false, false});
            a.rack().setStemMix(11, StemMix{0.0f, 0.0f, true, false});
            a.rack().setStemMix(12, StemMix{kStemMinDb, 0.0f, false, false});
            a.stemPanel = true;
        } else {
            a.setSong(demoSong(), {}, "made-up test audio - 20 s");
            a.overview = makeOverview(a.rack().stem(0)->left, a.rack().stem(0)->right);
        }
        if (opts.demo2) {   // the vintage chain: tape, tube EQ, tube, compressor, bus glue, limiter
            a.addPlugin("ARCHIVE", 0);
            if (auto* e = a.rack().effect(0)) { e->set(rack::tape::Input, 6); e->set(rack::tape::Wow, 15); }
            a.addPlugin("VELVET", 1);
            if (auto* e = a.rack().effect(1)) { e->set(rack::tubeeq::LowBoost, 6); e->set(rack::tubeeq::LowAtten, 4); e->set(rack::tubeeq::HighBoost, 4); }
            a.addPlugin("FURNACE", 2);
            if (auto* e = a.rack().effect(2)) e->set(rack::tubesat::Drive, 45);
            a.addPlugin("ENFORCER", 3);
            if (auto* e = a.rack().effect(3)) { e->set(rack::vca::Threshold, -26); e->set(rack::vca::AutoMakeup, 1); }
            a.addPlugin("THE FAMILY", 4);
            if (auto* e = a.rack().effect(4)) e->set(rack::glue::Threshold, -18);
            a.addPlugin("LOCKDOWN", 5);
            if (auto* e = a.rack().effect(5)) e->set(rack::limiter::Input, 6);
        } else {
            a.addPlugin("THE CLEANER", 0);
            if (auto* e = a.rack().effect(0)) e->set(rack::restorer::Reduction, 6.0f);
            a.addPlugin("SURGEON", 1);
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
            a.addPlugin("VELVET", 2);
            if (auto* e = a.rack().effect(2)) {
                using namespace rack::tubeeq;
                e->set(LowBoost, 4); e->set(LowAtten, 2); e->set(HighBoost, 3); e->set(Bandwidth, 6);
            }
            a.addPlugin("THE FAMILY", 3);
            if (auto* e = a.rack().effect(3)) { e->set(rack::glue::Threshold, -22); e->set(rack::glue::Makeup, 2); }
        }
        a.select(opts.closed ? -1 : std::clamp(opts.select > 0 ? opts.select - 1 : 1, 0, kMaxSlots - 1));
        if (opts.picker > 0) {
            a.picker = true;
            a.pickerAt = -1;
            a.pickerTab = std::clamp(opts.picker - 1, 0, static_cast<int>(rack::Group::kCount) - 1);
            a.pickerCards = true;
        }
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
        // The output picked last time; with none saved (or it's gone), the first that opens: ASIO drivers come
        // first in the list, then Windows' default output. Settings from before 0.29 saved "" for Windows' default.
        const std::string saved = a.settings.get("app", "device");
        int idx = -1;
        for (std::size_t i = 0; i < a.devices.size(); ++i) {
            const Device& d = a.devices[i];
            const bool match = opts.offline ? d.offline : !saved.empty() && d.key() == saved;
            if (match) idx = static_cast<int>(i);
        }
        if (idx >= 0) {
            a.openDevice(idx);
        } else {
            for (std::size_t i = 0; i < a.devices.size(); ++i) {
                const Device& d = a.devices[i];
                if (d.offline) continue;
                if (!d.asio && !d.id.empty()) continue;   // ASIO drivers, then Windows' default
                a.openDevice(static_cast<int>(i));
                if (a.host.running() || !d.asio) break;
            }
        }
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

    if (!opts.files.empty()) a.loadAudio(opts.files);
    else if (!opts.demo) a.say("Open a song or up to 13 stems (OPEN SONG, or drop WAV / MP3 / FLAC files on the window), then add plug-ins on the left");

    // MIDI keyboards (for the instruments in the chain)
    MidiInputs midiIn(a.rack());
    (void)midiIn.refresh();
    auto lastMidiScan = std::chrono::steady_clock::now();

    std::vector<RackViewState::PickerItem> pickerItems;
    std::vector<std::string> pickerTabs;
    std::vector<std::string> pluginNames;
    for (const auto& p : rack::plugins()) pluginNames.emplace_back(p.name);
    // The picker's faceplate cards: one picture of all of them, made at the size they are drawn (1:1) when
    // the picker first opens, on the GPU only while it is open (the plate is let go meanwhile).
    std::optional<ui::CardAtlas> cards;
    bool cardsReady = false;
    auto prepareCards = [&](float scale) {
        const int want = std::clamp(static_cast<int>(std::ceil(RackView::cardImageWidth() * scale)), 16, ui::kCardFileW);
        if (cardsReady && cards && cards->cellW == want) return;
        for (int w = want; w >= 16; w /= 2) {   // too big for the texture budget: half the size, then the list
            if (!cards || cards->cellW != w) cards = ui::buildCardAtlas(assets, pluginNames, w);
            hc::ui::TextureLevels t;
            t.width = cards->image.width;
            t.height = cards->image.height;
            t.levels.push_back(cards->image.rgba);   // opaque: straight = premultiplied; drawn 1:1, no mips needed
            std::string e;
            if (renderer.setAtlas(t, &e)) {
                cardsReady = true;
                if (cards->missing > 0) a.say(std::to_string(cards->missing) + " plug-in cards are missing - please reinstall", true);
                for (auto& it : pickerItems) it.card = cards->cell[static_cast<std::size_t>(it.plugin)];
                return;
            }
        }
        cardsReady = false;
        a.pickerCards = false;
        a.say("Not enough graphics memory for the plug-in cards - showing the list", true);
    };
    for (int g = 0; g < static_cast<int>(rack::Group::kCount); ++g) pickerTabs.emplace_back(rack::groupName(static_cast<rack::Group>(g)));
    {
        const auto list = rack::plugins();
        for (std::size_t i = 0; i < list.size(); ++i)
            pickerItems.push_back({list[i].name, list[i].blurb, static_cast<int>(list[i].group), static_cast<int>(i)});
    }

    hc::ui::Canvas canvas;
    int fbW = 0, fbH = 0;
    float bakedScale = 0.0f;
    std::map<std::pair<int, int>, hc::ui::TextureLevels> plates;   // (plug-in, ppu × 1000) -> plate texture (the open plug-in only)
    std::pair<int, int> plateKey{-1, -1}, wantPlateKey{-1, -1};
    auto plateChangedAt = std::chrono::steady_clock::now();
    Hit hover;
    bool faceDrag = false, seekDrag = false;
    Hit stemDrag;   // a stem's level or pan being dragged
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
            probe.pickerTabs = pickerTabs;
            probe.pickerTab = a.pickerTab;
            probe.pickerCards = a.pickerCards;
            probe.pickerScroll = a.pickerScroll;
            probe.selected = a.selected;
            for (int s = 0; s < kMaxSlots; ++s) probe.slots[static_cast<std::size_t>(s)].used = a.rack().effect(s) != nullptr;
            probe.stemPanel = a.stemPanel;
            for (int k = 0; k < kMaxStems; ++k) probe.stems[static_cast<std::size_t>(k)].used = a.rack().stem(k) != nullptr;
            switch (e.type) {
            case hc::ui::InputEvent::Type::MouseDown: {
                const Hit hh = view.hit(x, y, probe);
                switch (hh.action) {
                case Action::OpenSong:
                case Action::StemsLoadAll:
                case Action::StemLoad: {
                    fs::path folder = fromUtf8(a.settings.get("app", "folder"));
                    if (folder.empty()) folder = musicDir();
                    const bool one = hh.action == Action::StemLoad;
                    auto files = askOpenAudio(hwnd, folder, !one);
                    if (!files.empty()) a.loadAudio(std::move(files), one ? hh.index : -1);
                    break;
                }
                case Action::StemsButton: a.stemPanel = !a.stemPanel; break;
                case Action::StemsClose: a.stemPanel = false; break;
                case Action::StemsClear: if (a.rack().hasAudio()) a.clearStems(); break;
                case Action::StemRemove: a.removeStem(hh.index); break;
                case Action::StemMute:
                case Action::StemSolo: {
                    StemMix m = a.rack().stemMix(hh.index);
                    if (hh.action == Action::StemMute) m.mute = !m.mute;
                    else if (e.fine) {   // Ctrl- or Shift-click S: this one alone
                        for (int k = 0; k < kMaxStems; ++k) { StemMix o = a.rack().stemMix(k); o.solo = false; a.rack().setStemMix(k, o); }
                        m.solo = true;
                    } else m.solo = !m.solo;
                    a.rack().setStemMix(hh.index, m);
                    break;
                }
                case Action::StemLevel:
                case Action::StemPan: {
                    StemMix m = a.rack().stemMix(hh.index);
                    if (e.doubleClick) {   // back to 0 dB / centre
                        if (hh.action == Action::StemLevel) m.gainDb = 0.0f; else m.pan = 0.0f;
                    } else {
                        stemDrag = hh;
                        if (hh.action == Action::StemLevel) m.gainDb = stemFaderToDb(hh.value);
                        else m.pan = std::fabs(hh.value * 2.0f - 1.0f) < 0.04f ? 0.0f : hh.value * 2.0f - 1.0f;
                    }
                    a.rack().setStemMix(hh.index, m);
                    break;
                }
                case Action::Export: a.startExport(hwnd); break;
                case Action::Device: a.openDevice((a.deviceIndex + 1) % static_cast<int>(a.devices.size())); break;
                case Action::Play:
                    if (!a.rack().hasAudio()) { a.say("Open a song or stems first", true); break; }
                    if (a.rack().wantsPlay()) a.rack().pause();
                    else {
                        if (a.rack().playhead() >= a.rack().songFrames()) a.rack().seek(0);
                        a.rack().play();
                    }
                    break;
                case Action::Stop:
                    if (a.takeWet.isOpen()) a.stopRecord();
                    a.rack().stop();
                    break;
                case Action::Input: a.cycleInput(); break;
                case Action::Monitor: a.rack().setMonitor(!a.rack().monitor()); break;
                case Action::Record:
                    if (a.takeWet.isOpen()) a.stopRecord();
                    else a.startRecord();
                    break;
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
                case Action::AddOpen: a.picker = true; a.pickerAt = hh.index; a.pickerScroll = 0; break;
                case Action::AddPlugin:
                    a.picker = false;
                    a.addPlugin(rack::plugins()[static_cast<std::size_t>(hh.index)].name, a.pickerAt);
                    break;
                case Action::PickerClose: a.picker = false; break;
                case Action::PickerTab: a.pickerTab = hh.index; a.pickerScroll = 0; break;
                case Action::PickerView: a.pickerCards = !a.pickerCards; a.saveSettings(); break;
                case Action::PickerScroll: a.pickerScroll = std::clamp(a.pickerScroll + hh.index, 0, RackView::maxScroll(probe)); break;
                case Action::FaceClose:
                    if (faceDrag && a.face.controller) a.face.controller->mouseUp();
                    faceDrag = false;
                    a.closeFace();
                    hover = {};
                    break;
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
                } else if (stemDrag.action == Action::StemLevel || stemDrag.action == Action::StemPan) {
                    const Rect f = stemDrag.action == Action::StemLevel ? RackView::stemLevelRect(stemDrag.index) : RackView::stemPanRect(stemDrag.index);
                    const float v = std::clamp((x - f.x) / f.w, 0.0f, 1.0f);
                    StemMix m = a.rack().stemMix(stemDrag.index);
                    if (stemDrag.action == Action::StemLevel) m.gainDb = stemFaderToDb(v);
                    else m.pan = std::fabs(v * 2.0f - 1.0f) < 0.04f ? 0.0f : v * 2.0f - 1.0f;
                    a.rack().setStemMix(stemDrag.index, m);
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
                stemDrag = {};
                break;
            case hc::ui::InputEvent::Type::Wheel:
                if (a.stemPanel && !a.picker) {   // over a fader: 0.5 dB a notch (0.1 with Shift / fine); over pan: 2 %
                    const Hit hh = view.hit(x, y, probe);
                    if ((hh.action == Action::StemLevel || hh.action == Action::StemPan) && e.wheel != 0.0f) {
                        StemMix m = a.rack().stemMix(hh.index);
                        const float d = e.wheel > 0.0f ? 1.0f : -1.0f;
                        if (hh.action == Action::StemLevel) m.gainDb = std::clamp(m.gainDb + d * (e.fine ? 0.1f : 0.5f), kStemMinDb, kStemMaxDb);
                        else m.pan = std::clamp(m.pan + d * 0.02f, -1.0f, 1.0f);
                        a.rack().setStemMix(hh.index, m);
                    }
                } else if (a.picker) {   // the cards scroll a row per notch
                    if (a.pickerCards && e.wheel != 0.0f)
                        a.pickerScroll = std::clamp(a.pickerScroll + (e.wheel > 0.0f ? -1 : 1), 0, RackView::maxScroll(probe));
                } else if (a.face.controller && view.hit(x, y, probe).action == Action::Face) {
                    float px, py;
                    toPlate(x, y, px, py);
                    a.face.controller->wheel(px, py, e.wheel, e.fine);
                }
                break;
            case hc::ui::InputEvent::Type::Key:
                if (e.key == VK_SPACE && a.rack().hasAudio()) {
                    if (a.rack().wantsPlay()) a.rack().pause();
                    else { if (a.rack().playhead() >= a.rack().songFrames()) a.rack().seek(0); a.rack().play(); }
                } else if ((e.key == VK_LEFT || e.key == VK_RIGHT) && a.rack().songFrames() > 0) {
                    const double step = (e.key == VK_LEFT ? -5.0 : 5.0) * kRate;
                    const double to = std::clamp(static_cast<double>(a.rack().playhead()) + step, 0.0, static_cast<double>(a.rack().songFrames() - 1));
                    a.rack().seek(static_cast<std::uint64_t>(to));
                } else if (e.key == 'A' && !e.ctrl) a.rack().setOriginal(false);
                else if (e.key == 'B' && !e.ctrl) a.rack().setOriginal(true);
                else if (e.key == VK_ESCAPE) {   // the picker first, then the plate
                    if (a.picker) a.picker = false;
                    else if (a.stemPanel) a.stemPanel = false;
                    else if (!faceDrag) { a.closeFace(); hover = {}; }
                }
                break;
            case hc::ui::InputEvent::Type::Drop:
            case hc::ui::InputEvent::Type::OpenFile:
                {   // one file: a song; several: stems (dropped on a stem's row in the STEMS panel: into that slot)
                    std::vector<fs::path> files;
                    for (const auto& f : e.files) if (hc::media::isAudioFileName(f)) files.emplace_back(f);
                    if (files.empty()) break;
                    const Hit at = view.hit(x, y, probe);
                    const bool row = a.stemPanel && (at.action == Action::StemLoad || at.action == Action::StemLevel || at.action == Action::StemPan) && files.size() == 1;
                    a.loadAudio(std::move(files), row ? at.index : -1);
                }
                break;
            default: break;
            }
        }

        a.host.poll();
        a.pollLoad();
        a.pollExport();
        if (a.face.slot >= 0 && a.rack().effect(a.face.slot) != a.face.fx) a.rebuildFace();   // chain changed under it

        // ---- the selected plug-in's metal plate, rendered at the screen's pixel density ----
        // ---- the picker's cards on the GPU only while it is open (and the plate let go meanwhile) ----
        if (a.picker && a.pickerCards) {
            if (plateKey.first >= 0) { renderer.clearPanel(); plateKey = {-1, -1}; }
            prepareCards(canvas.scale);
        } else if (cardsReady || renderer.hasAtlas()) {
            renderer.clearAtlas();
            cardsReady = false;
        }
        if (a.face.info && !(a.picker && a.pickerCards)) {
            float ox, oy, z;
            RackView::facePlacement(ox, oy, z);
            const float ppu = std::min({canvas.scale * z, 4096.0f / ui::kPlateW, 4096.0f / ui::kPlateH});
            // keyed by the plug-in itself: many plug-ins share a plate type but each has its own plate
            const int plugIndex = static_cast<int>(a.face.info - rack::plugins().data());
            const std::pair<int, int> key{plugIndex, static_cast<int>(std::lround(ppu * 1000.0f))};
            if (key != wantPlateKey) { wantPlateKey = key; plateChangedAt = std::chrono::steady_clock::now(); }
            // While the window is being resized, wait until it settles (a plate takes a moment to render).
            const bool settled = std::chrono::steady_clock::now() - plateChangedAt > std::chrono::milliseconds(250) || plateKey.first != key.first;
            if (key != plateKey && settled) {
                auto it = plates.find(key);
                if (it == plates.end()) {
                    plates.clear();   // only the open plug-in's plate is kept (each one is several MB)
                    const std::optional<hc::ui::Image> pic = ui::loadPlateArt(assets, a.face.info->name);
                    it = plates.emplace(key, hc::ui::buildMips(hc::ui::premultiply(ui::renderRackPlate(*a.face.layout, art->brand, ppu, pic ? &*pic : nullptr)))).first;
                }
                if (renderer.setPanel(it->second, &err)) plateKey = key;
                else a.say("GPU: " + err, true);
            }
        }

        // ---- persist (at most every 5 s, only when the chain changed) ----
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::min(0.1f, std::chrono::duration<float>(now - lastFrame).count());
        lastFrame = now;
        if (a.takeWet.isOpen()) a.drainTakes();   // the takes go to disk as they come in
        if (now - lastMidiScan > std::chrono::seconds(3)) {   // a keyboard plugged in or out
            lastMidiScan = now;
            if (midiIn.refresh()) a.say("MIDI: " + std::to_string(midiIn.opened()) + (midiIn.opened() == 1 ? " keyboard" : " keyboards") + " ready");
        }
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
        vs.stemPanel = a.stemPanel;
        vs.stemCount = a.rack().stemCount();
        for (int k = 0; k < kMaxStems; ++k) {
            StemView& st = vs.stems[static_cast<std::size_t>(k)];
            const auto s = a.rack().stem(k);
            const float pk = a.rack().stemPeak(k);
            float& shown = a.stemPeakDb[static_cast<std::size_t>(k)];
            const float pd = pk > 1e-6f ? 20.0f * std::log10(pk) : -120.0f;
            shown = std::max(pd, shown - 24.0f * dt);   // falls 24 dB a second
            st.used = s != nullptr;
            if (!s) continue;
            st.name = s->name;
            st.info = a.stemInfo[static_cast<std::size_t>(k)];
            const StemMix m = a.rack().stemMix(k);
            st.gainDb = m.gainDb;
            st.pan = m.pan;
            st.mute = m.mute;
            st.solo = m.solo;
            st.peakDb = shown;
        }
        vs.selected = a.selected;
        vs.picker = a.picker;
        vs.pickerItems = pickerItems;
        vs.pickerTabs = pickerTabs;
        vs.pickerTab = a.pickerTab;
        vs.pickerCards = a.pickerCards;
        vs.pickerScroll = a.pickerScroll;
        vs.cardsReady = cardsReady;
        vs.cardCols = cards ? std::max(1, cards->cols) : 1;
        vs.cardRows = cards ? std::max(1, cards->rows) : 1;
        vs.faceOpen = a.face.view != nullptr;
        vs.device = a.host.running() ? a.host.deviceName() : std::string("none - click to choose");
        vs.input = a.inputLabel();
        vs.inputOn = a.rack().inputFirst() >= 0;
        vs.monitor = a.rack().monitor();
        vs.recording = a.takeWet.isOpen();
        vs.recSeconds = static_cast<double>(a.takeWet.frames()) / kRate;
        {
            const float ip = a.rack().inputPeak();
            const float idb = ip > 1e-6f ? 20.0f * std::log10(ip) : -120.0f;
            static float shown = -120.0f;
            shown = idb > shown ? idb : std::max(idb, shown - 1.2f);   // falls back about 70 dB a second
            vs.inputDb = shown;
        }
        const bool fresh = !a.notice.empty() && now - a.noticeAt < std::chrono::seconds(a.noticeAlert ? 15 : 8);
        vs.status = fresh ? a.notice : a.host.running() ? "Playing through " + a.host.deviceName() + "  (" + a.host.format() + ")" : a.host.problem();
        vs.alert = fresh ? a.noticeAlert : !a.host.running();
        vs.exportProgress = a.exportProgress.load();

        // ---- draw ----
        renderer.beginFrame(canvas);
        view.drawBack(renderer, vs, hover);
        if (a.face.view && plateKey.first == static_cast<int>(a.face.info - rack::plugins().data())) {
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
