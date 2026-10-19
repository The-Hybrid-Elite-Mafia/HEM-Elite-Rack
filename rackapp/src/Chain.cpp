#include "Chain.hpp"

#include "hem/rack/Plugins.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace hem::app {

const char* pluginName(const hc::fx::Effect& e) {
    // Every plug-in has its own static parameter table: that identifies it.
    static const std::vector<std::pair<const hc::fx::ParamInfo*, const char*>> tables = [] {
        std::vector<std::pair<const hc::fx::ParamInfo*, const char*>> t;
        for (const auto& p : rack::plugins()) t.emplace_back(p.create()->params().data(), p.name);
        return t;
    }();
    for (const auto& [table, name] : tables)
        if (table == e.params().data()) return name;
    return "";
}

std::vector<SlotState> snapshotChain(const RackProcessor& rack) {
    std::vector<SlotState> out;
    for (int s = 0; s < kMaxSlots; ++s) {
        const hc::fx::Effect* e = rack.effect(s);
        if (!e) continue;
        SlotState st;
        st.slot = s;
        st.on = rack.slotOn(s);
        st.name = pluginName(*e);
        const auto ps = e->params();
        for (std::size_t i = 0; i < ps.size(); ++i) st.values.emplace_back(ps[i].id, e->get(static_cast<int>(i)));
        out.push_back(std::move(st));
    }
    return out;
}

std::string chainToText(const std::vector<SlotState>& chain) {
    std::string out;
    char buf[64];
    for (const SlotState& s : chain) {
        out += "slot" + std::to_string(s.slot + 1) + "=" + s.name + "|on=" + (s.on ? "1" : "0");
        for (const auto& [id, v] : s.values) {
            std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v));
            out += "|" + id + "=" + buf;
        }
        out += "\n";
    }
    return out;
}

std::vector<SlotState> chainFromText(const std::string& text, std::string* note) {
    std::vector<SlotState> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("slot", 0) != 0) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const int slot = std::atoi(line.substr(4, eq - 4).c_str()) - 1;
        if (slot < 0 || slot >= kMaxSlots) continue;
        SlotState st;
        st.slot = slot;
        std::size_t p = eq + 1, bar = line.find('|', p);
        st.name = line.substr(p, bar == std::string::npos ? std::string::npos : bar - p);
        if (!rack::findPlugin(st.name.c_str())) {
            if (note) *note += "unknown plug-in \"" + st.name + "\" skipped; ";
            continue;
        }
        while (bar != std::string::npos) {
            p = bar + 1;
            bar = line.find('|', p);
            const std::string kv = line.substr(p, bar == std::string::npos ? std::string::npos : bar - p);
            const auto e = kv.find('=');
            if (e == std::string::npos) continue;
            const std::string k = kv.substr(0, e);
            const float v = std::strtof(kv.c_str() + e + 1, nullptr);
            if (k == "on") st.on = v >= 0.5f;
            else st.values.emplace_back(k, v);
        }
        std::erase_if(out, [&](const SlotState& o) { return o.slot == slot; });
        out.push_back(std::move(st));
    }
    std::sort(out.begin(), out.end(), [](const SlotState& a, const SlotState& b) { return a.slot < b.slot; });
    return out;
}

std::unique_ptr<hc::fx::Effect> makePlugin(const char* name) {
    const rack::PluginInfo* info = rack::findPlugin(name);
    if (!info) return nullptr;
    auto e = info->create();
    e->prepare(kRate, kChunk);
    return e;
}

std::unique_ptr<hc::fx::Effect> makePlugin(const SlotState& state) {
    auto e = makePlugin(state.name.c_str());
    if (!e) return nullptr;
    const auto ps = e->params();
    for (const auto& [id, v] : state.values)
        for (std::size_t i = 0; i < ps.size(); ++i)
            if (id == ps[i].id) { e->set(static_cast<int>(i), v); break; }
    e->reset();   // start from the restored settings, not from a glide toward them
    return e;
}

bool renderOffline(const Song& song, const std::vector<SlotState>& chain, std::vector<float>& outL, std::vector<float>& outR,
                   const std::function<bool(float)>& progress, std::string* error) {
    std::vector<std::unique_ptr<hc::fx::Effect>> fx;
    std::uint64_t latency = 0;
    for (const SlotState& s : chain) {
        if (!s.on) continue;
        auto e = makePlugin(s);
        if (!e) { if (error) *error = "unknown plug-in " + s.name; return false; }
        latency += e->latency();
        fx.push_back(std::move(e));
    }
    const std::uint64_t n = song.frames();
    const std::uint64_t total = n + latency;
    outL.assign(n, 0.0f);
    outR.assign(n, 0.0f);
    std::vector<float> l(kChunk), r(kChunk);
    std::uint64_t lastReport = 0;
    for (std::uint64_t pos = 0; pos < total; pos += kChunk) {
        const auto m = static_cast<std::uint32_t>(std::min<std::uint64_t>(kChunk, total - pos));
        for (std::uint32_t i = 0; i < m; ++i) {
            const std::uint64_t k = pos + i;
            l[i] = k < n ? song.left[k] : 0.0f;
            r[i] = k < n ? song.right[k] : 0.0f;
        }
        for (auto& e : fx) e->process(l.data(), r.data(), m);
        for (std::uint32_t i = 0; i < m; ++i) {
            const std::uint64_t k = pos + i;
            if (k >= latency && k - latency < n) {
                outL[k - latency] = l[i];
                outR[k - latency] = r[i];
            }
        }
        if (progress && pos - lastReport >= 48000) {
            lastReport = pos;
            if (!progress(static_cast<float>(static_cast<double>(pos) / static_cast<double>(std::max<std::uint64_t>(total, 1))))) {
                if (error) *error = "cancelled";
                return false;
            }
        }
    }
    if (progress) progress(1.0f);
    return true;
}

} // namespace hem::app
