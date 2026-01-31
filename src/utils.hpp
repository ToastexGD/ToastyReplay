#ifndef _utils_hpp
#define _utils_hpp
#include <vector>

#define MAKE_STRING(X) #X

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
