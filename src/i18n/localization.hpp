#pragma once

#include <Geode/Geode.hpp>
#include <fmt/format.h>
#include <string>
#include <string_view>

namespace toasty::i18n {
    enum class UiLanguage {
        Auto,
        English,
        Spanish,
    };

    void initialize();
    void refresh();

    UiLanguage getConfiguredLanguage();
    UiLanguage getActiveLanguage();

    std::string_view getLanguageSettingValue(UiLanguage language);
    std::string_view getLanguageDisplayName(UiLanguage language);

    std::string_view tr(std::string_view key);
    std::string trf(std::string_view key, fmt::format_args args);

    namespace detail {
        std::string trfImpl(std::string_view key, fmt::format_args args);
    }

    template <class... Args>
    std::string trf(std::string_view key, Args&&... args) {
        return detail::trfImpl(key, fmt::make_format_args(args...));
    }
}
