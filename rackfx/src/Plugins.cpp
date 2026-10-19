#include "hem/rack/Plugins.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/ConsoleEq.hpp"
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
    {"CONSOLE EQ", "CHANNEL EQUALIZER", "COLOR WHAT MATTERS", "Fx|EQ",
     "Big-console channel EQ: filters, four bands, classic or modern curves, drive",
     {0x9CAF2094u, 0x19C94666u, 0x84891576u, 0x5D6DCD8Fu}, {0xA1FA67E5u, 0x71DD4649u, 0xB1EB1036u, 0xB378AFE0u},
     Face::ConsoleEq, &makeConsoleEq},
    {"BUS GLUE", "MIX BUS COMPRESSOR", "GLUE WHAT MATTERS", "Fx|Dynamics",
     "Console-style bus compressor: a few dB that pulls the mix together",
     {0x8123CD1Fu, 0x26D64A8Fu, 0x85460D6Cu, 0x9DE3CF07u}, {0x41B35BAAu, 0xE74641DAu, 0xBAA0F616u, 0x7A3DABCDu},
     Face::BusGlue, &makeBusGlue},
};
} // namespace

std::span<const PluginInfo> plugins() noexcept { return kPlugins; }

const PluginInfo* findPlugin(const char* name) noexcept {
    for (const PluginInfo& p : kPlugins) if (name && std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

} // namespace hem::rack
