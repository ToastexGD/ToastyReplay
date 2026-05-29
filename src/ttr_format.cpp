#include "ttr_format.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
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
    state.motion.dashX = player->m_dashX;
    state.motion.dashY = player->m_dashY;
    state.motion.dashAngle = player->m_dashAngle;
    state.motion.dashStartTime = player->m_dashStartTime;
    state.motion.slopeStartTime = player->m_slopeStartTime;
    state.motion.fallSpeed = player->m_fallSpeed;
    state.motion.slopeVelocity = player->m_slopeVelocity;
    state.motion.shipRotation = player->m_shipRotation;
    state.motion.lastPortalPosition = player->m_lastPortalPos;
    state.motion.stateForceVector = player->m_stateForceVector;
    state.flags.upsideDown = player->m_isUpsideDown;
    state.flags.holdingLeft = player->m_holdingLeft;
    state.flags.holdingRight = player->m_holdingRight;
    state.flags.platformer = isPlatformer;
    state.flags.dead = player->m_isDead;
    state.flags.ship = player->m_isShip;
    state.flags.bird = player->m_isBird;
    state.flags.ball = player->m_isBall;
    state.flags.wave = player->m_isDart;
    state.flags.robot = player->m_isRobot;
    state.flags.spider = player->m_isSpider;
    state.flags.swing = player->m_isSwing;
    state.flags.sideways = player->m_isSideways;
    state.flags.dashing = player->m_isDashing;
    state.flags.onSlope = player->m_isOnSlope;
    state.flags.wasOnSlope = player->m_wasOnSlope;
    state.flags.onGround = player->m_isOnGround;
    state.flags.goingLeft = player->m_isGoingLeft;
    state.flags.platformerMovingRight = player->m_platformerMovingRight;
    state.flags.slidingRight = player->m_isSlidingRight;
    state.flags.accelerating = player->m_isAccelerating;
    state.flags.affectedByForces = player->m_affectedByForces;
    state.flags.jumpBuffered = player->m_jumpBuffered;
    state.flags.buttonHolds[0] = player->m_holdingButtons[1];
    state.flags.buttonHolds[1] = player->m_holdingButtons[2];
    state.flags.buttonHolds[2] = player->m_holdingButtons[3];
    state.environment.gravity = player->m_gravity;
    state.environment.gravityMod = player->m_gravityMod;
    state.environment.playerSpeed = player->m_playerSpeed;
    state.environment.playerSpeedAC = player->m_playerSpeedAC;
    state.environment.speedMultiplier = player->m_speedMultiplier;
    state.environment.vehicleSize = player->m_vehicleSize;
    state.environment.reverseRelated = player->m_reverseRelated;
    state.environment.stateDartSlide = player->m_stateDartSlide;
    state.environment.stateFlipGravity = player->m_stateFlipGravity;
    state.environment.stateForce = player->m_stateForce;
    state.environment.dualContext = isDual;
    state.environment.twoPlayerContext = isTwoPlayer;
    state.environment.extendedState = true;
    return state;
}

static uint32_t packExtendedPlayerFlags(PlayerStateBundle const& state) {
    uint32_t flags = 0;
    if (state.flags.ship) flags |= 1 << 0;
    if (state.flags.bird) flags |= 1 << 1;
    if (state.flags.ball) flags |= 1 << 2;
    if (state.flags.wave) flags |= 1 << 3;
    if (state.flags.robot) flags |= 1 << 4;
    if (state.flags.spider) flags |= 1 << 5;
    if (state.flags.swing) flags |= 1 << 6;
    if (state.flags.sideways) flags |= 1 << 7;
    if (state.flags.dashing) flags |= 1 << 8;
    if (state.flags.onSlope) flags |= 1 << 9;
    if (state.flags.wasOnSlope) flags |= 1 << 10;
    if (state.flags.onGround) flags |= 1 << 11;
    if (state.flags.goingLeft) flags |= 1 << 12;
    if (state.flags.platformerMovingRight) flags |= 1 << 13;
    if (state.flags.slidingRight) flags |= 1 << 14;
    if (state.flags.accelerating) flags |= 1 << 15;
    if (state.flags.affectedByForces) flags |= 1 << 16;
    if (state.flags.jumpBuffered) flags |= 1 << 17;
    return flags;
}

