#pragma once
// The settings these tests (and the QA report's extra context) start from: each plug-in's defaults from
// before Steven's zero-colour rule. The plug-ins now LOAD with every colour control at zero and pass the
// sound untouched; the quality checks still exercise the DSP from this coloured starting point.
// Generated from the measured old / new defaults (72 plug-ins, 132 controls).
#include <string>
#include <utility>
#include <vector>

namespace colourstart {

struct Entry {
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<Entry>& table() {
    static const std::vector<Entry> t = {
        {"ARCHIVE", {{"bump", 50.0f}}},
        {"BACK ALLEY", {{"mix", 50.0f}}},
        {"BACKROOM", {{"mix", 20.0f}}},
        {"BIG HOUSE", {{"mix", 50.0f}}},
        {"BIG SHOT", {{"ceiling", -0.3f}, {"dither", 0.0f}}},
        {"BODY DOUBLE", {{"v1on", 1.0f}, {"v2on", 1.0f}, {"v3on", 1.0f}, {"v4on", 1.0f}}},
        {"BOTTOM FEEDER", {{"drivegain", 5.0f}, {"drivemix", 5.0f}, {"gain", 5.0f}}},
        {"CATHEDRAL", {{"mix", 25.0f}}},
        {"CHAUFFEUR", {{"rangemin", -6.0f}, {"rangemax", 6.0f}}},
        {"CHROME", {{"mix", 25.0f}}},
        {"COIL", {{"drive", 30.0f}, {"mix", 30.0f}}},
        {"COLD CASE", {{"depth", 80.0f}, {"mix", 100.0f}}},
        {"CONSIGLIERE", {{"mix", 100.0f}}},
        {"CRATE DIGGER", {{"wear", 30.0f}, {"warp", 20.0f}, {"monolows", 1.0f}}},
        {"CUTTHROAT", {{"lhpon", 1.0f}, {"llpon", 1.0f}, {"rhpon", 1.0f}, {"rlpon", 1.0f}}},
        {"CYCLONE", {{"drive", 20.0f}, {"mix", 100.0f}}},
        {"DEAD DROP", {{"mix", 100.0f}}},
        {"DEEP POCKETS", {{"harmonics", 0.0f}, {"highpass", 1.0f}}},
        {"DOUBLE CROSS", {{"spread", 1.0f}}},
        {"DRIVE-BY", {{"gainon", 1.0f}, {"panon", 1.0f}, {"pitchon", 1.0f}, {"airon", 1.0f}, {"reverbon", 1.0f}}},
        {"ENFORCER", {{"mix", 100.0f}}},
        {"FAST MONEY", {{"dither", 0.0f}}},
        {"FINGERPRINT", {{"mix", 15.0f}}},
        {"FURNACE", {{"drive", 40.0f}, {"mix", 100.0f}}},
        {"GETAWAY", {{"depth", 60.0f}, {"mix", 50.0f}}},
        {"GRAND LARCENY", {{"pedalnoise", 35.0f}, {"tremdepth", 60.0f}, {"pandepth", 33.0f}, {"phmix", 25.0f}, {"phdepth", 53.5f}, {"chdepth", 25.0f}, {"rvmix", 30.0f}}},
        {"HEAVYWEIGHT", {{"mix", 100.0f}}},
        {"HIGH ROLLER", {{"punch", 3.0f}}},
        {"HIGH STAKES", {{"dither", 0.0f}}},
        {"HITMAN", {{"mix", 100.0f}}},
        {"HOT WIRE", {{"gain", 5.0f}, {"depth", 3.0f}}},
        {"HUSTLE", {{"amdepth", 50.0f}, {"fmdepth", 50.0f}, {"mix", 100.0f}}},
        {"IRON FIST", {{"mix", 100.0f}}},
        {"KNOCKOUT", {{"mix", 100.0f}}},
        {"LAST WORD", {{"dither", 0.0f}}},
        {"LOCKDOWN", {{"ceiling", -1.0f}}},
        {"LONE WOLF", {{"compress", 1.0f}}},
        {"LOW PROFILE", {{"intensity", 0.0f}}},
        {"LOW RIDER", {{"lowair", -6.0f}}},
        {"MADE MAN", {{"ratio", 100.0f}}},
        {"MIXTAPE", {{"drive", 35.0f}, {"wow", 20.0f}, {"flutter", 20.0f}}},
        {"OLD MONEY", {{"mix", 100.0f}}},
        {"PAWN SHOP", {{"devicemix", 100.0f}, {"spacemix", 30.0f}, {"wowa", 30.0f}}},
        {"QUICK CASH", {{"dither", 0.0f}}},
        {"QUICK DRAW", {{"mix", 100.0f}}},
        {"REEL MONEY", {{"wobble", 50.0f}}},
        {"REPEAT OFFENDER", {{"mix", 30.0f}}},
        {"SAFE HOUSE", {{"amount", 17.0f}}},
        {"SHAKEDOWN", {{"depth", 50.0f}}},
        {"SHAPESHIFTER", {{"mix", 100.0f}}},
        {"SHREDDER", {{"mix", 100.0f}}},
        {"SILENCER", {{"threshold", -30.0f}}},
        {"SIX SHOOTER", {{"tap1on", 1.0f}, {"tap2on", 1.0f}, {"tap3on", 1.0f}, {"tap4on", 1.0f}, {"tap5on", 1.0f}, {"tap6on", 1.0f}}},
        {"SLOW BURN", {{"compress", 1.0f}}},
        {"SMOKESCREEN", {{"depth", 70.0f}, {"mix", 50.0f}}},
        {"SPARK", {{"drive", 50.0f}, {"mix", 25.0f}}},
        {"SPEAKEASY", {{"early", 1.0f}, {"reverb", 1.0f}}},
        {"STONE COLD", {{"mix", 100.0f}}},
        {"SWEET SIXTEEN", {{"dither", 0.0f}}},
        {"SWERVE", {{"depth", 100.0f}, {"mix", 50.0f}}},
        {"THE CLEANER", {{"reduction", 5.0f}}},
        {"THE CREW", {{"depth", 40.0f}, {"mix", 50.0f}}},
        {"THE DON", {{"ratio", 2.0f}}},
        {"THE FAMILY", {{"mix", 100.0f}}},
        {"THE INFORMANT", {{"modulatorlevel", -60.0f}, {"noiselevel", -14.0f}, {"morphlevel", 0.0f}}},
        {"THE RACKET", {{"pedal", 0.0f}, {"oddrive", 50.0f}, {"dsdrive", 50.0f}, {"rpdrive", 50.0f}, {"hvdrive", 50.0f}, {"fldepth", 50.0f}, {"thmix", 50.0f}, {"prmix", 50.0f}, {"brmix", 50.0f}, {"ecmix", 50.0f}, {"femix", 50.0f}, {"rmmix", 50.0f}, {"spmix", 50.0f}}},
        {"THE SETUP", {{"mix", 100.0f}}},
        {"TIGHT LIPS", {{"range", -16.0f}}},
        {"TOP DOG", {{"dither", 0.0f}}},
        {"TORCH", {{"drive", 5.0f}}},
        {"UNDERTOW", {{"mix", 50.0f}}},
        {"WHEELMAN", {{"rangemin", -6.0f}, {"rangemax", 6.0f}}},
    };
    return t;
}

// The old defaults for the plug-in called `name` (empty when nothing changed).
inline const std::vector<std::pair<const char*, float>>& forName(const std::string& name) {
    static const std::vector<std::pair<const char*, float>> none;
    for (const auto& e : table()) if (name == e.name) return e.values;
    return none;
}

} // namespace colourstart
