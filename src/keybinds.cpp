#include <Geode/Bindings.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/loader/Log.hpp>
#include "gui.hpp"
#include "ToastyReplay.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("ToastyReplay loaded! Press B to toggle menu.");
}

class $modify(ToastyKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat) {
        if (down && !repeat) {
            if (key == enumKeyCodes::KEY_B) {
                log::info("B key pressed!");
                GUI* gui = GUI::get();
                if (gui) {
                    gui->visible = !gui->visible;
                    log::info("GUI visible: {}", gui->visible);
                    auto pl = PlayLayer::get();
                    if (!gui->visible && pl && !pl->m_isPaused) {
                        PlatformToolbox::hideCursor();
                    }
                }
            }
            
            if (key == enumKeyCodes::KEY_V) {
                auto pl = PlayLayer::get();
                if (pl) {
                    ToastyReplay* mgr = ToastyReplay::get();
                    if (mgr) {
                        mgr->frameAdvance = !mgr->frameAdvance;
                    }
                }
            }
        }
        
        if (down && key == enumKeyCodes::KEY_C) {
            auto pl = PlayLayer::get();
            ToastyReplay* mgr = ToastyReplay::get();
            if (pl && mgr && mgr->frameAdvance) {
                mgr->stepFrame = true;
            }
        }
        
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat);
    }
};
