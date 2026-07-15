#include "lang/localization.hpp"
#include "gui/cocos/frontend.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace toasty::lang {
    namespace {
        using Table = std::unordered_map<std::string, std::string>;

        struct LocalizationState {
            std::mutex mutex;
            std::once_flag initOnce;
            Table english;
            std::unordered_map<UiLanguage, Table> localized;
            std::unordered_set<std::string> missingKeyWarnings;
            UiLanguage configuredLanguage = UiLanguage::Auto;
            UiLanguage activeLanguage = UiLanguage::English;
            bool resourcesLoaded = false;
        };

        LocalizationState& state() {
            static LocalizationState s;
            return s;
        }

        constexpr std::array supportedLanguages{
            UiLanguage::English,
            UiLanguage::Spanish,
            UiLanguage::French,
            UiLanguage::Vietnamese,
            UiLanguage::Chinese,
        };

        std::string_view toCode(UiLanguage language) {
            switch (language) {
                case UiLanguage::English: return "en";
                case UiLanguage::Spanish: return "es";
                case UiLanguage::French: return "fr";
                case UiLanguage::Vietnamese: return "vi";
                case UiLanguage::Chinese: return "zh";
                case UiLanguage::Auto:
                default: return "en";
            }
        }

        UiLanguage fromSettingValue(std::string_view value) {
            if (value == "English") return UiLanguage::English;
            if (value == "Espanol") return UiLanguage::Spanish;
            if (value == "Francais") return UiLanguage::French;
            if (value == "TiengViet") return UiLanguage::Vietnamese;
            if (value == "Zhongwen") return UiLanguage::Chinese;
            return UiLanguage::Auto;
        }

        UiLanguage resolveAutoLanguage() {
            auto* app = cocos2d::CCApplication::sharedApplication();
            if (!app) {
                return UiLanguage::English;
            }

            switch (app->getCurrentLanguage()) {
                case cocos2d::kLanguageSpanish: return UiLanguage::Spanish;
                case cocos2d::kLanguageEnglish:
                default: return UiLanguage::English;
            }
        }

        void loadLanguageTable(UiLanguage language, std::filesystem::path const& path, Table& outTable) {
            auto jsonResult = utils::file::readJson(path);
            if (!jsonResult) {
                log::warn("Could not load localization file '{}': {}", utils::string::pathToString(path), jsonResult.unwrapErr());
                return;
            }

            auto tableResult = jsonResult.unwrap().as<Table>();
            if (!tableResult) {
                log::warn("Could not parse localization file '{}': {}", utils::string::pathToString(path), tableResult.unwrapErr());
                return;
            }
            outTable = tableResult.unwrap();

            if (language == UiLanguage::English) {
                return;
            }

            auto const& english = state().english;
            for (auto const& [key, _] : english) {
                if (!outTable.contains(key)) {
                    log::debug("Localization file '{}' is missing key '{}'", utils::string::pathToString(path), key);
                }
            }

            for (auto const& [key, _] : outTable) {
                if (!english.contains(key)) {
                    log::debug("Localization file '{}' contains unexpected key '{}'", utils::string::pathToString(path), key);
                }
            }
        }

        std::filesystem::path getLanguageResourcePath(std::string_view fileName) {
            auto resourcesDir = Mod::get()->getResourcesDir();
            auto nestedPath = resourcesDir / "lang" / std::string(fileName);
            if (std::filesystem::exists(nestedPath)) {
                return nestedPath;
            }
            return resourcesDir / std::string(fileName);
        }

        void loadResources() {
            auto& s = state();
            std::scoped_lock lock(s.mutex);
            if (s.resourcesLoaded) {
                return;
            }

            loadLanguageTable(UiLanguage::English, getLanguageResourcePath("en.json"), s.english);
            if (s.english.empty()) {
                log::warn("English localization table is empty; localization will fall back to keys");
            }

            for (auto language : supportedLanguages) {
                if (language == UiLanguage::English) {
                    s.localized[language] = s.english;
                    continue;
                }

                Table table;
                loadLanguageTable(language, getLanguageResourcePath(std::string(toCode(language)) + ".json"), table);
                s.localized[language] = std::move(table);
            }

            s.resourcesLoaded = true;
        }

        void updateActiveLanguageLocked(LocalizationState& s) {
            s.activeLanguage = s.configuredLanguage == UiLanguage::Auto
                ? resolveAutoLanguage()
                : s.configuredLanguage;
        }

        std::string_view lookupLocked(LocalizationState& s, std::string_view key) {
            auto activeTableIt = s.localized.find(s.activeLanguage);
            if (activeTableIt != s.localized.end()) {
                if (auto it = activeTableIt->second.find(std::string(key)); it != activeTableIt->second.end()) {
                    return it->second;
                }
            }

            if (auto it = s.english.find(std::string(key)); it != s.english.end()) {
                return it->second;
            }

            if (s.missingKeyWarnings.emplace(std::string(key)).second) {
                log::warn("Missing localization key '{}'", key);
            }
            return key;
        }
    }

    void initialize() {
        auto& s = state();
        std::call_once(s.initOnce, [] {
            loadResources();
            {
                auto& inner = state();
                std::scoped_lock lock(inner.mutex);
                inner.configuredLanguage = fromSettingValue(Mod::get()->getSettingValue<std::string>("ui_language"));
                updateActiveLanguageLocked(inner);
            }
            listenForSettingChanges<std::string>("ui_language", [](std::string const&) {
                refresh();
                toasty::frontend::refreshMenuState();
            });
        });
    }

    void refresh() {
        initialize();

        auto& s = state();
        std::scoped_lock lock(s.mutex);
        s.configuredLanguage = fromSettingValue(Mod::get()->getSettingValue<std::string>("ui_language"));
        updateActiveLanguageLocked(s);
    }

    UiLanguage getConfiguredLanguage() {
        initialize();
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        return s.configuredLanguage;
    }

    UiLanguage getActiveLanguage() {
        initialize();
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        return s.activeLanguage;
    }

    std::string_view getLanguageSettingValue(UiLanguage language) {
        switch (language) {
            case UiLanguage::English:    return "English";
            case UiLanguage::Spanish:    return "Espanol";
            case UiLanguage::French:     return "Francais";
            case UiLanguage::Vietnamese: return "TiengViet";
            case UiLanguage::Chinese:    return "Zhongwen";
            case UiLanguage::Auto:
            default: return "Auto";
        }
    }

    std::string_view getLanguageDisplayName(UiLanguage language) {
        switch (language) {
            case UiLanguage::English:    return "English";
            case UiLanguage::Spanish:    return "Espa" "\xC3\xB1" "ol";
            case UiLanguage::French:     return "Fran" "\xC3\xA7" "ais";
            case UiLanguage::Vietnamese: return "Tieng Viet";
            case UiLanguage::Chinese:    return "Chinese (Simplified)";
            case UiLanguage::Auto:
            default: return "Auto (System)";
        }
    }

    std::string_view tr(std::string_view key) {
        initialize();

        auto& s = state();
        std::scoped_lock lock(s.mutex);
        return lookupLocked(s, key);
    }

    namespace detail {
        Result<std::string> interpolate(std::string_view pattern, std::span<FormatArg const> args) {
            std::string output;
            output.reserve(pattern.size());
            size_t cursor = 0;
            while (cursor < pattern.size()) {
                if (pattern[cursor] == '{') {
                    if (cursor + 1 < pattern.size() && pattern[cursor + 1] == '{') {
                        output.push_back('{');
                        cursor += 2;
                        continue;
                    }
                    size_t close = pattern.find('}', cursor + 1);
                    if (close == std::string_view::npos) {
                        return Err("missing closing brace");
                    }
                    auto name = pattern.substr(cursor + 1, close - cursor - 1);
                    auto value = std::find_if(args.begin(), args.end(), [name](FormatArg const& arg) {
                        return arg.name == name;
                    });
                    if (value == args.end()) {
                        return Err("unknown placeholder '{}'", name);
                    }
                    output += value->value;
                    cursor = close + 1;
                    continue;
                }
                if (pattern[cursor] == '}') {
                    if (cursor + 1 < pattern.size() && pattern[cursor + 1] == '}') {
                        output.push_back('}');
                        cursor += 2;
                        continue;
                    }
                    return Err("unexpected closing brace");
                }
                output.push_back(pattern[cursor]);
                ++cursor;
            }
            return Ok(std::move(output));
        }

        std::string trfImpl(std::string_view key, std::span<FormatArg const> args) {
            auto pattern = std::string(tr(key));
            auto formatted = interpolate(pattern, args);
            if (!formatted) {
                log::warn("Failed to format localization key '{}': {}", key, formatted.unwrapErr());
                return pattern;
            }
            return std::move(formatted).unwrap();
        }
    }
}
