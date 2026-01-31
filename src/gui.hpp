#ifndef _gui_hpp
#define _gui_hpp

#include <imgui-cocos.hpp>

class MenuInterface {
public:
    ImFont* smallTypeface = nullptr;
    ImFont* largeTypeface = nullptr;
    ImFont* extraLargeTypeface = nullptr;

private:
    char macroNameBuffer[256] = {0};
    bool macroNameReady = false;

public:
    static auto* get() {
        static MenuInterface* singleton = new MenuInterface();
        return singleton;
    }

    bool shown = false;
    bool previouslyShown = false;
    bool validated = false;
    bool validationFailed = false;
    bool setupComplete = false;

    ImVec4 fontColor = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
    ImVec4 bgColor = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    float windowAlpha = 1.0f;
    float textScale = 1.0f;
    bool cyclingColors = false;
    float cycleRate = 1.0f;
    bool layoutReset = false;

    ImVec2 primaryPanelDims = ImVec2(350, 525);
    ImVec2 utilityPanelDims = ImVec2(200, 320);
    ImVec2 toolsPanelDims = ImVec2(200, 380);
    ImVec2 stylePanelDims = ImVec2(200, 320);
    ImVec2 shortcutsPanelDims = ImVec2(250, 400);

    std::string activeHotkeyCapture = "";
    bool capturingHotkey = false;

    bool stepperActive = false;
    bool stepRequested = false;

    void displayMacroDetails();
    void displayModeSelector();
    void displayPrimaryPanel();
    void displayOverlayBranding();
    void drawInterface();
    void initialize();
};

void displayOverlayBranding();

#endif
