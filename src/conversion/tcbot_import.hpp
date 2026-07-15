#pragma once

#include "conversion/replay_import.hpp"

#include <Geode/Result.hpp>

#include <filesystem>
#include <string>

namespace toasty::tcbot {

geode::Result<toasty::conversion::ReplayImportResult> importToTTR3(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
);

}
