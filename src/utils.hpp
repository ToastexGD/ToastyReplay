#ifndef _utils_hpp
#define _utils_hpp

#include <Geode/utils/string.hpp>

#include <charconv>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#define MAKE_STRING(X) #X

namespace toasty {
    template <typename Int>
    inline std::optional<Int> parseInteger(std::string_view text, int base = 10) {
        static_assert(std::is_integral_v<Int>, "parseInteger requires an integral type");

        Int value = 0;
        auto const* begin = text.data();
        auto const* end = begin + text.size();
        auto result = std::from_chars(begin, end, value, base);
        if (result.ec != std::errc{} || result.ptr != end) {
            return std::nullopt;
        }
        return value;
    }

    inline std::string pathToUtf8(std::filesystem::path const& path) {
        return geode::utils::string::pathToString(path);
    }

    inline std::filesystem::path stringToPath(std::string_view value) {
#ifdef GEODE_IS_WINDOWS
        std::u8string utf8Value;
        utf8Value.reserve(value.size());
        for (char ch : value) {
            utf8Value.push_back(static_cast<char8_t>(ch));
        }
        return std::filesystem::path(utf8Value);
#else
        return std::filesystem::path(value);
#endif
    }
}

template<typename T, typename U> constexpr std::pair<ptrdiff_t, size_t> createMemberOffset(U T::*field)
{
    return std::pair((ptrdiff_t)&((T*)nullptr->*field), sizeof(U));
}

template<typename T> std::vector<std::byte> extractFieldData(T* obj, std::pair<ptrdiff_t, size_t> fieldOffset) {
    std::vector<std::byte> buffer(fieldOffset.second);
    std::memcpy((void*)buffer.data(), (void*)((uintptr_t)obj + fieldOffset.first), fieldOffset.second);

    return buffer;
};

template<typename T> void applyFieldData(T* obj, std::pair<ptrdiff_t, size_t> fieldOffset, std::vector<std::byte> buffer) {
    std::memcpy((void*)((uintptr_t)obj + fieldOffset.first), (void*)buffer.data(), fieldOffset.second);
};

#endif
