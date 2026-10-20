# =============================================================================
#  HEMEliteRack.exe — the standalone rack (GUI subsystem).
#  hem_rackapp_core — its audio side (chain, A/B, transport, export render), shared with the tests.
# =============================================================================
add_library(hem_rackapp_core STATIC src/RackEngine.cpp src/Chain.cpp src/TakeWriter.cpp)
target_include_directories(hem_rackapp_core PUBLIC src)
target_link_libraries(hem_rackapp_core PUBLIC hem::rackfx hc::engine)

configure_file(resources/HEMEliteRack.rc.in "${CMAKE_CURRENT_BINARY_DIR}/HEMEliteRack.rc" @ONLY)
add_executable(HEMEliteRack WIN32
    src/main.cpp
    src/RackHost.cpp
    src/RackView.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/HEMEliteRack.rc")
target_link_libraries(HEMEliteRack PRIVATE hem_rackapp_core hem::rackui hc::io hc::media hc::params opengl32 shell32 ole32 winmm)
target_compile_definitions(HEMEliteRack PRIVATE HEM_RACK_VERSION_STR="${PROJECT_VERSION}")
set_target_properties(HEMEliteRack PROPERTIES OUTPUT_NAME "HEMEliteRack")
hem_copy_assets(HEMEliteRack)
