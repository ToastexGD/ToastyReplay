#include "i18n/localization.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/file.hpp>

#include <array>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace toasty::i18n {
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
        };

        std::string_view toCode(UiLanguage language) {
            switch (language) {
                case UiLanguage::English: return "en";
                case UiLanguage::Spanish: return "es";
                case UiLanguage::Auto:
                default: return "en";
            }
        }

        UiLanguage fromSettingValue(std::string_view value) {
            if (value == "Spanish") return UiLanguage::Spanish;
            if (value == "English") return UiLanguage::English;
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
                log::warn("Failed to load localization file '{}': {}", path.string(), jsonResult.unwrapErr());
                return;
            }

            auto tableResult = jsonResult.unwrap().as<Table>();
            if (!tableResult) {
                log::warn("Failed to parse localization file '{}': {}", path.string(), tableResult.unwrapErr());
                return;
            }
            outTable = tableResult.unwrap();

            if (language == UiLanguage::English) {
                return;
            }

            auto const& english = state().english;
            for (auto const& [key, _] : english) {
                if (!outTable.contains(key)) {
                    log::warn("Localization file '{}' is missing key '{}', falling back to English", path.string(), key);
                }
            }

            for (auto const& [key, _] : outTable) {
                if (!english.contains(key)) {
                    log::warn("Localization file '{}' contains unexpected key '{}'", path.string(), key);
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
            case UiLanguage::English: return "English";
            case UiLanguage::Spanish: return "Spanish";
            case UiLanguage::Auto:
            default: return "Auto";
        }
    }

    std::string_view getLanguageDisplayName(UiLanguage language) {
        switch (language) {
            case UiLanguage::English: return "English";
            case UiLanguage::Spanish: return "Espa" "\xC3\xB1" "ol";
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

    std::string trf(std::string_view key, fmt::format_args args) {
        return detail::trfImpl(key, args);
    }

    namespace detail {
        std::string trfImpl(std::string_view key, fmt::format_args args) {
            auto pattern = std::string(tr(key));
            try {
                return fmt::vformat(pattern, args);
            } catch (std::exception const& error) {
                log::warn("Failed to format localization key '{}': {}", key, error.what());
                return pattern;
            }
        }
    }
}
