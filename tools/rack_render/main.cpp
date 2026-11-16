// hem_rack_render — runs HEM-Elite-Rack plug-ins offline on a WAV file, with no window and no sound card.
// It is the bridge the Suno repair pipeline (tools/suno_repair) uses to drive the real rack DSP.
//
//   hem_rack_render --list [--out catalog.json]
//       Writes every plug-in and every parameter (index, id, range, default, taper, choices) as JSON.
//       The pipeline reads this file, so the AI can only pick names and ids that exist in this build.
//
//   hem_rack_render --chain chain.hemchain --in in.wav --out out.wav [--report report.json] [--tempo 120]
//       Runs the chain on the file and writes a 32-bit float WAV, delay-compensated (each plug-in's
//       latency is flushed and trimmed so the output lines up with the input sample for sample).
//
// chain.hemchain (plain text, one command per line, '#' starts a comment):
//   plugin SURGEON          <- a plug-in by its exact name (spaces allowed)
//   set some_param_id 3.5   <- a parameter of the plug-in above, by its id, in plain units
//   set some_choice Bell    <- choices and toggles may also be given by their shown name
//
// Exit codes: 0 ok, 1 bad arguments, 2 bad chain (unknown plug-in / parameter / value),
//             3 audio file error, 4 the output holds NaN or Inf samples.
#include "hem/rack/Plugins.hpp"
#include "hybrid/fx/Fx.hpp"
#include "hybrid/media/AudioFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string jsonEscape(const char* s) {
    std::string o;
    if (!s) return o;
    for (const char* c = s; *c; ++c) {
        const unsigned char u = static_cast<unsigned char>(*c);
        switch (*c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (u < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", u);
                    o += buf;
                } else {
                    o += *c;
                }
        }
    }
    return o;
}

