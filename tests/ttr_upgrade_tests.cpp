#include "conversion/ttr_upgrade.hpp"
#include "format/ttr3_format.hpp"
#include "format/ttr_format.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

static bool nearlyEqual(double lhs, double rhs, double epsilon = 1e-12) {
    return std::abs(lhs - rhs) <= epsilon;
}

static uint32_t readU32(std::vector<uint8_t> const& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static void writeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8)) & 0xff);
    }
}

static void writeDouble(std::vector<uint8_t>& bytes, size_t offset, double value) {
    uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    for (size_t index = 0; index < sizeof(raw); ++index) {
        bytes[offset + index] = static_cast<uint8_t>((raw >> (index * 8)) & 0xff);
    }
}

static TTRMacro makeTTR2Fixture() {
    TTRMacro source;
    source.fileFormat = TTRFileFormat::TTR2;
    source.author = "Toast";
    source.name = "source";
    source.levelName = "Dash";
    source.levelId = 22;
    source.framerate = 360.0;
    source.duration = 2.75;
    source.gameVersion = 22081;
    source.startPosX = 12.5f;
    source.startPosY = -3.25f;
    source.recordedFromStartPos = true;
    source.accuracyMode = AccuracyMode::CBS;
    source.platformerMode = true;
    source.twoPlayerMode = true;
    source.rngLocked = true;
    source.exactCbsTiming = true;
    source.rngSeed = 1337;
    source.recordTimestamp = 123456789;

    TTRInput press;
    press.tick = 30;
    press.actionType = 1;
    press.setPressed(true);
    press.cbsTimeOffset = 0.00125;
    press.stepOffset = static_cast<float>(press.cbsTimeOffset * source.framerate);
    source.inputs.push_back(press);

    TTRInput release;
    release.tick = 180;
    release.actionType = 2;
    release.setPlayer2(true);
    release.setPressed(false);
    release.cbsTimeOffset = 0.0005;
    release.stepOffset = static_cast<float>(release.cbsTimeOffset * source.framerate);
    source.inputs.push_back(release);

    PlaybackAnchor anchor;
    anchor.tick = 180;
    anchor.hasPlayer2 = true;
    anchor.player1LatchMask = 0x03;
    anchor.player2LatchMask = 0x05;
    anchor.rng.locked = true;
    anchor.rng.seed = source.rngSeed;
    anchor.rng.fastRandState = 998877;
    anchor.player1.motion.position = cocos2d::CCPoint(100.5f, 200.25f);
    anchor.player2.motion.position = cocos2d::CCPoint(101.5f, 201.25f);
    anchor.player1.motion.verticalVelocity = 4.5;
    anchor.player2.motion.verticalVelocity = -3.25;
    source.anchors.push_back(anchor);

    TTRCheckpoint checkpoint;
    checkpoint.tick = 360;
    checkpoint.priorTick = 180;
    checkpoint.rngState = 445566;
    source.checkpoints.push_back(checkpoint);

    TTRAttemptSegment attempt;
    attempt.deathTick = 540;
    attempt.deathPlayer2 = true;
    TTRInput attemptInput = press;
    attemptInput.tick = 20;
    attempt.inputs.push_back(attemptInput);
    PlaybackAnchor attemptAnchor = anchor;
    attemptAnchor.tick = 40;
    attempt.anchors.push_back(attemptAnchor);
    source.persistenceAttempts.push_back(attempt);
    return source;
}

static void test_ttr2_upgrade_preserves_every_section() {
    auto source = makeTTR2Fixture();
    auto result = toasty::ttr_upgrade::convertLegacyTTRToTTR3(
        source,
        "source_ttr3",
        "Replacement",
        999
    );
    assert(result);

    auto payload = std::move(result).unwrap();
    assert(payload.fidelity == toasty::conversion::ReplayImportFidelity::Verified);
    assert(payload.inputCount == source.inputs.size());
    assert(nearlyEqual(payload.tps, source.framerate));

    std::string error;
    auto converted = toasty::ttr3::deserialize(payload.bytes, &error);
    assert(converted.has_value());
    assert(error.empty());
    assert(converted->sourceFormatId == toasty::ttr_upgrade::kTTR2SourceFormatId);
    assert(converted->author == source.author);
    assert(converted->levelName == source.levelName);
    assert(converted->levelId == source.levelId);
    assert(converted->gameVersion == source.gameVersion);
    assert(nearlyEqual(converted->framerateHint, source.framerate));
    assert(nearlyEqual(converted->duration, source.duration));
    assert(nearlyEqual(converted->startPosX, source.startPosX));
    assert(nearlyEqual(converted->startPosY, source.startPosY));
    assert(converted->recordTimestamp == source.recordTimestamp);
    assert(converted->rngSeed == source.rngSeed);
    assert(converted->losslessVerified);
    assert(converted->macroConverted);
    assert(converted->recordedFromStartPos);
    assert(converted->platformerMode);
    assert(converted->twoPlayerMode);
    assert(converted->rngLocked);
    assert(converted->accuracyMode == AccuracyMode::CBS);

    assert(converted->inputs.size() == 2);
    assert(nearlyEqual(converted->inputs[0].timeSeconds, 30.0 / 360.0 + 0.00125));
    assert(converted->inputs[0].button == 1);
    assert(converted->inputs[0].pressed);
    assert(!converted->inputs[0].player2);
    assert(nearlyEqual(converted->inputs[1].timeSeconds, 180.0 / 360.0 + 0.0005));
    assert(converted->inputs[1].button == 2);
    assert(converted->inputs[1].player2);
    assert(!converted->inputs[1].pressed);

    assert(converted->anchors.size() == 1);
    assert(nearlyEqual(converted->anchors[0].timeSeconds, 0.5));
    assert(converted->anchors[0].state.hasPlayer2);
    assert(converted->anchors[0].state.player1LatchMask == 0x03);
    assert(converted->anchors[0].state.player2LatchMask == 0x05);
    assert(converted->anchors[0].state.rng.fastRandState == 998877);
    assert(nearlyEqual(converted->anchors[0].state.player1.motion.position.x, 100.5));
    assert(nearlyEqual(converted->anchors[0].state.player2.motion.position.y, 201.25));

    assert(converted->checkpoints.size() == 1);
    assert(nearlyEqual(converted->checkpoints[0].timeSeconds, 1.0));
    assert(nearlyEqual(converted->checkpoints[0].priorTimeSeconds, 0.5));
    assert(converted->checkpoints[0].rngState == 445566);

    assert(converted->persistenceAttempts.size() == 1);
    assert(nearlyEqual(converted->persistenceAttempts[0].deathTimeSeconds, 1.5));
    assert(converted->persistenceAttempts[0].deathPlayer2);
    assert(converted->persistenceAttempts[0].inputs.size() == 1);
    assert(converted->persistenceAttempts[0].anchors.size() == 1);
}

