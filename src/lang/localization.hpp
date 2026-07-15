#pragma once

#include <Geode/Geode.hpp>
#include <fmt/format.h>
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace toasty::lang {
    enum class UiLanguage {
        Auto,
        English,
        Spanish,
        French,
        Vietnamese,
        Chinese,
    };

    void initialize();
    void refresh();

    UiLanguage getConfiguredLanguage();
    UiLanguage getActiveLanguage();

    std::string_view getLanguageSettingValue(UiLanguage language);
    std::string_view getLanguageDisplayName(UiLanguage language);

    std::string_view tr(std::string_view key);

    struct FormatArg {
        std::string name;
        std::string value;
    };

    template <class T>
    FormatArg arg(std::string_view name, T&& value) {
        return {std::string(name), fmt::format("{}", std::forward<T>(value))};
    }

    namespace detail {
        std::string trfImpl(std::string_view key, std::span<FormatArg const> args);
    }

    template <class... Args>
    std::string trf(std::string_view key, Args&&... args) {
        static_assert((std::is_same_v<std::remove_cvref_t<Args>, FormatArg> && ...));
        std::array<FormatArg, sizeof...(Args)> values{std::forward<Args>(args)...};
        return detail::trfImpl(key, values);
    }
}
