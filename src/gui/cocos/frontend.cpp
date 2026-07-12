#include "gui/cocos/frontend.hpp"

#include "gui/cocos/tr_menu_popup.hpp"
#include "gui/gui.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace toasty::frontend {
namespace {
    bool isCocosValue(std::string const& value) {
        return value == "Cocos2d";
    }

    CocosThemePalette themeFor(std::string const& name) {
        if (name == "Toasty") {
            return { { 19, 15, 29 }, { 29, 24, 43 }, { 24, 22, 36 }, { 18, 19, 31 }, { 25, 24, 39 }, { 79, 59, 86 }, { 31, 40, 60 }, { 239, 112, 113 }, { 96, 207, 224 }, { 255, 211, 174 }, { 176, 181, 204 }, { 58, 56, 74 } };
        }
        if (name == "Ocean") {
            return { { 31, 82, 126 }, { 24, 66, 103 }, { 22, 58, 91 }, { 18, 47, 76 }, { 24, 61, 94 }, { 66, 159, 197 }, { 29, 78, 111 }, { 62, 206, 224 }, { 111, 231, 177 }, { 195, 244, 255 }, { 172, 208, 220 }, { 44, 87, 116 } };
        }
        if (name == "Forest") {
            return { { 72, 111, 50 }, { 54, 87, 43 }, { 49, 78, 40 }, { 42, 67, 36 }, { 55, 84, 45 }, { 133, 169, 72 }, { 63, 96, 54 }, { 151, 211, 73 }, { 242, 193, 74 }, { 240, 226, 154 }, { 194, 207, 169 }, { 76, 101, 64 } };
        }
        if (name == "Violet") {
            return { { 93, 61, 137 }, { 70, 44, 106 }, { 59, 39, 88 }, { 45, 31, 69 }, { 62, 43, 87 }, { 152, 100, 190 }, { 76, 52, 104 }, { 202, 116, 231 }, { 93, 220, 206 }, { 238, 202, 255 }, { 204, 184, 218 }, { 83, 62, 104 } };
        }
        return { { 255, 255, 255 }, { 131, 76, 38 }, { 114, 68, 37 }, { 82, 50, 31 }, { 105, 62, 36 }, { 208, 143, 66 }, { 83, 92, 51 }, { 126, 211, 33 }, { 255, 204, 58 }, { 255, 225, 130 }, { 229, 207, 177 }, { 120, 86, 62 } };
    }

    bool hasFocusedTextInput(CCNode* node) {
        if (!node) {
            return false;
        }
        if (auto* input = typeinfo_cast<CCTextInputNode*>(node); input && input->m_selected) {
            return true;
        }
        for (CCNode* child : node->getChildrenExt()) {
            if (hasFocusedTextInput(child)) {
                return true;
            }
        }
        return false;
    }
}

MenuFrontend current() {
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
    auto* mod = Mod::get();
    mod->setSettingValue<std::string>("menu_frontend", useCocos ? std::string("Cocos2d") : std::string("ImGui"));
    auto result = mod->saveData();
    if (!result) {
        log::warn("Failed to persist menu style: {}", result.unwrapErr());
    }
}

std::vector<std::string> const& cocosThemeNames() {
    static const std::vector<std::string> names = { "Native", "Toasty", "Ocean", "Forest", "Violet" };
    return names;
}

std::string cocosThemeName() {
    auto name = Mod::get()->getSettingValue<std::string>("cocos_theme");
    auto const& names = cocosThemeNames();
    return std::find(names.begin(), names.end(), name) != names.end() ? name : std::string("Native");
}

CocosThemePalette cocosTheme() {
    return themeFor(cocosThemeName());
}

void setCocosTheme(std::string const& name) {
    auto const& names = cocosThemeNames();
    auto value = std::find(names.begin(), names.end(), name) != names.end() ? name : std::string("Native");
    auto* mod = Mod::get();
    mod->setSettingValue<std::string>("cocos_theme", value);
    auto result = mod->saveData();
    if (!result) {
        log::warn("Failed to persist Cocos theme: {}", result.unwrapErr());
    }
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

void refreshMenuState() {
    if (current() == MenuFrontend::Cocos) {
        TRMenuPopup::refreshOpenMenu();
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
#ifdef GEODE_IS_MOBILE
    return false;
#else
    return true;
#endif
}

bool textInputActive() {
    return hasFocusedTextInput(CCDirector::sharedDirector()->getRunningScene());
}

char const* keybindSettingId(std::string_view id) {
    if (id == "menu")            return "bind_menu";
    if (id == "click_sounds")    return "bind_click_sounds";
    if (id == "safe_mode")       return "bind_safe_mode";
    if (id == "trajectory")      return "bind_trajectory";
    if (id == "hitboxes")        return "bind_hitboxes";
    if (id == "noclip")          return "bind_noclip";
    if (id == "rng_lock")        return "bind_rng_lock";
    if (id == "frame_advance")   return "bind_frame_advance";
    if (id == "frame_step")      return "bind_frame_step";
    if (id == "audio_pitch")     return "bind_audio_pitch";
    if (id == "layout_mode")     return "bind_layout_mode";
    if (id == "disable_shaders") return "bind_disable_shaders";
    if (id == "no_mirror")       return "bind_no_mirror";
    if (id == "autoclicker")     return "bind_autoclicker";
    if (id == "replay_toggle")   return "bind_replay_toggle";
    if (id == "no_death_effect") return "bind_no_death_effect";
    if (id == "no_effects")      return "bind_no_effects";
    if (id == "hide_endscreen")  return "bind_hide_endscreen";
    if (id == "hide_new_best")   return "bind_hide_new_best";
    if (id == "instant_reset")   return "bind_instant_reset";
    if (id == "autosave")        return "bind_autosave";
    if (id == "persistence_mode") return "bind_persistence_mode";
    if (id == "mute_left_right") return "bind_mute_left_right";
    if (id == "render_watermark") return "bind_render_watermark";
    return nullptr;
}

std::string keybindDisplay(std::string_view settingKey) {
    auto setting = typeinfo_pointer_cast<KeybindSettingV3>(Mod::get()->getSetting(settingKey));
    if (!setting || setting->getValue().empty()) {
        return "Unbound";
    }
    auto const& values = setting->getValue();
    std::string display = values.front().toString();
    if (values.size() > 1) {
        display += " +" + std::to_string(values.size() - 1);
    }
    return display;
}

void openKeybindEditor() {
#ifdef GEODE_IS_MOBILE
    FLAlertLayer::create("Keybind Manager", "Keybind gesture support is not yet available for mobile. Sorry :3", "OK")->show();
#else
    geode::openKeybindsPopup(std::nullopt, Mod::get());
#endif
}

}
