#include "gui/gui.hpp"
#include "i18n/localization.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "hacks/autoclicker.hpp"
#include "online/online_client.hpp"
#include "utils.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <array>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <cstdint>
#include <fmt/format.h>
#include <regex>
#include <system_error>
#include <vector>

using namespace geode::prelude;

static ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t
    );
}

static float smoothStep(float current, float target, float speed, float dt) {
    return current + (target - current) * std::min(1.0f, dt * speed);
}

static ImVec4 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

static ImVec4 brighten(const ImVec4& c, float amount) {
    return ImVec4(
        std::clamp(c.x + amount, 0.0f, 1.0f),
        std::clamp(c.y + amount, 0.0f, 1.0f),
        std::clamp(c.z + amount, 0.0f, 1.0f),
        c.w
    );
}

static ImU32 toU32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

static ImVec2 snapPos(ImVec2 p) {
    return ImVec2(std::round(p.x), std::round(p.y));
}

static size_t countMacroClicks(const MacroSequence* macro) {
    if (!macro) return 0;
    return static_cast<size_t>(std::count_if(macro->inputs.begin(), macro->inputs.end(), [](const MacroAction& action) {
        return action.down;
    }));
}

static const char* getAccuracyTag(AccuracyMode mode) {
    switch (mode) {
        case AccuracyMode::CBS:
            return "CBS";
        case AccuracyMode::CBF:
            return "CBF";
        default:
            return nullptr;
    }
}

static ImVec4 getAccuracyTagColor(AccuracyMode mode) {
    switch (mode) {
        case AccuracyMode::CBF:
            return ImVec4(1.0f, 0.22f, 0.22f, 1.0f);
        case AccuracyMode::CBS:
            return ImVec4(1.0f, 0.22f, 0.22f, 1.0f);
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static ImVec4 getTTRTagColor() {
    return ImVec4(0.30f, 0.70f, 1.0f, 1.0f);
}

static float sanitizeClamped(float value, float minValue, float maxValue, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minValue, maxValue);
}

static ImVec4 sanitizeColor(ImVec4 value, ImVec4 fallback) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) || !std::isfinite(value.w)) {
        return fallback;
    }
    float maxComp = std::max(std::max(value.x, value.y), std::max(value.z, value.w));
    if (maxComp > 1.0001f && maxComp <= 255.0f) {
        value.x /= 255.0f;
        value.y /= 255.0f;
        value.z /= 255.0f;
        value.w /= 255.0f;
    }
    value.x = std::clamp(value.x, 0.0f, 1.0f);
    value.y = std::clamp(value.y, 0.0f, 1.0f);
    value.z = std::clamp(value.z, 0.0f, 1.0f);
    value.w = std::clamp(value.w, 0.0f, 1.0f);
    return value;
}

template <class T>
static T loadSavedValueWithFallback(
    Mod* mod,
    std::string_view canonicalKey,
    T defaultValue,
    std::initializer_list<std::string_view> legacyKeys = {}
) {
    std::string canonicalKeyStr(canonicalKey);
    if (mod->hasSavedValue(canonicalKeyStr)) {
        return mod->getSavedValue<T>(canonicalKeyStr, defaultValue);
    }

    for (auto legacyKey : legacyKeys) {
        std::string legacyKeyStr(legacyKey);
        if (mod->hasSavedValue(legacyKeyStr)) {
            return mod->getSavedValue<T>(legacyKeyStr, defaultValue);
        }
    }

    return defaultValue;
}

static void drawSolidRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, const ThemeEngine& theme, float alpha, bool border = true) {
    ImVec4 fill(theme.cardColor.x, theme.cardColor.y, theme.cardColor.z, theme.cardColor.w * alpha);
    dl->AddRectFilled(min, max, toU32(fill), rounding);
    if (border)
        dl->AddRect(min, max, theme.getAccentU32(0.18f * alpha), rounding, 0, 1.0f);
}

static std::string trString(std::string_view key) {
    return std::string(toasty::i18n::tr(key));
}

static std::string trFormat(std::string_view key, fmt::format_args args) {
    return toasty::i18n::trf(key, args);
}

template <class... Args>
static std::string trFormat(std::string_view key, Args&&... args) {
    return toasty::i18n::trf(key, std::forward<Args>(args)...);
}

static std::string getLocalizedDisplayLabel(const char* label) {
    if (!label) {
        return {};
    }

    std::string_view raw(label);
    size_t suffixPos = raw.find("##");
    std::string_view display = suffixPos == std::string_view::npos
        ? raw
        : raw.substr(0, suffixPos);

    if (display.empty()) {
        return {};
    }

    return trString(display);
}

static void imguiTextTr(std::string_view key) {
    auto text = trString(key);
    ImGui::TextUnformatted(text.c_str());
}

static void imguiTextWrappedTr(std::string_view key) {
    auto text = trString(key);
    ImGui::TextWrapped("%s", text.c_str());
}

namespace {
    static std::filesystem::path getReplayDirectoryPath() {
        return ReplayStorage::getReplayDirectoryPath();
    }

    static bool readReplayDirectoryTimestamp(std::filesystem::file_time_type& outTime, bool& valid) {
        std::error_code ec;
        auto replayDir = getReplayDirectoryPath();
        if (!std::filesystem::exists(replayDir, ec) || ec) {
            valid = false;
            return false;
        }

        outTime = std::filesystem::last_write_time(replayDir, ec);
        valid = !ec;
        return valid;
    }

    static void drawPopupChrome(MenuInterface& ui, const char* title, float rounding = 0.0f, float titleBandHeight = 28.0f) {
        ImVec2 winPos = snapPos(ImGui::GetWindowPos());
        ImVec2 winSize = snapPos(ImGui::GetWindowSize());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 winMax = ImVec2(winPos.x + winSize.x, winPos.y + winSize.y);

        drawSolidRect(dl, winPos, winMax, rounding, ui.theme, 0.72f, false);
        fg->AddRect(
            winPos,
            winMax,
            ui.theme.getAccentU32(0.36f),
            rounding,
            0,
            1.0f
        );

        float textY = winPos.y + 12.0f;
        dl->AddText(
            ImVec2(winPos.x + 14.0f, textY),
            ui.theme.getTextU32(),
            title
        );
        float dividerY = textY + ImGui::GetFontSize() + 10.0f;
        dl->AddLine(
            ImVec2(winPos.x + 1.0f, dividerY),
            ImVec2(winPos.x + winSize.x - 1.0f, dividerY),
            ui.theme.getAccentU32(0.30f),
            1.0f
        );

        ImGui::Dummy(ImVec2(0.0f, titleBandHeight + 8.0f));
    }

    static std::string sanitizeReplayName(std::string name) {
        return ReplayStorage::sanitizeReplayName(std::move(name));
    }

    static bool renameStoredReplayFile(const std::string& oldName, const std::string& requestedName, std::string& finalName, std::string& error) {
        error.clear();

        auto sanitizedName = sanitizeReplayName(requestedName);
        if (sanitizedName.empty()) {
            error = "Name cannot be empty.";
            return false;
        }

        auto replayDir = getReplayDirectoryPath();
        std::error_code ec;
        if (!std::filesystem::exists(replayDir, ec) || ec) {
            error = "Replay folder is unavailable.";
            return false;
        }

        std::filesystem::path sourcePath;
        for (auto const& entry : std::filesystem::directory_iterator(replayDir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            if (toasty::pathToUtf8(entry.path().stem()) == oldName) {
                sourcePath = entry.path();
                break;
            }
        }

        if (sourcePath.empty()) {
            error = "Replay file was not found.";
            return false;
        }

        std::string resolvedName = ReplayStorage::makeUniqueReplayName(sanitizedName, oldName);
        auto destPath = sourcePath.parent_path() / (resolvedName + toasty::pathToUtf8(sourcePath.extension()));
        if (destPath == sourcePath) {
            finalName = resolvedName;
            return true;
        }

        std::filesystem::rename(sourcePath, destPath, ec);
        if (ec) {
            error = "Failed to rename replay.";
            return false;
        }

        finalName = resolvedName;
        return true;
    }

}


ImVec4 ThemeEngine::computeCycleColor(float rate) const {
    static float hueVal = 0.0f;
    hueVal += rate * ImGui::GetIO().DeltaTime * 0.03f;
    if (hueVal > 1.0f) hueVal -= 1.0f;

    float h = hueVal * 6.0f;
    float c = 1.0f;
    float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));

    float r = 0, g = 0, b = 0;
    if (h < 1.0f) { r = c; g = x; }
    else if (h < 2.0f) { r = x; g = c; }
    else if (h < 3.0f) { g = c; b = x; }
    else if (h < 4.0f) { g = x; b = c; }
    else if (h < 5.0f) { r = x; b = c; }
    else { r = c; b = x; }

    return ImVec4(r, g, b, 1.0f);
}

ImVec4 ThemeEngine::getAccent() const {
    return glowCycleEnabled ? computeCycleColor(glowCycleRate) : accentColor;
}

ImVec4 ThemeEngine::getGlowAccent() const {
    return glowCycleEnabled ? computeCycleColor(glowCycleRate) : accentColor;
}

ImU32 ThemeEngine::getAccentU32(float alpha) const {
    ImVec4 c = getAccent();
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 ThemeEngine::getAccentDimU32(float factor) const {
    ImVec4 c = getAccent();
    c.x *= factor; c.y *= factor; c.z *= factor;
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 ThemeEngine::getTextU32() const { return ImGui::ColorConvertFloat4ToU32(textPrimary); }
ImU32 ThemeEngine::getTextSecondaryU32() const { return ImGui::ColorConvertFloat4ToU32(textSecondary); }
ImU32 ThemeEngine::getCardU32() const { return ImGui::ColorConvertFloat4ToU32(cardColor); }

void ThemeEngine::applyToImGuiStyle() {
    ImGuiStyle* s = &ImGui::GetStyle();
    ImVec4 accent = getAccent();

    s->WindowPadding = ImVec2(18, 18);
    s->WindowRounding = cornerRadius;
    s->FramePadding = ImVec2(11, 8);
    s->FrameRounding = cornerRadius;
    s->ItemSpacing = ImVec2(10, 9);
    s->ItemInnerSpacing = ImVec2(8, 7);
    s->IndentSpacing = 20.0f;
    s->ScrollbarSize = 11.0f;
    s->ScrollbarRounding = cornerRadius;
    s->GrabMinSize = 9.0f;
    s->GrabRounding = cornerRadius;
    s->WindowBorderSize = 1.0f;
    s->FrameBorderSize = 1.0f;
    s->PopupBorderSize = 1.0f;

    ImVec4 winBg = ImVec4(
        0.06f + bgColor.x * 0.12f,
        0.06f + bgColor.y * 0.14f,
        0.08f + bgColor.z * 0.16f,
        0.35f
    );

    ImVec4 frameBase = ImVec4(0.14f, 0.14f, 0.18f, 0.55f);
    ImVec4 frameHover = ImVec4(accent.x * 0.3f + 0.12f, accent.y * 0.3f + 0.12f, accent.z * 0.3f + 0.14f, 0.65f);
    ImVec4 frameActive = ImVec4(accent.x * 0.4f + 0.10f, accent.y * 0.4f + 0.10f, accent.z * 0.4f + 0.12f, 0.75f);

    s->Colors[ImGuiCol_Text] = textPrimary;
    s->Colors[ImGuiCol_TextDisabled] = textSecondary;
    s->Colors[ImGuiCol_WindowBg] = winBg;
    s->Colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.06f, 0.08f, 0.92f);
    s->Colors[ImGuiCol_Border] = ImVec4(accent.x * 0.4f, accent.y * 0.4f, accent.z * 0.4f, 0.20f);
    s->Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    s->Colors[ImGuiCol_FrameBg] = frameBase;
    s->Colors[ImGuiCol_FrameBgHovered] = frameHover;
    s->Colors[ImGuiCol_FrameBgActive] = frameActive;
    s->Colors[ImGuiCol_CheckMark] = accent;
    s->Colors[ImGuiCol_SliderGrab] = accent;
    s->Colors[ImGuiCol_SliderGrabActive] = brighten(accent, 0.15f);
    s->Colors[ImGuiCol_Button] = frameBase;
    s->Colors[ImGuiCol_ButtonHovered] = frameHover;
    s->Colors[ImGuiCol_ButtonActive] = frameActive;
    s->Colors[ImGuiCol_Header] = ImVec4(accent.x * 0.2f + 0.08f, accent.y * 0.2f + 0.08f, accent.z * 0.2f + 0.10f, 0.42f);
    s->Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x * 0.3f + 0.08f, accent.y * 0.3f + 0.08f, accent.z * 0.3f + 0.10f, 0.52f);
    s->Colors[ImGuiCol_HeaderActive] = ImVec4(accent.x * 0.4f + 0.08f, accent.y * 0.4f + 0.08f, accent.z * 0.4f + 0.10f, 0.62f);
    s->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.04f, 0.06f, 0.30f);
    s->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(accent.x * 0.5f, accent.y * 0.5f, accent.z * 0.5f, 0.40f);
    s->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(accent.x * 0.6f, accent.y * 0.6f, accent.z * 0.6f, 0.50f);
    s->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(accent.x * 0.7f, accent.y * 0.7f, accent.z * 0.7f, 0.60f);
    s->Colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    s->Colors[ImGuiCol_Separator] = ImVec4(accent.x * 0.3f, accent.y * 0.3f, accent.z * 0.3f, 0.20f);
    s->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.01f, 0.02f, 0.03f, 0.58f);
}

void ThemeEngine::resetDefaults() {
    applyPreset(7);
    textScale = 1.0f;
    glowCycleEnabled = false;
    glowCycleRate = 0.5f;
}

static const ThemePreset kThemePresets[] = {
    { "Dark Purple",
      ImVec4(0.65f, 0.20f, 0.85f, 1.0f),
      ImVec4(0.08f, 0.08f, 0.10f, 0.92f),
      ImVec4(0.12f, 0.12f, 0.15f, 1.0f),
      ImVec4(0.93f, 0.93f, 0.95f, 1.0f),
      ImVec4(0.55f, 0.55f, 0.60f, 1.0f),
      5.0f, 0.90f },
    { "Midnight Blue",
      ImVec4(0.25f, 0.52f, 0.96f, 1.0f),
      ImVec4(0.06f, 0.07f, 0.12f, 0.92f),
      ImVec4(0.09f, 0.10f, 0.16f, 1.0f),
      ImVec4(0.92f, 0.94f, 0.98f, 1.0f),
      ImVec4(0.50f, 0.55f, 0.65f, 1.0f),
      5.0f, 0.92f },
    { "Rose",
      ImVec4(0.92f, 0.30f, 0.50f, 1.0f),
      ImVec4(0.10f, 0.06f, 0.08f, 0.92f),
      ImVec4(0.15f, 0.09f, 0.11f, 1.0f),
      ImVec4(0.96f, 0.92f, 0.93f, 1.0f),
      ImVec4(0.60f, 0.50f, 0.52f, 1.0f),
      5.0f, 0.90f },
    { "Monochrome",
      ImVec4(0.70f, 0.70f, 0.70f, 1.0f),
      ImVec4(0.08f, 0.08f, 0.08f, 0.92f),
      ImVec4(0.14f, 0.14f, 0.14f, 1.0f),
      ImVec4(0.90f, 0.90f, 0.90f, 1.0f),
      ImVec4(0.50f, 0.50f, 0.50f, 1.0f),
      4.0f, 0.92f },
    { "Megahack",
      ImVec4(0.91f, 0.27f, 0.60f, 1.0f),
      ImVec4(0.07f, 0.07f, 0.07f, 0.92f),
      ImVec4(0.11f, 0.12f, 0.11f, 1.0f),
      ImVec4(0.92f, 0.95f, 0.92f, 1.0f),
      ImVec4(0.48f, 0.55f, 0.50f, 1.0f),
      4.0f, 0.90f },
    { "Sunset",
      ImVec4(0.95f, 0.55f, 0.15f, 1.0f),
      ImVec4(0.10f, 0.07f, 0.05f, 0.92f),
      ImVec4(0.15f, 0.10f, 0.07f, 1.0f),
      ImVec4(0.96f, 0.94f, 0.90f, 1.0f),
      ImVec4(0.60f, 0.52f, 0.45f, 1.0f),
      5.0f, 0.88f },
    { "Crimson",
      ImVec4(0.85f, 0.15f, 0.20f, 1.0f),
      ImVec4(0.09f, 0.06f, 0.06f, 0.92f),
      ImVec4(0.14f, 0.09f, 0.09f, 1.0f),
      ImVec4(0.95f, 0.92f, 0.92f, 1.0f),
      ImVec4(0.58f, 0.50f, 0.50f, 1.0f),
      5.0f, 0.90f },
    { "Aqua",
      ImVec4(0.15f, 0.80f, 0.75f, 1.0f),
      ImVec4(0.05f, 0.08f, 0.09f, 0.92f),
      ImVec4(0.08f, 0.13f, 0.14f, 1.0f),
      ImVec4(0.92f, 0.96f, 0.96f, 1.0f),
      ImVec4(0.48f, 0.58f, 0.58f, 1.0f),
      5.0f, 0.90f },
};

void ThemeEngine::applyPreset(int index) {
    if (index < 0 || index >= getPresetCount()) return;
    const auto& p = kThemePresets[index];
    accentColor = p.accent;
    bgColor = p.bg;
    cardColor = p.card;
    textPrimary = p.textPrimary;
    textSecondary = p.textSecondary;
    cornerRadius = p.cornerRadius;
    bgOpacity = p.bgOpacity;
    activePreset = index;
}

const ThemePreset* ThemeEngine::getPresets() { return kThemePresets; }
int ThemeEngine::getPresetCount() { return sizeof(kThemePresets) / sizeof(kThemePresets[0]); }

float AnimationState::easeOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

float AnimationState::easeInOutQuad(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 2.0f * t * t : 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
}

void AnimationState::update(float dt) {
    if (dt > 0.05f) dt = 0.05f;
    float step = dt * animSpeed;

    if (opening) {
        openProgress += step;
        if (openProgress >= 1.0f) { openProgress = 1.0f; opening = false; }
    }
    if (closing) {
        openProgress -= step;
        if (openProgress <= 0.0f) openProgress = 0.0f;
    }

    if (tabTransition < 1.0f) {
        tabTransition += step * 1.5f;
        if (tabTransition >= 1.0f) tabTransition = 1.0f;
    }
}

void MenuInterface::markReplayListDirty(bool queueRefresh) {
    replayListDirty = true;
    if (queueRefresh) {
        replayRefreshQueued = true;
    }
}

bool MenuInterface::hasReplayDirectoryChanged() const {
    std::filesystem::file_time_type currentTime{};
    bool valid = false;
    readReplayDirectoryTimestamp(currentTime, valid);

    if (valid != replayDirTimeValid) {
        return true;
    }

    return valid && currentTime != replayDirLastWriteTime;
}

void MenuInterface::captureReplayDirectoryTimestamp() {
    std::filesystem::file_time_type currentTime{};
    bool valid = false;
    readReplayDirectoryTimestamp(currentTime, valid);
    replayDirTimeValid = valid;
    replayDirLastWriteTime = currentTime;
}

void MenuInterface::refreshReplayListIfNeeded(bool force) {
    auto* engine = ReplayEngine::get();
    if (!engine) return;

    bool directoryChanged = hasReplayDirectoryChanged();
    if (!force && !replayListDirty && !directoryChanged) {
        replayRefreshQueued = false;
        return;
    }

    engine->reloadMacroList();
    replayListDirty = false;
    replayRefreshQueued = false;
    captureReplayDirectoryTimestamp();
}

MenuInterface* MenuInterface::get() {
    static MenuInterface* singleton = new MenuInterface();
    return singleton;
}

void ReplayEngine::processHotkeys() {}

