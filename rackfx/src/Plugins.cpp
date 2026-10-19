#include "hem/rack/Plugins.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Tools.hpp"
#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/ColorFx.hpp"
#include "hem/rack/Space.hpp"
#include "hem/rack/Movement.hpp"
#include "hem/rack/Specialty.hpp"

#include <cstring>

namespace hem::rack {
namespace {
const PluginInfo kPlugins[] = {
    {"SURGEON", "MASTERING EQUALIZER", "SHAPE WHAT MATTERS", "Fx|EQ",
     "16-band EQ: surgical or musical, dynamic bands, mid / side, analog character",
     {0x4E3A71C2u, 0x9D0B4F6Au, 0xA1C35E28u, 0x7F6B0D13u}, {0x2C8E5B90u, 0x61F14A07u, 0xB3D2786Eu, 0x0A9C4E51u},
     Face::EliteEq, Group::Eq, &makeEliteEq},
    {"THE CLEANER", "AUDIO RESTORATION SUITE", "RESTORE WHAT MATTERS", "Fx|Restoration",
     "Adaptive hiss and noise reduction that keeps the air and the attacks",
     {0x7B19E4D3u, 0x3A6C42F1u, 0x95E0B87Au, 0xC4271D6Bu}, {0x1E5D93A8u, 0xF2B04C69u, 0x6A83E15Du, 0xD70C2B94u},
     Face::Restorer, Group::Tools, &makeRestorer},
    {"THE FAMILY", "MIX BUS COMPRESSOR", "GLUE WHAT MATTERS", "Fx|Dynamics",
     "Console-style bus compressor: a few dB that pulls the mix together",
     {0x8123CD1Fu, 0x26D64A8Fu, 0x85460D6Cu, 0x9DE3CF07u}, {0x41B35BAAu, 0xE74641DAu, 0xBAA0F616u, 0x7A3DABCDu},
     Face::BusGlue, Group::Dynamics, &makeBusGlue},
    {"ARCHIVE", "TWO-TRACK TAPE MACHINE", "WARM WHAT MATTERS", "Fx|Distortion",
     "Tape machine: saturation, head bump, 15 / 30 IPS, wow, hiss",
     {0xC19770CEu, 0x3C3E4544u, 0xB85176CFu, 0xFC8B0FFCu}, {0xD6247491u, 0xB8DC4C3Du, 0xB366FBB3u, 0xC119C02Fu},
     Face::Tape, Group::Color, &makeTape},
    {"VELVET", "PASSIVE PROGRAM EQUALIZER", "LIFT WHAT MATTERS", "Fx|EQ",
     "Classic passive tube EQ: low boost and atten, high boost, air",
     {0x661A429Fu, 0xE4AF4669u, 0xA64C8773u, 0xABF96B39u}, {0x81122522u, 0xEB354A97u, 0x8459C53Bu, 0xFEF050E8u},
     Face::TubeEq, Group::Eq, &makeTubeEq},
    {"LOCKDOWN", "MASTERING LIMITER", "FINISH WHAT MATTERS", "Fx|Dynamics",
     "True-peak mastering limiter: loud, clean, never over the ceiling",
     {0xFD6E565Fu, 0x14E04E14u, 0x9E86898Cu, 0x56C20F12u}, {0x04E63B1Eu, 0x87E94A5Cu, 0xAA51530Du, 0x42655297u},
     Face::Limiter, Group::Dynamics, &makeLimiter},
    {"WIDE LOAD", "STEREO IMAGER", "WIDEN WHAT MATTERS", "Fx|Spatial",
     "Width from mono to extra-wide, mono bass for a tight centred low end",
     {0x6E699F26u, 0x846A42C5u, 0xB9EDBDE1u, 0x92BAE369u}, {0xB14269B2u, 0x82534750u, 0xAFC1627Au, 0x1A08719Cu},
     Face::Width, Group::Tools, &makeStereoWidth},
    {"SPARK", "HARMONIC EXCITER", "BRIGHTEN WHAT MATTERS", "Fx|Distortion",
     "Adds fresh tube or tape harmonics up top: air and presence without EQ",
     {0x0355171Du, 0xF09642EFu, 0x8333D0E4u, 0x1487B5A3u}, {0xB6DF8D8Du, 0xE6F8456Eu, 0x9AC15612u, 0xD053DB43u},
     Face::Exciter, Group::Color, &makeExciter},
    {"THE ACCOUNTANT", "LOUDNESS ANALYZER", "MEASURE WHAT MATTERS", "Fx|Analyzer",
     "LUFS momentary / short / integrated, range, true peak, phase",
     {0xBCE2E2E1u, 0x1F5C4964u, 0xB9783BC2u, 0x4B40A45Bu}, {0xF68CB16Cu, 0x90254E67u, 0xAD7EAB9Du, 0x64A0886Eu},
     Face::Meter, Group::Tools, &makeLoudnessMeter},
    {"ENFORCER", "MODERN COMPRESSOR", "CONTROL WHAT MATTERS", "Fx|Dynamics",
     "Clean, modern compressor: ratio, knee, attack, release or auto, auto makeup, sidechain HPF",
     {0x823E513Au, 0x83E8451Bu, 0xA51AB94Cu, 0xE6F6EA5Bu}, {0x1FBA4F56u, 0xF9B74DC4u, 0xA5C98199u, 0x9A92B5B2u},
     Face::VcaComp, Group::Dynamics, &makeVcaComp},
    {"FURNACE", "TUBE SATURATOR", "WARM WHAT MATTERS", "Fx|Distortion",
     "Tube stage: drive, bias for even harmonics, tone, mix - colour without the volume jump",
     {0x4B77D551u, 0xB1494534u, 0x8B631BB3u, 0x5A93E0F1u}, {0x84D480F5u, 0xD1364F7Du, 0x91770D82u, 0xA8291747u},
     Face::TubeSat, Group::Color, &makeTubeSat},
    {"CATHEDRAL", "CONCERT HALL REVERB", "SPACE WHAT MATTERS", "Fx|Reverb",
     "Big, lush hall: pre-delay, size, decay, damping, early reflections, modulation",
     {0x385E671Fu, 0x66FF4D76u, 0x9B107E22u, 0x61F3B9D4u}, {0x562C6642u, 0x032845B0u, 0x9CD3E930u, 0x24DD7BBCu},
     Face::Hall, Group::Space, &makeHall},
    {"CHROME", "PLATE REVERB", "SHINE WHAT MATTERS", "Fx|Reverb",
     "Bright, dense plate: instant smooth tail for vocals and snares",
     {0x41724663u, 0x5F744FE1u, 0x974108DDu, 0xFD3E46F9u}, {0x71F19E30u, 0xEE154D54u, 0x96B15612u, 0x9B0E28D4u},
     Face::Plate, Group::Space, &makePlate},
    {"REPEAT OFFENDER", "STEREO DELAY", "ECHO WHAT MATTERS", "Fx|Delay",
     "Clean stereo delay: note values at a BPM, ping-pong, filters in the repeats",
     {0x72B3AE16u, 0x72E34303u, 0xBD88785Fu, 0x7CFF75D3u}, {0x0D7A0C5Bu, 0x2A184EE2u, 0xB500AA8Du, 0x0D36A023u},
     Face::DigitalDelay, Group::Space, &makeDigitalDelay},
    {"THE CREW", "STEREO CHORUS", "THICKEN WHAT MATTERS", "Fx|Modulation",
     "Lush stereo chorus: one to three voices, rate, depth, width",
     {0x48350A57u, 0xB56744A0u, 0xB738E358u, 0x9B71D740u}, {0x14845463u, 0x22734602u, 0x90F751E2u, 0x212E7234u},
     Face::Chorus, Group::Movement, &makeChorus},
    {"GETAWAY", "JET FLANGER", "SWEEP WHAT MATTERS", "Fx|Modulation",
     "Jet-sweep flanger: manual, depth, positive or negative feedback",
     {0x53032564u, 0xED2D4397u, 0x92AB2CE1u, 0xBA4B4F0Fu}, {0x0DE8CBF5u, 0xDA764B82u, 0x9979AE24u, 0xE9E9C89Du},
     Face::Flanger, Group::Movement, &makeFlanger},
    {"SMOKESCREEN", "STAGE PHASER", "SWIRL WHAT MATTERS", "Fx|Modulation",
     "Phaser: 4 to 12 stages swept around a center, feedback",
     {0x97487206u, 0xF5E54729u, 0xB0810E54u, 0xE3ED0A51u}, {0x427A7AA9u, 0x8258496Du, 0xBCA3A26Au, 0xE2CC7DA4u},
     Face::Phaser, Group::Movement, &makePhaser},
    {"SHAKEDOWN", "TREMOLO / AUTO PAN", "PULSE WHAT MATTERS", "Fx|Modulation",
     "Tremolo or auto pan: sine, triangle or square",
     {0xF4DD0E25u, 0x58814A73u, 0xACE4F6C9u, 0xEE99C36Bu}, {0xCDA68B31u, 0x5CB2472Cu, 0x8F966234u, 0xA654200Du},
     Face::Tremolo, Group::Movement, &makeTremolo},
    {"CYCLONE", "ROTATING SPEAKER", "SPIN THE SOUND", "Fx|Modulation",
     "Rotating speaker: slow / fast with real spin-up, drive, horn and drum",
     {0xA46F700Fu, 0x6F954377u, 0xB3860B04u, 0x7662B910u}, {0xDE2F1818u, 0xFAF9455Fu, 0x8CA8643Eu, 0x63AE488Au},
     Face::Rotary, Group::Movement, &makeRotary},
    {"SHAPESHIFTER", "ENVELOPE FILTER", "MOVE WHAT MATTERS", "Fx|Filter",
     "Resonant filter that follows the music or an LFO: low, band or high-pass",
     {0x8AE47DD6u, 0xE4214FCDu, 0xB41EE0B9u, 0x52D638C9u}, {0x5E10F011u, 0xEA344B32u, 0xB87C53EFu, 0xF056BF17u},
     Face::AutoFilter, Group::Movement, &makeAutoFilter},
    {"UNDERGROUND", "SUB ENHANCER", "DEEPEN WHAT MATTERS", "Fx|Tools",
     "More low end: a new sub an octave down, or harmonics phones can hear",
     {0xBEBC94ACu, 0xFA844612u, 0xA7B73D4Au, 0xE5A8AE97u}, {0x9AC67CF1u, 0x69904D0Du, 0x9BF19F3Bu, 0xB405C9E6u},
     Face::Sub, Group::Tools, &makeSub},
    {"SPOTLIGHT", "VOCAL CHANNEL", "LEAD WHAT MATTERS", "Fx|Tools",
     "Vocal chain on one plate: high-pass, body, compress, de-ess, presence, air",
     {0x5402C843u, 0x92874D3Au, 0xA6C4FC1Au, 0x5A305B03u}, {0x9460E821u, 0x92FA46E0u, 0x8C435F35u, 0x8287A4EDu},
     Face::VocalStrip, Group::Tools, &makeVocalStrip},
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
