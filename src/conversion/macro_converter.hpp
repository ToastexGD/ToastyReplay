#ifndef _macro_converter_hpp
#define _macro_converter_hpp

#include "format/replay.hpp"
#include "format/ttr_format.hpp"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toasty::conversion {

enum class ReplayFormat {
    Unknown,
    MegaHackJson,
    MegaHackBinary,
    TasBotJson,
    ZBotFrame,
    OmegaBot,
    YBotFrame,
    YBot2,
    Echo,
    Amethyst,
    OsuReplay,
    GDMO,
    ReplayBot,
    Rush,
    KDBot,
    Plaintext,
    DDHOR,
    XBotFrame,
    XdBot,
    QBot,
    RBot,
    Zephyrus,
    ReplayEngine1,
    ReplayEngine2,
    ReplayEngine3,
    Silicate1,
    Silicate2,
    Silicate3,
    GDR2,
    GdrJson,
    UvBot,
    TCBot,
};

enum class ConversionTarget {
    TTR3,
    GDR,
};

struct ImportedTpsEvent {
    double timeSeconds = 0.0;
    double tps = 240.0;
};

enum class TTR3Route {
    LosslessTTR3,
    Blocked,
    Unsupported,
};

struct TTR3InspectionFacts {
    bool hasTcbotV2VersionByte = false;
    bool hasGdmoCorrectionRecords = false;
    bool hasFractionalPlaintextFrames = false;
    bool hasDecodedGdr2Extensions = false;
    bool hasUvBotPhysicsAnchors = false;
    bool ybot2SourceTpsHighEnough = false;
};

struct TTR3Eligibility {
    TTR3Route route = TTR3Route::Unsupported;
    bool lossless = false;
    std::string message;
};

struct ImportedInput {
    double time = 0.0;
    double sourceFrame = 0.0;
    int64_t tick = 0;
    uint64_t sequence = 0;
    int button = 1;
    bool player2 = false;
    bool pressed = false;
    bool swift = false;
    float stepOffset = 0.0f;
    double cbsTimeOffset = -1.0;
};

struct ImportedReplay {
    ReplayFormat format = ReplayFormat::Unknown;
    std::string sourceName;
    std::string levelName;
    int32_t levelId = 0;
    double fps = 240.0;
    double duration = 0.0;
    bool platformerMode = false;
    bool twoPlayerMode = false;
    bool dynamicTiming = false;
    bool needsCbsTiming = false;
    std::vector<ImportedInput> inputs;
    std::vector<PlaybackAnchor> anchors;
    std::vector<ImportedTpsEvent> tpsEvents;
    std::vector<std::string> warnings;
    bool sourceLosslessVerified = false;
    bool hasFractionalPlaintextFrames = false;
    bool hasDecodedGdr2Extensions = false;
    bool convertedForTTR3 = false;
};

struct DetectedReplay {
    std::filesystem::path path;
    std::string stem;
    std::string filename;
    ReplayFormat format = ReplayFormat::Unknown;
    bool recognized = false;
    bool supported = false;
    bool converted = false;
    std::string detail;
};

struct ConversionResult {
    bool ok = false;
    std::string outputName;
    std::filesystem::path outputPath;
    ReplayFormat detectedFormat = ReplayFormat::Unknown;
    size_t inputCount = 0;
    double fps = 240.0;
    std::string message;
    std::vector<std::string> warnings;
};

inline bool isFiniteTTR3Fps(double fps) {
    return std::isfinite(fps) && fps >= 1.0 && fps <= 1000000.0;
}

inline double safeTTR3Fps(double fps) {
    return isFiniteTTR3Fps(fps) ? fps : 240.0;
}

inline int sanitizeTTR3Button(int button) {
    return button >= 1 && button <= 3 ? button : 1;
}

inline int32_t materializeTTR3Tick(double timeSeconds, double fps) {
    double tick = std::floor(std::max(0.0, timeSeconds) * safeTTR3Fps(fps));
    return static_cast<int32_t>(std::clamp<double>(
        tick,
        0.0,
        static_cast<double>(std::numeric_limits<int32_t>::max())
    ));
}

