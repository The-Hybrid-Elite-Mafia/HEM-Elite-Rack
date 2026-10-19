add_executable(hem_rack_snapshot main.cpp)
target_link_libraries(hem_rack_snapshot PRIVATE hem::rackui hem::rackfx opengl32)
hem_copy_assets(hem_rack_snapshot)
