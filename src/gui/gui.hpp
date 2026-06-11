#ifndef _gui_hpp
#define _gui_hpp

#include <imgui-cocos.hpp>
#include <filesystem>
#include <future>
#include <unordered_map>
#include <string>
#include <vector>

#include "conversion/macro_converter.hpp"
#include "gui/frame_editor.hpp"
#include "render/render_preset.hpp"
#include "render/render_config.hpp"

struct ThemePreset {
    const char* name;
    ImVec4 accent, bg, card, textPrimary, textSecondary;
    float cornerRadius;
    float bgOpacity;
};

struct ThemeEngine {
    ImVec4 accentColor = ImVec4(0.15f, 0.80f, 0.75f, 1.0f);
    ImVec4 bgColor = ImVec4(0.05f, 0.08f, 0.09f, 0.92f);
    ImVec4 cardColor = ImVec4(0.08f, 0.13f, 0.14f, 1.0f);
    ImVec4 textPrimary = ImVec4(0.92f, 0.96f, 0.96f, 1.0f);
    ImVec4 textSecondary = ImVec4(0.48f, 0.58f, 0.58f, 1.0f);
    float bgOpacity = 0.90f;
    float cornerRadius = 5.0f;
    float textScale = 1.0f;
    bool glowCycleEnabled = false;
    float glowCycleRate = 0.5f;
    int activePreset = 7;

    ImVec4 computeCycleColor(float rate) const;
    ImVec4 getAccent() const;
    ImVec4 getGlowAccent() const;
    ImU32 getAccentU32(float alpha = 1.0f) const;
    ImU32 getAccentDimU32(float factor = 0.3f) const;
    ImU32 getTextU32() const;
    ImU32 getTextSecondaryU32() const;
    ImU32 getCardU32() const;
    void applyToImGuiStyle();
    void resetDefaults();
    void applyPreset(int index);
    static const ThemePreset* getPresets();
    static int getPresetCount();
};

enum AnimDirection {
    ANIM_CENTER,
    ANIM_FROM_LEFT,
    ANIM_FROM_RIGHT,
    ANIM_FROM_TOP,
    ANIM_FROM_BOTTOM
};

enum RenderWatermarkFont {
    RENDER_WATERMARK_FONT_NORMAL_PUSAB = 0,
    RENDER_WATERMARK_FONT_GOLD_PUSAB = 1
};

enum RenderWatermarkCorner {
    RENDER_WATERMARK_CORNER_TOP_LEFT = 0,
    RENDER_WATERMARK_CORNER_TOP_RIGHT = 1,
    RENDER_WATERMARK_CORNER_BOTTOM_LEFT = 2,
    RENDER_WATERMARK_CORNER_BOTTOM_RIGHT = 3
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
    int layoutMode = 0;
    int noMirror = 0;
    int autoclicker = 0;
    int disableShaders = 0;
    int clickSounds = 0;
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
    cocos2d::CCTexture2D* logoTexture = nullptr;
    bool prideLogoEnabled = false;

    int activeTab = 0;
    int previousTab = -1;
    int mainSubTab = 0;

    ThemeEngine theme;
    AnimationState anim;
    KeybindSet keybinds;

    float ambientTime = 0.0f;
    bool ambientWavesEnabled = true;

    int* rebindTarget = nullptr;
    std::string keybindConflictError;

    FrameEditor frameEditor;

    char macroNameBuffer[256] = {0};
    bool macroNameReady = false;
    char rngBuffer[32] = "1";
    bool rngBufferInit = false;
    float tempTickRate = 240.0f;
    float tempGameSpeed = 1.0f;

    int renderPresetIndex = 1;
    char renderNameBuf[256] = "";
    char renderWidthBuf[16] = "1920";
    char renderHeightBuf[16] = "1080";
    char renderFpsBuf[16] = "60";
    char renderCodecBuf[64] = "";
    char renderBitrateBuf[16] = "30";
    char renderExtBuf[16] = ".mp4";
    char renderArgsBuf[256] = "-pix_fmt yuv420p";
    char renderVideoArgsBuf[256] = "colorspace=all=bt709:iall=bt470bg:fast=1";
    char renderAudioArgsBuf[256] = "";
    char renderAudioCodecBuf[64] = "aac";
    char renderSecondsAfterBuf[16] = "3";
    bool renderIncludeAudio = true;
    bool renderIncludeClicks = false;
    float renderSfxVol = 1.0f;
    float renderMusicVol = 1.0f;
    bool renderHideEndscreen = false;
    bool renderHideLevelComplete = false;
    bool renderWatermarkEnabled = false;
    int renderWatermarkFont = RENDER_WATERMARK_FONT_NORMAL_PUSAB;
    int renderWatermarkCorner = RENDER_WATERMARK_CORNER_BOTTOM_RIGHT;
    float renderWatermarkScale = 1.0f;
    bool renderBufsInit = false;
    bool advancedWarningAccepted = false;
    bool showAdvancedWarning = false;
    bool showMkvWarning = false;
    bool mkvWarningIsExp = false;

