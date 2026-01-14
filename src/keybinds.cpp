#include <geode.custom-keybinds/include/Keybinds.hpp>
#include <imgui-cocos.hpp>
#include "gui.hpp"
#include "ToastyReplay.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/LoadingLayer.hpp>
using namespace geode::prelude;

#ifdef _WIN32
#include <Windows.h>
#include <thread>
#include <atomic>
#include <chrono>
#endif

$execute {
    using namespace keybinds;
    ToastyReplay* mgr = ToastyReplay::get();

    BindManager::get()->registerBindable({
        "gui_toggle"_spr,
        "ToastyReplay Toggle",
        "Toggles the ToastyReplay GUI",
        { Keybind::create(KEY_B, Modifier::None) },
        "ToastyReplay"
    });

    new keybinds::EventListener<InvokeBindFilter>([=](InvokeBindEvent* event) {
        if (event->isDown() && !ImGui::GetIO().WantCaptureKeyboard) {
            log::info("B key pressed, toggling GUI visibility");
            GUI* gui = GUI::get();
            gui->visible = !gui->visible;
            log::info("GUI visible: {}", gui->visible);

            auto pl = PlayLayer::get();

            if (!gui->visible && pl && !pl->m_isPaused) {
                PlatformToolbox::hideCursor();
            }
        }

        return keybinds::ListenerResult::Propagate;
    }, InvokeBindFilter(nullptr, "gui_toggle"_spr));
}

#ifdef _WIN32
// Fallback polling thread: toggles GUI on B press if real keybinds aren't available.
// This is a simple fallback for testing — run on a separate thread and debounce.
static std::atomic_bool _toastyreplay_poll_stop{false};

struct _ToastyReplayKeyPollerStarter {
    _ToastyReplayKeyPollerStarter() {
        std::thread([](){
            bool prev = false;
            while (!_toastyreplay_poll_stop.load()) {
                bool down = (GetAsyncKeyState('B') & 0x8000) != 0;
                if (down && !prev) {
                    if (!ImGui::GetIO().WantCaptureKeyboard) {
                        GUI* gui = GUI::get();
                        if (gui) {
                            gui->visible = !gui->visible;
                            auto pl = PlayLayer::get();
                            if (!gui->visible && pl && !pl->m_isPaused) {
                                PlatformToolbox::hideCursor();
                            }
                        }
                    }
                }
                prev = down;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }).detach();
    }
    ~_ToastyReplayKeyPollerStarter() { _toastyreplay_poll_stop.store(true); }
} _toastyreplayKeyPollerStarterInstance;
#endif
