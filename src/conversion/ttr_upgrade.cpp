#include "conversion/ttr_upgrade.hpp"

#include "format/ttr3_format.hpp"
#include "format/ttr_format.hpp"

#include <cmath>
#include <optional>
#include <string_view>

namespace toasty::ttr_upgrade {
namespace {

constexpr size_t kMaxTTR3StringSize = 4096;

std::optional<std::string> normalizeLegacyOffsets(
    std::vector<TTRInput>& inputs,
    double tps,
    std::string_view label
) {
    for (auto& input : inputs) {
        if (input.hasAbsoluteTime() || input.cbsTimeOffset != -1.0) {
            continue;
        }
        if (!std::isfinite(input.stepOffset) || input.stepOffset < 0.0f || input.stepOffset >= 1.0f) {
            return std::string(label) + " contains an invalid legacy sub-frame offset.";
        }
        if (input.stepOffset > 0.0f) {
            input.cbsTimeOffset = static_cast<double>(input.stepOffset) / tps;
        }
    }
    return std::nullopt;
}

std::optional<std::string> normalizeLegacyTiming(TTRMacro& source) {
    if (source.fileFormat != TTRFileFormat::LegacyTTR || !usesTimedAccuracy(source.accuracyMode)) {
        return std::nullopt;
    }
    if (!std::isfinite(source.framerate) || source.framerate <= 0.0) {
        return "The replay has an invalid TPS value.";
    }
    if (auto error = normalizeLegacyOffsets(source.inputs, source.framerate, "The replay")) {
        return error;
    }
    for (auto& attempt : source.persistenceAttempts) {
        if (auto error = normalizeLegacyOffsets(attempt.inputs, source.framerate, "A persistence attempt")) {
            return error;
        }
    }
    return std::nullopt;
}

std::optional<std::string> validateInputs(
    std::vector<TTRInput> const& inputs,
    double tps,
    std::string_view label
) {
    double previousTime = -1.0;
    for (auto const& input : inputs) {
        if (input.tick < 0) {
            return std::string(label) + " contains a negative input tick.";
        }
        if (input.actionType < 1 || input.actionType > 3) {
            return std::string(label) + " contains an invalid input button.";
        }
        if ((input.flags & ~0x03u) != 0) {
            return std::string(label) + " contains unsupported input flags.";
        }

        double time = static_cast<double>(input.tick) / tps;
        if (input.cbsTimeOffset != -1.0) {
            if (!std::isfinite(input.cbsTimeOffset) || input.cbsTimeOffset < 0.0) {
                return std::string(label) + " contains an invalid sub-frame time.";
            }
            time += input.cbsTimeOffset;
        }
        if (!std::isfinite(time) || time < previousTime) {
            return std::string(label) + " contains inputs that cannot be ordered exactly.";
        }
        previousTime = time;
    }
    return std::nullopt;
}

std::optional<std::string> validateAnchors(
    std::vector<PlaybackAnchor> const& anchors,
    std::string_view label
) {
    int32_t previousTick = -1;
    for (auto const& anchor : anchors) {
        if (anchor.tick < 0 || anchor.tick < previousTick) {
            return std::string(label) + " contains anchors that cannot be ordered exactly.";
        }
        previousTick = anchor.tick;
    }
    return std::nullopt;
}

std::optional<std::string> validateSource(TTRMacro const& source) {
    if (source.fileFormat != TTRFileFormat::TTR2 && source.fileFormat != TTRFileFormat::LegacyTTR) {
        return "Only TTR and TTR2 replays can be upgraded.";
    }
    if (!std::isfinite(source.framerate) || source.framerate < 1.0 || source.framerate > 1000000.0) {
        return "The replay has an invalid TPS value.";
    }
    if (!std::isfinite(source.duration) || source.duration < 0.0) {
        return "The replay has an invalid duration.";
    }
    if (!std::isfinite(source.startPosX) || !std::isfinite(source.startPosY)) {
        return "The replay has invalid start position data.";
    }
    if (source.author.size() > kMaxTTR3StringSize || source.levelName.size() > kMaxTTR3StringSize) {
        return "The replay metadata is too large for TTR3.";
    }
    if (source.inputs.empty() && source.persistenceAttempts.empty()) {
        return "The replay has no inputs to upgrade.";
    }
    if (auto error = validateInputs(source.inputs, source.framerate, "The replay")) {
        return error;
    }
    if (auto error = validateAnchors(source.anchors, "The replay")) {
        return error;
    }

    int32_t previousCheckpointTick = -1;
    for (auto const& checkpoint : source.checkpoints) {
        if (checkpoint.tick < 0 || checkpoint.priorTick < 0 || checkpoint.tick < previousCheckpointTick) {
            return "The replay contains checkpoints that cannot be ordered exactly.";
        }
        previousCheckpointTick = checkpoint.tick;
    }

    for (auto const& attempt : source.persistenceAttempts) {
        if (attempt.deathTick < 0) {
            return "A persistence attempt has an invalid death tick.";
        }
        if (auto error = validateInputs(attempt.inputs, source.framerate, "A persistence attempt")) {
            return error;
        }
        if (auto error = validateAnchors(attempt.anchors, "A persistence attempt")) {
            return error;
        }
    }
    return std::nullopt;
}

}

geode::Result<toasty::conversion::ReplayImportPayload> convertLegacyTTRToTTR3(
    TTRMacro const& source,
    std::string name,
    std::string author,
    int64_t timestamp
) {
    auto normalized = source;
    if (auto error = normalizeLegacyTiming(normalized)) {
        return geode::Err("{}", *error);
    }
    if (auto error = validateSource(normalized)) {
        return geode::Err("{}", *error);
    }
    if (name.size() > kMaxTTR3StringSize) {
        return geode::Err("The output replay name is too large for TTR3.");
    }

    auto macro = toasty::ttr3::fromTTRMacro(normalized);
    macro.sourceFormatId = normalized.fileFormat == TTRFileFormat::TTR2
        ? kTTR2SourceFormatId
        : kLegacyTTRSourceFormatId;
    macro.name = std::move(name);
    if (macro.author.empty()) {
        macro.author = std::move(author);
    }
    if (macro.recordTimestamp == 0) {
        macro.recordTimestamp = timestamp;
    }
    macro.losslessVerified = true;
    macro.macroConverted = true;

    auto bytes = toasty::ttr3::serialize(macro);
    if (bytes.empty()) {
        return geode::Err("Failed to serialize the replay as TTR3.");
    }

    std::string parseError;
    auto verified = toasty::ttr3::deserialize(bytes, &parseError);
    if (!verified) {
        return geode::Err("The converted TTR3 replay failed validation: {}", parseError);
    }
    if (verified->inputs.size() != macro.inputs.size() ||
        verified->anchors.size() != macro.anchors.size() ||
        verified->checkpoints.size() != macro.checkpoints.size() ||
        verified->persistenceAttempts.size() != macro.persistenceAttempts.size()) {
        return geode::Err("The converted TTR3 replay did not preserve every replay section.");
    }

    toasty::conversion::ReplayImportPayload payload;
    payload.bytes = std::move(bytes);
    payload.inputCount = normalized.inputs.size();
    payload.tps = normalized.framerate;
    payload.fidelity = toasty::conversion::ReplayImportFidelity::Verified;
    return geode::Ok(std::move(payload));
}

}