std::string getKeyName(int code) {
    if (code == 0) return "None";
    if (code >= 65 && code <= 90) return std::string(1, (char)code);
    if (code >= 48 && code <= 57) return std::string(1, (char)code);
    if (code >= 112 && code <= 123) return "F" + std::to_string(code - 111);
    if (code == 32) return "Space";
    if (code == 8) return "Bksp";
    if (code == 9) return "Tab";
    if (code == 13) return "Enter";
    if (code == 16) return "Shift";
    if (code == 17) return "Ctrl";
    if (code == 18) return "Alt";
    if (code == 27) return "Esc";
    if (code == 37) return "Left";
    if (code == 38) return "Up";
    if (code == 39) return "Right";
    if (code == 40) return "Down";
    if (code == 46) return "Del";
    if (code == 45) return "Ins";
    if (code == 36) return "Home";
    if (code == 35) return "End";
    if (code == 33) return "PgUp";
    if (code == 34) return "PgDn";
    if (code == 192) return "`";
    if (code == 189) return "-";
    if (code == 187) return "=";
    if (code == 219) return "[";
    if (code == 221) return "]";
    if (code == 220) return "\\";
    if (code == 186) return ";";
    if (code == 222) return "'";
    if (code == 188) return ",";
    if (code == 190) return ".";
    if (code == 191) return "/";
    return "Key" + std::to_string(code);
}

static std::string checkKeybindConflict(int* target, int newKey, const KeybindSet& keybinds) {
    if (newKey == 0) return "";
    struct Entry { const char* name; const int* ptr; };
    Entry entries[] = {
        {"Menu Toggle",   &keybinds.menu},
        {"Frame Advance", &keybinds.frameAdvance},
        {"Frame Step",    &keybinds.frameStep},
        {"Replay Toggle", &keybinds.replayToggle},
        {"Noclip",        &keybinds.noclip},
        {"Safe Mode",     &keybinds.safeMode},
        {"Trajectory",    &keybinds.trajectory},
        {"Audio Pitch",   &keybinds.audioPitch},
        {"RNG Lock",      &keybinds.rngLock},
        {"Hitboxes",      &keybinds.hitboxes},
        {"Layout Mode",   &keybinds.layoutMode},
        {"No Mirror",     &keybinds.noMirror},
    };
    for (auto& e : entries) {
        if (e.ptr != target && *e.ptr == newKey) {
            return trFormat(
                "Already bound to {keybind}",
                fmt::arg("keybind", toasty::i18n::tr(e.name))
            );
        }
    }
    return "";
}

namespace Widgets {

bool ToggleSwitch(const char* label, bool* value, ThemeEngine& theme, AnimationState& anim) {
    ImGui::PushID(value);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 accent = theme.getAccent();
    float dt = ImGui::GetIO().DeltaTime;
    std::string displayLabel = getLocalizedDisplayLabel(label);

    const float width = 44.0f;
    const float height = 22.0f;
    const float radius = height * 0.5f;
    float labelWidth = ImGui::CalcTextSize(displayLabel.c_str()).x;

    ImGui::InvisibleButton(label, ImVec2(width + 12.0f + labelWidth, height));
    ImGuiID id = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    if (clicked) {
        *value = !*value;
    }

    float& toggleT = anim.toggleAnims[id];
    float& hoverT = anim.hoverAnims[id];
    toggleT = smoothStep(toggleT, *value ? 1.0f : 0.0f, 13.0f, dt);
    hoverT = smoothStep(hoverT, hovered ? 1.0f : 0.0f, 11.0f, dt);

    ImVec4 offCol = ImVec4(0.18f, 0.18f, 0.22f, 0.50f);
    ImVec4 onCol = ImVec4(
        accent.x * 0.5f + 0.05f,
        accent.y * 0.5f + 0.05f,
        accent.z * 0.5f + 0.05f,
        0.60f
    );
    ImVec4 track = lerpColor(offCol, onCol, toggleT);
    if (hoverT > 0.0f) {
        track = brighten(track, hoverT * 0.03f);
    }

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), toU32(track), radius);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), toU32(ImVec4(accent.x * 0.4f, accent.y * 0.4f, accent.z * 0.4f, 0.25f)), radius, 0, 1.0f);

    float knobX = pos.x + radius + toggleT * (width - height);
    ImVec2 knobCenter(knobX, pos.y + radius);
    dl->AddCircleFilled(knobCenter, radius - 2.0f, IM_COL32(220, 222, 230, 240));
    dl->AddCircle(knobCenter, radius - 2.0f, theme.getAccentU32(0.30f + toggleT * 0.40f), 0, 1.0f);

    ImU32 textCol = hovered ? theme.getTextU32() : theme.getTextSecondaryU32();
    dl->AddText(ImVec2(pos.x + width + 12.0f, pos.y + 2.0f), textCol, displayLabel.c_str());

    ImGui::PopID();
    return clicked;
}

bool StyledButton(const char* label, ImVec2 size, ThemeEngine& theme, AnimationState& anim, float roundingOverride) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 accent = theme.getAccent();
    float dt = ImGui::GetIO().DeltaTime;
    std::string displayLabel = getLocalizedDisplayLabel(label);

    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = 32.0f;

    ImGui::InvisibleButton(label, size);
    ImGuiID id = ImGui::GetItemID();
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    float& hoverT = anim.hoverAnims[id];
    hoverT = smoothStep(hoverT, hovered ? 1.0f : 0.0f, 12.0f, dt);

    float rounding = roundingOverride >= 0.0f
        ? roundingOverride
        : theme.cornerRadius;
    float press = held ? 0.92f : 1.0f;
    ImVec2 scaled = ImVec2(size.x * press, size.y * press);
    ImVec2 shift = ImVec2((size.x - scaled.x) * 0.5f, (size.y - scaled.y) * 0.5f);
    ImVec2 bMin = ImVec2(pos.x + shift.x, pos.y + shift.y);
    ImVec2 bMax = ImVec2(bMin.x + scaled.x, bMin.y + scaled.y);

    drawSolidRect(dl, bMin, bMax, rounding, theme, 1.0f + hoverT * 0.6f);

    ImVec2 textSize = ImGui::CalcTextSize(displayLabel.c_str());
    ImVec2 textPos(
        bMin.x + (scaled.x - textSize.x) * 0.5f,
        bMin.y + (scaled.y - textSize.y) * 0.5f
    );
    dl->AddText(
        textPos,
        hovered ? toU32(brighten(theme.textPrimary, 0.05f)) : theme.getTextU32(),
        displayLabel.c_str()
    );

    return clicked;
}

bool StyledSliderFloat(const char* label, float* value, float vmin, float vmax, ThemeEngine& theme, bool allowManualInput) {
    ImGui::PushID(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float availWidth = ImGui::GetContentRegionAvail().x - 10.0f;
    float dt = ImGui::GetIO().DeltaTime;
    ImVec4 accent = theme.getAccent();
    std::string displayLabel = getLocalizedDisplayLabel(label);

    float displayVal = *value;
    float sliderFrac = std::clamp((*value - vmin) / std::max(vmax - vmin, FLT_EPSILON), 0.0f, 1.0f);

    char valBuf[64];
    if (std::fabs(vmax - vmin) > 10.0f)
        snprintf(valBuf, sizeof(valBuf), "%.0f", displayVal);
    else
        snprintf(valBuf, sizeof(valBuf), "%.2f", displayVal);

    dl->AddText(pos, theme.getTextSecondaryU32(), displayLabel.c_str());

    if (allowManualInput) {
        float inputW = 52.0f;
        float inputX = pos.x + availWidth - inputW;
        ImGui::SetCursorScreenPos(ImVec2(inputX, pos.y - 2.0f));
        ImGui::SetNextItemWidth(inputW);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.getTextU32() ? ImGui::ColorConvertU32ToFloat4(theme.getTextU32()) : ImVec4(0.95f, 0.96f, 0.99f, 1.0f));
        if (ImGui::InputFloat("##val", value, 0.0f, 0.0f, "%.2f")) {
            if (*value < 0.0f) *value = 0.0f;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        sliderFrac = std::clamp((*value - vmin) / std::max(vmax - vmin, FLT_EPSILON), 0.0f, 1.0f);
    } else {
        ImVec2 valSize = ImGui::CalcTextSize(valBuf);
        dl->AddText(ImVec2(pos.x + availWidth - valSize.x, pos.y), theme.getTextU32(), valBuf);
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 20.0f));

    pos = ImGui::GetCursorScreenPos();
    const float trackH = 6.0f;
    const float knobR = 8.0f;
    float trackY = pos.y + knobR;

    ImGui::InvisibleButton("##slider", ImVec2(availWidth, knobR * 2.0f + 6.0f));
    ImGuiID id = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    if (active) {
        float mouseX = ImGui::GetIO().MousePos.x;
        sliderFrac = std::clamp((mouseX - pos.x) / std::max(availWidth, 1.0f), 0.0f, 1.0f);
        *value = vmin + sliderFrac * (vmax - vmin);
    }

    static std::unordered_map<ImGuiID, float> smoothFrac;
    static std::unordered_map<ImGuiID, float> hoverAnim;
    float& animFrac = smoothFrac[id];
    float& hoverT = hoverAnim[id];
    animFrac = smoothStep(animFrac, sliderFrac, 14.0f, dt);
    hoverT = smoothStep(hoverT, (hovered || active) ? 1.0f : 0.0f, 12.0f, dt);

    float fillX = pos.x + animFrac * availWidth;
    ImVec2 trackMin(pos.x, trackY - trackH * 0.5f);
    ImVec2 trackMax(pos.x + availWidth, trackY + trackH * 0.5f);

    ImVec4 trackBaseTop = ImVec4(1.0f, 1.0f, 1.0f, 0.18f);
    ImVec4 trackBaseBottom = ImVec4(0.92f, 0.95f, 1.0f, 0.12f);
    dl->AddRectFilledMultiColor(trackMin, trackMax, toU32(trackBaseTop), toU32(trackBaseTop), toU32(trackBaseBottom), toU32(trackBaseBottom));
    dl->AddRect(trackMin, trackMax, toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.12f)), trackH * 0.5f, 0, 1.0f);

    ImVec2 fillMin = trackMin;
    ImVec2 fillMax(fillX, trackMax.y);
    ImVec4 fillTop = withAlpha(accent, 0.74f);
    ImVec4 fillBottom = withAlpha(brighten(accent, -0.06f), 0.74f);
    dl->AddRectFilledMultiColor(fillMin, fillMax, toU32(fillTop), toU32(fillTop), toU32(fillBottom), toU32(fillBottom));

    float haloR = knobR + hoverT * 5.0f;
    dl->AddCircleFilled(ImVec2(fillX, trackY), haloR, theme.getAccentU32(0.14f + hoverT * 0.12f));
    dl->AddCircleFilled(ImVec2(fillX, trackY), knobR, IM_COL32(245, 249, 255, 245));
    dl->AddCircle(ImVec2(fillX, trackY), knobR, theme.getAccentU32(0.5f + hoverT * 0.4f), 0, 1.2f);

    ImGui::PopID();
    return active;
}

bool StyledSliderInt(const char* label, int* value, int vmin, int vmax, ThemeEngine& theme) {
    float fv = (float)*value;
    bool changed = StyledSliderFloat(label, &fv, (float)vmin, (float)vmax, theme);
    *value = (int)std::round(fv);
    return changed;
}

void SectionHeader(const char* text, ThemeEngine& theme) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    std::string displayText = getLocalizedDisplayLabel(text);
    ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
    ImVec4 accent = theme.getAccent();

    dl->AddText(pos, theme.getAccentU32(0.96f), displayText.c_str());
    float lineY = pos.y + textSize.y + 5.0f;
    float leftW = std::min(120.0f, width * 0.35f);
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x, lineY),
        ImVec2(pos.x + leftW, lineY + 2.0f),
        theme.getAccentU32(0.74f),
        theme.getAccentU32(0.28f),
        theme.getAccentU32(0.28f),
        theme.getAccentU32(0.74f)
    );
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x + leftW, lineY),
        ImVec2(pos.x + width, lineY + 1.0f),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f + accent.x * 0.04f)),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f + accent.z * 0.04f))
    );

    ImGui::Dummy(ImVec2(0, textSize.y + 11.0f));
}

bool ModuleCard(const char* name, const char* description, bool* enabled,
                ThemeEngine& theme, AnimationState& anim, int* keybind) {
    ImGui::PushID(enabled);
    ImGuiID id = ImGui::GetID(name);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    float height = description ? 56.0f : 42.0f;
    float dt = ImGui::GetIO().DeltaTime;
    ImVec4 accent = theme.getAccent();
    float rounding = theme.cornerRadius;
    std::string displayName = getLocalizedDisplayLabel(name);
    std::string displayDescription = description ? getLocalizedDisplayLabel(description) : std::string();

    ImGui::InvisibleButton(name, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(0);
    if (clicked) {
        *enabled = !*enabled;
    }

    float& hoverT = anim.hoverAnims[id];
    float& toggleT = anim.toggleAnims[id];
    hoverT = smoothStep(hoverT, hovered ? 1.0f : 0.0f, 10.0f, dt);
    toggleT = smoothStep(toggleT, *enabled ? 1.0f : 0.0f, 12.0f, dt);

    ImVec2 cardMin = pos;
    ImVec2 cardMax(pos.x + width, pos.y + height);
    drawSolidRect(dl, cardMin, cardMax, rounding, theme, 1.0f + hoverT * 0.7f);
    if (*enabled) {
        dl->AddRect(cardMin, cardMax, theme.getAccentU32(0.36f + hoverT * 0.22f), rounding, 0, 1.2f);
    }

    ImU32 nameCol = *enabled ? theme.getAccentU32(0.98f) : theme.getTextU32();
    dl->AddText(ImVec2(pos.x + 14.0f, pos.y + (description ? 9.0f : 12.0f)), nameCol, displayName.c_str());

    if (description) {
        dl->AddText(ImVec2(pos.x + 14.0f, pos.y + 31.0f), theme.getTextSecondaryU32(), displayDescription.c_str());
    }

    float toggleW = 40.0f, toggleH = 20.0f;
    float toggleX = pos.x + width - toggleW - 12;
    float toggleY = pos.y + (height - toggleH) / 2;
    float toggleR = toggleH * 0.5f;

    ImVec4 trackCol = lerpColor(ImVec4(0.93f, 0.96f, 1.0f, 0.20f), withAlpha(accent, 0.30f), toggleT);
    dl->AddRectFilled(
        ImVec2(toggleX, toggleY), ImVec2(toggleX + toggleW, toggleY + toggleH),
        toU32(trackCol), toggleR
    );
    dl->AddRect(ImVec2(toggleX, toggleY), ImVec2(toggleX + toggleW, toggleY + toggleH), toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.16f)), toggleR, 0, 1.0f);

    float knobX = toggleX + toggleR + toggleT * (toggleW - toggleH);
    dl->AddCircleFilled(ImVec2(knobX, toggleY + toggleR), toggleR - 2.0f, IM_COL32(247, 250, 255, 245));
    dl->AddCircle(ImVec2(knobX, toggleY + toggleR), toggleR - 2.0f, theme.getAccentU32(0.3f + toggleT * 0.3f), 0, 1.0f);
    (void)hovered;
    (void)keybind;

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PopID();
    return clicked;
}

static const void* activeModuleKey = nullptr;

bool ModuleCardBegin(const char* name, const char* description, bool* enabled,
                     ThemeEngine& theme, AnimationState& anim, int* keybind) {
    bool clicked = ModuleCard(name, description, enabled, theme, anim, keybind);

    auto& data = anim.moduleAnims[(const void*)enabled];
    float target = *enabled ? 1.0f : 0.0f;
    float speed = ImGui::GetIO().DeltaTime * anim.animSpeed;
    if (data.progress < target) data.progress = std::min(data.progress + speed, target);
    else if (data.progress > target) data.progress = std::max(data.progress - speed, target);

    if (data.progress <= 0.0f) return false;

    activeModuleKey = (const void*)enabled;
    float t = anim.easeOutCubic(data.progress);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    char childId[128];
    snprintf(childId, sizeof(childId), "##mod_%s", name);
    float childH = (data.progress >= 0.99f) ? 0.0f : (data.height > 0.0f ? data.height * t : 280.0f * t);
    ImGui::BeginChild(childId, ImVec2(-1, childH), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Indent(14);
    ImGui::Dummy(ImVec2(0, 4));

    if (keybind) {
        KeybindButton("Keybind", keybind, theme, anim);
        auto* ui = MenuInterface::get();
        if (ui && !ui->keybindConflictError.empty() && ui->rebindTarget != keybind) {
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", ui->keybindConflictError.c_str());
        }
        ImGui::Dummy(ImVec2(0, 6));
    }

    return true;
}

void ModuleCardEnd() {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Unindent(14);

    if (activeModuleKey) {
        auto& iface = *MenuInterface::get();
        auto it = iface.anim.moduleAnims.find(activeModuleKey);
        if (it != iface.anim.moduleAnims.end()) {
            float measured = ImGui::GetCursorPosY();
            if (it->second.progress >= 0.99f)
                it->second.height = measured;
        }
        activeModuleKey = nullptr;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void StatusBadge(const char* text, ImVec4 color) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::string displayText = getLocalizedDisplayLabel(text);
    ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
    float padX = 9.0f, padY = 4.0f;

    ImVec4 bgCol(color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 0.85f);
    dl->AddRectFilled(pos, ImVec2(pos.x + textSize.x + padX * 2, pos.y + textSize.y + padY * 2),
        toU32(bgCol), 999.0f);
    dl->AddRect(pos, ImVec2(pos.x + textSize.x + padX * 2, pos.y + textSize.y + padY * 2),
        toU32(withAlpha(color, 0.95f)), 999.0f, 0, 1.0f);
    dl->AddText(ImVec2(pos.x + padX, pos.y + padY), toU32(withAlpha(color, 0.98f)), displayText.c_str());

    ImGui::Dummy(ImVec2(textSize.x + padX * 2, textSize.y + padY * 2));
}

bool PillButton(const char* label, bool active, float width, ThemeEngine& theme, AnimationState& anim) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float height = 32.0f;
    float dt = ImGui::GetIO().DeltaTime;
    float rounding = height * 0.5f;
    ImVec4 accent = theme.getAccent();
    std::string displayLabel = getLocalizedDisplayLabel(label);

    ImGui::InvisibleButton(label, ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    float& hoverT = anim.hoverAnims[id];
    hoverT = smoothStep(hoverT, hovered ? 1.0f : 0.0f, 12.0f, dt);

    ImVec2 pMax(pos.x + width, pos.y + height);
    drawSolidRect(dl, pos, pMax, rounding, theme, 1.0f + hoverT * 0.6f + (active ? 0.2f : 0.0f));
    if (held) {
        dl->AddRectFilled(pos, pMax, toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.06f)), rounding);
    }
    if (active) {
        dl->AddRectFilled(ImVec2(pos.x + 12.0f, pMax.y - 2.0f), ImVec2(pMax.x - 12.0f, pMax.y), theme.getAccentU32(0.92f), 4.0f);
    }

    ImVec2 textSize = ImGui::CalcTextSize(displayLabel.c_str());
    ImVec2 textPos(pos.x + (width - textSize.x) * 0.5f, pos.y + (height - textSize.y) * 0.5f);
    ImU32 textCol = active ? toU32(ImVec4(0.97f, 0.99f, 1.0f, 0.98f)) : (hovered ? theme.getTextU32() : theme.getTextSecondaryU32());
    dl->AddText(textPos, textCol, displayLabel.c_str());

    return clicked;
}

