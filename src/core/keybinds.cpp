#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>

#include "gui/gui.hpp"
#include "gui/cocos/frontend.hpp"
#include "ToastyReplay.hpp"
#include "hacks/autoclicker.hpp"
#include "audio/clicksounds.hpp"

using namespace geode::prelude;

namespace {
enum class KeybindAction {
    MenuToggle,
    FrameAdvance,
    ReplayToggle,
    Noclip,
    SafeMode,
    Trajectory,
    AudioPitch,
    RngLock,
    Hitboxes,
    LayoutMode,
    NoMirror,
    Autoclicker,
    DisableShaders,
    ClickSounds,
    FrameStep,
    NoDeathEffect,
    NoEffects,
    HideEndscreen,
    HideNewBest,
    InstantReset,
    Autosave,
    PersistenceMode,
    MuteLeftRight,
    RenderWatermark
};

struct KeybindEntry {
    char const* setting;
    char const* legacySave;
    KeybindAction action;
    bool requiresPlayLayer;
    bool fireOnRepeatToo;
};

constexpr KeybindEntry dispatchTable[] = {
    { "bind_menu",            "key_menu",            KeybindAction::MenuToggle,     false, false },
    { "bind_frame_advance",   "key_frame_advance",   KeybindAction::FrameAdvance,   true,  false },
    { "bind_replay_toggle",   "key_replay_toggle",   KeybindAction::ReplayToggle,   false, false },
    { "bind_noclip",          "key_noclip",          KeybindAction::Noclip,         false, false },
    { "bind_safe_mode",       "key_safe_mode",       KeybindAction::SafeMode,       false, false },
    { "bind_trajectory",      "key_trajectory",      KeybindAction::Trajectory,     false, false },
    { "bind_audio_pitch",     "key_audio_pitch",     KeybindAction::AudioPitch,     false, false },
    { "bind_rng_lock",        "key_rng_lock",        KeybindAction::RngLock,        false, false },
    { "bind_hitboxes",        "key_hitboxes",        KeybindAction::Hitboxes,       false, false },
    { "bind_layout_mode",     "key_layout_mode",     KeybindAction::LayoutMode,     false, false },
    { "bind_no_mirror",       "key_no_mirror",       KeybindAction::NoMirror,       false, false },
    { "bind_autoclicker",     "key_autoclicker",     KeybindAction::Autoclicker,    false, false },
    { "bind_disable_shaders", "key_disable_shaders", KeybindAction::DisableShaders, false, false },
    { "bind_click_sounds",    "key_click_sounds",    KeybindAction::ClickSounds,    false, false },
    { "bind_frame_step",      "key_frame_step",      KeybindAction::FrameStep,      true,  true  },
    { "bind_no_death_effect", "key_no_death_effect", KeybindAction::NoDeathEffect, false, false },
    { "bind_no_effects",      "key_no_effects",      KeybindAction::NoEffects,     false, false },
    { "bind_hide_endscreen",  "key_hide_endscreen",  KeybindAction::HideEndscreen, false, false },
    { "bind_hide_new_best",   "key_hide_new_best",   KeybindAction::HideNewBest,   false, false },
    { "bind_instant_reset",   "key_instant_reset",   KeybindAction::InstantReset,  false, false },
    { "bind_autosave",        "key_autosave",        KeybindAction::Autosave,      false, false },
    { "bind_persistence_mode", "key_persistence_mode", KeybindAction::PersistenceMode, false, false },
    { "bind_mute_left_right", "key_mute_left_right", KeybindAction::MuteLeftRight, false, false },
    { "bind_render_watermark", "key_render_watermark", KeybindAction::RenderWatermark, false, false },
};

bool shouldPersist(KeybindAction action) {
    return action != KeybindAction::MenuToggle
        && action != KeybindAction::FrameAdvance
        && action != KeybindAction::FrameStep
        && action != KeybindAction::ReplayToggle;
}

bool shouldRefreshMenu(KeybindAction action) {
    return action != KeybindAction::MenuToggle;
}

bool runAction(KeybindAction action, ReplayEngine* engine, PlayLayer* playLayer) {
    switch (action) {
        case KeybindAction::MenuToggle:
            toasty::frontend::toggleMenu();
            return true;
        case KeybindAction::FrameAdvance:
            engine->setFrameStepEnabled(!engine->tickStepping, playLayer);
            return true;
        case KeybindAction::ReplayToggle:
            if (engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart) {
                engine->haltExecution();
            } else if (engine->hasMacroInputs()) {
                engine->requestExecutionStart();
            }
            return true;
        case KeybindAction::Noclip:
            engine->collisionBypass = !engine->collisionBypass;
            return true;
        case KeybindAction::SafeMode:
            engine->protectedMode = !engine->protectedMode;
            return true;
        case KeybindAction::Trajectory:
            engine->pathPreview = !engine->pathPreview;
            return true;
        case KeybindAction::AudioPitch:
            engine->audioPitchEnabled = !engine->audioPitchEnabled;
            return true;
        case KeybindAction::RngLock:
            engine->rngLocked = !engine->rngLocked;
            return true;
        case KeybindAction::Hitboxes:
            engine->showHitboxes = !engine->showHitboxes;
            return true;
        case KeybindAction::LayoutMode:
            engine->layoutMode = !engine->layoutMode;
            return true;
        case KeybindAction::NoMirror:
            engine->noMirrorEffect = !engine->noMirrorEffect;
            return true;
        case KeybindAction::Autoclicker:
            Autoclicker::get()->enabled = !Autoclicker::get()->enabled;
            return true;
        case KeybindAction::DisableShaders:
            engine->disableShaders = !engine->disableShaders;
            return true;
        case KeybindAction::ClickSounds:
            ClickSoundManager::get()->enabled = !ClickSoundManager::get()->enabled;
            return true;
        case KeybindAction::FrameStep:
            if (engine->tickStepping && !engine->renderer.recording) {
                engine->singleTickStep = true;
            }
            return true;
        case KeybindAction::NoDeathEffect:
            engine->noDeathEffect = !engine->noDeathEffect;
            return true;
        case KeybindAction::NoEffects:
            engine->noEffect = !engine->noEffect;
            return true;
        case KeybindAction::HideEndscreen:
            engine->hideEndscreen = !engine->hideEndscreen;
            return true;
        case KeybindAction::HideNewBest:
            engine->hideNewBest = !engine->hideNewBest;
            return true;
        case KeybindAction::InstantReset:
            engine->fastPlayback = !engine->fastPlayback;
            return true;
        case KeybindAction::Autosave:
            engine->completionAutosave = !engine->completionAutosave;
            if (engine->completionAutosave && engine->persistenceMode) {
                engine->setPersistenceMode(false);
            }
            Mod::get()->setSavedValue("eng_completion_autosave", engine->completionAutosave);
            return true;
        case KeybindAction::PersistenceMode:
            if (engine->engineMode != MODE_DISABLED) {
                return false;
            }
            engine->setPersistenceMode(!engine->persistenceMode);
            return true;
        case KeybindAction::MuteLeftRight: {
            auto* clickSounds = ClickSoundManager::get();
            clickSounds->muteLeftRightClicks = !clickSounds->muteLeftRightClicks;
            Mod::get()->setSavedValue("click_mute_left_right", clickSounds->muteLeftRightClicks);
            return true;
        }
        case KeybindAction::RenderWatermark: {
            auto* menu = MenuInterface::get();
            menu->renderWatermarkEnabled = !menu->renderWatermarkEnabled;
            Mod::get()->setSavedValue("render_watermark_enabled", menu->renderWatermarkEnabled);
            return true;
        }
    }
    return false;
}

void migrateLegacyKeybinds() {
    auto* mod = Mod::get();
    if (mod->getSavedValue<bool>("native_keybinds_migrated", false)) {
        return;
    }
    for (auto const& entry : dispatchTable) {
        if (!mod->hasSavedValue(entry.legacySave)) {
            continue;
        }
        auto setting = typeinfo_pointer_cast<KeybindSettingV3>(mod->getSetting(entry.setting));
        if (!setting) {
            continue;
        }
        int legacyKey = mod->getSavedValue<int>(entry.legacySave, 0);
        if (legacyKey > 0) {
            setting->setValue({ Keybind(static_cast<enumKeyCodes>(legacyKey), KeyboardModifier::None) });
        } else {
            setting->setValue({});
        }
    }
    mod->setSavedValue("native_keybinds_migrated", true);
}

void registerKeybinds() {
    migrateLegacyKeybinds();
    for (auto const& entry : dispatchTable) {
        listenForKeybindSettingPresses(entry.setting, [entry](Keybind const&, bool down, bool repeat, double) {
            if (!down || (repeat && !entry.fireOnRepeatToo)) {
                return;
            }
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) {
                return;
            }
            if (toasty::frontend::textInputActive()) {
                return;
            }
            auto* engine = ReplayEngine::get();
            auto* playLayer = PlayLayer::get();
            if (!engine || (entry.requiresPlayLayer && !playLayer)) {
                return;
            }
            if (!runAction(entry.action, engine, playLayer)) {
                return;
            }
            if (shouldPersist(entry.action)) {
                toasty::frontend::persistSettings();
            }
            if (shouldRefreshMenu(entry.action)) {
                toasty::frontend::refreshMenuState();
            }
        });
    }

    KeyboardInputEvent().listen([](KeyboardInputData& data) {
        if (data.action == KeyboardInputData::Action::Repeat || !PlayLayer::get() || toasty::frontend::textInputActive()) {
            return ListenerResult::Propagate;
        }
        if (data.key == KEY_Space || data.key == KEY_Up || data.key == KEY_W) {
            Autoclicker::get()->trackUserInput(data.action == KeyboardInputData::Action::Press, false);
        }
        return ListenerResult::Propagate;
    }).leak();
}
}

$on_mod(Loaded) {
    registerKeybinds();
}
