#include "core/gameplay_layer.hpp"
#include "hacks/hitbox_overlay_model.hpp"
#include "audio/click_audio_math.hpp"
#include "core/replay_timing.hpp"
#include "core/start_position_policy.hpp"
#include "format/ttr_format.hpp"
#include "format/ttr3_format.hpp"

#include <cassert>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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

static void test_vanilla_persistence_keeps_navigation_data() {
    TTRMacro macro;
    macro.accuracyMode = AccuracyMode::Vanilla;
    macro.exactCbsTiming = true;

    TTRInput input;
    input.tick = 30;
    input.stepOffset = 0.5f;
    input.cbsTimeOffset = 0.002;
    input.timeSeconds = 0.125;
    input.swiftPairAnchor = true;
    macro.inputs.push_back(input);
    macro.anchors.emplace_back();
    macro.checkpoints.emplace_back();

    TTRAttemptSegment attempt;
    attempt.inputs.push_back(input);
    attempt.anchors.emplace_back();
    macro.persistenceAttempts.push_back(attempt);

    normalizeTTRPersistenceTiming(macro);

    assert(macro.inputs[0].stepOffset == 0.0f);
    assert(macro.inputs[0].cbsTimeOffset == -1.0);
    assert(macro.inputs[0].timeSeconds == -1.0);
    assert(!macro.inputs[0].swiftPairAnchor);
    assert(macro.anchors.size() == 1);
    assert(macro.checkpoints.size() == 1);
    assert(macro.persistenceAttempts[0].anchors.size() == 1);
    assert(!macro.exactCbsTiming);
}

static void test_exact_ttr3_playback_timing() {
    double target = toasty::replay_timing::targetTimestampForPlaybackInput(10.0, 20.0, 0.125, 0.003);
    assert(std::abs(target - 10.125) < 0.000001);
    assert(toasty::replay_timing::classifyExactInputDispatch(target, 10.1, 10.2) == toasty::replay_timing::ExactInputDispatch::QueueNative);
    assert(toasty::replay_timing::shouldSkipRepeatedProcessSlice(true, 24, 24, true, 2, 2, 1.0, 1.0));
    assert(!toasty::replay_timing::shouldSkipRepeatedProcessSlice(true, 24, 24, true, 2, 2, 1.0, 1.01));
}

static void test_playback_uses_required_macro_tps() {
    assert(toasty::replay_timing::playbackRuntimeTps(240.0, 360.0) == 360.0);
    assert(toasty::replay_timing::playbackRuntimeTps(240.0, 120.0) == 240.0);
    assert(toasty::replay_timing::playbackRuntimeTps(144.0, -1.0) == 144.0);
    assert(toasty::replay_timing::playbackRuntimeTps(-1.0, -1.0) == 240.0);
}

static void test_respawn_override_uses_scheduler_not_dead_update_polling() {
    auto source = readProjectFile("src/hacks/safemode.cpp");
    assert(source.find("scheduleOnce(schedule_selector(GuardedPlayLayer::applyRespawnOverride)") != std::string::npos);
    assert(source.find("unschedule(schedule_selector(GuardedPlayLayer::applyRespawnOverride)") != std::string::npos);
    assert(source.find("respawnTimer") == std::string::npos);
}

static void test_module_card_animation_uses_current_description_fade() {
    auto source = readProjectFile("src/gui/gui.cpp");
    assert(source.find("if (data.progress > 0.0f && !displayDescription.empty())") != std::string::npos);
    assert(source.find("float fade = anim.easeOutCubic(data.progress);") != std::string::npos);
    assert(source.find("280.0f * t") == std::string::npos);
}

static void test_dispatch_keybinds_are_visible_on_module_cards() {
    auto source = readProjectFile("src/gui/gui.cpp");
    assert(source.find("Widgets::ModuleCard(\"Autoclicker\", \"Auto-click at configurable intervals\", &ac->enabled, theme, anim, \"bind_autoclicker\")") != std::string::npos);
    assert(source.find("Widgets::ModuleCard(\"Click Sounds\", \"Play click and release sounds on input\", &csm->enabled, theme, anim, \"bind_click_sounds\")") != std::string::npos);
}

static void test_cocos_menu_frontend_setting_is_persisted() {
    auto frontend = readProjectFile("src/gui/cocos/frontend.cpp");
    auto popup = readProjectFile("src/gui/cocos/tr_menu_popup.cpp");
    assert(frontend.find("mod->setSettingValue<std::string>(\"menu_frontend\"") != std::string::npos);
    assert(frontend.find("auto result = mod->saveData();") != std::string::npos);
    assert(popup.find("toasty::frontend::setMenuFrontend(idx == 1)") != std::string::npos);
}

int main() {
    test_click_audio_controls_change_output();
    test_zero_percent_start_position_can_play_from_start();
    test_editor_playtest_counts_as_gameplay_active();
    test_editor_hitbox_overlay_requires_active_playtest();
    test_ttr3_bridge_preserves_integer_ticks_at_arbitrary_tps();
    test_vanilla_persistence_keeps_navigation_data();
    test_exact_ttr3_playback_timing();
    test_playback_uses_required_macro_tps();
    test_respawn_override_uses_scheduler_not_dead_update_polling();
    test_module_card_animation_uses_current_description_fade();
    test_dispatch_keybinds_are_visible_on_module_cards();
    test_cocos_menu_frontend_setting_is_persisted();
    return 0;
}
