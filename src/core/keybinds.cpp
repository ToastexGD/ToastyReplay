#include <Geode/Bindings.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include "gui/gui.hpp"
#include "ToastyReplay.hpp"
#include "core/input_timing.hpp"
#include "hacks/autoclicker.hpp"

using namespace geode::prelude;

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
        if (ui && down && !repeat) {
            if (ui->keybinds.menu != 0 && k == ui->keybinds.menu) {
                handledByHotkey = true;
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
            }
        }

        if (engine && ui && down && !repeat) {
            auto pl = PlayLayer::get();

            if (ui->keybinds.frameAdvance != 0 && k == ui->keybinds.frameAdvance && pl) {
                handledByHotkey = true;
                engine->tickStepping = !engine->tickStepping;
            }

            if (ui->keybinds.replayToggle != 0 && k == ui->keybinds.replayToggle) {
                handledByHotkey = true;
                if (engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart)
                    engine->haltExecution();
                else if (engine->hasMacroInputs())
                    engine->requestExecutionStart();
            }

            if (ui->keybinds.noclip != 0 && k == ui->keybinds.noclip) {
                handledByHotkey = true;
                engine->collisionBypass = !engine->collisionBypass;
            }

            if (ui->keybinds.safeMode != 0 && k == ui->keybinds.safeMode) {
                handledByHotkey = true;
                engine->protectedMode = !engine->protectedMode;
            }

            if (ui->keybinds.trajectory != 0 && k == ui->keybinds.trajectory) {
                handledByHotkey = true;
                engine->pathPreview = !engine->pathPreview;
            }

            if (ui->keybinds.audioPitch != 0 && k == ui->keybinds.audioPitch) {
                handledByHotkey = true;
                engine->audioPitchEnabled = !engine->audioPitchEnabled;
            }

            if (ui->keybinds.rngLock != 0 && k == ui->keybinds.rngLock) {
                handledByHotkey = true;
                engine->rngLocked = !engine->rngLocked;
            }

            if (ui->keybinds.hitboxes != 0 && k == ui->keybinds.hitboxes) {
                handledByHotkey = true;
                engine->showHitboxes = !engine->showHitboxes;
            }

            if (ui->keybinds.layoutMode != 0 && k == ui->keybinds.layoutMode) {
                handledByHotkey = true;
                engine->layoutMode = !engine->layoutMode;
            }

            if (ui->keybinds.noMirror != 0 && k == ui->keybinds.noMirror) {
                handledByHotkey = true;
                engine->noMirrorEffect = !engine->noMirrorEffect;
            }

            if (ui->keybinds.autoclicker != 0 && k == ui->keybinds.autoclicker) {
                handledByHotkey = true;
                Autoclicker::get()->enabled = !Autoclicker::get()->enabled;
            }
        }

        if (engine && ui && down) {
            if (ui->keybinds.frameStep != 0 && k == ui->keybinds.frameStep && engine->tickStepping) {
                handledByHotkey = true;
                engine->singleTickStep = true;
            }
        }

        if (!repeat && !handledByHotkey) {
            InputTiming::queueCurrentTimestamp();
        }

        if (!repeat && !handledByHotkey && PlayLayer::get()) {
            if (key == enumKeyCodes::KEY_Space || key == enumKeyCodes::KEY_Up || key == enumKeyCodes::KEY_W) {
                Autoclicker::get()->trackUserInput(down, false);
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
