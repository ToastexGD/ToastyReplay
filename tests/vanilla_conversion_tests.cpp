#include "core/gameplay_layer.hpp"
#include "hacks/hitbox_overlay_model.hpp"
#include "conversion/macro_converter.hpp"
#include "audio/click_audio_math.hpp"
#include "core/replay_timing.hpp"
#include "core/start_position_policy.hpp"
#include "format/ttr3_format.hpp"

#include <cassert>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace toasty::conversion;
using namespace toasty::gameplay;

namespace {
PlayLayer* g_fakePlayLayer = nullptr;
LevelEditorLayer* g_fakeEditor = nullptr;
}

namespace toasty::gameplay::detail {
    PlayLayer* (*playLayerGetter)() = []() -> PlayLayer* { return g_fakePlayLayer; };
    LevelEditorLayer* (*editorGetter)() = []() -> LevelEditorLayer* { return g_fakeEditor; };
}

static void resetGameplayLayerState() {
    g_fakePlayLayer = nullptr;
    g_fakeEditor = nullptr;
    setEditorPlaytestActive(false);
}

static void test_click_audio_controls_change_output() {
    assert(std::abs(toasty::clickaudio::volumeGain(0.5f) - 0.5f) < 0.0001f);
    assert(std::abs(toasty::clickaudio::volumeGain(1.0f) - 1.0f) < 0.0001f);
    assert(std::abs(toasty::clickaudio::volumeGain(1.5f) - 2.5f) < 0.0001f);
    assert(std::abs(toasty::clickaudio::volumeGain(2.0f) - 4.0f) < 0.0001f);
    assert(std::abs(toasty::clickaudio::pitchFactor(0.25f) - 1.25f) < 0.0001f);
    assert(!toasty::clickaudio::shouldUseSecondaryPack(true, true, false));
    assert(toasty::clickaudio::shouldUseSecondaryPack(true, true, true));
}

static void test_zero_percent_start_position_can_play_from_start() {
    assert(toasty::start_position::isAtLevelStart(0.0f, 5000.0f));
    assert(toasty::start_position::isAtLevelStart(45.0f, 5000.0f));
    assert(!toasty::start_position::isAtLevelStart(250.0f, 5000.0f));
    assert(!toasty::start_position::shouldRecordFromStartPosition(false, 0));
    assert(!toasty::start_position::shouldRecordFromStartPosition(true, 0));
    assert(toasty::start_position::shouldRecordFromStartPosition(true, 1));
}

static void test_editor_playtest_counts_as_gameplay_active() {
    resetGameplayLayerState();
    g_fakeEditor = reinterpret_cast<LevelEditorLayer*>(0x3);
    setEditorPlaytestActive(true);
    assert(mode() == Mode::EditorPlaytest);
    assert(activeLayer() == g_fakeEditor);
    assert(isGameplayActive());
    assert(isEditorPlaytest());
    assert(!isEditorBuildMode());
}

static void test_editor_hitbox_overlay_requires_active_playtest() {
    assert(!toasty::hitbox_overlay::shouldRenderEditorOverlay(false, false));
    assert(!toasty::hitbox_overlay::shouldRenderEditorOverlay(false, true));
    assert(!toasty::hitbox_overlay::shouldRenderEditorOverlay(true, false));
    assert(toasty::hitbox_overlay::shouldRenderEditorOverlay(true, true));
}

