#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace toasty::frontend {

enum class MenuFrontend {
    ImGui,
    Cocos,
};

using CocosColor = std::array<std::uint8_t, 3>;

struct CocosThemePalette {
    CocosColor shell;
    CocosColor header;
    CocosColor navigation;
    CocosColor content;
    CocosColor cell;
    CocosColor cellBorder;
    CocosColor subCell;
    CocosColor accent;
    CocosColor secondary;
    CocosColor sectionText;
    CocosColor mutedText;
    CocosColor inactive;
};

MenuFrontend current();

bool isCocos();

void setMenuFrontend(bool useCocos);

std::vector<std::string> const& cocosThemeNames();
std::string cocosThemeName();
CocosThemePalette cocosTheme();
void setCocosTheme(std::string const& name);

void toggleMenu();

void refreshMenuState();

void persistSettings();

bool renderWatermarkEnabled();
void setRenderWatermarkEnabled(bool enabled);

bool desktopKeybinds();
bool textInputActive();

char const* keybindSettingId(std::string_view id);

std::string keybindDisplay(std::string_view settingKey);

void openKeybindEditor();

}
