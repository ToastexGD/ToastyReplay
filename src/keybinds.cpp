#include <Geode/Bindings.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include "gui.hpp"
#include "ToastyReplay.hpp"

using namespace geode::prelude;

class $modify(CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat) {
        if (down && !repeat) {
            // B key - toggle GUI
            if (key == KEY_B) {
                GUI* gui = GUI::get();
                if (gui) {
                    gui->visible = !gui->visible;
                    auto pl = PlayLayer::get();
                    if (!gui->visible && pl && !pl->m_isPaused) {
                        PlatformToolbox::hideCursor();
                    }
                }
            }
            
            // V key - toggle frame advance (only in PlayLayer)
            if (key == KEY_V) {
                auto pl = PlayLayer::get();
                if (pl) {
                    ToastyReplay* mgr = ToastyReplay::get();
                    if (mgr) {
                        mgr->frameAdvance = !mgr->frameAdvance;
                    }
                }
            }
            
            // C key - advance one frame (only in PlayLayer with frame advance enabled)
            if (key == KEY_C) {
                auto pl = PlayLayer::get();
                ToastyReplay* mgr = ToastyReplay::get();
                if (pl && mgr && mgr->frameAdvance) {
                    mgr->stepFrame = true;
                }
            }
        }
        
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat);
    }
};
