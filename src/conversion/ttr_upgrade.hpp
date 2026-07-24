#pragma once

#include "conversion/replay_import.hpp"

#include <Geode/Result.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

class TTRMacro;

namespace toasty::ttr_upgrade {

inline constexpr uint64_t kLegacyTTRSourceFormatId = 0x00000000FFFF0001ull;
inline constexpr uint64_t kTTR2SourceFormatId = 0x00000000FFFF0002ull;

geode::Result<toasty::conversion::ReplayImportPayload> convertLegacyTTRToTTR3(
    TTRMacro const& source,
    std::string name,
    std::string author,
    int64_t timestamp
);

geode::Result<toasty::conversion::ReplayImportResult> upgradeLegacyTTRToTTR3(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
);

}
