#ifndef _macro_converter_hpp
#define _macro_converter_hpp

#include "replay.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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
    UvBot,
    TCBot,
};

enum class ConversionTarget {
    TTR,
    GDR,
};

struct ImportedInput {
    double time = 0.0;
    double sourceFrame = 0.0;
    int64_t tick = 0;
    uint64_t sequence = 0;
    int button = 1;
    bool player2 = false;
    bool pressed = false;
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
    std::vector<std::string> warnings;
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

const char* formatDisplayName(ReplayFormat format);
bool isSupportedFormat(ReplayFormat format);
std::vector<ReplayFormat> supportedFormats();
bool isForeignReplayExtension(std::filesystem::path const& path);

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
