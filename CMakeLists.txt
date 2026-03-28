cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(ToastyReplay VERSION 1.3.0 LANGUAGES CXX)

file(GLOB SOURCES
    src/ttr_format.cpp
    src/core/checkpoint_handler.cpp
    src/core/replay_engine.cpp
    src/core/checkpoint_system.cpp
    src/core/cbf_integration.cpp
    src/core/input_timing.cpp
    src/core/keybinds.cpp
    src/gui/gui.cpp
    src/gui/frame_editor.cpp
    src/tools/layoutmode.cpp
    src/tools/nomirror.cpp
    src/tools/speedhackaudio.cpp
    src/hacks/autoclicker.cpp
    src/hacks/physicsbypass.cpp
    src/hacks/safemode.cpp
    src/hacks/hitboxes.cpp
    src/hacks/trajectory.cpp
    src/render/renderer.cpp
    src/render/watermark.cpp
    src/audio/clicksounds.cpp
    src/audio/playsound.cpp
    src/online/online_client.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${gd-imgui-cocos_SOURCE_DIR}/include
)

if (MSVC)
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

add_subdirectory($ENV{GEODE_SDK} $ENV{GEODE_SDK}/build)

CPMAddPackage("gh:maxnut/GDReplayFormat#9c32b5529067901418ea6e4dfbd36437dc8712bc")
set(IMGUI_VERSION "v1.91.4" CACHE STRING "Pin imgui version" FORCE)
CPMAddPackage("gh:matcool/gd-imgui-cocos#97643336209b760a3d59d83b770546b1cfef4e7d")
set(GD_IMGUI_COCOS_BACKEND_FILE "${gd-imgui-cocos_SOURCE_DIR}/src/backend.cpp")
file(READ "${GD_IMGUI_COCOS_BACKEND_FILE}" GD_IMGUI_COCOS_BACKEND_CONTENT)
string(REPLACE
"static const auto iniPath = (Mod::get()->getSaveDir() / \"imgui.ini\").string();"
"static const auto iniPath = new std::string((Mod::get()->getSaveDir() / \"imgui.ini\").string());"
GD_IMGUI_COCOS_BACKEND_CONTENT
"${GD_IMGUI_COCOS_BACKEND_CONTENT}"
)
string(REPLACE
"io.IniFilename = iniPath.c_str();"
"io.IniFilename = iniPath->c_str();"
GD_IMGUI_COCOS_BACKEND_CONTENT
"${GD_IMGUI_COCOS_BACKEND_CONTENT}"
)
if (NOT GD_IMGUI_COCOS_BACKEND_CONTENT MATCHES "readClipboardText")
string(REPLACE
"#ifdef GEODE_IS_MOBILE\n\nclass ImGuiIMEDelegate"
"#ifdef GEODE_IS_WINDOWS\nstatic std::string readClipboardText() {\n\tif (!OpenClipboard(nullptr)) return {};\n\n\tHANDLE hData = GetClipboardData(CF_UNICODETEXT);\n\tif (hData == nullptr) {\n\t\tCloseClipboard();\n\t\treturn {};\n\t}\n\n\tauto pszText = static_cast<wchar_t*>(GlobalLock(hData));\n\tif (pszText == nullptr) {\n\t\tCloseClipboard();\n\t\treturn {};\n\t}\n\n\tint size = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, nullptr, 0, nullptr, nullptr);\n\tstd::string text;\n\tif (size > 0) {\n\t\ttext.resize(static_cast<size_t>(size));\n\t\tWideCharToMultiByte(CP_UTF8, 0, pszText, -1, text.data(), size, nullptr, nullptr);\n\t\tif (!text.empty() && text.back() == '\\0') {\n\t\t\ttext.pop_back();\n\t\t}\n\t}\n\n\tGlobalUnlock(hData);\n\tCloseClipboard();\n\treturn text;\n}\n#endif\n\n#ifdef GEODE_IS_MOBILE\n\nclass ImGuiIMEDelegate"
GD_IMGUI_COCOS_BACKEND_CONTENT
"${GD_IMGUI_COCOS_BACKEND_CONTENT}"
)
endif()
string(REPLACE
"auto static read = geode::utils::clipboard::read();"
"auto static read = new std::string();"
GD_IMGUI_COCOS_BACKEND_CONTENT
"${GD_IMGUI_COCOS_BACKEND_CONTENT}"
)
string(REPLACE
"read = geode::utils::clipboard::read();"
"*read = readClipboardText();"
GD_IMGUI_COCOS_BACKEND_CONTENT
"${GD_IMGUI_COCOS_BACKEND_CONTENT}"
)
string(REPLACE
"return read.c_str();"
"return read->c_str();"
GD_IMGUI_COCOS_BACKEND_CONTENT
"${GD_IMGUI_COCOS_BACKEND_CONTENT}"
)
file(WRITE "${GD_IMGUI_COCOS_BACKEND_FILE}" "${GD_IMGUI_COCOS_BACKEND_CONTENT}")
CPMAddPackage(
    NAME ZLIB
    GITHUB_REPOSITORY madler/zlib
    VERSION 1.3.1
    OPTIONS "ZLIB_BUILD_EXAMPLES OFF"
)
if (ZLIB_ADDED)
    set(ZLIB_INCLUDE_DIR ${ZLIB_SOURCE_DIR} ${ZLIB_BINARY_DIR})
    set(ZLIB_LIBRARY zlibstatic)
    target_include_directories(${PROJECT_NAME} PRIVATE ${ZLIB_INCLUDE_DIR})
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")

if (WIN32)
target_link_libraries(${PROJECT_NAME} imgui-cocos libGDR zlibstatic)
elseif (APPLE)
target_link_libraries(${PROJECT_NAME} imgui-cocos libGDR zlibstatic "-framework IOKit")
endif()


setup_geode_mod(${PROJECT_NAME})