std::string num(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

const char* taperName(hc::fx::Taper t) {
    switch (t) {
        case hc::fx::Taper::Linear: return "linear";
        case hc::fx::Taper::Log: return "log";
        case hc::fx::Taper::Choice: return "choice";
        case hc::fx::Taper::Toggle: return "toggle";
    }
    return "linear";
}

int writeCatalog(const std::filesystem::path& out) {
    std::ostringstream js;
    js << "{\n  \"format\": \"hem-rack-catalog/1\",\n  \"plugins\": [\n";
    bool firstPlugin = true;
    for (const auto& info : hem::rack::plugins()) {
        auto fx = info.create();
        if (!fx) continue;
        fx->prepare(48000.0, hc::fx::Effect::kMaxBlock);
        if (!firstPlugin) js << ",\n";
        firstPlugin = false;
        js << "    {\"name\": \"" << jsonEscape(info.name) << "\", \"suite\": \"" << jsonEscape(info.suite)
           << "\", \"group\": \"" << jsonEscape(hem::rack::groupName(info.group)) << "\", \"subCategory\": \""
           << jsonEscape(info.subCategory) << "\", \"blurb\": \"" << jsonEscape(info.blurb)
           << "\", \"latency48k\": " << fx->latency() << ", \"params\": [";
        const auto ps = fx->params();
        for (std::size_t i = 0; i < ps.size(); ++i) {
            const auto& p = ps[i];
            if (i) js << ",";
            const bool stored = info.firstStored >= 0 && static_cast<int>(i) >= info.firstStored;
            js << "\n      {\"index\": " << i << ", \"id\": \"" << jsonEscape(p.id) << "\", \"name\": \"" << jsonEscape(p.name)
               << "\", \"unit\": \"" << jsonEscape(p.unit) << "\", \"min\": " << num(p.min) << ", \"max\": " << num(p.max)
               << ", \"default\": " << num(p.def) << ", \"taper\": \"" << taperName(p.taper) << "\", \"stored\": "
               << (stored ? "true" : "false");
            if (p.taper == hc::fx::Taper::Choice && p.choices) {
                js << ", \"choices\": [";
                const int n = static_cast<int>(std::lround(p.max)) + 1;
                for (int c = 0; c < n; ++c) js << (c ? ", " : "") << "\"" << jsonEscape(p.choices[c]) << "\"";
                js << "]";
            }
            js << "}";
        }
        js << "\n    ]}";
    }
    js << "\n  ]\n}\n";
    if (out.empty()) {
        std::fputs(js.str().c_str(), stdout);
        return 0;
    }
    std::ofstream f(out, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
        return 3;
    }
    f << js.str();
    return 0;
}

struct Setting {
    std::string id, value;
    int line = 0;
};
struct Step {
    std::string plugin;
    int line = 0;
    std::vector<Setting> settings;
};

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool readChain(const std::filesystem::path& file, std::vector<Step>& steps, std::string& err) {
    std::ifstream f(file);
    if (!f) {
        err = "cannot open chain file " + file.string();
        return false;
    }
    std::string raw;
    int n = 0;
    while (std::getline(f, raw)) {
        ++n;
        const auto hash = raw.find('#');
        const std::string line = trim(hash == std::string::npos ? raw : raw.substr(0, hash));
        if (line.empty()) continue;
        if (line.rfind("plugin ", 0) == 0) {
            steps.push_back({trim(line.substr(7)), n, {}});
        } else if (line.rfind("set ", 0) == 0) {
            if (steps.empty()) {
                err = "line " + std::to_string(n) + ": 'set' before any 'plugin'";
                return false;
            }
            const std::string rest = trim(line.substr(4));
            const auto sp = rest.find_first_of(" \t");
            if (sp == std::string::npos) {
                err = "line " + std::to_string(n) + ": 'set' needs an id and a value";
                return false;
            }
            steps.back().settings.push_back({rest.substr(0, sp), trim(rest.substr(sp)), n});
        } else {
            err = "line " + std::to_string(n) + ": unknown command (use 'plugin' or 'set')";
            return false;
        }
    }
    return true;
}

bool applySetting(hc::fx::Effect& fx, const hem::rack::PluginInfo& info, const Setting& s, std::string& err) {
    const auto ps = fx.params();
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (s.id != ps[i].id) continue;
        if (info.firstStored >= 0 && static_cast<int>(i) >= info.firstStored) {
            err = "line " + std::to_string(s.line) + ": '" + s.id + "' is stored memory, not a control";
            return false;
        }
        char* end = nullptr;
        const float v = std::strtof(s.value.c_str(), &end);
        float plain = 0.0f;
        if (end && *end == '\0' && std::isfinite(v)) {
            plain = v;
        } else if (!hc::fx::parseValue(ps[i], s.value, plain)) {
            err = "line " + std::to_string(s.line) + ": bad value '" + s.value + "' for " + s.id;
            return false;
        }
        fx.set(static_cast<int>(i), plain);
        return true;
    }
    err = "line " + std::to_string(s.line) + ": " + info.name + " has no parameter '" + s.id + "'";
    return false;
}

