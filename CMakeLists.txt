cmake_minimum_required(VERSION 3.25)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(ToastyReplay VERSION 1.3.1 LANGUAGES CXX)

set(TOASTYREPLAY_SOURCES
    src/ttr_format.cpp
    src/core/checkpoint_handler.cpp
    src/core/replay_engine.cpp
    src/core/checkpoint_system.cpp
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
    src/i18n/localization.cpp
    src/online/online_client.cpp
)

function(toastyreplay_replace_once_or_verify content_var search replacement)
    string(FIND "${${content_var}}" "${replacement}" replacement_index)
    if (replacement_index GREATER -1)
        set(${content_var} "${${content_var}}" PARENT_SCOPE)
        return()
    endif()

    string(FIND "${${content_var}}" "${search}" search_index)
    if (search_index EQUAL -1)
        message(FATAL_ERROR "Unable to patch gd-imgui-cocos backend. Missing expected snippet: ${search}")
    endif()

    string(REPLACE "${search}" "${replacement}" updated_content "${${content_var}}")
    set(${content_var} "${updated_content}" PARENT_SCOPE)
endfunction()

function(toastyreplay_replace_if_present content_var search replacement)
    string(FIND "${${content_var}}" "${replacement}" replacement_index)
    if (replacement_index GREATER -1)
        set(${content_var} "${${content_var}}" PARENT_SCOPE)
        return()
    endif()

    string(FIND "${${content_var}}" "${search}" search_index)
    if (search_index EQUAL -1)
        set(${content_var} "${${content_var}}" PARENT_SCOPE)
        return()
    endif()

    string(REPLACE "${search}" "${replacement}" updated_content "${${content_var}}")
    set(${content_var} "${updated_content}" PARENT_SCOPE)
endfunction()

function(patch_gd_imgui_cocos_backend backend_file)
    file(READ "${backend_file}" backend_content)

    toastyreplay_replace_once_or_verify(
        backend_content
        "#include <Geode/Geode.hpp>"
        "#include <Geode/Geode.hpp>\n#include <Geode/utils/string.hpp>"
    )
    set(toastyreplay_imgui_ini_line "static const auto iniPath = new std::string(geode::utils::string::pathToString(Mod::get()->getSaveDir() / \"imgui.ini\"));")
    toastyreplay_replace_if_present(
        backend_content
        "static const auto iniPath = (Mod::get()->getSaveDir() / \"imgui.ini\").string();"
        "${toastyreplay_imgui_ini_line}"
    )
    toastyreplay_replace_if_present(
        backend_content
        "static const auto iniPath = new std::string((Mod::get()->getSaveDir() / \"imgui.ini\").string());"
        "${toastyreplay_imgui_ini_line}"
    )
    toastyreplay_replace_once_or_verify(
        backend_content
        "${toastyreplay_imgui_ini_line}"
        "${toastyreplay_imgui_ini_line}"
    )
    toastyreplay_replace_once_or_verify(
        backend_content
        "io.IniFilename = iniPath.c_str();"
        "io.IniFilename = iniPath->c_str();"
    )
    toastyreplay_replace_once_or_verify(
        backend_content
        "#ifdef GEODE_IS_MOBILE\n\nclass ImGuiIMEDelegate"
        "#ifdef GEODE_IS_WINDOWS\nstatic std::string readClipboardText() {\n\tif (!OpenClipboard(nullptr)) return {};\n\n\tHANDLE hData = GetClipboardData(CF_UNICODETEXT);\n\tif (hData == nullptr) {\n\t\tCloseClipboard();\n\t\treturn {};\n\t}\n\n\tauto pszText = static_cast<wchar_t*>(GlobalLock(hData));\n\tif (pszText == nullptr) {\n\t\tCloseClipboard();\n\t\treturn {};\n\t}\n\n\tint size = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, nullptr, 0, nullptr, nullptr);\n\tstd::string text;\n\tif (size > 0) {\n\t\ttext.resize(static_cast<size_t>(size));\n\t\tWideCharToMultiByte(CP_UTF8, 0, pszText, -1, text.data(), size, nullptr, nullptr);\n\t\tif (!text.empty() && text.back() == '\\0') {\n\t\t\ttext.pop_back();\n\t\t}\n\t}\n\n\tGlobalUnlock(hData);\n\tCloseClipboard();\n\treturn text;\n}\n#endif\n\n#ifdef GEODE_IS_MOBILE\n\nclass ImGuiIMEDelegate"
    )
    toastyreplay_replace_once_or_verify(
        backend_content
        "auto static read = geode::utils::clipboard::read();"
        "auto static read = new std::string();"
    )
    toastyreplay_replace_once_or_verify(
        backend_content
        "read = geode::utils::clipboard::read();"
        "*read = readClipboardText();"
    )
    toastyreplay_replace_once_or_verify(
        backend_content
        "return read.c_str();"
        "return read->c_str();"
    )

    file(WRITE "${backend_file}" "${backend_content}")
