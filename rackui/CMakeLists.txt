# =============================================================================
#  hem::rackui — the HEM-Elite-Rack faceplates (teal / gunmetal, crown and ghost):
#  layout, CPU plate, live drawing (hc::ui Renderer), mouse handling, plug-in window.
# =============================================================================
add_library(hem_rackui STATIC
    src/FaceLayouts.cpp
    src/RackPlate.cpp
    src/FaceView.cpp
    src/FaceController.cpp
    src/EffectFeed.cpp
    src/FaceWindow.cpp)
add_library(hem::rackui ALIAS hem_rackui)
target_include_directories(hem_rackui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(hem_rackui PUBLIC hem::rackfx hc::ui PRIVATE opengl32 user32)
