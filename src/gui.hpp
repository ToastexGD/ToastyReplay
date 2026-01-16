#ifndef _gui_hpp
#define _gui_hpp

#include <imgui-cocos.hpp>

class GUI {
public:
    ImFont* s_font = nullptr;
    ImFont* l_font = nullptr;
    ImFont* vl_font = nullptr;

private:
    char tempReplayName[256] = {0};
    bool tempReplayNameInitialized = false;
    
public:
    static auto* get() {
        static GUI* instance = new GUI();
        return instance;
    }

    bool showCBFMessage = false;
    bool shownCBFMessage = false;
    
    bool visible = false;
    bool lastVisible = false;
    bool key = false;
    bool keyCheckFailed = false;
    bool callbackInit = false;

    ImVec4 textColor = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
    ImVec4 backgroundColor = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
    float menuOpacity = 1.0f;
    float fontSize = 1.0f;
    bool rgbTextColor = false;
    float rgbSpeed = 1.0f;
    bool themeResetRequested = false;
    
    ImVec2 mainPanelSize = ImVec2(350, 525);
    ImVec2 infoPanelSize = ImVec2(200, 320);
    ImVec2 hackPanelSize = ImVec2(200, 380);
    ImVec2 themePanelSize = ImVec2(200, 320);
    ImVec2 keybindsPanelSize = ImVec2(250, 400);

    std::string capturingKeybind = "";
    bool isCapturingKeybind = false;

    bool frameStepper = false;
    bool shouldStep = false;

    void renderReplayInfo();
    void renderStateSwitcher();
    void renderMainPanel();
    void renderWatermarkOverlay();
    void renderer();
    void setup();
};

void renderWatermarkOverlay();

#endif
