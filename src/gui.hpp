#ifndef _gui_hpp
#define _gui_hpp

#include <imgui-cocos.hpp>
#include <unordered_map>
#include <string>

struct ThemeEngine {
    ImVec4 accentColor = ImVec4(0.65f, 0.20f, 0.85f, 1.0f);
    ImVec4 bgColor = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
    ImVec4 cardColor = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
    ImVec4 textPrimary = ImVec4(0.93f, 0.93f, 0.95f, 1.0f);
    ImVec4 textSecondary = ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
    float windowAlpha = 0.92f;
    float cornerRadius = 10.0f;
    float textScale = 1.0f;

    bool cyclingAccent = false;
    float cycleRate = 1.0f;
    float blurStrength = 0.5f;
    bool cursorGlow = true;
    float glowRadius = 120.0f;

    ImVec4 computeCycleColor(float rate);
    ImVec4 getAccent();
    ImU32 getAccentU32(float alpha = 1.0f);
    ImU32 getAccentDimU32(float factor = 0.3f);
    ImU32 getTextU32();
    ImU32 getTextSecondaryU32();
    ImU32 getCardU32();
    void applyToImGuiStyle();
    void resetDefaults();
};

enum AnimDirection {
    ANIM_CENTER,
    ANIM_FROM_LEFT,
    ANIM_FROM_RIGHT,
    ANIM_FROM_TOP,
    ANIM_FROM_BOTTOM
};

struct AnimationState {
    float openProgress = 0.0f;
    bool opening = false;
    bool closing = false;

    float tabTransition = 1.0f;
    int transitionFromTab = -1;

    std::unordered_map<ImGuiID, float> toggleAnims;
    std::unordered_map<ImGuiID, float> hoverAnims;
    struct ModuleAnimData { float progress = 0.0f; float height = 0.0f; };
    std::unordered_map<const void*, ModuleAnimData> moduleAnims;

    float animSpeed = 8.0f;
    AnimDirection openDirection = ANIM_CENTER;
    ImVec2 smoothCursorPos = ImVec2(0, 0);
    bool cursorPosInitialized = false;

    void update(float dt);
    float easeOutCubic(float t);
    float easeInOutQuad(float t);
};

struct KeybindSet {
    int menu = 0x42;
    int frameAdvance = 0x56;
    int frameStep = 0x43;
    int replayToggle = 0;
    int noclip = 0;
    int safeMode = 0;
    int trajectory = 0;
    int audioPitch = 0;
    int rngLock = 0;
    int hitboxes = 0;
};

class MenuInterface {
public:
    static MenuInterface* get();

    ImFont* fontBody = nullptr;
    ImFont* fontSmall = nullptr;
    ImFont* fontHeading = nullptr;
    ImFont* fontTitle = nullptr;

    bool shown = false;
    bool previouslyShown = false;
    bool validated = false;
    bool validationFailed = false;
    bool setupComplete = false;

    int activeTab = 0;
    int previousTab = -1;

    ThemeEngine theme;
    AnimationState anim;
    KeybindSet keybinds;

    int* rebindTarget = nullptr;

    char macroNameBuffer[256] = {0};
    bool macroNameReady = false;
    char rngBuffer[32] = "1";
    bool rngBufferInit = false;
    float tempTickRate = 240.0f;
    float tempGameSpeed = 1.0f;

    ImVec2 windowPos = ImVec2(-1, -1);
    bool windowPosInitialized = false;

    void initialize();
    void drawInterface();
    void saveSettings();
    void loadSettings();

private:
    void drawBackdrop();
    void drawCursorGlow();
    void drawMainWindow();
    void drawTitleBar();
    void drawTabBar();
    void drawTabContent();
    void drawStatusBar();

    void drawReplayTab();
    void drawToolsTab();
    void drawHacksTab();
    void drawSettingsTab();

    void switchTab(int newTab);
};

std::string getKeyName(int code);

namespace Widgets {
    bool ToggleSwitch(const char* label, bool* value, ThemeEngine& theme, AnimationState& anim);
    bool StyledButton(const char* label, ImVec2 size, ThemeEngine& theme, AnimationState& anim);
    bool StyledSliderFloat(const char* label, float* value, float min, float max, ThemeEngine& theme);
    bool StyledSliderInt(const char* label, int* value, int min, int max, ThemeEngine& theme);
    void SectionHeader(const char* text, ThemeEngine& theme);
    bool ModuleCard(const char* name, const char* description, bool* enabled, ThemeEngine& theme, AnimationState& anim);
    bool ModuleCardBegin(const char* name, const char* description, bool* enabled, ThemeEngine& theme, AnimationState& anim);
    void ModuleCardEnd();
    void StatusBadge(const char* text, ImVec4 color);
    bool PillButton(const char* label, bool active, float width, ThemeEngine& theme, AnimationState& anim);
    void KeybindButton(const char* label, int* keyCode, ThemeEngine& theme, AnimationState& anim);
}

void displayOverlayBranding();

#endif
