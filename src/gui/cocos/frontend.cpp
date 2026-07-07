#include "gui/cocos/frontend.hpp"

#include "gui/cocos/tr_menu_popup.hpp"
#include "gui/gui.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <mutex>
#include <string>

using namespace geode::prelude;

namespace toasty::frontend {
namespace {
    constexpr char const* kCocosMenuWarningText =
        "my cocos menu and structuring sucks, if you find any bugs please report it in my discord, believe me, there are bugs. Thanks!";

    std::once_flag g_frontendWarningInit;
    bool g_lastFrontendWasCocos = false;

    bool isCocosValue(std::string const& value) {
        return value == "Cocos2d";
    }

    void showCocosMenuWarning() {
        FLAlertLayer::create("Warning", kCocosMenuWarningText, "OK")->show();
    }

    void handleMenuFrontendChanged(std::string const& value) {
        bool const nowCocos = isCocosValue(value);
        if (nowCocos && !g_lastFrontendWasCocos) {
            showCocosMenuWarning();
        }
        g_lastFrontendWasCocos = nowCocos;
    }

    void initializeFrontendWarningListener() {
        std::call_once(g_frontendWarningInit, [] {
            g_lastFrontendWasCocos = isCocosValue(Mod::get()->getSettingValue<std::string>("menu_frontend"));
            listenForSettingChanges<std::string>("menu_frontend", [](std::string const& value) {
                handleMenuFrontendChanged(value);
            });
        });
    }
}

MenuFrontend current() {
    initializeFrontendWarningListener();
    auto value = Mod::get()->getSettingValue<std::string>("menu_frontend");
    if (isCocosValue(value)) {
        return MenuFrontend::Cocos;
    }
    return MenuFrontend::ImGui;
}

bool isCocos() {
    return current() == MenuFrontend::Cocos;
}

void setMenuFrontend(bool useCocos) {
    initializeFrontendWarningListener();
    Mod::get()->setSettingValue<std::string>("menu_frontend", useCocos ? std::string("Cocos2d") : std::string("ImGui"));
}

void toggleMenu() {
    if (current() == MenuFrontend::Cocos) {
        TRMenuPopup::toggle();
        return;
    }
    auto* ui = MenuInterface::get();
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

void persistSettings() {
    MenuInterface::get()->saveSettings();
}

bool renderWatermarkEnabled() {
    return MenuInterface::get()->renderWatermarkEnabled;
}

void setRenderWatermarkEnabled(bool enabled) {
    MenuInterface::get()->renderWatermarkEnabled = enabled;
    Mod::get()->setSavedValue("render_watermark_enabled", enabled);
}

bool desktopKeybinds() {
#ifdef GEODE_IS_ANDROID
    return false;
#else
    return true;
#endif
}

int* keybindPtr(std::string const& id) {
    auto& kb = MenuInterface::get()->keybinds;
    if (id == "menu")            return &kb.menu;
    if (id == "click_sounds")    return &kb.clickSounds;
    if (id == "safe_mode")       return &kb.safeMode;
    if (id == "trajectory")      return &kb.trajectory;
    if (id == "hitboxes")        return &kb.hitboxes;
    if (id == "noclip")          return &kb.noclip;
    if (id == "rng_lock")        return &kb.rngLock;
    if (id == "frame_advance")   return &kb.frameAdvance;
    if (id == "frame_step")      return &kb.frameStep;
    if (id == "audio_pitch")     return &kb.audioPitch;
    if (id == "layout_mode")     return &kb.layoutMode;
    if (id == "disable_shaders") return &kb.disableShaders;
    if (id == "no_mirror")       return &kb.noMirror;
    if (id == "autoclicker")     return &kb.autoclicker;
    if (id == "replay_toggle")   return &kb.replayToggle;
    return nullptr;
}

std::string keyName(int code) {
    return getKeyName(code);
}

std::vector<std::pair<std::string, std::string>> allKeybinds() {
    return {
        { "Menu Toggle", "menu" },
        { "Replay Toggle", "replay_toggle" },
        { "Frame Advance", "frame_advance" },
        { "Advance Frame", "frame_step" },
        { "Noclip", "noclip" },
        { "Safe Mode", "safe_mode" },
        { "Trajectory", "trajectory" },
        { "Hitboxes", "hitboxes" },
        { "RNG Lock", "rng_lock" },
        { "Audio Pitch", "audio_pitch" },
        { "Layout Mode", "layout_mode" },
        { "Disable Shaders", "disable_shaders" },
        { "No Mirror", "no_mirror" },
        { "Click Sounds", "click_sounds" },
        { "Autoclicker", "autoclicker" },
    };
}

void beginRebind(int* keyPtr) {
    MenuInterface::get()->rebindTarget = keyPtr;
}

bool isRebinding(int* keyPtr) {
    return MenuInterface::get()->rebindTarget == keyPtr;
}

}
