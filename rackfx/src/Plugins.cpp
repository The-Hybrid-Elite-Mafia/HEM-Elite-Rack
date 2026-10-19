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
#include "hem/rack/ClassicEqs.hpp"
#include "hem/rack/MoreDynamics.hpp"

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
    {"BRIT PRE", "BRITISH CONSOLE CHANNEL", "DRIVE WHAT MATTERS", "Fx|EQ",
     "British console channel: 18 dB high-pass, low, mid and high, preamp drive through iron",
     {0xEC9EC13Cu, 0x5510450Bu, 0xBBC55190u, 0x8EC26DE3u}, {0x9F1768D3u, 0x833D4210u, 0x8AAC1AE2u, 0xC0FB90B3u},
     Face::BritPre, Group::Eq, &makeBritPre, &kBritCurve},
    {"AMERICAN EQ", "THREE-BAND EQUALIZER", "PUNCH WHAT MATTERS", "Fx|EQ",
     "Punchy three-band EQ: proportional Q, peak or shelf, band-pass filter, drive",
     {0x340E095Eu, 0x58914483u, 0xB15FDE44u, 0xC498D070u}, {0x7F790A9Cu, 0xA19546C5u, 0xBBEF70E2u, 0x9E2D4318u},
     Face::AmericanEq, Group::Eq, &makeAmericanEq, &kAmericanCurve},
    {"MID EQ", "TUBE MIDRANGE EQUALIZER", "FOCUS WHAT MATTERS", "Fx|EQ",
     "Tube midrange EQ: low peak, dip, high peak: shape the middle of a mix",
     {0xF6958F8Cu, 0x84DC4B49u, 0x948ED3FFu, 0x92B8921Bu}, {0x863B7A41u, 0xF0324AB8u, 0x9E6DB7CBu, 0xD06E810Cu},
     Face::MidEq, Group::Eq, &makeMidEq, &kMidEqCurve},
    {"AIR EQ", "MASTERING EQUALIZER", "OPEN WHAT MATTERS", "Fx|EQ",
     "Clean, broad mastering EQ: sub to 2.5 kHz bands and an AIR shelf up to 20 kHz",
     {0x5B76F458u, 0xB9A343B9u, 0xA698AE67u, 0x7392734Du}, {0xAD625C42u, 0x10D140C3u, 0xA15730EAu, 0x04F60D31u},
     Face::AirEq, Group::Eq, &makeAirEq, &kAirCurve},
    {"GRAPHIC EQ", "TEN-BAND GRAPHIC EQUALIZER", "BALANCE WHAT MATTERS", "Fx|EQ",
     "Ten bands, 31 Hz to 16 kHz, ±12 dB: quick, clear tone shaping",
     {0xC553EDB8u, 0x018244E0u, 0x85EE116Eu, 0x267C903Cu}, {0x297B4BAEu, 0xFA464996u, 0xB2B2C9B7u, 0x0D3F53FCu},
     Face::GraphicEq, Group::Eq, &makeGraphicEq, &kGraphicCurve},
    {"TILT", "TILT EQUALIZER", "TIP WHAT MATTERS", "Fx|EQ",
     "One knob: darker or brighter around a pivot, plus low cut and high cut",
     {0xD5CACDF2u, 0xCE234DCBu, 0x965055ACu, 0x0DAEFD70u}, {0x25474B39u, 0xB559473Au, 0xB1B03CD2u, 0xBA9FF5D0u},
     Face::Tilt, Group::Eq, &makeTilt, &kTiltCurve},
    {"VCA COMP", "MODERN COMPRESSOR", "CONTROL WHAT MATTERS", "Fx|Dynamics",
     "Clean, modern compressor: ratio, knee, attack, release or auto, auto makeup, sidechain HPF",
     {0x823E513Au, 0x83E8451Bu, 0xA51AB94Cu, 0xE6F6EA5Bu}, {0x1FBA4F56u, 0xF9B74DC4u, 0xA5C98199u, 0x9A92B5B2u},
     Face::VcaComp, Group::Dynamics, &makeVcaComp},
    {"PUNCH 160", "RMS COMPRESSOR", "HIT WHAT MATTERS", "Fx|Dynamics",
     "Classic RMS compressor: threshold, compression, over easy - punchy drums and bass",
     {0xC892426Fu, 0x97AC4C40u, 0xA9C2E348u, 0xC378F1B4u}, {0xAC2865D4u, 0xACDC4FDFu, 0xBA504220u, 0xB97B78A2u},
     Face::Punch160, Group::Dynamics, &makePunch160},
    {"THRUST BUS", "BUS COMPRESSOR", "DRIVE THE MIX", "Fx|Dynamics",
     "Punchy bus compressor: thrust keeps the bass from pumping, knee, new or old style",
     {0x10BDADF2u, 0x7D7B4966u, 0xA6EC2503u, 0x005C2DDAu}, {0x700B8492u, 0x2D2E4528u, 0xB294A3AAu, 0x3DF92F30u},
     Face::ThrustBus, Group::Dynamics, &makeThrustBus},
    {"BRIT BUS", "DIODE BRIDGE COMPRESSOR", "WEIGHT WHAT MATTERS", "Fx|Dynamics",
     "British bus compressor and limiter: auto recovery, iron drive, thick and smooth",
     {0xA16BD1C1u, 0xAA0B45A9u, 0xB511151Fu, 0xB398B2F4u}, {0x90C334DAu, 0xD9774D28u, 0x9A84ED2Bu, 0xC7B4235Au},
     Face::BritBus, Group::Dynamics, &makeBritBus},
    {"GATE", "GATE / EXPANDER", "SILENCE WHAT DOESN'T", "Fx|Dynamics",
     "Gate or expander: range, hold, hysteresis, sidechain filters, listen",
     {0xE0412A84u, 0x2B2544FFu, 0xA67BB3F2u, 0x1369187Fu}, {0xCEA8B3ADu, 0xFACF47CBu, 0x8962EFE7u, 0x4A03D6DFu},
     Face::Gate, Group::Dynamics, &makeGate},
    {"MULTIBAND", "THREE-BAND COMPRESSOR", "BALANCE THE BANDS", "Fx|Dynamics",
     "Three-band compressor: low, mid and high each with their own threshold, ratio and gain",
     {0x6C8E86E2u, 0x22C9473Cu, 0x998322BCu, 0xB093647Bu}, {0xB1E68B77u, 0x2FBD4391u, 0xA66F0851u, 0x05ABC7A1u},
     Face::Multiband, Group::Dynamics, &makeMultiband},
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