endfunction()

function(patch_gd_replay_format_header header_file)
    file(READ "${header_file}" replay_content)

    string(FIND "${replay_content}" "static std::optional<Self> tryImportData(std::vector<uint8_t> const& data, bool importInputs = true)" already_patched_index)
    if (already_patched_index GREATER -1)
        return()
    endif()

    toastyreplay_replace_once_or_verify(
        replay_content
        [=[	static Self importData(std::vector<uint8_t> const& data, bool importInputs = true) {
		Self replay;
		json replayJson;

		try {
			replayJson = json::from_msgpack(data);
		} catch(std::exception& e) {
			replayJson = json::parse(data);
		}

		replay.gameVersion = replayJson["gameVersion"];
		replay.description = replayJson["description"];
		replay.version = replayJson["version"];
		replay.duration = replayJson["duration"];
		replay.botInfo.name = replayJson["bot"]["name"];
		replay.botInfo.version = replayJson["bot"]["version"];
		replay.levelInfo.id = replayJson["level"]["id"];
		replay.levelInfo.name = replayJson["level"]["name"];
		replay.author = replayJson["author"];
		replay.seed = replayJson["seed"];
		replay.coins = replayJson["coins"];
		replay.ldm = replayJson["ldm"];
		
		if(replayJson.contains("framerate"))
			replay.framerate = replayJson["framerate"];
		replay.parseExtension(replayJson.get<json::object_t>());

		if(!importInputs)
			return replay;

		for (json const& inputJson : replayJson["inputs"]) {
			InputType input;
			input.frame = inputJson["frame"];
			input.button = inputJson["btn"];
			input.player2 = inputJson["2p"];
			input.down = inputJson["down"];
			input.parseExtension(inputJson.get<json::object_t>());

			replay.inputs.push_back(input);
		}

		return replay;
	}
]=]
        [=[	static std::optional<Self> tryImportData(std::vector<uint8_t> const& data, bool importInputs = true) {
		Self replay;
		json replayJson = json::from_msgpack(data, true, false);
		if (replayJson.is_discarded()) {
			replayJson = json::parse(data, nullptr, false);
		}
		if (!replayJson.is_object()) {
			return std::nullopt;
		}

		auto const* root = replayJson.get_ptr<const json::object_t*>();
		if (!root) {
			return std::nullopt;
		}

		auto getValue = [](json::object_t const& object, char const* key) -> json const* {
			auto it = object.find(key);
			return it == object.end() ? nullptr : &it->second;
		};

		auto getString = [&](json::object_t const& object, char const* key, std::string& out) -> bool {
			auto const* value = getValue(object, key);
			if (!value) return false;
			auto const* stringValue = value->template get_ptr<const std::string*>();
			if (!stringValue) return false;
			out = *stringValue;
			return true;
		};

		auto getBool = [&](json::object_t const& object, char const* key, bool& out) -> bool {
			auto const* value = getValue(object, key);
			if (!value) return false;
			auto const* boolValue = value->template get_ptr<const bool*>();
			if (!boolValue) return false;
			out = *boolValue;
			return true;
		};

		auto getNumber = [&](json::object_t const& object, char const* key, auto& out) -> bool {
			auto const* value = getValue(object, key);
			if (!value) return false;

			if (auto const* floatValue = value->template get_ptr<const json::number_float_t*>()) {
				out = static_cast<std::decay_t<decltype(out)>>(*floatValue);
				return true;
			}
			if (auto const* signedValue = value->template get_ptr<const json::number_integer_t*>()) {
				out = static_cast<std::decay_t<decltype(out)>>(*signedValue);
				return true;
			}
			if (auto const* unsignedValue = value->template get_ptr<const json::number_unsigned_t*>()) {
				out = static_cast<std::decay_t<decltype(out)>>(*unsignedValue);
				return true;
			}

			return false;
		};

		auto const* botJson = getValue(*root, "bot");
		auto const* levelJson = getValue(*root, "level");
		auto const* inputsJson = getValue(*root, "inputs");
		if (!botJson || !botJson->is_object() || !levelJson || !levelJson->is_object()) {
			return std::nullopt;
		}

		auto const* botObject = botJson->template get_ptr<const json::object_t*>();
		auto const* levelObject = levelJson->template get_ptr<const json::object_t*>();
		if (!botObject || !levelObject) {
			return std::nullopt;
		}

		if (!getNumber(*root, "gameVersion", replay.gameVersion)
			|| !getString(*root, "description", replay.description)
			|| !getNumber(*root, "version", replay.version)
			|| !getNumber(*root, "duration", replay.duration)
			|| !getString(*botObject, "name", replay.botInfo.name)
			|| !getString(*botObject, "version", replay.botInfo.version)
			|| !getNumber(*levelObject, "id", replay.levelInfo.id)
			|| !getString(*levelObject, "name", replay.levelInfo.name)
			|| !getString(*root, "author", replay.author)
			|| !getNumber(*root, "seed", replay.seed)
			|| !getNumber(*root, "coins", replay.coins)
			|| !getBool(*root, "ldm", replay.ldm)) {
			return std::nullopt;
		}

		getNumber(*root, "framerate", replay.framerate);
		replay.parseExtension(*root);

		if(!importInputs)
			return replay;

		if (!inputsJson || !inputsJson->is_array()) {
			return std::nullopt;
		}

		for (json const& inputJson : *inputsJson) {
			auto const* inputObject = inputJson.template get_ptr<const json::object_t*>();
			if (!inputObject) {
				return std::nullopt;
			}

			InputType input;
			if (!getNumber(*inputObject, "frame", input.frame)
				|| !getNumber(*inputObject, "btn", input.button)
				|| !getBool(*inputObject, "2p", input.player2)
				|| !getBool(*inputObject, "down", input.down)) {
				return std::nullopt;
			}
			input.parseExtension(*inputObject);

			replay.inputs.push_back(input);
		}

		return replay;
	}

	static Self importData(std::vector<uint8_t> const& data, bool importInputs = true) {
		auto replay = tryImportData(data, importInputs);
		return replay.value_or(Self{});
	}
]=]
    )

    file(WRITE "${header_file}" "${replay_content}")
