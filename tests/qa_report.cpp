// HEM QA pass for the 120 HEM-Elite-Rack plug-ins: stability, parameter sweep, meter liveness and the
// HEM-board faceplate (faceLayout + rackFrameMap). Report only — writes qa_report_eliterack.csv and never
// fails the test run. Run it with: hem_rack_tests --qa
#include "Harness.hpp"
#include "QaCommon.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/ui/Face.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace hem::test {
namespace {

using hemqa::Stereo;
using namespace hem::ui;

Stereo run(hc::fx::Effect& e, const Stereo& in) {
    Stereo out = in;
    for (std::size_t pos = 0; pos < out.size(); pos += hemqa::kBlock) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(hemqa::kBlock, out.size() - pos));
        e.process(out.l.data() + pos, out.r.data() + pos, n);
    }
    return out;
}

hemqa::Rect qr(const Rect& r) { return {r.x, r.y, r.w, r.h}; }

bool stepped(const hc::fx::ParamInfo& p) { return p.taper == hc::fx::Taper::Choice || p.taper == hc::fx::Taper::Toggle; }

// Parameters a QA sweep may move: the plug-in's controls (its stored memory after firstStored is not a control).
int controlCount(const rack::PluginInfo& info, int count) { return info.firstStored >= 0 ? std::min(info.firstStored, count) : count; }

bool keepAtDefault(const hc::fx::ParamInfo& p) {   // switches that silence or pass audio straight through
    const std::string id = p.id;
    for (const char* k : {"bypass", "solo", "listen", "mute", "freeze", "delta", "audition", "learn", "capture", "reset", "clear"})
        if (id.find(k) != std::string::npos) return true;
    return false;
}

// The meters a plate shows: the effect's own (meter(i)) and, for compressors, gain reduction.
struct MeterRef { std::string name; int index; };   // index -1 = reductionDb()
std::vector<MeterRef> plateMeters(const rack::PluginInfo& info, const hc::fx::Effect& e, const FaceLayout& L) {
    std::vector<MeterRef> out;
    const auto ms = e.meters();
    for (int i = 0; i < static_cast<int>(ms.size()); ++i) out.push_back({ms[static_cast<std::size_t>(i)].name, i});
    bool gr = info.plate && info.plate->reportsReduction;
    for (const Widget& w : L.widgets) gr = gr || (w.kind == WKind::GrMeter && w.choice < 0);
    if (gr) out.push_back({"GAIN REDUCTION", -1});
    return out;
}

