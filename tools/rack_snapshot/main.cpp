// hem_rack_snapshot — renders every HEM-Elite-Rack faceplate off-screen with the real faceplate
// code (hem::ui::FaceWindow, OpenGL) after running the plug-in on a few seconds of test audio, so
// the spectrum, noise curve and EQ curve are live. Saves one PNG per plug-in.
//   hem_rack_snapshot [--out <folder>] [--scale 0.75]
// Exit 3 when a faceplate fails to open or comes out blank.
#include "hem/rack/BusGlue.hpp"
#include "hem/rack/ConsoleEq.hpp"
#include "hem/rack/Dynamics.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/ui/FaceWindow.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

namespace {

std::filesystem::path exeDir() {
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
}

double lumaStdDev(const hc::ui::Image& img) {
    double s = 0, sq = 0;
    const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = &img.rgba[i * 4];
        const double l = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        s += l; sq += l * l;
    }
    const double m = s / static_cast<double>(std::max<std::size_t>(n, 1));
    return std::sqrt(std::max(0.0, sq / static_cast<double>(std::max<std::size_t>(n, 1)) - m * m));
}

// A short "song": kick, bass, a voice, hats and hiss (the RESTORER has something to find).
void feed(hc::fx::Effect& e, double seconds) {
    e.prepare(48000.0, 512);
    std::vector<float> l(512), r(512);
    std::uint32_t rng = 5;
    for (int b = 0; b < static_cast<int>(48000 * seconds / 512); ++b) {
        for (int i = 0; i < 512; ++i) {
            const double t = (b * 512 + i) / 48000.0, beat = std::fmod(t, 0.65);
            rng = rng * 1664525u + 1013904223u;
            const float nz = static_cast<float>(rng >> 8) / 8388608.0f - 1.0f;
            const float kick = 0.5f * static_cast<float>(std::exp(-beat * 16.0) * std::sin(2 * std::numbers::pi * 55 * beat));
            const float bass = 0.18f * static_cast<float>(std::sin(2 * std::numbers::pi * 82.4 * t));
            const float voice = 0.12f * static_cast<float>(std::sin(2 * std::numbers::pi * 440 * t) + 0.4 * std::sin(2 * std::numbers::pi * 880 * t));
            const float hat = (std::fmod(t, 0.1625) < 0.015 ? 0.1f : 0.0f) * nz;
            const float hiss = 0.012f * nz;
            l[static_cast<std::size_t>(i)] = kick + bass + voice + hat + hiss;
            r[static_cast<std::size_t>(i)] = kick + bass + 0.7f * voice + hat + hiss;
        }
        e.process(l.data(), r.data(), 512);
    }
}

class Host final : public hc::ui::PanelHost {
public:
    explicit Host(hc::fx::Effect& e) : e_(e) {}
    float normalized(int i) const override { return hc::fx::toNormalized(e_.params()[static_cast<std::size_t>(i)], e_.get(i)); }
    void beginEdit(int) override {}
    void setNormalized(int i, float v) override { e_.set(i, hc::fx::toPlain(e_.params()[static_cast<std::size_t>(i)], v)); }
    void endEdit(int) override {}
    std::string text(int i) const override { return hc::fx::formatValue(e_.params()[static_cast<std::size_t>(i)], e_.get(i)); }
    bool active(int i) const override { return e_.paramActive(i); }

private:
    hc::fx::Effect& e_;
};

} // namespace