int render(const std::filesystem::path& chainFile, const std::filesystem::path& in, const std::filesystem::path& out,
           const std::filesystem::path& reportFile, double tempo) {
    std::vector<Step> steps;
    std::string err;
    if (!readChain(chainFile, steps, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }

    struct Live {
        const hem::rack::PluginInfo* info;
        std::unique_ptr<hc::fx::Effect> fx;
    };
    std::vector<Live> chain;
    for (const auto& st : steps) {
        const auto* info = hem::rack::findPlugin(st.plugin.c_str());
        if (!info) {
            std::fprintf(stderr, "line %d: no plug-in named '%s'\n", st.line, st.plugin.c_str());
            return 2;
        }
        auto fx = info->create();
        for (const auto& s : st.settings)
            if (!applySetting(*fx, *info, s, err)) {
                std::fprintf(stderr, "%s\n", err.c_str());
                return 2;
            }
        chain.push_back({info, std::move(fx)});
    }

    auto decoded = hc::media::decodeFile(in);
    if (!decoded) {
        std::fprintf(stderr, "cannot read %s: %s\n", in.string().c_str(), decoded.error().c_str());
        return 3;
    }
    std::vector<float> L = std::move(decoded->left), R = std::move(decoded->right);
    const std::size_t frames = L.size();
    const double sr = decoded->sampleRate;

    std::ostringstream rep;
    rep << "{\n  \"input\": \"" << jsonEscape(in.string().c_str()) << "\",\n  \"sampleRate\": " << num(sr)
        << ",\n  \"frames\": " << frames << ",\n  \"chain\": [";

    const std::uint32_t block = hc::fx::Effect::kMaxBlock;
    for (std::size_t c = 0; c < chain.size(); ++c) {
        auto& fx = *chain[c].fx;
        fx.prepare(sr, block);
        fx.reset();
        fx.setTempo(tempo);
        const std::uint32_t lat = fx.latency();
        L.resize(frames + lat, 0.0f);   // flush the plug-in's delay line with silence
        R.resize(frames + lat, 0.0f);
        double grMin = 0.0;
        for (std::size_t pos = 0; pos < L.size(); pos += block) {
            const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(block, L.size() - pos));
            fx.process(L.data() + pos, R.data() + pos, n);
            grMin = std::min(grMin, static_cast<double>(fx.reductionDb()));
        }
        L.erase(L.begin(), L.begin() + lat);   // line the output up with the input
        R.erase(R.begin(), R.begin() + lat);
        rep << (c ? "," : "") << "\n    {\"plugin\": \"" << jsonEscape(chain[c].info->name) << "\", \"latency\": " << lat
            << ", \"maxReductionDb\": " << num(grMin) << "}";
    }

    std::size_t bad = 0;
    double peak = 0.0, sumL = 0.0, sumR = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) {
            ++bad;
            L[i] = 0.0f;
            R[i] = 0.0f;
            continue;
        }
        peak = std::max({peak, static_cast<double>(std::fabs(L[i])), static_cast<double>(std::fabs(R[i]))});
        sumL += L[i];
        sumR += R[i];
    }
    const double inv = frames ? 1.0 / static_cast<double>(frames) : 0.0;
    rep << "\n  ],\n  \"nonFiniteSamples\": " << bad << ",\n  \"samplePeakDbfs\": " << num(peak > 0 ? 20.0 * std::log10(peak) : -300.0)
        << ",\n  \"dcLeft\": " << num(sumL * inv) << ",\n  \"dcRight\": " << num(sumR * inv) << "\n}\n";

    if (!hc::media::writeWav(out, L.data(), R.data(), frames, static_cast<std::uint32_t>(sr), hc::media::WavEncoding::Float32)) {
        std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
        return 3;
    }
    if (!reportFile.empty()) {
        std::ofstream f(reportFile, std::ios::binary);
        f << rep.str();
    } else {
        std::fputs(rep.str().c_str(), stdout);
    }
    if (bad) {
        std::fprintf(stderr, "%zu NaN/Inf samples in the output (written as silence)\n", bad);
        return 4;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool list = false;
    std::filesystem::path chain, in, out, report;
    double tempo = 120.0;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        const bool hasNext = i + 1 < argc;
        if (k == "--list") list = true;
        else if (k == "--chain" && hasNext) chain = argv[++i];
        else if (k == "--in" && hasNext) in = argv[++i];
        else if (k == "--out" && hasNext) out = argv[++i];
        else if (k == "--report" && hasNext) report = argv[++i];
        else if (k == "--tempo" && hasNext) tempo = std::strtod(argv[++i], nullptr);
        else {
            std::fprintf(stderr, "unknown or incomplete argument: %s\n", k.c_str());
            return 1;
        }
    }
    if (list) return writeCatalog(out);
    if (chain.empty() || in.empty() || out.empty()) {
        std::fprintf(stderr, "usage: hem_rack_render --list [--out catalog.json]\n"
                             "       hem_rack_render --chain c.hemchain --in in.wav --out out.wav [--report r.json] [--tempo bpm]\n");
        return 1;
    }
    return render(chain, in, out, report, tempo);
}
