#include "conversion/tcbot_import.hpp"

#include "conversion/tcbot_format.hpp"
#include "format/replay.hpp"
#include "format/ttr_format.hpp"
#include "utils.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>

namespace toasty::tcbot {
namespace {

constexpr uint64_t kSourceFormatId = 0x000000000054434dull;

int32_t tickForTime(double timeSeconds, double tps) {
    double raw = std::floor(std::max(0.0, timeSeconds) * tps + 0.000001);
    return static_cast<int32_t>(std::clamp(
        raw,
        0.0,
        static_cast<double>(std::numeric_limits<int32_t>::max())
    ));
}

}

geode::Result<toasty::conversion::ReplayImportResult> importToTTR3(
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
        return geode::Err("Could not read the TCBot replay.");
    }

    auto parsedResult = parse(*bytes);
    if (!parsedResult) {
        return geode::Err("{}", parsedResult.unwrapErr());
    }
    auto replay = std::move(parsedResult).unwrap();
    if (replay.inputs.empty()) {
        return geode::Err("The TCBot replay has no inputs to import.");
    }

    std::string baseName = toasty::pathToUtf8(sourcePath.stem());
    if (baseName.empty()) {
        baseName = "macro";
    }
    std::string outputName = ReplayStorage::makeUniqueReplayNameInDirectory(
        outputDirectory,
        baseName + "_ttr3"
    );
    auto outputPath = outputDirectory / (outputName + ".ttr3");
    bool hasSwift = std::any_of(replay.inputs.begin(), replay.inputs.end(), [](Input const& input) {
        return input.swift;
    });
    bool exactTiming = replay.dynamicTiming || hasSwift;

    TTRMacro macro;
    macro.fileFormat = TTRFileFormat::TTR3;
    macro.sourceFormatId = kSourceFormatId;
    macro.losslessVerified = false;
    macro.macroConverted = true;
    macro.bestEffort = true;
    macro.author = std::move(author);
    macro.name = outputName;
    macro.persistedName = outputName;
    macro.levelName = outputName;
    macro.framerate = replay.initialTps;
    macro.duration = replay.duration;
    macro.accuracyMode = exactTiming ? AccuracyMode::CBS : AccuracyMode::Vanilla;
    macro.exactCbsTiming = exactTiming;
    macro.platformerMode = std::any_of(replay.inputs.begin(), replay.inputs.end(), [](Input const& input) {
        return input.button > 1;
    });
    macro.twoPlayerMode = std::any_of(replay.inputs.begin(), replay.inputs.end(), [](Input const& input) {
        return input.player2;
    });
    macro.rngLocked = replay.hasSeed;
    macro.rngSeed = replay.hasSeed
        ? static_cast<uint32_t>(replay.seed ^ (replay.seed >> 32))
        : 0;
    macro.recordTimestamp = static_cast<int64_t>(std::time(nullptr));
    macro.tpsEvents.reserve(replay.tpsEvents.size());
    for (auto const& event : replay.tpsEvents) {
        macro.tpsEvents.push_back({event.timeSeconds, event.tps});
    }

    macro.inputs.reserve(replay.inputs.size());
    for (auto const& input : replay.inputs) {
        TTRInput converted;
        converted.tick = tickForTime(input.timeSeconds, replay.initialTps);
        converted.actionType = input.button;
        converted.setPlayer2(input.player2);
        converted.setPressed(input.pressed);
        converted.timeSeconds = input.timeSeconds;
        converted.swiftPairAnchor = input.swift && input.pressed;
        if (exactTiming) {
            double snappedTime = static_cast<double>(converted.tick) / replay.initialTps;
            converted.cbsTimeOffset = std::max(0.0, input.timeSeconds - snappedTime);
            converted.stepOffset = static_cast<float>(std::clamp(
                converted.cbsTimeOffset * replay.initialTps,
                0.0,
                0.999999
            ));
        }
        macro.inputs.push_back(converted);
    }

    auto outputBytes = macro.serialize();
    if (outputBytes.empty()) {
        return geode::Err("Failed to serialize the TCBot replay as TTR3.");
    }
    auto writeResult = geode::utils::file::writeBinarySafe(outputPath, outputBytes);
    if (!writeResult) {
        return geode::Err("Failed to write the TTR3 replay: {}", writeResult.unwrapErr());
    }

    toasty::conversion::ReplayImportResult result;
    result.outputName = outputName;
    result.outputPath = outputPath;
    result.inputCount = replay.inputs.size();
    result.tps = replay.initialTps;
    result.message = fmt::format(
        "Imported {} TCBot inputs at {} TPS to {}.ttr3",
        result.inputCount,
        result.tps,
        result.outputName
    );
    return geode::Ok(std::move(result));
}

}