    // Experimental render tab state
    RenderConfig expConfig;
    bool expConfigInit = false;
    bool expGpuProbed = false;
    bool expAdvancedOpen = false;
    bool expRendererRestartNotice = false;
    char expRenderNameBuf[256] = "";
    char expRenderFpsBuf[16] = "60";
    char expAdvCodecBuf[64] = "";
    char expAdvCrfBuf[8] = "";
    char expAdvMaxBitrateBuf[24] = "";
    char expAdvExtBuf[16] = "";
    char expAdvExtraArgsBuf[256] = "";
    char expAdvVideoArgsBuf[256] = "";
    char expAdvAudioArgsBuf[256] = "";
    char expAdvAudioCodecBuf[64] = "";
    char expAdvSecondsAfterBuf[16] = "3";
    int expRvDeleteConfirm = -1;
    char backupCodecBuf[64] = "";
    char backupBitrateBuf[16] = "30";
    char backupExtBuf[16] = ".mp4";
    char backupArgsBuf[256] = "-pix_fmt yuv420p";
    char backupVideoArgsBuf[256] = "colorspace=all=bt709:iall=bt470bg:fast=1";
    char backupAudioArgsBuf[256] = "";
    char backupAudioCodecBuf[64] = "aac";
    char backupSecondsAfterBuf[16] = "3";

    ImVec2 windowPos = ImVec2(-1, -1);
    bool windowPosInitialized = false;
    ImVec2 windowSize = ImVec2(580.0f, 540.0f);

    void initialize();
    void ensureLogoTexture();
    void drawInterface();
    void saveSettings();
    void loadSettings();

private:
    float tabIndicatorX = -1.0f;
    bool replayListDirty = true;
    bool replayRefreshQueued = true;
    bool replayDirTimeValid = false;
    std::filesystem::file_time_type replayDirLastWriteTime{};
    char replayRenameBuffer[256] = {0};
    std::string replayRenameOriginalName;
    std::string replayRenameError;
    bool replayRenamePopupRequested = false;
    bool replayRenameFocusInput = false;

    bool replayActionPopupRequested = false;
    std::string replayActionMacroName;
    bool replayActionIsTTR = false;
    bool replayActionIsLegacyCBS = false;
    bool replayActionCanEdit = true;
    bool replayLoadPending = false;
    std::string replayLoadPendingMacroName;
    bool replayLoadPendingIsTTR = false;
    double replayLoadReadyTime = 0.0;
    std::string replayConvertSelectedPath;
    char replayConvertNameBuffer[256] = {0};
    bool replayConvertTargetTTR = true;
    bool replayConvertRunning = false;
    bool replayConvertStatusOk = false;
    std::string replayConvertStatus;
    std::vector<std::string> replayConvertWarnings;
    std::future<toasty::conversion::ConversionResult> replayConvertFuture;
    bool replayConvertMarkSourceOnComplete = false;
    bool replayConvertSelectOutputOnComplete = false;
    bool replayConvertShowStandaloneStatus = false;
    std::string replayConvertSourceKeyOnComplete;
    bool replayConversionsExpanded = false;

    std::vector<std::string> presetNames;
    int presetSelectedIndex = -1;
    bool presetListDirty = true;
    char presetNameBuf[128] = "";
    std::string presetError;

    void drawBackdrop();
    void drawAmbientWaves(ImDrawList* dl, ImVec2 panelMin, ImVec2 panelMax);
    void drawMainWindow();
    void drawTitleBar();
    void drawTabBar();
    void drawTabContent();
    void drawStatusBar();
    void drawMainSubTabBar();

    void drawReplayTab();
    void drawToolsTab();
    void drawHacksTab();
    void drawRenderTab();
    void drawExpRenderTab();
    void drawClicksTab();
    void drawSettingsTab();
    void loadRenderSettings();
    void loadExpRenderSettings();
    void applyRenderPreset(RenderPreset const& preset);
    RenderPreset captureRenderPreset();
    void drawRenderPresetsSection();
    void drawRenderAudioSection();
    void drawRenderDisplaySection();
    void markReplayListDirty(bool queueRefresh = true);
    void refreshReplayListIfNeeded(bool force);
    bool hasReplayDirectoryChanged() const;
    void captureReplayDirectoryTimestamp();

    void switchTab(int newTab);

    int clickPackIndex = 0;
    int clickPackIndexP2 = 0;
    bool clickPacksScanned = false;

    void drawAutoclickerTab();
    void drawOnlineTab();
    char issueTitleBuf[128] = {0};
    char issueDescBuf[1501] = {0};
    int selectedUploadMacro = -1;
    char uploadCommentBuf[501] = {0};
};

std::string getKeyName(int code);

namespace Widgets {
    bool ToggleSwitch(const char* label, bool* value, ThemeEngine& theme, AnimationState& anim);
    bool StyledButton(const char* label, ImVec2 size, ThemeEngine& theme, AnimationState& anim, float roundingOverride = -1.0f);
    bool StyledSliderFloat(const char* label, float* value, float min, float max, ThemeEngine& theme, bool allowManualInput = false);
    bool StyledSliderInt(const char* label, int* value, int min, int max, ThemeEngine& theme);
    void SectionHeader(const char* text, ThemeEngine& theme);
    bool ModuleCard(const char* name, const char* description, bool* enabled, ThemeEngine& theme, AnimationState& anim, int* keybind = nullptr);
    bool ModuleCardBegin(const char* name, const char* description, bool* enabled, ThemeEngine& theme, AnimationState& anim, int* keybind = nullptr);
    void ModuleCardEnd();
    void StatusBadge(const char* text, ImVec4 color);
    bool PillButton(const char* label, bool active, float width, ThemeEngine& theme, AnimationState& anim);
    void KeybindButton(const char* label, int* keyCode, ThemeEngine& theme, AnimationState& anim);
}

#endif
