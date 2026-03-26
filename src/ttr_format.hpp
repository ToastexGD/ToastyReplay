#ifndef _ttr_format_hpp
#define _ttr_format_hpp

#include "replay.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace geode::prelude;

#define TTR_FORMAT_VERSION 4
#define TTR_MAGIC "TTR"

enum TTRFlags : uint32_t {
    TTR_FLAG_ACCURACY_CBS = 1 << 0,
    TTR_FLAG_FROM_START_POS = 1 << 1,
    TTR_FLAG_PLATFORMER = 1 << 2,
    TTR_FLAG_TWO_PLAYER = 1 << 3,
    TTR_FLAG_RNG_LOCKED = 1 << 4,
    TTR_FLAG_ACCURACY_CBF = 1 << 5,
};

struct TTRInput {
    int32_t tick = 0;
    uint8_t actionType = 0;
    uint8_t flags = 0;
    float stepOffset = 0.0f;

    bool isPlayer2() const { return (flags & 0x01) != 0; }
    bool isPressed() const { return (flags & 0x02) != 0; }

    void setPlayer2(bool value) { flags = (flags & ~0x01) | (value ? 0x01 : 0x00); }
    void setPressed(bool value) { flags = (flags & ~0x02) | (value ? 0x02 : 0x00); }
};

struct TTRCheckpoint {
    int32_t tick = 0;
    uint64_t rngState = 0;
    int32_t priorTick = 0;
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
    uint32_t rngSeed = 0;
    int64_t recordTimestamp = 0;
    std::vector<TTRInput> inputs;
    std::vector<PlaybackAnchor> anchors;
    std::vector<TTRCheckpoint> checkpoints;

    void recordAction(int tick, int button, bool player2, bool pressed, float offset);
    void recordAnchor(int tick, PlayerObject* p1, PlayerObject* p2, bool isPlatformer, bool isDual = true);
    void truncateAfter(int tick);
    std::vector<uint8_t> serialize() const;
    static TTRMacro* deserialize(std::vector<uint8_t> const& data);
    void persist();
    static TTRMacro* loadFromDisk(std::string const& filename);
    std::vector<MacroAction> toMacroActions() const;
};

#endif
