# =============================================================================
#  HEM-Elite-Rack.vst3 — every HEM-Elite-Rack plug-in as VST3 with their faceplates.
#  Steinberg VST3 SDK v3.8.1 (MIT licence) is fetched at configure time, never committed.
#  VSTGUI and the SDK examples are not fetched / built. Steinberg's validator runs after linking.
# =============================================================================
include(FetchContent)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT        OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK           OFF CACHE BOOL "" FORCE)   # no link into Common Files (needs admin)
set(SMTG_RUN_VST_VALIDATOR            ON  CACHE BOOL "" FORCE)
set(SMTG_USE_STATIC_CRT               ON  CACHE BOOL "" FORCE)   # matches The Hybrid Core's /MT

set(CMAKE_CXX_STANDARD 17)   # the SDK is C++17 code; our plug-in target stays C++23

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.8.1_build_84
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk
    GIT_PROGRESS   FALSE)
FetchContent_MakeAvailable(vst3sdk)

smtg_add_vst3plugin(HEMEliteRackVST3 PACKAGE_NAME "HEM-Elite-Rack" SOURCES_LIST src/EliteRackVst3.cpp
    "${vst3sdk_SOURCE_DIR}/public.sdk/source/common/pluginview.cpp")
set_target_properties(HEMEliteRackVST3 PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(HEMEliteRackVST3 SYSTEM PRIVATE "${vst3sdk_SOURCE_DIR}")
target_compile_definitions(HEMEliteRackVST3 PRIVATE HEM_RACK_VERSION_STR="${PROJECT_VERSION}")
target_link_libraries(HEMEliteRackVST3 PRIVATE sdk hem::rackui hem::rackfx)

# Fonts, the knob parts' source faceplate, the crown / ghost and every plug-in's faceplate picture go into the bundle's Resources.
get_target_property(HEM_VST3_PKG HEMEliteRackVST3 SMTG_PLUGIN_PACKAGE_PATH)
add_custom_command(TARGET HEMEliteRackVST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${HEM_VST3_PKG}/Contents/Resources/fonts" "${HEM_VST3_PKG}/Contents/Resources/faceplates"
            "${HEM_VST3_PKG}/Contents/Resources/brand"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${HEM_HC_DIR}/assets/fonts/Poppins-Medium.ttf" "${HEM_HC_DIR}/assets/fonts/Poppins-SemiBold.ttf"
            "${HEM_HC_DIR}/assets/fonts/Poppins-Bold.ttf" "${HEM_HC_DIR}/assets/fonts/OFL.txt"
            "${HEM_VST3_PKG}/Contents/Resources/fonts/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HEM_HC_DIR}/assets/faceplates/Main-Faceplate.png" "${HEM_VST3_PKG}/Contents/Resources/faceplates/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/assets/brand/crown_badge.png" "${PROJECT_SOURCE_DIR}/assets/brand/ghost.png"
            "${HEM_VST3_PKG}/Contents/Resources/brand/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/plates" "${HEM_VST3_PKG}/Contents/Resources/plates"
    VERBATIM)

message(STATUS "[HEM-Elite-Rack] VST3 bundle enabled (SDK ${vst3sdk_SOURCE_DIR})")