static void unpackExtendedPlayerFlags(PlayerStateBundle& state, uint32_t flags) {
    state.flags.ship = (flags & (1 << 0)) != 0;
    state.flags.bird = (flags & (1 << 1)) != 0;
    state.flags.ball = (flags & (1 << 2)) != 0;
    state.flags.wave = (flags & (1 << 3)) != 0;
    state.flags.robot = (flags & (1 << 4)) != 0;
    state.flags.spider = (flags & (1 << 5)) != 0;
    state.flags.swing = (flags & (1 << 6)) != 0;
    state.flags.sideways = (flags & (1 << 7)) != 0;
    state.flags.dashing = (flags & (1 << 8)) != 0;
    state.flags.onSlope = (flags & (1 << 9)) != 0;
    state.flags.wasOnSlope = (flags & (1 << 10)) != 0;
    state.flags.onGround = (flags & (1 << 11)) != 0;
    state.flags.goingLeft = (flags & (1 << 12)) != 0;
    state.flags.platformerMovingRight = (flags & (1 << 13)) != 0;
    state.flags.slidingRight = (flags & (1 << 14)) != 0;
    state.flags.accelerating = (flags & (1 << 15)) != 0;
    state.flags.affectedByForces = (flags & (1 << 16)) != 0;
    state.flags.jumpBuffered = (flags & (1 << 17)) != 0;
}

static void writeAnchorPlayer(std::vector<uint8_t>& payload, PlayerStateBundle const& state, bool includeExtended) {
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

    if (!includeExtended) {
        return;
    }

    writeLE<uint32_t>(payload, packExtendedPlayerFlags(state));
    writeLE<double>(payload, state.motion.dashX);
    writeLE<double>(payload, state.motion.dashY);
    writeLE<double>(payload, state.motion.dashAngle);
    writeLE<double>(payload, state.motion.dashStartTime);
    writeLE<double>(payload, state.motion.slopeStartTime);
    writeLE<float>(payload, state.motion.fallSpeed);
    writeLE<float>(payload, state.motion.slopeVelocity);
    writeLE<float>(payload, state.motion.shipRotation.x);
    writeLE<float>(payload, state.motion.shipRotation.y);
    writeLE<float>(payload, state.motion.lastPortalPosition.x);
    writeLE<float>(payload, state.motion.lastPortalPosition.y);
    writeLE<float>(payload, state.motion.stateForceVector.x);
    writeLE<float>(payload, state.motion.stateForceVector.y);
    writeLE<float>(payload, state.environment.gravityMod);
    writeLE<float>(payload, state.environment.playerSpeed);
    writeLE<float>(payload, state.environment.playerSpeedAC);
    writeLE<double>(payload, state.environment.speedMultiplier);
    writeLE<float>(payload, state.environment.vehicleSize);
    writeLE<int32_t>(payload, state.environment.reverseRelated);
    writeLE<int32_t>(payload, state.environment.stateDartSlide);
    writeLE<int32_t>(payload, state.environment.stateFlipGravity);
    writeLE<int32_t>(payload, state.environment.stateForce);
}

