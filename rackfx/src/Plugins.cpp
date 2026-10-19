#include "hem/rack/Plugins.hpp"

#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Restorer.hpp"

#include <cstring>

namespace hem::rack {
namespace {
const PluginInfo kPlugins[] = {
    {"ELITE EQ", "MASTERING EQUALIZER", "SHAPE WHAT MATTERS", "Fx|EQ",
     "16-band EQ: surgical or musical, dynamic bands, mid / side, analog character",
     {0x4E3A71C2u, 0x9D0B4F6Au, 0xA1C35E28u, 0x7F6B0D13u}, {0x2C8E5B90u, 0x61F14A07u, 0xB3D2786Eu, 0x0A9C4E51u},
     Face::EliteEq, &makeEliteEq},
    {"RESTORER", "AUDIO RESTORATION SUITE", "RESTORE WHAT MATTERS", "Fx|Restoration",
     "Adaptive hiss and noise reduction that keeps the air and the attacks",
     {0x7B19E4D3u, 0x3A6C42F1u, 0x95E0B87Au, 0xC4271D6Bu}, {0x1E5D93A8u, 0xF2B04C69u, 0x6A83E15Du, 0xD70C2B94u},
     Face::Restorer, &makeRestorer},
};
} // namespace

std::span<const PluginInfo> plugins() noexcept { return kPlugins; }

const PluginInfo* findPlugin(const char* name) noexcept {
    for (const PluginInfo& p : kPlugins) if (name && std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

} // namespace hem::rack
