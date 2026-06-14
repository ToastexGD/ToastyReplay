#pragma once

#include "core/accuracy_mode.hpp"
#include "replay.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class TTRMacro;

namespace toasty::ttr3 {

inline constexpr uint16_t kWireVersion = 1;
inline constexpr uint64_t kNativeSourceFormatId = 0x00000000FFFF0003ull;

enum Flags : uint32_t {
    FlagLosslessVerified = 1u << 0,
    FlagFromStartPos = 1u << 1,
    FlagPlatformer = 1u << 2,
    FlagTwoPlayer = 1u << 3,
    FlagRngLocked = 1u << 4,
    FlagHasAnchors = 1u << 5,
    FlagHasCheckpoints = 1u << 6,
    FlagHasPersistence = 1u << 7,
    FlagHasTpsEvents = 1u << 8,
    FlagMacroConverted = 1u << 9,
    FlagCompressed = 1u << 10,
};

enum class SectionKind : uint8_t {
    Inputs = 1,
    TpsEvents = 2,
    Anchors = 3,
    Checkpoints = 4,
    Persistence = 5,
};

struct Input {
    double timeSeconds = 0.0;
    uint8_t button = 1;
    bool player2 = false;
    bool pressed = false;
    bool swiftPairAnchor = false;
};

struct TpsEvent {
    double timeSeconds = 0.0;
    double tps = 240.0;
};

struct Checkpoint {
    double timeSeconds = 0.0;
    uint64_t rngState = 0;
    double priorTimeSeconds = 0.0;
};

struct Anchor {
    double timeSeconds = 0.0;
    PlaybackAnchor state;
};

struct Attempt {
    double deathTimeSeconds = 0.0;
    bool deathPlayer2 = false;
    std::vector<Input> inputs;
    std::vector<Anchor> anchors;
};

struct Macro {
    uint64_t sourceFormatId = kNativeSourceFormatId;
    uint64_t gameVersion = 0;
    int32_t levelId = 0;
    std::string name;
    std::string levelName;
    std::string author;
    double framerateHint = 240.0;
    double duration = 0.0;
    float startPosX = 0.0f;
    float startPosY = 0.0f;
    int64_t recordTimestamp = 0;
    uint32_t rngSeed = 0;
    bool losslessVerified = false;
    bool recordedFromStartPos = false;
    bool platformerMode = false;
    bool twoPlayerMode = false;
    bool rngLocked = false;
    bool macroConverted = false;
    AccuracyMode accuracyMode = AccuracyMode::Vanilla;
    std::vector<Input> inputs;
    std::vector<TpsEvent> tpsEvents;
    std::vector<Anchor> anchors;
    std::vector<Checkpoint> checkpoints;
    std::vector<Attempt> persistenceAttempts;
};

struct WriteOptions {
    bool compressPayload = true;
};

std::vector<uint8_t> serialize(Macro const& macro, WriteOptions options = {});
std::optional<Macro> deserialize(std::vector<uint8_t> const& data, std::string* error = nullptr);
double maxSourceTps(Macro const& macro);
Macro fromTTRMacro(TTRMacro const& macro);
TTRMacro toTTRMacro(Macro const& macro);

}
