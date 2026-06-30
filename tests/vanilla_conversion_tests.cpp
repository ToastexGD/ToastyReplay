#include "conversion/macro_converter.hpp"

#include <cassert>

using namespace toasty::conversion;

static void test_conditional_source_signatures_allow_ttr3() {
    {
        TTR3InspectionFacts facts;
        facts.hasTcbotV2VersionByte = true;
        auto eligibility = inspectTTR3Eligibility(ReplayFormat::TCBot, facts);
        assert(eligibility.lossless);
        assert(eligibility.route == TTR3Route::LosslessTTR3);
    }
    {
        TTR3InspectionFacts facts;
        facts.hasFractionalPlaintextFrames = true;
        auto eligibility = inspectTTR3Eligibility(ReplayFormat::Plaintext, facts);
        assert(eligibility.lossless);
        assert(eligibility.route == TTR3Route::LosslessTTR3);
    }
    {
        TTR3InspectionFacts facts;
        facts.ybot2SourceTpsHighEnough = true;
        auto eligibility = inspectTTR3Eligibility(ReplayFormat::YBot2, facts);
        assert(eligibility.lossless);
        assert(eligibility.route == TTR3Route::LosslessTTR3);
    }
    {
        TTR3InspectionFacts facts;
        facts.hasUvBotPhysicsAnchors = true;
        auto eligibility = inspectTTR3Eligibility(ReplayFormat::UvBot, facts);
        assert(eligibility.lossless);
        assert(eligibility.route == TTR3Route::LosslessTTR3);
    }
}

static void test_vanilla_ttr3_conversion_strips_playback_fixups_but_keeps_source_signature() {
    ImportedReplay replay;
    replay.format = ReplayFormat::Amethyst;
    replay.sourceName = "vanilla-anchor-source";
    replay.levelName = "test-level";
    replay.fps = 240.0;
    replay.duration = 0.2;
    replay.sourceLosslessVerified = true;
    replay.convertedForTTR3 = true;
    replay.inputs.push_back({ .time = 0.1, .sourceFrame = 24.0, .sequence = 0, .button = 1, .player2 = false, .pressed = true });
    replay.inputs.push_back({ .time = 0.2, .sourceFrame = 48.0, .sequence = 1, .button = 1, .player2 = false, .pressed = false });
    PlaybackAnchor anchor;
    anchor.tick = 24;
    replay.anchors.push_back(anchor);

    auto macro = buildTTR3FromImported(replay, "vanilla-anchor-source", "Toast");
    assert(macro.accuracyMode == AccuracyMode::Vanilla);
    assert(macro.macroConverted);
    assert(macro.sourceFormatId == static_cast<uint64_t>(ReplayFormat::Amethyst));
    assert(macro.losslessVerified);
    assert(macro.anchors.empty());
    assert(macro.checkpoints.empty());
    for (auto const& input : macro.inputs) {
        assert(input.stepOffset == 0.0f);
        assert(input.cbsTimeOffset < 0.0);
        assert(!input.hasAbsoluteTime());
        assert(!input.swiftPairAnchor);
    }
}

static void test_cbs_ttr3_conversion_keeps_authoritative_anchors() {
    ImportedReplay replay;
    replay.format = ReplayFormat::Amethyst;
    replay.sourceName = "timed-anchor-source";
    replay.levelName = "test-level";
    replay.fps = 240.0;
    replay.duration = 0.2;
    replay.sourceLosslessVerified = true;
    replay.convertedForTTR3 = true;
    replay.inputs.push_back({ .time = 0.1005, .sourceFrame = 24.0, .sequence = 0, .button = 1, .player2 = false, .pressed = true });
    replay.inputs.push_back({ .time = 0.2005, .sourceFrame = 48.0, .sequence = 1, .button = 1, .player2 = false, .pressed = false });
    PlaybackAnchor anchor;
    anchor.tick = 24;
    replay.anchors.push_back(anchor);

    auto macro = buildTTR3FromImported(replay, "timed-anchor-source", "Toast");
    assert(macro.accuracyMode == AccuracyMode::CBS);
    assert(macro.exactCbsTiming);
    assert(macro.macroConverted);
    assert(macro.anchors.size() == 1);
    assert(macro.inputs[0].hasAbsoluteTime());
    assert(macro.inputs[0].cbsTimeOffset >= 0.0);
}

int main() {
    test_conditional_source_signatures_allow_ttr3();
    test_vanilla_ttr3_conversion_strips_playback_fixups_but_keeps_source_signature();
    test_cbs_ttr3_conversion_keeps_authoritative_anchors();
    return 0;
}
