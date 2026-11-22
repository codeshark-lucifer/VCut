cmake_minimum_required(VERSION 3.20)

project(VCut VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(SDL3 REQUIRED CONFIG)
find_package(OpenGL REQUIRED)
find_package(nlohmann_json REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libswscale libswresample libavutil)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)

# Dear ImGui (docking branch, vendored in external/imgui)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui PUBLIC SDL3::SDL3 OpenGL::GL)

function(vcut_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# vcut_core: everything that is not UI (model, media, engine, export, io).
# Kept separate so it can be unit-tested headless.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE VCUT_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_SOURCE_DIR}/src/media/*.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/*.cpp
    ${CMAKE_SOURCE_DIR}/src/export/*.cpp
    ${CMAKE_SOURCE_DIR}/src/io/*.cpp)
add_library(vcut_core STATIC ${VCUT_CORE_SOURCES})
target_include_directories(vcut_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(vcut_core PUBLIC PkgConfig::FFMPEG PkgConfig::FREETYPE nlohmann_json::nlohmann_json SDL3::SDL3)
vcut_warnings(vcut_core)

# ---------------------------------------------------------------------------
# vcut: the editor application
# ---------------------------------------------------------------------------
file(GLOB_RECURSE VCUT_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/app/*.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/*.cpp)
if(WIN32)
    enable_language(RC)
    list(APPEND VCUT_APP_SOURCES ${CMAKE_SOURCE_DIR}/src/app/vcut.rc)
endif()
add_executable(vcut ${CMAKE_SOURCE_DIR}/src/main.cpp ${VCUT_APP_SOURCES})
target_link_libraries(vcut PRIVATE vcut_core imgui)
vcut_warnings(vcut)
# No console window in Release builds (logs stay visible in Debug)
set_target_properties(vcut PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(vcut_tests ${CMAKE_SOURCE_DIR}/tests/test_timeline.cpp)
target_link_libraries(vcut_tests PRIVATE vcut_core)
vcut_warnings(vcut_tests)
add_test(NAME timeline COMMAND vcut_tests)

add_executable(vcut_pipeline_test ${CMAKE_SOURCE_DIR}/tests/test_pipeline.cpp)
target_link_libraries(vcut_pipeline_test PRIVATE vcut_core)
vcut_warnings(vcut_pipeline_test)

find_program(FFMPEG_EXE ffmpeg)
if(FFMPEG_EXE)
    set(TEST_MEDIA_DIR ${CMAKE_BINARY_DIR}/test_media)
    file(MAKE_DIRECTORY ${TEST_MEDIA_DIR})
    add_test(NAME generate_media
        COMMAND ${FFMPEG_EXE} -y -hide_banner -loglevel error
            -f lavfi -i testsrc2=size=640x360:rate=30:duration=4
            -f lavfi -i sine=frequency=440:sample_rate=44100:duration=4
            -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest ${TEST_MEDIA_DIR}/clip.mp4)
    set_tests_properties(generate_media PROPERTIES FIXTURES_SETUP media)
    add_test(NAME pipeline
        COMMAND vcut_pipeline_test ${TEST_MEDIA_DIR}/clip.mp4 ${TEST_MEDIA_DIR}/export.mp4)
    set_tests_properties(pipeline PROPERTIES FIXTURES_REQUIRED media)
endif()
