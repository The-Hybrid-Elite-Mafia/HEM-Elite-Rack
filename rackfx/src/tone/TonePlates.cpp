// What's on each tone-shop plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/ToneShop.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- STRAIGHT SHOOTER / BOTTOM LINE: the curve on top, the bands under it, the globals at the bottom
namespace le {
using namespace lineq;
constexpr PlateChoice bandSteppers[] = {{Type, "TYPE"}};
constexpr PlateKnob bandKnobs[] = {{Freq, "FREQ"}, {Gain, "GAIN"}, {Q, "Q"}};
constexpr PlateToggle bandToggles[] = {{On, "BAND"}};
constexpr PlateBands straightBands{.count = straight::kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .steppers = bandSteppers,
                                   .toggles = bandToggles};
constexpr PlateBands bottomBands{.count = bottomline::kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .steppers = bandSteppers,
                                 .toggles = bandToggles};
// the globals sit at the same offsets after the bands in both
template <int kOut>
struct Globals {
    static constexpr PlateKnob row1[] = {{kOut, "GAIN"}};
    static constexpr PlateToggle toggles[] = {{kOut + 1, "TRIM", true}, {kOut + 3, "DITHER"}};
    static constexpr PlateChoice steppers[] = {{kOut + 2, "FILTER"}, {kOut + 4, "SCALE"}};
};
using SG = Globals<straight::Output>;
using BG = Globals<bottomline::Output>;
} // namespace le

// ---- TEN TOES / FOUR CORNERS / SMOOTH TALKER: the curve (left, and right when it differs), LEFT / RIGHT tabs
namespace ie {
using namespace lineq;
constexpr const char* pageNames[] = {"LEFT", "RIGHT"};
constexpr PlateChoice bandSteppers[] = {{Type, "TYPE"}};
constexpr PlateKnob bandKnobs[] = {{Freq, "FREQ"}, {Gain, "GAIN"}, {Q, "Q"}};
constexpr PlateToggle bandToggles[] = {{On, "BAND"}};
constexpr PlateToggle filterToggles[] = {{On, "FILTER"}};
constexpr PlateBands ttBands{.count = tentoes::kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .steppers = bandSteppers,
                             .toggles = bandToggles, .pages = 2, .pageStride = tentoes::kSide, .pageNames = pageNames};
constexpr PlateBands fcBands{.count = fourcorners::kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .steppers = bandSteppers,
                             .toggles = filterToggles, .pages = 2, .pageStride = fourcorners::kSide, .pageNames = pageNames};
constexpr PlateBands sxBands{.count = smoothtalker::kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .steppers = bandSteppers,
                             .toggles = bandToggles, .pages = 2, .pageStride = smoothtalker::kSide, .pageNames = pageNames};
constexpr PlateKnob ttRow1[] = {{tentoes::InputL, "INPUT L", "OFF", "0"}, {tentoes::InputR, "INPUT R", "OFF", "0"},
                                {tentoes::OutputL, "OUTPUT L"}, {tentoes::OutputR, "OUTPUT R"}};
constexpr PlateToggle ttToggles[] = {{tentoes::Trim, "TRIM", true}};
constexpr PlateToggle ttLeft[] = {{tentoes::Link, "LINK"}, {tentoes::PhaseL, "PHASE L"}, {tentoes::PhaseR, "PHASE R"}};
constexpr PlateKnob fcRow1[] = {{fourcorners::GainL, "GAIN L"}, {fourcorners::GainR, "GAIN R"}};
constexpr PlateToggle fcLeft[] = {{fourcorners::Link, "LINK"}};
constexpr PlateKnob sxRow1[] = {{smoothtalker::Output, "OUTPUT", "OFF", "+12"}};
constexpr PlateToggle sxToggles[] = {{smoothtalker::Trim, "TRIM", true}};
constexpr PlateToggle sxLeft[] = {{smoothtalker::Link, "LINK"}, {smoothtalker::Spectrum, "SPECTRUM"}};
} // namespace ie

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

const PlateSpec kStraightShooter{.note1 = "NO PHASE SHIFT", .note2 = "ONLY THE LEVELS MOVE", .screen = Screen::Curve, .toggles = le::SG::toggles,
                                 .row1 = le::SG::row1, .steppers = le::SG::steppers, .bands = &le::straightBands,
                                 .demo = [](hc::fx::Effect& e) {
                                     using namespace lineq;
                                     e.set(band(0, Type), FLowCut); e.set(band(0, Freq), 35); e.set(band(1, Gain), -3);
                                     e.set(band(3, Gain), 2.5f); e.set(band(3, Q), 0.8f); e.set(band(5, Gain), 3);
                                 }};
const PlateSpec kBottomLine{.note1 = "THE LOW END", .note2 = "NO PHASE SHIFT", .screen = Screen::Curve, .toggles = le::BG::toggles,
                            .row1 = le::BG::row1, .steppers = le::BG::steppers, .bands = &le::bottomBands,
                            .demo = [](hc::fx::Effect& e) {
                                using namespace lineq;
                                e.set(band(0, On), 1); e.set(band(0, Freq), 28); e.set(band(1, Gain), 3.5f); e.set(band(1, Freq), 70);
                                e.set(band(2, Type), Bell); e.set(band(2, Freq), 280); e.set(band(2, Gain), -2.5f); e.set(band(2, Q), 1.4f);
                            }};
const PlateSpec kTenToes{.note1 = "TEN BANDS", .note2 = "LEFT AND RIGHT", .screen = Screen::Curve, .toggles = ie::ttToggles, .row1 = ie::ttRow1,
                         .leftToggles = ie::ttLeft, .bands = &ie::ttBands,
                         .demo = [](hc::fx::Effect& e) {
                             using namespace lineq;
                             e.set(band(0, Type), tentoes::LowCut); e.set(band(0, Freq), 40); e.set(band(3, Gain), 3); e.set(band(5, Gain), -4);
                             e.set(band(5, Q), 4); e.set(band(8, Type), tentoes::HighShelf); e.set(band(8, Gain), 2.5f);
                         }};
const PlateSpec kFourCorners{.note1 = "FOUR FILTERS", .note2 = "LEFT AND RIGHT", .screen = Screen::Curve, .row1 = ie::fcRow1,
                             .leftToggles = ie::fcLeft, .bands = &ie::fcBands,
                             .demo = [](hc::fx::Effect& e) {
                                 using namespace lineq;
                                 e.set(band(0, Gain), 3); e.set(band(1, Gain), -2.5f); e.set(band(2, Gain), 2); e.set(band(3, Gain), 4);
                             }};
const PlateSpec kSmoothTalker{.note1 = "SIX BANDS", .note2 = "SMOOTH AND MUSICAL", .screen = Screen::Curve, .toggles = ie::sxToggles, .row1 = ie::sxRow1,
                            .leftToggles = ie::sxLeft, .bands = &ie::sxBands,
                            .demo = [](hc::fx::Effect& e) {
                                using namespace lineq;
                                e.set(band(0, On), 1); e.set(band(0, Freq), 35); e.set(band(1, Gain), 2.5f); e.set(band(2, Gain), -3);
                                e.set(band(4, Gain), 3);
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
