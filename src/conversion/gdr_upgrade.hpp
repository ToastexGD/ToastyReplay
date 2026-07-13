#pragma once

#include "conversion/replay_import.hpp"

#include <Geode/Geode.hpp>

#include <filesystem>
#include <string>

namespace toasty::gdr_upgrade {

geode::Result<toasty::conversion::ReplayImportResult> upgradeLegacyGDRToTTR3(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
);

}
