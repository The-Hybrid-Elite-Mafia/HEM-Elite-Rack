#pragma once
// Tests start every plug-in from its old coloured defaults (ColouredStart.hpp): the plug-ins themselves now load
// with every colour control at zero and pass the sound untouched, which release_tests.cpp and qa_report.cpp check.
#include "ColouredStart.hpp"
#include "hem/rack/Plugins.hpp"

#include <memory>
#include <string>

namespace hem::test {

inline void recolour(hc::fx::Effect& e, const hem::rack::PluginInfo& info) {
    const auto ps = e.params();
    for (const auto& [id, v] : colourstart::forName(info.name))
        for (std::size_t i = 0; i < ps.size(); ++i)
            if (std::string(ps[i].id) == id) e.set(static_cast<int>(i), v);
}
inline void recolour(hc::fx::Effect& e) {   // finds the plug-in by its parameter table
    for (const auto& info : hem::rack::plugins())
        if (info.create()->params().data() == e.params().data()) { recolour(e, info); return; }
}
inline std::unique_ptr<hc::fx::Effect> colouredCreate(const hem::rack::PluginInfo* info) {
    auto e = info->create();
    recolour(*e, *info);
    return e;
}

} // namespace hem::test
