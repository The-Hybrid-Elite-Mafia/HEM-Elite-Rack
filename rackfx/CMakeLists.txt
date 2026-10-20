# =============================================================================
#  hem::rackfx — the HEM-Elite-Rack plug-ins and their analysis.
#  Built on The Hybrid Core's hc::fx effect interface: the same objects run in the
#  rack app, in the VST3 bundle and in the tests.
# =============================================================================
add_library(hem_rackfx STATIC
    src/Spectrum.cpp
    src/EliteEq.cpp
    src/Restorer.cpp
    src/BusGlue.cpp
    src/Tape.cpp
    src/TubeEq.cpp
    src/Limiter.cpp
    src/Tools.cpp
    src/LoudnessMeter.cpp
    src/MoreDynamics.cpp
    src/Hitman.cpp
    src/ColorFx.cpp
    src/Space.cpp
    src/Movement.cpp
    src/Specialty.cpp
    src/arsenal/Switchblade.cpp
    src/arsenal/Territory.cpp
    src/arsenal/Consigliere.cpp
    src/arsenal/Heavyweight.cpp
    src/arsenal/TheDon.cpp
    src/arsenal/SlowBurn.cpp
    src/arsenal/IronFist.cpp
    src/arsenal/Knockout.cpp
    src/arsenal/FullHouse.cpp
    src/arsenal/Wheelman.cpp
    src/arsenal/BigShot.cpp
    src/arsenal/Silencer.cpp
    src/arsenal/Breathless.cpp
    src/arsenal/TommyGun.cpp
    src/arsenal/Plates.cpp
    src/Plugins.cpp)
add_library(hem::rackfx ALIAS hem_rackfx)
target_include_directories(hem_rackfx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hem_rackfx PUBLIC hc::build_flags hc::fx)
