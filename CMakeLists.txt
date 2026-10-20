# =============================================================================
#  HEM-Elite-Rack — Hybrid Elite Mafia's plug-in rack: a simple Windows app to load a song,
#  stack HEM plug-ins, hear them live, A/B and export, plus the same plug-ins as VST3.
#  Built on The Hybrid Core (external/HybridCore, a pinned git submodule) as a library.
#  Licence: GPLv3 or later (LICENSE). Sound: ASIO (Steinberg's ASIO SDK under its GPLv3 option) or WASAPI.
# =============================================================================
cmake_minimum_required(VERSION 3.28)

if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "[HEM-Elite-Rack] Windows host required.")
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "HEM-Elite-Rack configurations" FORCE)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HEM_RACK_VERSION)
string(STRIP "${HEM_RACK_VERSION}" HEM_RACK_VERSION)

project(HEMEliteRack
    VERSION ${HEM_RACK_VERSION}
    DESCRIPTION "Hybrid Elite Mafia plug-in rack"
    LANGUAGES CXX RC)

set(HEM_HC_DIR "${PROJECT_SOURCE_DIR}/external/HybridCore")
if(NOT EXISTS "${HEM_HC_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "[HEM-Elite-Rack] external/HybridCore is empty: run  git submodule update --init")
endif()

# The Hybrid Core's platform rules (MSVC x64, C++23, static CRT, AVX2) apply to this project too.
list(APPEND CMAKE_MODULE_PATH "${HEM_HC_DIR}/cmake")
include(HybridCorePlatform)

# The Hybrid Core as a library only, with ASIO: the SDK must be in external/HybridCore/external/asiosdk
# (external/HybridCore/tools/fetch-asio-sdk.ps1 downloads Steinberg's own zip and checks it). Never committed.
set(HC_BUILD_APPS       OFF CACHE BOOL "" FORCE)
set(HC_WITH_ASIO        ON  CACHE BOOL "" FORCE)
set(HC_REQUIRE_ASIO_SDK ON  CACHE BOOL "" FORCE)
add_subdirectory("${HEM_HC_DIR}" HybridCore EXCLUDE_FROM_ALL)

# Runtime assets (Poppins fonts, the knob parts' source faceplate, the HEM crown and ghost)
# are copied next to every program that draws.
function(hem_copy_assets target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${HEM_HC_DIR}/assets/fonts" "$<TARGET_FILE_DIR:${target}>/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${HEM_HC_DIR}/assets/faceplates" "$<TARGET_FILE_DIR:${target}>/assets/faceplates"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets/brand" "$<TARGET_FILE_DIR:${target}>/assets/brand"
        VERBATIM)
endfunction()

add_subdirectory(rackfx)
add_subdirectory(rackui)
add_subdirectory(rackapp)
add_subdirectory(tools/rack_snapshot)

option(HEM_RACK_BUILD_VST3 "Also build the VST3 bundle (fetches the MIT-licensed Steinberg VST3 SDK)" ON)
if(HEM_RACK_BUILD_VST3)
    add_subdirectory(plugin)
endif()

enable_testing()
add_subdirectory(tests)

set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT HEMEliteRack)
