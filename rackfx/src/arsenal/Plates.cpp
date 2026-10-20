// What's on each arsenal plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "Plates.hpp"

#include "hem/rack/Arsenal.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- KNOCKOUT: no screen; the shapes and guard across the top, the hits and tails on one row
namespace ko {
using namespace knockout;
constexpr PlateChoice choices[] = {{AttackShape, "ATTACK SHAPE"}, {SustainShape, "SUSTAIN SHAPE"}, {Guard, "GUARD"}};
constexpr PlateKnob row1[] = {{Attack, "ATTACK", "-", "+"}, {AttackSens, "SENSITIVITY"}, {AttackTime, "LENGTH"},
                              {Sustain, "SUSTAIN", "-", "+"}, {SustainSens, "SENSITIVITY"}, {SustainTime, "LENGTH"}};
constexpr PlateKnob row2[] = {{Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace ko

// ---- HAYMAKER
namespace hm {
using namespace haymaker;
constexpr PlateReadout readouts[] = {{0, "GAIN", "dB", "+"}, {1, "OUTPUT PEAK", "dBFS"}};
constexpr PlateKnob row1[] = {{Range, "RANGE", "-24", "+18", K::Big}, {Sens, "SENS", "-10", "+10"}, {Duration, "DURATION"}, {Release, "RELEASE"},
                              {Output, "OUTPUT"}};
constexpr PlateToggle leftT[] = {{Trim, "TRIM", true}};
} // namespace hm

// ---- FOUR KINGS: four band columns and the MASTER column
namespace fk {
using namespace fourkings;
constexpr PlateKnob bandKnobs[] = {{Range, "RANGE"}, {Sens, "SENS"}};
constexpr PlateToggle bandToggles[] = {{On, "ON"}};
constexpr PlateBands bands{.count = kBands + 1, .first = 0, .stride = kPerBand, .knobs = bandKnobs, .toggles = bandToggles, .meter = 0,
                           .lastIsMaster = true};
constexpr PlateKnob row1[] = {{Duration, "DURATION"}, {Release, "RELEASE"}, {Output, "OUTPUT"}};
constexpr PlateToggle toggles[] = {{Trim, "TRIM", true}};
} // namespace fk

// ---- BACKBONE
namespace bb {
using namespace backbone;
constexpr PlateReadout readouts[] = {{0, "CUT", "dB", "+"}, {1, "BOOST", "dB", "+"}, {2, "OUTPUT PEAK", "dBFS"}};
constexpr PlateKnob row1[] = {{LowLevel, "LOW LEVEL", "0", "48", K::Big}, {HighLevel, "HIGH LEVEL", "0", "-48", K::Big}, {Output, "OUTPUT", "0", "-48"}};
} // namespace bb

// ---- FULL HOUSE
namespace fh {
using namespace fullhouse;
constexpr PlateReadout readouts[] = {{0, "ENERGY", "dB"}, {1, "LOW GAIN", "dB", "+"}, {2, "HIGH", "dB", "+"}, {3, "OUTPUT", "dBFS"}};
constexpr PlateChoice choices[] = {{LevelerMode, "LEVELER RELEASE"}};
constexpr PlateToggle toggles[] = {{GateOn, "GATE ON"}, {LowOn, "LOW ON"}, {LevelerOn, "LEVELER ON"}, {HighOn, "HIGH ON"}};
constexpr PlateKnob row1[] = {{Gate, "GATE", "-96", "0"}, {LowThreshold, "LOW THRESHOLD"}, {LowGain, "LOW GAIN", "0", "48"},
                              {Leveler, "LEVELER", "-48", "0"}, {HighThreshold, "HIGH THRESHOLD"}, {HighGain, "HIGH GAIN", "-48", "0"}};
} // namespace fh

// ---- WHEELMAN / CHAUFFEUR
namespace wm {
using namespace wheelman;
constexpr PlateReadout readouts[] = {{0, "RIDING", "dB", "+"}, {1, "LEVEL", "dB"}};
constexpr PlateChoice left[] = {{Attack, "ATTACK"}};
constexpr PlateToggle leftT[] = {{Live, "LIVE"}};
constexpr PlateKnob row1[] = {{Target, "TARGET", "-42", "0", K::Big}, {Sensitivity, "SENSITIVITY", "-12", "+12"}, {Spill, "SPILL", "-48", "-12"},
                              {Output, "OUTPUT"}};
constexpr PlateKnob row2[] = {{RangeMin, "RANGE MIN", "", "", K::Small}, {RangeMax, "RANGE MAX", "", "", K::Small}, {Idle, "IDLE", "", "", K::Small}};
} // namespace wm
namespace cf {
using namespace chauffeur;
constexpr PlateReadout readouts[] = {{0, "RIDING", "dB", "+"}, {1, "LEVEL", "dB"}};
constexpr PlateChoice left[] = {{Response, "RESPONSE"}};
constexpr PlateKnob row1[] = {{Target, "TARGET", "-42", "0", K::Big}, {NoteSens, "NOTE DETECT", "0", "100"}, {IgnoreSpill, "IGNORE SPILL"},
                              {IgnoreArtifacts, "IGNORE CLICKS", "0", "100"}, {Output, "OUTPUT"}};
constexpr PlateKnob row2[] = {{RangeMin, "RANGE MIN", "", "", K::Small}, {RangeMax, "RANGE MAX", "", "", K::Small}, {Idle, "IDLE", "", "", K::Small}};
} // namespace cf

// ---- SILENCER / TIGHT LIPS / OMERTA
namespace si {
using namespace silencer;
constexpr PlateReadout readouts[] = {{0, "ENERGY", "dB"}, {2, "OUTPUT PEAK", "dBFS"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-60", "0", K::Big}, {Freq, "FREQUENCY", "2k", "16k", K::Big}};
constexpr PlateChoice steppers[] = {{Audio, "AUDIO"}, {Sidechain, "SIDECHAIN"}, {Monitor, "MONITOR"}};
} // namespace si
namespace tl {
using namespace tightlips;
constexpr PlateChoice choices[] = {{Mode, "MODE"}, {Filter, "SIDECHAIN"}};
constexpr PlateToggle toggles[] = {{Listen, "LISTEN"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-80", "0", K::Big}, {Range, "RANGE", "-48", "0"}, {Freq, "FREQUENCY", "2k", "16k", K::Big}};
} // namespace tl
namespace om {
using namespace omerta;
constexpr PlateToggle toggles[] = {{Lookahead, "LOOKAHEAD"}, {Monitor, "MONITOR"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-80", "0", K::Big}, {Range, "RANGE", "-48", "0"}, {Width, "DETECTION WIDTH", "0", "100"},
                              {Mode, "MODE", "WIDE", "SPLIT"}};
} // namespace om

// ---- BREATHLESS
namespace br {
using namespace breathless;
constexpr PlateReadout readouts[] = {{0, "BREATH", "%"}, {1, "LEVEL", "dB"}, {2, "TAKEN OFF", "dB", "+"}};
constexpr PlateChoice left[] = {{Listen, "LISTEN"}};
constexpr PlateToggle leftT[] = {{RoomTone, "ROOM TONE"}};
constexpr PlateKnob row1[] = {{Sensitivity, "SENSITIVITY", "0", "100", K::Big}, {LevelGate, "LEVEL GATE"}, {Reduction, "REDUCTION", "ALL", "0", K::Big},
                              {FadeOut, "FADE OUT", "", "", K::Small}, {FadeIn, "FADE IN", "", "", K::Small}};
} // namespace br

// ---- TOMMY GUN
namespace tg {
using namespace tommygun;
constexpr PlateChoice choices[] = {{Rate, "RATE"}, {SyncSource, "TEMPO FROM"}};
constexpr PlateKnob row1[] = {{Amount, "AMOUNT", "0", "10", K::Big}, {Offset, "OFFSET", "-180", "+180"}, {Bpm, "BPM", "60", "200"}};
} // namespace tg

} // namespace

const PlateSpec kKnockout{.note1 = "SHAPE THE HITS", .note2 = "AND THE TAILS", .screen = Screen::None, .choices = ko::choices,
                          .row1 = ko::row1, .row2 = ko::row2,
                          .demo = [](hc::fx::Effect& e) { e.set(knockout::Attack, 60); e.set(knockout::Sustain, -40); }};
const PlateSpec kHaymaker{.note1 = "HIT HARDER", .note2 = "OR SOFTER", .screen = Screen::Readouts, .readouts = hm::readouts, .row1 = hm::row1,
                          .leftToggles = hm::leftT, .demo = [](hc::fx::Effect& e) { e.set(haymaker::Range, 9); }};
const PlateSpec kFourKings{.note1 = "FOUR BANDS", .note2 = "OF PUNCH", .toggles = fk::toggles, .row1 = fk::row1, .bands = &fk::bands,
                           .demo = [](hc::fx::Effect& e) { e.set(fourkings::band(0, fourkings::Range), 9); }};
const PlateSpec kBackbone{.note1 = "QUIET PARTS UP", .note2 = "LOUD PARTS HELD", .screen = Screen::Readouts, .readouts = bb::readouts, .row1 = bb::row1,
                          .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(backbone::LowLevel, 18); e.set(backbone::HighLevel, -12); }};
const PlateSpec kFullHouse{.note1 = "LOUDER, NOT SMALLER", .note2 = "QUIET PARTS COME UP", .screen = Screen::Readouts, .readouts = fh::readouts,
                           .choices = fh::choices, .toggles = fh::toggles, .row1 = fh::row1, .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) { e.set(fullhouse::LowGain, 10); e.set(fullhouse::LevelerOn, 1); }};
const PlateSpec kWheelman{.note1 = "RIDES THE VOCAL", .note2 = "SO YOU DON'T HAVE TO", .screen = Screen::Readouts, .readouts = wm::readouts,
                          .row1 = wm::row1, .row2 = wm::row2, .left = wm::left, .leftToggles = wm::leftT,
                          .demo = [](hc::fx::Effect& e) { e.set(wheelman::Target, -24); }};
const PlateSpec kChauffeur{.note1 = "RIDES THE BASS", .note2 = "NOTE BY NOTE", .screen = Screen::Readouts, .readouts = cf::readouts,
                           .row1 = cf::row1, .row2 = cf::row2, .left = cf::left,
                           .demo = [](hc::fx::Effect& e) { e.set(chauffeur::Target, -18); }};
const PlateSpec kSilencer{.note1 = "SOFTER ESSES", .note2 = "SAME VOICE", .screen = Screen::Needle, .readouts = si::readouts, .row1 = si::row1,
                          .steppers = si::steppers, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(silencer::Threshold, -40); }};
const PlateSpec kTightLips{.note1 = "ESSES DOWN", .note2 = "NO LISP", .screen = Screen::Needle, .choices = tl::choices, .toggles = tl::toggles,
                           .row1 = tl::row1, .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) { e.set(tightlips::Threshold, -50); }};
const PlateSpec kOmerta{.note1 = "THE ESSES", .note2 = "KEEP QUIET", .screen = Screen::Needle, .toggles = om::toggles, .row1 = om::row1,
                        .reportsReduction = true, .demo = [](hc::fx::Effect& e) { e.set(omerta::Threshold, -30); }};
const PlateSpec kBreathless{.note1 = "BREATHS GO QUIET", .note2 = "WORDS STAY PUT", .screen = Screen::Readouts, .readouts = br::readouts,
                            .row1 = br::row1, .left = br::left, .leftToggles = br::leftT,
                            .demo = [](hc::fx::Effect& e) { e.set(breathless::LevelGate, -20); }};
const PlateSpec kTommyGun{.note1 = "ONE KNOB", .note2 = "THE ROOM PUMPS", .screen = Screen::Needle, .choices = tg::choices,
                          .row1 = tg::row1, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(tommygun::Amount, 7); }};

} // namespace hem::rack::plates
