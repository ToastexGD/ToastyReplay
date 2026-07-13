cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(ToastyReplay VERSION 2.2.1 LANGUAGES CXX)

set(TOASTYREPLAY_SOURCES
    src/format/ttr_format.cpp
    src/format/ttr3_format.cpp
    src/conversion/gdr_upgrade.cpp
    src/conversion/tcbot_format.cpp
    src/conversion/tcbot_import.cpp
    src/core/checkpoint_handler.cpp
    src/core/replay_engine.cpp
    src/core/checkpoint_system.cpp
    src/core/input_timing.cpp
    src/core/frame_stepper.cpp
    src/core/keybinds.cpp
    src/editor/editor_playtest.cpp
    src/gui/gui.cpp
    src/gui/credits_popup.cpp
    src/gui/cocos/frontend.cpp
    src/gui/cocos/localized_label.cpp
    src/gui/cocos/tr_menu_popup.cpp
    src/gui/cocos/tr_frame_editor_popup.cpp
    src/gui/cocos/tr_replay_actions_popup.cpp
    src/gui/cocos/mobile_float.cpp
    src/gui/cocos/cells/cells.cpp
    src/gui/frame_editor.cpp
    src/tools/layoutmode.cpp
    src/tools/nomirror.cpp
    src/tools/speedhackaudio.cpp
    src/tools/disableshaders.cpp
    src/tools/visual_suppression.cpp
    src/hacks/autoclicker.cpp
    src/hacks/physicsbypass.cpp
    src/hacks/safemode.cpp
    src/hacks/hitboxes.cpp
    src/trajectory/trajectory.cpp
    src/trajectory/trajectory_physics.cpp
    src/render/render_config.cpp
    src/render/render_preset.cpp
    src/render/renderer.cpp
    src/render/watermark.cpp
    src/audio/clicksounds.cpp
    src/audio/playsound.cpp
    src/lang/localization.cpp
    src/online/online_client.cpp
)