inline void finishImportForTTR3(ImportedReplay& replay) {
    replay.convertedForTTR3 = true;
    replay.fps = safeTTR3Fps(replay.fps);

    std::stable_sort(replay.inputs.begin(), replay.inputs.end(), [](ImportedInput const& a, ImportedInput const& b) {
        if (a.time != b.time) return a.time < b.time;
        if (a.sourceFrame != b.sourceFrame) return a.sourceFrame < b.sourceFrame;
        return a.sequence < b.sequence;
    });

    constexpr double kDuplicateEpsilon = 0.000001;
    std::unordered_map<int, double> lastTransitionTime;
    std::vector<ImportedInput> normalized;
    normalized.reserve(replay.inputs.size());

    for (auto input : replay.inputs) {
        input.button = sanitizeTTR3Button(input.button);
        if (!std::isfinite(input.time) || input.time < 0.0) {
            input.time = std::isfinite(input.sourceFrame) && input.sourceFrame >= 0.0
                ? input.sourceFrame / replay.fps
                : 0.0;
        }
        if (!std::isfinite(input.sourceFrame) || input.sourceFrame < 0.0) {
            input.sourceFrame = input.time * replay.fps;
        }

        int transitionKey = input.button
            | (input.player2 ? 0x100 : 0)
            | (input.pressed ? 0x200 : 0);
        auto last = lastTransitionTime.find(transitionKey);
        if (last != lastTransitionTime.end() && std::abs(input.time - last->second) <= kDuplicateEpsilon) {
            continue;
        }
        lastTransitionTime[transitionKey] = input.time;

        input.tick = materializeTTR3Tick(input.time, replay.fps);
        input.cbsTimeOffset = std::max(0.0, input.time - static_cast<double>(input.tick) / replay.fps);
        input.stepOffset = static_cast<float>(std::clamp(input.cbsTimeOffset * replay.fps, 0.0, 0.999999));
        input.sequence = static_cast<uint64_t>(normalized.size());
        normalized.push_back(input);
    }

    replay.inputs = std::move(normalized);
    if (!replay.inputs.empty()) {
        replay.duration = std::max(replay.duration, replay.inputs.back().time);
    }
}

inline void finishImportForTarget(ImportedReplay& replay, ConversionTarget target) {
    if (target == ConversionTarget::TTR3) {
        finishImportForTTR3(replay);
    }
}

inline TTRMacro buildTTR3FromImported(
    ImportedReplay const& imported,
    std::string outputName,
    std::string author
) {
    double fps = safeTTR3Fps(imported.fps);

    TTRMacro macro;
    macro.fileFormat = TTRFileFormat::TTR3;
    macro.sourceFormatId = static_cast<uint64_t>(imported.format);
    macro.losslessVerified = true;
    macro.macroConverted = true;
    macro.bestEffort = false;
    macro.author = std::move(author);
    macro.name = outputName;
    macro.persistedName = outputName;
    macro.levelName = imported.levelName.empty() ? outputName : imported.levelName;
    macro.levelId = imported.levelId;
    macro.framerate = fps;
    macro.duration = std::max(0.0, imported.duration);
    macro.accuracyMode = AccuracyMode::CBS;
    macro.exactCbsTiming = true;
    macro.platformerMode = imported.platformerMode;
    macro.twoPlayerMode = imported.twoPlayerMode;
    macro.anchors = imported.anchors;
    macro.tpsEvents.clear();
    macro.tpsEvents.reserve(imported.tpsEvents.size());
    for (auto const& event : imported.tpsEvents) {
        macro.tpsEvents.push_back({ event.timeSeconds, event.tps });
    }
    macro.recordTimestamp = static_cast<int64_t>(std::time(nullptr));

    if (macro.tpsEvents.empty()) {
        macro.tpsEvents.push_back({0.0, fps});
    }

    for (auto& anchor : macro.anchors) {
        if (!anchor.hasAbsoluteTime()) {
            anchor.timeSeconds = static_cast<double>(std::max(0, anchor.tick)) / fps;
        }
        macro.twoPlayerMode = macro.twoPlayerMode || anchor.hasPlayer2;
    }

    macro.inputs.reserve(imported.inputs.size());
    for (auto const& input : imported.inputs) {
        double timeSeconds = std::isfinite(input.time) && input.time >= 0.0 ? input.time : 0.0;
        TTRInput out;
        out.tick = materializeTTR3Tick(timeSeconds, fps);
        out.actionType = static_cast<uint8_t>(sanitizeTTR3Button(input.button));
        out.setPlayer2(input.player2);
        out.setPressed(input.pressed);
        out.timeSeconds = timeSeconds;
        out.swiftPairAnchor = input.swift && input.pressed;
        out.cbsTimeOffset = std::max(0.0, timeSeconds - static_cast<double>(out.tick) / fps);
        out.stepOffset = static_cast<float>(std::clamp(out.cbsTimeOffset * fps, 0.0, 0.999999));
        macro.twoPlayerMode = macro.twoPlayerMode || input.player2;
        macro.duration = std::max(macro.duration, timeSeconds);
        macro.inputs.push_back(out);
    }

    return macro;
}

