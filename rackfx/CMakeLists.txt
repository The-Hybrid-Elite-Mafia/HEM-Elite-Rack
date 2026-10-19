# =============================================================================
#  hem::rackfx — the HEM-Elite-Rack plug-ins (ELITE EQ, RESTORER, CONSOLE EQ, BUS GLUE, TAPE, TUBE EQ, VARI-MU, OPTO, FET, LIMITER, DE-ESSER, STEREO WIDTH, CLIPPER, EXCITER, TRANSIENT, LOUDNESS METER, BRIT PRE, AMERICAN EQ, MID EQ, AIR EQ, GRAPHIC EQ, TILT, VCA COMP, PUNCH 160, THRUST BUS, BRIT BUS, GATE, MULTIBAND, TUBE SAT, PREAMP, CASSETTE, VINYL, BIT CRUSHER, TRANSFORMER, HALL, ROOM, PLATE, SPRING, TAPE ECHO, DIGITAL DELAY, CHORUS, FLANGER, PHASER, TREMOLO, ROTARY, AUTO FILTER, SUB, BASS TIGHTENER, VOCAL STRIP) and their analysis.
#  Built on The Hybrid Core's hc::fx effect interface: the same objects run in the
#  rack app, in the VST3 bundle and in the tests.
# =============================================================================
add_library(hem_rackfx STATIC
    src/Spectrum.cpp
    src/EliteEq.cpp
    src/Restorer.cpp
    src/ConsoleEq.cpp
    src/BusGlue.cpp
    src/Tape.cpp
    src/TubeEq.cpp
    src/VariMu.cpp
    src/Opto.cpp
    src/Fet.cpp
    src/Limiter.cpp
    src/Tools.cpp
    src/LoudnessMeter.cpp
    src/ClassicEqs.cpp
    src/MoreDynamics.cpp
    src/ColorFx.cpp
    src/Space.cpp
    src/Movement.cpp
    src/Specialty.cpp
    src/Plugins.cpp)
add_library(hem::rackfx ALIAS hem_rackfx)
target_include_directories(hem_rackfx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hem_rackfx PUBLIC hc::build_flags hc::fx)