add_library(${PROJECT_NAME} SHARED ${TOASTYREPLAY_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /FS)

    set_source_files_properties(
        src/audio/clicksounds.cpp
        src/audio/playsound.cpp
        src/gui/gui.cpp
        src/online/online_client.cpp
        src/render/renderer.cpp
        src/tools/layoutmode.cpp
        src/tools/speedhackaudio.cpp
        PROPERTIES
            SKIP_PRECOMPILE_HEADERS ON
            COMPILE_OPTIONS "$<$<CONFIG:Release>:/Od;/Ob0>"
    )
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage(
    NAME GDReplayFormat
    GITHUB_REPOSITORY maxnut/GDReplayFormat
    GIT_TAG 77bd505853541a87a12895dc23de05f6585d0515
    DOWNLOAD_ONLY YES
)
add_library(libGDR INTERFACE)
target_include_directories(libGDR INTERFACE "${GDReplayFormat_SOURCE_DIR}/include")

set(IMGUI_VERSION "v1.91.4" CACHE STRING "Pin imgui version" FORCE)
CPMAddPackage(
    NAME gd-imgui-cocos
    GITHUB_REPOSITORY matcool/gd-imgui-cocos
    GIT_TAG b93f08ccef778a53ebba09b20c347f6a63980119
)

CPMAddPackage(
    NAME ZLIB
    GITHUB_REPOSITORY madler/zlib
    VERSION 1.3.1
    OPTIONS "ZLIB_BUILD_EXAMPLES OFF"
)

set(TOASTYREPLAY_LINK_LIBRARIES imgui-cocos libGDR)
if (TARGET zlibstatic)
    target_include_directories(${PROJECT_NAME} PRIVATE ${ZLIB_SOURCE_DIR} ${ZLIB_BINARY_DIR})
    list(APPEND TOASTYREPLAY_LINK_LIBRARIES zlibstatic)
endif()

add_executable(toastyreplay_vanilla_conversion_tests
    src/format/ttr3_format.cpp
    tests/vanilla_conversion_tests.cpp
)
target_include_directories(toastyreplay_vanilla_conversion_tests PRIVATE src)
target_compile_features(toastyreplay_vanilla_conversion_tests PRIVATE cxx_std_23)
target_compile_definitions(toastyreplay_vanilla_conversion_tests PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")
target_compile_definitions(toastyreplay_vanilla_conversion_tests PRIVATE TOASTYREPLAY_GAMEPLAY_LAYER_TEST)
if (MSVC)
    target_compile_definitions(toastyreplay_vanilla_conversion_tests PRIVATE _HAS_ITERATOR_DEBUGGING=0)
endif()
target_link_libraries(toastyreplay_vanilla_conversion_tests PRIVATE GeodeBindings libGDR)
if (TARGET zlibstatic)
    target_include_directories(toastyreplay_vanilla_conversion_tests PRIVATE ${ZLIB_SOURCE_DIR} ${ZLIB_BINARY_DIR})
    target_link_libraries(toastyreplay_vanilla_conversion_tests PRIVATE zlibstatic)
endif()

add_executable(toastyreplay_gdr_import_tests
    tests/gdr_import_tests.cpp
)
target_include_directories(toastyreplay_gdr_import_tests PRIVATE src)
target_compile_features(toastyreplay_gdr_import_tests PRIVATE cxx_std_23)
target_compile_definitions(toastyreplay_gdr_import_tests PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")
if (MSVC)
    target_compile_definitions(toastyreplay_gdr_import_tests PRIVATE _HAS_ITERATOR_DEBUGGING=0)
    target_compile_options(toastyreplay_gdr_import_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(toastyreplay_gdr_import_tests PRIVATE -UNDEBUG)
endif()
target_link_libraries(toastyreplay_gdr_import_tests PRIVATE GeodeBindings libGDR)

add_executable(toastyreplay_tcbot_format_tests
    src/conversion/tcbot_format.cpp
    tests/tcbot_format_tests.cpp
)
target_include_directories(toastyreplay_tcbot_format_tests PRIVATE src)
target_compile_features(toastyreplay_tcbot_format_tests PRIVATE cxx_std_23)
if (MSVC)
    target_compile_definitions(toastyreplay_tcbot_format_tests PRIVATE _HAS_ITERATOR_DEBUGGING=0)
    target_compile_options(toastyreplay_tcbot_format_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(toastyreplay_tcbot_format_tests PRIVATE -UNDEBUG)
endif()
target_link_libraries(toastyreplay_tcbot_format_tests PRIVATE GeodeBindings)

set(TOASTYREPLAY_WATERMARK_ROOT "" CACHE PATH "Private ToastyReplay watermark artifact root")
if (NOT TOASTYREPLAY_WATERMARK_ROOT AND DEFINED ENV{TOASTYREPLAY_WATERMARK_ROOT})
    set(TOASTYREPLAY_WATERMARK_ROOT "$ENV{TOASTYREPLAY_WATERMARK_ROOT}" CACHE PATH "Private ToastyReplay watermark artifact root" FORCE)
endif()

if (WIN32 AND TOASTYREPLAY_WATERMARK_ROOT)
    set(_toastyreplay_watermark_include "${TOASTYREPLAY_WATERMARK_ROOT}/include")
    set(_toastyreplay_watermark_lib "${TOASTYREPLAY_WATERMARK_ROOT}/lib/win/x64/toasty_watermark.lib")
    if (NOT EXISTS "${_toastyreplay_watermark_include}/toasty_watermark.hpp")
        message(FATAL_ERROR "Missing private watermark header at ${_toastyreplay_watermark_include}/toasty_watermark.hpp")
    endif()
    if (NOT EXISTS "${_toastyreplay_watermark_lib}")
        message(FATAL_ERROR "Missing private watermark library at ${_toastyreplay_watermark_lib}")
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE "${_toastyreplay_watermark_include}")
    list(APPEND TOASTYREPLAY_LINK_LIBRARIES "${_toastyreplay_watermark_lib}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE TOASTYREPLAY_HAS_PRIVATE_WATERMARK)
    message(STATUS "Private static watermark library enabled: ${_toastyreplay_watermark_lib}")
else()
    message(STATUS "Private static watermark library not configured. Debug builds use the local fallback; Release builds skip live playback watermark drawing.")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    "$<$<CONFIG:Debug>:TOASTYREPLAY_ALLOW_WATERMARK_STUB>"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")

if (WIN32)
    target_link_libraries(${PROJECT_NAME} ${TOASTYREPLAY_LINK_LIBRARIES} Crypt32 Advapi32)
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} ${TOASTYREPLAY_LINK_LIBRARIES} "-framework IOKit")
endif()


setup_geode_mod(${PROJECT_NAME})