void KeybindButton(const char* label, int* keyCode, ThemeEngine& theme, AnimationState& anim) {
    MenuInterface* ui = MenuInterface::get();
    ImGui::PushID(keyCode);
    std::string displayLabel = getLocalizedDisplayLabel(label);

    
    static int* settingsRebindActive = nullptr;
    static int settingsRebindBackup = 0;

    bool isRebinding = (ui->rebindTarget == keyCode);

    
    if (settingsRebindActive == keyCode && !isRebinding) {
        int newKey = *keyCode;
        bool changed = newKey != settingsRebindBackup;
        if (newKey != 0 && newKey != settingsRebindBackup) {
            std::string conflict = checkKeybindConflict(keyCode, newKey, ui->keybinds);
            if (!conflict.empty()) {
                *keyCode = settingsRebindBackup;
                ui->keybindConflictError = conflict;
            } else {
                ui->keybindConflictError.clear();
                ui->saveSettings();
            }
        } else if (changed) {
            ui->keybindConflictError.clear();
            ui->saveSettings();
        }
        settingsRebindActive = nullptr;
    }

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 36.0f;
    float dt = ImGui::GetIO().DeltaTime;

    std::string keyText = isRebinding ? "..." : getKeyName(*keyCode);

    ImGui::InvisibleButton(label, ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    bool hovered = ImGui::IsItemHovered();
    bool leftClick = ImGui::IsItemClicked(0);
    bool rightClick = ImGui::IsItemClicked(1);

    float& hoverT = anim.hoverAnims[id];
    hoverT = smoothStep(hoverT, hovered ? 1.0f : 0.0f, 11.0f, dt);

    ImVec4 accent = theme.getAccent();
    float rounding = theme.cornerRadius;
    ImVec2 rowMax(pos.x + width, pos.y + height);
    drawSolidRect(dl, pos, rowMax, rounding, theme, 1.0f + hoverT * 0.7f);

    dl->AddText(ImVec2(pos.x + 12.0f, pos.y + (height - ImGui::CalcTextSize(displayLabel.c_str()).y) * 0.5f), theme.getTextU32(), displayLabel.c_str());
    ImVec2 txtSize = ImGui::CalcTextSize(keyText.c_str());
    float btnW = std::max(txtSize.x + 22.0f, 54.0f);
    float btnH = 26.0f;
    float btnX = pos.x + width - btnW - 10.0f;
    float btnY = pos.y + (height - btnH) / 2;

    ImVec2 btnMin(btnX, btnY);
    ImVec2 btnMax(btnX + btnW, btnY + btnH);
    drawSolidRect(dl, btnMin, btnMax, 9.0f, theme, isRebinding ? 1.5f : (1.0f + hoverT * 0.4f));
    dl->AddRect(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH),
        isRebinding ? theme.getAccentU32(0.7f) : toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.16f)), 9.0f, 0, 1.0f);
    dl->AddText(ImVec2(btnX + (btnW - txtSize.x) / 2, btnY + (btnH - txtSize.y) / 2),
        isRebinding ? toU32(ImVec4(0.98f, 0.99f, 1.0f, 1.0f)) : theme.getTextU32(), keyText.c_str());

    if (leftClick) {
        if (isRebinding) {
            ui->rebindTarget = nullptr;
        } else {
            settingsRebindActive = keyCode;
            settingsRebindBackup = *keyCode;
            ui->rebindTarget = keyCode;
            ui->keybindConflictError.clear();
        }
    }
    if (rightClick) {
        *keyCode = 0;
        if (isRebinding)
            ui->rebindTarget = nullptr;
        ui->keybindConflictError.clear();
        ui->saveSettings();
    }
    ImGui::PopID();
}

}

void MenuInterface::switchTab(int newTab) {
    if (newTab == activeTab) return;
    previousTab = activeTab;
    activeTab = newTab;
    anim.tabTransition = 0.0f;
    if (newTab == 5) {
        OnlineClient::get()->refreshAuthStatus();
    }
}

void MenuInterface::drawBackdrop() {
    if (anim.openProgress <= 0.0f) return;
    float openT = anim.easeOutCubic(anim.openProgress);
    ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float dimAlpha = theme.bgOpacity * 0.35f * openT;
    bgDraw->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, static_cast<int>(dimAlpha * 255.0f)));
}

void MenuInterface::drawAmbientWaves(ImDrawList* dl, ImVec2 panelMin, ImVec2 panelMax) {
    if (!ambientWavesEnabled) return;

    float dt = ImGui::GetIO().DeltaTime;
    ambientTime += dt;

    float panelW = panelMax.x - panelMin.x;
    float panelH = panelMax.y - panelMin.y;
    float cx = panelMin.x + panelW * 0.5f;
    float cy = panelMin.y + panelH * 0.5f;

    dl->PushClipRect(panelMin, panelMax, true);

    ImVec4 accent = theme.getAccent();
    int ar = static_cast<int>(accent.x * 255);
    int ag = static_cast<int>(accent.y * 255);
    int ab = static_cast<int>(accent.z * 255);

    struct Orb {
        float xFreq, yFreq;
        float xPhase, yPhase;
        float xAmp, yAmp;
        float radius;
        float peakAlpha;
    };

    Orb orbs[] = {
        {0.23f, 0.17f, 0.0f,  1.5f,  0.38f, 0.32f, 180.0f, 0.018f},
        {0.19f, 0.26f, 2.8f,  0.4f,  0.34f, 0.36f, 160.0f, 0.016f},
        {0.31f, 0.14f, 1.2f,  3.9f,  0.28f, 0.40f, 170.0f, 0.017f},
        {0.15f, 0.22f, 4.5f,  2.1f,  0.42f, 0.28f, 140.0f, 0.015f},
        {0.27f, 0.33f, 3.3f,  5.0f,  0.30f, 0.34f, 150.0f, 0.014f},
        {0.12f, 0.29f, 5.5f,  0.8f,  0.36f, 0.30f, 130.0f, 0.013f},
        {0.35f, 0.18f, 1.8f,  4.6f,  0.32f, 0.38f, 145.0f, 0.015f},
        {0.21f, 0.24f, 3.7f,  2.9f,  0.40f, 0.26f, 155.0f, 0.014f},
    };

    constexpr int layers = 40;

    for (auto& o : orbs) {
        float x = cx + sinf(ambientTime * o.xFreq + o.xPhase) * panelW * o.xAmp;
        float y = cy + sinf(ambientTime * o.yFreq + o.yPhase) * panelH * o.yAmp;

        for (int i = layers; i >= 1; i--) {
            float t = static_cast<float>(i) / layers;
            float r = o.radius * t;
            float g = (1.0f - t);
            float falloff = g * g * g;
            float a = o.peakAlpha * falloff;
            int ai = static_cast<int>(a * 255.0f);
            if (ai <= 0) continue;
            dl->AddCircleFilled(ImVec2(x, y), r, IM_COL32(ar, ag, ab, ai), 48);
        }
    }

    dl->PopClipRect();
}

void MenuInterface::drawTitleBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImVec4 accent = theme.getAccent();

    ImFont* titleF = fontTitle ? fontTitle : ImGui::GetFont();
    ImFont* subtitleF = fontSmall ? fontSmall : ImGui::GetFont();
    float titleSize = titleF->FontSize;
    float subtitleSize = subtitleF->FontSize;

    const char* titleText = "ToastyReplay";
    ImVec2 titleSz = titleF->CalcTextSizeA(titleSize, FLT_MAX, 0.f, titleText);
    dl->AddText(titleF, titleSize, pos, toU32(ImVec4(0.98f, 0.99f, 1.0f, 0.99f)), titleText);

    std::string versionText = "v" MOD_VERSION;
    ImVec2 versionSz = subtitleF->CalcTextSizeA(subtitleSize, FLT_MAX, 0.f, versionText.c_str());
    float versionY = pos.y + (titleSz.y - versionSz.y) * 0.58f;
    dl->AddText(subtitleF, subtitleSize, ImVec2(pos.x + titleSz.x + 10.f, versionY), toU32(ImVec4(accent.x, accent.y, accent.z, 0.92f)), versionText.c_str());

    float y = pos.y + titleSz.y + 8.f;
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x, y),
        ImVec2(pos.x + width, y + 1.0f),
        theme.getAccentU32(0.62f),
        theme.getAccentU32(0.18f),
        theme.getAccentU32(0.18f),
        theme.getAccentU32(0.62f)
    );
    ImGui::Dummy(ImVec2(0.0f, (y - pos.y) + 10.0f));
}

void MenuInterface::drawTabBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 barMin = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    const char* tabNames[] = { "Main", "Render", "Clicks", "Autoclicker", "Settings", "Online" };
    const int tabCount = 6;
    float tabH = 38.0f;
    float tabW = width / tabCount;
    float rounding = theme.cornerRadius;
    ImVec2 barMax(barMin.x + width, barMin.y + tabH);
    ImVec4 accent = theme.getAccent();
    float dt = ImGui::GetIO().DeltaTime;

    drawSolidRect(dl, barMin, barMax, rounding, theme, 1.0f);

    float targetX = barMin.x + activeTab * tabW;
    if (tabIndicatorX < 0.0f) tabIndicatorX = targetX;
    tabIndicatorX = smoothStep(tabIndicatorX, targetX, 14.0f + anim.animSpeed * 0.7f, dt);
    float minX = barMin.x;
    float maxX = barMin.x + (tabCount - 1) * tabW;
    tabIndicatorX = std::clamp(tabIndicatorX, minX, maxX);

    ImVec2 activeMin(tabIndicatorX + 3.0f, barMin.y + 3.0f);
    ImVec2 activeMax(tabIndicatorX + tabW - 3.0f, barMax.y - 3.0f);
    drawSolidRect(dl, activeMin, activeMax, rounding - 3.0f, theme, 1.6f);
    dl->AddRectFilled(ImVec2(activeMin.x + 10.0f, activeMax.y - 2.0f), ImVec2(activeMax.x - 10.0f, activeMax.y), theme.getAccentU32(0.92f), 4.0f);

    if (fontBody) ImGui::PushFont(fontBody);
    ImVec2 originalCursor = ImGui::GetCursorScreenPos();

    for (int i = 0; i < tabCount; i++) {
        ImVec2 tabMin(barMin.x + i * tabW, barMin.y);
        ImVec2 tabMax(tabMin.x + tabW, barMax.y);
        ImGui::SetCursorScreenPos(tabMin);
        char tabId[64];
        snprintf(tabId, sizeof(tabId), "##tab_%d", i);
        ImGui::InvisibleButton(tabId, ImVec2(tabW, tabH));
        ImGuiID id = ImGui::GetItemID();
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            switchTab(i);
        }

        float& hoverT = anim.hoverAnims[id];
        hoverT = smoothStep(hoverT, hovered ? 1.0f : 0.0f, 12.0f, dt);
        if (hoverT > 0.01f && activeTab != i) {
            ImVec2 hMin(tabMin.x + 3.0f, tabMin.y + 3.0f);
            ImVec2 hMax(tabMax.x - 3.0f, tabMax.y - 3.0f);
            drawSolidRect(dl, hMin, hMax, rounding - 3.0f, theme, hoverT);
        }

        bool active = (activeTab == i);
        std::string displayName = trString(tabNames[i]);
        ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
        ImVec2 textPos(tabMin.x + (tabW - textSize.x) * 0.5f, tabMin.y + (tabH - textSize.y) * 0.5f);
        ImU32 color = active ? toU32(ImVec4(0.98f, 0.99f, 1.0f, 0.98f)) : (hovered ? theme.getTextU32() : theme.getTextSecondaryU32());
        dl->AddText(textPos, color, displayName.c_str());
    }

    if (fontBody) ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(originalCursor.x, barMax.y + 10.0f));
    ImGui::Dummy(ImVec2(width, 0.0f));
}

void MenuInterface::drawTabContent() {
    if (frameEditor.isActive()) {
        if (fontBody) ImGui::PushFont(fontBody);
        frameEditor.draw(*this);
        if (fontBody) ImGui::PopFont();
        return;
    }

    float t = anim.easeOutCubic(anim.tabTransition);
    float offsetY = (1.0f - t) * 14.0f;

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);

    if (fontBody) ImGui::PushFont(fontBody);

    switch (activeTab) {
        case 0:
            drawMainSubTabBar();
            switch (mainSubTab) {
                case 0: drawReplayTab(); break;
                case 1: drawToolsTab(); break;
                case 2: drawHacksTab(); break;
            }
            break;
        case 1: drawRenderTab(); break;
        case 2: drawClicksTab(); break;
        case 3: drawAutoclickerTab(); break;
        case 4: drawSettingsTab(); break;
        case 5: drawOnlineTab(); break;
    }

    if (fontBody) ImGui::PopFont();
    ImGui::PopStyleVar();
}

void MenuInterface::drawMainSubTabBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    const char* subNames[] = { "Replay", "Tools", "Hacks" };
    const int subCount = 3;
    float subW = width / subCount;
    float subH = 30.0f;
    float dt = ImGui::GetIO().DeltaTime;

    static float subIndicatorX = -1.0f;
    float targetX = pos.x + mainSubTab * subW;
    if (subIndicatorX < 0.0f) subIndicatorX = targetX;
    subIndicatorX = smoothStep(subIndicatorX, targetX, 14.0f + anim.animSpeed * 0.7f, dt);

    for (int i = 0; i < subCount; i++) {
        ImVec2 tabMin(pos.x + i * subW, pos.y);
        ImVec2 tabMax(tabMin.x + subW, pos.y + subH);
        ImGui::SetCursorScreenPos(tabMin);
        char tabId[64];
        snprintf(tabId, sizeof(tabId), "##subtab_%d", i);
        ImGui::InvisibleButton(tabId, ImVec2(subW, subH));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            mainSubTab = i;
        }

        bool active = (mainSubTab == i);
        if (fontSmall) ImGui::PushFont(fontSmall);
        std::string displayName = trString(subNames[i]);
        ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
        ImVec2 textPos(tabMin.x + (subW - textSize.x) * 0.5f, tabMin.y + (subH - textSize.y) * 0.5f);
        ImU32 color = active ? theme.getAccentU32(0.98f) : (hovered ? theme.getTextU32() : theme.getTextSecondaryU32());
        dl->AddText(textPos, color, displayName.c_str());
        if (fontSmall) ImGui::PopFont();
    }

    float indW = subW * 0.5f;
    float indX = subIndicatorX + (subW - indW) * 0.5f;
    dl->AddRectFilled(
        ImVec2(indX, pos.y + subH - 2.0f),
        ImVec2(indX + indW, pos.y + subH),
        theme.getAccentU32(0.92f), 2.0f
    );

    dl->AddLine(
        ImVec2(pos.x, pos.y + subH),
        ImVec2(pos.x + width, pos.y + subH),
        theme.getAccentU32(0.15f), 1.0f
    );

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + subH + 6.0f));
    ImGui::Dummy(ImVec2(width, 0.0f));
}

void MenuInterface::drawStatusBar() {
    ReplayEngine* engine = ReplayEngine::get();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    float padX = ImGui::GetStyle().WindowPadding.x;
    float barH = 30.0f;
    float barY = windowPos.y + windowSize.y - barH - 10.0f;
    ImVec4 accent = theme.getAccent();
    ImVec2 barMin(windowPos.x + padX, barY);
    ImVec2 barMax(windowPos.x + windowSize.x - padX, barY + barH);

    drawSolidRect(dl, barMin, barMax, theme.cornerRadius, theme, 1.0f);

    if (fontSmall) ImGui::PushFont(fontSmall);

    int tick = PlayLayer::get() ? engine->lastTickIndex : 0;
    auto statusText = trFormat(
        "TPS: {tps}    Speed: {speed}x    Tick: {tick}",
        fmt::arg("tps", fmt::format("{:.0f}", engine->tickRate)),
        fmt::arg("speed", fmt::format("{:.2f}", engine->gameSpeed)),
        fmt::arg("tick", tick)
    );

    ImVec2 textSize = ImGui::CalcTextSize(statusText.c_str());
    float textY = barY + (barH - textSize.y) / 2;
    dl->AddText(ImVec2(windowPos.x + padX + 12.0f, textY), theme.getTextSecondaryU32(), statusText.c_str());

    AccuracyMode statusAccuracyMode = engine->engineMode == MODE_EXECUTE
        ? engine->activeMacroAccuracyMode()
        : engine->selectedAccuracyMode;
    if (statusAccuracyMode != AccuracyMode::Vanilla) {
        std::string accuracyText = trFormat(
            "{mode} ON",
            fmt::arg("mode", getAccuracyTag(statusAccuracyMode))
        );
        const char* cbfTxt = accuracyText.c_str();
        ImVec2 cbfSize = ImGui::CalcTextSize(cbfTxt);
        float badgeX = windowPos.x + padX + 12.0f + textSize.x + 14.0f;
        float badgeY = barY + (barH - (cbfSize.y + 8.0f)) * 0.5f;
        ImVec2 badgeMin(badgeX, badgeY);
        ImVec2 badgeMax(badgeX + cbfSize.x + 14.0f, badgeY + cbfSize.y + 8.0f);
        dl->AddRectFilled(badgeMin, badgeMax, toU32(withAlpha(getAccuracyTagColor(statusAccuracyMode), 0.85f)), 999.0f);
        dl->AddText(ImVec2(badgeMin.x + 7.0f, badgeMin.y + 4.0f), IM_COL32(255, 250, 250, 255), cbfTxt);
    }

    if (fontSmall) ImGui::PopFont();
}

