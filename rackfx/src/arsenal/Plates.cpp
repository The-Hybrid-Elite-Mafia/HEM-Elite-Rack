// What's on each arsenal plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "Plates.hpp"

#include "hem/rack/Arsenal.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- KNOCKOUT: no screen; the switches across the top, the hits and tails on one row, the bands and output under
namespace ko {
using namespace knockout;
constexpr PlateChoice choices[] = {{Mode, "MODE"}, {AttackShape, "ATTACK SHAPE"}, {SustainShape, "SUSTAIN SHAPE"}, {Guard, "GUARD"}};
constexpr PlateKnob row1[] = {{Attack, "ATTACK", "-", "+"}, {AttackSens, "SENSITIVITY"}, {AttackTime, "LENGTH"},
                              {Sustain, "SUSTAIN", "-", "+"}, {SustainSens, "SENSITIVITY"}, {SustainTime, "LENGTH"}};
constexpr PlateKnob row2[] = {{Band1, "LOW", "", "", K::Small}, {Band2, "LOW MID", "", "", K::Small}, {Band3, "HIGH MID", "", "", K::Small},
                              {Band4, "HIGH", "", "", K::Small}, {Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace ko

// ---- FULL HOUSE
namespace fh {
using namespace fullhouse;
constexpr PlateReadout readouts[] = {{0, "LIFTED", "dB", "+"}, {1, "LEVELER", "dB", "+"}, {2, "HELD", "dB", "+"}};
constexpr PlateChoice left[] = {{LevelSpeed, "LEVELER SPEED"}};
constexpr PlateKnob row1[] = {{Gate, "GATE", "OFF", "-30"}, {LowThreshold, "LOW THRESHOLD"}, {LowLift, "MAX LIFT", "0", "24"},
                              {Leveler, "LEVELER", "0", "100"}, {HighLevel, "HIGH LEVEL", "0", "100"}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace fh

// ---- WHEELMAN
namespace wm {
using namespace wheelman;
constexpr PlateReadout readouts[] = {{0, "RIDING", "dB", "+"}, {1, "LEVEL", "dB"}};
constexpr PlateChoice left[] = {{Source, "SOURCE"}, {Speed, "SPEED"}};
constexpr PlateKnob row1[] = {{Target, "TARGET", "-40", "0", K::Big}, {Sensitivity, "SENSITIVITY"}, {MaxBoost, "MAX BOOST", "0", "18"},
                              {MaxCut, "MAX CUT", "0", "18"}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace wm

// ---- SILENCER
namespace si {
using namespace silencer;
constexpr PlateChoice choices[] = {{Detect, "DETECT"}, {Filter, "CLASSIC FILTER"}, {Split, "TAKES DOWN"}};
constexpr PlateToggle toggles[] = {{Lookahead, "LOOKAHEAD"}, {Listen, "LISTEN"}};
constexpr PlateKnob row1[] = {{Freq, "FREQUENCY", "2k", "16k", K::Big}, {Threshold, "THRESHOLD", "-60", "0", K::Big}, {Range, "RANGE", "0", "30"},
                              {Width, "SMART WIDTH", "", "", K::Small}};
} // namespace si

// ---- BREATHLESS
namespace br {
using namespace breathless;
constexpr PlateReadout readouts[] = {{0, "BREATH", "%"}, {1, "LEVEL", "dB"}, {2, "TAKEN OFF", "dB", "+"}};
constexpr PlateChoice left[] = {{Listen, "LISTEN"}};
constexpr PlateKnob row1[] = {{Sensitivity, "SENSITIVITY", "0", "100", K::Big}, {LevelGate, "LEVEL GATE"}, {Reduction, "REDUCTION", "0", "60", K::Big},
                              {FadeOut, "FADE OUT", "", "", K::Small}, {FadeIn, "FADE IN", "", "", K::Small}};
} // namespace br

// ---- TOMMY GUN
namespace tg {
using namespace tommygun;
constexpr PlateChoice choices[] = {{Rate, "RATE"}, {SyncSource, "TEMPO FROM"}};
constexpr PlateKnob row1[] = {{Amount, "AMOUNT", "0", "100", K::Big}, {Offset, "OFFSET", "-180", "+180"}, {Bpm, "BPM", "60", "200"}};
} // namespace tg

} // namespace

const PlateSpec kKnockout{.note1 = "SHAPE THE HITS", .note2 = "AND THE TAILS", .screen = Screen::None, .choices = ko::choices,
                          .row1 = ko::row1, .row2 = ko::row2,
                          .demo = [](hc::fx::Effect& e) { e.set(knockout::Attack, 60); e.set(knockout::Sustain, -40); e.set(knockout::Mode, 1); }};
const PlateSpec kFullHouse{.note1 = "LOUDER, NOT SMALLER", .note2 = "QUIET PARTS COME UP", .screen = Screen::Readouts, .readouts = fh::readouts,
                           .row1 = fh::row1, .left = fh::left, .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) {
                               using namespace fullhouse;
                               e.set(LowLift, 10); e.set(Leveler, 50); e.set(HighLevel, 60);
                           }};
const PlateSpec kWheelman{.note1 = "RIDES THE FADER", .note2 = "SO YOU DON'T HAVE TO", .screen = Screen::Readouts, .readouts = wm::readouts,
                          .row1 = wm::row1, .left = wm::left,
                          .demo = [](hc::fx::Effect& e) { e.set(wheelman::Target, -24); }};
const PlateSpec kSilencer{.note1 = "SOFTER ESSES", .note2 = "SAME VOICE", .screen = Screen::Needle, .choices = si::choices,
                          .toggles = si::toggles, .row1 = si::row1, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(silencer::Threshold, -40); }};
const PlateSpec kBreathless{.note1 = "BREATHS GO QUIET", .note2 = "WORDS STAY PUT", .screen = Screen::Readouts, .readouts = br::readouts,
                            .row1 = br::row1, .left = br::left,
                            .demo = [](hc::fx::Effect& e) { e.set(breathless::LevelGate, -20); }};
const PlateSpec kTommyGun{.note1 = "ONE KNOB", .note2 = "THE ROOM PUMPS", .screen = Screen::Needle, .choices = tg::choices,
                          .row1 = tg::row1, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(tommygun::Amount, 70); }};

} // namespace hem::rack::plates
