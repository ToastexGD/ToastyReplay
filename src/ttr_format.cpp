#include "ttr_format.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <type_traits>

#include <zlib.h>

template <typename T>
static void writeLE(std::vector<uint8_t>& buffer, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    size_t position = buffer.size();
    buffer.resize(position + sizeof(T));
    std::memcpy(buffer.data() + position, &value, sizeof(T));
}

template <typename T>
static T readLE(std::vector<uint8_t> const& data, size_t& position) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (position + sizeof(T) > data.size()) {
        throw std::runtime_error("TTR: unexpected end of data");
    }

    T value;
    std::memcpy(&value, data.data() + position, sizeof(T));
    position += sizeof(T);
    return value;
}

static void writeString(std::vector<uint8_t>& buffer, std::string const& value) {
    uint16_t length = static_cast<uint16_t>(std::min(value.size(), static_cast<size_t>(65535)));
    writeLE<uint16_t>(buffer, length);
    buffer.insert(buffer.end(), value.begin(), value.begin() + length);
}

static std::string readString(std::vector<uint8_t> const& data, size_t& position) {
    uint16_t length = readLE<uint16_t>(data, position);
    if (position + length > data.size()) {
        throw std::runtime_error("TTR: unexpected end of string data");
    }

    std::string value(data.begin() + position, data.begin() + position + length);
    position += length;
    return value;
}

