#include <Geode/Bindings.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/loader/Log.hpp>
#include "gui.hpp"
#include "ToastyReplay.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("ToastyReplay loaded! Press B to toggle menu.");
}

class $modify(MenuKeyHandler, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat) {
        if (down && !repeat) {
            if (key == enumKeyCodes::KEY_B) {
                log::info("B key pressed!");
                MenuInterface* ui = MenuInterface::get();
                if (ui) {
                    ui->shown = !ui->shown;
                    log::info("GUI visible: {}", ui->shown);
                    auto pl = PlayLayer::get();
                    if (!ui->shown && pl && !pl->m_isPaused) {
                        PlatformToolbox::hideCursor();
                    }
                }
            }

            if (key == enumKeyCodes::KEY_V) {
                auto pl = PlayLayer::get();
                if (pl) {
                    ReplayEngine* engine = ReplayEngine::get();
                    if (engine) {
                        engine->tickStepping = !engine->tickStepping;
                    }
                }
            }
        }

        if (down && key == enumKeyCodes::KEY_C) {
            auto pl = PlayLayer::get();
            ReplayEngine* engine = ReplayEngine::get();
            if (pl && engine && engine->tickStepping) {
                engine->singleTickStep = true;
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat);
    }
};
