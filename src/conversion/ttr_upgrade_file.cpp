#include "conversion/ttr_upgrade.hpp"

#include "format/replay.hpp"
#include "format/ttr_format.hpp"
#include "utils.hpp"

#include <Geode/utils/file.hpp>

#include <ctime>
#include <memory>

namespace toasty::ttr_upgrade {

geode::Result<toasty::conversion::ReplayImportResult> upgradeLegacyTTRToTTR3(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
) {
    auto directoryResult = geode::utils::file::createDirectoryAll(outputDirectory);
    if (!directoryResult) {
        return geode::Err("Replay folder is unavailable: {}", directoryResult.unwrapErr());
    }

    auto bytes = ReplayStorage::readReplayBytes(sourcePath);
    if (!bytes) {
        return geode::Err("Could not read the TTR replay.");
    }

    std::unique_ptr<TTRMacro> source(TTRMacro::deserialize(*bytes));
    if (!source) {
        return geode::Err("The file is not a valid TTR or TTR2 replay.");
    }
    if (source->fileFormat == TTRFileFormat::TTR3) {
        return geode::Err("The replay is already stored as TTR3.");
    }

    std::string baseName = toasty::pathToUtf8(sourcePath.stem());
    if (baseName.empty()) {
        baseName = "macro";
    }
    std::string outputName = ReplayStorage::makeUniqueReplayNameInDirectory(
        outputDirectory,
        baseName + "_ttr3"
    );

    auto converted = convertLegacyTTRToTTR3(
        *source,
        outputName,
        std::move(author),
        static_cast<int64_t>(std::time(nullptr))
    );
    if (!converted) {
        return geode::Err("{}", converted.unwrapErr());
    }
    auto payload = std::move(converted).unwrap();

    auto outputPath = outputDirectory / (outputName + ".ttr3");
    auto writeResult = geode::utils::file::writeBinarySafe(outputPath, payload.bytes);
    if (!writeResult) {
        return geode::Err("Failed to write the TTR3 replay: {}", writeResult.unwrapErr());
    }

    toasty::conversion::ReplayImportResult result;
    result.outputName = outputName;
    result.outputPath = outputPath;
    result.inputCount = payload.inputCount;
    result.tps = payload.tps;
    result.fidelity = payload.fidelity;
    result.message = fmt::format(
        "Upgraded {} inputs at {} TPS to {}.ttr3",
        result.inputCount,
        result.tps,
        result.outputName
    );
    geode::log::debug(
        "Upgraded ToastyReplay macro to '{}' with {} inputs at {:.1f} TPS",
        toasty::pathToUtf8(result.outputPath),
        result.inputCount,
        result.tps
    );
    return geode::Ok(std::move(result));
}

}