static void writeVarint(std::vector<uint8_t>& buffer, uint32_t value) {
    while (value >= 0x80) {
        buffer.push_back(static_cast<uint8_t>(value & 0x7F) | 0x80);
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
}

static uint32_t readVarint(std::vector<uint8_t> const& data, size_t& position) {
    uint32_t result = 0;
    int shift = 0;
    while (position < data.size()) {
        uint8_t byte = data[position++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return result;
        }

        shift += 7;
        if (shift >= 35) {
            throw std::runtime_error("TTR: varint too large");
        }
    }

    throw std::runtime_error("TTR: unexpected end of varint");
}

static std::vector<uint8_t> zlibCompress(std::vector<uint8_t> const& input) {
    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    std::vector<uint8_t> output(bound);
    int result = compress2(output.data(), &bound, input.data(), static_cast<uLong>(input.size()), Z_DEFAULT_COMPRESSION);
    if (result != Z_OK) {
        throw std::runtime_error("TTR: zlib compress failed");
    }
    output.resize(bound);
    return output;
}

static std::vector<uint8_t> zlibDecompress(
    std::vector<uint8_t> const& input,
    size_t offset,
    size_t length,
    uint32_t uncompressedSize
) {
    std::vector<uint8_t> output(uncompressedSize);
    uLongf destinationLength = uncompressedSize;
    int result = uncompress(output.data(), &destinationLength, input.data() + offset, static_cast<uLong>(length));
    if (result != Z_OK) {
        throw std::runtime_error("TTR: zlib decompress failed");
    }
    output.resize(destinationLength);
    return output;
}

static uint8_t packHoldMask(std::array<bool, 4> const& holds) {
    uint8_t mask = 0;
    for (size_t index = 0; index < holds.size(); ++index) {
        if (holds[index]) {
            mask |= static_cast<uint8_t>(1u << index);
        }
    }
    return mask;
}

static std::array<bool, 4> unpackHoldMask(uint8_t mask) {
    std::array<bool, 4> holds = { false, false, false, false };
    for (size_t index = 0; index < holds.size(); ++index) {
        holds[index] = (mask & static_cast<uint8_t>(1u << index)) != 0;
    }
    return holds;
}

static PlayerStateBundle capturePlayerState(PlayerObject* player, bool isPlatformer, bool isDual, bool isTwoPlayer) {
    PlayerStateBundle state;
    auto position = player->getPosition();
    state.motion.position = position;
    state.motion.rotation = player->getRotation();
    state.motion.verticalVelocity = player->m_yVelocity;
    state.motion.preSlopeVerticalVelocity = player->m_yVelocityBeforeSlope;
    state.motion.horizontalVelocity = isPlatformer ? player->m_platformerXVelocity : 0.0;
    state.flags.upsideDown = player->m_isUpsideDown;
    state.flags.holdingLeft = player->m_holdingLeft;
    state.flags.holdingRight = player->m_holdingRight;
    state.flags.platformer = isPlatformer;
    state.flags.dead = player->m_isDead;
    state.flags.buttonHolds[0] = player->m_holdingButtons[1];
    state.flags.buttonHolds[1] = player->m_holdingButtons[2];
    state.flags.buttonHolds[2] = player->m_holdingButtons[3];
    state.environment.gravity = player->m_gravity;
    state.environment.dualContext = isDual;
    state.environment.twoPlayerContext = isTwoPlayer;
    return state;
}

static void writeAnchorPlayerV3(std::vector<uint8_t>& payload, PlayerStateBundle const& state) {
    writeLE<float>(payload, state.motion.position.x);
    writeLE<float>(payload, state.motion.position.y);
    writeLE<double>(payload, state.motion.verticalVelocity);
    writeLE<double>(payload, state.motion.preSlopeVerticalVelocity);
    writeLE<double>(payload, state.motion.horizontalVelocity);
    writeLE<float>(payload, state.motion.rotation);
    writeLE<float>(payload, static_cast<float>(state.environment.gravity));

    uint8_t stateFlags = 0;
    if (state.flags.upsideDown) stateFlags |= 1 << 0;
    if (state.flags.holdingLeft) stateFlags |= 1 << 1;
    if (state.flags.holdingRight) stateFlags |= 1 << 2;
    if (state.flags.platformer) stateFlags |= 1 << 3;
    if (state.flags.dead) stateFlags |= 1 << 4;
    if (state.environment.dualContext) stateFlags |= 1 << 5;
    if (state.environment.twoPlayerContext) stateFlags |= 1 << 6;
    writeLE<uint8_t>(payload, stateFlags);
    writeLE<uint8_t>(payload, packHoldMask(state.flags.buttonHolds));
}

static PlayerStateBundle readAnchorPlayerV3(std::vector<uint8_t> const& payload, size_t& position) {
    PlayerStateBundle state;
    state.motion.position.x = readLE<float>(payload, position);
    state.motion.position.y = readLE<float>(payload, position);
    state.motion.verticalVelocity = readLE<double>(payload, position);
    state.motion.preSlopeVerticalVelocity = readLE<double>(payload, position);
    state.motion.horizontalVelocity = readLE<double>(payload, position);
    state.motion.rotation = readLE<float>(payload, position);
    state.environment.gravity = static_cast<double>(readLE<float>(payload, position));

    uint8_t stateFlags = readLE<uint8_t>(payload, position);
    state.flags.upsideDown = (stateFlags & (1 << 0)) != 0;
    state.flags.holdingLeft = (stateFlags & (1 << 1)) != 0;
    state.flags.holdingRight = (stateFlags & (1 << 2)) != 0;
    state.flags.platformer = (stateFlags & (1 << 3)) != 0;
    state.flags.dead = (stateFlags & (1 << 4)) != 0;
    state.environment.dualContext = (stateFlags & (1 << 5)) != 0;
    state.environment.twoPlayerContext = (stateFlags & (1 << 6)) != 0;
    state.flags.buttonHolds = unpackHoldMask(readLE<uint8_t>(payload, position));
    return state;
}

struct LegacyTTRPlayerSnapshot {
    double x = 0.0;
    double y = 0.0;
    double yVelocity = 0.0;
    double yVelocityBeforeSlope = 0.0;
    double xVelocity = 0.0;
    float rotation = 0.0f;
    uint8_t flags = 0;
};

static LegacyTTRPlayerSnapshot readPlayerSnapshotV1(std::vector<uint8_t> const& data, size_t& position) {
    LegacyTTRPlayerSnapshot snapshot;
    snapshot.x = readLE<double>(data, position);
    snapshot.y = readLE<double>(data, position);
    snapshot.yVelocity = readLE<double>(data, position);
    snapshot.yVelocityBeforeSlope = readLE<double>(data, position);
    snapshot.xVelocity = readLE<double>(data, position);
    snapshot.rotation = readLE<float>(data, position);
    snapshot.flags = readLE<uint8_t>(data, position);
    return snapshot;
}

static LegacyTTRPlayerSnapshot readPlayerSnapshotV2(std::vector<uint8_t> const& data, size_t& position) {
    LegacyTTRPlayerSnapshot snapshot;
    snapshot.x = static_cast<double>(readLE<float>(data, position));
    snapshot.y = static_cast<double>(readLE<float>(data, position));
    snapshot.yVelocity = readLE<double>(data, position);
    snapshot.yVelocityBeforeSlope = readLE<double>(data, position);
    snapshot.xVelocity = readLE<double>(data, position);
    snapshot.rotation = readLE<float>(data, position);
    snapshot.flags = readLE<uint8_t>(data, position);
    return snapshot;
}

static PlayerStateBundle convertLegacySnapshot(LegacyTTRPlayerSnapshot const& snapshot, bool isPlatformer, bool isDual, bool isTwoPlayer) {
    PlayerStateBundle state;
    state.motion.position = cocos2d::CCPoint{
        static_cast<float>(snapshot.x),
        static_cast<float>(snapshot.y)
    };
    state.motion.verticalVelocity = snapshot.yVelocity;
    state.motion.preSlopeVerticalVelocity = snapshot.yVelocityBeforeSlope;
    state.motion.horizontalVelocity = isPlatformer ? snapshot.xVelocity : 0.0;
    state.motion.rotation = snapshot.rotation;
    state.flags.upsideDown = (snapshot.flags & 0x01) != 0;
    state.flags.platformer = isPlatformer;
    state.environment.dualContext = isDual;
    state.environment.twoPlayerContext = isTwoPlayer;
    return state;
}

static void writeHeader(std::vector<uint8_t>& buffer, TTRMacro const& macro) {
    buffer.push_back('T');
    buffer.push_back('T');
    buffer.push_back('R');
    buffer.push_back('\0');

    writeLE<uint16_t>(buffer, TTR_FORMAT_VERSION);

    uint32_t flags = 0;
    if (macro.accuracyMode == AccuracyMode::CBS) flags |= TTR_FLAG_ACCURACY_CBS;
    if (macro.accuracyMode == AccuracyMode::CBF) flags |= TTR_FLAG_ACCURACY_CBF;
    if (macro.recordedFromStartPos) flags |= TTR_FLAG_FROM_START_POS;
    if (macro.platformerMode) flags |= TTR_FLAG_PLATFORMER;
    if (macro.twoPlayerMode) flags |= TTR_FLAG_TWO_PLAYER;
    if (macro.rngLocked) flags |= TTR_FLAG_RNG_LOCKED;
    writeLE<uint32_t>(buffer, flags);

    size_t headerSizePosition = buffer.size();
    writeLE<uint32_t>(buffer, 0);

    writeString(buffer, macro.author);
    writeString(buffer, macro.name);
    writeString(buffer, macro.levelName);
    writeLE<int32_t>(buffer, macro.levelId);
    writeLE<double>(buffer, macro.framerate);
    writeLE<double>(buffer, macro.duration);
    writeLE<uint32_t>(buffer, macro.gameVersion);
    writeLE<float>(buffer, macro.startPosX);
    writeLE<float>(buffer, macro.startPosY);
    writeLE<uint32_t>(buffer, macro.rngSeed);
    writeLE<int64_t>(buffer, macro.recordTimestamp);

    uint32_t headerSize = static_cast<uint32_t>(buffer.size());
    std::memcpy(buffer.data() + headerSizePosition, &headerSize, sizeof(uint32_t));
}

void TTRMacro::recordAction(int tick, int button, bool player2, bool pressed, float offset) {
    TTRInput input;
    input.tick = tick;
    input.actionType = static_cast<uint8_t>(button);
    input.setPlayer2(player2);
    input.setPressed(pressed);
    input.stepOffset = offset;
    inputs.push_back(input);
}

void TTRMacro::recordAnchor(int tick, PlayerObject* p1, PlayerObject* p2, bool isPlatformer, bool isDual) {
    PlaybackAnchor anchor;
    anchor.tick = tick;
    anchor.hasPlayer2 = isDual;
    anchor.player1 = capturePlayerState(p1, isPlatformer, isDual, twoPlayerMode);
    anchor.player1LatchMask = packHoldMask(anchor.player1.flags.buttonHolds);

    if (isDual) {
        anchor.player2 = capturePlayerState(p2, isPlatformer, isDual, twoPlayerMode);
        anchor.player2LatchMask = packHoldMask(anchor.player2.flags.buttonHolds);
    }

    anchor.rng.locked = rngLocked;
    anchor.rng.seed = rngSeed;
    anchor.rng.fastRandState = GameToolbox::getfast_srand();

    if (!anchors.empty() && anchors.back().tick == tick) {
        anchors.back() = std::move(anchor);
    } else {
        anchors.push_back(std::move(anchor));
    }
}

void TTRMacro::truncateAfter(int tick) {
    while (!inputs.empty() && inputs.back().tick >= tick) {
        inputs.pop_back();
    }
    while (!anchors.empty() && anchors.back().tick >= tick) {
        anchors.pop_back();
    }
}

std::vector<uint8_t> TTRMacro::serialize() const {
    std::vector<uint8_t> output;
    output.reserve(256);
    writeHeader(output, *this);

    std::vector<uint8_t> payload;
    payload.reserve(inputs.size() * 6 + anchors.size() * 48 + checkpoints.size() * 16 + 16);

    writeLE<uint32_t>(payload, static_cast<uint32_t>(inputs.size()));
    int32_t previousInputTick = 0;
    for (auto const& input : inputs) {
        writeVarint(payload, static_cast<uint32_t>(input.tick - previousInputTick));
        previousInputTick = input.tick;
        writeLE<uint8_t>(payload, input.actionType);
        writeLE<uint8_t>(payload, input.flags);
        if (usesTimedAccuracy(accuracyMode)) {
            writeLE<float>(payload, input.stepOffset);
        }
    }

    writeLE<uint32_t>(payload, static_cast<uint32_t>(anchors.size()));
    int32_t previousAnchorTick = 0;
    for (auto const& anchor : anchors) {
        writeVarint(payload, static_cast<uint32_t>(anchor.tick - previousAnchorTick));
        previousAnchorTick = anchor.tick;

        uint8_t anchorFlags = 0;
        if (anchor.hasPlayer2) anchorFlags |= 1 << 0;
        if (anchor.rng.fastRandState != 0) anchorFlags |= 1 << 1;
        writeLE<uint8_t>(payload, anchorFlags);
        writeAnchorPlayerV3(payload, anchor.player1);
        writeLE<uint8_t>(payload, anchor.player1LatchMask);
        if (anchor.hasPlayer2) {
            writeAnchorPlayerV3(payload, anchor.player2);
            writeLE<uint8_t>(payload, anchor.player2LatchMask);
        }
        if ((anchorFlags & (1 << 1)) != 0) {
            writeLE<uint64_t>(payload, static_cast<uint64_t>(anchor.rng.fastRandState));
        }
    }

    writeLE<uint32_t>(payload, static_cast<uint32_t>(checkpoints.size()));
    for (auto const& checkpoint : checkpoints) {
        writeLE<int32_t>(payload, checkpoint.tick);
        writeLE<uint64_t>(payload, checkpoint.rngState);
        writeLE<int32_t>(payload, checkpoint.priorTick);
    }

    auto compressedPayload = zlibCompress(payload);
    writeLE<uint32_t>(output, static_cast<uint32_t>(payload.size()));
    output.insert(output.end(), compressedPayload.begin(), compressedPayload.end());
    return output;
}

static bool readSharedMetadata(
    std::vector<uint8_t> const& data,
    size_t& position,
    uint32_t headerSize,
    TTRMacro& macro
) {
    try {
        macro.author = readString(data, position);
        macro.name = readString(data, position);
        macro.levelName = readString(data, position);
        macro.levelId = readLE<int32_t>(data, position);
        macro.framerate = readLE<double>(data, position);
        macro.duration = readLE<double>(data, position);
        macro.gameVersion = readLE<uint32_t>(data, position);
        macro.startPosX = readLE<float>(data, position);
        macro.startPosY = readLE<float>(data, position);
        macro.rngSeed = readLE<uint32_t>(data, position);
        macro.recordTimestamp = readLE<int64_t>(data, position);
    } catch (...) {
        return false;
    }

    if (headerSize > position && headerSize <= data.size()) {
        position = headerSize;
    }
    return true;
}

static TTRMacro* deserializeLegacyV1(
    std::vector<uint8_t> const& data,
    size_t position,
    uint32_t flags,
    uint32_t headerSize,
    TTRMacro* macro
) {
    if (!readSharedMetadata(data, position, headerSize, *macro)) {
        delete macro;
        return nullptr;
    }

    try {
        uint32_t inputCount = readLE<uint32_t>(data, position);
        macro->inputs.reserve(inputCount);
        for (uint32_t index = 0; index < inputCount; ++index) {
            TTRInput input;
            input.tick = readLE<int32_t>(data, position);
            input.actionType = readLE<uint8_t>(data, position);
            input.flags = readLE<uint8_t>(data, position);
            input.stepOffset = readLE<float>(data, position);
            macro->inputs.push_back(input);
        }

        uint32_t anchorCount = readLE<uint32_t>(data, position);
        macro->anchors.reserve(anchorCount);
        for (uint32_t index = 0; index < anchorCount; ++index) {
            PlaybackAnchor anchor;
            anchor.tick = readLE<int32_t>(data, position);
            anchor.hasPlayer2 = true;
            anchor.player1 = convertLegacySnapshot(
                readPlayerSnapshotV1(data, position),
                macro->platformerMode,
                true,
                macro->twoPlayerMode
            );
            anchor.player2 = convertLegacySnapshot(
                readPlayerSnapshotV1(data, position),
                macro->platformerMode,
                true,
                macro->twoPlayerMode
            );
            macro->anchors.push_back(anchor);
        }

        if (position < data.size()) {
            uint32_t checkpointCount = readLE<uint32_t>(data, position);
            macro->checkpoints.reserve(checkpointCount);
            for (uint32_t index = 0; index < checkpointCount; ++index) {
                TTRCheckpoint checkpoint;
                checkpoint.tick = readLE<int32_t>(data, position);
                checkpoint.rngState = readLE<uint64_t>(data, position);
                checkpoint.priorTick = readLE<int32_t>(data, position);
                macro->checkpoints.push_back(checkpoint);
            }
        }
    } catch (...) {
        delete macro;
        return nullptr;
    }

    return macro;
}

static TTRMacro* deserializeCompressedPayload(
    std::vector<uint8_t> const& data,
    size_t position,
    uint16_t version,
    uint32_t flags,
    uint32_t headerSize,
    TTRMacro* macro
) {
    if (!readSharedMetadata(data, position, headerSize, *macro)) {
        delete macro;
        return nullptr;
    }

    try {
        uint32_t uncompressedSize = readLE<uint32_t>(data, position);
        auto payload = zlibDecompress(data, position, data.size() - position, uncompressedSize);

        size_t payloadPosition = 0;
        bool hasTimedOffsets = (flags & (TTR_FLAG_ACCURACY_CBS | TTR_FLAG_ACCURACY_CBF)) != 0;

        uint32_t inputCount = readLE<uint32_t>(payload, payloadPosition);
        macro->inputs.reserve(inputCount);
        int32_t previousInputTick = 0;
        for (uint32_t index = 0; index < inputCount; ++index) {
            TTRInput input;
            previousInputTick += static_cast<int32_t>(readVarint(payload, payloadPosition));
            input.tick = previousInputTick;
            input.actionType = readLE<uint8_t>(payload, payloadPosition);
            input.flags = readLE<uint8_t>(payload, payloadPosition);
            input.stepOffset = hasTimedOffsets ? readLE<float>(payload, payloadPosition) : 0.0f;
            macro->inputs.push_back(input);
        }

        uint32_t anchorCount = readLE<uint32_t>(payload, payloadPosition);
        macro->anchors.reserve(anchorCount);
        int32_t previousAnchorTick = 0;
        for (uint32_t index = 0; index < anchorCount; ++index) {
            PlaybackAnchor anchor;
            previousAnchorTick += static_cast<int32_t>(readVarint(payload, payloadPosition));
            anchor.tick = previousAnchorTick;

            if (version >= 3) {
                uint8_t anchorFlags = readLE<uint8_t>(payload, payloadPosition);
                anchor.hasPlayer2 = (anchorFlags & (1 << 0)) != 0;
                anchor.player1 = readAnchorPlayerV3(payload, payloadPosition);
                anchor.player1LatchMask = readLE<uint8_t>(payload, payloadPosition);
                if (anchor.hasPlayer2) {
                    anchor.player2 = readAnchorPlayerV3(payload, payloadPosition);
                    anchor.player2LatchMask = readLE<uint8_t>(payload, payloadPosition);
                }
                if ((anchorFlags & (1 << 1)) != 0) {
                    anchor.rng.fastRandState = static_cast<uintptr_t>(readLE<uint64_t>(payload, payloadPosition));
                }
                anchor.rng.locked = macro->rngLocked;
                anchor.rng.seed = macro->rngSeed;
            } else {
                uint8_t anchorFlags = readLE<uint8_t>(payload, payloadPosition);
                anchor.hasPlayer2 = (anchorFlags & 0x01) != 0;
                anchor.player1 = convertLegacySnapshot(
                    readPlayerSnapshotV2(payload, payloadPosition),
                    macro->platformerMode,
                    anchor.hasPlayer2,
                    macro->twoPlayerMode
                );
                if (anchor.hasPlayer2) {
                    anchor.player2 = convertLegacySnapshot(
                        readPlayerSnapshotV2(payload, payloadPosition),
                        macro->platformerMode,
                        anchor.hasPlayer2,
                        macro->twoPlayerMode
                    );
                }
            }

            macro->anchors.push_back(anchor);
        }

        if (payloadPosition < payload.size()) {
            uint32_t checkpointCount = readLE<uint32_t>(payload, payloadPosition);
            macro->checkpoints.reserve(checkpointCount);
            for (uint32_t index = 0; index < checkpointCount; ++index) {
                TTRCheckpoint checkpoint;
                checkpoint.tick = readLE<int32_t>(payload, payloadPosition);
                checkpoint.rngState = readLE<uint64_t>(payload, payloadPosition);
                checkpoint.priorTick = readLE<int32_t>(payload, payloadPosition);
                macro->checkpoints.push_back(checkpoint);
            }
        }
    } catch (...) {
        delete macro;
        return nullptr;
    }

    return macro;
}

TTRMacro* TTRMacro::deserialize(std::vector<uint8_t> const& data) {
    if (data.size() < 14) {
        return nullptr;
    }

    if (data[0] != 'T' || data[1] != 'T' || data[2] != 'R' || data[3] != '\0') {
        return nullptr;
    }

    size_t position = 4;
    uint16_t version = readLE<uint16_t>(data, position);
    if (version > TTR_FORMAT_VERSION) {
        log::warn("[TTR] Format version {} is newer than supported ({})", version, TTR_FORMAT_VERSION);
        return nullptr;
    }

    uint32_t flags = readLE<uint32_t>(data, position);
    uint32_t headerSize = readLE<uint32_t>(data, position);

    auto* macro = new TTRMacro();
    if ((flags & TTR_FLAG_ACCURACY_CBF) != 0) {
        macro->accuracyMode = AccuracyMode::CBF;
    } else if ((flags & TTR_FLAG_ACCURACY_CBS) != 0) {
        macro->accuracyMode = AccuracyMode::CBS;
    } else {
        macro->accuracyMode = AccuracyMode::Vanilla;
    }
    macro->recordedFromStartPos = (flags & TTR_FLAG_FROM_START_POS) != 0;
    macro->platformerMode = (flags & TTR_FLAG_PLATFORMER) != 0;
    macro->twoPlayerMode = (flags & TTR_FLAG_TWO_PLAYER) != 0;
    macro->rngLocked = (flags & TTR_FLAG_RNG_LOCKED) != 0;

    if (version == 1) {
        return deserializeLegacyV1(data, position, flags, headerSize, macro);
    }

    return deserializeCompressedPayload(data, position, version, flags, headerSize, macro);
}

void TTRMacro::persist() {
    author = GJAccountManager::get()->m_username;
    duration = inputs.empty() ? 0.0 : static_cast<double>(inputs.back().tick) / framerate;
    recordTimestamp = static_cast<int64_t>(std::time(nullptr));

    auto directory = ReplayStorage::getReplayDirectoryPath();
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directory(directory);
    }

    name = ReplayStorage::makeUniqueReplayName(name, persistedName);
    persistedName = name;

    auto bytes = serialize();
    std::ofstream output(directory / (name + ".ttr"), std::ios::binary);
    output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    output.close();

    log::info(
        "[TTR] Saved macro to {} ({} bytes, {} inputs, {} anchors)",
        (directory / (name + ".ttr")).string(),
        bytes.size(),
        inputs.size(),
        anchors.size()
    );
}

