// What's on each tone-shop plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/ToneShop.hpp"
#include "hem/rack/Comeback.hpp"

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
    static constexpr PlateChoice steppers[] = {{kOut + 2, "FILTER"}, {kOut + 4, "VIEW RANGE"}};
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

// ---- LOW RIDER / UNDERTOW / DEEP POCKETS / LOW PROFILE
namespace lo {
namespace r = lowrider;
constexpr PlateReadout rReadouts[] = {{0, "OUTPUT", "dBFS", "+"}};
constexpr PlateToggle rToggles[] = {{r::Align, "LINE UP"}};
constexpr PlateKnob rRow1[] = {{r::Range, "RANGE", "20", "120", K::Big}, {r::LowAir, "NEW OCTAVE", "OFF", "+30"}, {r::Lo, "LOWS", "OFF", "+30"},
                               {r::Direct, "DRY", "OFF", "0"}, {r::Output, "OUTPUT", "-60", "0"}, {r::Trim, "TRIM", "-12", "+12", K::Small}};
namespace u = undertow;
constexpr PlateReadout uReadouts[] = {{0, "SUB", "dBFS", "+"}, {1, "OUTPUT", "dBFS", "+"}};
constexpr PlateChoice uChoices[] = {{u::MonoSub, "SUB IS"}};
constexpr PlateToggle uToggles[] = {{u::Sub1On, "OCTAVE DOWN"}, {u::Sub2On, "TWO DOWN"}};
constexpr PlateKnob uRow1[] = {{u::RangeMin, "LOWEST", "20", "120"}, {u::RangeMax, "HIGHEST", "40", "240"}, {u::Sub1, "OCTAVE DOWN", "OFF", "+12"},
                               {u::Sub2, "TWO DOWN", "OFF", "+12"}};
constexpr PlateKnob uRow2[] = {{u::Drive, "DRIVE", "0", "100"}, {u::Dynamics, "FOLLOW", "STEADY", "FOLLOW"}, {u::Mix, "MIX", "DRY", "SUBS"},
                               {u::Output, "OUTPUT", "-18", "0"}};
namespace d = deeppockets;
constexpr PlateReadout dReadouts[] = {{0, "PEAK", "dBFS", "+"}};
constexpr PlateChoice dChoices[] = {{d::Listen, "MONITOR"}, {d::HighPass, "HIGH-PASS"}};
constexpr PlateToggle dToggles[] = {{d::Reset, "RESET PEAK", true}};
constexpr PlateKnob dRow1[] = {{d::Frequency, "FREQUENCY", "32", "256", K::Big}, {d::Original, "REAL BASS", "OFF", "+12"},
                               {d::Harmonics, "OVERTONES", "OFF", "+12"}, {d::Input, "INPUT", "OFF", "0"}};
constexpr PlateKnob dRow2[] = {{d::Decay, "DECAY", "-24", "-9"}, {d::Ratio, "RATIO", "1", "4"}, {d::Response, "SPEED", "FAST", "SLOW"}};
namespace l = lowprofile;
constexpr PlateReadout lReadouts[] = {{0, "OUTPUT", "dBFS", "+"}};
constexpr PlateToggle lToggles[] = {{l::OriginalIn, "REAL BASS"}};
constexpr PlateKnob lRow1[] = {{l::Frequency, "FREQUENCY", "32", "256"}, {l::Intensity, "AMOUNT", "-24", "+24", K::Big}, {l::Gain, "GAIN", "OFF", "0"}};
} // namespace lo

// ---- THE BOSS: the needle, order / low cut / limit beside it, dynamics over EQ
namespace bo {
using namespace boss;
constexpr PlateChoice choices[] = {{Meter, "METER"}, {MidMode, "MID"}};
constexpr PlateToggle toggles[] = {{EqIn, "EQ"}, {Phase, "PHASE"}};
constexpr PlateChoice left[] = {{Source, "SOURCE"}};
constexpr PlateToggle leftToggles[] = {{Trim, "TRIM", true}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-18", "+18"}, {Preamp, "PREAMP", "20", "70"}, {High, "HIGH 10K", "-16", "+12"},
                              {MidGain, "MID", "0", "15"}, {MidFreq, "MID FREQ", ".7K", "6K"}, {LowGain, "LOW", "0", "15"},
                              {LowFreq, "LOW FREQ", "OFF", "400"}, {LowCut, "LOW CUT 50", "OFF", "-15"}};
constexpr PlateKnob row2[] = {{Analog, "HUM", "OFF", "60"}, {Noise, "HISS", "FULL", "LOW"}, {Output, "OUTPUT"},
                              {VuCal, "VU ZERO", "8", "24"}};
} // namespace bo

