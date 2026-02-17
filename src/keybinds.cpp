#include <Geode/Bindings.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include "gui.hpp"
#include "ToastyReplay.hpp"

using namespace geode::prelude;

class $modify(MenuKeyHandler, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double) {
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
            return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, 0.0);
        }

        if (ui && down && !repeat) {
            if (ui->keybinds.menu != 0 && k == ui->keybinds.menu) {
                if (!ui->shown && !ui->anim.opening && !ui->anim.closing) {
                    ui->shown = true;
                    ui->anim.opening = true;
                    ui->anim.closing = false;
                    ui->anim.openProgress = 0.0f;
                } else if (ui->shown && !ui->anim.closing && !ui->anim.opening) {
                    ui->anim.closing = true;
                    ui->anim.opening = false;
                }
            }
        }

        if (engine && ui && down && !repeat) {
            auto pl = PlayLayer::get();

            if (ui->keybinds.frameAdvance != 0 && k == ui->keybinds.frameAdvance && pl)
                engine->tickStepping = !engine->tickStepping;

            if (ui->keybinds.replayToggle != 0 && k == ui->keybinds.replayToggle) {
                if (engine->engineMode == MODE_EXECUTE)
                    engine->haltExecution();
                else if (engine->activeMacro)
                    engine->beginExecution();
            }

            if (ui->keybinds.noclip != 0 && k == ui->keybinds.noclip)
                engine->collisionBypass = !engine->collisionBypass;

            if (ui->keybinds.safeMode != 0 && k == ui->keybinds.safeMode)
                engine->protectedMode = !engine->protectedMode;

            if (ui->keybinds.trajectory != 0 && k == ui->keybinds.trajectory)
                engine->pathPreview = !engine->pathPreview;

            if (ui->keybinds.audioPitch != 0 && k == ui->keybinds.audioPitch)
                engine->audioPitchEnabled = !engine->audioPitchEnabled;

            if (ui->keybinds.rngLock != 0 && k == ui->keybinds.rngLock)
                engine->rngLocked = !engine->rngLocked;

            if (ui->keybinds.hitboxes != 0 && k == ui->keybinds.hitboxes)
                engine->showHitboxes = !engine->showHitboxes;
        }

        if (engine && ui && down) {
            if (ui->keybinds.frameStep != 0 && k == ui->keybinds.frameStep && engine->tickStepping)
                engine->singleTickStep = true;
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, 0.0);
    }
};
