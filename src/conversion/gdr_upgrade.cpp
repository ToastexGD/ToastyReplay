#include "conversion/gdr_upgrade.hpp"

#include "format/replay.hpp"
#include "format/ttr_format.hpp"
#include "utils.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <limits>
#include <memory>

using namespace geode::prelude;

namespace toasty::gdr_upgrade {
namespace {

double safeFps(double fps) {
    return std::isfinite(fps) && fps >= 1.0 && fps <= 1000000.0 ? fps : 240.0;
}

double replayDuration(MacroSequence const& replay, double fps) {
    double duration = std::isfinite(replay.duration) && replay.duration >= 0.0 ? replay.duration : 0.0;
    for (auto const& input : replay.inputs) {
        duration = std::max(duration, static_cast<double>(input.frame) / fps);
    }
    return duration;
}

bool hasTimedPlaybackData(MacroSequence const& replay) {
    if (usesTimedAccuracy(replay.accuracyMode) || !replay.anchors.empty()) {
        return true;
    }
    return std::any_of(replay.inputs.begin(), replay.inputs.end(), [](MacroAction const& input) {
        return input.stepOffset != 0.0f || input.hasAbsoluteTime() || input.swiftPairAnchor;
    });
}

std::string sourceStem(std::filesystem::path const& path) {
    std::string filename = toasty::pathToUtf8(path.filename());
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    constexpr std::string_view suffix = ".gdr.json";
    if (lower.size() > suffix.size() && lower.ends_with(suffix)) {
        return filename.substr(0, filename.size() - suffix.size());
    }
    return toasty::pathToUtf8(path.stem());
}

}

Result<toasty::conversion::ReplayImportResult> upgradeLegacyGDRToTTR3(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
) {
    auto directoryResult = utils::file::createDirectoryAll(outputDirectory);
    if (!directoryResult) {
        return Err("Replay folder is unavailable: {}", directoryResult.unwrapErr());
    }

    auto bytes = ReplayStorage::readReplayBytes(sourcePath);
    if (!bytes) {
        return Err("Could not read the legacy GDR replay.");
    }

    auto imported = MacroSequence::tryImportData(*bytes);
    if (!imported) {
        return Err("The file is not a valid GDR or GDR JSON replay.");
    }

    imported->inferMissingPlatformerMode();
    if (imported->inputs.empty()) {
        return Err("The GDR replay has no inputs to upgrade.");
    }
    if (hasTimedPlaybackData(*imported)) {
        return Err("Timed legacy GDR replays are playback-only because exact conversion is not verified.");
    }

    std::string baseName = sourceStem(sourcePath);
    if (baseName.empty()) {
        baseName = imported->name.empty() ? "macro" : imported->name;
    }
    std::string outputName = ReplayStorage::makeUniqueReplayNameInDirectory(
        outputDirectory,
        baseName + "_ttr3"
    );
    auto outputPath = outputDirectory / (outputName + ".ttr3");
    double fps = safeFps(imported->framerate);

    TTRMacro macro;
    macro.fileFormat = TTRFileFormat::TTR3;
    macro.sourceFormatId = 0;
    macro.losslessVerified = true;
    macro.macroConverted = true;
    macro.author = author.empty() ? imported->author : std::move(author);
    macro.name = outputName;
    macro.persistedName = outputName;
    macro.levelName = imported->levelInfo.name.empty() ? outputName : imported->levelInfo.name;
    macro.levelId = imported->levelInfo.id;
    macro.framerate = fps;
    macro.duration = replayDuration(*imported, fps);
    macro.accuracyMode = AccuracyMode::Vanilla;
    macro.platformerMode = imported->platformerMode;
    macro.twoPlayerMode = std::any_of(imported->inputs.begin(), imported->inputs.end(), [](MacroAction const& input) {
        return input.player2;
    });
    macro.recordedFromStartPos = imported->recordedFromStartPos;
    macro.startPosX = imported->startPosX;
    macro.startPosY = imported->startPosY;
    macro.exactCbsTiming = false;
    macro.tpsEvents = {{0.0, fps}};
    macro.recordTimestamp = static_cast<int64_t>(std::time(nullptr));
    macro.inputs.reserve(imported->inputs.size());

    for (auto const& input : imported->inputs) {
        macro.recordAction(
            static_cast<int>(std::clamp<int64_t>(
                static_cast<int64_t>(input.frame),
                0,
                static_cast<int64_t>(std::numeric_limits<int32_t>::max())
            )),
            input.button,
            input.player2,
            input.down,
            0.0f,
            -1.0
        );
    }

    auto outputBytes = macro.serialize();
    if (outputBytes.empty()) {
        return Err("Failed to serialize the TTR3 replay.");
    }

    auto writeResult = utils::file::writeBinarySafe(outputPath, outputBytes);
    if (!writeResult) {
        return Err("Failed to write the TTR3 replay: {}", writeResult.unwrapErr());
    }

    toasty::conversion::ReplayImportResult result;
    result.outputName = outputName;
    result.outputPath = outputPath;
    result.inputCount = imported->inputs.size();
    result.tps = fps;
    result.message = fmt::format(
        "Upgraded {} inputs at {} TPS to {}.ttr3",
        result.inputCount,
        result.tps,
        result.outputName
    );
    log::debug(
        "Upgraded legacy GDR replay to '{}' with {} inputs at {:.1f} TPS",
        toasty::pathToUtf8(result.outputPath),
        result.inputCount,
        result.tps
    );
    return Ok(std::move(result));
}

}
