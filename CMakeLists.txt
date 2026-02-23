cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(ToastyReplay VERSION 1.2.2 LANGUAGES CXX)

file(GLOB SOURCES
	src/gui.cpp
    src/keybinds.cpp
    src/physicsbypass.cpp
    src/replay_engine.cpp
    src/checkpoint_system.cpp
    src/speedhackaudio.cpp
    src/cbf_integration.cpp
    src/playsound.cpp
    src/trajectory.cpp
    src/safemode.cpp
    src/watermark.cpp
    src/hitboxes.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${gd-imgui-cocos_SOURCE_DIR}/include
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

set(GEODE_BINDINGS_REPO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/bindings" CACHE PATH "Path to bindings" FORCE)

add_subdirectory($ENV{GEODE_SDK} $ENV{GEODE_SDK}/build)

CPMAddPackage("gh:maxnut/GDReplayFormat#9c32b5529067901418ea6e4dfbd36437dc8712bc")
set(IMGUI_VERSION "v1.91.4" CACHE STRING "Pin imgui version" FORCE)
CPMAddPackage("gh:matcool/gd-imgui-cocos#97643336209b760a3d59d83b770546b1cfef4e7d")
target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")

if (WIN32)
target_link_libraries(${PROJECT_NAME} imgui-cocos libGDR)
elseif (APPLE)
target_link_libraries(${PROJECT_NAME} imgui-cocos libGDR "-framework IOKit")
endif()


setup_geode_mod(${PROJECT_NAME})
