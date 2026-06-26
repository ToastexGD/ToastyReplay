#pragma once

#include <string>
#include <utility>
#include <vector>

namespace toasty::frontend {

enum class MenuFrontend {
    ImGui,
    Cocos,
};

MenuFrontend current();

bool isCocos();

void toggleMenu();

void persistSettings();

bool renderWatermarkEnabled();
void setRenderWatermarkEnabled(bool enabled);

bool desktopKeybinds();

int* keybindPtr(std::string const& id);

std::string keyName(int code);

std::vector<std::pair<std::string, std::string>> allKeybinds();

void beginRebind(int* keyPtr);

bool isRebinding(int* keyPtr);

}
