#include "hem/rack/Plugins.hpp"

#include "hem/rack/BusGlue.hpp"
#include "hem/rack/Tape.hpp"
#include "hem/rack/TubeEq.hpp"
#include "hem/rack/EliteEq.hpp"
#include "hem/rack/Limiter.hpp"
#include "hem/rack/Restorer.hpp"
#include "hem/rack/Tools.hpp"
#include "hem/rack/MoreDynamics.hpp"
#include "hem/rack/Hitman.hpp"
#include "hem/rack/ColorFx.hpp"
#include "hem/rack/Space.hpp"
#include "hem/rack/Movement.hpp"
#include "hem/rack/Specialty.hpp"
#include "hem/rack/Arsenal.hpp"
#include "hem/rack/ToneShop.hpp"
#include "arsenal/Plates.hpp"

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
    {"HITMAN", "DUAL FET PEAK COMPRESSOR", "EAT THE PEAKS", "Fx|Dynamics",
     "Dual FET compressor: L/R or M/S, lookahead, sidechain EQ, saturation, mix",
     {0x8B792152u, 0x8154454Du, 0xAF70E40Au, 0x892CECEDu}, {0xEE8B2C74u, 0x8D894CE5u, 0xAA3D510Fu, 0x23EB1DB9u},
     Face::Hitman, Group::Dynamics, &makeHitman, &kHitmanScCurve},
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
    // ---- the dynamics arsenal
    {"SWITCHBLADE", "CHANNEL DYNAMICS", "CUT WHAT MATTERS", "Fx|Dynamics",
     "Compressor and gate in one, with a filter that steers them or works on one band",
     {0xA2AFE143u, 0xF92038FEu, 0x84AB6549u, 0x6F87C249u}, {0x686C254Eu, 0xAABA5403u, 0xFB6DFBCDu, 0x2B158D02u},
     Face::Spec, Group::Dynamics, &makeSwitchblade, nullptr, &plates::kSwitchblade},
    {"TERRITORY", "MULTIBAND DYNAMICS", "RULE WHAT MATTERS", "Fx|Dynamics",
     "Four or five bands, each turned down or lifted up on its own; natural or linear phase",
     {0x1ADB3E58u, 0x8A23C51Eu, 0xC2FC9866u, 0x028959C7u}, {0xB008CCE0u, 0xCA3BB4A9u, 0x88A04B62u, 0xDB00F461u},
     Face::Territory, Group::Dynamics, &makeTerritory},
    {"CONSIGLIERE", "PROGRAM LEVELER", "CALM WHAT MATTERS", "Fx|Dynamics",
     "Two-knob leveler: smooth light-cell or firmer solid-state, compress or limit",
     {0x7A1A5583u, 0x8BCC61DFu, 0x97172ED3u, 0xEA376FECu}, {0x82A5F3C9u, 0x9E591404u, 0x7A564F50u, 0x34C7A1E3u},
     Face::Spec, Group::Dynamics, &makeConsigliere, nullptr, &plates::kConsigliere},
    {"HEAVYWEIGHT", "PUNCH COMPRESSOR", "HIT WHAT MATTERS", "Fx|Dynamics",
     "Holds the body, lets the hits through, adds analog weight; release on the beat",
     {0xD06EAE2Fu, 0x76208618u, 0xA120550Eu, 0xFC20F560u}, {0x930C184Eu, 0xDBFCFE4Eu, 0xAB988AEDu, 0x63CD64DEu},
     Face::Spec, Group::Dynamics, &makeHeavyweight, nullptr, &plates::kHeavyweight},
    {"THE DON", "BROADCAST BUS COMPRESSOR", "RUN WHAT MATTERS", "Fx|Dynamics",
     "Old-school bus compressor that listens to its own output and breathes with the mix",
     {0x6DC2200Fu, 0x0A7B5431u, 0xD796D721u, 0xBE40AAC7u}, {0xD9BC1507u, 0xA8280283u, 0xF95DE33Cu, 0xE85EB99Cu},
     Face::Spec, Group::Dynamics, &makeTheDon, nullptr, &plates::kTheDon},
    {"SLOW BURN", "TUBE LIMITER", "WARM WHAT MATTERS", "Fx|Dynamics",
     "Variable-mu tube limiting: six time settings, linked, dual or mid / side",
     {0xAB4F6116u, 0x4863F4C0u, 0x4F90AFA3u, 0x5F566660u}, {0xAB5188C5u, 0x24D8D9CCu, 0x794F011Bu, 0x63276F98u},
     Face::Spec, Group::Dynamics, &makeSlowBurn, nullptr, &plates::kSlowBurn},
    {"IRON FIST", "CONSOLE CHANNEL COMPRESSOR", "GRIP WHAT MATTERS", "Fx|Dynamics",
     "Drive it in like a console channel: compressor, limiter, de-ess and output iron",
     {0xDD7E714Eu, 0x9EFC6925u, 0xEE24DB5Eu, 0x580FFD36u}, {0x3C6074F9u, 0x419ABE22u, 0x277931F2u, 0x75CA1619u},
     Face::Spec, Group::Dynamics, &makeIronFist, nullptr, &plates::kIronFist},
    {"KNOCKOUT", "TRANSIENT DESIGNER", "SNAP WHAT MATTERS", "Fx|Dynamics",
     "More or less attack and sustain, on the whole sound or in four bands",
     {0xF8AF5AC3u, 0xFECEF841u, 0xAAD93754u, 0xE8F1DBB0u}, {0xAEA03172u, 0x18552D48u, 0x9FEBF2CBu, 0x578BFF38u},
     Face::Spec, Group::Dynamics, &makeKnockout, nullptr, &plates::kKnockout},
    {"FULL HOUSE", "LOUDNESS LEVELER", "FILL WHAT MATTERS", "Fx|Dynamics",
     "Lifts the quiet parts, rides the average, holds the loud parts, gates the noise",
     {0x9910D819u, 0xDFC77DEFu, 0x1D3A5808u, 0xDB4DBDF9u}, {0x5871970Fu, 0x49D41475u, 0xB6EF7271u, 0x9427D4A7u},
     Face::Spec, Group::Dynamics, &makeFullHouse, nullptr, &plates::kFullHouse},
    {"WHEELMAN", "AUTO LEVEL RIDER", "RIDE WHAT MATTERS", "Fx|Dynamics",
     "Rides a vocal or a bass to a steady level, the way you'd ride the fader",
     {0xDE589DA5u, 0xF1B5F848u, 0x46FF2D5Du, 0x732800F7u}, {0x2858B369u, 0x1830C26Cu, 0xD9E605ACu, 0x8A895AD2u},
     Face::Spec, Group::Dynamics, &makeWheelman, nullptr, &plates::kWheelman},
    {"BIG SHOT", "LOUDNESS MAXIMIZER", "PUSH WHAT MATTERS", "Fx|Dynamics",
     "Maximizer: one band, or the limiting shared across 5 or 16 bands; dither built in",
     {0xDDC1BE6Eu, 0xB1994C0Fu, 0x13D29BF1u, 0x9C5F265Du}, {0x5EF03878u, 0x22156A12u, 0x733FBA70u, 0x9984EA90u},
     Face::Spec, Group::Dynamics, &makeBigShot, nullptr, &plates::kBigShot},
    {"SILENCER", "DE-ESSER", "QUIET WHAT MATTERS", "Fx|Dynamics",
     "Takes the bite off esses: classic filter or smart detection, split or wide",
     {0x98682FA8u, 0x57ABADF0u, 0xD02C2107u, 0x1E614A11u}, {0x3379EB16u, 0x35FF9D53u, 0xA7890626u, 0x6DCD032Cu},
     Face::Spec, Group::Dynamics, &makeSilencer, nullptr, &plates::kSilencer},
    {"BREATHLESS", "BREATH REMOVER", "CLEAR WHAT MATTERS", "Fx|Restoration",
     "Finds the breaths between the words and turns them down, with smooth fades",
     {0x3A2FCA20u, 0x9A141E38u, 0xE3BDB0CBu, 0x0F598244u}, {0xE88EE078u, 0x08694F12u, 0x17E97E5Bu, 0xAB95AC29u},
     Face::Spec, Group::Tools, &makeBreathless, nullptr, &plates::kBreathless},
    {"TOMMY GUN", "TEMPO PUMP", "PUMP WHAT MATTERS", "Fx|Dynamics",
     "One knob: the track ducks on the beat and pumps back up, no sidechain needed",
     {0xCDCF4E33u, 0x94F34F16u, 0xF950E8EBu, 0x2E312C24u}, {0xBF9A653Du, 0xDAF342ABu, 0x0A88515Fu, 0x49EC0F1Du},
     Face::Spec, Group::Dynamics, &makeTommyGun, nullptr, &plates::kTommyGun},
    {"STRAIGHT SHOOTER", "LINEAR PHASE EQUALIZER", "SHAPE WHAT MATTERS", "Fx|EQ",
     "Linear-phase EQ: cuts, shelves and three bells with no phase shift at all",
     {0x53BC2FE2u, 0x4685448Eu, 0x9E2098A5u, 0x4C0ADF14u}, {0x11356263u, 0x80C449F4u, 0xBA7022BBu, 0xC0F2A2BEu},
     Face::Spec, Group::Eq, &makeStraightShooter, &straight::kCurve, &plates::kStraightShooter},
    {"CUTTHROAT", "SWEEP FILTER", "CUT WHAT MATTERS", "Fx|Filter",
     "High-pass and low-pass with a one-knob sweep, resonance and drive",
     {0x4F17BD9Au, 0x093D40BFu, 0xA5F44ED5u, 0x68244674u}, {0x9E198314u, 0xB10E44E9u, 0xA5C8F43Cu, 0x0AFBFD85u},
     Face::Spec, Group::Eq, &makeCutthroat, &cutthroat::kCurve, &plates::kCutthroat},
    {"THE BOSS", "CHANNEL STRIP", "RUN WHAT MATTERS", "Fx|Dynamics",
     "Gate, compressor, four-band EQ, low cut and limiter in one channel",
     {0x82D3954Bu, 0x486C4D59u, 0x93C55EDDu, 0xCC9D71C9u}, {0x146B2456u, 0xF3A44803u, 0xA5C4439Bu, 0x6E01E0E6u},
     Face::Spec, Group::Tools, &makeTheBoss, nullptr, &plates::kTheBoss},
    {"KINGPIN", "DRUM CHANNEL", "HIT WHAT MATTERS", "Fx|Dynamics",
     "Drum channel: gate, thump, snap, punch, crush and drive",
     {0xC1C0D57Eu, 0x097E4E12u, 0xAD48C393u, 0x522671CBu}, {0x0AE9BC6Du, 0xEEB94CC5u, 0x80C29BC7u, 0x0CF175B7u},
     Face::Spec, Group::Tools, &makeKingpin, nullptr, &plates::kKingpin},
    {"GOLD CHAIN", "ONE-KNOB TONE", "ENRICH WHAT MATTERS", "Fx|Distortion",
     "One knob: warmth, density and sheen that grow together",
     {0x9993EEF7u, 0x90B14614u, 0x9929D6A7u, 0x151A5BA2u}, {0x18D31CC3u, 0x0CBB4891u, 0xABEEA077u, 0x9358B38Au},
     Face::Spec, Group::Color, &makeGoldChain, nullptr, &plates::kGoldChain},
    {"IVORIES", "PIANO TONE", "PLAY WHAT MATTERS", "Fx|EQ",
     "Piano tone: brighter or darker, body, compression, slap delay, doubler, width",
     {0x384AA925u, 0xD76A4658u, 0x9B105B78u, 0xB8AA0F11u}, {0x91C13BB0u, 0x2B3E48BEu, 0xAC3EED43u, 0x2A5AE083u},
     Face::Spec, Group::Color, &makeIvories, nullptr, &plates::kIvories},
    {"HIGH ROLLER", "FIVE-BAND ENHANCER", "LIFT WHAT MATTERS", "Fx|Mastering",
     "Five bands, each with its own lift and width: detail comes up, the mix opens",
     {0x699235E6u, 0xE598408Au, 0xA455C565u, 0xCBDD727Bu}, {0x35DFC976u, 0x317A4BE8u, 0xAE464337u, 0x61FE8DE8u},
     Face::Spec, Group::Color, &makeHighRoller, nullptr, &plates::kHighRoller},
    {"THE MIDDLEMAN", "CENTER / SIDES BALANCE", "BALANCE WHAT MATTERS", "Fx|Spatial",
     "Turn the middle or the sides up or down, lows and highs apart, hits steered",
     {0x7EA39FCCu, 0x23234CB9u, 0x8C189009u, 0x52F521A2u}, {0x482683CAu, 0xD7724738u, 0xA106E319u, 0xD9882F04u},
     Face::Spec, Group::Tools, &makeMiddleman, nullptr, &plates::kMiddleman},
    {"DOUBLE CROSS", "MONO TO STEREO", "SPREAD WHAT MATTERS", "Fx|Spatial",
     "Mono to stereo that folds back to mono perfectly",
     {0xDAF83147u, 0x0C4942B4u, 0xB92FD178u, 0x7850B2DDu}, {0x002710CFu, 0x88344A0Fu, 0x8173B704u, 0x11760CEAu},
     Face::Spec, Group::Tools, &makeDoubleCross, nullptr, &plates::kDoubleCross},
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