void auditDsp(const rack::PluginInfo& info, hemqa::Report& rep) {
    const std::string name = info.name;
    auto e = info.create();
    const std::span<const hc::fx::ParamInfo> ps = e->params();
    const int controls = controlCount(info, static_cast<int>(ps.size()));
    const Stereo full = hemqa::testAudio(1.0), silence(static_cast<std::size_t>(0.5 * hemqa::kRate));

    e->prepare(hemqa::kRate, hemqa::kBlock);
    e->setDefaults();
    e->reset();
    if (!hemqa::finite(run(*e, full))) rep.add(name, "DSP", "NAN_OR_INF", "defaults", "non-finite output on 1 s of test audio at default settings");

    auto setContext = [&](int skip, int mode) {   // 0 defaults, 1 min, 2 max, 3 middle (stored memory stays at default)
        e->setDefaults();
        if (mode == 0) return;
        for (int k = 0; k < controls; ++k) {
            const auto& p = ps[static_cast<std::size_t>(k)];
            if (k == skip || keepAtDefault(p)) continue;
            e->set(k, mode == 1 ? p.min : mode == 2 ? p.max : hc::fx::toPlain(p, 0.5f));
        }
    };
    bool fresh = false;
    auto stateHash = [&](int mode, int param, float value, bool& blewUp, bool& getOk) {
        if (fresh) { e = info.create(); e->prepare(hemqa::kRate, hemqa::kBlock); }
        setContext(param, mode);
        if (param >= 0) {
            e->set(param, value);
            getOk = hemqa::closeTo(e->get(param), hc::fx::clampPlain(ps[static_cast<std::size_t>(param)], value));
        }
        e->reset();
        const Stereo o = run(*e, full);
        blewUp = !hemqa::finite(o);
        hemqa::Hash h;
        h.add(o);
        h.add(e->reductionDb());
        for (int m = 0; m < static_cast<int>(e->meters().size()); ++m) h.add(e->meter(m));
        return h.h;
    };
    bool dummyBlew = false, dummyGet = true;
    bool deterministic = stateHash(0, -1, 0, dummyBlew, dummyGet) == stateHash(0, -1, 0, dummyBlew, dummyGet);
    if (!deterministic) {
        // reset() does not bring the effect back to the same start: run every state on a fresh instance instead
        fresh = true;
        if (stateHash(0, -1, 0, dummyBlew, dummyGet) == stateHash(0, -1, 0, dummyBlew, dummyGet)) {
            deterministic = true;
            rep.add(name, "DSP", "RESET_NOT_REPEATABLE", "reset()", "two runs after reset() differ (a fresh instance repeats): offline renders may not match; sweep ran on fresh instances");
        } else {
            rep.note(name, "output differs even between two fresh instances (random source): DEAD_WIRE check skipped");
        }
    }
    for (int mode = 1; mode <= 3; ++mode) {
        bool b = false, g = true;
        (void)stateHash(mode, -1, 0, b, g);
        if (b) rep.add(name, "DSP", "NAN_OR_INF_SWEEP", mode == 1 ? "all controls at min" : mode == 2 ? "all controls at max" : "all controls at middle", "non-finite output");
    }

    for (int i = 0; i < controls; ++i) {
        e->setDefaults();
        if (!e->paramActive(i)) continue;
        const hc::fx::ParamInfo& p = ps[static_cast<std::size_t>(i)];
        bool sampled = false;
        const std::vector<float> values = hemqa::sweepValues(p.min, p.def, p.max, stepped(p), &sampled);
        if (sampled) rep.note(name, std::string(p.id) + ": stepped control with more than 64 steps, 64 sampled");
        bool blew = false, mism = false, live = !deterministic;
        std::string blewAt, mismAt;
        for (int mode = 0; mode <= 3 && !live; ++mode) {
            std::set<std::uint64_t> hashes;
            for (float v : values) {
                bool b = false, g = true;
                hashes.insert(stateHash(mode, i, v, b, g));
                if (b && !blew) { blew = true; blewAt = hc::fx::formatValue(p, hc::fx::clampPlain(p, v)); }
                if (!g && !mism) { mism = true; mismAt = hemqa::fmt("%g", v); }
            }
            live = hashes.size() > 1;
        }
        if (deterministic && live && !blew) {
            // also catch a blow-up at the default context's extremes when the control was live straight away
            for (float v : values) {
                bool b = false, g = true;
                (void)stateHash(0, i, v, b, g);
                if (b) { blew = true; blewAt = hc::fx::formatValue(p, hc::fx::clampPlain(p, v)); break; }
                if (!g && !mism) { mism = true; mismAt = hemqa::fmt("%g", v); }
            }
        }
        if (blew) rep.add(name, "DSP", "NAN_OR_INF_SWEEP", p.id, "non-finite output at " + blewAt);
        if (mism) rep.add(name, "DSP", "GET_SET_MISMATCH", p.id, "get() != set() at " + mismAt);
        if (!live) rep.add(name, "DSP", "DEAD_WIRE", p.id, "audio + meters bit-identical at all " + std::to_string(values.size()) +
                           " values, with the other controls at default / min / max / middle");
    }

    // meters
    const FaceLayout L = faceLayout(info);
    const std::vector<MeterRef> meters = plateMeters(info, *e, L);
    auto read = [&](const MeterRef& m) { return m.index < 0 ? e->reductionDb() : e->meter(m.index); };
    std::vector<int> moved(meters.size(), 0), bad(meters.size(), 0);
    std::vector<float> lastHot(meters.size(), 0.0f);
    for (int mode = 0; mode <= 3; ++mode) {
        setContext(-1, mode);
        e->reset();
        (void)run(*e, silence);
        std::vector<float> quiet;
        for (const auto& m : meters) quiet.push_back(read(m));
        (void)run(*e, full);
        for (std::size_t m = 0; m < meters.size(); ++m) {
            const float hot = read(meters[m]);
            if (!std::isfinite(hot) || !std::isfinite(quiet[m])) bad[m] = 1;
            else if (quiet[m] != hot) moved[m] = 1;
            lastHot[m] = hot;
        }
    }
    for (std::size_t m = 0; m < meters.size(); ++m) {
        if (bad[m]) rep.add(name, "METER", "METER_NAN_OR_INF", meters[m].name, "non-finite meter value");
        if (!moved[m]) rep.add(name, "METER", "METER_STUCK", meters[m].name, "reads " + hemqa::fmt("%g", lastHot[m]) +
                               " for both silence and hot signal, with the controls at default / min / max / middle");
    }
}