TTRMacro* TTRMacro::loadFromDisk(std::string const& filename) {
    auto directory = Mod::get()->getSaveDir() / "replays";
    if (!std::filesystem::exists(directory) && !std::filesystem::create_directory(directory)) {
        return nullptr;
    }

    auto path = directory / (filename + ".ttr");
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        input = std::ifstream(directory / filename, std::ios::binary);
        if (!input.is_open()) {
            return nullptr;
        }
    }

    input.seekg(0, std::ios::end);
    auto fileSize = input.tellg();
    input.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    input.read(reinterpret_cast<char*>(bytes.data()), fileSize);
    input.close();

    auto* result = deserialize(bytes);
    if (result) {
        result->name = filename;
        result->persistedName = filename;
        log::info("[TTR] Loaded macro: {} ({} inputs, {} anchors)", filename, result->inputs.size(), result->anchors.size());
    }
    return result;
}

std::vector<MacroAction> TTRMacro::toMacroActions() const {
    std::vector<MacroAction> actions;
    actions.reserve(inputs.size());
    for (auto const& input : inputs) {
        actions.emplace_back(
            input.tick,
            static_cast<int>(input.actionType),
            input.isPlayer2(),
            input.isPressed(),
            input.stepOffset
        );
    }
    return actions;
}
