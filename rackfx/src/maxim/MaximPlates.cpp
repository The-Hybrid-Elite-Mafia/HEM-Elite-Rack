// What's on each maximizer's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/Maximizers.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

// ---- BIG SHOT: the needle, what it took off and the input beside it, two big knobs
namespace bs {
using namespace bigshot;
constexpr PlateReadout readouts[] = {{0, "MOST OFF", "dB", "+"}, {1, "INPUT PEAK", "dBFS"}};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-30", "0", K::Big}, {Ceiling, "CEILING", "-30", "0", K::Big}};
constexpr PlateToggle leftT[] = {{AutoRelease, "AUTO REL"}};
constexpr PlateChoice steppers[] = {{Quantize, "QUANTIZE"}, {Dither, "DITHER"}, {Shaping, "SHAPING"}};
} // namespace bs

// ---- LAST WORD
namespace lw {
using namespace lastword;
constexpr PlateReadout readouts[] = {{0, "IN LEFT", "dBFS"}, {1, "IN RIGHT", "dBFS"}};
constexpr PlateChoice choices[] = {{Domain, "DOMAIN"}};
constexpr PlateKnob row1[] = {{InputL, "INPUT L", "-60", "0", K::Small}, {InputR, "INPUT R", "-60", "0", K::Small}, {Threshold, "THRESHOLD", "-30", "0", K::Big},
                              {Ceiling, "CEILING", "-30", "0"}, {Release, "RELEASE"}};
constexpr PlateToggle leftT[] = {{InputLink, "INPUT LINK"}, {AutoRelease, "AUTO REL"}};
constexpr PlateChoice steppers[] = {{Quantize, "QUANTIZE"}, {Dither, "DITHER"}, {Shaping, "SHAPING"}};
} // namespace lw

// ---- HIGH STAKES / FAST MONEY: five band columns, the crossovers between them
namespace mm {
using namespace multimax;
constexpr PlateKnob bandKnobs[] = {{Gain, "GAIN"}, {Priority, "PRIORITY"}, {BandRelease, "RELEASE"}};
constexpr PlateToggle bandToggles[] = {{BandLink, "LINK"}, {Solo, "SOLO"}};
constexpr PlateKnob borders[] = {{X1, "SPLIT 1"}, {X2, "SPLIT 2"}, {X3, "SPLIT 3"}, {X4, "SPLIT 4"}};
constexpr PlateBands bands{.count = kBands, .first = Band0, .stride = kPerBand, .knobs = bandKnobs, .toggles = bandToggles,
                           .borders = borders, .meter = 0};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD"}, {Ceiling, "CEILING"}, {Release, "RELEASE"}, {Separation, "SEPARATION"}};
constexpr PlateToggle toggles[] = {{Link, "LINK"}};
constexpr PlateChoice steppers[] = {{ReleaseType, "RELEASE"}, {Quantize, "QUANTIZE"}, {Dither, "DITHER"}, {Shaping, "SHAPING"}};
void demo(hc::fx::Effect& e) {
    e.set(Threshold, -8);
    e.set(band(0, Priority), 3);
}
} // namespace mm

// ---- TOP DOG / QUICK CASH
namespace ud {
using namespace ultramax;
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD", "-30", "0", K::Big}, {Ceiling, "CEILING", "-30", "0", K::Big}, {Release, "RELEASE"}};
constexpr PlateToggle toggles[] = {{Link, "LINK"}};
constexpr PlateChoice steppers[] = {{Profile, "PROFILE"}, {Quantize, "QUANTIZE"}, {Dither, "DITHER"}, {Shaping, "SHAPING"}};
} // namespace ud

// ---- SWEET SIXTEEN: six EQ / priority columns
namespace s16 {
using namespace sixteen;
constexpr PlateKnob bandKnobs[] = {{Gain, "GAIN"}, {Priority, "PRIORITY"}, {Freq, "FREQ"}, {Q, "Q"}};
constexpr PlateChoice bandSteppers[] = {{Type, "TYPE"}};
constexpr PlateToggle bandToggles[] = {{BandLink, "LINK"}, {In, "IN"}};
constexpr PlateBands bands{.count = kEqBands, .first = Band0, .stride = kPerBand, .knobs = bandKnobs, .steppers = bandSteppers,
                           .toggles = bandToggles, .meter = 0};
constexpr PlateKnob row1[] = {{Threshold, "THRESHOLD"}, {Ceiling, "CEILING"}, {Release, "RELEASE"}};
constexpr PlateToggle toggles[] = {{Link, "LINK"}};
constexpr PlateChoice steppers[] = {{Character, "CHARACTER"}, {Separation, "SEPARATION"}, {Quantize, "QUANTIZE"}, {Dither, "DITHER"}, {Shaping, "SHAPING"}};
void demo(hc::fx::Effect& e) {
    e.set(Threshold, -8);
    e.set(band(2, Gain), 3);
    e.set(band(2, Priority), 3);
}
} // namespace s16

} // namespace

const PlateSpec kBigShot{.note1 = "LOUD AS YOU LIKE", .note2 = "NEVER OVER THE TOP", .screen = Screen::Needle, .readouts = bs::readouts, .row1 = bs::row1,
                         .leftToggles = bs::leftT, .steppers = bs::steppers, .reportsReduction = true,
                         .demo = [](hc::fx::Effect& e) { e.set(bigshot::Threshold, -8); }};
const PlateSpec kLastWord{.note1 = "THE PEAK STOPS", .note2 = "RIGHT HERE", .screen = Screen::Needle, .readouts = lw::readouts, .choices = lw::choices,
                          .row1 = lw::row1, .leftToggles = lw::leftT, .steppers = lw::steppers, .reportsReduction = true,
                          .demo = [](hc::fx::Effect& e) { e.set(lastword::Threshold, -8); e.set(lastword::Domain, 2); }};
const PlateSpec kHighStakes{.note1 = "FIVE BANDS", .note2 = "ALL IN", .toggles = mm::toggles, .row1 = mm::row1, .steppers = mm::steppers,
                            .bands = &mm::bands, .reportsReduction = true, .demo = &mm::demo};
const PlateSpec kFastMoney{.note1 = "FIVE BANDS", .note2 = "NO WAITING", .toggles = mm::toggles, .row1 = mm::row1, .steppers = mm::steppers,
                           .bands = &mm::bands, .reportsReduction = true, .demo = &mm::demo};
const PlateSpec kTopDog{.note1 = "PICK A PROFILE", .note2 = "TAKE THE TOP", .screen = Screen::Needle, .toggles = ud::toggles, .row1 = ud::row1,
                        .steppers = ud::steppers, .reportsReduction = true,
                        .demo = [](hc::fx::Effect& e) { e.set(ultramax::Threshold, -8); }};
const PlateSpec kQuickCash{.note1 = "PICK A PROFILE", .note2 = "NO WAITING", .screen = Screen::Needle, .toggles = ud::toggles, .row1 = ud::row1,
                           .steppers = ud::steppers, .reportsReduction = true,
                           .demo = [](hc::fx::Effect& e) { e.set(ultramax::Threshold, -8); }};
const PlateSpec kSweetSixteen{.note1 = "SIXTEEN BANDS", .note2 = "SIX TO STEER", .toggles = s16::toggles, .row1 = s16::row1, .steppers = s16::steppers,
                              .bands = &s16::bands, .reportsReduction = true, .demo = &s16::demo};

} // namespace hem::rack::plates
