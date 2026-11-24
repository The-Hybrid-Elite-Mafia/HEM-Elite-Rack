// HEM QA pass for the 120 HEM-Elite-Rack plug-ins: stability, parameter sweep, meter liveness and the
// HEM-board faceplate (faceLayout + rackFrameMap). Report only — writes qa_report_eliterack.csv and never
// fails the test run. Run it with: hem_rack_tests --qa
#include "Harness.hpp"
#include "QaCommon.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Plugins.hpp"
#include "hem/rack/Tape.hpp"
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

constexpr int kRandomSettings = 8;   // random settings of all the other controls tried for a still-silent control

// A working control that only acts in one exact situation the generic settings do not reach. Each entry was
// checked in the code; `sets` are applied before playing, `moves` halfway through (a control turned while playing).
struct Setting { const char* id; float value; };
struct Condition { const char* plugin; const char* control; std::vector<Setting> sets, moves; const char* why; };
const std::vector<Condition>& conditions() {
    static const std::vector<Condition> c = {
        {"SWITCHBLADE", "pdr", {{"compon", 1}, {"threshold", -6}, {"ratio", 10}, {"release", 2000}}, {},
         "SMART REL only changes the release after a SHORT hit over the threshold, with a long RELEASE"},
        {"SWERVE", "tie", {}, {{"rate", 5}},
         "TIE keeps the sweep (depth x rate) when RATE is turned while playing"},
    };
    return c;
}

// Controls that only change the screen, never the sound or a meter. Found from the plate where the plate says so
// (a needle's IN / GR / OUT selector, an EQ graph's scale or spectrum switch); the rest checked in the code.
std::string displayOnly(const rack::PluginInfo& info, const FaceLayout& L, int i) {
    for (const Widget& w : L.widgets)
        if (w.kind == WKind::GrMeter && w.param2 == i) return "picks what the needle shows (FaceView::grMeter)";
    if (info.curve && info.curve->scaleParam == i) return "the EQ graph's scale (CurveInfo::scaleParam)";
    if (info.curve && info.curve->spectrumParam == i) return "the music behind the EQ graph on / off (CurveInfo::spectrumParam)";
    if (std::string(info.name) == "ARCHIVE" && i == rack::tape::VuCal) return "which dBFS level reads 0 VU on the needles (FaceView::vuMeter)";
    return {};
}

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
    // GR is on the plate when the plate says so, or a needle shows it. A needle with a two-way IN / OUT selector
    // (param2 with max 1) never shows GR (FaceView::grMeter), so it does not count.
    bool gr = info.plate && info.plate->reportsReduction;
    const auto ps = e.params();
    for (const Widget& w : L.widgets) {
        if (w.kind != WKind::GrMeter || w.choice >= 0) continue;
        const bool twoWay = w.param2 >= 0 && w.meter >= 0 && w.param2 < static_cast<int>(ps.size()) && ps[static_cast<std::size_t>(w.param2)].max < 1.5f;
        gr = gr || !twoWay;
    }
    if (gr) out.push_back({"GAIN REDUCTION", -1});
    return out;
}

