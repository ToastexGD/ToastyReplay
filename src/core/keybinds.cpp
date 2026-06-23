#include <Geode/Bindings.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
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
    FrameStep
};

struct KeybindEntry {
    int KeybindSet::* slot;
    KeybindAction action;
    bool requiresPlayLayer;
    bool fireOnRepeatToo;
};

static constexpr KeybindEntry dispatchTable[] = {
    { &KeybindSet::menu,           KeybindAction::MenuToggle,     false, false },
    { &KeybindSet::frameAdvance,   KeybindAction::FrameAdvance,   true,  false },
    { &KeybindSet::replayToggle,   KeybindAction::ReplayToggle,   false, false },
    { &KeybindSet::noclip,         KeybindAction::Noclip,         false, false },
    { &KeybindSet::safeMode,       KeybindAction::SafeMode,       false, false },
    { &KeybindSet::trajectory,     KeybindAction::Trajectory,     false, false },
    { &KeybindSet::audioPitch,     KeybindAction::AudioPitch,     false, false },
    { &KeybindSet::rngLock,        KeybindAction::RngLock,        false, false },
    { &KeybindSet::hitboxes,       KeybindAction::Hitboxes,       false, false },
    { &KeybindSet::layoutMode,     KeybindAction::LayoutMode,     false, false },
    { &KeybindSet::noMirror,       KeybindAction::NoMirror,       false, false },
    { &KeybindSet::autoclicker,    KeybindAction::Autoclicker,    false, false },
    { &KeybindSet::disableShaders, KeybindAction::DisableShaders, false, false },
    { &KeybindSet::clickSounds,    KeybindAction::ClickSounds,    false, false },
    { &KeybindSet::frameStep,      KeybindAction::FrameStep,      true,  true  },
};

static bool runAction(KeybindAction action, ReplayEngine* engine, MenuInterface* ui, PlayLayer* pl) {
    switch (action) {
        case KeybindAction::MenuToggle:
            if (toasty::frontend::isCocos()) {
                toasty::frontend::toggleMenu();
                return true;
            }
            if (ui->anim.closing) {
                ui->anim.closing = false;
                ui->anim.opening = true;
                ui->shown = true;
            } else if (ui->anim.opening) {
                ui->anim.opening = false;
                ui->anim.closing = true;
            } else if (!ui->shown) {
                ui->shown = true;
                ui->anim.opening = true;
                ui->anim.closing = false;
                ui->anim.openProgress = 0.0f;
            } else {
                ui->anim.closing = true;
                ui->anim.opening = false;
            }
            return true;

        case KeybindAction::FrameAdvance:
            engine->setFrameStepEnabled(!engine->tickStepping, pl);
            return true;

        case KeybindAction::ReplayToggle:
            if (engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart)
                engine->haltExecution();
            else if (engine->hasMacroInputs())
                engine->requestExecutionStart();
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
            if (engine->tickStepping && !engine->renderer.recording)
                engine->singleTickStep = true;
            return true;
    }
    return false;
}

}

class $modify(MenuKeyHandler, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        MenuInterface* ui = MenuInterface::get();
        ReplayEngine* engine = ReplayEngine::get();
        int k = (int)key;

        if (down && !repeat && ui && ui->rebindTarget) {
            if (key == enumKeyCodes::KEY_Escape)
                ui->rebindTarget = nullptr;
            else {
                *ui->rebindTarget = k;
                ui->rebindTarget = nullptr;
            }
            return true;
        }

        if (ImGui::GetIO().WantTextInput) {
            return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
        }

        bool handledByHotkey = false;
        PlayLayer* pl = PlayLayer::get();

        if (ui && engine && down) {
            for (const auto& entry : dispatchTable) {
                int bound = ui->keybinds.*entry.slot;
                if (bound == 0 || k != bound) continue;
                if (repeat && !entry.fireOnRepeatToo) continue;
                if (entry.requiresPlayLayer && !pl) continue;

                if (runAction(entry.action, engine, ui, pl))
                    handledByHotkey = true;
            }
        }

        if (!repeat && !handledByHotkey && PlayLayer::get()) {
            if (key == enumKeyCodes::KEY_Space || key == enumKeyCodes::KEY_Up || key == enumKeyCodes::KEY_W) {
                Autoclicker::get()->trackUserInput(down, false);
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
