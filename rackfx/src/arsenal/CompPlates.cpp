// What's on each hardware-style compressor's plate (Face::Spec), and the settings its CI screenshot uses.
#include "Plates.hpp"

#include "hem/rack/Arsenal.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- CONSIGLIERE / STONE COLD: the needle (IN / GR / OUT), meter, mode and mains beside it
namespace op {
using namespace opto;
constexpr PlateChoice choices[] = {{Meter, "METER"}, {Mode, "MODE"}, {Analog, "HUM"}};
constexpr PlateToggle toggles[] = {{AutoMakeup, "AUTO MAKEUP"}};
constexpr PlateKnob row2A[] = {{PeakReduction, "SQUEEZE", "0", "100", K::Big}, {Gain, "GAIN", "0", "100", K::Big},
                               {HiFreq, "BRIGHT DETECT", "FLAT", "MORE"}, {Mix, "MIX", "DRY", "WET", K::Small}, {Trim, "TRIM", "", "", K::Small}};
constexpr PlateKnob row3A[] = {{PeakReduction, "SQUEEZE", "0", "10", K::Big}, {Gain, "GAIN", "0", "10", K::Big},
                               {HiFreq, "BRIGHT DETECT", "FLAT", "MORE"}, {Mix, "MIX", "DRY", "WET", K::Small}, {Trim, "TRIM", "", "", K::Small}};
} // namespace op