static std::string readProjectFile(char const* relativePath) {
    static constexpr char const* prefixes[] = { "", "../", "../../", "../../../" };
    for (auto const* prefix : prefixes) {
        auto path = std::filesystem::path(prefix) / relativePath;
        if (!std::filesystem::exists(path)) {
            continue;
        }
        std::ifstream in(path, std::ios::binary);
        assert(in.good());
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    assert(!"missing project file for source regression test");
    return {};
}

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

static void test_vanilla_ttr3_swift_clicks_keep_every_edge() {
    ImportedReplay replay;
    replay.format = ReplayFormat::Silicate3;
    replay.sourceName = "swift-vanilla-source";
    replay.levelName = "test-level";
    replay.fps = 240.0;
    replay.duration = 0.5;
    replay.sourceLosslessVerified = true;

    replay.inputs.push_back({ .time = 0.1, .sourceFrame = 24.0, .sequence = 0,
        .button = 1, .player2 = false, .pressed = true,  .swift = true });
    replay.inputs.push_back({ .time = 0.1, .sourceFrame = 24.0, .sequence = 1,
        .button = 1, .player2 = false, .pressed = false, .swift = true });
    replay.inputs.push_back({ .time = 0.1, .sourceFrame = 24.0, .sequence = 2,
        .button = 1, .player2 = true, .pressed = true,  .swift = true });
    replay.inputs.push_back({ .time = 0.1, .sourceFrame = 24.0, .sequence = 3,
        .button = 1, .player2 = true, .pressed = false, .swift = true });

    finishImportForTarget(replay, ConversionTarget::TTR3);

    assert(replay.inputs.size() == 4);
    assert(replay.inputs[0].pressed);
    assert(!replay.inputs[1].pressed);
    assert(replay.inputs[2].pressed);
    assert(!replay.inputs[3].pressed);
    for (auto const& input : replay.inputs) {
        assert(input.swift);
        assert(input.tick == 24);
        assert(input.stepOffset == 0.0f);
        assert(std::abs(input.time - 0.1) < 0.000001);
    }

    auto macro = buildTTR3FromImported(replay, "swift-vanilla-source", "Toast");
    assert(macro.accuracyMode == AccuracyMode::Vanilla);
    assert(macro.inputs.size() == 4);
    assert(macro.inputs[0].isPressed());
    assert(!macro.inputs[1].isPressed());
    assert(macro.inputs[2].isPressed());
    assert(!macro.inputs[3].isPressed());
    assert(!macro.inputs[0].isPlayer2());
    assert(macro.inputs[2].isPlayer2());
    for (auto const& input : macro.inputs) {
        assert(input.actionType == 1);
        assert(input.stepOffset == 0.0f);
        assert(input.cbsTimeOffset < 0.0);
        assert(!input.hasAbsoluteTime());
        assert(!input.swiftPairAnchor);
    }
}

static void test_ttr3_same_tick_non_swift_taps_preserve_edges_with_cbs_offsets() {
    ImportedReplay replay;
    replay.format = ReplayFormat::Amethyst;
    replay.sourceName = "same-tick-tap-source";
    replay.levelName = "test-level";
    replay.fps = 240.0;
    replay.duration = 0.5;
    replay.sourceLosslessVerified = true;

    replay.inputs.push_back({ .time = 50.0 / 240.0, .sourceFrame = 50.0, .sequence = 0,
        .button = 1, .player2 = false, .pressed = true });
    replay.inputs.push_back({ .time = 50.0 / 240.0, .sourceFrame = 50.0, .sequence = 1,
        .button = 1, .player2 = false, .pressed = false });
    replay.inputs.push_back({ .time = 50.0 / 240.0, .sourceFrame = 50.0, .sequence = 2,
        .button = 1, .player2 = false, .pressed = true });

    finishImportForTarget(replay, ConversionTarget::TTR3);

    assert(replay.inputs.size() == 3);
    assert(replay.needsCbsTiming);
    assert(replay.inputs[0].pressed);
    assert(!replay.inputs[1].pressed);
    assert(replay.inputs[2].pressed);
    assert(replay.inputs[0].time < replay.inputs[1].time);
    assert(replay.inputs[1].time < replay.inputs[2].time);
    assert(replay.inputs[1].stepOffset > 0.0f);
    assert(replay.inputs[2].stepOffset > replay.inputs[1].stepOffset);

    auto macro = buildTTR3FromImported(replay, "same-tick-tap-source", "Toast");
    assert(macro.accuracyMode == AccuracyMode::CBS);
    assert(macro.inputs.size() == 3);
    assert(macro.inputs[0].isPressed());
    assert(!macro.inputs[1].isPressed());
    assert(macro.inputs[2].isPressed());
    assert(macro.inputs[1].stepOffset > 0.0f);
    assert(macro.inputs[2].stepOffset > macro.inputs[1].stepOffset);
}

static void test_single_same_tick_tap_stays_vanilla() {
    ImportedReplay replay;
    replay.format = ReplayFormat::TCBot;
    replay.fps = 360.0;
    replay.sourceLosslessVerified = true;
    replay.inputs.push_back({ .time = 100.0 / replay.fps, .sourceFrame = 100.0, .sequence = 0,
        .button = 1, .player2 = false, .pressed = true });
    replay.inputs.push_back({ .time = 100.0 / replay.fps, .sourceFrame = 100.0, .sequence = 1,
        .button = 1, .player2 = false, .pressed = false });

    finishImportForTarget(replay, ConversionTarget::TTR3);
    auto macro = buildTTR3FromImported(replay, "single-tap", "Toast");

    assert(!replay.needsCbsTiming);
    assert(macro.accuracyMode == AccuracyMode::Vanilla);
    assert(macro.inputs.size() == 2);
    assert(macro.inputs[0].tick == 100);
    assert(macro.inputs[1].tick == 100);
    assert(macro.inputs[0].isPressed());
    assert(!macro.inputs[1].isPressed());
}

static void test_integer_frames_materialize_exactly_at_arbitrary_tps() {
    for (double tps : { 30.0, 60.0, 144.0, 240.0, 360.0, 1000.0, 2026.0 }) {
        for (int frame = 0; frame <= 10000; ++frame) {
            double time = static_cast<double>(frame) / tps;
            assert(materializeTTR3Tick(time, tps) == frame);
            assert(ttr3SnappedCbsOffset(time, frame, tps) == 0.0);
        }
    }
}

static void test_ttr3_bridge_preserves_integer_ticks_at_arbitrary_tps() {
    for (double tps : { 30.0, 60.0, 144.0, 240.0, 360.0, 1000.0, 2026.0 }) {
        toasty::ttr3::Macro macro;
        macro.framerateHint = tps;
        std::array<int, 8> frames = { 1, 2, 7, 59, 143, 239, 999, 9999 };
        for (int frame : frames) {
            macro.inputs.push_back({ static_cast<double>(frame) / tps, 1, false, true, false });
        }
        auto bridged = toasty::ttr3::toTTRMacro(macro);
        assert(bridged.inputs.size() == macro.inputs.size());
        for (size_t index = 0; index < bridged.inputs.size(); ++index) {
            assert(bridged.inputs[index].tick == frames[index]);
        }
    }
}

static void test_exact_ttr3_playback_timing() {
    double target = toasty::replay_timing::targetTimestampForPlaybackInput(10.0, 20.0, 0.125, 0.003);
    assert(std::abs(target - 10.125) < 0.000001);
    assert(toasty::replay_timing::classifyExactInputDispatch(target, 10.1, 10.2) == toasty::replay_timing::ExactInputDispatch::QueueNative);
    assert(toasty::replay_timing::shouldSkipRepeatedProcessSlice(true, 24, 24, true, 2, 2, 1.0, 1.0));
    assert(!toasty::replay_timing::shouldSkipRepeatedProcessSlice(true, 24, 24, true, 2, 2, 1.0, 1.01));
}

static void test_deterministic_seed_is_preserved() {
    ImportedReplay replay;
    replay.format = ReplayFormat::TCBot;
    replay.hasDeterministicSeed = true;
    replay.deterministicSeed = 0x123456789ABCDEF0ull;

    auto macro = buildTTR3FromImported(replay, "seeded", "Toast");

    assert(macro.rngLocked);
    assert(macro.rngSeed == static_cast<uint32_t>(0x123456789ABCDEF0ull ^ (0x123456789ABCDEF0ull >> 32)));
}

static void test_tcm_rate_metadata_supports_tps_and_delta_time() {
    assert(std::abs(decodeTcmFps(1, 0, 360.0f) - 360.0) < 0.000001);
    assert(std::abs(decodeTcmFps(2, 0x02, 1000.0f) - 1000.0) < 0.000001);
    assert(std::abs(decodeTcmFps(2, 0, 0.001f) - 1000.0) < 0.001);
    assert(decodeTcmFps(2, 0, 0.0f) == 240.0);
}

static void test_respawn_override_uses_scheduler_not_dead_update_polling() {
    auto source = readProjectFile("src/hacks/safemode.cpp");
    assert(source.find("scheduleOnce(schedule_selector(GuardedPlayLayer::applyRespawnOverride)") != std::string::npos);
    assert(source.find("unschedule(schedule_selector(GuardedPlayLayer::applyRespawnOverride)") != std::string::npos);
    assert(source.find("respawnTimer") == std::string::npos);
}

static void test_module_card_animation_uses_measured_height_without_snap() {
    auto source = readProjectFile("src/gui/gui.cpp");
    assert(source.find("moduleContentHeightForProgress") != std::string::npos);
    assert(source.find("ImGuiChildFlags_None") != std::string::npos);
    assert(source.find("280.0f * t") == std::string::npos);
    assert(source.find("it->second.height = measured;") != std::string::npos);
}

static void test_dispatch_keybinds_are_visible_on_module_cards() {
    auto source = readProjectFile("src/gui/gui.cpp");
    assert(source.find("Widgets::ModuleCard(\"Autoclicker\", \"Auto-click at configurable intervals\", &ac->enabled, theme, anim, &keybinds.autoclicker)") != std::string::npos);
    assert(source.find("Widgets::ModuleCard(\"Click Sounds\", \"Play click and release sounds on input\", &csm->enabled, theme, anim, &keybinds.clickSounds)") != std::string::npos);
}

static void test_cocos_menu_warning_is_registered_for_frontend_setting() {
    auto frontend = readProjectFile("src/gui/cocos/frontend.cpp");
    auto popup = readProjectFile("src/gui/cocos/tr_menu_popup.cpp");
    assert(frontend.find("listenForSettingChanges<std::string>(\"menu_frontend\"") != std::string::npos);
    assert(frontend.find("my cocos menu and structuring sucks") != std::string::npos);
    assert(frontend.find("FLAlertLayer::create(\"Warning\"") != std::string::npos);
    assert(popup.find("toasty::frontend::setMenuFrontend(idx == 1)") != std::string::npos);
}

int main() {
    test_click_audio_controls_change_output();
    test_zero_percent_start_position_can_play_from_start();
    test_editor_playtest_counts_as_gameplay_active();
    test_editor_hitbox_overlay_requires_active_playtest();
    test_conditional_source_signatures_allow_ttr3();
    test_vanilla_ttr3_conversion_strips_playback_fixups_but_keeps_source_signature();
    test_cbs_ttr3_conversion_keeps_authoritative_anchors();
    test_vanilla_ttr3_swift_clicks_keep_every_edge();
    test_ttr3_same_tick_non_swift_taps_preserve_edges_with_cbs_offsets();
    test_single_same_tick_tap_stays_vanilla();
    test_integer_frames_materialize_exactly_at_arbitrary_tps();
    test_ttr3_bridge_preserves_integer_ticks_at_arbitrary_tps();
    test_exact_ttr3_playback_timing();
    test_deterministic_seed_is_preserved();
    test_tcm_rate_metadata_supports_tps_and_delta_time();
    test_respawn_override_uses_scheduler_not_dead_update_polling();
    test_module_card_animation_uses_measured_height_without_snap();
    test_dispatch_keybinds_are_visible_on_module_cards();
    test_cocos_menu_warning_is_registered_for_frontend_setting();
    return 0;
}