const char* formatDisplayName(ReplayFormat format);
bool isSupportedFormat(ReplayFormat format);
std::vector<ReplayFormat> supportedFormats();
bool isForeignReplayExtension(std::filesystem::path const& path);
inline constexpr char kTTR3BlockedWarning[] =
    "This macro format does not store enough timing information for frame-perfect TTR3 conversion. "
    "Re-record the macro on ToastyReplay for guaranteed accuracy.";

inline TTR3Eligibility inspectTTR3Eligibility(ReplayFormat format, TTR3InspectionFacts const& facts) {
    auto lossless = [] {
        return TTR3Eligibility {
            TTR3Route::LosslessTTR3,
            true,
            "Frame-perfect TTR3 conversion is available."
        };
    };
    auto blocked = [] {
        return TTR3Eligibility {
            TTR3Route::Blocked,
            false,
            kTTR3BlockedWarning
        };
    };
    auto unsupported = [] {
        return TTR3Eligibility {
            TTR3Route::Unsupported,
            false,
            "This macro format is not supported for TTR3 conversion."
        };
    };

    switch (format) {
        case ReplayFormat::Silicate2:
        case ReplayFormat::Silicate3:
        case ReplayFormat::DDHOR:
        case ReplayFormat::Amethyst:
        case ReplayFormat::Zephyrus:
        case ReplayFormat::ReplayEngine1:
        case ReplayFormat::ReplayEngine3:
            return lossless();

        case ReplayFormat::TCBot:
            return facts.hasTcbotV2VersionByte ? lossless() : blocked();
        case ReplayFormat::GDMO:
            return facts.hasGdmoCorrectionRecords ? lossless() : blocked();
        case ReplayFormat::Plaintext:
            return facts.hasFractionalPlaintextFrames ? lossless() : blocked();
        case ReplayFormat::GDR2:
            return facts.hasDecodedGdr2Extensions ? lossless() : blocked();
        case ReplayFormat::YBot2:
            return facts.ybot2SourceTpsHighEnough ? lossless() : blocked();
        case ReplayFormat::UvBot:
            return facts.hasUvBotPhysicsAnchors ? lossless() : blocked();

        case ReplayFormat::MegaHackJson:
        case ReplayFormat::MegaHackBinary:
        case ReplayFormat::TasBotJson:
        case ReplayFormat::ZBotFrame:
        case ReplayFormat::YBotFrame:
        case ReplayFormat::Echo:
        case ReplayFormat::ReplayBot:
        case ReplayFormat::Rush:
        case ReplayFormat::KDBot:
        case ReplayFormat::XBotFrame:
        case ReplayFormat::XdBot:
        case ReplayFormat::GdrJson:
        case ReplayFormat::RBot:
        case ReplayFormat::ReplayEngine2:
        case ReplayFormat::Silicate1:
            return blocked();

        case ReplayFormat::Unknown:
        case ReplayFormat::OmegaBot:
        case ReplayFormat::OsuReplay:
        case ReplayFormat::QBot:
            return unsupported();
    }

    return unsupported();
}

DetectedReplay detectReplay(
    std::filesystem::path const& path,
    std::unordered_set<std::string> const& convertedSources = {},
    std::unordered_set<std::string> const& usableStems = {}
);

std::optional<ImportedReplay> importReplay(
    std::filesystem::path const& path,
    std::string* error = nullptr
);

ConversionResult convertReplay(
    std::filesystem::path const& sourcePath,
    ConversionTarget target,
    std::string requestedName,
    std::string author,
    std::filesystem::path const& outputDirectory
);

ConversionResult convertNativeGDRToTTRDuplicate(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
);

std::string normalizedPathKey(std::filesystem::path const& path);

}

#endif
