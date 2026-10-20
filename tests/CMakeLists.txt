# =============================================================================
#  hem_rack_tests — DSP, faceplates (no GPU) and the rack app's audio side (no sound card).
# =============================================================================
add_executable(hem_rack_tests
    main.cpp
    AllocTracker.cpp
    rackfx_tests.cpp
    arsenal_tests.cpp tone_tests.cpp hideout_tests.cpp streets_tests.cpp
    face_tests.cpp
    rackapp_tests.cpp)
target_include_directories(hem_rack_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hem_rack_tests PRIVATE hem::rackfx hem::rackui hem_rackapp_core hc::media)
hem_copy_assets(hem_rack_tests)
add_test(NAME hem_rack_tests COMMAND hem_rack_tests)