// ---- KINGPIN: three big moves on top, gate and tone under them
namespace kp {
using namespace kingpin;
constexpr PlateChoice choices[] = {{Meter, "METER"}};
constexpr PlateToggle toggles[] = {{Analog, "ANALOG"}};
constexpr PlateChoice steppers[] = {{Type, "TYPE"}};
constexpr PlateKnob row1[] = {{Sens, "PICKUP", "-50", "+50"}, {Compress, "COMPRESS", "0", "100", K::Big}, {Gate, "GATE", "OFF", "+50"},
                              {Fx, "AMBIENCE", "0", "100"}, {Bass, "BASS", "0", "5"}, {Treble, "TREBLE", "0", "5"}, {Output, "OUTPUT", "-50", "+50"}};
} // namespace kp

// ---- GOLD CHAIN: the needle, LINK beside it, the one big knob between in and out
namespace gc {
using namespace goldchain;
constexpr PlateToggle toggles[] = {{Link, "LINK"}, {Analog, "ANALOG"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-18", "+18"}, {Tone, "TONE", "0", "100", K::Big}, {Output, "OUTPUT", "-18", "+18"}};
} // namespace gc

// ---- IVORIES
namespace iv {
using namespace ivories;
constexpr PlateToggle toggles[] = {{DelayOn, "SLAP"}, {DoublerOn, "THICKEN"}, {Analog, "HUM"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-18", "+18"}, {Piano, "VOICING", "DARK", "BRIGHT", K::Big}, {DelayLevel, "SLAP LEVEL", "-24", "0"},
                              {DoublerLevel, "DOUBLER LEVEL", "-24", "0"}, {Output, "OUTPUT", "-18", "+18"}};
} // namespace iv

// ---- HIGH ROLLER: lift across the top, width under it, band by band
namespace hr {
using namespace highroller;
constexpr PlateKnob bandKnobs[] = {{Gain, "GAIN"}, {Width, "WIDTH"}};
constexpr PlateToggle bandToggles[] = {{Solo, "SOLO"}, {Bypass, "BYPASS"}};
constexpr PlateKnob borders[] = {{Cross1, "XOVER"}, {Cross2, "XOVER"}, {Cross3, "XOVER"}, {Cross4, "XOVER"}};
constexpr PlateBands bands{.count = kBands, .first = 0, .stride = kStride, .knobs = bandKnobs, .toggles = bandToggles, .borders = borders, .meter = 0};
constexpr PlateKnob row1[] = {{Input, "INPUT"}, {Direct, "DRY", "OFF", "0"}, {Punch, "SNAP", "0", "5"}, {Output, "OUTPUT"}};
} // namespace hr

// ---- THE MIDDLEMAN: centre and sides read out, one row of moves
namespace mm {
using namespace middleman;
constexpr PlateReadout readouts[] = {{0, "CENTER", "%"}, {1, "OUT L", "dBFS"}, {2, "OUT R", "dBFS"}};
constexpr PlateKnob row1[] = {{Low, "LOW", "CENTER", "SIDES"}, {High, "HIGH", "CENTER", "SIDES"}, {Punch, "SNAP", "CENTER", "SIDES"},
                              {Center, "CENTER", "OFF", "+6", K::Big}, {Sides, "SIDES", "OFF", "+6", K::Big}, {Master, "ALL", "-24", "+6"}};
} // namespace mm

// ---- DOUBLE CROSS
namespace dc {
using namespace doublecross;
constexpr PlateReadout readouts[] = {{0, "PEAK L / M", "dBFS", "+"}, {1, "PEAK R / S", "dBFS", "+"}};
constexpr PlateChoice choices[] = {{Kind, "KIND"}};
constexpr PlateToggle toggles[] = {{PolarityL, "POLARITY L"}, {PolarityR, "POLARITY R"}, {Swap, "SWAP"}, {Reset, "RESET", true}};
constexpr PlateChoice left[] = {{MeterMode, "METER"}};
constexpr PlateKnob row1[] = {{Spread, "SPREAD", "0", "1.2"}, {Freq, "FREQ", "32", "16K"}, {LfSpread, "BASS SPREAD", "0", "2"},
                              {Sweeps, "COMBS", "2", "22"}, {Shape, "CURVE", "TRI", "SQ+"}};
constexpr PlateKnob row2[] = {{FCenter, "SWEEP FOCUS"}, {FDensity, "SWEEP SPACING", "-1", "+1"}, {Tweak, "FINE SHIFT", "-1", "+1"}, {Input, "INPUT", "-24", "0"},
                              {Width, "WIDTH", "MONO", "1"}, {Rotation, "AIM", "L", "R"}};
} // namespace dc

// ---- CUTTHROAT: the curve, the slope beside it, the sweep first
namespace ct {
using namespace cutthroat;
constexpr const char* pageNames[] = {"LEFT", "RIGHT"};
constexpr const char* columnNames[] = {"HIGH-PASS", "LOW-PASS"};
constexpr PlateKnob bandKnobs[] = {{1, "FREQ"}};
constexpr PlateToggle bandToggles[] = {{0, "ON"}};
constexpr PlateBands bands{.count = 2, .first = 0, .stride = 2, .knobs = bandKnobs, .toggles = bandToggles, .pages = 2, .pageStride = kSide,
                           .pageNames = pageNames, .columnNames = columnNames};
constexpr PlateToggle left[] = {{Link, "LINK"}};
} // namespace ct
namespace tvn {
using namespace tunnelvision;
constexpr PlateChoice choices[] = {{Resonance, "RESONANCE"}};
constexpr PlateKnob row1[] = {{Filter, "FILTER", "0", "10", K::Big}};
} // namespace tvn

// ---- FOUNDRY / BOARDROOM: the peak readouts on top, the stepped rings and gains under them
constexpr PlateReadout peakReadouts[] = {{0, "PEAK", "dBFS", "+"}, {1, "TO TOP", "dB", "+"}, {2, "CLIPS", "", "0"}};
namespace fd {
using namespace foundry;
constexpr PlateToggle toggles[] = {{EqIn, "EQ"}, {Phase, "FLIP"}, {Vintage, "VINTAGE"}, {Clear, "CLEAR", true}};
constexpr PlateKnob row1[] = {{LowCut, "LOW CUT", "OFF", "360"}, {LowFreq, "LOWS AT", "OFF", "220"}, {LowGain, "LOWS", "-18", "+18"},
                              {MidFreq, "MIDDLE AT", "OFF", "7.2K"}, {MidGain, "MIDDLE", "-18", "+18"}, {HighFreq, "HIGHS AT", "OFF", "12K"},
                              {HighGain, "HIGHS", "-18", "+18"}, {Output, "OUTPUT", "-18", "+18"}};
} // namespace fd
namespace bdr {
using namespace boardroom;
constexpr PlateToggle toggles[] = {{EqIn, "EQ"}, {Phase, "FLIP"}, {Vintage, "VINTAGE"}, {Clear, "CLEAR", true}};
constexpr PlateToggle leftToggles[] = {{LowBell, "LOWS BELL"}, {LoMidTight, "LO MID TIGHT"}, {HiMidTight, "HI MID TIGHT"}, {HighBell, "HIGHS BELL"}};
constexpr PlateKnob row1[] = {{HighPass, "HIGH-PASS", "OFF", "270"}, {LowPass, "LOW-PASS", "OFF", "3.9K"}, {LowFreq, "LOWS AT", "OFF", "330"},
                              {LowGain, "LOWS", "-18", "+18"}, {LoMidFreq, "LOW MIDS AT", "OFF", "1.2K"}, {LoMidGain, "LOW MIDS", "-18", "+18"}};
constexpr PlateKnob row2[] = {{HiMidFreq, "HIGH MIDS AT", "OFF", "8.2K"}, {HiMidGain, "HIGH MIDS", "-18", "+18"}, {HighFreq, "HIGHS AT", "3.3K", "15K"},
                              {HighGain, "HIGHS", "-18", "+18"}, {Output, "OUTPUT", "-18", "+18"}};
} // namespace bdr
// ---- MIDTOWN: the curve on top, the three pairs of rings and amounts
namespace mt {
using namespace midtown;
constexpr PlateToggle toggles[] = {{EqIn, "EQ"}};
constexpr PlateChoice steppers[] = {{Hum, "HUM"}};
constexpr PlateKnob row1[] = {{LowFreq, "LOW PEAK AT", "200", "1K"}, {LowPeak, "LOW PEAK", "0", "10"}, {DipFreq, "DIP AT", "200", "7K"},
                              {Dip, "DIP", "0", "10"}, {HighFreq, "HIGH PEAK AT", "1.5K", "5K"}, {HighPeak, "HIGH PEAK", "0", "10"},
                              {Output, "OUTPUT", "-18", "+18"}};
} // namespace mt

} // namespace

const PlateSpec kFoundry{.note1 = "THE CONSOLE CHANNEL", .note2 = "IRON AND GLASS", .screen = Screen::Readouts, .readouts = peakReadouts,
                         .toggles = fd::toggles, .row1 = fd::row1,
                         .demo = [](hc::fx::Effect& e) {
                             using namespace foundry;
                             e.set(LowCut, 3); e.set(LowGain, 4); e.set(MidFreq, 5); e.set(MidGain, 3); e.set(HighGain, 2);
                         }};
const PlateSpec kBoardroom{.note1 = "FOUR BANDS", .note2 = "THE BIG DESK", .screen = Screen::Readouts, .readouts = peakReadouts,
                           .toggles = bdr::toggles, .row1 = bdr::row1, .row2 = bdr::row2, .leftToggles = bdr::leftToggles,
                           .demo = [](hc::fx::Effect& e) {
                               using namespace boardroom;
                               e.set(HighPass, 2); e.set(LowGain, 3); e.set(LoMidGain, -4); e.set(HiMidGain, 3); e.set(HiMidTight, 1);
                           }};
const PlateSpec kMidtown{.note1 = "THE MIDDLE", .note2 = "WHERE THE SONG LIVES", .screen = Screen::Curve, .toggles = mt::toggles,
                         .row1 = mt::row1, .steppers = mt::steppers,
                         .demo = [](hc::fx::Effect& e) {
                             using namespace midtown;
                             e.set(LowPeak, 4); e.set(Dip, 5); e.set(DipFreq, 2); e.set(HighPeak, 6); e.set(HighFreq, 3);
                         }};
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
const PlateSpec kLowRider{.note1 = "A NEW LOW OCTAVE", .note2 = "UNDER THE BASS", .screen = Screen::Readouts, .readouts = lo::rReadouts,
                          .toggles = lo::rToggles, .row1 = lo::rRow1,
                          .demo = [](hc::fx::Effect& e) { e.set(lowrider::LowAir, 6); }};
const PlateSpec kUndertow{.note1 = "SUBS FROM THE BASS", .note2 = "ONE OR TWO OCTAVES DOWN", .screen = Screen::Readouts, .readouts = lo::uReadouts,
                          .choices = lo::uChoices, .toggles = lo::uToggles, .row1 = lo::uRow1, .row2 = lo::uRow2,
                          .demo = [](hc::fx::Effect& e) { e.set(undertow::Sub2On, 1); e.set(undertow::Drive, 20); }};
const PlateSpec kDeepPockets{.note1 = "BASS ON SMALL SPEAKERS", .note2 = "HEARD, NOT JUST FELT", .screen = Screen::Readouts, .readouts = lo::dReadouts,
                             .choices = lo::dChoices, .toggles = lo::dToggles, .row1 = lo::dRow1, .row2 = lo::dRow2,
                             .demo = [](hc::fx::Effect& e) { e.set(deeppockets::Original, -6); e.set(deeppockets::Ratio, 2); }};
const PlateSpec kLowProfile{.note1 = "BASS THAT CARRIES", .note2 = "ONE MOVE", .screen = Screen::Readouts, .readouts = lo::lReadouts,
                            .toggles = lo::lToggles, .row1 = lo::lRow1,
                            .demo = [](hc::fx::Effect& e) { e.set(lowprofile::Intensity, 6); }};
const PlateSpec kTheBoss{.note1 = "THE BRITISH DESK", .note2 = "ONE CHANNEL", .screen = Screen::Needle, .choices = bo::choices, .toggles = bo::toggles,
                         .row1 = bo::row1, .row2 = bo::row2, .left = bo::left, .leftToggles = bo::leftToggles, .meterSelect = boss::Meter, .vuMeter = 0,
                         .demo = [](hc::fx::Effect& e) {
                             using namespace boss;
                             e.set(Preamp, 3); e.set(High, 5); e.set(MidGain, 4); e.set(MidFreq, 4); e.set(LowFreq, 2); e.set(LowGain, 3);
                         }};
const PlateSpec kKingpin{.note1 = "DRUMS RUN THE BLOCK", .note2 = "PICK THE DRUM", .screen = Screen::Needle, .choices = kp::choices,
                         .toggles = kp::toggles, .row1 = kp::row1, .steppers = kp::steppers, .reportsReduction = true,
                         .meterSelect = kingpin::Meter, .vuMeter = 0,
                         .demo = [](hc::fx::Effect& e) { using namespace kingpin; e.set(Type, Snare); e.set(Compress, 60); e.set(Fx, 30); e.set(Treble, 2); }};
const PlateSpec kGoldChain{.note1 = "ONE KNOB", .note2 = "RICHER TONE", .screen = Screen::Needle, .toggles = gc::toggles, .row1 = gc::row1,
                           .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) { e.set(goldchain::Tone, 70); }};
const PlateSpec kIvories{.note1 = "THE KEYS", .note2 = "FRONT AND CENTER", .screen = Screen::None, .toggles = iv::toggles, .row1 = iv::row1,
                         .reportsReduction = true,
                         .demo = [](hc::fx::Effect& e) { using namespace ivories; e.set(Piano, 25); e.set(DelayOn, 1); e.set(DoublerOn, 1); }};
const PlateSpec kHighRoller{.note1 = "EVERY BAND", .note2 = "RICHER AND WIDER", .screen = Screen::None, .row1 = hr::row1, .bands = &hr::bands,
                            .demo = [](hc::fx::Effect& e) {
                                using namespace highroller;
                                e.set(band(2, Gain), 20); e.set(band(3, Width), 1.6f); e.set(band(4, Gain), 14);
                            }};
const PlateSpec kMiddleman{.note1 = "THE MIDDLE", .note2 = "AND THE SIDES", .screen = Screen::Readouts, .readouts = mm::readouts, .row1 = mm::row1,
                           .demo = [](hc::fx::Effect& e) { using namespace middleman; e.set(Center, -8); e.set(Low, 70); e.set(Punch, 40); }};
const PlateSpec kDoubleCross{.note1 = "ONE IN", .note2 = "TWO OUT", .screen = Screen::Readouts, .readouts = dc::readouts, .choices = dc::choices,
                             .toggles = dc::toggles, .row1 = dc::row1, .row2 = dc::row2, .left = dc::left,
                             .demo = [](hc::fx::Effect& e) { e.set(doublecross::Sweeps, 10); e.set(doublecross::FDensity, 0.3f); }};
const PlateSpec kCutthroat{.note1 = "HIGH-PASS, LOW-PASS", .note2 = "LEFT AND RIGHT", .screen = Screen::Curve, .leftToggles = ct::left,
                           .bands = &ct::bands,
                           .demo = [](hc::fx::Effect& e) { using namespace cutthroat; e.set(HpFreq, 120); e.set(LpFreq, 9000); }};
const PlateSpec kTunnelVision{.note1 = "ONE KNOB", .note2 = "CLOSE IT DOWN", .screen = Screen::Curve, .choices = tvn::choices, .row1 = tvn::row1,
                              .demo = [](hc::fx::Effect& e) { e.set(tunnelvision::Filter, 5.5f); e.set(tunnelvision::Resonance, 2); }};

} // namespace hem::rack::plates
