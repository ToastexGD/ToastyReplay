#include "ttr_format.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <type_traits>

#include <zlib.h>

static constexpr size_t kMaxTTRStringSize = 16 * 1024;
static constexpr uint32_t kMaxTTRPayloadSize = 64 * 1024 * 1024;

struct TTRReadContext {
    std::vector<uint8_t> const& data;
    size_t position = 0;
    bool failed = false;
};

template <typename T>
static void writeLE(std::vector<uint8_t>& buffer, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    size_t position = buffer.size();
    buffer.resize(position + sizeof(T));
    std::memcpy(buffer.data() + position, &value, sizeof(T));
}

template <typename T>
static T readLE(TTRReadContext& ctx) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value {};
    if (ctx.failed || ctx.position + sizeof(T) > ctx.data.size()) {
        ctx.failed = true;
        return value;
    }

    std::memcpy(&value, ctx.data.data() + ctx.position, sizeof(T));
    ctx.position += sizeof(T);
    return value;
}

static uint32_t readCount(
    TTRReadContext& ctx,
    size_t minBytesPerEntry,
    char const* label
) {
    (void)label;

    auto count = readLE<uint32_t>(ctx);
    if (ctx.failed) {
        return 0;
    }

    auto remaining = ctx.data.size() - ctx.position;
    if (minBytesPerEntry == 0) {
        minBytesPerEntry = 1;
    }
    if (count > remaining / minBytesPerEntry + 1) {
        ctx.failed = true;
        return 0;
    }
    return count;
}

static void writeString(std::vector<uint8_t>& buffer, std::string const& value) {
    uint16_t length = static_cast<uint16_t>(std::min(value.size(), static_cast<size_t>(65535)));
    writeLE<uint16_t>(buffer, length);
    buffer.insert(buffer.end(), value.begin(), value.begin() + length);
}

static std::string readString(TTRReadContext& ctx) {
    uint16_t length = readLE<uint16_t>(ctx);
    if (ctx.failed) {
        return {};
    }

    if (length > kMaxTTRStringSize) {
        ctx.failed = true;
        return {};
    }
    if (ctx.position + length > ctx.data.size()) {
        ctx.failed = true;
        return {};
    }

    std::string value(ctx.data.begin() + ctx.position, ctx.data.begin() + ctx.position + length);
    ctx.position += length;
    return value;
}

static void writeVarint(std::vector<uint8_t>& buffer, uint32_t value) {
    while (value >= 0x80) {
        buffer.push_back(static_cast<uint8_t>(value & 0x7F) | 0x80);
        value >>= 7;
    }
    buffer.push_back(static_cast<uint8_t>(value));
}

static uint32_t readVarint(TTRReadContext& ctx) {
    uint32_t result = 0;
    int shift = 0;
    while (ctx.position < ctx.data.size()) {
        uint8_t byte = ctx.data[ctx.position++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return result;
        }

        shift += 7;
        if (shift >= 35) {
            ctx.failed = true;
            return 0;
        }
    }

    ctx.failed = true;
    return 0;
}

static bool zlibCompress(std::vector<uint8_t> const& input, std::vector<uint8_t>& output) {
    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    output.resize(bound);
    int result = compress2(output.data(), &bound, input.data(), static_cast<uLong>(input.size()), Z_DEFAULT_COMPRESSION);
    if (result != Z_OK) {
        output.clear();
        return false;
    }
    output.resize(bound);
    return true;
}

