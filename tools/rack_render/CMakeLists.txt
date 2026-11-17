# hem_rack_render — the rack plug-ins offline on WAV files (no window, no sound card).
# Used by tools/suno_repair to run the real HEM DSP from a repair manifest.
add_executable(hem_rack_render main.cpp)
target_link_libraries(hem_rack_render PRIVATE hem::rackfx hc::media hc::build_flags)