// ---- HEAVYWEIGHT
namespace hw {
using namespace heavyweight;
constexpr PlateChoice choices[] = {{Meter, "METER"}, {Sync, "RELEASE IN"}};
constexpr PlateToggle toggles[] = {{Limiter, "LIMITER"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-48", "0"}, {Ratio, "RATIO", "1", "50"}, {Attack, "ATTACK"},
                              {Punch, "SNAP", "0", "30"}, {Release, "RELEASE"}};
constexpr PlateKnob row2[] = {{Bpm, "BPM", "", "", K::Small}, {Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
constexpr PlateChoice steppers[] = {{Note, "NOTE"}, {Analog, "HUM"}};
} // namespace hw

// ---- THE DON
namespace dn {
using namespace thedon;
constexpr PlateChoice choices[] = {{Meter, "METER"}, {Ratio, "RATIO"}, {Analog, "HUM"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-24", "+16", K::Big}, {Decay, "DECAY", "1", "32"}, {Output, "OUTPUT"},
                              {VuCal, "VU ZERO", "8", "24", K::Small}};
} // namespace dn

// ---- SLOW BURN / LONE WOLF
namespace sb {
using namespace slowburn;
constexpr PlateChoice choices[] = {{Link, "LINK"}, {Mains, "HUM"}};
constexpr PlateToggle toggles[] = {{Compress, "COMPRESS"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-20", "0"}, {Threshold, "THRESHOLD", "0", "10", K::Big}, {Time, "TIMING", "1", "6"},
                              {Output, "OUTPUT"}};
} // namespace sb
namespace lw {
using namespace lonewolf;
constexpr PlateChoice choices[] = {{Mains, "HUM"}};
constexpr PlateToggle toggles[] = {{Compress, "COMPRESS"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-20", "0"}, {Threshold, "THRESHOLD", "0", "10", K::Big}, {Time, "TIMING", "1", "6"},
                              {Output, "OUTPUT"}};
} // namespace lw

// ---- IRON FIST
namespace ir {
using namespace ironfist;
constexpr PlateChoice choices[] = {{Meter, "METER"}, {Ratio, "RATIO"}};
constexpr PlateToggle toggles[] = {{Compress, "COMPRESS"}, {DeEss, "DE-ESS"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-24", "+18"}, {Output, "OUTPUT"}, {Mix, "MIX", "DRY", "WET"}, {Trim, "TRIM"}};
constexpr PlateKnob row2[] = {{LimitLevel, "LIMIT AT", "", "", K::Small}, {LimitAttack, "LIMIT ATTACK", "", "", K::Small}};
constexpr PlateToggle leftT[] = {{Limiter, "LIMITER"}, {AutoMakeup, "AUTO MAKEUP"}};
constexpr PlateChoice steppers[] = {{Release, "RELEASE"}, {LimitRelease, "LIMIT REL"}, {Analog, "HUM"}};
} // namespace ir

// ---- QUICK DRAW
namespace qd {
using namespace quickdraw;
constexpr PlateChoice choices[] = {{Meter, "METER"}, {Revision, "ERA"}};
constexpr PlateToggle toggles[] = {{Compress, "COMPRESS"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-60", "0", K::Big}, {Output, "OUTPUT", "-60", "0", K::Big}, {Attack, "ATTACK", "1", "7"},
                              {Release, "RELEASE", "1", "7"}, {Mix, "MIX", "", "", K::Small}, {Trim, "TRIM", "", "", K::Small}};
constexpr PlateToggle leftT[] = {{AutoMakeup, "AUTO MAKEUP"}};
constexpr PlateChoice steppers[] = {{Ratio, "RATIO"}, {Analog, "HUM"}};
} // namespace qd

// ---- OLD MONEY
namespace om {
using namespace oldmoney;
constexpr PlateReadout readouts[] = {{0, "INPUT PEAK", "dBFS"}, {1, "OUTPUT PEAK", "dBFS"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-60", "0", K::Big}, {Ratio, "RATIO"}, {Attack, "ATTACK"}, {Release, "RELEASE"},
                              {Output, "OUTPUT"}};
constexpr PlateKnob row2[] = {{Mix, "MIX", "", "", K::Small}, {Trim, "TRIM", "", "", K::Small}};
constexpr PlateChoice steppers[] = {{ReleaseMode, "RELEASE"}, {Behavior, "LET-GO"}, {Character, "COLOR"}};
} // namespace om

// ---- TERRITORY / STRAIGHT EDGE: band columns and the MASTER column, crossovers between the bands
namespace mb {
constexpr PlateKnob bandKnobs[] = {{territory::Threshold, "THRESHOLD"}, {territory::Gain, "GAIN"}, {territory::Range, "RANGE"},
                                   {territory::Attack, "ATTACK"}, {territory::Release, "RELEASE"}};
constexpr PlateToggle bandToggles[] = {{territory::Solo, "SOLO"}, {territory::Bypass, "BYPASS"}};
constexpr PlateKnob borders4[] = {{territory::X1, "SPLIT"}, {territory::X2, "SPLIT"}, {territory::X3, "SPLIT"}};
constexpr PlateBands bands4{.count = territory::kBands + 1, .first = 0, .stride = territory::kPerBand, .knobs = bandKnobs, .toggles = bandToggles,
                            .borders = borders4, .meter = 0, .lastIsMaster = true};
constexpr PlateKnob row4[] = {{territory::Q, "Q"}, {territory::Knee, "KNEE"}, {territory::Output, "OUTPUT"}};
constexpr PlateChoice steppers4[] = {{territory::ReleaseMode, "RELEASE"}, {territory::Behavior, "LET-GO"}};
constexpr PlateKnob borders5[] = {{straightedge::X1, "SPLIT"}, {straightedge::X2, "SPLIT"}, {straightedge::X3, "SPLIT"}, {straightedge::X4, "SPLIT"}};
constexpr PlateBands bands5{.count = straightedge::kBands + 1, .first = 0, .stride = straightedge::kPerBand, .knobs = bandKnobs,
                            .toggles = bandToggles, .borders = borders5, .meter = 0, .lastIsMaster = true};
constexpr PlateKnob row5[] = {{straightedge::Adaptive, "ADAPTIVE"}, {straightedge::Knee, "KNEE"}, {straightedge::Output, "OUTPUT"}};
constexpr PlateToggle toggles5[] = {{straightedge::Trim, "TRIM", true}, {straightedge::Dither, "DITHER"}};
constexpr PlateChoice steppers5[] = {{straightedge::Makeup, "MAKEUP"}, {straightedge::ReleaseMode, "RELEASE"}, {straightedge::Behavior, "LET-GO"}};
} // namespace mb

} // namespace

const PlateSpec kTerritory{.note1 = "FOUR TERRITORIES", .note2 = "EACH ITS OWN RULES", .row1 = mb::row4, .steppers = mb::steppers4, .bands = &mb::bands4,
                           .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) { e.set(territory::band(1, territory::Threshold), -40); e.set(territory::band(3, territory::Range), -12); }};
const PlateSpec kStraightEdge{.note1 = "FIVE BANDS", .note2 = "NO PHASE SHIFT", .toggles = mb::toggles5, .row1 = mb::row5, .steppers = mb::steppers5,
                              .bands = &mb::bands5, .reportsReduction = true,
                              .demo = [](hc::fx::Effect& e) { e.set(straightedge::band(1, straightedge::Threshold), -40); e.set(straightedge::Adaptive, -12); }};
const PlateSpec kConsigliere{.note1 = "TWO KNOBS", .note2 = "SMOOTH CONTROL", .screen = Screen::Needle, .choices = op::choices,
                             .toggles = op::toggles, .row1 = op::row2A, .reportsReduction = true, .meterSelect = opto::Meter, .vuMeter = 0,
                             .demo = [](hc::fx::Effect& e) { e.set(opto::PeakReduction, 55); e.set(opto::Gain, 15); }};
const PlateSpec kStoneCold{.note1 = "SOLID STATE", .note2 = "QUICK AND FIRM", .screen = Screen::Needle, .choices = op::choices,
                           .toggles = op::toggles, .row1 = op::row3A, .reportsReduction = true, .meterSelect = opto::Meter, .vuMeter = 0,
                           .demo = [](hc::fx::Effect& e) { e.set(opto::PeakReduction, 5.5f); e.set(opto::Gain, 1.5f); }};
const PlateSpec kHeavyweight{.note1 = "HITS STAY HARD", .note2 = "THE BODY GETS HELD", .screen = Screen::Needle, .choices = hw::choices,
                             .toggles = hw::toggles, .row1 = hw::row1, .row2 = hw::row2, .steppers = hw::steppers, .reportsReduction = true,
                             .meterSelect = heavyweight::Meter, .vuMeter = 0,
                             .demo = [](hc::fx::Effect& e) {
                                 using namespace heavyweight;
                                 e.set(Threshold, -26); e.set(Punch, 12); e.set(Sync, 1);
                             }};
const PlateSpec kTheDon{.note1 = "OLD SCHOOL BUS", .note2 = "IT BREATHES", .screen = Screen::Needle, .choices = dn::choices,
                        .row1 = dn::row1, .reportsReduction = true, .meterSelect = thedon::Meter, .vuMeter = 0,
                        .demo = [](hc::fx::Effect& e) { e.set(thedon::Threshold, 8); e.set(thedon::Ratio, 3); }};
const PlateSpec kSlowBurn{.note1 = "TUBE LIMITING", .note2 = "SLOW AND SWEET", .screen = Screen::Needle, .choices = sb::choices,
                          .toggles = sb::toggles, .row1 = sb::row1, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(slowburn::Threshold, 6); e.set(slowburn::Time, 4); }};
const PlateSpec kLoneWolf{.note1 = "ONE CHANNEL", .note2 = "OF TUBE LIMITING", .screen = Screen::Needle, .choices = lw::choices,
                          .toggles = lw::toggles, .row1 = lw::row1, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(lonewolf::Threshold, 6); e.set(lonewolf::Time, 2); }};
const PlateSpec kIronFist{.note1 = "DRIVE IT IN", .note2 = "CONSOLE STYLE", .screen = Screen::Needle, .choices = ir::choices,
                          .toggles = ir::toggles, .row1 = ir::row1, .row2 = ir::row2, .leftToggles = ir::leftT, .steppers = ir::steppers,
                          .reportsReduction = true, .meterSelect = ironfist::Meter, .vuMeter = 0,
                          .demo = [](hc::fx::Effect& e) { e.set(ironfist::Input, 14); e.set(ironfist::Limiter, 1); }};
const PlateSpec kQuickDraw{.note1 = "FAST HANDS", .note2 = "GRAB THE PEAKS", .screen = Screen::Needle, .choices = qd::choices,
                           .toggles = qd::toggles, .row1 = qd::row1, .leftToggles = qd::leftT, .steppers = qd::steppers,
                           .reportsReduction = true, .meterSelect = quickdraw::Meter, .vuMeter = 0,
                           .demo = [](hc::fx::Effect& e) { e.set(quickdraw::Input, -12); e.set(quickdraw::Attack, 5); }};
const PlateSpec kOldMoney{.note1 = "SMOOTH OPERATOR", .note2 = "ON ANYTHING", .screen = Screen::Needle, .readouts = om::readouts,
                          .row1 = om::row1, .row2 = om::row2, .steppers = om::steppers, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(oldmoney::Threshold, -30); e.set(oldmoney::Ratio, 3); }};

} // namespace hem::rack::plates
