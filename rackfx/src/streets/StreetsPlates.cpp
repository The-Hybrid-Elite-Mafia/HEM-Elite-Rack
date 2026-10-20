// What's on each streets plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/Streets.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

namespace to {
using namespace torch;
constexpr PlateChoice choices[] = {{Model, "MODEL"}, {Cabinet, "CABINET"}};
constexpr PlateKnob row1[] = {{Gain, "GAIN", "0", "100", K::Big}, {Bass, "BASS"}, {P::Mid, "MID"}, {Treble, "TREBLE"}, {Presence, "PRESENCE"}};
constexpr PlateKnob row2[] = {{Gate, "GATE", "", "", K::Small}, {Room, "ROOM", "", "", K::Small}, {Master, "MASTER", "", "", K::Small}};
} // namespace to

namespace rk {
using namespace racket;
constexpr PlateChoice choices[] = {{Type, "TYPE"}};
constexpr PlateKnob row1[] = {{Drive, "DRIVE", "0", "100", K::Big}, {Tone, "TONE", "DARK", "BRIGHT"}, {Level, "LEVEL"}, {Mix, "MIX", "DRY", "WET"}};
} // namespace rk

namespace sn {
using namespace snitch;
constexpr PlateReadout readouts[] = {{0, "NOTE", "", "note"}, {1, "CENTS", "OFF", "+"}, {2, "PITCH", "Hz"}};
constexpr PlateToggle toggles[] = {{Mute, "MUTE"}};
constexpr PlateKnob row1[] = {{Reference, "REFERENCE A", "430", "450"}};
} // namespace sn

namespace pr {
using namespace promotion;
constexpr PlateChoice choices[] = {{Character, "CHARACTER"}};
constexpr PlateKnob row1[] = {{Shift, "SHIFT", "-12", "+12", K::Big}, {Fine, "FINE", "-100", "+100"}, {Mix, "MIX", "DRY", "WET"}, {Output, "OUTPUT"}};
} // namespace pr

namespace wg {
using namespace wiseguys;
constexpr PlateKnob row1[] = {{Interval1, "VOICE 1"}, {Level1, "LEVEL 1"}, {Pan1, "PAN 1"}, {Interval2, "VOICE 2"}, {Level2, "LEVEL 2"}, {Pan2, "PAN 2"}};
constexpr PlateKnob row2[] = {{Interval3, "VOICE 3"}, {Level3, "LEVEL 3"}, {Pan3, "PAN 3"}, {Dry, "DRY"}, {Output, "OUTPUT"}};
} // namespace wg

namespace mm2 {
using namespace mademan;
constexpr PlateReadout readouts[] = {{0, "SUNG", "", "note"}, {1, "PULL", "CENTS", "+"}};
constexpr PlateChoice choices[] = {{Scale, "SCALE"}};
constexpr PlateChoice steppers[] = {{Key, "KEY"}};
constexpr PlateKnob row1[] = {{Speed, "SPEED", "HARD", "SOFT", K::Big}, {Amount, "AMOUNT"}, {Mix, "MIX", "DRY", "WET"}};
} // namespace mm2

namespace dt {
using namespace detective;
constexpr PlateReadout readouts[] = {{0, "KEY", "", "key"}, {1, "SURE", "%", "0"}};
constexpr PlateChoice choices[] = {{P::Listen, "LISTEN"}};
constexpr PlateToggle toggles[] = {{Reset, "RESET"}};
} // namespace dt

namespace wi {
using namespace witness;
constexpr PlateReadout readouts[] = {{0, "LEFT", "VU", "+"}, {1, "RIGHT", "VU", "+"}, {2, "PEAK LEFT", "dBFS"}, {3, "PEAK RIGHT", "dBFS"}};
constexpr PlateKnob row1[] = {{Calibration, "0 VU AT", "-24", "-8"}};
constexpr PlateChoice left[] = {{Hold, "PEAK HOLD"}};
} // namespace wi

namespace lw {
using namespace livewire;
constexpr PlateChoice choices[] = {{Mode, "MODE"}};
constexpr PlateKnob row1[] = {{Freq, "FREQ"}, {Level, "LEVEL", "-60", "0", K::Big}, {SweepTime, "SWEEP TIME"}};
constexpr PlateChoice steppers[] = {{Wave, "WAVE"}, {Channels, "SIDES"}};
constexpr PlateToggle leftT[] = {{On, "TONE ON"}};
} // namespace lw

} // namespace

const PlateSpec kTorch{.note1 = "LIGHT IT UP", .note2 = "AMP AND CABINET", .screen = Screen::None, .choices = to::choices,
                       .row1 = to::row1, .row2 = to::row2,
                       .demo = [](hc::fx::Effect& e) { using namespace torch; e.set(Model, 2); e.set(Gain, 60); }};
const PlateSpec kRacket{.note1 = "MAKE SOME NOISE", .note2 = "STEP ON IT", .screen = Screen::None, .choices = rk::choices, .row1 = rk::row1,
                        .demo = [](hc::fx::Effect& e) { using namespace racket; e.set(Type, 2); e.set(Drive, 70); }};
const PlateSpec kSnitch{.note1 = "TELLS YOU", .note2 = "WHEN YOU'RE OFF", .screen = Screen::Readouts, .readouts = sn::readouts,
                        .toggles = sn::toggles, .row1 = sn::row1, .demo = nullptr};
const PlateSpec kPromotion{.note1 = "MOVE IT UP", .note2 = "OR DOWN", .screen = Screen::None, .choices = pr::choices, .row1 = pr::row1,
                           .demo = [](hc::fx::Effect& e) { e.set(promotion::Shift, 5); }};
const PlateSpec kWiseguys{.note1 = "BACKUP SINGERS", .note2 = "ON CALL", .screen = Screen::None, .row1 = wg::row1, .row2 = wg::row2,
                          .demo = [](hc::fx::Effect& e) { using namespace wiseguys; e.set(Level3, 40); }};
const PlateSpec kMadeMan{.note1 = "EVERY NOTE", .note2 = "FALLS IN LINE", .screen = Screen::Readouts, .readouts = mm2::readouts,
                         .choices = mm2::choices, .row1 = mm2::row1, .steppers = mm2::steppers,
                         .demo = [](hc::fx::Effect& e) { using namespace mademan; e.set(Key, 9); e.set(Scale, 2); }};
const PlateSpec kDetective{.note1 = "WHAT KEY", .note2 = "IS IT IN?", .screen = Screen::Readouts, .readouts = dt::readouts,
                           .choices = dt::choices, .toggles = dt::toggles, .demo = nullptr};
const PlateSpec kWitness{.note1 = "SAW EVERYTHING", .note2 = "SAYS IT STRAIGHT", .screen = Screen::Readouts, .readouts = wi::readouts,
                         .row1 = wi::row1, .left = wi::left, .demo = nullptr};
const PlateSpec kLiveWire{.note1 = "TEST TONES", .note2 = "SWEEPS AND NOISE", .screen = Screen::None, .choices = lw::choices,
                          .row1 = lw::row1, .leftToggles = lw::leftT, .steppers = lw::steppers,
                          .demo = [](hc::fx::Effect& e) { e.set(livewire::Wave, 6); }};

} // namespace hem::rack::plates