void MenuInterface::drawReplayTab() {
    ReplayEngine* engine = ReplayEngine::get();

    Widgets::SectionHeader("Mode", theme);

    constexpr float kPopupRounding = 0.0f;
    float pillW = (ImGui::GetContentRegionAvail().x - 20) / 3.0f;

    if (Widgets::PillButton("Disable", engine->engineMode == MODE_DISABLED, pillW, theme, anim)) {
        engine->pendingPlaybackStart = false;
        if (engine->engineMode == MODE_CAPTURE) {
            if (engine->ttrMode && engine->activeTTR) {
                delete engine->activeTTR;
                engine->activeTTR = nullptr;
            } else if (engine->activeMacro) {
                delete engine->activeMacro;
                engine->activeMacro = nullptr;
            }
            AccuracyRuntime::applyRuntimeAccuracyMode(engine->selectedAccuracyMode);
            engine->resetTimingTracking();
            engine->engineMode = MODE_DISABLED;
        } else if (engine->engineMode == MODE_EXECUTE) {
            engine->haltExecution();
        } else {
            engine->engineMode = MODE_DISABLED;
        }
        engine->clearStartPosWarning();
    }
    ImGui::SameLine(0, 10);
    static bool showFormatPopup = false;
    if (Widgets::PillButton("Record", engine->engineMode == MODE_CAPTURE, pillW, theme, anim)) {
        showFormatPopup = true;
    }
    ImGui::SameLine(0, 10);
    bool playbackActive = engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart;
    if (Widgets::PillButton("Playback", playbackActive, pillW, theme, anim)) {
        if (playbackActive) {
            engine->haltExecution();
        } else if (engine->hasMacroInputs()) {
            engine->requestExecutionStart();
            if (engine->engineMode == MODE_EXECUTE) {
                anim.closing = true;
                anim.opening = false;
            }
        }
    }

    if (showFormatPopup) {
        ImGui::OpenPopup("##FormatSelect");
        showFormatPopup = false;
    }
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kPopupRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginPopup("##FormatSelect", ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        auto popupTitle = trString("Select Format");
        drawPopupChrome(*this, popupTitle.c_str(), kPopupRounding);
        float popupBtnW = 120.0f;
        bool cbfRecording = engine->selectedAccuracyMode == AccuracyMode::CBF;
        if (Widgets::StyledButton("TTR", ImVec2(popupBtnW, 30), theme, anim, 6.0f)) {
            engine->ttrMode = true;
            if (PlayLayer::get())
                engine->beginCapture(PlayLayer::get()->m_level);
            else
                engine->engineMode = MODE_CAPTURE;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 10);
        if (cbfRecording) ImGui::BeginDisabled();
        if (Widgets::StyledButton("GDR", ImVec2(popupBtnW, 30), theme, anim, 6.0f)) {
            engine->ttrMode = false;
            if (PlayLayer::get())
                engine->beginCapture(PlayLayer::get()->m_level);
            else
                engine->engineMode = MODE_CAPTURE;
            ImGui::CloseCurrentPopup();
        }
        if (cbfRecording) ImGui::EndDisabled();
        if (cbfRecording) {
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
            imguiTextWrappedTr("CBF recording is only available in TTR format.");
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    ImGui::Dummy(ImVec2(0, 8));

    if (engine->engineMode == MODE_CAPTURE && engine->hasMacro()) {
        size_t actionCount = 0;

        if (engine->ttrMode && engine->activeTTR)
            actionCount = engine->activeTTR->inputs.size();
        else if (engine->activeMacro)
            actionCount = engine->activeMacro->inputs.size();

        Widgets::StatusBadge("RECORDING", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::SameLine();
        Widgets::StatusBadge(engine->ttrMode ? "TTR" : "GDR",
            engine->ttrMode ? getTTRTagColor() : ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
        if (auto* accuracyTag = getAccuracyTag(engine->selectedAccuracyMode)) {
            ImGui::SameLine();
            Widgets::StatusBadge(accuracyTag, getAccuracyTagColor(engine->selectedAccuracyMode));
        }
        ImGui::SameLine();
        auto actionsText = trFormat("Actions: {count}", fmt::arg("count", actionCount));
        ImGui::TextUnformatted(actionsText.c_str());
        ImGui::Dummy(ImVec2(0, 4));

        if (!macroNameReady) {
            std::string currentName = engine->getMacroName();
            strncpy(macroNameBuffer, currentName.c_str(), sizeof(macroNameBuffer) - 1);
            macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
            macroNameReady = true;
        }

        imguiTextTr("Macro Name:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##recordingName", macroNameBuffer, sizeof(macroNameBuffer))) {
            if (engine->ttrMode && engine->activeTTR)
                engine->activeTTR->name = macroNameBuffer;
            else if (engine->activeMacro)
                engine->activeMacro->name = macroNameBuffer;
        }

        ImGui::Dummy(ImVec2(0, 4));
        float btnW = (ImGui::GetContentRegionAvail().x - 10) / 2.0f;

        if (Widgets::StyledButton("Save Macro", ImVec2(btnW, 30), theme, anim)) {
            if (engine->ttrMode && engine->activeTTR && !engine->activeTTR->inputs.empty()) {
                engine->activeTTR->persist();
                std::strncpy(macroNameBuffer, engine->activeTTR->name.c_str(), sizeof(macroNameBuffer) - 1);
                macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
                markReplayListDirty();
                refreshReplayListIfNeeded(true);
            } else if (!engine->ttrMode && engine->activeMacro && !engine->activeMacro->inputs.empty()) {
                engine->activeMacro->persist(engine->activeMacro->accuracyMode, static_cast<int>(engine->tickRate));
                std::strncpy(macroNameBuffer, engine->activeMacro->name.c_str(), sizeof(macroNameBuffer) - 1);
                macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
                markReplayListDirty();
                refreshReplayListIfNeeded(true);
            }
        }
        ImGui::SameLine(0, 10);
        if (Widgets::StyledButton("Stop", ImVec2(btnW, 30), theme, anim)) {
            if (engine->ttrMode && engine->activeTTR) {
                delete engine->activeTTR;
                engine->activeTTR = nullptr;
            } else if (engine->activeMacro) {
                delete engine->activeMacro;
                engine->activeMacro = nullptr;
            }
            engine->pendingPlaybackStart = false;
            engine->engineMode = MODE_DISABLED;
            macroNameReady = false;
        }
        ImGui::Dummy(ImVec2(0, 4));
    } else {
        macroNameReady = false;
    }

    if (engine->engineMode == MODE_EXECUTE && engine->hasMacro()) {
        size_t actionCount = 0;
        std::string macName;

        if (engine->ttrMode && engine->activeTTR) {
            actionCount = engine->activeTTR->inputs.size();
            macName = engine->activeTTR->name;
        } else if (engine->activeMacro) {
            actionCount = engine->activeMacro->inputs.size();
            macName = engine->activeMacro->name;
        }

        Widgets::StatusBadge("PLAYING", ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::SameLine();
        Widgets::StatusBadge(engine->ttrMode ? "TTR" : "GDR",
            engine->ttrMode ? getTTRTagColor() : ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
        {
            AccuracyMode accuracyMode = engine->activeMacroAccuracyMode();
            if (auto* accuracyTag = getAccuracyTag(accuracyMode)) {
                ImGui::SameLine();
                Widgets::StatusBadge(accuracyTag, getAccuracyTagColor(accuracyMode));
            }
        }
        ImGui::SameLine();
        auto playbackInfo = trFormat(
            "{name} | Actions: {count}",
            fmt::arg("name", macName),
            fmt::arg("count", actionCount)
        );
        ImGui::TextUnformatted(playbackInfo.c_str());

        if (engine->startPosActive) {
            Widgets::StatusBadge("STARTPOS", ImVec4(0.3f, 0.7f, 1.0f, 1.0f));
            ImGui::SameLine();
            auto offsetText = trFormat("Offset: {ticks} ticks", fmt::arg("ticks", engine->tickOffset));
            ImGui::TextUnformatted(offsetText.c_str());
        }

        ImGui::Dummy(ImVec2(0, 4));

        if (Widgets::StyledButton("Stop Playback", ImVec2(-1, 30), theme, anim))
            engine->haltExecution();

        ImGui::Dummy(ImVec2(0, 4));
    }

    if (engine->hasStartPosWarning()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        auto warningText = engine->getStartPosWarningText();
        ImGui::TextWrapped("%s", warningText.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
    }

    Widgets::SectionHeader("Saved Replays", theme);

    float listPadY = 8.0f;
    float listPadX = 10.0f;
    float listH = std::max(80.0f, std::min(200.0f, (float)engine->storedMacros.size() * 28.0f + listPadY * 2.0f));
    ImVec2 listPos = ImGui::GetCursorScreenPos();
    float listW = ImGui::GetContentRegionAvail().x;
    drawSolidRect(ImGui::GetWindowDrawList(), listPos, ImVec2(listPos.x + listW, listPos.y + listH), theme.cornerRadius, theme, 0.55f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##MacroList", ImVec2(-1, listH), false);

    
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
    ImGui::Dummy(ImVec2(0, listPadY));

    if (engine->storedMacros.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
        imguiTextTr("No saved replays");
        ImGui::PopStyleColor();
    }

    auto macroListCopy = engine->storedMacros;
    for (const auto& macroName : macroListCopy) {
        bool isSelected = (engine->hasMacro() && engine->engineMode != MODE_CAPTURE && engine->getMacroName() == macroName);
        bool isIncompatible = engine->incompatibleMacros.count(macroName) > 0;
        bool isCBS = engine->cbsMacros.count(macroName) > 0;
        bool isCBF = engine->cbfMacros.count(macroName) > 0;
        bool isTTR = engine->ttrMacros.count(macroName) > 0;
        ImGui::PushID(macroName.c_str());

        float xBtnW = 20.0f;
        float rowH = ImGui::GetTextLineHeight() + 8.0f;
        float fullRowW = ImGui::GetContentRegionAvail().x;

        
        std::string rowLabel = macroName;
        float accuracyLabelW = 0.0f;
        if (!isIncompatible && isCBF) {
            accuracyLabelW = ImGui::CalcTextSize("CBF").x + 16.0f;
        } else if (!isIncompatible && isCBS) {
            accuracyLabelW = ImGui::CalcTextSize("CBS").x + 16.0f;
        }
        auto incompatibleText = trString("Incompatible");
        float ttrLabelW = (!isIncompatible && isTTR) ? ImGui::CalcTextSize("TTR").x + 16.0f : 0.0f;
        float gdrLabelW = (!isIncompatible && !isTTR) ? ImGui::CalcTextSize("GDR").x + 16.0f : 0.0f;
        float incompatLabelW = isIncompatible ? ImGui::CalcTextSize(incompatibleText.c_str()).x + 16.0f : 0.0f;
        float maxNameW = std::max(40.0f, fullRowW - xBtnW - accuracyLabelW - ttrLabelW - gdrLabelW - incompatLabelW - listPadX * 2.0f - 24.0f);
        if (ImGui::CalcTextSize(rowLabel.c_str()).x > maxNameW) {
            std::string clipped = rowLabel;
            while (!clipped.empty() && ImGui::CalcTextSize((clipped + "...").c_str()).x > maxNameW)
                clipped.pop_back();
            rowLabel = clipped + "...";
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX());
        ImVec2 rowStart = ImGui::GetCursorScreenPos();

        if (isIncompatible) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
        }

        bool rowActivated = ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_AllowOverlap, ImVec2(fullRowW, rowH));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDown(1)) {
            std::string tooltip;
            if (isCBS || isCBF) {
                tooltip += trString("CBF or CBS macros do not work on other menus!");
            }
            if (isTTR) {
                if (!tooltip.empty()) tooltip += "\n";
                tooltip += trString("TTR macros do not work on other menus!");
            }
            if (!tooltip.empty()) {
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
        }
        bool rowDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        bool rowRightClicked = ImGui::IsItemClicked(1);

        if (isIncompatible) {
            ImGui::PopStyleColor();
        }

        if (rowDoubleClicked || rowRightClicked) {
            replayActionMacroName = macroName;
            replayActionIsTTR = isTTR;
            replayActionPopupRequested = true;
        } else if (!isIncompatible && rowActivated && engine->engineMode != MODE_CAPTURE) {
            engine->pendingPlaybackStart = false;
            if (isTTR) {
                TTRMacro* loaded = TTRMacro::loadFromDisk(macroName);
                if (loaded) {
                    if (engine->activeTTR) delete engine->activeTTR;
                    if (engine->activeMacro) { delete engine->activeMacro; engine->activeMacro = nullptr; }
                    engine->activeTTR = loaded;
                    engine->ttrMode = true;
                }
            } else {
                MacroSequence* loaded = MacroSequence::loadFromDisk(macroName);
                if (loaded) {
                    if (engine->activeMacro) delete engine->activeMacro;
                    if (engine->activeTTR) { delete engine->activeTTR; engine->activeTTR = nullptr; }
                    engine->activeMacro = loaded;
                    engine->ttrMode = false;
                }
            }
        }

        float itemMinY = rowStart.y;
        float itemH = rowH;

        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowStart.x + listPadX, itemMinY + (itemH - ImGui::GetTextLineHeight()) * 0.5f),
            isIncompatible ? toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.4f)) : theme.getTextU32(),
            rowLabel.c_str()
        );

        float tagX = rowStart.x + fullRowW - xBtnW - listPadX;
        if (isIncompatible) {
            ImVec2 tagSize = ImGui::CalcTextSize(incompatibleText.c_str());
            tagX -= tagSize.x + 8.0f;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX, itemMinY + (itemH - tagSize.y) * 0.5f),
                toU32(ImVec4(1.0f, 0.2f, 0.2f, 1.0f)), incompatibleText.c_str()
            );
        }
        if (!isIncompatible && (isCBS || isCBF)) {
            AccuracyMode listedMode = isCBF ? AccuracyMode::CBF : AccuracyMode::CBS;
            const char* tag = getAccuracyTag(listedMode);
            ImVec2 tagSize = ImGui::CalcTextSize(tag);
            tagX -= tagSize.x + 8.0f;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX, itemMinY + (itemH - tagSize.y) * 0.5f),
                toU32(getAccuracyTagColor(listedMode)), tag
            );
        }
        if (!isIncompatible && isTTR) {
            const char* tag = "TTR";
            ImVec2 tagSize = ImGui::CalcTextSize(tag);
            tagX -= tagSize.x + 8.0f;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX, itemMinY + (itemH - tagSize.y) * 0.5f),
                toU32(getTTRTagColor()), tag
            );
        } else if (!isIncompatible) {
            const char* tag = "GDR";
            ImVec2 tagSize = ImGui::CalcTextSize(tag);
            tagX -= tagSize.x + 8.0f;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX, itemMinY + (itemH - tagSize.y) * 0.5f),
                toU32(ImVec4(1.0f, 0.9f, 0.2f, 1.0f)), tag
            );
        }

        float xBtnX = rowStart.x + fullRowW - xBtnW - listPadX;
        float xBtnY = itemMinY + (itemH - xBtnW) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(xBtnX, xBtnY));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::Button("x", ImVec2(xBtnW, xBtnW))) {
            auto dir = getReplayDirectoryPath();
            bool deleted = false;
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && toasty::pathToUtf8(entry.path().stem()) == macroName) {
                    std::filesystem::remove(entry.path());
                    deleted = true;
                    break;
                }
            }
            if (deleted) {
                markReplayListDirty();
                refreshReplayListIfNeeded(true);
            }
        }
        ImGui::PopStyleVar();

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    if (replayActionPopupRequested) {
        ImGui::OpenPopup("Macro Actions");
        replayActionPopupRequested = false;
    }

    if (replayRenamePopupRequested) {
        ImGui::OpenPopup("Rename Replay");
        replayRenamePopupRequested = false;
    }

    constexpr float kActionPopupRounding = 0.0f;
    ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kActionPopupRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginPopupModal(
        "Macro Actions",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
    )) {
        auto actionTitle = trString("Macro Actions");
        drawPopupChrome(*this, actionTitle.c_str(), kActionPopupRounding);
        ImGui::TextColored(theme.getAccent(), "%s", replayActionMacroName.c_str());
        ImGui::Dummy(ImVec2(0, 6));

        constexpr float actionBtnW = 230.0f;
        if (Widgets::StyledButton("Rename##actionRename", ImVec2(actionBtnW, 30.0f), theme, anim, 6.0f)) {
            replayRenameOriginalName = replayActionMacroName;
            std::strncpy(replayRenameBuffer, replayActionMacroName.c_str(), sizeof(replayRenameBuffer) - 1);
            replayRenameBuffer[sizeof(replayRenameBuffer) - 1] = '\0';
            replayRenameError.clear();
            replayRenameFocusInput = true;
            ImGui::CloseCurrentPopup();
            replayRenamePopupRequested = true;
        }

        ImGui::Dummy(ImVec2(0, 4));

        {
            bool canEdit = true;
            if (replayActionIsTTR) {
                TTRMacro* probe = TTRMacro::loadFromDisk(replayActionMacroName);
                if (probe) {
                    if (probe->accuracyMode != AccuracyMode::Vanilla) canEdit = false;
                    delete probe;
                }
            } else {
                MacroSequence* probe = MacroSequence::loadFromDisk(replayActionMacroName);
                if (probe) {
                    if (probe->accuracyMode != AccuracyMode::Vanilla) canEdit = false;
                    delete probe;
                }
            }

            if (!canEdit) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
                Widgets::StyledButton("Open Macro Editor##actionEdit", ImVec2(actionBtnW, 30.0f), theme, anim, 6.0f);
                ImGui::PopStyleVar();
                ImVec2 tipPos = ImGui::GetItemRectMin();
                tipPos.y += 34.0f;
                auto tipText = trString("CBS/CBF macros cannot be edited");
                ImGui::GetWindowDrawList()->AddText(tipPos, IM_COL32(255, 180, 80, 200), tipText.c_str());
            } else if (Widgets::StyledButton("Open Macro Editor##actionEdit", ImVec2(actionBtnW, 30.0f), theme, anim, 6.0f)) {
                if (replayActionIsTTR) {
                    TTRMacro* loaded = TTRMacro::loadFromDisk(replayActionMacroName);
                    if (loaded) {
                        frameEditor.openTTR(replayActionMacroName, loaded);
                        delete loaded;
                    }
                } else {
                    MacroSequence* loaded = MacroSequence::loadFromDisk(replayActionMacroName);
                    if (loaded) {
                        frameEditor.openGDR(replayActionMacroName, loaded);
                        delete loaded;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::Dummy(ImVec2(0, 6));

        if (Widgets::StyledButton("Cancel##actionCancel", ImVec2(actionBtnW, 28.0f), theme, anim, 6.0f)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    constexpr float kRenamePopupRounding = 0.0f;
    ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRenamePopupRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginPopupModal(
        "Rename Replay",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
    )) {
        auto renameTitle = trString("Rename Replay");
        drawPopupChrome(*this, renameTitle.c_str(), kRenamePopupRounding);
        imguiTextTr("Rename replay:");
        ImGui::TextColored(theme.getAccent(), "%s", replayRenameOriginalName.c_str());
        ImGui::Dummy(ImVec2(0, 6));

        if (replayRenameFocusInput) {
            ImGui::SetKeyboardFocusHere();
            replayRenameFocusInput = false;
        }

        bool submitted = ImGui::InputText(
            "##renameReplay",
            replayRenameBuffer,
            sizeof(replayRenameBuffer),
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue
        );

        if (!replayRenameError.empty()) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", replayRenameError.c_str());
        }

        ImGui::Dummy(ImVec2(0, 10));
        constexpr float popupButtonWidth = 110.0f;
        bool confirm = Widgets::StyledButton("Rename##confirmRename", ImVec2(popupButtonWidth, 28.0f), theme, anim, 6.0f);
        ImGui::SameLine(0, 8);
        bool cancel = Widgets::StyledButton("Cancel##cancelRename", ImVec2(popupButtonWidth, 28.0f), theme, anim, 6.0f);

        if (confirm || submitted) {
            std::string renamedTo;
            if (renameStoredReplayFile(replayRenameOriginalName, replayRenameBuffer, renamedTo, replayRenameError)) {
                if (engine->engineMode != MODE_CAPTURE && engine->hasMacro() && engine->getMacroName() == replayRenameOriginalName) {
                    if (engine->ttrMode && engine->activeTTR) {
                        engine->activeTTR->name = renamedTo;
                        engine->activeTTR->persistedName = renamedTo;
                    } else if (engine->activeMacro) {
                        engine->activeMacro->name = renamedTo;
                        engine->activeMacro->persistedName = renamedTo;
                    }
                }

                replayRenameOriginalName.clear();
                replayRenameError.clear();
                replayRenameBuffer[0] = '\0';
                markReplayListDirty();
                refreshReplayListIfNeeded(true);
                ImGui::CloseCurrentPopup();
            }
        }

        if (cancel) {
            replayRenameOriginalName.clear();
            replayRenameError.clear();
            replayRenameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    ImGui::Dummy(ImVec2(0, 4));
    float btnW = (ImGui::GetContentRegionAvail().x - 10) / 2.0f;

    if (Widgets::StyledButton("Refresh", ImVec2(btnW, 28), theme, anim)) {
        markReplayListDirty();
        refreshReplayListIfNeeded(true);
    }

    ImGui::SameLine(0, 10);
    if (Widgets::StyledButton("Open Folder", ImVec2(btnW, 28), theme, anim)) {
        auto dir = getReplayDirectoryPath();
        if (std::filesystem::exists(dir) || std::filesystem::create_directory(dir))
            utils::file::openFolder(dir);
    }

    if (engine->hasMacro() && engine->engineMode == MODE_EXECUTE && PlayLayer::get()) {
        size_t actionCount = 0;
        std::string loadedName;
        AccuracyMode loadedAccuracyMode = AccuracyMode::Vanilla;

        if (engine->ttrMode && engine->activeTTR) {
            actionCount = engine->activeTTR->inputs.size();
            loadedName = engine->activeTTR->name;
            loadedAccuracyMode = engine->activeTTR->accuracyMode;
        } else if (engine->activeMacro) {
            actionCount = engine->activeMacro->inputs.size();
            loadedName = engine->activeMacro->name;
            loadedAccuracyMode = engine->activeMacro->accuracyMode;
        }

        ImGui::Dummy(ImVec2(0, 4));
        Widgets::SectionHeader("Loaded Macro", theme);
        auto nameText = trFormat("Name: {name}", fmt::arg("name", loadedName));
        ImGui::TextUnformatted(nameText.c_str());
        if (engine->ttrMode) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, getTTRTagColor());
            ImGui::TextUnformatted("(TTR)");
            ImGui::PopStyleColor();
        }
        if (auto* accuracyTag = getAccuracyTag(loadedAccuracyMode)) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, getAccuracyTagColor(loadedAccuracyMode));
            ImGui::Text("(%s)", accuracyTag);
            ImGui::PopStyleColor();
        }
        auto loadedActions = trFormat("Actions: {count}", fmt::arg("count", actionCount));
        ImGui::TextUnformatted(loadedActions.c_str());
        if (engine->ttrMode && engine->activeTTR) {
            auto anchorsText = trFormat("Anchors: {count}", fmt::arg("count", engine->activeTTR->anchors.size()));
            ImGui::TextUnformatted(anchorsText.c_str());
        }

        if (engine->engineMode == MODE_EXECUTE) {
            ImGui::Dummy(ImVec2(0, 4));
            Widgets::ToggleSwitch("Ignore Manual Input", &engine->userInputIgnored, theme, anim);
        }
    }
}