endfunction()

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

add_subdirectory($ENV{GEODE_SDK} $ENV{GEODE_SDK}/build)

CPMAddPackage("gh:maxnut/GDReplayFormat#9c32b5529067901418ea6e4dfbd36437dc8712bc")
patch_gd_replay_format_header("${GDReplayFormat_SOURCE_DIR}/include/gdr/gdr.hpp")
set(IMGUI_VERSION "v1.91.4" CACHE STRING "Pin imgui version" FORCE)
CPMAddPackage("gh:matcool/gd-imgui-cocos#97643336209b760a3d59d83b770546b1cfef4e7d")
target_include_directories(${PROJECT_NAME} PRIVATE ${gd-imgui-cocos_SOURCE_DIR}/include)
patch_gd_imgui_cocos_backend("${gd-imgui-cocos_SOURCE_DIR}/src/backend.cpp")

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

target_compile_definitions(${PROJECT_NAME} PRIVATE MOD_VERSION="${CMAKE_PROJECT_VERSION}")

if (WIN32)
    target_link_libraries(${PROJECT_NAME} ${TOASTYREPLAY_LINK_LIBRARIES})
elseif (APPLE)
    target_link_libraries(${PROJECT_NAME} ${TOASTYREPLAY_LINK_LIBRARIES} "-framework IOKit")
endif()


setup_geode_mod(${PROJECT_NAME})