int main(int argc, char** argv) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::filesystem::path out = "rack-snapshots";
    float scale = 0.75f;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if (k == "--out" && i + 1 < argc) out = argv[++i];
        else if (k == "--scale" && i + 1 < argc) scale = std::strtof(argv[++i], nullptr);
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    int failures = 0;
    for (const auto& info : hem::rack::plugins()) {
        auto fx = info.create();
        if (info.face == hem::rack::Face::EliteEq) {   // a typical mastering curve, one dynamic band
            using namespace hem::rack::eq;
            const struct { Type t; float f, g, q; Stereo st; } bands[] = {{Type::LowCut, 28, 0, 0.71f, Stereo::Stereo},
                                                                        {Type::Bell, 220, -3.5f, 1.4f, Stereo::Stereo},
                                                                        {Type::Bell, 3200, 2.5f, 0.9f, Stereo::Mid},
                                                                        {Type::HighShelf, 11000, 3.0f, 0.71f, Stereo::Side}};
            for (int b = 0; b < 4; ++b) {
                fx->set(bandParam(b, On), 1);
                fx->set(bandParam(b, TypeC), static_cast<float>(static_cast<int>(bands[b].t)));
                fx->set(bandParam(b, Freq), bands[b].f);
                fx->set(bandParam(b, Gain), bands[b].g);
                fx->set(bandParam(b, Q), bands[b].q);
                fx->set(bandParam(b, StereoC), static_cast<float>(static_cast<int>(bands[b].st)));
            }
            fx->set(bandParam(1, Dyn), 1);
            fx->set(bandParam(1, Threshold), -36);
            fx->set(AutoGain, 1);
        } else if (info.face == hem::rack::Face::ConsoleEq) {   // a typical channel: low cut, a little low end, scoop, air
            using namespace hem::rack::console;
            fx->set(HpfOn, 1); fx->set(HpfHz, 60); fx->set(LfGain, 3); fx->set(LfHz, 90);
            fx->set(LmfGain, -4); fx->set(LmfHz, 350); fx->set(LmfQ, 1.4f); fx->set(HmfGain, 2.5f); fx->set(HmfHz, 3500);
            fx->set(HfGain, 4); fx->set(HfHz, 10000); fx->set(Curve, 1); fx->set(Drive, 25);
        } else if (info.face == hem::rack::Face::BusGlue) {     // 4:1, 3 ms, AUTO, working a few dB
            using namespace hem::rack::glue;
            fx->set(Threshold, -18); fx->set(ScHpf, 2); fx->set(Makeup, 2);
        } else if (info.face == hem::rack::Face::Tape) {
            using namespace hem::rack::tape;
            fx->set(Input, 6); fx->set(Bump, 60); fx->set(Wow, 15);
        } else if (info.face == hem::rack::Face::TubeEq) {
            using namespace hem::rack::tubeeq;
            fx->set(LowBoost, 6); fx->set(LowAtten, 4); fx->set(HighBoost, 5); fx->set(Bandwidth, 4); fx->set(HighAtten, 2); fx->set(Drive, 30);
        } else if (info.face == hem::rack::Face::VariMu) {
            fx->set(hem::rack::varimu::Threshold, -26);
        } else if (info.face == hem::rack::Face::Opto) {
            fx->set(hem::rack::opto::PeakReduction, 60); fx->set(hem::rack::opto::Gain, 4);
        } else if (info.face == hem::rack::Face::Fet) {
            fx->set(hem::rack::fet::Input, 24);
        } else {
            fx->set(hem::rack::restorer::Reduction, 6.0f);
        }
        hem::ui::EffectFeed feedSrc(*fx);
        feedSrc.setSpectrumActive(true);
        feed(*fx, 5.0);
        Host host(*fx);
        hem::ui::FaceWindow win;
        std::string err;
        if (!win.openHidden(info, fx->params(), host, feedSrc, exeDir() / "assets", scale, &err)) {
            std::printf("FAIL %s: %s\n", info.name, err.c_str());
            ++failures;
            continue;
        }
        if (info.face == hem::rack::Face::EliteEq) win.state().selectedBand = 1;
        const hc::ui::Image img = win.snapshot();
        const double sd = lumaStdDev(img);
        std::string file = info.name;
        for (char& c : file) if (c == ' ') c = '_';
        const auto path = out / ("HEM_" + file + ".png");
        const bool ok = !img.empty() && sd > 8.0 && hc::ui::writePng(path, img);
        std::printf("%s %s: %ux%u, luma spread %.1f -> %s\n", ok ? "OK  " : "FAIL", info.name, img.width, img.height, sd, path.string().c_str());
        if (!ok) ++failures;
    }
    return failures ? 3 : 0;
}
