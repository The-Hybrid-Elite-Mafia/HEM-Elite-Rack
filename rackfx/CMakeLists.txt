# =============================================================================
#  hem::rackfx — the HEM-Elite-Rack plug-ins (SURGEON, THE CLEANER, BOARDROOM, THE FAMILY, WIRETAP, ARCHIVE, VELVET, SMOKE, LANTERN, TRIGGER, LOCKDOWN, HUSH MONEY, WIDE LOAD, RAZOR, SPARK, SUCKER PUNCH, THE ACCOUNTANT, FOUNDRY, KNUCKLES, MIDTOWN, PENTHOUSE, BLUEPRINT, LEVERAGE, ENFORCER, HEAVY HAND, MUSCLE, UNDERBOSS, BOUNCER, TRIAD, FURNACE, FRONT MAN, MIXTAPE, CRATE DIGGER, SHREDDER, ANVIL, CATHEDRAL, BACKROOM, CHROME, COIL, REPEAT OFFENDER, THE CREW, GETAWAY, SMOKESCREEN, SHAKEDOWN, CYCLONE, SHAPESHIFTER, UNDERGROUND, CHOKEHOLD, SPOTLIGHT) and their analysis.
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