void auditDsp(const rack::PluginInfo& info, hemqa::Report& rep) {
    const std::string name = info.name;
    auto e = info.create();
    const std::span<const hc::fx::ParamInfo> ps = e->params();
    const int controls = controlCount(info, static_cast<int>(ps.size()));
    const Stereo full = hemqa::testAudio(1.0), voice = hemqa::voiceAudio(1.0), silence(static_cast<std::size_t>(0.5 * hemqa::kRate));

    e->prepare(hemqa::kRate, hemqa::kBlock);
    e->setDefaults();
    e->reset();
    if (!hemqa::finite(run(*e, full))) rep.add(name, "DSP", "NAN_OR_INF", "defaults", "non-finite output on 1 s of test audio at default settings");

    // Settings of the other controls (stored memory always stays at default): 0 defaults, 1 all at min,
    // 2 all at max, 3 all at middle, 4 defaults played with a single voice (pitch tools),
    // 5 defaults with ONE other control (pairK) at pairV.
    int pairK = 0;
    float pairV = 0;
    auto setContext = [&](int skip, int mode) {
        e->setDefaults();
        if (mode == 0 || mode == 4) return;
        if (mode == 5) { if (pairK != skip) e->set(pairK, pairV); return; }
        if (mode == 6) return;
        if (mode >= 7) {   // a random setting of every other control (seeded: the same every run)
            std::uint32_t r = 0x9E3779B9u * static_cast<std::uint32_t>(mode);
            for (int k = 0; k < controls; ++k) {
                const auto& p = ps[static_cast<std::size_t>(k)];
                r = r * 1664525u + 1013904223u;
                if (k == skip || keepAtDefault(p)) continue;
                const float u = static_cast<float>(r >> 8) / 16777216.0f;
                e->set(k, stepped(p) ? std::floor(p.min + u * (p.max - p.min + 1.0f)) : hc::fx::toPlain(p, u));
            }
            return;
        }
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
        Stereo o;
        if (mode == 6 && param >= 0) {
            // pressed while playing: the first half at its default, then the new value (a push button acts on
            // what it has heard: TRIM, CLEAR, TAP...)
            e->reset();
            Stereo a(full.size() / 2), b(full.size() - full.size() / 2);
            for (std::size_t k = 0; k < a.size(); ++k) { a.l[k] = full.l[k]; a.r[k] = full.r[k]; }
            for (std::size_t k = 0; k < b.size(); ++k) { b.l[k] = full.l[a.size() + k]; b.r[k] = full.r[a.size() + k]; }
            const Stereo oa = run(*e, a);
            e->set(param, value);
            const Stereo ob = run(*e, b);
            o = Stereo(full.size());
            for (std::size_t k = 0; k < oa.size(); ++k) { o.l[k] = oa.l[k]; o.r[k] = oa.r[k]; }
            for (std::size_t k = 0; k < ob.size(); ++k) { o.l[oa.size() + k] = ob.l[k]; o.r[oa.size() + k] = ob.r[k]; }
        } else {
            if (param >= 0) {
                e->set(param, value);
                getOk = hemqa::closeTo(e->get(param), hc::fx::clampPlain(ps[static_cast<std::size_t>(param)], value));
            }
            e->reset();
            o = run(*e, mode == 4 ? voice : full);
        }
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
        auto liveIn = [&](int mode) {
            std::set<std::uint64_t> hashes;
            for (float v : values) { bool b = false, g = true; hashes.insert(stateHash(mode, i, v, b, g)); }
            return hashes.size() > 1;
        };
        // Still silent: a control can depend on what is played (a pitch tool needs a voice) or on ONE other
        // control (a band's frequency needs its gain off 0 dB). Try both before calling it dead.
        if (!live) live = liveIn(4);
        for (int k = 0; k < controls && !live; ++k) {
            const auto& pk = ps[static_cast<std::size_t>(k)];
            if (k == i || keepAtDefault(pk)) continue;
            for (float o : hemqa::sweepValues(pk.min, pk.def, pk.max, stepped(pk))) {
                if (o == pk.def) continue;
                pairK = k; pairV = o;
                if ((live = liveIn(5))) break;
            }
        }
        // ...or it acts when pressed while the music plays, or it needs two or more other controls set
        // together (an EQ / dynamics ORDER switch needs the EQ off flat AND the compressor working).
        if (!live) live = liveIn(6);
        for (int seed = 7; seed < 7 + kRandomSettings && !live; ++seed) live = liveIn(seed);
        for (const Condition& c : conditions()) {
            if (live || name != c.plugin || std::string(p.id) != c.control) continue;
            auto find = [&](const char* id) { for (int k = 0; k < static_cast<int>(ps.size()); ++k) if (std::string(ps[static_cast<std::size_t>(k)].id) == id) return k; return -1; };
            std::set<std::uint64_t> hashes;
            for (float v : values) {
                e = info.create();
                e->prepare(hemqa::kRate, hemqa::kBlock);
                e->setDefaults();
                for (const Setting& st : c.sets) e->set(find(st.id), st.value);
                e->set(i, v);
                e->reset();
                Stereo a(full.size() / 2), b(full.size() - full.size() / 2);
                for (std::size_t k = 0; k < a.size(); ++k) { a.l[k] = full.l[k]; a.r[k] = full.r[k]; }
                for (std::size_t k = 0; k < b.size(); ++k) { b.l[k] = full.l[a.size() + k]; b.r[k] = full.r[a.size() + k]; }
                hemqa::Hash h;
                h.add(run(*e, a));
                for (const Setting& st : c.moves) e->set(find(st.id), st.value);
                h.add(run(*e, b));
                hashes.insert(h.h);
            }
            live = hashes.size() > 1;
            rep.note(name, std::string(p.id) + (live ? ": live in its own situation: " : ": STILL silent in its own situation: ") + c.why);
        }
        if (!live) {
            const std::string why = displayOnly(info, faceLayout(info), i);
            if (!why.empty()) { rep.note(name, std::string(p.id) + ": screen only, by design: " + why); live = true; }
        }
        if (!live) rep.add(name, "DSP", "DEAD_WIRE", p.id, "audio + meters bit-identical at all " + std::to_string(values.size()) +
                           " values, with the other controls at default / min / max / middle, with a single voice, with each"
                           " other control moved on its own, pressed while playing, and in " + std::to_string(kRandomSettings) +
                           " random settings of all the other controls");
    }

    // meters
    const FaceLayout L = faceLayout(info);
    const std::vector<MeterRef> meters = plateMeters(info, *e, L);
    auto read = [&](const MeterRef& m) { return m.index < 0 ? e->reductionDb() : e->meter(m.index); };
    std::vector<int> moved(meters.size(), 0), bad(meters.size(), 0);
    std::vector<float> lastHot(meters.size(), 0.0f);
    auto meterPass = [&](int mode) {
        e = info.create();
        e->prepare(hemqa::kRate, hemqa::kBlock);
        setContext(-1, mode);
        e->reset();
        (void)run(*e, silence);
        std::vector<float> quiet;
        for (const auto& m : meters) quiet.push_back(read(m));
        // read the meters after every block, the way a faceplate reads them while the music plays
        const Stereo& in = mode == 4 ? voice : full;
        Stereo blk(hemqa::kBlock);
        for (std::size_t pos = 0; pos < in.size(); pos += hemqa::kBlock) {
            const std::size_t nb = std::min<std::size_t>(hemqa::kBlock, in.size() - pos);
            std::copy_n(in.l.data() + pos, nb, blk.l.data());
            std::copy_n(in.r.data() + pos, nb, blk.r.data());
            e->process(blk.l.data(), blk.r.data(), static_cast<std::uint32_t>(nb));
            for (std::size_t m = 0; m < meters.size(); ++m) {
                const float hot = read(meters[m]);
                if (!std::isfinite(hot) || !std::isfinite(quiet[m])) bad[m] = 1;
                else if (quiet[m] != hot) moved[m] = 1;
                lastHot[m] = hot;
            }
        }
    };
    auto allMoved = [&] { return std::all_of(moved.begin(), moved.end(), [](int x) { return x != 0; }); };
    for (int mode = 0; mode <= 4; ++mode) meterPass(mode);
    for (int seed = 7; seed < 7 + kRandomSettings && !allMoved(); ++seed) meterPass(seed);
    // Same as the sweep: a meter can need one other control moved (a compressor's GR needs a lower threshold).
    for (int k = 0; k < controls && !allMoved(); ++k) {
        const auto& pk = ps[static_cast<std::size_t>(k)];
        if (keepAtDefault(pk)) continue;
        for (float o : hemqa::sweepValues(pk.min, pk.def, pk.max, stepped(pk))) {
            if (o == pk.def) continue;
            pairK = k; pairV = o;
            meterPass(5);
            if (allMoved()) break;
        }
    }
    for (std::size_t m = 0; m < meters.size(); ++m) {
        if (bad[m]) rep.add(name, "METER", "METER_NAN_OR_INF", meters[m].name, "non-finite meter value");
        if (!moved[m]) rep.add(name, "METER", "METER_STUCK", meters[m].name, "reads " + hemqa::fmt("%g", lastHot[m]) +
                               " for both silence and hot signal (read after every block), with the controls at default / min / max /"
                               " middle, with a single voice, in random settings and with each other control moved on its own");
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
