#include "ttr3_format.hpp"
#include "ttr_format.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

#include <zlib.h>

namespace toasty::ttr3 {
namespace {

static constexpr size_t kHeaderSize = 16;
static constexpr size_t kMaxStringSize = 4096;
static constexpr uint64_t kMaxPayloadSize = 512ull * 1024ull * 1024ull;
static constexpr uint32_t kKnownFlags =
    FlagLosslessVerified | FlagFromStartPos | FlagPlatformer | FlagTwoPlayer |
    FlagRngLocked | FlagHasAnchors | FlagHasCheckpoints | FlagHasPersistence |
    FlagHasTpsEvents | FlagMacroConverted | FlagCompressed;

struct ReadContext;

template <typename T>
static T readLE(ReadContext& ctx);

static std::string readString(ReadContext& ctx);
static bool checkCountFits(ReadContext& ctx, uint64_t count, size_t minRecordSize, std::string* error, char const* label);
static void writeInputsSection(std::vector<uint8_t>& out, std::vector<Input> const& inputs);
static std::vector<Input> readInputsSection(ReadContext& ctx, double framerateHint, std::string* error);
static std::vector<TpsEvent> readTpsEventsSection(ReadContext& ctx, std::string* error);
static std::vector<Checkpoint> readCheckpointsSection(ReadContext& ctx, std::string* error);
static std::vector<Anchor> readAnchorsSection(ReadContext& ctx, std::string* error);
static std::vector<Attempt> readPersistenceSection(ReadContext& ctx, double framerateHint, std::string* error);
static PlayerStateBundle readPlayerState(ReadContext& ctx);
static Anchor readAnchor(ReadContext& ctx, std::string* error);

struct ReadContext {
    std::vector<uint8_t> const& data;
    size_t position = 0;
    size_t end = 0;
    bool failed = false;

    explicit ReadContext(
        std::vector<uint8_t> const& bytes,
        size_t start = 0,
        size_t limit = std::numeric_limits<size_t>::max()
    )
        : data(bytes), position(start), end(std::min(limit, bytes.size())) {
        if (start > end) {
            failed = true;
        }
    }

