// What's on each streets plug-in's plate (Face::Spec), and the settings its CI screenshot uses.
#include "../arsenal/Plates.hpp"

#include "hem/rack/Streets.hpp"
#include "hem/rack/Garage.hpp"

namespace hem::rack::plates {
namespace {
using K = KnobSize;

namespace to {
using namespace torch;
constexpr PlateToggle toggles[] = {{AmpOn, "AMP"}, {PhaseB, "PHASE B"}};
constexpr PlateChoice steppers[] = {{Type, "TYPE"}, {Routing, "ROUTING"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-12", "+12"}, {P::Drive, "DRIVE", "0", "10", K::Big}, {Bass, "BASS", "0", "10"}, {P::Mid, "MID", "0", "10"},
                              {Treble, "TREBLE", "0", "10"}, {Presence, "PRESENCE", "0", "10"}, {Master, "MASTER", "0", "10"}, {Output, "OUTPUT", "-12", "+12"}};
constexpr PlateKnob row2[] = {{CabA, "CAB A", "", "", K::Small}, {MicA, "MIC A", "", "", K::Small}, {VolA, "VOLUME A", "0", "10", K::Small},
                              {PanA, "PAN A", "L", "R", K::Small}, {CabB, "CAB B", "", "", K::Small}, {MicB, "MIC B", "", "", K::Small},
                              {VolB, "VOLUME B", "0", "10", K::Small}, {PanB, "PAN B", "L", "R", K::Small}};
} // namespace to

namespace hw {
using namespace hotwire;
constexpr PlateReadout readouts[] = {{0, "INPUT", "dBFS", "+"}, {1, "OUTPUT", "dBFS", "+"}, {2, "CLIPS", "", "0"}};
constexpr PlateToggle toggles[] = {{CabOff, "CAB OFF"}, {Clear, "CLEAR", true}};
constexpr PlateChoice steppers[] = {{Amp, "AMP"}, {Channel, "CHANNEL"}, {Cabinet, "CABINET"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-12", "+12"}, {Gain, "GAIN", "0", "10", K::Big}, {Bass, "BASS", "0", "10"}, {Middle, "MIDDLE", "0", "10"},
                              {Treble, "TREBLE", "0", "10"}, {Volume, "VOLUME", "0", "10"}, {Output, "OUTPUT", "-12", "+12"}};
constexpr PlateKnob row2[] = {{Focus, "FOCUS", "OFF", "10", K::Small}, {Gate, "GATE", "OFF", "0", K::Small}, {GateSpeed, "GATE SPEED", "0", "10", K::Small},
                              {Depth, "DEPTH", "0", "10", K::Small}, {Presence, "PRESENCE", "0", "10", K::Small}, {CabTone, "CAB TONE", "DARK", "BRIGHT", K::Small},
                              {Room, "ROOM", "OFF", "100", K::Small}};
} // namespace hw
namespace bf {
using namespace bottomfeeder;
constexpr PlateReadout readouts[] = {{0, "INPUT", "dBFS", "+"}, {1, "OUTPUT", "dBFS", "+"}, {2, "CLIPS", "", "0"}};
constexpr PlateToggle toggles[] = {{DiFlip, "DI FLIP"}, {CabOff, "CAB OFF"}, {Clear, "CLEAR", true}};
constexpr PlateChoice steppers[] = {{Amp, "AMP"}, {DriveType, "DRIVE"}, {Voice, "VOICE"}};
constexpr PlateKnob row1[] = {{Input, "INPUT", "-12", "+12"}, {Gain, "GAIN", "0", "10", K::Big}, {Bass, "BASS", "0", "10"}, {Mid, "MID", "0", "10"},
                              {HiMid, "HI MID", "0", "10"}, {High, "HIGH", "0", "10"}, {Output, "OUTPUT", "-12", "+12"}};
constexpr PlateKnob row2[] = {{DiTone, "DI TONE", "0", "10", K::Small}, {DiLevel, "DI LEVEL", "0", "10", K::Small}, {Blend, "BLEND", "AMP", "DI", K::Small},
                              {DriveTone, "DRIVE TONE", "0", "10", K::Small}, {DriveGain, "DRIVE GAIN", "0", "10", K::Small},
                              {DriveMix, "DRIVE MIX", "0", "10", K::Small}, {CabTone, "CAB TONE", "DARK", "BRIGHT", K::Small}, {Room, "ROOM", "OFF", "100", K::Small}};
} // namespace bf

namespace rk {
using namespace racket;
constexpr PlateChoice choices[] = {{Type, "TYPE"}};
constexpr PlateKnob row1[] = {{Drive, "DRIVE", "0", "100", K::Big}, {Tone, "TONE", "DARK", "BRIGHT"}, {Level, "LEVEL"}, {Mix, "MIX", "DRY", "WET"}};
} // namespace rk

namespace sn {
using namespace snitch;
constexpr PlateReadout readouts[] = {{0, "NOTE", "", "note"}, {1, "CENTS", "OFF", "+"}, {2, "PITCH", "Hz"}};
constexpr PlateChoice steppers[] = {{Mode, "MODE"}};
constexpr PlateKnob row1[] = {{Reference, "REFERENCE A", "-6", "+6"}};
constexpr PlateToggle noteToggles[] = {{0, "ON"}};
constexpr const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr PlateBands notes{.count = 12, .first = Note0, .stride = 1, .toggles = noteToggles, .columnNames = noteNames};
} // namespace sn

namespace pr {
using namespace promotion;
constexpr PlateChoice choices[] = {{Entry, "SET BY"}, {P::Mode, "MODE"}};
constexpr PlateKnob row1[] = {{Semitones, "SEMITONES", "-12", "+12", K::Big}, {Cents, "CENTS", "0", "99"}, {FromHz, "FROM", "20", "20K"},
                              {ToHz, "TO", "20", "20K"}};
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
constexpr PlateReadout readouts[] = {{0, "KEY", "", "key"}, {1, "OR", "", "key"}, {2, "OR", "", "key"}, {3, "TUNING", "Hz", "+"}};
constexpr PlateChoice choices[] = {{Spell, "SPELL"}};
constexpr PlateToggle toggles[] = {{Detect, "DETECT", true}, {Edit, "EDIT"}};
constexpr PlateChoice steppers[] = {{Root, "ROOT"}, {P::Scale, "SCALE"}};
} // namespace dt

namespace wi {
using namespace witness;
constexpr PlateReadout readouts[] = {{0, "LEFT", "VU", "+"}, {1, "RIGHT", "VU", "+"}, {2, "CLIP LEFT", "", "0"}, {3, "CLIP RIGHT", "", "0"}};
constexpr PlateToggle toggles[] = {{Clear, "CLEAR", true}};
constexpr PlateKnob row1[] = {{Headroom, "HEADROOM", "4", "26"}, {DisplayDelay, "DISPLAY DELAY", "0", "1000"}};
} // namespace wi

namespace lw {
using namespace livewire;
constexpr PlateChoice choices[] = {{Type, "TYPE"}, {Send, "SEND"}};
constexpr PlateToggle toggles[] = {{Flip, "FLIP"}};
constexpr PlateKnob row1[] = {{Freq, "FREQ", "20", "21K", K::Big}, {Gain, "GAIN", "-120", "0", K::Big}};
constexpr PlateChoice steppers[] = {{QuickFreq, "QUICK FREQ"}, {QuickGain, "QUICK GAIN"}};
constexpr PlateToggle leftT[] = {{On, "POWER"}};
} // namespace lw

} // namespace

const PlateSpec kTorch{.note1 = "LIGHT IT UP", .note2 = "AMP AND TWO CABINETS", .screen = Screen::None, .toggles = to::toggles,
                       .row1 = to::row1, .row2 = to::row2, .steppers = to::steppers,
                       .demo = [](hc::fx::Effect& e) { using namespace torch; e.set(Type, Crunch); e.set(Drive, 7); e.set(VolB, 4); }};
const PlateSpec kHotWire{.note1 = "THE HEAD", .note2 = "AND THE BOX", .screen = Screen::Readouts, .readouts = hw::readouts, .toggles = hw::toggles,
                         .row1 = hw::row1, .row2 = hw::row2, .steppers = hw::steppers,
                         .demo = [](hc::fx::Effect& e) { e.set(hotwire::Gain, 7); e.set(hotwire::Room, 30); }};
const PlateSpec kBottomFeeder{.note1 = "THE LOW END", .note2 = "AMP AND DI", .screen = Screen::Readouts, .readouts = bf::readouts, .toggles = bf::toggles,
                              .row1 = bf::row1, .row2 = bf::row2, .steppers = bf::steppers,
                              .demo = [](hc::fx::Effect& e) { e.set(bottomfeeder::DriveType, 1); e.set(bottomfeeder::DriveMix, 4); }};
const PlateSpec kRacket{.note1 = "MAKE SOME NOISE", .note2 = "STEP ON IT", .screen = Screen::None, .choices = rk::choices, .row1 = rk::row1,
                        .demo = [](hc::fx::Effect& e) { using namespace racket; e.set(Type, 2); e.set(Drive, 70); }};
const PlateSpec kSnitch{.note1 = "TELLS YOU", .note2 = "WHEN YOU'RE OFF", .screen = Screen::Readouts, .readouts = sn::readouts,
                        .row1 = sn::row1, .steppers = sn::steppers, .bands = &sn::notes,
                        .demo = [](hc::fx::Effect& e) { e.set(snitch::Mode, snitch::MyNotes); }};
const PlateSpec kPromotion{.note1 = "MOVE IT UP", .note2 = "OR DOWN", .screen = Screen::None, .choices = pr::choices, .row1 = pr::row1,
                           .demo = [](hc::fx::Effect& e) { e.set(promotion::Semitones, 17); }};
const PlateSpec kWiseguys{.note1 = "BACKUP SINGERS", .note2 = "ON CALL", .screen = Screen::None, .row1 = wg::row1, .row2 = wg::row2,
                          .demo = [](hc::fx::Effect& e) { using namespace wiseguys; e.set(Level3, 40); }};
const PlateSpec kMadeMan{.note1 = "EVERY NOTE", .note2 = "FALLS IN LINE", .screen = Screen::Readouts, .readouts = mm2::readouts,
                         .choices = mm2::choices, .row1 = mm2::row1, .steppers = mm2::steppers,
                         .demo = [](hc::fx::Effect& e) { using namespace mademan; e.set(Key, 9); e.set(Scale, 2); }};
const PlateSpec kDetective{.note1 = "WHAT KEY", .note2 = "IS IT IN?", .screen = Screen::Readouts, .readouts = dt::readouts,
                           .choices = dt::choices, .toggles = dt::toggles, .steppers = dt::steppers, .demo = nullptr};
const PlateSpec kWitness{.note1 = "SAW EVERYTHING", .note2 = "SAYS IT STRAIGHT", .screen = Screen::Readouts, .readouts = wi::readouts,
                         .toggles = wi::toggles, .row1 = wi::row1, .demo = nullptr};
const PlateSpec kLiveWire{.note1 = "TEST SIGNALS", .note2 = "PINK, WHITE, SINE", .screen = Screen::None, .choices = lw::choices, .toggles = lw::toggles,
                          .row1 = lw::row1, .leftToggles = lw::leftT, .steppers = lw::steppers,
                          .demo = [](hc::fx::Effect& e) { e.set(livewire::Type, livewire::Sine); }};

} // namespace hem::rack::plates
