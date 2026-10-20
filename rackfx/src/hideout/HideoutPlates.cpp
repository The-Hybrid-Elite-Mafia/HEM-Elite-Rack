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


// ---- BACK ALLEY
namespace ba {
using namespace backalley;
constexpr PlateChoice choices[] = {{Space, "SPACE"}};
constexpr PlateKnob row1[] = {{Reverb, "REVERB"}, {Decay, "DECAY"}, {Echo, "ECHO"}, {Time, "TIME"}, {Feedback, "FEEDBACK"}};
constexpr PlateKnob row2[] = {{Wobble, "WOBBLE", "", "", K::Small}, {Grit, "GRIT", "", "", K::Small}, {Narrow, "NARROW", "", "", K::Small},
                              {Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace ba

// ---- DRIVE-BY
namespace db {
using namespace driveby;
constexpr PlateChoice choices[] = {{Mode, "MODE"}, {Rate, "PASS EVERY"}};
constexpr PlateKnob row1[] = {{Position, "POSITION", "LEFT", "RIGHT", K::Big}, {Speed, "SPEED"}, {Distance, "DISTANCE"}, {Air, "AIR"}};
constexpr PlateKnob row2[] = {{Bpm, "BPM", "", "", K::Small}, {Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
constexpr PlateChoice left[] = {{SyncSource, "TEMPO FROM"}};
} // namespace db

// ---- BODY DOUBLE
namespace bd {
using namespace bodydouble;
constexpr PlateChoice choices[] = {{Voices, "VOICES"}};
constexpr PlateKnob row1[] = {{Delay, "DELAY"}, {Detune, "DETUNE"}, {Spread, "SPREAD", "0", "100", K::Big}};
constexpr PlateKnob row2[] = {{VoiceLevel, "VOICE LEVEL", "", "", K::Small}, {LowCut, "LOW CUT", "", "", K::Small}, {Dry, "DRY", "", "", K::Small},
                              {Output, "OUTPUT", "", "", K::Small}};
} // namespace bd

// ---- HUSTLE
namespace hu {
using namespace hustle;
constexpr PlateChoice choices[] = {{P::Wave, "WAVE"}, {Note, "NOTE"}};
constexpr PlateKnob row1[] = {{Rate, "RATE"}, {Tremolo, "TREMOLO"}, {Vibrato, "VIBRATO"}, {Pan, "PAN"}, {Offset, "OFFSET"}};
constexpr PlateKnob row2[] = {{Bpm, "BPM", "", "", K::Small}, {Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
constexpr PlateChoice left[] = {{SyncSource, "TEMPO FROM"}};
} // namespace hu

// ---- THE INFORMANT
namespace inf {
using namespace informant;
constexpr PlateChoice choices[] = {{Note, "NOTE"}, {Chord, "CHORD"}, {P::Wave, "CARRIER"}, {Bands, "BANDS"}};
constexpr PlateKnob row1[] = {{React, "REACT"}, {Shift, "SHIFT", "-12", "+12"}, {Sibilance, "SIBILANCE"}};
constexpr PlateKnob row2[] = {{Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
constexpr PlateChoice steppers[] = {{Octave, "OCTAVE"}};
} // namespace inf

// ---- STRAPPED
namespace st {
using namespace strapped;
constexpr PlateChoice choices[] = {{Note, "DELAY NOTE"}};
constexpr PlateKnob row1[] = {{Tight, "TIGHT"}, {Compress, "COMPRESS"}, {Bass, "BASS"}, {Mids, "MIDS"}, {Treble, "TREBLE"}};
constexpr PlateKnob row2[] = {{Chorus, "CHORUS"}, {Delay, "DELAY"}, {Room, "ROOM"}, {Width, "WIDTH"}, {Output, "OUTPUT"}};
} // namespace st

// ---- SAFE HOUSE
namespace sh {
using namespace safehouse;
constexpr PlateChoice choices[] = {{Room, "ROOM"}};
constexpr PlateKnob row1[] = {{Angle, "ANGLE"}, {Crossfeed, "CROSSFEED", "0", "100", K::Big}, {RoomLevel, "ROOM LEVEL"}, {Bass, "BASS"},
                              {Output, "OUTPUT", "", "", K::Small}};
} // namespace sh

} // namespace

const PlateSpec kFixer{.note1 = "FIXES WHAT'S NASTY", .note2 = "ONLY WHEN IT IS", .screen = Screen::None, .row1 = fx3::row1, .bands = &fx3::bands,
                       .reportsReduction = true,
                       .demo = [](hc::fx::Effect& e) {
                           using namespace fixer;
                           e.set(band(0, Amount), 60); e.set(band(1, Amount), 70); e.set(band(2, Amount), 75);
                       }};

const PlateSpec kBackAlley{.note1 = "WORN OUT ROOM", .note2 = "WORN OUT TAPE", .screen = Screen::None, .choices = ba::choices,
                           .row1 = ba::row1, .row2 = ba::row2,
                           .demo = [](hc::fx::Effect& e) { using namespace backalley; e.set(Grit, 60); e.set(Wobble, 50); }};
const PlateSpec kDriveBy{.note1 = "HERE IT COMES", .note2 = "THERE IT GOES", .screen = Screen::None, .choices = db::choices,
                         .row1 = db::row1, .row2 = db::row2, .left = db::left,
                         .demo = [](hc::fx::Effect& e) { using namespace driveby; e.set(Mode, 0); e.set(Position, -35); }};
const PlateSpec kBodyDouble{.note1 = "TWO OF YOU", .note2 = "OR FOUR", .screen = Screen::None, .choices = bd::choices,
                            .row1 = bd::row1, .row2 = bd::row2,
                            .demo = [](hc::fx::Effect& e) { using namespace bodydouble; e.set(Voices, 1); e.set(Detune, 12); }};
const PlateSpec kHustle{.note1 = "LEVEL, PITCH, PAN", .note2 = "ONE MOTION", .screen = Screen::None, .choices = hu::choices,
                        .row1 = hu::row1, .row2 = hu::row2, .left = hu::left,
                        .demo = [](hc::fx::Effect& e) { using namespace hustle; e.set(Tremolo, 60); e.set(Pan, 50); e.set(Vibrato, 20); }};
const PlateSpec kInformant{.note1 = "THE WORDS TALK", .note2 = "THE SYNTH SINGS", .screen = Screen::None, .choices = inf::choices,
                           .row1 = inf::row1, .row2 = inf::row2, .steppers = inf::steppers,
                           .demo = [](hc::fx::Effect& e) { using namespace informant; e.set(Note, 9); e.set(Chord, 3); }};
const PlateSpec kStrapped{.note1 = "THE GUITAR", .note2 = "READY FOR THE MIX", .screen = Screen::None, .choices = st::choices,
                          .row1 = st::row1, .row2 = st::row2,
                          .demo = [](hc::fx::Effect& e) { using namespace strapped; e.set(Compress, 50); e.set(Chorus, 30); e.set(Delay, 25); e.set(Room, 30); }};
const PlateSpec kSafeHouse{.note1 = "SPEAKERS IN", .note2 = "YOUR HEADPHONES", .screen = Screen::None, .choices = sh::choices, .row1 = sh::row1,
                           .demo = [](hc::fx::Effect& e) { e.set(safehouse::Room, 2); }};

} // namespace hem::rack::plates
