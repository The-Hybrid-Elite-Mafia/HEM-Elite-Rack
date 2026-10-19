#include "hem/rack/Plugins.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/ConsoleEq.hpp"
#include "hem/rack/Dynamics.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Tools.hpp"

#include <cstring>

namespace hem::rack {
namespace {
const PluginInfo kPlugins[] = {
    {"ELITE EQ", "MASTERING EQUALIZER", "SHAPE WHAT MATTERS", "Fx|EQ",
     "16-band EQ: surgical or musical, dynamic bands, mid / side, analog character",
     {0x4E3A71C2u, 0x9D0B4F6Au, 0xA1C35E28u, 0x7F6B0D13u}, {0x2C8E5B90u, 0x61F14A07u, 0xB3D2786Eu, 0x0A9C4E51u},
     Face::EliteEq, Group::Eq, &makeEliteEq},
    {"RESTORER", "AUDIO RESTORATION SUITE", "RESTORE WHAT MATTERS", "Fx|Restoration",
     "Adaptive hiss and noise reduction that keeps the air and the attacks",
     {0x7B19E4D3u, 0x3A6C42F1u, 0x95E0B87Au, 0xC4271D6Bu}, {0x1E5D93A8u, 0xF2B04C69u, 0x6A83E15Du, 0xD70C2B94u},
     Face::Restorer, Group::Tools, &makeRestorer},
    {"CONSOLE EQ", "CHANNEL EQUALIZER", "COLOR WHAT MATTERS", "Fx|EQ",
     "Big-console channel EQ: filters, four bands, classic or modern curves, drive",
     {0x9CAF2094u, 0x19C94666u, 0x84891576u, 0x5D6DCD8Fu}, {0xA1FA67E5u, 0x71DD4649u, 0xB1EB1036u, 0xB378AFE0u},
     Face::ConsoleEq, Group::Eq, &makeConsoleEq},
    {"BUS GLUE", "MIX BUS COMPRESSOR", "GLUE WHAT MATTERS", "Fx|Dynamics",
     "Console-style bus compressor: a few dB that pulls the mix together",
     {0x8123CD1Fu, 0x26D64A8Fu, 0x85460D6Cu, 0x9DE3CF07u}, {0x41B35BAAu, 0xE74641DAu, 0xBAA0F616u, 0x7A3DABCDu},
     Face::BusGlue, Group::Dynamics, &makeBusGlue},
    {"TAPE", "TWO-TRACK TAPE MACHINE", "WARM WHAT MATTERS", "Fx|Distortion",
     "Tape machine: saturation, head bump, 15 / 30 IPS, wow, hiss",
     {0xC19770CEu, 0x3C3E4544u, 0xB85176CFu, 0xFC8B0FFCu}, {0xD6247491u, 0xB8DC4C3Du, 0xB366FBB3u, 0xC119C02Fu},
     Face::Tape, Group::Color, &makeTape},
    {"TUBE EQ", "PASSIVE PROGRAM EQUALIZER", "LIFT WHAT MATTERS", "Fx|EQ",
     "Classic passive tube EQ: low boost and atten, high boost, air",
     {0x661A429Fu, 0xE4AF4669u, 0xA64C8773u, 0xABF96B39u}, {0x81122522u, 0xEB354A97u, 0x8459C53Bu, 0xFEF050E8u},
     Face::TubeEq, Group::Eq, &makeTubeEq},
    {"VARI-MU", "TUBE COMPRESSOR", "SMOOTH WHAT MATTERS", "Fx|Dynamics",
     "Variable-mu tube compressor: smooth glue, six time constants, mid / side",
     {0x4ED10205u, 0x88854719u, 0xBB625414u, 0x1CBC8170u}, {0x9C005F99u, 0x59F940CAu, 0xB5143F9Au, 0x7000958Eu},
     Face::VariMu, Group::Dynamics, &makeVariMu},
    {"OPTO", "OPTICAL LEVELER", "LEVEL WHAT MATTERS", "Fx|Dynamics",
     "Optical leveler: two knobs, gentle and musical, great on vocals and bass",
     {0x61DE98F0u, 0x1AE84259u, 0xB012E407u, 0x09D9F8FCu}, {0x1D8FB1BCu, 0x02654572u, 0x8EB1D2A1u, 0xA7614086u},
     Face::Opto, Group::Dynamics, &makeOpto},
    {"FET", "LIMITING AMPLIFIER", "PUNCH WHAT MATTERS", "Fx|Dynamics",
     "Fast FET compressor: attack, punch and grit, ratios up to ALL",
     {0x1C21C92Bu, 0x92DD4FC3u, 0x84EF7CE6u, 0x153913B0u}, {0x55FC9BFBu, 0xBEBB4626u, 0x89CC8A79u, 0x4D90FC5Bu},
     Face::Fet, Group::Dynamics, &makeFet},
    {"LIMITER", "MASTERING LIMITER", "FINISH WHAT MATTERS", "Fx|Dynamics",
     "True-peak mastering limiter: loud, clean, never over the ceiling",
     {0xFD6E565Fu, 0x14E04E14u, 0x9E86898Cu, 0x56C20F12u}, {0x04E63B1Eu, 0x87E94A5Cu, 0xAA51530Du, 0x42655297u},
     Face::Limiter, Group::Dynamics, &makeLimiter},
    {"DE-ESSER", "VOCAL DE-ESSER", "SMOOTH THE ESSES", "Fx|Dynamics",
     "Tames harsh S and T sounds: split or wide, listen to what it hears",
     {0x9305D64Cu, 0xE790482Eu, 0x96DF927Du, 0xBBBAB858u}, {0x73474311u, 0xF42A4CD2u, 0x9ECDF913u, 0xFF1E1240u},
     Face::DeEsser, Group::Dynamics, &makeDeEsser},
    {"STEREO WIDTH", "STEREO IMAGER", "WIDEN WHAT MATTERS", "Fx|Spatial",
     "Width from mono to extra-wide, mono bass for a tight centred low end",
     {0x6E699F26u, 0x846A42C5u, 0xB9EDBDE1u, 0x92BAE369u}, {0xB14269B2u, 0x82534750u, 0xAFC1627Au, 0x1A08719Cu},
     Face::Width, Group::Tools, &makeStereoWidth},
    {"CLIPPER", "SOFT CLIPPER", "SHAVE WHAT MATTERS", "Fx|Dynamics",
     "Oversampled clipper: shaves peaks for loudness before the limiter",
     {0x75A3B644u, 0x674C4FADu, 0xA8A90416u, 0x0D6167D7u}, {0x622EA2E3u, 0xD681408Fu, 0x883B5324u, 0xDE5E262Eu},
     Face::Clipper, Group::Dynamics, &makeClipper},
    {"EXCITER", "HARMONIC EXCITER", "BRIGHTEN WHAT MATTERS", "Fx|Distortion",
     "Adds fresh tube or tape harmonics up top: air and presence without EQ",
     {0x0355171Du, 0xF09642EFu, 0x8333D0E4u, 0x1487B5A3u}, {0xB6DF8D8Du, 0xE6F8456Eu, 0x9AC15612u, 0xD053DB43u},
     Face::Exciter, Group::Color, &makeExciter},
    {"TRANSIENT", "TRANSIENT SHAPER", "SHAPE THE HIT", "Fx|Dynamics",
     "More or less punch, more or less ring, at any level",
     {0x77B169CFu, 0x78534B33u, 0xA45C7DD3u, 0xF3FECB74u}, {0x94DA01D4u, 0x9BCF42DDu, 0x86F9A88Fu, 0x38D5CDCBu},
     Face::Transient, Group::Dynamics, &makeTransient},
    {"LOUDNESS METER", "LOUDNESS ANALYZER", "MEASURE WHAT MATTERS", "Fx|Analyzer",
     "LUFS momentary / short / integrated, range, true peak, phase",
     {0xBCE2E2E1u, 0x1F5C4964u, 0xB9783BC2u, 0x4B40A45Bu}, {0xF68CB16Cu, 0x90254E67u, 0xAD7EAB9Du, 0x64A0886Eu},
     Face::Meter, Group::Tools, &makeLoudnessMeter},
};
} // namespace

const char* groupName(Group g) noexcept {
    switch (g) {
    case Group::Eq: return "EQ";
    case Group::Dynamics: return "DYNAMICS";
    case Group::Color: return "COLOR";
    case Group::Space: return "SPACE";
    case Group::Movement: return "MOVEMENT";
    case Group::Tools: return "TOOLS";
    case Group::kCount: break;
    }
    return "";
}

std::span<const PluginInfo> plugins() noexcept { return kPlugins; }

const PluginInfo* findPlugin(const char* name) noexcept {
    for (const PluginInfo& p : kPlugins) if (name && std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

} // namespace hem::rack
