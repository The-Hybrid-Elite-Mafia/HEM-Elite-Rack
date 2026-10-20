// What's on each hideout plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/Hideout.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- THE FIXER: what each band takes off, read out; where and when over how much
namespace fx3 {
using namespace fixer;
constexpr PlateKnob bandKnobs[] = {{Amount, "AMOUNT"}, {Freq, "FREQ"}};
constexpr PlateToggle bandToggles[] = {{Listen, "LISTEN"}, {Bypass, "BYPASS"}};
constexpr const char* columnNames[] = {"MUD", "HARSH", "FIZZ"};
constexpr PlateBands bands{.count = kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .toggles = bandToggles, .meter = 0,
                           .columnNames = columnNames};
constexpr PlateKnob row1[] = {{Input, "INPUT"}, {Output, "OUTPUT"}};
} // namespace fx3

// ---- SIX SHOOTER


// ---- DEAD DROP
namespace dd {
using namespace deaddrop;
constexpr PlateReadout readouts[] = {{0, "OUTPUT", "dBFS", "+"}};
constexpr PlateChoice choices[] = {{Sync, "SYNC"}};
constexpr PlateToggle toggles[] = {{Dual, "SPLIT TIMES"}, {PingPong, "PING PONG"}, {LoFi, "LO-RES"}, {Tap, "TAP", true}};
constexpr PlateChoice steppers[] = {{PingStart, "START"}, {Analog, "VINTAGE"}};
constexpr PlateToggle leftToggles[] = {{PhaseL, "PHASE L"}, {PhaseR, "PHASE R"}};
constexpr PlateKnob row1[] = {{Time, "TIME"}, {Note, "NOTE"}, {Bpm, "BPM"}, {TimeR, "TIME R"}, {NoteR, "NOTE R"}, {Feedback, "FEEDBACK", "0", "200"}};
constexpr PlateKnob row2[] = {{HighPass, "HI-PASS", "OFF", "20K"}, {LowPass, "LO-PASS", "20", "OFF"}, {Link, "TIE FILTERS"}, {ModDepth, "MOD DEPTH"},
                              {ModRate, "MOD RATE"}, {Mix, "MIX"}, {Output, "OUTPUT"}};
} // namespace dd

// ---- BACK ALLEY
namespace ba {
using namespace backalley;
constexpr PlateChoice choices[] = {{Sync, "SYNC"}, {Type, "REVERB"}};
constexpr PlateToggle toggles[] = {{EchoOn, "ECHO"}, {ReverbOn, "REVERB"}, {PingPong, "PING PONG"}};
constexpr PlateKnob row1[] = {{Time, "TIME"}, {Note, "NOTE"}, {Bpm, "BPM"}, {Feedback, "FEEDBACK"}, {EchoLevel, "ECHO LEVEL", "OFF", "0"},
                              {Length, "LENGTH"}, {ReverbLevel, "REVERB LEVEL", "OFF", "0"}};
constexpr PlateKnob row2[] = {{HighPass, "HI-PASS"}, {LowPass, "LO-PASS"}, {Mono, "MONO", "0", "10"}, {Mix, "MIX", "DRY", "WET"}};
} // namespace ba

// ---- DRIVE-BY
namespace db {
using namespace driveby;
constexpr PlateKnob bandKnobs[] = {{0, "AMOUNT"}};
constexpr PlateToggle bandToggles[] = {{1, "ON"}};
constexpr const char* columnNames[] = {"GAIN", "PAN", "PITCH", "AIR LOSS", "REVERB"};
constexpr PlateBands bands{.count = 5, .first = Gain, .stride = 2, .knobs = bandKnobs, .toggles = bandToggles, .columnNames = columnNames};
constexpr PlateKnob row1[] = {{TrackTime, "PASS TIME"}, {CenterTime, "CLOSEST AT"}, {Start, "START", "L", "R"}, {End, "END", "L", "R"},
                              {Close, "CLOSE", "FAR", "NEAR"}, {ReverbTime, "REV TIME"}, {Brightness, "BRIGHT", "0", "1"}};
constexpr PlateToggle toggles[] = {{Fire, "FIRE", true}};
constexpr PlateChoice steppers[] = {{Direction, "WAY"}, {Mode, "MODE"}, {Trigger, "TRIGGER"}};
} // namespace db



// ---- HUSTLE
namespace hu {
using namespace hustle;
constexpr PlateChoice choices[] = {{Sync, "SYNC"}};
constexpr PlateChoice steppers[] = {{P::Wave, "WAVE"}};
constexpr PlateToggle toggles[] = {{AmOn, "LEVEL"}, {FmOn, "PITCH"}, {RotOn, "SWING"}};
constexpr PlateKnob row1[] = {{Rate, "RATE"}, {Factor, "MULTIPLIER", "x0.25", "x100"}, {AmDepth, "LEVEL DEPTH"}, {AmPhase, "LEVEL PHASE", "0", "270"},
                              {FmDepth, "PITCH DEPTH"}, {FmPhase, "PITCH PHASE", "0", "270"}};
constexpr PlateKnob row2[] = {{RotCenter, "SWING CENTER", "R", "L"}, {RotRange, "SWING RANGE"}, {P::Gain, "GAIN"}, {Mix, "MIX"}};
} // namespace hu

