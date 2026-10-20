// What's on each tone-shop plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/ToneShop.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- STRAIGHT SHOOTER: the curve, the two cut slopes beside it, frequencies over gains
namespace ss {
using namespace straight;
constexpr PlateChoice choices[] = {{LowCutSlope, "LOW CUT SLOPE"}, {HighCutSlope, "HIGH CUT SLOPE"}};
constexpr PlateKnob row1[] = {{LowCut, "LOW CUT"}, {LowFreq, "LOW"}, {Mid1Freq, "MID 1"}, {Mid2Freq, "MID 2"},
                              {Mid3Freq, "MID 3"}, {HighFreq, "HIGH"}, {HighCut, "HIGH CUT"}};
constexpr PlateKnob row2[] = {{LowGain, "LOW GAIN", "", "", K::Small}, {Mid1Gain, "MID 1 GAIN", "", "", K::Small}, {Mid1Q, "MID 1 Q", "", "", K::Small},
                              {Mid2Gain, "MID 2 GAIN", "", "", K::Small}, {Mid2Q, "MID 2 Q", "", "", K::Small}, {Mid3Gain, "MID 3 GAIN", "", "", K::Small},
                              {Mid3Q, "MID 3 Q", "", "", K::Small}, {HighGain, "HIGH GAIN", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace ss

// ---- THE BOSS: the needle, order / low cut / limit beside it, dynamics over EQ
namespace bo {
using namespace boss;
constexpr PlateChoice choices[] = {{Order, "ORDER"}, {LowCut, "LOW CUT"}};
constexpr PlateToggle toggles[] = {{Limit, "LIMIT"}};
constexpr PlateKnob row1[] = {{Gate, "GATE", "OFF", "0"}, {Threshold, "THRESHOLD", "-50", "0"}, {Ratio, "RATIO", "1", "20"},
                              {Attack, "ATTACK"}, {Release, "RELEASE"}, {Output, "OUTPUT"}};
constexpr PlateKnob row2[] = {{LowGain, "LOW", "", "", K::Small}, {LoMidGain, "LOW MID", "", "", K::Small}, {LoMidFreq, "LM FREQ", "", "", K::Small},
                              {HiMidGain, "HIGH MID", "", "", K::Small}, {HiMidFreq, "HM FREQ", "", "", K::Small}, {HighGain, "HIGH", "", "", K::Small}};
constexpr PlateChoice steppers[] = {{LowFreq, "LOW AT"}, {HighFreq, "HIGH AT"}};
} // namespace bo

// ---- KINGPIN: three big moves on top, gate and tone under them
namespace kp {
using namespace kingpin;
constexpr PlateKnob row1[] = {{Punch, "PUNCH", "0", "100", K::Big}, {Crush, "CRUSH", "0", "100", K::Big}, {Drive, "DRIVE", "0", "100", K::Big}};
constexpr PlateKnob row2[] = {{Gate, "GATE", "", "", K::Small}, {GateRelease, "GATE REL", "", "", K::Small}, {Thump, "THUMP", "", "", K::Small},
                              {ThumpFreq, "THUMP FREQ", "", "", K::Small}, {Snap, "SNAP", "", "", K::Small}, {SnapFreq, "SNAP FREQ", "", "", K::Small},
                              {Output, "OUTPUT", "", "", K::Small}};
} // namespace kp

// ---- GOLD CHAIN: the needle, LINK beside it, the one big knob between in and out
namespace gc {
using namespace goldchain;
constexpr PlateToggle toggles[] = {{Link, "LINK"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-12", "+12"}, {Tone, "TONE", "0", "100", K::Big}, {Mix, "MIX", "DRY", "WET"}, {Output, "OUTPUT", "-12", "+12"}};
} // namespace gc

// ---- IVORIES
namespace iv {
using namespace ivories;
constexpr PlateKnob row1[] = {{Tone, "TONE", "DARK", "BRIGHT", K::Big}, {Body, "BODY", "0", "100"}, {Compress, "COMPRESS", "0", "100"}};
constexpr PlateKnob row2[] = {{Delay, "DELAY"}, {Time, "TIME"}, {Doubler, "DOUBLER"}, {Width, "WIDTH"}, {Output, "OUTPUT"}};
} // namespace iv

// ---- HIGH ROLLER: lift across the top, width under it, band by band
namespace hr {
using namespace highroller;
constexpr PlateKnob row1[] = {{Lift1, "SUB LIFT"}, {Lift2, "LOW LIFT"}, {Lift3, "MID LIFT"}, {Lift4, "HIGH LIFT"}, {Lift5, "AIR LIFT"}, {Output, "OUTPUT", "", "", K::Small}};
constexpr PlateKnob row2[] = {{Width1, "SUB WIDTH"}, {Width2, "LOW WIDTH"}, {Width3, "MID WIDTH"}, {Width4, "HIGH WIDTH"}, {Width5, "AIR WIDTH"}, {Mix, "MIX", "", "", K::Small}};
} // namespace hr

// ---- THE MIDDLEMAN: centre and sides read out, one row of moves
namespace mm {
using namespace middleman;
constexpr PlateReadout readouts[] = {{0, "CENTER", "dB"}, {1, "SIDES", "dB"}};
constexpr PlateKnob row1[] = {{Center, "CENTER", "-12", "+12"}, {Sides, "SIDES", "-12", "+12"}, {LowBalance, "LOW BALANCE", "SIDES", "MID"},
                              {HighBalance, "HIGH BALANCE", "SIDES", "MID"}, {Punch, "PUNCH", "SIDES", "MID"}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace mm

// ---- DOUBLE CROSS
namespace dc {
using namespace doublecross;
constexpr PlateChoice choices[] = {{Style, "STYLE"}, {LowMono, "LOW STAYS MONO"}};
constexpr PlateKnob row1[] = {{Spread, "SPREAD", "0", "100", K::Big}, {Center, "CENTER", "-12", "+6"}, {Output, "OUTPUT"}};
} // namespace dc

// ---- CUTTHROAT: the curve, the slope beside it, the sweep first
namespace ct {
using namespace cutthroat;
constexpr PlateChoice choices[] = {{Slope, "SLOPE"}};
constexpr PlateKnob row1[] = {{Sweep, "SWEEP", "DARK", "THIN", K::Big}, {LowCut, "LOW CUT"}, {HighCut, "HIGH CUT"}, {Resonance, "RESONANCE"},
                              {Drive, "DRIVE"}, {Mix, "MIX", "", "", K::Small}, {Output, "OUTPUT", "", "", K::Small}};
} // namespace ct

} // namespace

const PlateSpec kStraightShooter{.note1 = "NO PHASE SHIFT", .note2 = "ONLY THE LEVELS MOVE", .screen = Screen::Curve, .choices = ss::choices,
                                 .row1 = ss::row1, .row2 = ss::row2,
                                 .demo = [](hc::fx::Effect& e) {
                                     using namespace straight;
                                     e.set(LowCutSlope, 2); e.set(LowCut, 35); e.set(Mid1Gain, -3); e.set(Mid2Gain, 2.5f); e.set(Mid2Q, 0.8f);
                                     e.set(HighGain, 3);
                                 }};
const PlateSpec kTheBoss{.note1 = "THE WHOLE CHANNEL", .note2 = "ONE PLATE", .screen = Screen::Needle, .choices = bo::choices, .toggles = bo::toggles,
                         .row1 = bo::row1, .row2 = bo::row2, .steppers = bo::steppers, .reportsReduction = true,
                         .demo = [](hc::fx::Effect& e) { using namespace boss; e.set(Threshold, -30); e.set(LowGain, 3); e.set(HiMidGain, 2); }};
const PlateSpec kKingpin{.note1 = "DRUMS RUN THE BLOCK", .note2 = "HIT HARDER", .screen = Screen::None, .row1 = kp::row1, .row2 = kp::row2,
                         .reportsReduction = true,
                         .demo = [](hc::fx::Effect& e) { using namespace kingpin; e.set(Punch, 60); e.set(Crush, 35); e.set(Drive, 25); e.set(Thump, 4); }};
const PlateSpec kGoldChain{.note1 = "ONE KNOB", .note2 = "RICHER TONE", .screen = Screen::Needle, .toggles = gc::toggles, .row1 = gc::row1,
                           .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) { e.set(goldchain::Tone, 70); }};
const PlateSpec kIvories{.note1 = "THE KEYS", .note2 = "FRONT AND CENTER", .screen = Screen::None, .row1 = iv::row1, .row2 = iv::row2,
                         .demo = [](hc::fx::Effect& e) { using namespace ivories; e.set(Tone, 30); e.set(Doubler, 40); e.set(Delay, 25); }};
const PlateSpec kHighRoller{.note1 = "EVERY BAND", .note2 = "BIGGER AND WIDER", .screen = Screen::None, .row1 = hr::row1, .row2 = hr::row2,
                            .demo = [](hc::fx::Effect& e) { using namespace highroller; e.set(Lift3, 40); e.set(Width4, 140); e.set(Width5, 160); }};
const PlateSpec kMiddleman{.note1 = "THE MIDDLE", .note2 = "AND THE SIDES", .screen = Screen::Readouts, .readouts = mm::readouts, .row1 = mm::row1,
                           .demo = [](hc::fx::Effect& e) { using namespace middleman; e.set(Sides, 3); e.set(LowBalance, 60); e.set(Punch, 40); }};
const PlateSpec kDoubleCross{.note1 = "ONE IN", .note2 = "TWO OUT", .screen = Screen::None, .choices = dc::choices, .row1 = dc::row1,
                             .demo = [](hc::fx::Effect& e) { e.set(doublecross::Spread, 70); }};
const PlateSpec kCutthroat{.note1 = "CUT THE LOWS", .note2 = "CUT THE HIGHS", .screen = Screen::Curve, .choices = ct::choices, .row1 = ct::row1,
                           .demo = [](hc::fx::Effect& e) { using namespace cutthroat; e.set(Sweep, -45); e.set(Resonance, 40); e.set(Slope, 1); }};

} // namespace hem::rack::plates