void MenuInterface::drawToolsTab() {
    ReplayEngine* engine = ReplayEngine::get();

    Widgets::SectionHeader("TPS Control", theme);
    auto currentTpsText = trFormat("Current: {value} TPS", fmt::arg("value", fmt::format("{:.0f}", engine->tickRate)));
    ImGui::TextColored(theme.getAccent(), "%s", currentTpsText.c_str());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
    ImGui::InputFloat("##tps", &tempTickRate, 0, 0, "%.0f");
    ImGui::SameLine(0, 6);
    if (Widgets::StyledButton("Apply###tps", ImVec2(78, 28), theme, anim)) {
        bool canChange = !PlayLayer::get() || engine->engineMode != MODE_EXECUTE;
        if (canChange) {
            engine->tickRate = tempTickRate;
            Mod::get()->setSavedValue("eng_tick_rate", (float)engine->tickRate);
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Speed Control", theme);
    auto currentSpeedText = trFormat("Current: {value}x", fmt::arg("value", fmt::format("{:.2f}", engine->gameSpeed)));
    ImGui::TextColored(theme.getAccent(), "%s", currentSpeedText.c_str());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
    ImGui::InputFloat("##speed", &tempGameSpeed, 0, 0, "%.2f");
    ImGui::SameLine(0, 6);
    if (Widgets::StyledButton("Apply###spd", ImVec2(78, 28), theme, anim))
        engine->gameSpeed = tempGameSpeed;

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Features", theme);

    if (Widgets::ModuleCardBegin("Frame Advance", "Pause and step frame-by-frame",
        &engine->tickStepping, theme, anim, &keybinds.frameAdvance)) {
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Speedhack Audio", "Apply speed changes to game audio",
        &engine->audioPitchEnabled, theme, anim, &keybinds.audioPitch)) {
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Layout Mode", "Remove all decorations",
        &engine->layoutMode, theme, anim, &keybinds.layoutMode)) {
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("No Mirror Effect", "Removed mirroring in playback/recording",
        &engine->noMirrorEffect, theme, anim, &keybinds.noMirror)) {
        Widgets::ToggleSwitch("Only Recording", &engine->noMirrorRecordingOnly, theme, anim);
        Widgets::ModuleCardEnd();
    }
}

void MenuInterface::drawHacksTab() {
    ReplayEngine* engine = ReplayEngine::get();
    ImGui::Dummy(ImVec2(0, 4));

    if (Widgets::ModuleCardBegin("Safe Mode", "Prevents stats and percentage gain",
        &engine->protectedMode, theme, anim, &keybinds.safeMode)) {
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Show Trajectory", "Display predicted player path",
        &engine->pathPreview, theme, anim, &keybinds.trajectory)) {
        Widgets::StyledSliderInt("Trajectory Length", &engine->pathLength, 50, 480, theme);
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Show Hitboxes", "Display collision bounds for objects",
        &engine->showHitboxes, theme, anim, &keybinds.hitboxes)) {
        Widgets::ToggleSwitch("On Death Only", &engine->hitboxOnDeath, theme, anim);
        Widgets::ToggleSwitch("Draw Trail", &engine->hitboxTrail, theme, anim);

        if (engine->hitboxTrail)
            Widgets::StyledSliderInt("Trail Length", &engine->hitboxTrailLength, 10, 600, theme);

        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Noclip", "Disable collision with obstacles",
        &engine->collisionBypass, theme, anim, &keybinds.noclip)) {
        float hitRate = 100.0f;
        if (engine->totalTickCount > 0)
            hitRate = 100.0f * (1.0f - (float)engine->bypassedCollisions / (float)engine->totalTickCount);
        if (hitRate < 0.0f) hitRate = 0.0f;

        ImVec4 hitColor;
        if (hitRate >= 90.0f) hitColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        else if (hitRate >= 70.0f) hitColor = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
        else hitColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        imguiTextTr("Accuracy:");
        ImGui::SameLine();
        ImGui::TextColored(hitColor, "%.2f%%", hitRate);
        auto deathFrameText = trFormat(
            "Deaths: {deaths} | Frames: {frames}",
            fmt::arg("deaths", engine->bypassedCollisions),
            fmt::arg("frames", engine->totalTickCount)
        );
        ImGui::TextUnformatted(deathFrameText.c_str());

        ImGui::Dummy(ImVec2(0, 4));
        if (engine->collisionLimitActive) {
            char label[64];
            snprintf(label, sizeof(label), "Accuracy Limit (%.1f%%)", engine->collisionThreshold);
            Widgets::ToggleSwitch(label, &engine->collisionLimitActive, theme, anim);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputFloat("##accuracyLimit", &engine->collisionThreshold, 0, 0, "%.1f")) {
                if (engine->collisionThreshold < 1.0f) engine->collisionThreshold = 1.0f;
                if (engine->collisionThreshold > 100.0f) engine->collisionThreshold = 100.0f;
            }
        } else {
            Widgets::ToggleSwitch("Accuracy Limit", &engine->collisionLimitActive, theme, anim);
            if (engine->collisionLimitActive && engine->collisionThreshold < 1.0f)
                engine->collisionThreshold = 80.0f;
        }

        ImGui::Dummy(ImVec2(0, 4));
        Widgets::ToggleSwitch("On Death Color", &engine->noclipDeathFlash, theme, anim);
        if (engine->noclipDeathFlash) {
            ImGui::Dummy(ImVec2(0, 4));
            float col[3] = { engine->noclipDeathColorR, engine->noclipDeathColorG, engine->noclipDeathColorB };
            if (ImGui::ColorEdit3("##deathColor", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                engine->noclipDeathColorR = col[0];
                engine->noclipDeathColorG = col[1];
                engine->noclipDeathColorB = col[2];
            }
        }
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("RNG Lock", "Use fixed seed for consistent RNG",
        &engine->rngLocked, theme, anim, &keybinds.rngLock)) {
        if (!rngBufferInit) {
            snprintf(rngBuffer, sizeof(rngBuffer), "%u", engine->rngSeedVal);
            rngBufferInit = true;
        }

        imguiTextTr("Seed Value:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##seedValue", rngBuffer, sizeof(rngBuffer), ImGuiInputTextFlags_CharsDecimal)) {
            auto parsed = toasty::parseInteger<unsigned long long>(rngBuffer);
            engine->rngSeedVal = parsed ? static_cast<unsigned int>(*parsed) : 1u;
        }
        Widgets::ModuleCardEnd();
    }
}

void MenuInterface::drawRenderTab() {
    auto* engine = ReplayEngine::get();
    auto* mod = Mod::get();
    Renderer& r = engine->renderer;

    
    struct ResPreset { const char* name; int width; int height; };
    static const ResPreset presets[] = {
        { "720p  (1280x720)",   1280,  720 },
        { "1080p (1920x1080)",  1920, 1080 },
        { "1440p (2560x1440)",  2560, 1440 },
        { "4K    (3840x2160)",  3840, 2160 },
    };
    static const int presetCount = 4;

    if (!renderBufsInit) {
        loadRenderSettings();
    }

    Widgets::SectionHeader("Render", theme);

    float inputW = ImGui::GetContentRegionAvail().x * 0.45f;

    imguiTextTr("Name");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderName", renderNameBuf, sizeof(renderNameBuf)))
        mod->setSavedValue("render_name", std::string(renderNameBuf));

    ImGui::Dummy(ImVec2(0, 4));

    if (r.recording) {
        Widgets::StatusBadge("Rendering", ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::Dummy(ImVec2(0, 4));
        if (Widgets::StyledButton("Stop Render", ImVec2(ImGui::GetContentRegionAvail().x, 36), theme, anim))
            r.toggle();
    } else {
        if (Widgets::StyledButton("Start Render", ImVec2(ImGui::GetContentRegionAvail().x, 36), theme, anim))
            r.toggle();
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Resolution", theme);

    imguiTextTr("Preset");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##resPreset", presets[renderPresetIndex].name)) {
        for (int i = 0; i < presetCount; i++) {
            bool selected = (renderPresetIndex == i);
            if (ImGui::Selectable(presets[i].name, selected)) {
                renderPresetIndex = i;
                snprintf(renderWidthBuf, sizeof(renderWidthBuf), "%d", presets[i].width);
                snprintf(renderHeightBuf, sizeof(renderHeightBuf), "%d", presets[i].height);
                mod->setSavedValue("render_width", (int64_t)presets[i].width);
                mod->setSavedValue("render_height", (int64_t)presets[i].height);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    imguiTextTr("FPS");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderFPS", renderFpsBuf, sizeof(renderFpsBuf), ImGuiInputTextFlags_CharsDecimal))
        mod->setSavedValue("render_fps", (int64_t)std::atoi(renderFpsBuf));

    
    if (showAdvancedWarning) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 0));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(theme.getAccent().x, theme.getAccent().y, theme.getAccent().z, 0.5f));

        ImGui::Begin("##advancedWarning", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, 1.0f));
        imguiTextWrappedTr("You are editing advanced changes. Are you sure you would like to continue?");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 12));

        float btnW = (ImGui::GetContentRegionAvail().x - 12) * 0.5f;

        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.20f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.25f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.45f, 0.16f, 1.0f));
        auto yesText = trString("Yes");
        if (ImGui::Button(yesText.c_str(), ImVec2(btnW, 34))) {
            advancedWarningAccepted = true;
            showAdvancedWarning = false;
            snprintf(backupCodecBuf, sizeof(backupCodecBuf), "%s", renderCodecBuf);
            snprintf(backupBitrateBuf, sizeof(backupBitrateBuf), "%s", renderBitrateBuf);
            snprintf(backupExtBuf, sizeof(backupExtBuf), "%s", renderExtBuf);
            snprintf(backupArgsBuf, sizeof(backupArgsBuf), "%s", renderArgsBuf);
            snprintf(backupVideoArgsBuf, sizeof(backupVideoArgsBuf), "%s", renderVideoArgsBuf);
            snprintf(backupAudioArgsBuf, sizeof(backupAudioArgsBuf), "%s", renderAudioArgsBuf);
            snprintf(backupSecondsAfterBuf, sizeof(backupSecondsAfterBuf), "%s", renderSecondsAfterBuf);
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 12);

        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.15f, 0.15f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.20f, 0.20f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.10f, 0.10f, 1.0f));
        auto noText = trString("No");
        if (ImGui::Button(noText.c_str(), ImVec2(btnW, 34))) {
            showAdvancedWarning = false;
            
            snprintf(renderCodecBuf, sizeof(renderCodecBuf), "%s", backupCodecBuf);
            snprintf(renderBitrateBuf, sizeof(renderBitrateBuf), "%s", backupBitrateBuf);
            snprintf(renderExtBuf, sizeof(renderExtBuf), "%s", backupExtBuf);
            snprintf(renderArgsBuf, sizeof(renderArgsBuf), "%s", backupArgsBuf);
            snprintf(renderVideoArgsBuf, sizeof(renderVideoArgsBuf), "%s", backupVideoArgsBuf);
            snprintf(renderAudioArgsBuf, sizeof(renderAudioArgsBuf), "%s", backupAudioArgsBuf);
            snprintf(renderSecondsAfterBuf, sizeof(renderSecondsAfterBuf), "%s", backupSecondsAfterBuf);
            
            mod->setSavedValue("render_codec", std::string(backupCodecBuf));
            mod->setSavedValue("render_bitrate", std::string(backupBitrateBuf));
            mod->setSavedValue("render_file_extension", std::string(backupExtBuf));
            mod->setSavedValue("render_args", std::string(backupArgsBuf));
            mod->setSavedValue("render_video_args", std::string(backupVideoArgsBuf));
            mod->setSavedValue("render_audio_args", std::string(backupAudioArgsBuf));
            mod->setSavedValue("render_seconds_after", std::string(backupSecondsAfterBuf));
        }
        ImGui::PopStyleColor(3);

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    auto guardAdvancedEdit = [&]() {
        if (!advancedWarningAccepted && !showAdvancedWarning) {
            snprintf(backupCodecBuf, sizeof(backupCodecBuf), "%s", renderCodecBuf);
            snprintf(backupBitrateBuf, sizeof(backupBitrateBuf), "%s", renderBitrateBuf);
            snprintf(backupExtBuf, sizeof(backupExtBuf), "%s", renderExtBuf);
            snprintf(backupArgsBuf, sizeof(backupArgsBuf), "%s", renderArgsBuf);
            snprintf(backupVideoArgsBuf, sizeof(backupVideoArgsBuf), "%s", renderVideoArgsBuf);
            snprintf(backupAudioArgsBuf, sizeof(backupAudioArgsBuf), "%s", renderAudioArgsBuf);
            snprintf(backupSecondsAfterBuf, sizeof(backupSecondsAfterBuf), "%s", renderSecondsAfterBuf);
            showAdvancedWarning = true;
        }
    };

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Encoding", theme);

    imguiTextTr("Codec");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderCodec", renderCodecBuf, sizeof(renderCodecBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_codec", std::string(renderCodecBuf));
    }

    imguiTextTr("Bitrate (M)");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderBitrate", renderBitrateBuf, sizeof(renderBitrateBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_bitrate", std::string(renderBitrateBuf));
    }

    imguiTextTr("Extension");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderExt", renderExtBuf, sizeof(renderExtBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_file_extension", std::string(renderExtBuf));
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Audio", theme);

    if (Widgets::ToggleSwitch("Include Audio", &renderIncludeAudio, theme, anim))
        mod->setSavedValue("render_include_audio", renderIncludeAudio);

    if (Widgets::ToggleSwitch("Include Click Sounds", &renderIncludeClicks, theme, anim))
        mod->setSavedValue("render_include_clicks", renderIncludeClicks);

    if (renderIncludeClicks) {
        auto* csm = ClickSoundManager::get();
        if (csm->p1Pack.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 0.8f));
            imguiTextWrappedTr("Configure a click pack in the Clicks tab first.");
            ImGui::PopStyleColor();
        }
    }

    if (Widgets::StyledSliderFloat("Click Volume", &renderSfxVol, 0.f, 1.f, theme))
        mod->setSavedValue("render_sfx_volume", (double)renderSfxVol);

    if (Widgets::StyledSliderFloat("Music Volume", &renderMusicVol, 0.f, 1.f, theme))
        mod->setSavedValue("render_music_volume", (double)renderMusicVol);

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Display", theme);

    if (Widgets::ToggleSwitch("Hide End Screen", &renderHideEndscreen, theme, anim))
        mod->setSavedValue("render_hide_endscreen", renderHideEndscreen);

    if (Widgets::ToggleSwitch("Hide Level Complete", &renderHideLevelComplete, theme, anim))
        mod->setSavedValue("render_hide_levelcomplete", renderHideLevelComplete);

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Advanced", theme);

    imguiTextTr("Extra Args");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderArgs", renderArgsBuf, sizeof(renderArgsBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_args", std::string(renderArgsBuf));
    }

    imguiTextTr("Video Filter");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderVArgs", renderVideoArgsBuf, sizeof(renderVideoArgsBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_video_args", std::string(renderVideoArgsBuf));
    }

    imguiTextTr("Audio Args");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderAArgs", renderAudioArgsBuf, sizeof(renderAudioArgsBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_audio_args", std::string(renderAudioArgsBuf));
    }

    imguiTextTr("Seconds After");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##renderSecAfter", renderSecondsAfterBuf, sizeof(renderSecondsAfterBuf))) {
        guardAdvancedEdit();
        if (advancedWarningAccepted)
            mod->setSavedValue("render_seconds_after", std::string(renderSecondsAfterBuf));
    }

    bool apiAvail = Loader::get()->isModLoaded("eclipse.ffmpeg-api");
    ImGui::Dummy(ImVec2(0, 4));
    Widgets::StatusBadge(apiAvail ? "FFmpeg API Available" : "FFmpeg API Not Found",
        apiAvail ? ImVec4(0.3f, 0.85f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Rendered Videos", theme);

    std::filesystem::path renderFolder = Mod::get()->getSettingValue<std::filesystem::path>("render_folder");
    if (renderFolder.empty() || toasty::pathToUtf8(renderFolder).find("{gd_dir}") != std::string::npos)
        renderFolder = geode::dirs::getGameDir() / "renders";

    std::vector<std::filesystem::directory_entry> renderFiles;
    if (std::filesystem::exists(renderFolder)) {
        for (auto& entry : std::filesystem::directory_iterator(renderFolder)) {
            if (entry.is_regular_file()) renderFiles.push_back(entry);
        }
    }

    float rvListPadY = 8.0f;
    float rvListPadX = 10.0f;
    float rvListH = std::max(80.0f, std::min(200.0f, (float)renderFiles.size() * 28.0f + rvListPadY * 2.0f));
    ImVec2 rvListPos = ImGui::GetCursorScreenPos();
    float rvListW = ImGui::GetContentRegionAvail().x;
    drawSolidRect(ImGui::GetWindowDrawList(), rvListPos, ImVec2(rvListPos.x + rvListW, rvListPos.y + rvListH), theme.cornerRadius, theme, 0.55f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##RenderList", ImVec2(-1, rvListH), false);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rvListPadX);
    ImGui::Dummy(ImVec2(0, rvListPadY));

    if (renderFiles.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rvListPadX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
        imguiTextTr("No rendered videos");
        ImGui::PopStyleColor();
    }

    for (auto& entry : renderFiles) {
        std::string name = toasty::pathToUtf8(entry.path().stem());
        std::string ext = toasty::pathToUtf8(entry.path().extension());
        ImGui::PushID(name.c_str());

        float rowH = ImGui::GetTextLineHeight() + 8.0f;
        float fullRowW = ImGui::GetContentRegionAvail().x;

        auto fileSize = entry.file_size();
        int w = 0, h = 0;
        std::string sizeStr;
        if (fileSize >= 1024 * 1024 * 1024)
            sizeStr = fmt::format("{:.1f} GB", fileSize / (1024.0 * 1024.0 * 1024.0));
        else if (fileSize >= 1024 * 1024)
            sizeStr = fmt::format("{:.1f} MB", fileSize / (1024.0 * 1024.0));
        else
            sizeStr = fmt::format("{:.0f} KB", fileSize / 1024.0);

        std::string resBadge;
        ImVec4 badgeColor(0.5f, 0.5f, 0.5f, 1.0f);
        int parsedW = 0, parsedH = 0;
        std::regex resRegex("_(\\d+)x(\\d+)_");
        std::smatch resMatch;
        if (std::regex_search(name, resMatch, resRegex)) {
            if (auto widthValue = toasty::parseInteger<int>(resMatch[1].str())) {
                parsedW = *widthValue;
            }
            if (auto heightValue = toasty::parseInteger<int>(resMatch[2].str())) {
                parsedH = *heightValue;
            }
        }
        if (parsedW == 1280 && parsedH == 720) { resBadge = "720p"; badgeColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); }
        else if (parsedW == 1920 && parsedH == 1080) { resBadge = "1080p"; badgeColor = ImVec4(0.3f, 0.85f, 0.4f, 1.0f); }
        else if (parsedW == 2560 && parsedH == 1440) { resBadge = "1440p"; badgeColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f); }
        else if (parsedW == 3840 && parsedH == 2160) { resBadge = "4K"; badgeColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); }
        else if (parsedW > 0 && parsedH > 0) { resBadge = fmt::format("{}x{}", parsedW, parsedH); }

        float badgeW = resBadge.empty() ? 0.0f : ImGui::CalcTextSize(resBadge.c_str()).x + 18.0f;
        float sizeW = ImGui::CalcTextSize(sizeStr.c_str()).x;
        float maxNameW = std::max(40.0f, fullRowW - badgeW - sizeW - rvListPadX * 2.0f - 24.0f);
        std::string rowLabel = name + ext;
        if (ImGui::CalcTextSize(rowLabel.c_str()).x > maxNameW) {
            std::string clipped = rowLabel;
            while (!clipped.empty() && ImGui::CalcTextSize((clipped + "...").c_str()).x > maxNameW)
                clipped.pop_back();
            rowLabel = clipped + "...";
        }

        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        ImGui::Selectable("##rvrow", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(fullRowW, rowH));
        float itemMinY = rowStart.y;

        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowStart.x + rvListPadX, itemMinY + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
            theme.getTextU32(), rowLabel.c_str()
        );

        float tagX = rowStart.x + fullRowW - rvListPadX;
        tagX -= sizeW;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(tagX, itemMinY + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
            theme.getTextSecondaryU32(), sizeStr.c_str()
        );

        if (!resBadge.empty()) {
            ImVec2 badgeTextSize = ImGui::CalcTextSize(resBadge.c_str());
            float bPadX = 6.0f, bPadY = 2.0f;
            float bW = badgeTextSize.x + bPadX * 2;
            float bH = badgeTextSize.y + bPadY * 2;
            tagX -= bW + 8.0f;
            float bY = itemMinY + (rowH - bH) * 0.5f;
            ImVec4 bgCol(badgeColor.x * 0.25f, badgeColor.y * 0.25f, badgeColor.z * 0.25f, 0.85f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(tagX, bY), ImVec2(tagX + bW, bY + bH), toU32(bgCol), 999.0f);
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(tagX, bY), ImVec2(tagX + bW, bY + bH), toU32(withAlpha(badgeColor, 0.95f)), 999.0f, 0, 1.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX + bPadX, bY + bPadY), toU32(withAlpha(badgeColor, 0.98f)), resBadge.c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 6));
    if (Widgets::StyledButton("Open Folder", ImVec2(-1, 32), theme, anim)) {
        if (!std::filesystem::exists(renderFolder))
            std::filesystem::create_directories(renderFolder);
        geode::utils::file::openFolder(renderFolder);
    }
}

