#include "hem/rack/Staging.hpp"

#include <cstring>

namespace hem::staging {
namespace {
const StagingInfo kList[] = {
    {1, "DYNAMIC EQ 8", "EIGHT BAND DYNAMIC EQ", "Fx|EQ", &makeDynamicEq8},
    {2, "V.P.R.A.", "VOCAL PRESENCE & RESONANCE ARCHITECT", "Fx|EQ", &makeVpra},
    {3, "SUB-BASS & 808 ARCHITECT", "SUB AND 808 BUILDER", "Fx|Generator", &makeSubArchitect},
    {4, "VOICECIRCUIT", "VOCAL RESYNTHESIS ENGINE", "Fx|Pitch Shift", &makeVoiceCircuit},
    {5, "NOTEFORGE", "VOCAL PITCH CORRECTION", "Fx|Pitch Shift", &makeNoteForge},
    {7, "THROATSHIFT", "PITCH & FORMANT EFFECTS", "Fx|Pitch Shift", &makeThroatShift},
    {8, "VOICEVEIL", "VOCAL NOISE REDUCTION", "Fx|Restoration", &makeVoiceVeil},
    {9, "LEVELPILOT", "AUTOMATIC VOCAL LEVELING", "Fx|Dynamics", &makeLevelPilot},
    {10, "S.I.P.A.", "STEREO IMAGE & PHASE ARCHITECT", "Fx|Spatial", &makeSipa},
    {11, "LEADSUITE", "INTEGRATED VOCAL PROCESSOR", "Fx", &makeLeadSuite},
    {12, "CHOIRFORGE", "EIGHT VOICE HARMONY ENGINE", "Fx|Pitch Shift", &makeChoirForge},
    {13, "VOICEPRESS", "VOCAL DYNAMICS", "Fx|Dynamics", &makeVoicePress},
    {14, "RESONANCEGUARD", "ADAPTIVE RESONANCE CONTROL", "Fx|EQ", &makeResonanceGuard},
    {16, "BANDMOTION", "SIX BAND DYNAMIC EQ", "Fx|EQ", &makeBandMotion},
    {17, "GRAPHICFORGE", "TEN BAND GRAPHIC EQUALIZER", "Fx|EQ", &makeGraphicForge},
    {18, "SILKCONTOUR", "BROADBAND & MIDRANGE TONE", "Fx|EQ", &makeSilkContour},
    {19, "GLOWLEVEL", "OPTICAL-STYLE LEVELING", "Fx|Dynamics", &makeGlowLevel},
    {20, "STRIKECLAMP", "FAST CHARACTER COMPRESSION", "Fx|Dynamics", &makeStrikeClamp},
    {21, "BUSFOUNDRY", "FLEXIBLE BUS DYNAMICS", "Fx|Dynamics", &makeBusFoundry},
    {22, "BANDCOUNCIL", "SIX-BAND DYNAMICS", "Fx|Dynamics", &makeBandCouncil},
    {23, "IMPACTGUIDE", "SPECTRAL COMPRESSOR", "Fx|Dynamics", &makeImpactGuide},
    {24, "CHAMBERPLATE", "CHAMBER, TAPE & PLATE AMBIENCE", "Fx|Reverb", &makeChamberPlate},
    {25, "ECHOFORGE", "MODULATED CHARACTER DELAY", "Fx|Delay", &makeEchoForge},
    {26, "MOTIONFIELD", "MORPHING CHORUS-DELAY-REVERB", "Fx|Modulation", &makeMotionField},
    {27, "DEPTHMATRIX", "ROUTABLE DELAY & REVERB MATRIX", "Fx|Delay", &makeDepthMatrix},
    {28, "CONSOLECORE", "CONSOLE CHANNEL PROCESSING", "Fx|Dynamics", &makeConsoleCore},
    {29, "CHANNELMATRIX", "MODULAR CHANNEL PROCESSOR", "Fx|Dynamics", &makeChannelMatrix},
    {31, "EMBERSTRIP", "SATURATING CHANNEL PROCESSOR", "Fx|Distortion", &makeEmberStrip},
    {32, "MIXBOND", "STEREO BUS COMPRESSION", "Fx|Dynamics", &makeMixBond},
    {33, "REELAURA", "TAPE COLOR & DELAY", "Fx|Distortion", &makeReelAura},
    {34, "PEAKCITADEL", "BROADBAND & MULTIBAND LIMITING", "Fx|Dynamics", &makePeakCitadel},
    {35, "IMPACTCROWN", "ENHANCEMENT & LOUDNESS", "Fx|Mastering", &makeImpactCrown},
    {36, "MASTERCOUNCIL", "MODULAR MASTERING CHAIN", "Fx|Mastering", &makeMasterCouncil},
    {37, "HITCONTOUR", "TRANSIENT SHAPING", "Fx|Dynamics", &makeHitContour},
    {38, "DRUMKEY", "PERCUSSION RETUNING", "Fx|Modulation", &makeDrumKey},
    {39, "AIREMBER", "AIR & EMBER EXCITER", "Fx|EQ", &makeAirEmber},
    {40, "BASSBEACON", "BASS HARMONICS & MONO", "Fx|EQ", &makeBassBeacon},
    {41, "ROOMERASER", "ROOM & TAIL REMOVER", "Fx|Restoration", &makeRoomEraser},
    {42, "MONITORSPHERE", "TRANSLATION MONITOR", "Fx|Spatial", &makeMonitorSphere},
    {43, "DE-ESSER", "SIBILANCE CONTROL", "Fx|Dynamics", &makeDeEsser},
    {44, "MASTER LIMITER", "TRUE-PEAK MASTER LIMITER", "Fx|Dynamics", &makeMasterLimiter},
    {45, "OPTICAL COMPRESSOR", "LIGHT-CELL COMPRESSION", "Fx|Dynamics", &makeOpticalComp},
    {46, "PARAMETRIC EQ", "FIVE-BAND PRECISION EQ", "Fx|EQ", &makeParametricEq},
    {47, "REVERB", "FIVE-ALGORITHM REVERB", "Fx|Reverb", &makeHemReverb},
    {48, "STEREO DELAY", "CLEAN STEREO DELAY", "Fx|Delay", &makeStereoDelay},
    {51, "TUBE PREAMP", "SELECTABLE TUBE STAGE", "Fx|Distortion", &makeTubePreamp},
    {52, "VCA COMPRESSOR", "CLEAN VCA COMPRESSION", "Fx|Dynamics", &makeVcaComp},
};
} // namespace

std::span<const StagingInfo> plugins() noexcept { return kList; }

const StagingInfo* find(const char* name) noexcept {
    for (const auto& p : kList) if (name && std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

} // namespace hem::staging
