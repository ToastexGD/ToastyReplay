#ifndef _toasty_pride_mode_hpp
#define _toasty_pride_mode_hpp

#include <filesystem>
#include <vector>

namespace toasty::pride {

inline std::filesystem::path resolveLogoAssetPath(
    bool prideLogoEnabled,
    std::vector<std::filesystem::path> const& prideCandidates,
    std::vector<std::filesystem::path> const& defaultCandidates
) {
    if (prideLogoEnabled) {
        for (auto const& path : prideCandidates) {
            if (!path.empty() && std::filesystem::exists(path)) {
                return path;
            }
        }
    }

    for (auto const& path : defaultCandidates) {
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }

    return {};
}

}

#endif