void MenuInterface::drawAutoclickerTab() {
    auto* ac = Autoclicker::get();
    auto* mod = Mod::get();
    auto* eng = ReplayEngine::get();

    ImGui::Dummy(ImVec2(0, 4));
    if (Widgets::ModuleCard("Autoclicker", "Auto-click at configurable intervals", &ac->enabled, theme, anim))
        mod->setSavedValue("ac_enabled", ac->enabled);

    if (ac->enabled && eng->engineMode == MODE_EXECUTE) {
        Widgets::StatusBadge("PAUSED (PLAYBACK)", ImVec4(0.8f, 0.6f, 0.2f, 1.0f));
    } else if (ac->enabled) {
        Widgets::StatusBadge("ACTIVE", ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Players", theme);

    if (Widgets::ToggleSwitch("Player 1", &ac->player1, theme, anim))
        mod->setSavedValue("ac_player1", ac->player1);
    if (Widgets::ToggleSwitch("Player 2", &ac->player2, theme, anim))
        mod->setSavedValue("ac_player2", ac->player2);

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Timing", theme);

    if (Widgets::StyledSliderInt("Hold Ticks", &ac->holdTicks, 1, 120, theme))
        mod->setSavedValue("ac_hold_ticks", ac->holdTicks);
    if (Widgets::StyledSliderInt("Release Ticks", &ac->releaseTicks, 1, 120, theme))
        mod->setSavedValue("ac_release_ticks", ac->releaseTicks);

    float cps = static_cast<float>(eng->tickRate) / static_cast<float>(ac->holdTicks + ac->releaseTicks);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(theme.textSecondary));
    auto cpsText = trFormat(
        "~{cps} clicks/sec at {tps} TPS",
        fmt::arg("cps", fmt::format("{:.1f}", cps)),
        fmt::arg("tps", fmt::format("{:.0f}", eng->tickRate))
    );
    ImGui::TextUnformatted(cpsText.c_str());
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Options", theme);

    if (Widgets::ToggleSwitch("Only While Holding", &ac->onlyWhileHolding, theme, anim))
        mod->setSavedValue("ac_only_holding", ac->onlyWhileHolding);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(theme.textSecondary));
    imguiTextWrappedTr("When enabled, only auto-clicks while you hold the jump button.");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Keybind", theme);

    Widgets::KeybindButton("Toggle Autoclicker", &keybinds.autoclicker, theme, anim);
}