    size_t remaining() const {
        return !failed && position <= end ? end - position : 0;
    }
};

static bool validTime(double value) {
    return std::isfinite(value) && value >= 0.0;
}

static bool validTps(double value) {
    return std::isfinite(value) && value > 0.0 && value <= 1000000.0;
}

template <typename T>
static void writeLE(std::vector<uint8_t>& buffer, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    size_t position = buffer.size();
    buffer.resize(position + sizeof(T));
    std::memcpy(buffer.data() + position, &value, sizeof(T));
}

static void patchU32(std::vector<uint8_t>& buffer, size_t position, uint32_t value) {
    std::memcpy(buffer.data() + position, &value, sizeof(value));
}

static void writeString(std::vector<uint8_t>& buffer, std::string const& value) {
    uint16_t length = static_cast<uint16_t>(std::min(value.size(), kMaxStringSize));
    writeLE<uint16_t>(buffer, length);
    buffer.insert(buffer.end(), value.begin(), value.begin() + length);
}

static bool zlibCompress(std::vector<uint8_t> const& input, std::vector<uint8_t>& output) {
    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    output.resize(bound);
    int result = compress2(
        output.data(),
        &bound,
        input.empty() ? Z_NULL : input.data(),
        static_cast<uLong>(input.size()),
        Z_DEFAULT_COMPRESSION
    );
    if (result != Z_OK) {
        output.clear();
        return false;
    }
    output.resize(bound);
    return true;
}

static bool zlibDecompress(
    std::vector<uint8_t> const& input,
    std::vector<uint8_t>& output,
    size_t expectedSize
) {
    if (expectedSize == 0 || expectedSize > kMaxPayloadSize) {
        return false;
    }
    output.resize(expectedSize);
    uLongf destinationLength = static_cast<uLongf>(expectedSize);
    int result = uncompress(
        output.data(),
        &destinationLength,
        input.empty() ? Z_NULL : input.data(),
        static_cast<uLong>(input.size())
    );
    if (result != Z_OK || destinationLength != expectedSize) {
        output.clear();
        return false;
    }
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

static void writePlayerState(std::vector<uint8_t>& payload, PlayerStateBundle const& state) {
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
    if (state.environment.extendedState) stateFlags |= 1 << 7;
    writeLE<uint8_t>(payload, stateFlags);
    writeLE<uint8_t>(payload, packHoldMask(state.flags.buttonHolds));
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

static PlayerStateBundle readPlayerState(ReadContext& ctx) {
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
    bool extendedFlagSet = (stateFlags & (1 << 7)) != 0;
    uint8_t holdMask = readLE<uint8_t>(ctx);
    if ((holdMask & 0xF0u) != 0) {
        ctx.failed = true;
        return state;
    }
    state.flags.buttonHolds = unpackHoldMask(holdMask);

    uint32_t extendedFlags = readLE<uint32_t>(ctx);
    if (extendedFlags & 0xFFF80000u) {
        ctx.failed = true;
        return state;
    }
    unpackExtendedPlayerFlags(state, extendedFlags);
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
    auto finite = [](auto value) {
        return std::isfinite(static_cast<double>(value));
    };
    if (ctx.failed ||
        !finite(state.motion.position.x) ||
        !finite(state.motion.position.y) ||
        !finite(state.motion.verticalVelocity) ||
        !finite(state.motion.preSlopeVerticalVelocity) ||
        !finite(state.motion.horizontalVelocity) ||
        !finite(state.motion.rotation) ||
        !finite(state.environment.gravity) ||
        !finite(state.motion.dashX) ||
        !finite(state.motion.dashY) ||
        !finite(state.motion.dashAngle) ||
        !finite(state.motion.dashStartTime) ||
        !finite(state.motion.slopeStartTime) ||
        !finite(state.motion.fallSpeed) ||
        !finite(state.motion.slopeVelocity) ||
        !finite(state.motion.shipRotation.x) ||
        !finite(state.motion.shipRotation.y) ||
        !finite(state.motion.lastPortalPosition.x) ||
        !finite(state.motion.lastPortalPosition.y) ||
        !finite(state.motion.stateForceVector.x) ||
        !finite(state.motion.stateForceVector.y) ||
        !finite(state.environment.gravityMod) ||
        !finite(state.environment.playerSpeed) ||
        !finite(state.environment.playerSpeedAC) ||
        !finite(state.environment.speedMultiplier) ||
        !finite(state.environment.vehicleSize)) {
        ctx.failed = true;
        return state;
    }

    state.environment.extendedState = extendedFlagSet;
    return state;
}

static void writeAnchor(std::vector<uint8_t>& payload, Anchor const& anchor) {
    writeLE<double>(payload, anchor.timeSeconds);
    uint8_t flags = 0;
    if (anchor.state.hasPlayer2) flags |= 1 << 0;
    if (anchor.state.rng.fastRandState != 0) flags |= 1 << 1;
    writeLE<uint8_t>(payload, flags);
    writeLE<uint8_t>(payload, anchor.state.player1LatchMask);
    writeLE<uint8_t>(payload, anchor.state.player2LatchMask);
    writeLE<uint8_t>(payload, 0);
    writePlayerState(payload, anchor.state.player1);
    if ((flags & (1 << 0)) != 0) {
        writePlayerState(payload, anchor.state.player2);
    }
    if ((flags & (1 << 1)) != 0) {
        writeLE<uint64_t>(payload, static_cast<uint64_t>(anchor.state.rng.fastRandState));
    }
    writeLE<uint8_t>(payload, anchor.state.rng.locked ? 1 : 0);
    writeLE<uint8_t>(payload, 0);
    writeLE<uint8_t>(payload, 0);
    writeLE<uint8_t>(payload, 0);
    writeLE<uint32_t>(payload, anchor.state.rng.seed);
}

static Anchor readAnchor(ReadContext& ctx, std::string* error) {
    Anchor anchor;
    anchor.timeSeconds = readLE<double>(ctx);
    uint8_t flags = readLE<uint8_t>(ctx);
    anchor.state.player1LatchMask = readLE<uint8_t>(ctx);
    anchor.state.player2LatchMask = readLE<uint8_t>(ctx);
    uint8_t reserved = readLE<uint8_t>(ctx);
    if (!validTime(anchor.timeSeconds) || reserved != 0 || (flags & ~0x03u) != 0) {
        ctx.failed = true;
        if (error) *error = "invalid TTR3 anchor";
        return anchor;
    }
    anchor.state.hasPlayer2 = (flags & (1 << 0)) != 0;
    anchor.state.player1 = readPlayerState(ctx);
    if (ctx.failed) {
        if (error && error->empty()) *error = "invalid TTR3 anchor player1";
        return anchor;
    }
    if (anchor.state.hasPlayer2) {
        anchor.state.player2 = readPlayerState(ctx);
        if (ctx.failed) {
            if (error && error->empty()) *error = "invalid TTR3 anchor player2";
            return anchor;
        }
    }
    if ((flags & (1 << 1)) != 0) {
        anchor.state.rng.fastRandState = static_cast<uintptr_t>(readLE<uint64_t>(ctx));
    }
    anchor.state.rng.locked = readLE<uint8_t>(ctx) != 0;
    uint8_t r0 = readLE<uint8_t>(ctx);
    uint8_t r1 = readLE<uint8_t>(ctx);
    uint8_t r2 = readLE<uint8_t>(ctx);
    anchor.state.rng.seed = readLE<uint32_t>(ctx);
    if (ctx.failed || r0 != 0 || r1 != 0 || r2 != 0) {
        ctx.failed = true;
        if (error && error->empty()) *error = "invalid TTR3 anchor";
    }
    return anchor;
}

static void writeAnchorsSection(std::vector<uint8_t>& out, std::vector<Anchor> const& anchors) {
    writeLE<uint64_t>(out, static_cast<uint64_t>(anchors.size()));
    for (auto const& anchor : anchors) {
        writeAnchor(out, anchor);
    }
}

static std::vector<Anchor> readAnchorsSection(ReadContext& ctx, std::string* error) {
    std::vector<Anchor> anchors;
    uint64_t count = readLE<uint64_t>(ctx);
    if (ctx.failed || !checkCountFits(ctx, count, 178, error, "anchor")) return anchors;
    anchors.reserve(static_cast<size_t>(count));
    for (uint64_t index = 0; index < count && !ctx.failed; ++index) {
        Anchor anchor = readAnchor(ctx, error);
        if (!ctx.failed) {
            anchors.push_back(anchor);
        }
    }
    return anchors;
}

static void writeInputsSectionRelative(std::vector<uint8_t>& out, std::vector<Input> const& inputs) {
    writeInputsSection(out, inputs);
}

static std::vector<Input> readInputsSectionRelative(ReadContext& ctx, double framerateHint, std::string* error) {
    return readInputsSection(ctx, framerateHint, error);
}

static void writePersistenceSection(std::vector<uint8_t>& out, std::vector<Attempt> const& attempts) {
    writeLE<uint64_t>(out, static_cast<uint64_t>(attempts.size()));
    for (auto const& attempt : attempts) {
        writeLE<double>(out, attempt.deathTimeSeconds);
        writeLE<uint8_t>(out, attempt.deathPlayer2 ? 1 : 0);
        writeLE<uint8_t>(out, 0);
        writeLE<uint8_t>(out, 0);
        writeLE<uint8_t>(out, 0);
        writeLE<uint8_t>(out, 0);
        writeLE<uint8_t>(out, 0);
        writeLE<uint8_t>(out, 0);
        writeLE<uint8_t>(out, 0);
        writeInputsSectionRelative(out, attempt.inputs);
        writeAnchorsSection(out, attempt.anchors);
    }
}

static std::vector<Attempt> readPersistenceSection(ReadContext& ctx, double framerateHint, std::string* error) {
    std::vector<Attempt> attempts;
    uint64_t count = readLE<uint64_t>(ctx);
    if (ctx.failed || !checkCountFits(ctx, count, 32, error, "persistence attempt")) return attempts;
    attempts.reserve(static_cast<size_t>(count));

    for (uint64_t index = 0; index < count && !ctx.failed; ++index) {
        Attempt attempt;
        attempt.deathTimeSeconds = readLE<double>(ctx);
        attempt.deathPlayer2 = readLE<uint8_t>(ctx) != 0;
        for (int i = 0; i < 7; ++i) {
            if (readLE<uint8_t>(ctx) != 0) {
                ctx.failed = true;
                if (error) *error = "invalid TTR3 persistence header";
                break;
            }
        }
        if (ctx.failed) break;
        if (!validTime(attempt.deathTimeSeconds)) {
            ctx.failed = true;
            if (error) *error = "invalid TTR3 persistence death time";
            break;
        }
        attempt.inputs = readInputsSectionRelative(ctx, framerateHint, error);
        if (ctx.failed) break;
        attempt.anchors = readAnchorsSection(ctx, error);
        if (ctx.failed) break;
        if (attempt.deathTimeSeconds == 0.0 && attempt.inputs.empty() && attempt.anchors.empty()) {
            ctx.failed = true;
            if (error) *error = "empty TTR3 persistence attempt";
            break;
        }
        attempts.push_back(std::move(attempt));
    }

    return attempts;
}

template <typename T>
static T readLE(ReadContext& ctx) {
    static_assert(std::is_trivially_copyable_v<T>);
    T value {};
    if (ctx.failed || ctx.remaining() < sizeof(T)) {
        ctx.failed = true;
        return value;
    }

    std::memcpy(&value, ctx.data.data() + ctx.position, sizeof(T));
    ctx.position += sizeof(T);
    return value;
}

static std::string readString(ReadContext& ctx) {
    uint16_t length = readLE<uint16_t>(ctx);
    if (ctx.failed || length > kMaxStringSize || ctx.remaining() < length) {
        ctx.failed = true;
        return {};
    }

    std::string value(ctx.data.begin() + ctx.position, ctx.data.begin() + ctx.position + length);
    ctx.position += length;
    return value;
}

static uint32_t buildFlags(Macro const& macro, WriteOptions const& options) {
    uint32_t flags = 0;
    if (macro.losslessVerified) flags |= FlagLosslessVerified;
    if (macro.recordedFromStartPos) flags |= FlagFromStartPos;
    if (macro.platformerMode) flags |= FlagPlatformer;
    if (macro.twoPlayerMode) flags |= FlagTwoPlayer;
    if (macro.rngLocked) flags |= FlagRngLocked;
    if (!macro.anchors.empty()) flags |= FlagHasAnchors;
    if (!macro.checkpoints.empty()) flags |= FlagHasCheckpoints;
    if (!macro.persistenceAttempts.empty()) flags |= FlagHasPersistence;
    if (!macro.tpsEvents.empty()) flags |= FlagHasTpsEvents;
    if (macro.macroConverted) flags |= FlagMacroConverted;
    if (options.compressPayload) flags |= FlagCompressed;
    return flags;
}

static uint8_t inputFlags(Input const& input) {
    uint8_t flags = 0;
    if (input.player2) flags |= 1u << 0;
    if (input.pressed) flags |= 1u << 1;
    if (input.swiftPairAnchor) flags |= 1u << 2;
    return flags;
}

static void writeInputsSection(std::vector<uint8_t>& out, std::vector<Input> const& inputs) {
    writeLE<uint64_t>(out, static_cast<uint64_t>(inputs.size()));
    for (auto const& input : inputs) {
        writeLE<double>(out, input.timeSeconds);
        writeLE<uint8_t>(out, input.button);
        writeLE<uint8_t>(out, inputFlags(input));
        writeLE<uint16_t>(out, 0);
    }
}

static void writeTpsEventsSection(std::vector<uint8_t>& out, std::vector<TpsEvent> const& events) {
    writeLE<uint64_t>(out, static_cast<uint64_t>(events.size()));
    for (auto const& event : events) {
        writeLE<double>(out, event.timeSeconds);
        writeLE<double>(out, event.tps);
    }
}

static void writeCheckpointsSection(std::vector<uint8_t>& out, std::vector<Checkpoint> const& checkpoints) {
    writeLE<uint64_t>(out, static_cast<uint64_t>(checkpoints.size()));
    for (auto const& checkpoint : checkpoints) {
        writeLE<double>(out, checkpoint.timeSeconds);
        writeLE<uint64_t>(out, checkpoint.rngState);
        writeLE<double>(out, checkpoint.priorTimeSeconds);
    }
}

struct SectionEntry {
    uint8_t kind = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

static void appendSection(
    std::vector<SectionEntry>& entries,
    std::vector<uint8_t>& payload,
    SectionKind kind,
    auto writer
) {
    SectionEntry entry;
    entry.kind = static_cast<uint8_t>(kind);
    entry.offset = static_cast<uint64_t>(payload.size());
    writer(payload);
    entry.size = static_cast<uint64_t>(payload.size() - static_cast<size_t>(entry.offset));
    entries.push_back(entry);
}

static bool checkCountFits(ReadContext& ctx, uint64_t count, size_t minRecordSize, std::string* error, char const* label) {
    if (minRecordSize == 0) {
        return true;
    }
    if (count > static_cast<uint64_t>(ctx.remaining() / minRecordSize)) {
        if (error) *error = std::string(label) + " count exceeds section size";
        ctx.failed = true;
        return false;
    }
    return true;
}

static std::vector<Input> readInputsSection(ReadContext& ctx, double framerateHint, std::string* error) {
    std::vector<Input> inputs;
    uint64_t count = readLE<uint64_t>(ctx);
    if (ctx.failed) return inputs;
    if (count > 100000000ull) {
        if (error) *error = "input count exceeds cap";
        ctx.failed = true;
        return inputs;
    }
    if (!checkCountFits(ctx, count, 12, error, "input")) return inputs;

    inputs.reserve(static_cast<size_t>(count));
    for (uint64_t index = 0; index < count && !ctx.failed; ++index) {
        Input input;
        input.timeSeconds = readLE<double>(ctx);
        input.button = readLE<uint8_t>(ctx);
        uint8_t flags = readLE<uint8_t>(ctx);
        uint16_t reserved = readLE<uint16_t>(ctx);
        if (!validTime(input.timeSeconds)) {
            if (error) *error = "invalid input time";
            ctx.failed = true;
            break;
        }
        if (input.button < 1 || input.button > 3) {
            if (error) *error = "invalid input button";
            ctx.failed = true;
            break;
        }
        if ((flags & ~0x07u) != 0) {
            if (error) *error = "unknown input flag bits";
            ctx.failed = true;
            break;
        }
        if (reserved != 0) {
            if (error) *error = "nonzero input reserved bytes";
            ctx.failed = true;
            break;
        }
        if (!inputs.empty() && input.timeSeconds < inputs.back().timeSeconds) {
            if (error) *error = "unsorted inputs";
            ctx.failed = true;
            break;
        }
        input.player2 = (flags & (1u << 0)) != 0;
        input.pressed = (flags & (1u << 1)) != 0;
        input.swiftPairAnchor = (flags & (1u << 2)) != 0;
        inputs.push_back(input);
    }

    double swiftPairTolerance = 1.0 / (validTps(framerateHint) ? framerateHint : 240.0);
    for (size_t i = 0; i < inputs.size() && !ctx.failed; ++i) {
        auto const& input = inputs[i];
        if (!input.swiftPairAnchor) {
            continue;
        }
        if (i + 1 >= inputs.size()) {
            if (error) *error = "malformed swift pair";
            ctx.failed = true;
            break;
        }
        auto& next = inputs[i + 1];
        bool matchingPair = input.button == next.button &&
            input.player2 == next.player2 &&
            input.pressed != next.pressed &&
            std::abs(input.timeSeconds - next.timeSeconds) < swiftPairTolerance;
        if (!matchingPair) {
            if (error) *error = "malformed swift pair";
            ctx.failed = true;
            break;
        }
        next.timeSeconds = input.timeSeconds;
    }

    return inputs;
}

static std::vector<TpsEvent> readTpsEventsSection(ReadContext& ctx, std::string* error) {
    std::vector<TpsEvent> events;
    uint64_t count = readLE<uint64_t>(ctx);
    if (ctx.failed || !checkCountFits(ctx, count, 16, error, "TPS event")) return events;
    events.reserve(static_cast<size_t>(count));

    for (uint64_t index = 0; index < count && !ctx.failed; ++index) {
        TpsEvent event;
        event.timeSeconds = readLE<double>(ctx);
        event.tps = readLE<double>(ctx);
        if (!validTime(event.timeSeconds) || !validTps(event.tps)) {
            if (error) *error = "invalid TPS event";
            ctx.failed = true;
            break;
        }
        if (!events.empty() && event.timeSeconds < events.back().timeSeconds) {
            if (error) *error = "unsorted TPS events";
            ctx.failed = true;
            break;
        }
        events.push_back(event);
    }
    return events;
}

static std::vector<Checkpoint> readCheckpointsSection(ReadContext& ctx, std::string* error) {
    std::vector<Checkpoint> checkpoints;
    uint64_t count = readLE<uint64_t>(ctx);
    if (ctx.failed || !checkCountFits(ctx, count, 24, error, "checkpoint")) return checkpoints;
    checkpoints.reserve(static_cast<size_t>(count));

    for (uint64_t index = 0; index < count && !ctx.failed; ++index) {
        Checkpoint checkpoint;
        checkpoint.timeSeconds = readLE<double>(ctx);
        checkpoint.rngState = readLE<uint64_t>(ctx);
        checkpoint.priorTimeSeconds = readLE<double>(ctx);
        if (!validTime(checkpoint.timeSeconds) || !validTime(checkpoint.priorTimeSeconds)) {
            if (error) *error = "invalid checkpoint time";
            ctx.failed = true;
            break;
        }
        if (!checkpoints.empty() && checkpoint.timeSeconds < checkpoints.back().timeSeconds) {
            if (error) *error = "unsorted checkpoints";
            ctx.failed = true;
            break;
        }
        checkpoints.push_back(checkpoint);
    }
    return checkpoints;
}

static bool isKnownSection(uint8_t kind) {
    return kind >= static_cast<uint8_t>(SectionKind::Inputs) &&
        kind <= static_cast<uint8_t>(SectionKind::Persistence);
}

static bool sectionFlagPresent(uint32_t flags, uint8_t kind) {
    switch (static_cast<SectionKind>(kind)) {
        case SectionKind::Inputs: return true;
        case SectionKind::TpsEvents: return (flags & FlagHasTpsEvents) != 0;
        case SectionKind::Anchors: return (flags & FlagHasAnchors) != 0;
        case SectionKind::Checkpoints: return (flags & FlagHasCheckpoints) != 0;
        case SectionKind::Persistence: return (flags & FlagHasPersistence) != 0;
    }
    return false;
}

static std::optional<SectionEntry> findSection(std::vector<SectionEntry> const& entries, SectionKind kind) {
    auto wanted = static_cast<uint8_t>(kind);
    auto it = std::find_if(entries.begin(), entries.end(), [wanted](SectionEntry const& entry) {
        return entry.kind == wanted;
    });
    if (it == entries.end()) {
        return std::nullopt;
    }
    return *it;
}

}

std::vector<uint8_t> serialize(Macro const& macro, WriteOptions options) {
    std::vector<uint8_t> output;
    output.reserve(256 + macro.inputs.size() * 12);

    output.push_back('T');
    output.push_back('T');
    output.push_back('R');
    output.push_back('3');
    writeLE<uint16_t>(output, kWireVersion);
    writeLE<uint16_t>(output, 0);
    uint32_t flags = buildFlags(macro, options);
    writeLE<uint32_t>(output, flags);
    size_t headerLenPosition = output.size();
    writeLE<uint32_t>(output, 0);

    writeLE<uint64_t>(output, macro.sourceFormatId);
    writeLE<uint64_t>(output, macro.gameVersion);
    writeLE<int32_t>(output, macro.levelId);
    writeString(output, macro.levelName);
    writeString(output, macro.author);
    writeLE<double>(output, macro.framerateHint);
    writeLE<float>(output, macro.startPosX);
    writeLE<float>(output, macro.startPosY);
    writeLE<int64_t>(output, macro.recordTimestamp);
    writeLE<uint32_t>(output, macro.rngSeed);
    writeLE<uint8_t>(output, static_cast<uint8_t>(sanitizeAccuracyMode(static_cast<int>(macro.accuracyMode))));

    if (output.size() > std::numeric_limits<uint32_t>::max()) {
        return {};
    }
    patchU32(output, headerLenPosition, static_cast<uint32_t>(output.size()));

    std::vector<SectionEntry> entries;
    std::vector<uint8_t> payload;
    appendSection(entries, payload, SectionKind::Inputs, [&](std::vector<uint8_t>& sectionPayload) {
        writeInputsSection(sectionPayload, macro.inputs);
    });
    if (!macro.tpsEvents.empty()) {
        appendSection(entries, payload, SectionKind::TpsEvents, [&](std::vector<uint8_t>& sectionPayload) {
            writeTpsEventsSection(sectionPayload, macro.tpsEvents);
        });
    }
    if (!macro.anchors.empty()) {
        appendSection(entries, payload, SectionKind::Anchors, [&](std::vector<uint8_t>& sectionPayload) {
            writeAnchorsSection(sectionPayload, macro.anchors);
        });
    }
    if (!macro.checkpoints.empty()) {
        appendSection(entries, payload, SectionKind::Checkpoints, [&](std::vector<uint8_t>& sectionPayload) {
            writeCheckpointsSection(sectionPayload, macro.checkpoints);
        });
    }
    if (!macro.persistenceAttempts.empty()) {
        appendSection(entries, payload, SectionKind::Persistence, [&](std::vector<uint8_t>& sectionPayload) {
            writePersistenceSection(sectionPayload, macro.persistenceAttempts);
        });
    }

    writeLE<uint16_t>(output, static_cast<uint16_t>(entries.size()));
    for (auto const& entry : entries) {
        writeLE<uint8_t>(output, entry.kind);
        writeLE<uint8_t>(output, 0);
        writeLE<uint8_t>(output, 0);
        writeLE<uint8_t>(output, 0);
        writeLE<uint64_t>(output, entry.offset);
        writeLE<uint64_t>(output, entry.size);
    }
    if (options.compressPayload) {
        std::vector<uint8_t> compressed;
        if (!zlibCompress(payload, compressed)) {
            return {};
        }
        output.insert(output.end(), compressed.begin(), compressed.end());
    } else {
        output.insert(output.end(), payload.begin(), payload.end());
    }
    return output;
}

std::optional<Macro> deserialize(std::vector<uint8_t> const& data, std::string* error) {
    auto fail = [&](std::string message) -> std::optional<Macro> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };

    if (data.size() < kHeaderSize) {
        return fail("TTR3 magic/header is missing");
    }
    if (std::memcmp(data.data(), "TTR3", 4) != 0) {
        return fail("invalid TTR3 magic");
    }

    ReadContext ctx { data, 4 };
    uint16_t version = readLE<uint16_t>(ctx);
    uint16_t reserved = readLE<uint16_t>(ctx);
    uint32_t flags = readLE<uint32_t>(ctx);
    uint32_t headerLen = readLE<uint32_t>(ctx);
    if (ctx.failed) return fail("truncated TTR3 header");
    if (version == 0 || version > kWireVersion) return fail("unsupported TTR3 version");
    if (reserved != 0) return fail("nonzero TTR3 reserved header field");
    if ((flags & ~kKnownFlags) != 0) return fail("unknown TTR3 flag bits");
    if (headerLen < kHeaderSize || headerLen > data.size()) return fail("invalid TTR3 header length");
    Macro macro;
    macro.losslessVerified = (flags & FlagLosslessVerified) != 0;
    macro.recordedFromStartPos = (flags & FlagFromStartPos) != 0;
    macro.platformerMode = (flags & FlagPlatformer) != 0;
    macro.twoPlayerMode = (flags & FlagTwoPlayer) != 0;
    macro.rngLocked = (flags & FlagRngLocked) != 0;
    macro.macroConverted = (flags & FlagMacroConverted) != 0;

    ReadContext metadataCtx { data, kHeaderSize, headerLen };
    macro.sourceFormatId = readLE<uint64_t>(metadataCtx);
    macro.gameVersion = readLE<uint64_t>(metadataCtx);
    macro.levelId = readLE<int32_t>(metadataCtx);
    macro.levelName = readString(metadataCtx);
    macro.author = readString(metadataCtx);
    macro.framerateHint = readLE<double>(metadataCtx);
    macro.startPosX = readLE<float>(metadataCtx);
    macro.startPosY = readLE<float>(metadataCtx);
    macro.recordTimestamp = readLE<int64_t>(metadataCtx);
    macro.rngSeed = readLE<uint32_t>(metadataCtx);
    if (metadataCtx.failed) return fail("invalid TTR3 metadata");
    if (!validTps(macro.framerateHint)) return fail("invalid TTR3 framerate hint");
    if (!std::isfinite(macro.startPosX) || !std::isfinite(macro.startPosY)) {
        return fail("invalid TTR3 metadata");
    }
    if (metadataCtx.remaining() >= 1) {
        uint8_t modeByte = readLE<uint8_t>(metadataCtx);
        if (metadataCtx.failed) return fail("invalid TTR3 metadata");
        macro.accuracyMode = sanitizeAccuracyMode(static_cast<int>(modeByte));
    } else {
        macro.accuracyMode = AccuracyMode::Vanilla;
    }

    ReadContext tableCtx { data, headerLen };
    uint16_t sectionCount = readLE<uint16_t>(tableCtx);
    if (tableCtx.failed) return fail("truncated TTR3 section table");
    std::vector<SectionEntry> entries;
    entries.reserve(sectionCount);
    bool seenKnown[6] = {false, false, false, false, false, false};
    for (uint16_t index = 0; index < sectionCount && !tableCtx.failed; ++index) {
        SectionEntry entry;
        entry.kind = readLE<uint8_t>(tableCtx);
        uint8_t r0 = readLE<uint8_t>(tableCtx);
        uint8_t r1 = readLE<uint8_t>(tableCtx);
        uint8_t r2 = readLE<uint8_t>(tableCtx);
        entry.offset = readLE<uint64_t>(tableCtx);
        entry.size = readLE<uint64_t>(tableCtx);
        if (r0 != 0 || r1 != 0 || r2 != 0) return fail("nonzero TTR3 section reserved bytes");
        if (isKnownSection(entry.kind)) {
            if (seenKnown[entry.kind]) return fail("duplicate TTR3 section");
            seenKnown[entry.kind] = true;
            if (!sectionFlagPresent(flags, entry.kind)) return fail("TTR3 section present without matching flag");
        }
        entries.push_back(entry);
    }
    if (tableCtx.failed) return fail("truncated TTR3 section table");
    if (!seenKnown[static_cast<uint8_t>(SectionKind::Inputs)]) return fail("missing TTR3 inputs section");
    if ((flags & FlagHasTpsEvents) != 0 && !seenKnown[static_cast<uint8_t>(SectionKind::TpsEvents)]) {
        return fail("missing flagged TTR3 TPS events section");
    }
    if ((flags & FlagHasAnchors) != 0 && !seenKnown[static_cast<uint8_t>(SectionKind::Anchors)]) {
        return fail("missing flagged TTR3 anchors section");
    }
    if ((flags & FlagHasCheckpoints) != 0 && !seenKnown[static_cast<uint8_t>(SectionKind::Checkpoints)]) {
        return fail("missing flagged TTR3 checkpoints section");
    }
    if ((flags & FlagHasPersistence) != 0 && !seenKnown[static_cast<uint8_t>(SectionKind::Persistence)]) {
        return fail("missing flagged TTR3 persistence section");
    }

    size_t payloadStart = tableCtx.position;
    size_t payloadSize = data.size() - payloadStart;
    std::vector<uint8_t> decodedPayload;
    std::vector<uint8_t> const* payloadBytes = &data;
    size_t payloadBase = payloadStart;
    if ((flags & FlagCompressed) != 0) {
        uint64_t expectedSize = 0;
        for (auto const& entry : entries) {
            expectedSize = std::max(expectedSize, entry.offset + entry.size);
        }
        if (expectedSize == 0 || expectedSize > kMaxPayloadSize) {
            return fail("invalid compressed TTR3 payload size");
        }
        std::vector<uint8_t> compressed(data.begin() + payloadStart, data.end());
        if (!zlibDecompress(compressed, decodedPayload, static_cast<size_t>(expectedSize))) {
            return fail("failed to decompress TTR3 payload");
        }
        payloadBytes = &decodedPayload;
        payloadBase = 0;
        payloadSize = decodedPayload.size();
    }
    uint64_t previousEnd = 0;
    for (auto const& entry : entries) {
        if (entry.offset < previousEnd) return fail("overlapping TTR3 section ranges");
        if (entry.size > std::numeric_limits<uint64_t>::max() - entry.offset) return fail("invalid TTR3 section range");
        uint64_t entryEnd = entry.offset + entry.size;
        if (entryEnd > static_cast<uint64_t>(payloadSize)) return fail("TTR3 section range outside payload");
        previousEnd = entryEnd;
    }

    auto readSection = [&](SectionEntry const& entry) {
        auto start = payloadBase + static_cast<size_t>(entry.offset);
        auto end = start + static_cast<size_t>(entry.size);
        return ReadContext { *payloadBytes, start, end };
    };

    if (auto entry = findSection(entries, SectionKind::Inputs)) {
        auto sectionCtx = readSection(*entry);
        macro.inputs = readInputsSection(sectionCtx, macro.framerateHint, error);
        if (sectionCtx.failed || sectionCtx.position != sectionCtx.end) {
            return fail(error && !error->empty() ? *error : "invalid TTR3 inputs section");
        }
    }
    if (auto entry = findSection(entries, SectionKind::TpsEvents)) {
        auto sectionCtx = readSection(*entry);
        macro.tpsEvents = readTpsEventsSection(sectionCtx, error);
        if (sectionCtx.failed || sectionCtx.position != sectionCtx.end) {
            return fail(error && !error->empty() ? *error : "invalid TTR3 TPS events section");
        }
    }
    if (auto entry = findSection(entries, SectionKind::Checkpoints)) {
        auto sectionCtx = readSection(*entry);
        macro.checkpoints = readCheckpointsSection(sectionCtx, error);
        if (sectionCtx.failed || sectionCtx.position != sectionCtx.end) {
            return fail(error && !error->empty() ? *error : "invalid TTR3 checkpoints section");
        }
    }
    if (findSection(entries, SectionKind::Anchors)) {
        auto sectionCtx = readSection(*findSection(entries, SectionKind::Anchors));
        macro.anchors = readAnchorsSection(sectionCtx, error);
        if (sectionCtx.failed || sectionCtx.position != sectionCtx.end) {
            return fail(error && !error->empty() ? *error : "invalid TTR3 anchors section");
        }
    }
    if (findSection(entries, SectionKind::Persistence)) {
        auto sectionCtx = readSection(*findSection(entries, SectionKind::Persistence));
        macro.persistenceAttempts = readPersistenceSection(sectionCtx, macro.framerateHint, error);
        if (sectionCtx.failed || sectionCtx.position != sectionCtx.end) {
            return fail(error && !error->empty() ? *error : "invalid TTR3 persistence section");
        }
    }

    if (error) error->clear();
    return macro;
}

double maxSourceTps(Macro const& macro) {
    double result = validTps(macro.framerateHint) ? macro.framerateHint : 240.0;
    for (auto const& event : macro.tpsEvents) {
        if (validTps(event.tps)) {
            result = std::max(result, event.tps);
        }
    }
    return result;
}

namespace {
    static double bridgeTps(double value) {
        return validTps(value) ? value : 240.0;
    }

    static int32_t materializeTickFromTime(double timeSeconds, double tps) {
        if (!validTime(timeSeconds)) {
            return 0;
        }
        double rawTick = std::floor(timeSeconds * bridgeTps(tps) + 0.000001);
        return static_cast<int32_t>(std::clamp<double>(
            rawTick,
            0.0,
            static_cast<double>(std::numeric_limits<int32_t>::max())
        ));
    }

    static double timeFromTick(int32_t tick, double tps) {
        return static_cast<double>(std::max<int32_t>(0, tick)) / bridgeTps(tps);
    }

    static double timeForInput(TTRInput const& input, double tps) {
        if (input.hasAbsoluteTime()) {
            return input.timeSeconds;
        }
        double result = timeFromTick(input.tick, tps);
        if (std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0) {
            result += input.cbsTimeOffset;
        }
        return result;
    }

    static double timeForAnchor(PlaybackAnchor const& anchor, double tps) {
        return anchor.hasAbsoluteTime() ? anchor.timeSeconds : timeFromTick(anchor.tick, tps);
    }

    static Input fromTTRInput(TTRInput const& input, double tps) {
        Input result;
        result.timeSeconds = timeForInput(input, tps);
        result.button = static_cast<uint8_t>(std::clamp<int>(input.actionType, 1, 3));
        result.player2 = input.isPlayer2();
        result.pressed = input.isPressed();
        result.swiftPairAnchor = input.swiftPairAnchor;
        return result;
    }

    static TTRInput toTTRInput(Input const& input, double tps) {
        TTRInput result;
        result.tick = materializeTickFromTime(input.timeSeconds, tps);
        result.actionType = static_cast<uint8_t>(std::clamp<int>(input.button, 1, 3));
        result.setPlayer2(input.player2);
        result.setPressed(input.pressed);
        result.timeSeconds = input.timeSeconds;
        result.swiftPairAnchor = input.swiftPairAnchor;

        double safeTps = bridgeTps(tps);
        double rawTick = input.timeSeconds * safeTps;
        double rounded = std::nearbyint(rawTick);
        constexpr double kSnapEpsilon = 1e-6;
        double fraction;
        if (std::abs(rawTick - rounded) < kSnapEpsilon) {
            fraction = 0.0;
        } else {
            double tickFloor = std::floor(rawTick);
            fraction = rawTick - tickFloor;
        }
        if (std::isfinite(fraction) && fraction > 0.0) {
            result.stepOffset = static_cast<float>(std::clamp(fraction, 0.0, 0.999999));
            result.cbsTimeOffset = result.stepOffset / safeTps;
        }
        return result;
    }

    static Anchor fromTTRAnchor(PlaybackAnchor const& anchor, double tps) {
        Anchor result;
        result.timeSeconds = timeForAnchor(anchor, tps);
        result.state = anchor;
        return result;
    }

    static PlaybackAnchor toTTRAnchor(Anchor const& anchor, double tps) {
        PlaybackAnchor result = anchor.state;
        result.timeSeconds = anchor.timeSeconds;
        result.tick = materializeTickFromTime(anchor.timeSeconds, tps);
        return result;
    }

    static Checkpoint fromTTRCheckpoint(TTRCheckpoint const& checkpoint, double tps) {
        Checkpoint result;
        result.timeSeconds = checkpoint.hasAbsoluteTime()
            ? checkpoint.timeSeconds
            : timeFromTick(checkpoint.tick, tps);
        result.rngState = checkpoint.rngState;
        result.priorTimeSeconds = checkpoint.hasAbsolutePriorTime()
            ? checkpoint.priorTimeSeconds
            : timeFromTick(checkpoint.priorTick, tps);
        return result;
    }

    static TTRCheckpoint toTTRCheckpoint(Checkpoint const& checkpoint, double tps) {
        TTRCheckpoint result;
        result.tick = materializeTickFromTime(checkpoint.timeSeconds, tps);
        result.rngState = checkpoint.rngState;
        result.priorTick = materializeTickFromTime(checkpoint.priorTimeSeconds, tps);
        result.timeSeconds = checkpoint.timeSeconds;
        result.priorTimeSeconds = checkpoint.priorTimeSeconds;
        return result;
    }

    static Attempt fromTTRAttempt(TTRAttemptSegment const& attempt, double tps) {
        Attempt result;
        result.deathTimeSeconds = attempt.hasAbsoluteDeathTime()
            ? attempt.deathTimeSeconds
            : timeFromTick(attempt.deathTick, tps);
        result.deathPlayer2 = attempt.deathPlayer2;
        result.inputs.reserve(attempt.inputs.size());
        for (auto const& input : attempt.inputs) {
            result.inputs.push_back(fromTTRInput(input, tps));
        }
        result.anchors.reserve(attempt.anchors.size());
        for (auto const& anchor : attempt.anchors) {
            result.anchors.push_back(fromTTRAnchor(anchor, tps));
        }
        return result;
    }

    static TTRAttemptSegment toTTRAttempt(Attempt const& attempt, double tps) {
        TTRAttemptSegment result;
        result.deathTimeSeconds = attempt.deathTimeSeconds;
        result.deathTick = materializeTickFromTime(attempt.deathTimeSeconds, tps);
        result.deathPlayer2 = attempt.deathPlayer2;
        result.inputs.reserve(attempt.inputs.size());
        for (auto const& input : attempt.inputs) {
            result.inputs.push_back(toTTRInput(input, tps));
        }
        result.anchors.reserve(attempt.anchors.size());
        for (auto const& anchor : attempt.anchors) {
            result.anchors.push_back(toTTRAnchor(anchor, tps));
        }
        return result;
    }

    static bool hasExactTimedOffsets(std::vector<TTRInput> const& inputs) {
        return std::any_of(inputs.begin(), inputs.end(), [](TTRInput const& input) {
            return std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0;
        });
    }
}

Macro fromTTRMacro(TTRMacro const& macro) {
    Macro result;
    double tps = bridgeTps(macro.framerate);
    result.sourceFormatId = macro.sourceFormatId;
    result.gameVersion = macro.gameVersion;
    result.levelId = macro.levelId;
    result.name = macro.name;
    result.levelName = macro.levelName;
    result.author = macro.author;
    result.framerateHint = tps;
    result.duration = macro.duration;
    result.startPosX = macro.startPosX;
    result.startPosY = macro.startPosY;
    result.recordTimestamp = macro.recordTimestamp;
    result.rngSeed = macro.rngSeed;
    result.losslessVerified = macro.losslessVerified;
    result.recordedFromStartPos = macro.recordedFromStartPos;
    result.platformerMode = macro.platformerMode;
    result.twoPlayerMode = macro.twoPlayerMode;
    result.rngLocked = macro.rngLocked;
    result.macroConverted = macro.macroConverted;
    result.accuracyMode = sanitizeAccuracyMode(static_cast<int>(writableAccuracyMode(macro.accuracyMode)));

    result.inputs.reserve(macro.inputs.size());
    for (auto const& input : macro.inputs) {
        result.inputs.push_back(fromTTRInput(input, tps));
    }
    result.tpsEvents.reserve(macro.tpsEvents.size());
    for (auto const& event : macro.tpsEvents) {
        result.tpsEvents.push_back({event.timeSeconds, event.tps});
    }
    result.anchors.reserve(macro.anchors.size());
    for (auto const& anchor : macro.anchors) {
        result.anchors.push_back(fromTTRAnchor(anchor, tps));
    }
    result.checkpoints.reserve(macro.checkpoints.size());
    for (auto const& checkpoint : macro.checkpoints) {
        result.checkpoints.push_back(fromTTRCheckpoint(checkpoint, tps));
    }
    result.persistenceAttempts.reserve(macro.persistenceAttempts.size());
    for (auto const& attempt : macro.persistenceAttempts) {
        result.persistenceAttempts.push_back(fromTTRAttempt(attempt, tps));
    }
    return result;
}

TTRMacro toTTRMacro(Macro const& macro) {
    TTRMacro result;
    double tps = bridgeTps(macro.framerateHint);
    result.fileFormat = TTRFileFormat::TTR3;
    result.sourceFormatId = macro.sourceFormatId;
    result.author = macro.author;
    result.levelName = macro.levelName;
    result.levelId = macro.levelId;
    result.framerate = tps;
    result.duration = macro.duration;
    result.gameVersion = static_cast<uint32_t>(std::min<uint64_t>(
        macro.gameVersion,
        std::numeric_limits<uint32_t>::max()
    ));
    result.startPosX = macro.startPosX;
    result.startPosY = macro.startPosY;
    result.recordTimestamp = macro.recordTimestamp;
    result.rngSeed = macro.rngSeed;
    result.losslessVerified = macro.losslessVerified;
    result.recordedFromStartPos = macro.recordedFromStartPos;
    result.platformerMode = macro.platformerMode;
    result.twoPlayerMode = macro.twoPlayerMode;
    result.rngLocked = macro.rngLocked;
    result.macroConverted = macro.macroConverted;
    result.bestEffort = !macro.losslessVerified;

    result.inputs.reserve(macro.inputs.size());
    for (auto const& input : macro.inputs) {
        result.inputs.push_back(toTTRInput(input, tps));
    }
    result.tpsEvents.reserve(macro.tpsEvents.size());
    for (auto const& event : macro.tpsEvents) {
        result.tpsEvents.push_back({event.timeSeconds, event.tps});
    }
    result.anchors.reserve(macro.anchors.size());
    for (auto const& anchor : macro.anchors) {
        result.anchors.push_back(toTTRAnchor(anchor, tps));
    }
    result.checkpoints.reserve(macro.checkpoints.size());
    for (auto const& checkpoint : macro.checkpoints) {
        result.checkpoints.push_back(toTTRCheckpoint(checkpoint, tps));
    }
    result.persistenceAttempts.reserve(macro.persistenceAttempts.size());
    for (auto const& attempt : macro.persistenceAttempts) {
        result.persistenceAttempts.push_back(toTTRAttempt(attempt, tps));
    }

    bool exactTiming = hasExactTimedOffsets(result.inputs);
    if (!exactTiming) {
        exactTiming = std::any_of(result.persistenceAttempts.begin(), result.persistenceAttempts.end(), [](TTRAttemptSegment const& attempt) {
            return hasExactTimedOffsets(attempt.inputs);
        });
    }
    result.exactCbsTiming = exactTiming;
    result.accuracyMode = sanitizeAccuracyMode(static_cast<int>(macro.accuracyMode));
    return result;
}

}