static void test_legacy_source_provenance() {
    auto source = makeTTR2Fixture();
    source.fileFormat = TTRFileFormat::LegacyTTR;
    source.author.clear();
    source.recordTimestamp = 0;
    source.inputs[0].cbsTimeOffset = -1.0;
    source.inputs[0].stepOffset = 0.5f;

    auto result = toasty::ttr_upgrade::convertLegacyTTRToTTR3(
        source,
        "legacy_ttr3",
        "Toast",
        999
    );
    assert(result);

    std::string error;
    auto converted = toasty::ttr3::deserialize(result.unwrap().bytes, &error);
    assert(converted.has_value());
    assert(converted->sourceFormatId == toasty::ttr_upgrade::kLegacyTTRSourceFormatId);
    assert(converted->author == "Toast");
    assert(converted->recordTimestamp == 999);
    assert(nearlyEqual(converted->inputs[0].timeSeconds, 30.5 / source.framerate));
}

static void test_reads_existing_ttr3_without_duration() {
    auto source = makeTTR2Fixture();
    auto result = toasty::ttr_upgrade::convertLegacyTTRToTTR3(source, "source_ttr3", "Toast", 999);
    assert(result);
    auto bytes = result.unwrap().bytes;
    uint32_t headerLength = readU32(bytes, 12);
    assert(headerLength >= sizeof(double));
    bytes.erase(
        bytes.begin() + static_cast<std::ptrdiff_t>(headerLength - sizeof(double)),
        bytes.begin() + static_cast<std::ptrdiff_t>(headerLength)
    );
    writeU32(bytes, 12, headerLength - sizeof(double));

    std::string error;
    auto converted = toasty::ttr3::deserialize(bytes, &error);
    assert(converted.has_value());
    assert(error.empty());
    assert(converted->duration == 0.0);
}

static void test_rejects_invalid_duration() {
    auto source = makeTTR2Fixture();
    auto result = toasty::ttr_upgrade::convertLegacyTTRToTTR3(source, "source_ttr3", "Toast", 999);
    assert(result);
    auto bytes = result.unwrap().bytes;
    uint32_t headerLength = readU32(bytes, 12);
    writeDouble(bytes, headerLength - sizeof(double), std::numeric_limits<double>::infinity());

    std::string error;
    auto converted = toasty::ttr3::deserialize(bytes, &error);
    assert(!converted.has_value());
    assert(error.find("duration") != std::string::npos);
}

static void test_rejects_inexact_sources() {
    auto wrongFormat = makeTTR2Fixture();
    wrongFormat.fileFormat = TTRFileFormat::TTR3;
    assert(!toasty::ttr_upgrade::convertLegacyTTRToTTR3(wrongFormat, "bad", "Toast", 1));

    auto unordered = makeTTR2Fixture();
    unordered.inputs[1].tick = 10;
    assert(!toasty::ttr_upgrade::convertLegacyTTRToTTR3(unordered, "bad", "Toast", 1));

    auto unknownFlags = makeTTR2Fixture();
    unknownFlags.inputs[0].flags |= 0x80;
    assert(!toasty::ttr_upgrade::convertLegacyTTRToTTR3(unknownFlags, "bad", "Toast", 1));

    auto badTiming = makeTTR2Fixture();
    badTiming.inputs[0].cbsTimeOffset = -2.0;
    assert(!toasty::ttr_upgrade::convertLegacyTTRToTTR3(badTiming, "bad", "Toast", 1));

    auto badLegacyOffset = makeTTR2Fixture();
    badLegacyOffset.fileFormat = TTRFileFormat::LegacyTTR;
    badLegacyOffset.inputs[0].cbsTimeOffset = -1.0;
    badLegacyOffset.inputs[0].stepOffset = 1.0f;
    assert(!toasty::ttr_upgrade::convertLegacyTTRToTTR3(badLegacyOffset, "bad", "Toast", 1));
}

int main() {
    test_ttr2_upgrade_preserves_every_section();
    test_legacy_source_provenance();
    test_reads_existing_ttr3_without_duration();
    test_rejects_invalid_duration();
    test_rejects_inexact_sources();
    return 0;
}
