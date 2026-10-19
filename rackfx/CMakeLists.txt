# =============================================================================
#  hem::rackfx — the HEM-Elite-Rack plug-ins (ELITE EQ, RESTORER, CONSOLE EQ, BUS GLUE) and their analysis.
#  Built on The Hybrid Core's hc::fx effect interface: the same objects run in the
#  rack app, in the VST3 bundle and in the tests.
# =============================================================================
add_library(hem_rackfx STATIC
    src/Spectrum.cpp
    src/EliteEq.cpp
    src/Restorer.cpp
    src/ConsoleEq.cpp
    src/BusGlue.cpp
    src/Plugins.cpp)
add_library(hem::rackfx ALIAS hem_rackfx)
target_include_directories(hem_rackfx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hem_rackfx PUBLIC hc::build_flags hc::fx)