void MenuInterface::drawClicksTab() {
    auto* csm = ClickSoundManager::get();
    auto* mod = Mod::get();

    if (!clickPacksScanned) {
        csm->scanClickPacks();
        csm->scanClickPacksP2();
        clickPacksScanned = true;

        if (!csm->activePackName.empty()) {
            for (int i = 0; i < (int)csm->availablePacks.size(); i++) {
                if (csm->availablePacks[i] == csm->activePackName) { clickPackIndex = i; break; }
            }
        }
        if (!csm->activePackNameP2.empty()) {
            for (int i = 0; i < (int)csm->availablePacksP2.size(); i++) {
                if (csm->availablePacksP2[i] == csm->activePackNameP2) { clickPackIndexP2 = i; break; }
            }
        }
    }

    ImGui::Dummy(ImVec2(0, 4));
    if (Widgets::ModuleCard("Click Sounds", "Play click and release sounds on input", &csm->enabled, theme, anim))
        mod->setSavedValue("click_enabled", csm->enabled);

    ImGui::Dummy(ImVec2(0, 6));
    Widgets::SectionHeader("Click Pack", theme);

    float btnW = 80.0f;
    float comboW = ImGui::GetContentRegionAvail().x - btnW - 8.0f;

    if (csm->availablePacks.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        imguiTextWrappedTr("No click packs found. Use Open Folder to add packs.");
        ImGui::PopStyleColor();
    } else {
        ImGui::SetNextItemWidth(comboW);
        if (ImGui::BeginCombo("##clickpack", csm->availablePacks[clickPackIndex].c_str())) {
            for (int i = 0; i < (int)csm->availablePacks.size(); i++) {
                bool selected = (clickPackIndex == i);
                if (ImGui::Selectable(csm->availablePacks[i].c_str(), selected)) {
                    clickPackIndex = i;
                    csm->activePackName = csm->availablePacks[i];
                    csm->loadClickPack(csm->activePackName, csm->p1Pack);
                    mod->setSavedValue("click_pack", csm->activePackName);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();
    if (Widgets::StyledButton("Refresh", ImVec2(btnW, 0), theme, anim)) {
        csm->scanClickPacks();
        clickPackIndex = 0;
        clickPackIndexP2 = 0;
        if (!csm->availablePacks.empty()) {
            csm->activePackName = csm->availablePacks[0];
            csm->loadClickPack(csm->activePackName, csm->p1Pack);
        }
    }

    ImGui::Dummy(ImVec2(0, 4));
    if (Widgets::StyledButton("Open Folder", ImVec2(-1, 32), theme, anim))
        csm->openClickFolder();

    if (!csm->p1Pack.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.getAccentU32() ? ImVec4(theme.textSecondary) : ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
        auto packStats = trFormat(
            "Hard: {hard}  Soft: {soft}  Release: {release}  Noise: {noise}",
            fmt::arg("hard", csm->p1Pack.hardCount()),
            fmt::arg("soft", csm->p1Pack.softCount()),
            fmt::arg("release", csm->p1Pack.releaseCount()),
            fmt::arg("noise", csm->p1Pack.noiseCount())
        );
        ImGui::TextUnformatted(packStats.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImVec4 linkColor = theme.getAccent();
    linkColor.w = 0.9f;
    ImGui::PushStyleColor(ImGuiCol_Text, linkColor);
    imguiTextWrappedTr("Find click sounds here");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 tMin = ImGui::GetItemRectMin();
        ImVec2 tMax = ImGui::GetItemRectMax();
        dl->AddLine(ImVec2(tMin.x, tMax.y), ImVec2(tMax.x, tMax.y), toU32(linkColor), 1.0f);
    }
    if (ImGui::IsItemClicked()) {
        geode::utils::web::openLinkInBrowser("https://discord.gg/NmTAZ2qHwj");
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Volume", theme);

    if (!csm->p1Pack.hardClicks.empty()) {
        if (Widgets::StyledSliderFloat("Hard Click", &csm->p1Pack.hardVolume, 0.0f, 2.0f, theme, true))
            mod->setSavedValue("click_hard_vol", (double)csm->p1Pack.hardVolume);
    }
    if (!csm->p1Pack.softClicks.empty()) {
        if (Widgets::StyledSliderFloat("Soft Click", &csm->p1Pack.softVolume, 0.0f, 2.0f, theme, true))
            mod->setSavedValue("click_soft_vol", (double)csm->p1Pack.softVolume);
    }
    if (csm->p1Pack.releaseCount() > 0) {
        if (Widgets::StyledSliderFloat("Release", &csm->p1Pack.releaseVolume, 0.0f, 2.0f, theme, true))
            mod->setSavedValue("click_release_vol", (double)csm->p1Pack.releaseVolume);
    }

    if (csm->p1Pack.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        imguiTextWrappedTr("Select a click pack to see volume controls.");
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Behavior", theme);

    if (Widgets::StyledSliderFloat("Softness", &csm->softness, 0.0f, 1.0f, theme))
        mod->setSavedValue("click_softness", (double)csm->softness);
    if (Widgets::StyledSliderFloat("Delay Min (ms)", &csm->clickDelayMin, 0.0f, 100.0f, theme)) {
        if (csm->clickDelayMin > csm->clickDelayMax) csm->clickDelayMax = csm->clickDelayMin;
        mod->setSavedValue("click_delay_min", (double)csm->clickDelayMin);
        mod->setSavedValue("click_delay_max", (double)csm->clickDelayMax);
    }
    if (Widgets::StyledSliderFloat("Delay Max (ms)", &csm->clickDelayMax, 0.0f, 100.0f, theme)) {
        if (csm->clickDelayMax < csm->clickDelayMin) csm->clickDelayMin = csm->clickDelayMax;
        mod->setSavedValue("click_delay_min", (double)csm->clickDelayMin);
        mod->setSavedValue("click_delay_max", (double)csm->clickDelayMax);
    }
    if (Widgets::ToggleSwitch("Play During Playback", &csm->playDuringPlayback, theme, anim))
        mod->setSavedValue("click_play_during_playback", csm->playDuringPlayback);

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Background Noise", theme);

    if (csm->p1Pack.noiseFiles.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        imguiTextWrappedTr("No noise files found. Add a 'noise' folder to your click pack.");
        ImGui::PopStyleColor();
    } else {
        if (Widgets::ToggleSwitch("Enable Background Noise", &csm->backgroundNoiseEnabled, theme, anim)) {
            mod->setSavedValue("click_bg_noise", csm->backgroundNoiseEnabled);
            if (csm->backgroundNoiseEnabled)
                csm->startBackgroundNoise();
            else
                csm->stopBackgroundNoise();
        }
        if (Widgets::StyledSliderFloat("Noise Volume", &csm->backgroundNoiseVolume, 0.0f, 2.0f, theme, true)) {
            mod->setSavedValue("click_bg_noise_vol", (double)csm->backgroundNoiseVolume);
            if (csm->bgNoiseChannel)
                csm->bgNoiseChannel->setVolume(csm->backgroundNoiseVolume);
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Player 2", theme);

    if (Widgets::ToggleSwitch("Separate P2 Clicks", &csm->separateP2Clicks, theme, anim))
        mod->setSavedValue("click_separate_p2", csm->separateP2Clicks);

    if (csm->separateP2Clicks) {
        ImGui::Dummy(ImVec2(0, 6));
        Widgets::SectionHeader("P2 Click Pack", theme);

        float p2BtnW = 80.0f;
        float p2ComboW = ImGui::GetContentRegionAvail().x - p2BtnW - 8.0f;

        if (csm->availablePacksP2.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
            imguiTextWrappedTr("No P2 click packs found. Use Open P2 Folder to add packs.");
            ImGui::PopStyleColor();
        } else {
            ImGui::SetNextItemWidth(p2ComboW);
            if (ImGui::BeginCombo("##clickpackp2", csm->availablePacksP2[clickPackIndexP2].c_str())) {
                for (int i = 0; i < (int)csm->availablePacksP2.size(); i++) {
                    bool selected = (clickPackIndexP2 == i);
                    if (ImGui::Selectable(csm->availablePacksP2[i].c_str(), selected)) {
                        clickPackIndexP2 = i;
                        csm->activePackNameP2 = csm->availablePacksP2[i];
                        csm->loadClickPack(csm->activePackNameP2, csm->p2Pack, true);
                        mod->setSavedValue("click_pack_p2", csm->activePackNameP2);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        ImGui::SameLine();
        if (Widgets::StyledButton("Refresh##p2", ImVec2(p2BtnW, 0), theme, anim)) {
            csm->scanClickPacksP2();
            clickPackIndexP2 = 0;
            if (!csm->availablePacksP2.empty()) {
                csm->activePackNameP2 = csm->availablePacksP2[0];
                csm->loadClickPack(csm->activePackNameP2, csm->p2Pack, true);
            }
        }

        ImGui::Dummy(ImVec2(0, 4));
        if (Widgets::StyledButton("Open P2 Folder", ImVec2(-1, 32), theme, anim))
            csm->openClickFolderP2();

        if (!csm->p2Pack.empty()) {
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
            auto packStats = trFormat(
                "Hard: {hard}  Soft: {soft}  Release: {release}  Noise: {noise}",
                fmt::arg("hard", csm->p2Pack.hardCount()),
                fmt::arg("soft", csm->p2Pack.softCount()),
                fmt::arg("release", csm->p2Pack.releaseCount()),
                fmt::arg("noise", csm->p2Pack.noiseCount())
            );
            ImGui::TextUnformatted(packStats.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0, 4));

        if (!csm->p2Pack.hardClicks.empty()) {
            if (Widgets::StyledSliderFloat("P2 Hard Click", &csm->p2Pack.hardVolume, 0.0f, 2.0f, theme, true))
                mod->setSavedValue("click_hard_vol_p2", (double)csm->p2Pack.hardVolume);
        }
        if (!csm->p2Pack.softClicks.empty()) {
            if (Widgets::StyledSliderFloat("P2 Soft Click", &csm->p2Pack.softVolume, 0.0f, 2.0f, theme, true))
                mod->setSavedValue("click_soft_vol_p2", (double)csm->p2Pack.softVolume);
        }
        if (csm->p2Pack.releaseCount() > 0) {
            if (Widgets::StyledSliderFloat("P2 Release", &csm->p2Pack.releaseVolume, 0.0f, 2.0f, theme, true))
                mod->setSavedValue("click_release_vol_p2", (double)csm->p2Pack.releaseVolume);
        }
    }
}

void MenuInterface::drawSettingsTab() {
    auto* eng = ReplayEngine::get();
    auto* mod = Mod::get();

    Widgets::SectionHeader("Customization", theme);

    imguiTextTr("Language");
    ImGui::SetNextItemWidth(-1);
    {
        using toasty::i18n::UiLanguage;
        static constexpr UiLanguage languages[] = {
            UiLanguage::Auto,
            UiLanguage::English,
            UiLanguage::Spanish,
        };

        UiLanguage configuredLanguage = toasty::i18n::getConfiguredLanguage();
        std::string preview = std::string(toasty::i18n::getLanguageDisplayName(configuredLanguage));
        if (ImGui::BeginCombo("##uiLanguage", preview.c_str())) {
            for (auto language : languages) {
                std::string displayName = std::string(toasty::i18n::getLanguageDisplayName(language));
                bool selected = configuredLanguage == language;
                if (ImGui::Selectable(displayName.c_str(), selected)) {
                    mod->setSettingValue<std::string>(
                        "ui_language",
                        std::string(toasty::i18n::getLanguageSettingValue(language))
                    );
                    toasty::i18n::refresh();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    imguiTextTr("Theme Preset");
    ImGui::SetNextItemWidth(-1);
    int presetCount = ThemeEngine::getPresetCount();
    const ThemePreset* presets = ThemeEngine::getPresets();
    auto customPresetText = trString("Custom");
    if (ImGui::BeginCombo("##themePreset", theme.activePreset >= 0 && theme.activePreset < presetCount ? presets[theme.activePreset].name : customPresetText.c_str())) {
        for (int i = 0; i < presetCount; i++) {
            bool selected = (theme.activePreset == i);
            if (ImGui::Selectable(presets[i].name, selected)) {
                theme.applyPreset(i);
                saveSettings();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Dummy(ImVec2(0, 8));
    imguiTextTr("Accent Color");
    ImGui::SameLine();
    if (ImGui::ColorEdit4("##accentColor", (float*)&theme.accentColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        theme.activePreset = -1;
    }

    ImGui::Dummy(ImVec2(0, 4));
    imguiTextTr("Background Color");
    ImGui::SameLine();
    if (ImGui::ColorEdit4("##bgColor", (float*)&theme.bgColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        theme.activePreset = -1;
    }

    ImGui::Dummy(ImVec2(0, 4));
    imguiTextTr("Card Color");
    ImGui::SameLine();
    if (ImGui::ColorEdit4("##cardColor", (float*)&theme.cardColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        theme.activePreset = -1;
    }

    ImGui::Dummy(ImVec2(0, 4));
    imguiTextTr("Text Color");
    ImGui::SameLine();
    if (ImGui::ColorEdit4("##textColor", (float*)&theme.textPrimary,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        theme.activePreset = -1;
    }

    ImGui::Dummy(ImVec2(0, 4));
    imguiTextTr("Secondary Text");
    ImGui::SameLine();
    if (ImGui::ColorEdit4("##text2Color", (float*)&theme.textSecondary,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
        theme.activePreset = -1;
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::StyledSliderFloat("Corner Rounding", &theme.cornerRadius, 0.0f, 16.0f, theme);
    ImGui::Dummy(ImVec2(0, 8));
    Widgets::StyledSliderFloat("Background Opacity", &theme.bgOpacity, 0.50f, 1.0f, theme);

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::StyledSliderFloat("Animation Speed", &anim.animSpeed, 2.0f, 24.0f, theme);

    ImGui::Dummy(ImVec2(0, 8));
    imguiTextTr("Open Animation");
    std::array<std::string, 5> animDirNamesStorage = {
        trString("Center"),
        trString("From Left"),
        trString("From Right"),
        trString("From Top"),
        trString("From Bottom"),
    };
    const char* animDirNames[] = {
        animDirNamesStorage[0].c_str(),
        animDirNamesStorage[1].c_str(),
        animDirNamesStorage[2].c_str(),
        animDirNamesStorage[3].c_str(),
        animDirNamesStorage[4].c_str(),
    };
    int currentDir = (int)anim.openDirection;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##animDir", &currentDir, animDirNames, 5))
        anim.openDirection = (AnimDirection)currentDir;

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::ToggleSwitch("Glow Color Cycle", &theme.glowCycleEnabled, theme, anim);
    if (theme.glowCycleEnabled) {
        ImGui::Dummy(ImVec2(0, 4));
        Widgets::StyledSliderFloat("Cycle Speed", &theme.glowCycleRate, 0.02f, 1.0f, theme);
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::ToggleSwitch("Ambient Background", &ambientWavesEnabled, theme, anim);

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Input Accuracy", theme);

    bool hasSyzziCBF = AccuracyRuntime::isSyzziCBFAvailable();
    bool cbsEnabled = eng->selectedAccuracyMode == AccuracyMode::CBS;
    bool cbfEnabled = eng->selectedAccuracyMode == AccuracyMode::CBF;
    bool accuracyChanged = false;

    if (eng->engineMode != MODE_DISABLED) ImGui::BeginDisabled();
    if (Widgets::ToggleSwitch("CBS", &cbsEnabled, theme, anim)) {
        eng->selectedAccuracyMode = cbsEnabled ? AccuracyMode::CBS : AccuracyMode::Vanilla;
        accuracyChanged = true;
    }

    if (!hasSyzziCBF) ImGui::BeginDisabled();
    if (Widgets::ToggleSwitch("CBF", &cbfEnabled, theme, anim)) {
        eng->selectedAccuracyMode = cbfEnabled ? AccuracyMode::CBF : AccuracyMode::Vanilla;
        accuracyChanged = true;
    }
    if (!hasSyzziCBF) ImGui::EndDisabled();
    if (eng->engineMode != MODE_DISABLED) ImGui::EndDisabled();

    if (!hasSyzziCBF) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        imguiTextWrappedTr("Install syzzi.click_between_frames to enable CBF mode.");
        ImGui::PopStyleColor();
    } else if (eng->engineMode != MODE_DISABLED) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        imguiTextWrappedTr("Stop recording or playback before changing accuracy mode.");
        ImGui::PopStyleColor();
    }

    if (accuracyChanged) {
        AccuracyRuntime::applyRuntimeAccuracyMode(eng->selectedAccuracyMode);
        saveSettings();
    }

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Advanced", theme);

    Widgets::ModuleCard("FastPlayback", "Start playback without restarting the level", &eng->fastPlayback, theme, anim);

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Keybinds", theme);

    struct KeybindEntry { const char* label; int* keyPtr; bool alwaysShow; };
    KeybindEntry kbEntries[] = {
        {"Menu Toggle",   &keybinds.menu,          true},
        {"Frame Advance", &keybinds.frameAdvance,  true},
        {"Frame Step",    &keybinds.frameStep,     true},
        {"Replay Toggle", &keybinds.replayToggle,  false},
        {"Noclip",        &keybinds.noclip,        false},
        {"Safe Mode",     &keybinds.safeMode,      false},
        {"Trajectory",    &keybinds.trajectory,     false},
        {"Hitboxes",      &keybinds.hitboxes,      false},
        {"Audio Pitch",   &keybinds.audioPitch,    false},
        {"RNG Lock",      &keybinds.rngLock,       false},
        {"Layout Mode",   &keybinds.layoutMode,    false},
        {"No Mirror",     &keybinds.noMirror,      false},
    };
    bool firstKb = true;
    for (auto& e : kbEntries) {
        if (!e.alwaysShow && *e.keyPtr == 0) continue;
        if (!firstKb) ImGui::Dummy(ImVec2(0, 4));
        Widgets::KeybindButton(e.label, e.keyPtr, theme, anim);
        firstKb = false;
    }

    ImGui::Dummy(ImVec2(0, 12));
    if (Widgets::StyledButton("Reset to Defaults", ImVec2(-1, 32), theme, anim)) {
        theme.resetDefaults();
        anim.animSpeed = 8.0f;
        anim.openDirection = ANIM_CENTER;
        eng->fastPlayback = false;
        eng->selectedAccuracyMode = AccuracyMode::Vanilla;
        AccuracyRuntime::applyRuntimeAccuracyMode(eng->selectedAccuracyMode);
        ambientWavesEnabled = true;
        saveSettings();
    }
}

static void saveColor(const char* prefix, const ImVec4& col) {
    auto* mod = Mod::get();
    mod->setSavedValue(std::string(prefix) + "_r", col.x);
    mod->setSavedValue(std::string(prefix) + "_g", col.y);
    mod->setSavedValue(std::string(prefix) + "_b", col.z);
    mod->setSavedValue(std::string(prefix) + "_a", col.w);
}

static ImVec4 loadColor(const char* prefix, const ImVec4& def) {
    auto* mod = Mod::get();
    return ImVec4(
        mod->getSavedValue<float>(std::string(prefix) + "_r", def.x),
        mod->getSavedValue<float>(std::string(prefix) + "_g", def.y),
        mod->getSavedValue<float>(std::string(prefix) + "_b", def.z),
        mod->getSavedValue<float>(std::string(prefix) + "_a", def.w)
    );
}

void MenuInterface::drawOnlineTab() {
    auto* online = OnlineClient::get();
    auto* engine = ReplayEngine::get();
    auto uploadRestriction = online->getRestrictionMessage(true);
    auto issueRestriction = online->getRestrictionMessage(false);

    
    Widgets::SectionHeader("Discord Account", theme);
    ImGui::Dummy(ImVec2(0, 4));

    if (online->isLinked()) {
        Widgets::StatusBadge("LINKED", ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
        ImGui::Dummy(ImVec2(0, 8));

        float avatarSize = 48.0f;

        if (online->avatarTexture) {
            auto* tex = online->avatarTexture;
            ImTextureID texId = (ImTextureID)(uintptr_t)(tex->getName());
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 center(cursorPos.x + avatarSize * 0.5f, cursorPos.y + avatarSize * 0.5f);
            float radius = avatarSize * 0.5f;
            dl->AddImageRounded(texId, cursorPos, ImVec2(cursorPos.x + avatarSize, cursorPos.y + avatarSize),
                ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), radius);
            ImGui::Dummy(ImVec2(avatarSize, 0));
            ImGui::SameLine(0, 12.0f);

            float textY = (avatarSize - ImGui::GetFontSize() * 1.4f - ImGui::GetFontSize()) * 0.5f;
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, textY));
            if (fontHeading) ImGui::PushFont(fontHeading);
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);
            ImGui::Text("%s", online->discordUsername.c_str());
            ImGui::PopStyleColor();
            if (fontHeading) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
            auto idText = trFormat("ID: {id}", fmt::arg("id", online->discordId));
            ImGui::TextUnformatted(idText.c_str());
            ImGui::PopStyleColor();
            auto blacklistText = online->getBlacklistStatusText();
            if (!blacklistText.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.25f, 1.0f));
                ImGui::TextWrapped("%s", blacklistText.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();

            ImGui::Dummy(ImVec2(0, avatarSize - ImGui::GetCursorPosY() + cursorPos.y - ImGui::GetWindowPos().y));
        } else {
            if (fontHeading) ImGui::PushFont(fontHeading);
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);
            ImGui::Text("%s", online->discordUsername.c_str());
            ImGui::PopStyleColor();
            if (fontHeading) ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
            auto idText = trFormat("ID: {id}", fmt::arg("id", online->discordId));
            ImGui::TextUnformatted(idText.c_str());
            ImGui::PopStyleColor();
            auto blacklistText = online->getBlacklistStatusText();
            if (!blacklistText.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.25f, 1.0f));
                ImGui::TextWrapped("%s", blacklistText.c_str());
                ImGui::PopStyleColor();
            }

            if (!online->avatarLoading && !online->avatarLoaded) {
                online->fetchAvatar();
            }
        }

        ImGui::Dummy(ImVec2(0, 6));
        if (Widgets::StyledButton("Unlink Account", ImVec2(-1, 32), theme, anim)) {
            online->unlinkAccount();
        }
    } else if (online->authPolling) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        imguiTextWrappedTr("Waiting for Discord authorization...");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
        if (Widgets::StyledButton("Cancel", ImVec2(-1, 32), theme, anim)) {
            online->stopAuthPolling();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        imguiTextTr("Not linked");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
        if (Widgets::StyledButton("Link Discord Account", ImVec2(-1, 32), theme, anim)) {
            online->startAuthFlow();
        }
    }

    
    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Upload Macro", theme);
    ImGui::Dummy(ImVec2(0, 4));

    auto& macros = engine->storedMacros;

    if (macros.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        imguiTextWrappedTr("No saved macros found.");
        ImGui::PopStyleColor();
    } else {
        
        if (selectedUploadMacro >= static_cast<int>(macros.size()))
            selectedUploadMacro = -1;

        auto emptyMacroPreview = trString("Select a macro...");
        const char* preview = selectedUploadMacro >= 0 ? macros[selectedUploadMacro].c_str() : emptyMacroPreview.c_str();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 25, 200));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(25, 25, 30, 240));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::BeginCombo("##macro_select", preview)) {
            for (int i = 0; i < static_cast<int>(macros.size()); i++) {
                
                if (engine->incompatibleMacros.count(macros[i])) continue;

                bool selected = (selectedUploadMacro == i);
                std::string label = macros[i];
                if (engine->ttrMacros.count(macros[i])) label += "  [TTR]";
                else label += "  [GDR]";

                if (ImGui::Selectable(label.c_str(), selected)) {
                    selectedUploadMacro = i;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImGui::Dummy(ImVec2(0, 4));

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(theme.textSecondary));
        imguiTextTr("Comment (optional)");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 25, 200));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::InputTextMultiline("##upload_comment", uploadCommentBuf, sizeof(uploadCommentBuf), ImVec2(-1, 60));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 4));

        bool canUpload = selectedUploadMacro >= 0 &&
            online->uploadState != OnlineClient::PENDING &&
            online->canUploadMacros();
        if (!canUpload) ImGui::BeginDisabled();
        if (Widgets::StyledButton("Upload to Discord", ImVec2(-1, 32), theme, anim)) {
            std::string comment(uploadCommentBuf);
            online->uploadMacro(macros[selectedUploadMacro], comment);
            uploadCommentBuf[0] = '\0';
        }
        if (!canUpload) ImGui::EndDisabled();
    }

    if (!uploadRestriction.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        ImGui::TextWrapped("%s", uploadRestriction.c_str());
        ImGui::PopStyleColor();
    }

    
    if (!online->uploadResultMsg.empty() && online->uploadResultMsg != uploadRestriction) {
        ImGui::Dummy(ImVec2(0, 4));
        ImVec4 color = (online->uploadState == OnlineClient::SUCCESS)
            ? ImVec4(0.2f, 0.8f, 0.4f, 1.0f)
            : (online->uploadState == OnlineClient::RSERROR)
                ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
                : theme.textSecondary;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", online->uploadResultMsg.c_str());
        ImGui::PopStyleColor();
    }

    
    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Report Issue", theme);
    ImGui::Dummy(ImVec2(0, 4));

    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 25, 200));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);
    imguiTextTr("Title");
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##issue_title", issueTitleBuf, sizeof(issueTitleBuf));

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);
    imguiTextTr("Description");
    ImGui::PopStyleColor();
    ImGui::InputTextMultiline("##issue_desc", issueDescBuf, sizeof(issueDescBuf), ImVec2(-1, 100));

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 4));

    size_t titleLength = std::strlen(issueTitleBuf);
    size_t descLength = std::strlen(issueDescBuf);
    bool titleOk = titleLength >= 5 && titleLength <= 100;
    bool descOk = descLength >= 10 && descLength <= 1500;
    bool canSubmit = titleOk &&
        descOk &&
        online->issueState != OnlineClient::PENDING &&
        online->canSubmitIssues();

    if (!canSubmit) ImGui::BeginDisabled();
    if (Widgets::StyledButton("Submit Issue", ImVec2(-1, 32), theme, anim)) {
        online->submitIssue(issueTitleBuf, issueDescBuf);
        std::memset(issueTitleBuf, 0, sizeof(issueTitleBuf));
        std::memset(issueDescBuf, 0, sizeof(issueDescBuf));
    }
    if (!canSubmit) ImGui::EndDisabled();

    if (!titleOk || !descOk) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        imguiTextTr("Title must be 5-100 chars. Description must be 10-1500 chars.");
        ImGui::PopStyleColor();
    }

    if (!issueRestriction.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        ImGui::TextWrapped("%s", issueRestriction.c_str());
        ImGui::PopStyleColor();
    }

    
    if (!online->issueResultMsg.empty() && online->issueResultMsg != issueRestriction) {
        ImGui::Dummy(ImVec2(0, 4));
        ImVec4 color = (online->issueState == OnlineClient::SUCCESS)
            ? ImVec4(0.2f, 0.8f, 0.4f, 1.0f)
            : (online->issueState == OnlineClient::RSERROR)
                ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f)
                : theme.textSecondary;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", online->issueResultMsg.c_str());
        ImGui::PopStyleColor();
    }
}

void MenuInterface::saveSettings() {
    auto* mod = Mod::get();
    auto* eng = ReplayEngine::get();
    OnlineClient::get()->save();

    if (!renderBufsInit) {
        loadRenderSettings();
    }

    saveColor("theme_accent", theme.accentColor);
    saveColor("theme_bg", theme.bgColor);
    saveColor("theme_card", theme.cardColor);
    saveColor("theme_text", theme.textPrimary);
    saveColor("theme_text2", theme.textSecondary);
    mod->setSavedValue("theme_bg_opacity", theme.bgOpacity);
    mod->setSavedValue("theme_corner_radius", theme.cornerRadius);
    mod->setSavedValue("theme_active_preset", theme.activePreset);
    mod->setSavedValue("theme_glow_cycle", theme.glowCycleEnabled);
    mod->setSavedValue("theme_glow_rate", theme.glowCycleRate);
    mod->setSavedValue("ambient_waves", ambientWavesEnabled);

    mod->setSavedValue("anim_speed", anim.animSpeed);
    mod->setSavedValue("anim_direction", (int)anim.openDirection);

    mod->setSavedValue("key_menu", keybinds.menu);
    mod->setSavedValue("key_frame_advance", keybinds.frameAdvance);
    mod->setSavedValue("key_frame_step", keybinds.frameStep);
    mod->setSavedValue("key_replay_toggle", keybinds.replayToggle);
    mod->setSavedValue("key_noclip", keybinds.noclip);
    mod->setSavedValue("key_safe_mode", keybinds.safeMode);
    mod->setSavedValue("key_trajectory", keybinds.trajectory);
    mod->setSavedValue("key_audio_pitch", keybinds.audioPitch);
    mod->setSavedValue("key_rng_lock", keybinds.rngLock);
    mod->setSavedValue("key_hitboxes", keybinds.hitboxes);
    mod->setSavedValue("key_layout_mode", keybinds.layoutMode);
    mod->setSavedValue("key_no_mirror", keybinds.noMirror);

    mod->setSavedValue("hack_hitboxes", eng->showHitboxes);
    mod->setSavedValue("hack_hitbox_death", eng->hitboxOnDeath);
    mod->setSavedValue("hack_hitbox_trail", eng->hitboxTrail);
    mod->setSavedValue("hack_hitbox_trail_len", eng->hitboxTrailLength);
    mod->setSavedValue("hack_trajectory", eng->pathPreview);
    mod->setSavedValue("hack_trajectory_len", eng->pathLength);
    mod->setSavedValue("hack_noclip", eng->collisionBypass);
    mod->setSavedValue("hack_noclip_flash", eng->noclipDeathFlash);
    mod->setSavedValue("hack_noclip_color_r", eng->noclipDeathColorR);
    mod->setSavedValue("hack_noclip_color_g", eng->noclipDeathColorG);
    mod->setSavedValue("hack_noclip_color_b", eng->noclipDeathColorB);
    mod->setSavedValue("hack_rng_lock", eng->rngLocked);
    mod->setSavedValue("hack_rng_seed", eng->rngSeedVal);
    mod->setSavedValue("hack_safe_mode", eng->protectedMode);
    mod->setSavedValue("hack_audio_pitch", eng->audioPitchEnabled);
    mod->setSavedValue("hack_no_mirror", eng->noMirrorEffect);
    mod->setSavedValue("hack_layout_mode", eng->layoutMode);
    mod->setSavedValue("hack_no_mirror_rec_only", eng->noMirrorRecordingOnly);
    mod->setSavedValue("hack_fast_playback", eng->fastPlayback);

    auto* ac = Autoclicker::get();
    mod->setSavedValue("ac_enabled", ac->enabled);
    mod->setSavedValue("ac_player1", ac->player1);
    mod->setSavedValue("ac_player2", ac->player2);
    mod->setSavedValue("ac_hold_ticks", ac->holdTicks);
    mod->setSavedValue("ac_release_ticks", ac->releaseTicks);
    mod->setSavedValue("ac_only_holding", ac->onlyWhileHolding);
    mod->setSavedValue("key_autoclicker", keybinds.autoclicker);

    mod->setSavedValue("eng_accuracy_mode", static_cast<int>(eng->selectedAccuracyMode));
    mod->setSavedValue("eng_tick_rate", (float)eng->tickRate);
    mod->setSavedValue("eng_speed", (float)eng->gameSpeed);
    mod->setSavedValue("eng_ttr_mode", eng->ttrMode);

    mod->setSavedValue("render_width", (int64_t)std::atoi(renderWidthBuf));
    mod->setSavedValue("render_height", (int64_t)std::atoi(renderHeightBuf));
    mod->setSavedValue("render_fps", (int64_t)std::atoi(renderFpsBuf));
    mod->setSavedValue("render_codec", std::string(renderCodecBuf));
    mod->setSavedValue("render_bitrate", std::string(renderBitrateBuf));
    mod->setSavedValue("render_file_extension", std::string(renderExtBuf));
    mod->setSavedValue("render_args", std::string(renderArgsBuf));
    mod->setSavedValue("render_video_args", std::string(renderVideoArgsBuf));
    mod->setSavedValue("render_audio_args", std::string(renderAudioArgsBuf));
    mod->setSavedValue("render_seconds_after", std::string(renderSecondsAfterBuf));
    mod->setSavedValue("render_include_audio", renderIncludeAudio);
    mod->setSavedValue("render_include_clicks", renderIncludeClicks);
    mod->setSavedValue("render_sfx_volume", (double)renderSfxVol);
    mod->setSavedValue("render_music_volume", (double)renderMusicVol);
    mod->setSavedValue("render_hide_endscreen", renderHideEndscreen);
    mod->setSavedValue("render_hide_levelcomplete", renderHideLevelComplete);

    auto* csm = ClickSoundManager::get();
    mod->setSavedValue("click_enabled", csm->enabled);
    mod->setSavedValue("click_pack", csm->activePackName);
    mod->setSavedValue("click_hard_vol", (double)csm->p1Pack.hardVolume);
    mod->setSavedValue("click_soft_vol", (double)csm->p1Pack.softVolume);
    mod->setSavedValue("click_release_vol", (double)csm->p1Pack.releaseVolume);
    mod->setSavedValue("click_softness", (double)csm->softness);
    mod->setSavedValue("click_delay_min", (double)csm->clickDelayMin);
    mod->setSavedValue("click_delay_max", (double)csm->clickDelayMax);
    mod->setSavedValue("click_play_during_playback", csm->playDuringPlayback);
    mod->setSavedValue("click_separate_p2", csm->separateP2Clicks);
    mod->setSavedValue("click_pack_p2", csm->activePackNameP2);
    mod->setSavedValue("click_hard_vol_p2", (double)csm->p2Pack.hardVolume);
    mod->setSavedValue("click_soft_vol_p2", (double)csm->p2Pack.softVolume);
    mod->setSavedValue("click_release_vol_p2", (double)csm->p2Pack.releaseVolume);
    mod->setSavedValue("click_bg_noise", csm->backgroundNoiseEnabled);
    mod->setSavedValue("click_bg_noise_vol", (double)csm->backgroundNoiseVolume);

    mod->setSavedValue("window_size_w", windowSize.x);
    mod->setSavedValue("window_size_h", windowSize.y);
    mod->setSavedValue("main_sub_tab", mainSubTab);
}

void MenuInterface::loadRenderSettings() {
    auto* mod = Mod::get();

    auto renderName = mod->getSavedValue<std::string>("render_name", "");
    auto renderWidth = loadSavedValueWithFallback<int64_t>(mod, "render_width", 1920);
    auto renderHeight = loadSavedValueWithFallback<int64_t>(mod, "render_height", 1080);
    auto renderFps = loadSavedValueWithFallback<int64_t>(mod, "render_fps", 60);
    auto renderCodec = loadSavedValueWithFallback<std::string>(mod, "render_codec", "");
    auto renderBitrate = loadSavedValueWithFallback<std::string>(mod, "render_bitrate", "30");
    auto renderExt = loadSavedValueWithFallback<std::string>(mod, "render_file_extension", ".mp4", {"render_extension"});
    auto renderArgs = loadSavedValueWithFallback<std::string>(mod, "render_args", "-pix_fmt yuv420p", {"render_extra_args"});
    auto renderVideoArgs = loadSavedValueWithFallback<std::string>(mod, "render_video_args", "colorspace=all=bt709:iall=bt470bg:fast=1");
    auto renderAudioArgs = loadSavedValueWithFallback<std::string>(mod, "render_audio_args", "");
    auto renderSecondsAfter = loadSavedValueWithFallback<std::string>(mod, "render_seconds_after", "3", {"render_after_seconds"});
    auto includeAudio = loadSavedValueWithFallback<bool>(mod, "render_include_audio", true, {"render_record_audio", "render_capture_audio"});
    auto includeClicks = loadSavedValueWithFallback<bool>(mod, "render_include_clicks", false);
    auto clickVolume = loadSavedValueWithFallback<double>(mod, "render_sfx_volume", 1.0);
    auto musicVolume = loadSavedValueWithFallback<double>(mod, "render_music_volume", 1.0);
    auto hideEndscreen = loadSavedValueWithFallback<bool>(mod, "render_hide_endscreen", false);
    auto hideLevelComplete = loadSavedValueWithFallback<bool>(mod, "render_hide_levelcomplete", false);

    snprintf(renderNameBuf, sizeof(renderNameBuf), "%s", renderName.c_str());
    snprintf(renderWidthBuf, sizeof(renderWidthBuf), "%lld", renderWidth);
    snprintf(renderHeightBuf, sizeof(renderHeightBuf), "%lld", renderHeight);
    snprintf(renderFpsBuf, sizeof(renderFpsBuf), "%lld", renderFps);
    snprintf(renderCodecBuf, sizeof(renderCodecBuf), "%s", renderCodec.c_str());
    snprintf(renderBitrateBuf, sizeof(renderBitrateBuf), "%s", renderBitrate.c_str());
    snprintf(renderExtBuf, sizeof(renderExtBuf), "%s", renderExt.c_str());
    snprintf(renderArgsBuf, sizeof(renderArgsBuf), "%s", renderArgs.c_str());
    snprintf(renderVideoArgsBuf, sizeof(renderVideoArgsBuf), "%s", renderVideoArgs.c_str());
    snprintf(renderAudioArgsBuf, sizeof(renderAudioArgsBuf), "%s", renderAudioArgs.c_str());
    snprintf(renderSecondsAfterBuf, sizeof(renderSecondsAfterBuf), "%s", renderSecondsAfter.c_str());

    renderIncludeAudio = includeAudio;
    renderIncludeClicks = includeClicks;
    renderSfxVol = static_cast<float>(clickVolume);
    renderMusicVol = static_cast<float>(musicVolume);
    renderHideEndscreen = hideEndscreen;
    renderHideLevelComplete = hideLevelComplete;

    renderPresetIndex = 1;
    struct ResPreset { const char* name; int width; int height; };
    static const ResPreset presets[] = {
        { "720p  (1280x720)",   1280,  720 },
        { "1080p (1920x1080)",  1920, 1080 },
        { "1440p (2560x1440)",  2560, 1440 },
        { "4K    (3840x2160)",  3840, 2160 },
    };
    for (int i = 0; i < static_cast<int>(sizeof(presets) / sizeof(presets[0])); i++) {
        if (presets[i].width == renderWidth && presets[i].height == renderHeight) {
            renderPresetIndex = i;
            break;
        }
    }

    snprintf(backupCodecBuf, sizeof(backupCodecBuf), "%s", renderCodecBuf);
    snprintf(backupBitrateBuf, sizeof(backupBitrateBuf), "%s", renderBitrateBuf);
    snprintf(backupExtBuf, sizeof(backupExtBuf), "%s", renderExtBuf);
    snprintf(backupArgsBuf, sizeof(backupArgsBuf), "%s", renderArgsBuf);
    snprintf(backupVideoArgsBuf, sizeof(backupVideoArgsBuf), "%s", renderVideoArgsBuf);
    snprintf(backupAudioArgsBuf, sizeof(backupAudioArgsBuf), "%s", renderAudioArgsBuf);
    snprintf(backupSecondsAfterBuf, sizeof(backupSecondsAfterBuf), "%s", renderSecondsAfterBuf);

    showAdvancedWarning = false;
    advancedWarningAccepted = false;
    renderBufsInit = true;
}

void MenuInterface::loadSettings() {
    auto* mod = Mod::get();
    auto* eng = ReplayEngine::get();
    OnlineClient::get()->load();

    ImVec4 accentDefault(0.15f, 0.80f, 0.75f, 1.0f);
    ImVec4 bgDefault(0.05f, 0.08f, 0.09f, 0.92f);
    ImVec4 cardDefault(0.08f, 0.13f, 0.14f, 1.0f);
    ImVec4 textDefault(0.92f, 0.96f, 0.96f, 1.0f);
    ImVec4 text2Default(0.48f, 0.58f, 0.58f, 1.0f);

    theme.accentColor = sanitizeColor(loadColor("theme_accent", accentDefault), accentDefault);
    theme.bgColor = sanitizeColor(loadColor("theme_bg", bgDefault), bgDefault);
    theme.cardColor = sanitizeColor(loadColor("theme_card", cardDefault), cardDefault);
    theme.textPrimary = sanitizeColor(loadColor("theme_text", textDefault), textDefault);
    theme.textSecondary = sanitizeColor(loadColor("theme_text2", text2Default), text2Default);
    theme.bgOpacity = sanitizeClamped(mod->getSavedValue<float>("theme_bg_opacity", 0.90f), 0.50f, 1.0f, 0.90f);
    theme.cornerRadius = sanitizeClamped(mod->getSavedValue<float>("theme_corner_radius", 5.0f), 0.0f, 16.0f, 5.0f);
    theme.activePreset = mod->getSavedValue<int>("theme_active_preset", 7);

    if (mod->hasSavedValue("theme_glow_cycle") || mod->hasSavedValue("theme_glow_rate")) {
        theme.glowCycleEnabled = mod->getSavedValue<bool>("theme_glow_cycle", false);
        theme.glowCycleRate = sanitizeClamped(mod->getSavedValue<float>("theme_glow_rate", 0.5f), 0.02f, 1.0f, 0.5f);
    } else {
        theme.glowCycleEnabled = mod->getSavedValue<bool>("theme_cycling", false);
        theme.glowCycleRate = sanitizeClamped(mod->getSavedValue<float>("theme_cycle_rate", 0.5f), 0.02f, 1.0f, 0.5f);
    }

    ambientWavesEnabled = mod->getSavedValue<bool>("ambient_waves", true);

    anim.animSpeed = sanitizeClamped(mod->getSavedValue<float>("anim_speed", 8.0f), 2.0f, 24.0f, 8.0f);
    int savedDirection = mod->getSavedValue<int>("anim_direction", ANIM_CENTER);
    anim.openDirection = static_cast<AnimDirection>(std::clamp(savedDirection, static_cast<int>(ANIM_CENTER), static_cast<int>(ANIM_FROM_BOTTOM)));

    keybinds.menu = mod->getSavedValue<int>("key_menu", 0x42);
    keybinds.frameAdvance = mod->getSavedValue<int>("key_frame_advance", 0x56);
    keybinds.frameStep = mod->getSavedValue<int>("key_frame_step", 0x43);
    keybinds.replayToggle = mod->getSavedValue<int>("key_replay_toggle", 0);
    keybinds.noclip = mod->getSavedValue<int>("key_noclip", 0);
    keybinds.safeMode = mod->getSavedValue<int>("key_safe_mode", 0);
    keybinds.trajectory = mod->getSavedValue<int>("key_trajectory", 0);
    keybinds.audioPitch = mod->getSavedValue<int>("key_audio_pitch", 0);
    keybinds.rngLock = mod->getSavedValue<int>("key_rng_lock", 0);
    keybinds.hitboxes = mod->getSavedValue<int>("key_hitboxes", 0);
    keybinds.layoutMode = mod->getSavedValue<int>("key_layout_mode", 0);
    keybinds.noMirror = mod->getSavedValue<int>("key_no_mirror", 0);
    keybinds.autoclicker = mod->getSavedValue<int>("key_autoclicker", 0);

    auto* ac = Autoclicker::get();
    ac->enabled = mod->getSavedValue<bool>("ac_enabled", false);
    ac->player1 = mod->getSavedValue<bool>("ac_player1", true);
    ac->player2 = mod->getSavedValue<bool>("ac_player2", false);
    ac->holdTicks = mod->getSavedValue<int>("ac_hold_ticks", 1);
    ac->releaseTicks = mod->getSavedValue<int>("ac_release_ticks", 1);
    ac->onlyWhileHolding = mod->getSavedValue<bool>("ac_only_holding", false);

    eng->showHitboxes = mod->getSavedValue<bool>("hack_hitboxes", false);
    eng->hitboxOnDeath = mod->getSavedValue<bool>("hack_hitbox_death", false);
    eng->hitboxTrail = mod->getSavedValue<bool>("hack_hitbox_trail", false);
    eng->hitboxTrailLength = mod->getSavedValue<int>("hack_hitbox_trail_len", 240);
    eng->pathPreview = mod->getSavedValue<bool>("hack_trajectory", false);
    eng->pathLength = mod->getSavedValue<int>("hack_trajectory_len", 312);
    eng->collisionBypass = mod->getSavedValue<bool>("hack_noclip", false);
    eng->noclipDeathFlash = mod->getSavedValue<bool>("hack_noclip_flash", true);
    eng->noclipDeathColorR = mod->getSavedValue<float>("hack_noclip_color_r", 1.0f);
    eng->noclipDeathColorG = mod->getSavedValue<float>("hack_noclip_color_g", 0.0f);
    eng->noclipDeathColorB = mod->getSavedValue<float>("hack_noclip_color_b", 0.0f);
    eng->rngLocked = mod->getSavedValue<bool>("hack_rng_lock", false);
    eng->rngSeedVal = mod->getSavedValue<int>("hack_rng_seed", 1);
    eng->protectedMode = mod->getSavedValue<bool>("hack_safe_mode", false);
    eng->audioPitchEnabled = mod->getSavedValue<bool>("hack_audio_pitch", true);
    eng->noMirrorEffect = mod->getSavedValue<bool>("hack_no_mirror", false);
    eng->layoutMode = mod->getSavedValue<bool>("hack_layout_mode", false);
    eng->noMirrorRecordingOnly = mod->getSavedValue<bool>("hack_no_mirror_rec_only", false);
    eng->fastPlayback = mod->getSavedValue<bool>("hack_fast_playback", false);
    eng->selectedAccuracyMode = sanitizeAccuracyMode(mod->getSavedValue<int>("eng_accuracy_mode", 0));
    eng->tickRate = mod->getSavedValue<float>("eng_tick_rate", 240.f);
    eng->gameSpeed = mod->getSavedValue<float>("eng_speed", 1.0f);
    eng->ttrMode = mod->getSavedValue<bool>("eng_ttr_mode", true);
    if (eng->selectedAccuracyMode == AccuracyMode::CBF && !AccuracyRuntime::isSyzziCBFAvailable()) {
        eng->selectedAccuracyMode = AccuracyMode::Vanilla;
    }
    AccuracyRuntime::applyRuntimeAccuracyMode(eng->selectedAccuracyMode);

    tempTickRate = (float)eng->tickRate;
    tempGameSpeed = (float)eng->gameSpeed;

    windowSize.x = sanitizeClamped(mod->getSavedValue<float>("window_size_w", 580.0f), 480.0f, 2000.0f, 580.0f);
    windowSize.y = sanitizeClamped(mod->getSavedValue<float>("window_size_h", 540.0f), 400.0f, 2000.0f, 540.0f);
    mainSubTab = std::clamp(mod->getSavedValue<int>("main_sub_tab", 0), 0, 2);

    loadRenderSettings();

    auto* csm = ClickSoundManager::get();
    csm->enabled = mod->getSavedValue<bool>("click_enabled", false);
    csm->activePackName = mod->getSavedValue<std::string>("click_pack", "");
    csm->p1Pack.hardVolume = static_cast<float>(mod->getSavedValue<double>("click_hard_vol", 1.0));
    csm->p1Pack.softVolume = static_cast<float>(mod->getSavedValue<double>("click_soft_vol", 0.5));
    csm->p1Pack.releaseVolume = static_cast<float>(mod->getSavedValue<double>("click_release_vol", 0.8));
    csm->softness = static_cast<float>(mod->getSavedValue<double>("click_softness", 0.5));
    csm->clickDelayMin = static_cast<float>(mod->getSavedValue<double>("click_delay_min", 0.0));
    csm->clickDelayMax = static_cast<float>(mod->getSavedValue<double>("click_delay_max", 0.0));
    csm->playDuringPlayback = mod->getSavedValue<bool>("click_play_during_playback", true);
    csm->separateP2Clicks = mod->getSavedValue<bool>("click_separate_p2", false);
    csm->activePackNameP2 = mod->getSavedValue<std::string>("click_pack_p2", "");
    csm->p2Pack.hardVolume = static_cast<float>(mod->getSavedValue<double>("click_hard_vol_p2", 1.0));
    csm->p2Pack.softVolume = static_cast<float>(mod->getSavedValue<double>("click_soft_vol_p2", 0.5));
    csm->p2Pack.releaseVolume = static_cast<float>(mod->getSavedValue<double>("click_release_vol_p2", 0.8));
    csm->backgroundNoiseEnabled = mod->getSavedValue<bool>("click_bg_noise", false);
    csm->backgroundNoiseVolume = static_cast<float>(mod->getSavedValue<double>("click_bg_noise_vol", 0.5));

    csm->scanClickPacks();
    csm->scanClickPacksP2();
    if (!csm->activePackName.empty())
        csm->loadClickPack(csm->activePackName, csm->p1Pack);
    if (csm->separateP2Clicks && !csm->activePackNameP2.empty())
        csm->loadClickPack(csm->activePackNameP2, csm->p2Pack, true);
}

void MenuInterface::drawMainWindow() {
    float t = anim.easeOutCubic(anim.openProgress);
    ImGuiIO& io = ImGui::GetIO();

    float winW = windowSize.x, winH = windowSize.y;
    bool animating = anim.opening || anim.closing || anim.openProgress < 1.0f;
    ImVec2 drawSize(winW, winH);

    if (!windowPosInitialized) {
        windowPos = ImVec2((io.DisplaySize.x - winW) / 2.0f, (io.DisplaySize.y - winH) / 2.0f);
        windowPosInitialized = true;
    }

    ImVec2 drawPos = windowPos;
    if (animating) {
        float invT = 1.0f - t;
        float slideOffset = 80.0f * invT;
        switch (anim.openDirection) {
            case ANIM_CENTER: {
                float scale = 0.8f + 0.2f * t;
                float scaledW = winW * scale;
                float scaledH = winH * scale;
                drawPos.x = windowPos.x + (winW - scaledW) / 2.0f;
                drawPos.y = windowPos.y + (winH - scaledH) / 2.0f;
                drawSize = ImVec2(scaledW, scaledH);
                ImGui::SetNextWindowSize(ImVec2(scaledW, scaledH));
                break;
            }
            case ANIM_FROM_LEFT:
                drawPos.x = windowPos.x - slideOffset;
                ImGui::SetNextWindowSize(ImVec2(winW, winH));
                break;
            case ANIM_FROM_RIGHT:
                drawPos.x = windowPos.x + slideOffset;
                ImGui::SetNextWindowSize(ImVec2(winW, winH));
                break;
            case ANIM_FROM_TOP:
                drawPos.y = windowPos.y - slideOffset;
                ImGui::SetNextWindowSize(ImVec2(winW, winH));
                break;
            case ANIM_FROM_BOTTOM:
                drawPos.y = windowPos.y + slideOffset;
                ImGui::SetNextWindowSize(ImVec2(winW, winH));
                break;
        }
        ImGui::SetNextWindowPos(drawPos);
    } else {
        ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 400.0f), ImVec2(2000.0f, 2000.0f));
    }

    {
        ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
        ImVec4 bg = theme.bgColor;
        bg.w = theme.bgOpacity;
        ImVec2 panelMax(drawPos.x + drawSize.x, drawPos.y + drawSize.y);
        bgDraw->AddRectFilled(drawPos, panelMax, toU32(bg), theme.cornerRadius);
        drawAmbientWaves(bgDraw, drawPos, panelMax);
        bgDraw->AddRect(drawPos, panelMax, theme.getAccentU32(0.25f), theme.cornerRadius, 0, 1.0f);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.cornerRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
    if (animating) flags |= ImGuiWindowFlags_NoResize;

    ImGui::Begin("##ToastyReplay", nullptr, flags);

    if (!animating) {
        ImVec2 curSize = ImGui::GetWindowSize();
        curSize.x = std::max(curSize.x, 480.0f);
        curSize.y = std::max(curSize.y, 400.0f);
        if (curSize.x != windowSize.x || curSize.y != windowSize.y) {
            windowSize = curSize;
        }
        drawSize = windowSize;

        ImVec2 cur = ImGui::GetWindowPos();
        float minVisible = 80.0f;
        cur.x = std::clamp(cur.x, minVisible - winW, io.DisplaySize.x - minVisible);
        cur.y = std::clamp(cur.y, 0.0f, io.DisplaySize.y - minVisible);
        if (cur.x != ImGui::GetWindowPos().x || cur.y != ImGui::GetWindowPos().y)
            ImGui::SetWindowPos(cur);
        windowPos = cur;
    }

    OnlineClient::get()->update(ImGui::GetIO().DeltaTime);

    drawTitleBar();
    drawTabBar();

    float statusBarH = 36.0f;
    float remainH = ImGui::GetContentRegionAvail().y - statusBarH;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##TabContent", ImVec2(0, remainH), false);
    ImGui::Indent(10.0f);
    drawTabContent();
    ImGui::Unindent(10.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    drawStatusBar();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void MenuInterface::drawInterface() {
    ReplayEngine* engine = ReplayEngine::get();
    if (engine) engine->processHotkeys();

    float dt = ImGui::GetIO().DeltaTime;
    anim.update(dt);

    if (anim.closing && anim.openProgress <= 0.0f) {
        shown = false;
        anim.closing = false;
        anim.openProgress = 0.0f;
        tabIndicatorX = -1.0f;
        saveSettings();
        previouslyShown = false;
        auto pl = PlayLayer::get();
        if (pl && !pl->m_isPaused)
            PlatformToolbox::hideCursor();
    }

    bool menuVisible = shown || anim.openProgress > 0.0f;
    bool inPlayback = ReplayEngine::get()->engineMode == MODE_EXECUTE && PlayLayer::get();
    bool shouldShowWatermark = menuVisible || inPlayback;
    if (shouldShowWatermark) {
        ImGuiIO& io = ImGui::GetIO();
        ImFont* wmFont = fontSmall ? fontSmall : ImGui::GetFont();
        const char* wmText = "ToastyReplay v" MOD_VERSION;
        ImVec2 textSz = wmFont->CalcTextSizeA(wmFont->FontSize, FLT_MAX, 0.f, wmText);
        ImVec2 wmPos(
            (io.DisplaySize.x - textSz.x) * 0.5f,
            io.DisplaySize.y - 25.0f
        );
        ImVec4 a = theme.getAccent();
        float wmAlpha = menuVisible
            ? 0.6f * anim.easeOutCubic(anim.openProgress)
            : 0.6f;
        ImGui::GetForegroundDrawList()->AddText(
            wmFont, wmFont->FontSize, wmPos,
            toU32(ImVec4(a.x, a.y, a.z, wmAlpha)), wmText
        );
    }

    if (!shown && anim.openProgress <= 0.0f) {
        tabIndicatorX = -1.0f;
        return;
    }

    if (shown && !previouslyShown && engine) {
        previouslyShown = true;
        replayRefreshQueued = true;
    }

    bool fullyOpen = shown && !anim.opening && !anim.closing && anim.openProgress >= 1.0f;
    if (fullyOpen && replayRefreshQueued) {
        refreshReplayListIfNeeded(false);
    }

    if (shown && !anim.closing)
        PlatformToolbox::showCursor();

    theme.applyToImGuiStyle();
    drawBackdrop();
    drawMainWindow();
}

void MenuInterface::initialize() {
    ImGuiIO& io = ImGui::GetIO();
    toasty::i18n::initialize();

    auto regularPath = Mod::get()->getResourcesDir() / "Inter-Regular.ttf";
    auto boldPath = Mod::get()->getResourcesDir() / "Inter-Bold.ttf";
    auto fallbackPath = Mod::get()->getResourcesDir() / "font.ttf";

    bool hasRegular = std::filesystem::exists(regularPath);
    bool hasBold = std::filesystem::exists(boldPath);
    bool hasFallback = std::filesystem::exists(fallbackPath);

    auto regularUtf8 = toasty::pathToUtf8(regularPath);
    auto boldUtf8 = toasty::pathToUtf8(boldPath);
    auto fallbackUtf8 = toasty::pathToUtf8(fallbackPath);

    const char* bodyFont = hasRegular ? regularUtf8.c_str() : (hasFallback ? fallbackUtf8.c_str() : nullptr);
    const char* headFont = hasBold ? boldUtf8.c_str() : bodyFont;
    ImFontGlyphRangesBuilder glyphBuilder;
    glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    ImVector<ImWchar> glyphRanges;
    glyphBuilder.BuildRanges(&glyphRanges);

    if (bodyFont) {
        fontBody = io.Fonts->AddFontFromFileTTF(bodyFont, 17.0f, nullptr, glyphRanges.Data);
        fontSmall = io.Fonts->AddFontFromFileTTF(bodyFont, 14.0f, nullptr, glyphRanges.Data);
    } else {
        fontBody = io.Fonts->AddFontDefault();
        fontSmall = io.Fonts->AddFontDefault();
    }

    if (headFont) {
        fontHeading = io.Fonts->AddFontFromFileTTF(headFont, 22.0f, nullptr, glyphRanges.Data);
        fontTitle = io.Fonts->AddFontFromFileTTF(headFont, 30.0f, nullptr, glyphRanges.Data);
    } else {
        fontHeading = io.Fonts->AddFontDefault();
        fontTitle = io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
    loadSettings();
    theme.applyToImGuiStyle();
    captureReplayDirectoryTimestamp();
    replayListDirty = true;
    replayRefreshQueued = true;
}

$on_mod(Loaded) {
    ImGuiCocos::get().setup([] {
        MenuInterface::get()->initialize();
    }).draw([] {
        MenuInterface::get()->drawInterface();
    });
}