bool interactive(WKind k) {
    switch (k) {
    case WKind::Knob: case WKind::Toggle: case WKind::ChoiceButton: case WKind::Stepper: case WKind::RangeSlider:
    case WKind::Bypass: case WKind::GraphRange: case WKind::BandButton: case WKind::PageTab: case WKind::Keyboard:
        return true;
    default:
        return false;   // displays; the ELITE EQ graph holds its own band nodes and range buttons inside it
    }
}

const char* kindName(WKind k) {
    switch (k) {
    case WKind::Knob: return "knob"; case WKind::Toggle: return "toggle"; case WKind::ChoiceButton: return "button";
    case WKind::Stepper: return "stepper"; case WKind::RangeSlider: return "range"; case WKind::Bypass: return "POWER";
    case WKind::GraphRange: return "graph range"; case WKind::BandButton: return "band"; case WKind::PageTab: return "page tab";
    case WKind::Keyboard: return "keyboard"; default: return "display";
    }
}

void auditFace(const rack::PluginInfo& info, hemqa::Report& rep) {
    const std::string name = info.name;
    auto e = info.create();
    e->setDefaults();
    const int count = static_cast<int>(e->params().size());
    const int controls = controlCount(info, count);
    const int meterCount = static_cast<int>(e->meters().size());
    const FaceLayout L = faceLayout(info);
    int pages = 0;
    for (const Widget& w : L.widgets) pages += w.kind == WKind::PageTab ? 1 : 0;
    pages = std::max(pages, 1);

    auto label = [&](const Widget& w) {
        std::string s = kindName(w.kind);
        if (!w.caption.empty()) s += " " + w.caption;
        else if (w.param >= 0 && w.param < count) s += std::string(" ") + e->params()[static_cast<std::size_t>(w.param)].id;
        return s;
    };
    auto badIndex = [&](const Widget& w, const std::string& what) { rep.add(name, "UI", "BAD_PARAM_INDEX", label(w), what); };

    std::set<int> reach;
    for (const Widget& w : L.widgets) {
        if (w.param >= count) badIndex(w, "param " + std::to_string(w.param) + " (has " + std::to_string(count) + ")");
        if (w.param2 >= count) badIndex(w, "param2 " + std::to_string(w.param2) + " (has " + std::to_string(count) + ")");
        if (w.kind == WKind::Keyboard && w.param + w.choice > count) badIndex(w, "keys run past the last parameter");
        if (w.pageStride > 0 && w.param >= 0 && w.param + (pages - 1) * w.pageStride >= count) badIndex(w, "last page's parameter is past the end");
        if (w.showParam >= count) badIndex(w, "showParam " + std::to_string(w.showParam));
        if (w.pageLink >= count) badIndex(w, "pageLink " + std::to_string(w.pageLink));
        if (w.control >= rack::eq::kPerBand) badIndex(w, "band control " + std::to_string(w.control));
        const bool meterChoice = w.kind == WKind::Readout || w.kind == WKind::VuMeter || (w.kind == WKind::GrMeter && w.choice >= 0);
        if (meterChoice && (w.choice < 0 || w.choice >= meterCount)) badIndex(w, "meter " + std::to_string(w.choice) + " (has " + std::to_string(meterCount) + ")");
        if (w.meter >= 0 && w.meter + 1 >= meterCount) badIndex(w, "needle meters " + std::to_string(w.meter) + " / " + std::to_string(w.meter + 1) + " (has " + std::to_string(meterCount) + ")");
        if (w.param >= 0 && w.choice >= 0 && w.param < count && (w.kind == WKind::ChoiceButton)) {
            const auto& p = e->params()[static_cast<std::size_t>(w.param)];
            if (w.choice > static_cast<int>(std::lround(p.max))) badIndex(w, "selects choice " + std::to_string(w.choice) + " past the last one");
        }
        // what the plate can reach
        if (w.param >= 0 && w.pageStride > 0) for (int pg = 0; pg < pages; ++pg) reach.insert(w.param + pg * w.pageStride);
        if (w.param >= 0) reach.insert(w.param);
        if (w.param2 >= 0) reach.insert(w.param2);
        if (w.kind == WKind::Keyboard) for (int k = 0; k < w.choice; ++k) reach.insert(w.param + k);
        if (w.control >= 0) for (int b = 0; b < rack::eq::kBands; ++b) reach.insert(rack::eq::bandParam(b, w.control));
    }
    for (int p = 0; p < controls; ++p)
        if (!reach.count(p) && e->paramActive(p)) rep.add(name, "UI", "ORPHAN_PARAM", e->params()[static_cast<std::size_t>(p)].id, "no control on the faceplate");

    const RackFrameMap fm = rackFrameMap();
    const hemqa::Rect bed = qr(rackframe::kBed), emblem = qr(rackframe::kEmblem);
    std::vector<std::size_t> hot;
    for (std::size_t i = 0; i < L.widgets.size(); ++i) if (interactive(L.widgets[i].kind)) hot.push_back(i);
    for (std::size_t a = 0; a < hot.size(); ++a) {
        const Widget& w = L.widgets[hot[a]];
        const hemqa::Rect f = qr(fm.toFrame(w.r));
        if (!hemqa::within(f, bed) || hemqa::overlaps(f, emblem))
            rep.add(name, "UI", "OUT_OF_BOUNDS", label(w), hemqa::fmt("frame x %.0f", f.x) + hemqa::fmt(" y %.0f", f.y) + hemqa::fmt(" w %.0f", f.w) +
                    hemqa::fmt(" h %.0f", f.h) + " (glass bed 230,230 3380x1690; emblem 1720,0 400x545)");
        for (std::size_t b = a + 1; b < hot.size(); ++b) {
            const Widget& o = L.widgets[hot[b]];
            if (exclusive(w, o)) continue;   // never on the plate together
            if (hemqa::overlaps(qr(w.r), qr(o.r))) rep.add(name, "UI", "HITBOX_OVERLAP", label(w) + " / " + label(o), "hit boxes collide");
        }
    }
}

} // namespace

void runQaReport() {
    std::printf("[QA] HEM-Elite-Rack: %zu plug-ins — stability, sweep, meters, faceplate\n", rack::plugins().size());
    hemqa::Report rep("HEM-Elite-Rack");
    for (const rack::PluginInfo& info : rack::plugins()) {
        rep.plugin(info.name);
        auditDsp(info, rep);
        auditFace(info, rep);
        std::printf("[QA]   %s done\n", info.name);
        std::fflush(stdout);
    }
    const auto file = hemqa::outDir() / "qa_report_eliterack.csv";
    std::printf("[QA] report: %s (%s)\n", file.string().c_str(), rep.write(file) ? "written" : "COULD NOT WRITE");
    rep.printSummary();
}

} // namespace hem::test