static PlayerStateBundle readAnchorPlayer(TTRReadContext& ctx, bool includeExtended) {
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

    if (includeExtended && !ctx.failed) {
        unpackExtendedPlayerFlags(state, readLE<uint32_t>(ctx));
        state.motion.dashX = readLE<double>(ctx);
        state.motion.dashY = readLE<double>(ctx);
        state.motion.dashAngle = readLE<double>(ctx);
        state.motion.dashStartTime = readLE<double>(ctx);
        state.motion.slopeStartTime = readLE<double>(ctx);
        state.motion.fallSpeed = readLE<float>(ctx);
        state.motion.slopeVelocity = readLE<float>(ctx);
        state.motion.shipRotation.x = readLE<float>(ctx);
        state.motion.shipRotation.y = readLE<float>(ctx);
        state.motion.lastPortalPosition.x = readLE<float>(ctx);
        state.motion.lastPortalPosition.y = readLE<float>(ctx);
        state.motion.stateForceVector.x = readLE<float>(ctx);
        state.motion.stateForceVector.y = readLE<float>(ctx);
        state.environment.gravityMod = readLE<float>(ctx);
        state.environment.playerSpeed = readLE<float>(ctx);
        state.environment.playerSpeedAC = readLE<float>(ctx);
        state.environment.speedMultiplier = readLE<double>(ctx);
        state.environment.vehicleSize = readLE<float>(ctx);
        state.environment.reverseRelated = readLE<int32_t>(ctx);
        state.environment.stateDartSlide = readLE<int32_t>(ctx);
        state.environment.stateFlipGravity = readLE<int32_t>(ctx);
        state.environment.stateForce = readLE<int32_t>(ctx);
        state.environment.extendedState = !ctx.failed;
    }
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
    buffer.push_back('2');

    writeLE<uint16_t>(buffer, TTR2_FORMAT_VERSION);

    uint32_t flags = 0;
    AccuracyMode storedAccuracyMode = writableAccuracyMode(macro.accuracyMode);
    bool hasExactCbsTiming = macro.exactCbsTiming || std::any_of(macro.inputs.begin(), macro.inputs.end(), [](TTRInput const& input) {
        return std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0f;
    });
    if (!hasExactCbsTiming) {
        hasExactCbsTiming = std::any_of(macro.persistenceAttempts.begin(), macro.persistenceAttempts.end(), [](TTRAttemptSegment const& attempt) {
            return std::any_of(attempt.inputs.begin(), attempt.inputs.end(), [](TTRInput const& input) {
                return std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0f;
            });
        });
    }
    if (storedAccuracyMode == AccuracyMode::CBS) flags |= TTR_FLAG_ACCURACY_CBS;
    if (storedAccuracyMode == AccuracyMode::CBS && hasExactCbsTiming) flags |= TTR_FLAG_EXACT_CBS_TIMING;
    if (macro.recordedFromStartPos) flags |= TTR_FLAG_FROM_START_POS;
    if (macro.platformerMode) flags |= TTR_FLAG_PLATFORMER;
    if (macro.twoPlayerMode) flags |= TTR_FLAG_TWO_PLAYER;
    if (macro.rngLocked) flags |= TTR_FLAG_RNG_LOCKED;
    if (!macro.persistenceAttempts.empty()) flags |= TTR_FLAG_PERSISTENCE;
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

void TTRMacro::recordAction(std::vector<TTRInput>& target, int tick, int button, bool player2, bool pressed, float offset, double cbsTimeOffset) {
    TTRInput input;
    input.tick = tick;
    input.actionType = static_cast<uint8_t>(button);
    input.setPlayer2(player2);
    input.setPressed(pressed);
    input.stepOffset = offset;
    if (std::isfinite(cbsTimeOffset) && cbsTimeOffset >= 0.0f) {
        input.cbsTimeOffset = cbsTimeOffset;
        exactCbsTiming = true;
    }
    target.push_back(input);
}

void TTRMacro::recordAction(int tick, int button, bool player2, bool pressed, float offset, double cbsTimeOffset) {
    recordAction(inputs, tick, button, player2, pressed, offset, cbsTimeOffset);
}

void TTRMacro::recordAnchor(std::vector<PlaybackAnchor>& target, int tick, PlayerObject* p1, PlayerObject* p2, bool isPlatformer, bool isDual) {
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

    if (!target.empty() && target.back().tick == tick) {
        target.back() = std::move(anchor);
    } else {
        target.push_back(std::move(anchor));
    }
}

void TTRMacro::recordAnchor(int tick, PlayerObject* p1, PlayerObject* p2, bool isPlatformer, bool isDual) {
    recordAnchor(anchors, tick, p1, p2, isPlatformer, isDual);
}

void TTRMacro::truncateAfter(int tick) {
    while (!inputs.empty() && inputs.back().tick >= tick) {
        inputs.pop_back();
    }
    while (!anchors.empty() && anchors.back().tick >= tick) {
        anchors.pop_back();
    }
}

static void writeTTR2Inputs(std::vector<uint8_t>& payload, std::vector<TTRInput> const& inputList, bool storeExactCbsTiming) {
    writeLE<uint32_t>(payload, static_cast<uint32_t>(inputList.size()));
    int32_t previousInputTick = 0;
    for (auto const& input : inputList) {
        writeVarint(payload, static_cast<uint32_t>(input.tick - previousInputTick));
        previousInputTick = input.tick;
        writeLE<uint8_t>(payload, input.actionType);
        writeLE<uint8_t>(payload, input.flags);
        if (storeExactCbsTiming) {
            writeLE<double>(payload, input.cbsTimeOffset);
        }
    }
}

static void writeTTR2Anchors(std::vector<uint8_t>& payload, std::vector<PlaybackAnchor> const& anchorList) {
    writeLE<uint32_t>(payload, static_cast<uint32_t>(anchorList.size()));
    int32_t previousAnchorTick = 0;
    for (auto const& anchor : anchorList) {
        writeVarint(payload, static_cast<uint32_t>(anchor.tick - previousAnchorTick));
        previousAnchorTick = anchor.tick;

        uint8_t anchorFlags = 0;
        if (anchor.hasPlayer2) anchorFlags |= 1 << 0;
        if (anchor.rng.fastRandState != 0) anchorFlags |= 1 << 1;
        writeLE<uint8_t>(payload, anchorFlags);
        writeAnchorPlayer(payload, anchor.player1, true);
        writeLE<uint8_t>(payload, anchor.player1LatchMask);
        if (anchor.hasPlayer2) {
            writeAnchorPlayer(payload, anchor.player2, true);
            writeLE<uint8_t>(payload, anchor.player2LatchMask);
        }
        if ((anchorFlags & (1 << 1)) != 0) {
            writeLE<uint64_t>(payload, static_cast<uint64_t>(anchor.rng.fastRandState));
        }
    }
}

std::vector<uint8_t> TTRMacro::serialize() const {
    std::vector<uint8_t> output;
    output.reserve(256);
    writeHeader(output, *this);
    AccuracyMode storedAccuracyMode = writableAccuracyMode(accuracyMode);
    bool storeExactCbsTiming = storedAccuracyMode == AccuracyMode::CBS;

    std::vector<uint8_t> payload;
    payload.reserve(inputs.size() * (storeExactCbsTiming ? 11 : 3) + anchors.size() * 180 + checkpoints.size() * 16 + persistenceAttempts.size() * 32 + 16);

    writeTTR2Inputs(payload, inputs, storeExactCbsTiming);
    writeTTR2Anchors(payload, anchors);

    writeLE<uint32_t>(payload, static_cast<uint32_t>(checkpoints.size()));
    for (auto const& checkpoint : checkpoints) {
        writeLE<int32_t>(payload, checkpoint.tick);
        writeLE<uint64_t>(payload, checkpoint.rngState);
        writeLE<int32_t>(payload, checkpoint.priorTick);
    }

    writeLE<uint32_t>(payload, static_cast<uint32_t>(persistenceAttempts.size()));
    for (auto const& attempt : persistenceAttempts) {
        writeLE<int32_t>(payload, attempt.deathTick);
        writeLE<uint8_t>(payload, attempt.deathPlayer2 ? 1 : 0);
        writeTTR2Inputs(payload, attempt.inputs, storeExactCbsTiming);
        writeTTR2Anchors(payload, attempt.anchors);
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
    bool hasTimedOffsets = (flags & (TTR_FLAG_ACCURACY_CBS | TTR_FLAG_ACCURACY_CBF | TTR_FLAG_ACCURACY_SUBSTEP)) != 0;
    bool hasExactCbsTiming = version >= 6 && (flags & TTR_FLAG_EXACT_CBS_TIMING) != 0;
    bool hasExtendedAnchors = version >= 6;
    size_t minInputBytes = 3 + (hasTimedOffsets ? sizeof(float) : 0) + (hasExactCbsTiming ? sizeof(float) : 0);
    size_t minAnchorBytes = hasExtendedAnchors ? 150 : (version >= 3 ? 45 : 39);

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
        input.cbsTimeOffset = hasExactCbsTiming ? static_cast<double>(readLE<float>(payloadCtx)) : -1.0;
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
            anchor.player1 = readAnchorPlayer(payloadCtx, hasExtendedAnchors);
            anchor.player1LatchMask = readLE<uint8_t>(payloadCtx);
            if (anchor.hasPlayer2) {
                anchor.player2 = readAnchorPlayer(payloadCtx, hasExtendedAnchors);
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

static void readTTR2Inputs(
    TTRReadContext& payloadCtx,
    TTRMacro* macro,
    std::vector<TTRInput>& inputList,
    bool hasCbsTiming
) {
    size_t minInputBytes = 3 + (hasCbsTiming ? sizeof(double) : 0);
    uint32_t inputCount = readCount(payloadCtx, minInputBytes, "input");
    inputList.reserve(inputList.size() + inputCount);
    int32_t previousInputTick = 0;
    for (uint32_t index = 0; index < inputCount && !payloadCtx.failed; ++index) {
        TTRInput input;
        previousInputTick += static_cast<int32_t>(readVarint(payloadCtx));
        input.tick = previousInputTick;
        input.actionType = readLE<uint8_t>(payloadCtx);
        input.flags = readLE<uint8_t>(payloadCtx);
        if (hasCbsTiming) {
            input.cbsTimeOffset = readLE<double>(payloadCtx);
            if (std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0) {
                input.stepOffset = static_cast<float>(input.cbsTimeOffset * macro->framerate);
            }
        }
        if (!payloadCtx.failed) {
            inputList.push_back(input);
        }
    }
}

static void readTTR2Anchors(
    TTRReadContext& payloadCtx,
    TTRMacro* macro,
    std::vector<PlaybackAnchor>& anchorList
) {
    size_t minAnchorBytes = 150;
    uint32_t anchorCount = readCount(payloadCtx, minAnchorBytes, "anchor");
    anchorList.reserve(anchorList.size() + anchorCount);
    int32_t previousAnchorTick = 0;
    for (uint32_t index = 0; index < anchorCount && !payloadCtx.failed; ++index) {
        PlaybackAnchor anchor;
        previousAnchorTick += static_cast<int32_t>(readVarint(payloadCtx));
        anchor.tick = previousAnchorTick;

        uint8_t anchorFlags = readLE<uint8_t>(payloadCtx);
        anchor.hasPlayer2 = (anchorFlags & (1 << 0)) != 0;
        anchor.player1 = readAnchorPlayer(payloadCtx, true);
        anchor.player1LatchMask = readLE<uint8_t>(payloadCtx);
        if (anchor.hasPlayer2) {
            anchor.player2 = readAnchorPlayer(payloadCtx, true);
            anchor.player2LatchMask = readLE<uint8_t>(payloadCtx);
        }
        if ((anchorFlags & (1 << 1)) != 0) {
            anchor.rng.fastRandState = static_cast<uintptr_t>(readLE<uint64_t>(payloadCtx));
        }
        anchor.rng.locked = macro->rngLocked;
        anchor.rng.seed = macro->rngSeed;

        if (!payloadCtx.failed) {
            anchorList.push_back(anchor);
        }
    }
}

static TTRMacro* deserializeTTR2CompressedPayload(
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
    bool hasCbsTiming = macro->accuracyMode == AccuracyMode::CBS;
    readTTR2Inputs(payloadCtx, macro, macro->inputs, hasCbsTiming);
    readTTR2Anchors(payloadCtx, macro, macro->anchors);

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

    if (!payloadCtx.failed && version >= 2 && payloadCtx.position < payload.size()) {
        uint32_t attemptCount = readCount(payloadCtx, 9, "persistence attempt");
        macro->persistenceAttempts.reserve(attemptCount);
        for (uint32_t index = 0; index < attemptCount && !payloadCtx.failed; ++index) {
            TTRAttemptSegment attempt;
            attempt.deathTick = readLE<int32_t>(payloadCtx);
            attempt.deathPlayer2 = readLE<uint8_t>(payloadCtx) != 0;
            readTTR2Inputs(payloadCtx, macro, attempt.inputs, hasCbsTiming);
            readTTR2Anchors(payloadCtx, macro, attempt.anchors);
            if (!payloadCtx.failed && attempt.hasData()) {
                macro->persistenceAttempts.push_back(std::move(attempt));
            }
        }
    } else if ((flags & TTR_FLAG_PERSISTENCE) != 0) {
        payloadCtx.failed = true;
    }

    if (payloadCtx.failed) {
        delete macro;
        return nullptr;
    }

    macro->exactCbsTiming = macro->accuracyMode == AccuracyMode::CBS && std::any_of(macro->inputs.begin(), macro->inputs.end(), [](TTRInput const& input) {
        return std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0;
    });
    return macro;
}

TTRMacro* TTRMacro::deserialize(std::vector<uint8_t> const& data) {
    if (data.size() < 14) {
        return nullptr;
    }

    bool isTTR2 = data[0] == 'T' && data[1] == 'T' && data[2] == 'R' && data[3] == '2';
    bool isLegacyTTR = data[0] == 'T' && data[1] == 'T' && data[2] == 'R' && data[3] == '\0';
    if (!isTTR2 && !isLegacyTTR) {
        return nullptr;
    }

    TTRReadContext ctx { data, 4 };
    uint16_t version = readLE<uint16_t>(ctx);
    if (ctx.failed) {
        return nullptr;
    }
    uint16_t supportedVersion = isTTR2 ? TTR2_FORMAT_VERSION : TTR_FORMAT_VERSION;
    if (version > supportedVersion) {
        log::warn("[TTR] Format version {} is newer than supported ({})", version, supportedVersion);
        return nullptr;
    }

    uint32_t flags = readLE<uint32_t>(ctx);
    uint32_t headerSize = readLE<uint32_t>(ctx);
    if (ctx.failed) {
        return nullptr;
    }

    auto* macro = new TTRMacro();
    macro->fileFormat = isTTR2 ? TTRFileFormat::TTR2 : TTRFileFormat::LegacyTTR;
    if ((flags & TTR_FLAG_ACCURACY_CBS) != 0) {
        macro->accuracyMode = AccuracyMode::CBS;
    } else {
        macro->accuracyMode = AccuracyMode::Vanilla;
    }
    macro->exactCbsTiming = isTTR2
        ? ((flags & TTR_FLAG_EXACT_CBS_TIMING) != 0 && macro->accuracyMode == AccuracyMode::CBS)
        : ((flags & TTR_FLAG_EXACT_CBS_TIMING) != 0 && version >= 6);
    macro->recordedFromStartPos = (flags & TTR_FLAG_FROM_START_POS) != 0;
    macro->platformerMode = (flags & TTR_FLAG_PLATFORMER) != 0;
    macro->twoPlayerMode = (flags & TTR_FLAG_TWO_PLAYER) != 0;
    macro->rngLocked = (flags & TTR_FLAG_RNG_LOCKED) != 0;

    if (isTTR2) {
        return deserializeTTR2CompressedPayload(data, ctx.position, version, flags, headerSize, macro);
    }

    if (version == 1) {
        return deserializeLegacyV1(data, ctx.position, flags, headerSize, macro);
    }

    return deserializeCompressedPayload(data, ctx.position, version, flags, headerSize, macro);
}

void TTRMacro::persist() {
    author = GJAccountManager::get()->m_username;
    duration = 0.0;
    double safeFramerate = std::isfinite(framerate) && framerate > 0.0 ? framerate : 240.0;
    for (auto const& input : inputs) {
        double inputTime = static_cast<double>(std::max(0, input.tick)) / safeFramerate;
        if (std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0) {
            inputTime += input.cbsTimeOffset;
        }
        duration = std::max(duration, inputTime);
    }
    recordTimestamp = static_cast<int64_t>(std::time(nullptr));
    accuracyMode = writableAccuracyMode(accuracyMode);
    auto stripTimedOffsets = [](std::vector<TTRInput>& inputList) {
        for (auto& input : inputList) {
            input.stepOffset = 0.0f;
            input.cbsTimeOffset = -1.0;
        }
    };
    if (!usesTimedAccuracy(accuracyMode)) {
        stripTimedOffsets(inputs);
        for (auto& attempt : persistenceAttempts) {
            stripTimedOffsets(attempt.inputs);
        }
        exactCbsTiming = false;
    } else if (accuracyMode != AccuracyMode::CBS) {
        exactCbsTiming = false;
    } else {
        exactCbsTiming = std::any_of(inputs.begin(), inputs.end(), [](TTRInput const& input) {
            return std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0f;
        });
        if (!exactCbsTiming) {
            exactCbsTiming = std::any_of(persistenceAttempts.begin(), persistenceAttempts.end(), [](TTRAttemptSegment const& attempt) {
                return std::any_of(attempt.inputs.begin(), attempt.inputs.end(), [](TTRInput const& input) {
                    return std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0f;
                });
            });
        }
    }

    auto directory = ReplayStorage::getReplayDirectoryPath();
    if (!std::filesystem::exists(directory)) {
        std::filesystem::create_directory(directory);
    }

    std::string excludedName = loadedFromLegacyFormat() ? "" : persistedName;
    name = ReplayStorage::makeUniqueReplayName(name, excludedName);
    persistedName = name;

    auto bytes = serialize();
    if (bytes.empty()) {
        log::warn("[TTR2] Failed to save macro {}", name);
        return;
    }
    auto outputPath = directory / (name + ".ttr2");
    std::ofstream output(outputPath, std::ios::binary);
    output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    output.close();
    fileFormat = TTRFileFormat::TTR2;

    log::info(
        "[TTR2] Saved macro to {} ({} bytes, {} inputs, {} anchors)",
        toasty::pathToUtf8(outputPath),
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

    std::vector<std::filesystem::path> candidates;
    std::filesystem::path requested(filename);
    auto requestedExtension = toasty::pathToUtf8(requested.extension());
    std::transform(requestedExtension.begin(), requestedExtension.end(), requestedExtension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (requestedExtension == ".ttr2" || requestedExtension == ".ttr") {
        candidates.push_back(directory / requested);
    } else {
        candidates.push_back(directory / (filename + ".ttr2"));
        candidates.push_back(directory / (filename + ".ttr"));
        candidates.push_back(directory / filename);
    }

    std::filesystem::path path;
    for (auto const& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.empty()) {
        return nullptr;
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
        auto extension = toasty::pathToUtf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (extension == ".ttr") {
            result->fileFormat = TTRFileFormat::LegacyTTR;
        }
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

std::vector<MacroAction> TTRMacro::toPersistenceMacroActions() const {
    size_t totalInputs = inputs.size();
    for (auto const& attempt : persistenceAttempts) {
        totalInputs += attempt.inputs.size();
    }

    std::vector<MacroAction> actions;
    actions.reserve(totalInputs);

    int32_t baseTick = 0;
    for (auto const& attempt : persistenceAttempts) {
        for (auto const& input : attempt.inputs) {
            actions.emplace_back(
                baseTick + input.tick,
                static_cast<int>(input.actionType),
                input.isPlayer2(),
                input.isPressed(),
                input.stepOffset
            );
        }
        baseTick += std::max<int32_t>(1, attempt.deathTick);
    }

    for (auto const& input : inputs) {
        actions.emplace_back(
            baseTick + input.tick,
            static_cast<int>(input.actionType),
            input.isPlayer2(),
            input.isPressed(),
            input.stepOffset
        );
    }

    return actions;
}
