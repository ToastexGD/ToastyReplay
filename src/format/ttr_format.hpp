#ifndef _ttr_format_hpp
#define _ttr_format_hpp

#include "replay.hpp"

#include <Geode/Geode.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace geode::prelude;

#define TTR_FORMAT_VERSION 6
#define TTR2_FORMAT_VERSION 2
#define TTR_MAGIC "TTR"
#define TTR2_MAGIC "TTR2"

enum class TTRFileFormat : uint8_t {
    TTR2 = 0,
    LegacyTTR = 1,
    TTR3 = 2,
};

enum TTRFlags : uint32_t {
    TTR_FLAG_ACCURACY_CBS = 1 << 0,
    TTR_FLAG_FROM_START_POS = 1 << 1,
    TTR_FLAG_PLATFORMER = 1 << 2,
    TTR_FLAG_TWO_PLAYER = 1 << 3,
    TTR_FLAG_RNG_LOCKED = 1 << 4,
    TTR_FLAG_ACCURACY_CBF = 1 << 5,
    TTR_FLAG_ACCURACY_SUBSTEP = 1 << 6,
    TTR_FLAG_EXACT_CBS_TIMING = 1 << 7,
    TTR_FLAG_PERSISTENCE = 1 << 8,
};

struct TTRInput {
    int32_t tick = 0;
    uint8_t actionType = 0;
    uint8_t flags = 0;
    float stepOffset = 0.0f;
    double cbsTimeOffset = -1.0;
    double timeSeconds = -1.0;
    bool swiftPairAnchor = false;

    bool isPlayer2() const { return (flags & 0x01) != 0; }
    bool isPressed() const { return (flags & 0x02) != 0; }
    bool hasAbsoluteTime() const { return std::isfinite(timeSeconds) && timeSeconds >= 0.0; }

    void setPlayer2(bool value) { flags = (flags & ~0x01) | (value ? 0x01 : 0x00); }
    void setPressed(bool value) { flags = (flags & ~0x02) | (value ? 0x02 : 0x00); }
};

struct TTRCheckpoint {
    int32_t tick = 0;
    uint64_t rngState = 0;
    int32_t priorTick = 0;
    double timeSeconds = -1.0;
    double priorTimeSeconds = -1.0;

    bool hasAbsoluteTime() const { return std::isfinite(timeSeconds) && timeSeconds >= 0.0; }
    bool hasAbsolutePriorTime() const { return std::isfinite(priorTimeSeconds) && priorTimeSeconds >= 0.0; }
};

struct TTRTpsEvent {
    double timeSeconds = 0.0;
    double tps = 240.0;
};

struct TTRAttemptSegment {
    int32_t deathTick = 0;
    double deathTimeSeconds = -1.0;
    bool deathPlayer2 = false;
    std::vector<TTRInput> inputs;
    std::vector<PlaybackAnchor> anchors;

    bool hasAbsoluteDeathTime() const {
        return std::isfinite(deathTimeSeconds) && deathTimeSeconds >= 0.0;
    }

    bool hasData() const {
        return deathTick > 0 || hasAbsoluteDeathTime() || !inputs.empty() || !anchors.empty();
    }
};

class TTRMacro {
public:
    std::string author;
    std::string name;
    std::string persistedName;
    std::string levelName;
    int32_t levelId = 0;
    double framerate = 240.0;
    double duration = 0.0;
    uint32_t gameVersion = 0;
    float startPosX = 0.f;
    float startPosY = 0.f;
    bool recordedFromStartPos = false;
    AccuracyMode accuracyMode = AccuracyMode::Vanilla;
    bool platformerMode = false;
    bool twoPlayerMode = false;
    bool rngLocked = false;
    bool exactCbsTiming = false;
    TTRFileFormat fileFormat = TTRFileFormat::TTR2;
    uint64_t sourceFormatId = 0x00000000FFFF0003ull;
    uint32_t rngSeed = 0;
    int64_t recordTimestamp = 0;
    bool losslessVerified = false;
    bool macroConverted = false;
    bool bestEffort = false;
    std::vector<TTRInput> inputs;
    std::vector<TTRTpsEvent> tpsEvents;
    std::vector<PlaybackAnchor> anchors;
    std::vector<TTRCheckpoint> checkpoints;
    std::vector<TTRAttemptSegment> persistenceAttempts;

    bool loadedFromLegacyFormat() const { return fileFormat == TTRFileFormat::LegacyTTR; }
    bool loadedFromTTR3() const { return fileFormat == TTRFileFormat::TTR3; }
    double maxSourceTps() const;
    void materializeTTR3RuntimeTicks(double runtimeTps);
    void recordAction(int tick, int button, bool player2, bool pressed, float offset, double cbsTimeOffset = -1.0);
    void recordAction(std::vector<TTRInput>& target, int tick, int button, bool player2, bool pressed, float offset, double cbsTimeOffset = -1.0);
    void recordAnchor(int tick, PlayerObject* p1, PlayerObject* p2, bool isPlatformer, bool isDual = true);
    void recordAnchor(std::vector<PlaybackAnchor>& target, int tick, PlayerObject* p1, PlayerObject* p2, bool isPlatformer, bool isDual = true);
    void truncateAfter(int tick);
    std::vector<uint8_t> serialize() const;
    std::vector<uint8_t> serializeTTR3() const;
    static TTRMacro* deserialize(std::vector<uint8_t> const& data);
    bool persist();
    bool persistToDirectory(std::filesystem::path const& directory);
    bool saveToPath(std::filesystem::path const& path);
    static TTRMacro* loadFromDisk(std::string const& filename);
    static TTRMacro* loadFromPath(std::filesystem::path const& path);
    std::vector<MacroAction> toMacroActions() const;
    std::vector<MacroAction> toPersistenceMacroActions() const;
};

inline void normalizeTTRPersistenceTiming(TTRMacro& macro) {
    macro.accuracyMode = writableAccuracyMode(macro.accuracyMode);

    auto stripTimedOffsets = [](std::vector<TTRInput>& inputList) {
        for (auto& input : inputList) {
            input.stepOffset = 0.0f;
            input.cbsTimeOffset = -1.0;
            input.timeSeconds = -1.0;
            input.swiftPairAnchor = false;
        }
    };

    if (!usesTimedAccuracy(macro.accuracyMode)) {
        stripTimedOffsets(macro.inputs);
        for (auto& attempt : macro.persistenceAttempts) {
            stripTimedOffsets(attempt.inputs);
        }
        macro.exactCbsTiming = false;
        return;
    }

    macro.exactCbsTiming = false;
    for (auto const& input : macro.inputs) {
        if (std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0) {
            macro.exactCbsTiming = true;
            return;
        }
    }
    for (auto const& attempt : macro.persistenceAttempts) {
        for (auto const& input : attempt.inputs) {
            if (std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0) {
                macro.exactCbsTiming = true;
                return;
            }
        }
    }
}

#endif