// ---- STRAPPED
namespace st {
using namespace strapped;
constexpr PlateKnob bandKnobs[] = {{0, "LEVEL"}};
constexpr PlateChoice bandSteppers[] = {{kFaders, "STYLE"}};
constexpr const char* columnNames[] = {"BASS", "TREBLE", "COMPRESS", "REVERB", "DELAY", "PITCH"};
constexpr PlateBands bands{.count = kFaders, .first = Fader0, .stride = 1, .knobs = bandKnobs, .steppers = bandSteppers,
                           .columnNames = columnNames};
constexpr PlateKnob row1[] = {{Sensitivity, "SENSITIVITY"}, {Balance, "BALANCE"}, {Output, "OUTPUT"}};
constexpr PlateToggle toggles[] = {{ReAmp, "RE-AMP"}};
constexpr PlateChoice steppers[] = {{Mode, "MODE"}};
} // namespace st

// ---- SAFE HOUSE
namespace sh {
using namespace safehouse;
constexpr PlateKnob row1[] = {{Amount, "ROOM", "0", "100", K::Big}, {Front, "SPEAKER ANGLE"}, {Rotate, "HEAD TURN"}, {Output, "OUTPUT"}};
constexpr PlateKnob row2[] = {{Head, "HEAD SIZE"}, {Arc, "EAR TO EAR"}};
constexpr PlateToggle toggles[] = {{MuteL, "MUTE L"}, {MuteR, "MUTE R"}};
} // namespace sh

} // namespace

const PlateSpec kFixer{.note1 = "FIXES WHAT'S NASTY", .note2 = "ONLY WHEN IT IS", .screen = Screen::None, .row1 = fx3::row1, .bands = &fx3::bands,
                       .reportsReduction = true,
                       .demo = [](hc::fx::Effect& e) {
                           using namespace fixer;
                           e.set(band(0, Amount), 60); e.set(band(1, Amount), 70); e.set(band(2, Amount), 75);
                       }};

const PlateSpec kDeadDrop{.note1 = "THE DROP COMES BACK", .note2 = "CLEAN OR DIRTY", .screen = Screen::Readouts, .readouts = dd::readouts,
                          .choices = dd::choices, .toggles = dd::toggles, .row1 = dd::row1, .row2 = dd::row2, .leftToggles = dd::leftToggles,
                          .steppers = dd::steppers,
                          .demo = [](hc::fx::Effect& e) { using namespace deaddrop; e.set(PingPong, 1); e.set(LowPass, 6000); e.set(ModDepth, 20); }};
const PlateSpec kBackAlley{.note1 = "WORN ECHO", .note2 = "SPRING OR PLATE", .screen = Screen::None, .choices = ba::choices, .toggles = ba::toggles,
                           .row1 = ba::row1, .row2 = ba::row2,
                           .demo = [](hc::fx::Effect& e) { using namespace backalley; e.set(PingPong, 1); e.set(LowPass, 6000); }};
const PlateSpec kDriveBy{.note1 = "HERE IT COMES", .note2 = "THERE IT GOES", .screen = Screen::None, .toggles = db::toggles, .row1 = db::row1,
                         .steppers = db::steppers, .bands = &db::bands,
                         .demo = [](hc::fx::Effect& e) { using namespace driveby; e.set(Close, 85); e.set(Pitch, 150); }};

const PlateSpec kHustle{.note1 = "LEVEL, PITCH, PLACE", .note2 = "ONE MOTION", .screen = Screen::None, .choices = hu::choices, .toggles = hu::toggles,
                        .row1 = hu::row1, .row2 = hu::row2, .steppers = hu::steppers,
                        .demo = [](hc::fx::Effect& e) { using namespace hustle; e.set(Rate, 2); e.set(FmDepth, 20); }};
const PlateSpec kStrapped{.note1 = "THE GUITAR", .note2 = "READY FOR THE MIX", .screen = Screen::None, .toggles = st::toggles,
                          .row1 = st::row1, .steppers = st::steppers, .bands = &st::bands, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) {
                              using namespace strapped;
                              e.set(Mode, 1); e.set(Fader0 + CompressF, 3); e.set(Fader0 + ReverbF, -2); e.set(Fader0 + DelayF, -4);
                              e.set(Fader0 + BassF, 2); e.set(Fader0 + PitchF, -3);
                          }};
const PlateSpec kSafeHouse{.note1 = "SPEAKERS IN", .note2 = "YOUR HEADPHONES", .screen = Screen::None, .toggles = sh::toggles, .row1 = sh::row1,
                           .row2 = sh::row2, .demo = [](hc::fx::Effect& e) { e.set(safehouse::Amount, 35); e.set(safehouse::Rotate, -20); }};

} // namespace hem::rack::plates