static bool zlibDecompress(
    std::vector<uint8_t> const& input,
    size_t offset,
    size_t length,
    uint32_t uncompressedSize,
    std::vector<uint8_t>& output
) {
    if (offset > input.size() || length > input.size() - offset) {
        return false;
    }
    if (uncompressedSize == 0 || uncompressedSize > kMaxTTRPayloadSize) {
        return false;
    }

    output.resize(uncompressedSize);
    uLongf destinationLength = uncompressedSize;
    int result = uncompress(output.data(), &destinationLength, input.data() + offset, static_cast<uLong>(length));
    if (result != Z_OK) {
        output.clear();
        return false;
    }
    output.resize(destinationLength);
    return true;
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

static PlayerStateBundle readAnchorPlayerV3(TTRReadContext& ctx) {
    PlayerStateBundle state;
    state.motion.position.x = readLE<float>(ctx);
    state.motion.position.y = readLE<float>(ctx);
    state.motion.verticalVelocity = readLE<double>(ctx);
    state.motion.preSlopeVerticalVelocity = readLE<double>(ctx);
    state.motion.horizontalVelocity = readLE<double>(ctx);
    state.motion.rotation = readLE<float>(ctx);
    state.environment.gravity = static_cast<double>(readLE<float>(ctx));

    uint8_t stateFlags = readLE<uint8_t>(ctx);
    state.flags.upsideDown = (stateFlags & (1 << 0)) != 0;
    state.flags.holdingLeft = (stateFlags & (1 << 1)) != 0;
    state.flags.holdingRight = (stateFlags & (1 << 2)) != 0;
    state.flags.platformer = (stateFlags & (1 << 3)) != 0;
    state.flags.dead = (stateFlags & (1 << 4)) != 0;
    state.environment.dualContext = (stateFlags & (1 << 5)) != 0;
    state.environment.twoPlayerContext = (stateFlags & (1 << 6)) != 0;
    state.flags.buttonHolds = unpackHoldMask(readLE<uint8_t>(ctx));
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

static LegacyTTRPlayerSnapshot readPlayerSnapshotV1(TTRReadContext& ctx) {
    LegacyTTRPlayerSnapshot snapshot;
    snapshot.x = readLE<double>(ctx);
    snapshot.y = readLE<double>(ctx);
    snapshot.yVelocity = readLE<double>(ctx);
    snapshot.yVelocityBeforeSlope = readLE<double>(ctx);
    snapshot.xVelocity = readLE<double>(ctx);
    snapshot.rotation = readLE<float>(ctx);
    snapshot.flags = readLE<uint8_t>(ctx);
    return snapshot;
}

static LegacyTTRPlayerSnapshot readPlayerSnapshotV2(TTRReadContext& ctx) {
    LegacyTTRPlayerSnapshot snapshot;
    snapshot.x = static_cast<double>(readLE<float>(ctx));
    snapshot.y = static_cast<double>(readLE<float>(ctx));
    snapshot.yVelocity = readLE<double>(ctx);
    snapshot.yVelocityBeforeSlope = readLE<double>(ctx);
    snapshot.xVelocity = readLE<double>(ctx);
    snapshot.rotation = readLE<float>(ctx);
    snapshot.flags = readLE<uint8_t>(ctx);
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

    std::vector<uint8_t> compressedPayload;
    if (!zlibCompress(payload, compressedPayload)) {
        log::warn("[TTR] Failed to compress macro payload");
        return {};
    }
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
    if (headerSize < position || headerSize > data.size()) {
        return false;
    }

    TTRReadContext ctx { data, position };
    macro.author = readString(ctx);
    macro.name = readString(ctx);
    macro.levelName = readString(ctx);
    macro.levelId = readLE<int32_t>(ctx);
    macro.framerate = readLE<double>(ctx);
    macro.duration = readLE<double>(ctx);
    macro.gameVersion = readLE<uint32_t>(ctx);
    macro.startPosX = readLE<float>(ctx);
    macro.startPosY = readLE<float>(ctx);
    macro.rngSeed = readLE<uint32_t>(ctx);
    macro.recordTimestamp = readLE<int64_t>(ctx);
    if (ctx.failed) {
        return false;
    }

    position = ctx.position;
    if (headerSize > position) {
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

    TTRReadContext ctx { data, position };
    uint32_t inputCount = readCount(ctx, 10, "input");
    macro->inputs.reserve(inputCount);
    for (uint32_t index = 0; index < inputCount && !ctx.failed; ++index) {
        TTRInput input;
        input.tick = readLE<int32_t>(ctx);
        input.actionType = readLE<uint8_t>(ctx);
        input.flags = readLE<uint8_t>(ctx);
        input.stepOffset = readLE<float>(ctx);
        if (!ctx.failed) {
            macro->inputs.push_back(input);
        }
    }

    uint32_t anchorCount = readCount(ctx, 94, "anchor");
    macro->anchors.reserve(anchorCount);
    for (uint32_t index = 0; index < anchorCount && !ctx.failed; ++index) {
        PlaybackAnchor anchor;
        anchor.tick = readLE<int32_t>(ctx);
        anchor.hasPlayer2 = true;
        anchor.player1 = convertLegacySnapshot(
            readPlayerSnapshotV1(ctx),
            macro->platformerMode,
            true,
            macro->twoPlayerMode
        );
        anchor.player2 = convertLegacySnapshot(
            readPlayerSnapshotV1(ctx),
            macro->platformerMode,
            true,
            macro->twoPlayerMode
        );
        if (!ctx.failed) {
            macro->anchors.push_back(anchor);
        }
    }

    if (!ctx.failed && ctx.position < ctx.data.size()) {
        uint32_t checkpointCount = readCount(ctx, 16, "checkpoint");
        macro->checkpoints.reserve(checkpointCount);
        for (uint32_t index = 0; index < checkpointCount && !ctx.failed; ++index) {
            TTRCheckpoint checkpoint;
            checkpoint.tick = readLE<int32_t>(ctx);
            checkpoint.rngState = readLE<uint64_t>(ctx);
            checkpoint.priorTick = readLE<int32_t>(ctx);
            if (!ctx.failed) {
                macro->checkpoints.push_back(checkpoint);
            }
        }
    }

    if (ctx.failed) {
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

    TTRReadContext headerCtx { data, position };
    uint32_t uncompressedSize = readLE<uint32_t>(headerCtx);
    if (headerCtx.failed) {
        delete macro;
        return nullptr;
    }

    std::vector<uint8_t> payload;
    if (!zlibDecompress(data, headerCtx.position, data.size() - headerCtx.position, uncompressedSize, payload)) {
        delete macro;
        return nullptr;
    }

    TTRReadContext payloadCtx { payload, 0 };
    bool hasTimedOffsets = (flags & (TTR_FLAG_ACCURACY_CBS | TTR_FLAG_ACCURACY_CBF)) != 0;
    size_t minInputBytes = hasTimedOffsets ? 7 : 3;
    size_t minAnchorBytes = version >= 3 ? 45 : 39;

    uint32_t inputCount = readCount(payloadCtx, minInputBytes, "input");
    macro->inputs.reserve(inputCount);
    int32_t previousInputTick = 0;
    for (uint32_t index = 0; index < inputCount && !payloadCtx.failed; ++index) {
        TTRInput input;
        previousInputTick += static_cast<int32_t>(readVarint(payloadCtx));
        input.tick = previousInputTick;
        input.actionType = readLE<uint8_t>(payloadCtx);
        input.flags = readLE<uint8_t>(payloadCtx);
        input.stepOffset = hasTimedOffsets ? readLE<float>(payloadCtx) : 0.0f;
        if (!payloadCtx.failed) {
            macro->inputs.push_back(input);
        }
    }

    uint32_t anchorCount = readCount(payloadCtx, minAnchorBytes, "anchor");
    macro->anchors.reserve(anchorCount);
    int32_t previousAnchorTick = 0;
    for (uint32_t index = 0; index < anchorCount && !payloadCtx.failed; ++index) {
        PlaybackAnchor anchor;
        previousAnchorTick += static_cast<int32_t>(readVarint(payloadCtx));
        anchor.tick = previousAnchorTick;

        if (version >= 3) {
            uint8_t anchorFlags = readLE<uint8_t>(payloadCtx);
            anchor.hasPlayer2 = (anchorFlags & (1 << 0)) != 0;
            anchor.player1 = readAnchorPlayerV3(payloadCtx);
            anchor.player1LatchMask = readLE<uint8_t>(payloadCtx);
            if (anchor.hasPlayer2) {
                anchor.player2 = readAnchorPlayerV3(payloadCtx);
                anchor.player2LatchMask = readLE<uint8_t>(payloadCtx);
            }
            if ((anchorFlags & (1 << 1)) != 0) {
                anchor.rng.fastRandState = static_cast<uintptr_t>(readLE<uint64_t>(payloadCtx));
            }
            anchor.rng.locked = macro->rngLocked;
            anchor.rng.seed = macro->rngSeed;
        } else {
            uint8_t anchorFlags = readLE<uint8_t>(payloadCtx);
            anchor.hasPlayer2 = (anchorFlags & 0x01) != 0;
            anchor.player1 = convertLegacySnapshot(
                readPlayerSnapshotV2(payloadCtx),
                macro->platformerMode,
                anchor.hasPlayer2,
                macro->twoPlayerMode
            );
            if (anchor.hasPlayer2) {
                anchor.player2 = convertLegacySnapshot(
                    readPlayerSnapshotV2(payloadCtx),
                    macro->platformerMode,
                    anchor.hasPlayer2,
                    macro->twoPlayerMode
                );
            }
        }

        if (!payloadCtx.failed) {
            macro->anchors.push_back(anchor);
        }
    }

    if (!payloadCtx.failed && payloadCtx.position < payload.size()) {
        uint32_t checkpointCount = readCount(payloadCtx, 16, "checkpoint");
        macro->checkpoints.reserve(checkpointCount);
        for (uint32_t index = 0; index < checkpointCount && !payloadCtx.failed; ++index) {
            TTRCheckpoint checkpoint;
            checkpoint.tick = readLE<int32_t>(payloadCtx);
            checkpoint.rngState = readLE<uint64_t>(payloadCtx);
            checkpoint.priorTick = readLE<int32_t>(payloadCtx);
            if (!payloadCtx.failed) {
                macro->checkpoints.push_back(checkpoint);
            }
        }
    }

    if (payloadCtx.failed) {
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

    TTRReadContext ctx { data, 4 };
    uint16_t version = readLE<uint16_t>(ctx);
    if (ctx.failed) {
        return nullptr;
    }
    if (version > TTR_FORMAT_VERSION) {
        log::warn("[TTR] Format version {} is newer than supported ({})", version, TTR_FORMAT_VERSION);
        return nullptr;
    }

    uint32_t flags = readLE<uint32_t>(ctx);
    uint32_t headerSize = readLE<uint32_t>(ctx);
    if (ctx.failed) {
        return nullptr;
    }

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
        return deserializeLegacyV1(data, ctx.position, flags, headerSize, macro);
    }

    return deserializeCompressedPayload(data, ctx.position, version, flags, headerSize, macro);
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
    if (bytes.empty()) {
        log::warn("[TTR] Failed to save macro {}", name);
        return;
    }
    std::ofstream output(directory / (name + ".ttr"), std::ios::binary);
    output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    output.close();

    log::info(
        "[TTR] Saved macro to {} ({} bytes, {} inputs, {} anchors)",
        toasty::pathToUtf8(directory / (name + ".ttr")),
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
    if (!std::filesystem::exists(path)) {
        path = directory / filename;
        if (!std::filesystem::exists(path)) {
            return nullptr;
        }
    }

    auto bytes = ReplayStorage::readReplayBytes(path);
    if (!bytes) {
        return nullptr;
    }

    auto* result = deserialize(*bytes);
    if (result) {
        auto stem = toasty::pathToUtf8(path.stem());
        result->name = stem;
        result->persistedName = stem;
        log::info("[TTR] Loaded macro: {} ({} inputs, {} anchors)", stem, result->inputs.size(), result->anchors.size());
    } else {
        log::warn("[TTR] Failed to load macro {}", toasty::pathToUtf8(path));
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
