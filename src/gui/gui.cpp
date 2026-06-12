#include "gui/gui.hpp"
#include "gui/pride_mode.hpp"
#include "lang/localization.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "hacks/autoclicker.hpp"
#include "online/online_client.hpp"
#include "render/watermark_bridge.hpp"
#include "utils.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <array>
#include <charconv>
#include <chrono>
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
        default:
            return nullptr;
    }
}

static ImVec4 getAccuracyTagColor(AccuracyMode mode) {
    switch (mode) {
        case AccuracyMode::CBS:
            return ImVec4(1.0f, 0.22f, 0.22f, 1.0f);
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

static const char* getAutoclickerModeLabel(AutoclickerMode mode) {
    switch (mode) {
        case AutoclickerMode::Timed:
            return "Timed High-CPS";
        default:
            return "Legacy (TPS-bound)";
    }
}

static ImVec4 getTTRTagColor() {
    return ImVec4(0.30f, 0.70f, 1.0f, 1.0f);
}

static ImVec4 getPlatformerTagColor() {
    return ImVec4(0.28f, 0.95f, 0.58f, 1.0f);
}

static float sanitizeClamped(float value, float minValue, float maxValue, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, minValue, maxValue);
}

static int sanitizeRenderWatermarkFont(int value) {
    return std::clamp(
        value,
        static_cast<int>(RENDER_WATERMARK_FONT_NORMAL_PUSAB),
        static_cast<int>(RENDER_WATERMARK_FONT_GOLD_PUSAB)
    );
}

static int sanitizeRenderWatermarkCorner(int value) {
    return std::clamp(
        value,
        static_cast<int>(RENDER_WATERMARK_CORNER_TOP_LEFT),
        static_cast<int>(RENDER_WATERMARK_CORNER_BOTTOM_RIGHT)
    );
}

static const char* getRenderWatermarkFontLabel(int value) {
    switch (sanitizeRenderWatermarkFont(value)) {
        case RENDER_WATERMARK_FONT_GOLD_PUSAB:
            return "Gold Pusab";
        default:
            return "Normal Pusab";
    }
}

static const char* getRenderWatermarkCornerLabel(int value) {
    switch (sanitizeRenderWatermarkCorner(value)) {
        case RENDER_WATERMARK_CORNER_TOP_LEFT:
            return "Top Left";
        case RENDER_WATERMARK_CORNER_TOP_RIGHT:
            return "Top Right";
        case RENDER_WATERMARK_CORNER_BOTTOM_LEFT:
            return "Bottom Left";
        default:
            return "Bottom Right";
    }
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
    return std::string(toasty::lang::tr(key));
}

static std::string trFormat(std::string_view key, fmt::format_args args) {
    return toasty::lang::trf(key, args);
}

template <class... Args>
static std::string trFormat(std::string_view key, Args&&... args) {
    return toasty::lang::trf(key, std::forward<Args>(args)...);
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

    static std::filesystem::path findStoredReplayFile(std::string const& macroName, std::string_view requestedExtension) {
        auto replayDir = getReplayDirectoryPath();
        std::error_code ec;
        if (!std::filesystem::exists(replayDir, ec) || ec) {
            return {};
        }

        auto targetPath = replayDir / (macroName + std::string(requestedExtension));
        if (std::filesystem::is_regular_file(targetPath, ec) && !ec) {
            return targetPath;
        }

        std::string wantedExtension(requestedExtension);
        std::transform(wantedExtension.begin(), wantedExtension.end(), wantedExtension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        for (std::filesystem::directory_iterator it(replayDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }

            auto extension = toasty::pathToUtf8(it->path().extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (toasty::pathToUtf8(it->path().stem()) == macroName && extension == wantedExtension) {
                return it->path();
            }
        }

        return {};
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
        {"Autoclicker",   &keybinds.autoclicker},
        {"Disable Shaders", &keybinds.disableShaders},
        {"Click Sounds", &keybinds.clickSounds},
    };
    for (auto& e : entries) {
        if (e.ptr != target && *e.ptr == newKey) {
            return trFormat(
                "Already bound to {keybind}",
                fmt::arg("keybind", toasty::lang::tr(e.name))
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

static bool CollapsibleSectionHeader(const char* text, bool expanded, ThemeEngine& theme) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    std::string displayText = getLocalizedDisplayLabel(text);
    ImVec2 textSize = ImGui::CalcTextSize(displayText.c_str());
    float headerH = textSize.y + 11.0f;
    ImVec4 accent = theme.getAccent();

    ImGui::PushID(text);
    bool clicked = ImGui::InvisibleButton("##collapsibleSectionHeader", ImVec2(width, headerH));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    dl->AddText(pos, theme.getAccentU32(hovered ? 1.0f : 0.96f), displayText.c_str());
    float lineY = pos.y + textSize.y + 5.0f;
    float leftW = std::min(120.0f, width * 0.35f);
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x, lineY),
        ImVec2(pos.x + leftW, lineY + 2.0f),
        theme.getAccentU32(hovered ? 0.88f : 0.74f),
        theme.getAccentU32(0.28f),
        theme.getAccentU32(0.28f),
        theme.getAccentU32(hovered ? 0.88f : 0.74f)
    );
    dl->AddRectFilledMultiColor(
        ImVec2(pos.x + leftW, lineY),
        ImVec2(pos.x + width, lineY + 1.0f),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f + accent.x * 0.04f)),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f)),
        toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f + accent.z * 0.04f))
    );

    const char* symbol = expanded ? "-" : "+";
    ImVec2 symbolSize = ImGui::CalcTextSize(symbol);
    ImVec2 symbolCenter(pos.x + width - 10.0f, pos.y + textSize.y * 0.5f);
    dl->AddText(
        ImVec2(symbolCenter.x - symbolSize.x * 0.5f, symbolCenter.y - symbolSize.y * 0.5f),
        theme.getAccentU32(hovered ? 1.0f : 0.82f),
        symbol
    );

    return clicked;
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
        Widgets::KeybindButton("Keybind", keybind, theme, anim);
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
    if (panelW <= 0.0f || panelH <= 0.0f) return;

    dl->PushClipRect(panelMin, panelMax, true);

    ImVec4 accent = theme.getAccent();
    int ar = static_cast<int>(accent.x * 255);
    int ag = static_cast<int>(accent.y * 255);
    int ab = static_cast<int>(accent.z * 255);

    struct StarLayer {
        int count;
        float radius;
        float radiusJitter;
        float speedX, speedY;
        float baseAlpha;
        float twinkleAmp;
    };

    StarLayer const layers[] = {
        { 56, 0.55f, 0.22f,  4.0f, 2.0f, 0.16f, 0.06f },
        { 28, 1.00f, 0.32f,  9.0f, 4.5f, 0.30f, 0.11f },
        { 14, 1.60f, 0.40f, 16.0f, 8.0f, 0.52f, 0.18f },
    };

    auto hash01 = [](uint32_t state) -> float {
        state ^= state >> 16;
        state *= 0x7FEB352DU;
        state ^= state >> 15;
        state *= 0x846CA68BU;
        state ^= state >> 16;
        return static_cast<float>(state & 0x00FFFFFFU) / 16777215.0f;
    };

    int starIndex = 0;
    for (auto const& layer : layers) {
        for (int i = 0; i < layer.count; ++i) {
            uint32_t seed = static_cast<uint32_t>(starIndex) * 0x9E3779B1U + 0xA5A5A5A5U;
            float r0 = hash01(seed);
            float r1 = hash01(seed ^ 0x68E31DA4U);
            float r2 = hash01(seed ^ 0xB5297A4DU);
            float r3 = hash01(seed ^ 0x1B56C4E9U);
            float r4 = hash01(seed ^ 0xC2B2AE35U);

            float driftedX = r0 * panelW + ambientTime * layer.speedX;
            float driftedY = r1 * panelH + ambientTime * layer.speedY;

            float wrappedX = std::fmod(driftedX, panelW);
            if (wrappedX < 0.0f) wrappedX += panelW;
            float wrappedY = std::fmod(driftedY, panelH);
            if (wrappedY < 0.0f) wrappedY += panelH;

            float x = panelMin.x + wrappedX;
            float y = panelMin.y + wrappedY;

            float radius = layer.radius * (1.0f - layer.radiusJitter * 0.5f + r2 * layer.radiusJitter);

            float twinklePhase = r3 * 6.2831853f;
            float twinkleRate = 0.6f + r4 * 1.6f;
            float twinkle = std::sinf(ambientTime * twinkleRate + twinklePhase) * layer.twinkleAmp;
            float alpha = std::clamp(layer.baseAlpha + twinkle, 0.0f, 1.0f);
            int ai = static_cast<int>(alpha * 255.0f);
            if (ai > 0) {
                dl->AddCircleFilled(ImVec2(x, y), radius, IM_COL32(ar, ag, ab, ai), 12);
            }

            ++starIndex;
        }
    }

    struct ShootingStar {
        bool active = false;
        float spawnTime = 0.0f;
        float duration = 0.0f;
        ImVec2 origin{0.0f, 0.0f};
        ImVec2 endPoint{0.0f, 0.0f};
        float trailLength = 90.0f;
    };

    static ShootingStar streak;
    static float nextSpawnEarliest = 0.0f;

    constexpr float kAvgIntervalSec = 90.0f;
    constexpr float kPostCooldownSec = 5.0f;

    if (!streak.active && ambientTime >= nextSpawnEarliest && dt > 0.0f) {
        uint32_t spawnSeed = static_cast<uint32_t>(ambientTime * 1000.0f) ^ 0xDEADBEEFU;
        float chance = std::clamp(dt / kAvgIntervalSec, 0.0f, 1.0f);
        if (hash01(spawnSeed) < chance) {
            streak.active = true;
            streak.spawnTime = ambientTime;
            streak.duration = 0.85f + hash01(spawnSeed + 1U) * 0.55f;

            float startXRel = -0.05f + hash01(spawnSeed + 2U) * 0.45f;
            float startYRel = -0.10f + hash01(spawnSeed + 3U) * 0.10f;
            streak.origin = ImVec2(
                panelMin.x + panelW * startXRel,
                panelMin.y + panelH * startYRel
            );

            float endXRel = 0.75f + hash01(spawnSeed + 4U) * 0.40f;
            float endYRel = 0.65f + hash01(spawnSeed + 5U) * 0.45f;
            streak.endPoint = ImVec2(
                panelMin.x + panelW * endXRel,
                panelMin.y + panelH * endYRel
            );

            streak.trailLength = 70.0f + hash01(spawnSeed + 6U) * 50.0f;
        }
    }

    if (streak.active) {
        float elapsed = ambientTime - streak.spawnTime;
        float progress = elapsed / std::max(0.0001f, streak.duration);
        if (progress >= 1.0f) {
            streak.active = false;
            nextSpawnEarliest = ambientTime + kPostCooldownSec;
        } else {
            ImVec2 head{
                streak.origin.x + (streak.endPoint.x - streak.origin.x) * progress,
                streak.origin.y + (streak.endPoint.y - streak.origin.y) * progress
            };
            ImVec2 delta{
                streak.endPoint.x - streak.origin.x,
                streak.endPoint.y - streak.origin.y
            };
            float deltaLen = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            ImVec2 dir{0.0f, 0.0f};
            if (deltaLen > 0.0f) {
                dir.x = delta.x / deltaLen;
                dir.y = delta.y / deltaLen;
            }

            float life = 1.0f - std::abs(progress - 0.5f) * 2.0f;
            life = std::clamp(life * 1.55f, 0.0f, 1.0f);

            constexpr int tailSegs = 14;
            for (int i = 0; i < tailSegs; ++i) {
                float t0 = static_cast<float>(i) / tailSegs;
                float t1 = static_cast<float>(i + 1) / tailSegs;
                float fade = (1.0f - t0) * (1.0f - t0) * life;
                int segAlpha = static_cast<int>(fade * 255.0f);
                if (segAlpha <= 0) continue;
                ImVec2 p0{
                    head.x - dir.x * streak.trailLength * t0,
                    head.y - dir.y * streak.trailLength * t0
                };
                ImVec2 p1{
                    head.x - dir.x * streak.trailLength * t1,
                    head.y - dir.y * streak.trailLength * t1
                };
                float thickness = std::max(0.7f, 1.7f * (1.0f - t0 * 0.6f));
                dl->AddLine(p0, p1, IM_COL32(ar, ag, ab, segAlpha), thickness);
            }

            int headAlpha = static_cast<int>(std::clamp(life * 0.92f, 0.0f, 1.0f) * 255.0f);
            if (headAlpha > 0) {
                dl->AddCircleFilled(head, 1.9f, IM_COL32(ar, ag, ab, headAlpha), 14);
            }
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

    float logoOffset = 0.0f;
    if (logoTexture) {
        float logoSize = titleSize + 4.0f;
        ImTextureID texId = (ImTextureID)(uintptr_t)(logoTexture->getName());
        ImVec2 logoMin(pos.x, pos.y - 2.0f);
        ImVec2 logoMax(pos.x + logoSize, pos.y + logoSize - 2.0f);
        dl->AddImageRounded(texId, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), logoSize * 0.2f);
        logoOffset = logoSize + 8.0f;
    }

    const char* titleText = toasty::branding::kProductName;
    ImVec2 titleSz = titleF->CalcTextSizeA(titleSize, FLT_MAX, 0.f, titleText);
    dl->AddText(titleF, titleSize, ImVec2(pos.x + logoOffset, pos.y), toU32(ImVec4(0.98f, 0.99f, 1.0f, 0.99f)), titleText);

    std::string versionText = toasty::branding::versionText();
    ImVec2 versionSz = subtitleF->CalcTextSizeA(subtitleSize, FLT_MAX, 0.f, versionText.c_str());
    float versionY = pos.y + (titleSz.y - versionSz.y) * 0.58f;
    ImVec2 versionPos(pos.x + logoOffset + titleSz.x + 10.f, versionY);
    dl->AddText(subtitleF, subtitleSize, versionPos, toU32(ImVec4(accent.x, accent.y, accent.z, 0.92f)), versionText.c_str());

    const char* editionText = toasty::branding::kEditionLabel;
    ImVec2 editionPos(versionPos.x + versionSz.x + 8.f, versionY);
    dl->AddText(subtitleF, subtitleSize, editionPos, IM_COL32(255, 166, 42, 245), editionText);

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

    AccuracyMode statusAccuracyMode = engine->hasMacro() && engine->engineMode == MODE_CAPTURE
        ? engine->activeMacroAccuracyMode()
        : (engine->engineMode == MODE_EXECUTE ? engine->activeMacroAccuracyMode() : engine->selectedAccuracyMode);
    if (auto* statusAccuracyTag = getAccuracyTag(statusAccuracyMode)) {
        std::string accuracyText = trFormat(
            "{mode} ON",
            fmt::arg("mode", statusAccuracyTag)
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
        auto disableAction = [engine]() {
            engine->pendingPlaybackStart = false;
            if (engine->engineMode == MODE_CAPTURE) {
                engine->discardActiveMacro();
                engine->engineMode = MODE_DISABLED;
            } else if (engine->engineMode == MODE_EXECUTE) {
                engine->haltExecution();
            } else {
                engine->engineMode = MODE_DISABLED;
            }
            engine->clearStartPosWarning();
        };

        if (engine->engineMode == MODE_CAPTURE) {
            engine->runWithUnsavedMacroGuard(std::move(disableAction));
        } else {
            disableAction();
        }
    }
    ImGui::SameLine(0, 10);
    if (Widgets::PillButton("Record", engine->engineMode == MODE_CAPTURE, pillW, theme, anim)) {
        if (engine->engineMode != MODE_CAPTURE) {
            auto startCapture = [engine]() {
                bool previousTTRMode = engine->ttrMode;
                engine->ttrMode = true;
                if (PlayLayer::get()) {
                    if (!engine->beginCapture(PlayLayer::get()->m_level)) {
                        engine->ttrMode = previousTTRMode;
                    }
                } else {
                    engine->ttrMode = previousTTRMode;
                }
            };
            if (Autoclicker::get()->enabled && Autoclicker::get()->isTimedMode()) {
                startCapture();
            } else {
                engine->runWithUnsavedMacroGuard(std::move(startCapture));
            }
        }
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

    ImGui::Dummy(ImVec2(0, 8));

    if (engine->engineMode == MODE_CAPTURE && engine->hasMacro()) {
        size_t actionCount = 0;

        if (engine->ttrMode && engine->activeTTR)
            actionCount = engine->activeTTR->inputs.size();
        else if (engine->activeMacro)
            actionCount = engine->activeMacro->inputs.size();

        Widgets::StatusBadge("RECORDING", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::SameLine();
        const char* recordingFormatLabel = engine->ttrMode
            ? (engine->activeTTR && engine->activeTTR->loadedFromLegacyFormat() ? "TTR" : "TTR2")
            : "GDR";
        Widgets::StatusBadge(recordingFormatLabel,
            engine->ttrMode ? getTTRTagColor() : ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
        if (auto* accuracyTag = getAccuracyTag(engine->activeMacroAccuracyMode())) {
            ImGui::SameLine();
            Widgets::StatusBadge(accuracyTag, getAccuracyTagColor(engine->activeMacroAccuracyMode()));
        }
        if (engine->activeMacroPlatformerMode()) {
            ImGui::SameLine();
            Widgets::StatusBadge("PLAT", getPlatformerTagColor());
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
            if (engine->ttrMode && engine->activeTTR) {
                if (engine->activeTTR->name != macroNameBuffer) {
                    engine->activeTTR->name = macroNameBuffer;
                    engine->markActiveMacroDirty();
                }
            } else if (engine->activeMacro && engine->activeMacro->name != macroNameBuffer) {
                engine->activeMacro->name = macroNameBuffer;
                engine->markActiveMacroDirty();
            }
        }

        ImGui::Dummy(ImVec2(0, 4));
        float btnW = (ImGui::GetContentRegionAvail().x - 10) / 2.0f;

        if (Widgets::StyledButton("Save Macro", ImVec2(btnW, 30), theme, anim)) {
            if (engine->ttrMode && engine->activeTTR &&
                (!engine->activeTTR->inputs.empty() || !engine->activeTTR->persistenceAttempts.empty())) {
                if (engine->saveActiveMacro()) {
                    std::strncpy(macroNameBuffer, engine->activeTTR->name.c_str(), sizeof(macroNameBuffer) - 1);
                    macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
                    markReplayListDirty();
                    refreshReplayListIfNeeded(true);
                }
            } else if (!engine->ttrMode && engine->activeMacro && !engine->activeMacro->inputs.empty()) {
                if (engine->saveActiveMacro()) {
                    std::strncpy(macroNameBuffer, engine->activeMacro->name.c_str(), sizeof(macroNameBuffer) - 1);
                    macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
                    markReplayListDirty();
                    refreshReplayListIfNeeded(true);
                }
            }
        }
        ImGui::SameLine(0, 10);
        if (Widgets::StyledButton("Stop", ImVec2(btnW, 30), theme, anim)) {
            auto stopCapture = [engine]() {
                engine->discardActiveMacro();
                engine->engineMode = MODE_DISABLED;
            };
            engine->runWithUnsavedMacroGuard(std::move(stopCapture));
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
        const char* activeFormatLabel = engine->ttrMode
            ? (engine->activeTTR && engine->activeTTR->loadedFromLegacyFormat() ? "TTR" : "TTR2")
            : "GDR";
        Widgets::StatusBadge(activeFormatLabel,
            engine->ttrMode ? getTTRTagColor() : ImVec4(1.0f, 0.9f, 0.2f, 1.0f));
        {
            AccuracyMode accuracyMode = engine->activeMacroAccuracyMode();
            if (auto* accuracyTag = getAccuracyTag(accuracyMode)) {
                ImGui::SameLine();
                Widgets::StatusBadge(accuracyTag, getAccuracyTagColor(accuracyMode));
            }
        }
        if (engine->activeMacroPlatformerMode()) {
            ImGui::SameLine();
            Widgets::StatusBadge("PLAT", getPlatformerTagColor());
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

    Widgets::SectionHeader("Usable Replays", theme);

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
        imguiTextTr("No usable replays");
        ImGui::PopStyleColor();
    }

    auto queueReplayLoad = [this](std::string const& macroName, bool isTTR) {
        replayLoadPending = true;
        replayLoadPendingMacroName = macroName;
        replayLoadPendingIsTTR = isTTR;
        replayLoadReadyTime = ImGui::GetTime() + static_cast<double>(ImGui::GetIO().MouseDoubleClickTime) + 0.02;
    };

    auto macroListCopy = engine->storedMacros;
    for (const auto& macroName : macroListCopy) {
        bool isSelected = (engine->hasMacro() && engine->engineMode != MODE_CAPTURE && engine->getMacroName() == macroName);
        bool isIncompatible = engine->incompatibleMacros.count(macroName) > 0;
        bool isCBS = engine->cbsMacros.count(macroName) > 0;
        bool isPlatformer = engine->platformerMacros.count(macroName) > 0;
        bool isTTR = engine->ttrMacros.count(macroName) > 0;
        bool isTTR2 = engine->ttr2Macros.count(macroName) > 0;
        bool isLegacyTTR = isTTR && !isTTR2;
        bool isLegacyCBS = engine->legacyCbsMacros.count(macroName) > 0 && !isTTR2;
        ImGui::PushID(macroName.c_str());

        float xBtnW = 20.0f;
        float rowH = ImGui::GetTextLineHeight() + 8.0f;
        float fullRowW = ImGui::GetContentRegionAvail().x;

        std::string rowLabel = macroName;
        float accuracyLabelW = 0.0f;
        if (!isIncompatible && isCBS) {
            auto* tag = getAccuracyTag(AccuracyMode::CBS);
            accuracyLabelW = ImGui::CalcTextSize(tag).x + 16.0f;
        }
        auto incompatibleText = trString("Incompatible");
        auto platformerText = trString("PLAT");
        const char* ttrFormatTag = isTTR2 ? "TTR2" : "TTR";
        float ttrLabelW = (!isIncompatible && isTTR) ? ImGui::CalcTextSize(ttrFormatTag).x + 16.0f : 0.0f;
        float gdrLabelW = (!isIncompatible && !isTTR) ? ImGui::CalcTextSize("GDR").x + 16.0f : 0.0f;
        float platformerLabelW = (!isIncompatible && isPlatformer) ? ImGui::CalcTextSize(platformerText.c_str()).x + 16.0f : 0.0f;
        float incompatLabelW = isIncompatible ? ImGui::CalcTextSize(incompatibleText.c_str()).x + 16.0f : 0.0f;
        float maxNameW = std::max(40.0f, fullRowW - xBtnW - accuracyLabelW - ttrLabelW - gdrLabelW - platformerLabelW - incompatLabelW - listPadX * 2.0f - 24.0f);
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
            if (isLegacyCBS) {
                tooltip += trString("Legacy CBS macros are playback only. Re-record in TTR2 CBS mode for exact timing.");
            } else if (isCBS) {
                tooltip += trString("CBS macros do not work on other menus!");
            }
            if (isTTR) {
                if (!tooltip.empty()) tooltip += "\n";
                tooltip += isLegacyTTR
                    ? trString("TTR macros do not work on other menus!")
                    : trString("TTR2 macros do not work on other menus!");
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

        if (!isIncompatible && (rowDoubleClicked || rowRightClicked)) {
            replayLoadPending = false;
            replayActionMacroName = macroName;
            replayActionIsTTR = isTTR;
            replayActionIsLegacyCBS = isLegacyCBS;
            replayActionCanEdit = !isCBS;
            replayActionPopupRequested = true;
        } else if (!isIncompatible && rowActivated && engine->engineMode != MODE_CAPTURE) {
            queueReplayLoad(macroName, isTTR);
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
        if (!isIncompatible && isCBS) {
            AccuracyMode taggedMode = AccuracyMode::CBS;
            const char* tag = getAccuracyTag(taggedMode);
            ImVec2 tagSize = ImGui::CalcTextSize(tag);
            tagX -= tagSize.x + 8.0f;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX, itemMinY + (itemH - tagSize.y) * 0.5f),
                toU32(getAccuracyTagColor(taggedMode)), tag
            );
        }
        if (!isIncompatible && isPlatformer) {
            ImVec2 tagSize = ImGui::CalcTextSize(platformerText.c_str());
            tagX -= tagSize.x + 8.0f;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX, itemMinY + (itemH - tagSize.y) * 0.5f),
                toU32(getPlatformerTagColor()), platformerText.c_str()
            );
        }
        if (!isIncompatible && isTTR) {
            const char* tag = ttrFormatTag;
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
            std::filesystem::path targetPath = dir / (macroName + (isTTR ? (isTTR2 ? ".ttr2" : ".ttr") : ".gdr"));
            std::error_code deleteEc;
            if (std::filesystem::is_regular_file(targetPath, deleteEc) && !deleteEc) {
                deleted = std::filesystem::remove(targetPath, deleteEc);
            }
            if (!deleted) {
                for (auto& entry : std::filesystem::directory_iterator(dir)) {
                    auto ext = toasty::pathToUtf8(entry.path().extension());
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    if (entry.is_regular_file() &&
                        toasty::pathToUtf8(entry.path().stem()) == macroName &&
                        ((isTTR && (ext == ".ttr2" || ext == ".ttr")) || (!isTTR && ext == ".gdr"))) {
                        std::filesystem::remove(entry.path());
                        deleted = true;
                        break;
                    }
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

    if (replayLoadPending) {
        if (engine->engineMode == MODE_CAPTURE) {
            replayLoadPending = false;
        } else if (ImGui::GetTime() >= replayLoadReadyTime) {
            auto macroName = replayLoadPendingMacroName;
            bool isTTR = replayLoadPendingIsTTR;
            replayLoadPending = false;

            if (engine->incompatibleMacros.count(macroName) == 0) {
                auto loadMacro = [engine, macroName, isTTR]() {
                    engine->pendingPlaybackStart = false;
                    if (isTTR) {
                        if (TTRMacro* loaded = TTRMacro::loadFromDisk(macroName)) {
                            engine->discardActiveMacro();
                            engine->activeTTR = loaded;
                            engine->ttrMode = true;
                            engine->clearActiveMacroDirty();
                        }
                    } else {
                        if (MacroSequence* loaded = MacroSequence::loadFromDisk(macroName)) {
                            engine->discardActiveMacro();
                            engine->activeMacro = loaded;
                            engine->ttrMode = false;
                            engine->clearActiveMacroDirty();
                        }
                    }
                };
                engine->runWithUnsavedMacroGuard(std::move(loadMacro));
            }
        }
    }

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
            bool canEdit = replayActionCanEdit;
            if (!canEdit) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
                Widgets::StyledButton("Open Macro Editor##actionEdit", ImVec2(actionBtnW, 30.0f), theme, anim, 6.0f);
                ImGui::PopStyleVar();
                ImVec2 tipPos = ImGui::GetItemRectMin();
                tipPos.y += 34.0f;
                auto tipText = replayActionIsLegacyCBS
                    ? trString("Legacy CBS macros are playback only. Re-record in TTR2 CBS mode for exact timing.")
                    : trString("CBS/CBF macros cannot be edited");
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

        if (!replayActionIsTTR) {
            bool canStartConversion = !replayConvertRunning && !replayActionIsLegacyCBS;
            if (!canStartConversion) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
            }
            bool convertClicked = Widgets::StyledButton(
                replayConvertRunning ? "Converting...##actionConvertTTR" : "Convert to TTR2##actionConvertTTR",
                ImVec2(actionBtnW, 30.0f),
                theme,
                anim,
                6.0f
            );
            if (!canStartConversion) {
                ImGui::PopStyleVar();
            }
            if (replayActionIsLegacyCBS) {
                ImVec2 tipPos = ImGui::GetItemRectMin();
                tipPos.y += 34.0f;
                auto tipText = trString("Legacy CBS macros are playback only. Re-record in TTR2 CBS mode for exact timing.");
                ImGui::GetWindowDrawList()->AddText(tipPos, IM_COL32(255, 180, 80, 200), tipText.c_str());
            }
            if (convertClicked && canStartConversion) {
                auto macroName = replayActionMacroName;
                auto startConversion = [this, macroName]() {
                    auto sourcePath = findStoredReplayFile(macroName, ".gdr");
                    if (sourcePath.empty()) {
                        replayConvertRunning = false;
                        replayConvertStatusOk = false;
                        replayConvertStatus = "Replay file was not found.";
                        replayConvertWarnings.clear();
                        replayConvertShowStandaloneStatus = true;
                        replayConvertMarkSourceOnComplete = false;
                        replayConvertSelectOutputOnComplete = false;
                        replayConvertSourceKeyOnComplete.clear();
                        return;
                    }

                    std::string author = GJAccountManager::get() ? GJAccountManager::get()->m_username : "";
                    auto outputDirectory = getReplayDirectoryPath();
                    replayConvertRunning = true;
                    replayConvertStatusOk = false;
                    replayConvertStatus = trString("Converting...");
                    replayConvertWarnings.clear();
                    replayConvertShowStandaloneStatus = true;
                    replayConvertMarkSourceOnComplete = false;
                    replayConvertSelectOutputOnComplete = true;
                    replayConvertSourceKeyOnComplete.clear();
                    replayConvertFuture = std::async(std::launch::async, [sourcePath, author, outputDirectory]() {
                        return toasty::conversion::convertNativeGDRToTTRDuplicate(sourcePath, author, outputDirectory);
                    });
                };
                ImGui::CloseCurrentPopup();
                engine->runWithUnsavedMacroGuard(std::move(startConversion));
            }

            ImGui::Dummy(ImVec2(0, 6));
        }

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

    if (replayConvertRunning && replayConvertFuture.valid() &&
        replayConvertFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = replayConvertFuture.get();
        replayConvertRunning = false;
        replayConvertStatusOk = result.ok;
        replayConvertStatus = result.message;
        replayConvertWarnings = result.warnings;
        if (result.ok) {
            if (replayConvertMarkSourceOnComplete && !replayConvertSourceKeyOnComplete.empty()) {
                engine->convertedForeignReplaySources.insert(replayConvertSourceKeyOnComplete);
            }
            if (!result.outputName.empty() && result.detectedFormat != toasty::conversion::ReplayFormat::Unknown) {
                engine->convertedMacroSources[result.outputName] = toasty::conversion::formatDisplayName(result.detectedFormat);
            }
            markReplayListDirty();
            refreshReplayListIfNeeded(true);
            if (replayConvertSelectOutputOnComplete && !result.outputName.empty()) {
                if (TTRMacro* loaded = TTRMacro::loadFromDisk(result.outputName)) {
                    engine->pendingPlaybackStart = false;
                    engine->discardActiveMacro();
                    engine->activeTTR = loaded;
                    engine->ttrMode = true;
                    engine->clearActiveMacroDirty();
                } else {
                    replayConvertStatusOk = false;
                    replayConvertStatus += "\nConverted, but failed to load the new TTR2.";
                }
            }
        }
        replayConvertMarkSourceOnComplete = false;
        replayConvertSelectOutputOnComplete = false;
        replayConvertSourceKeyOnComplete.clear();
    }

    if (replayConvertShowStandaloneStatus && !replayConvertStatus.empty()) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
        ImGui::PushStyleColor(ImGuiCol_Text, replayConvertStatusOk ? ImVec4(0.3f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.7f, 0.25f, 1.0f));
        ImGui::TextWrapped("%s", replayConvertStatus.c_str());
        ImGui::PopStyleColor();
        for (auto const& warning : replayConvertWarnings) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", warning.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Dummy(ImVec2(0, 6));
    if (Widgets::CollapsibleSectionHeader("Conversions", replayConversionsExpanded, theme)) {
        replayConversionsExpanded = !replayConversionsExpanded;
    }

    if (replayConversionsExpanded) {
    ImGui::Dummy(ImVec2(0, 2));
    Widgets::SectionHeader("Needs Conversion", theme);

    float convertListH = std::max(60.0f, std::min(150.0f, (float)engine->foreignReplays.size() * 28.0f + listPadY * 2.0f));
    ImVec2 convertListPos = ImGui::GetCursorScreenPos();
    float convertListW = ImGui::GetContentRegionAvail().x;
    drawSolidRect(ImGui::GetWindowDrawList(), convertListPos, ImVec2(convertListPos.x + convertListW, convertListPos.y + convertListH), theme.cornerRadius, theme, 0.42f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##ForeignMacroList", ImVec2(-1, convertListH), false);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
    ImGui::Dummy(ImVec2(0, listPadY));

    if (engine->foreignReplays.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
        imguiTextTr("No old macros found");
        ImGui::PopStyleColor();
    }

    for (auto const& entry : engine->foreignReplays) {
        auto pathKey = toasty::conversion::normalizedPathKey(entry.path);
        bool selected = replayConvertSelectedPath == pathKey;
        bool converted = entry.converted || engine->convertedForeignReplaySources.count(pathKey) > 0;
        bool canConvert = entry.supported && !replayConvertRunning;
        std::string statusText = converted ? "Converted" : (entry.supported ? "Convert" : (entry.recognized ? "Unsupported" : "Error"));
        ImVec4 statusColor = converted
            ? ImVec4(0.3f, 1.0f, 0.45f, 1.0f)
            : (entry.supported ? theme.getAccent() : ImVec4(1.0f, 0.35f, 0.28f, 1.0f));

        ImGui::PushID(pathKey.c_str());
        float rowH = ImGui::GetTextLineHeight() + 8.0f;
        float fullRowW = ImGui::GetContentRegionAvail().x;
        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        bool rowActivated = ImGui::Selectable("##foreignrow", selected, ImGuiSelectableFlags_AllowOverlap, ImVec2(fullRowW, rowH));
        if (ImGui::IsItemHovered() && !entry.detail.empty()) {
            ImGui::SetTooltip("%s", entry.detail.c_str());
        }
        if (rowActivated) {
            replayConvertSelectedPath = pathKey;
            std::strncpy(replayConvertNameBuffer, entry.stem.c_str(), sizeof(replayConvertNameBuffer) - 1);
            replayConvertNameBuffer[sizeof(replayConvertNameBuffer) - 1] = '\0';
            replayConvertTargetTTR = true;
            replayConvertStatus.clear();
            replayConvertWarnings.clear();
            replayConvertStatusOk = false;
            replayConvertShowStandaloneStatus = false;
        }

        std::string nameLabel = entry.filename.empty() ? entry.stem : entry.filename;
        std::string formatLabel = toasty::conversion::formatDisplayName(entry.format);
        float statusW = ImGui::CalcTextSize(statusText.c_str()).x + 10.0f;
        float formatW = ImGui::CalcTextSize(formatLabel.c_str()).x + 12.0f;
        float maxNameW = std::max(40.0f, fullRowW - statusW - formatW - listPadX * 2.0f - 22.0f);
        if (ImGui::CalcTextSize(nameLabel.c_str()).x > maxNameW) {
            while (!nameLabel.empty() && ImGui::CalcTextSize((nameLabel + "...").c_str()).x > maxNameW) {
                nameLabel.pop_back();
            }
            nameLabel += "...";
        }

        float itemMinY = rowStart.y;
        float itemH = rowH;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowStart.x + listPadX, itemMinY + (itemH - ImGui::GetTextLineHeight()) * 0.5f),
            entry.supported ? theme.getTextU32() : toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.55f)),
            nameLabel.c_str()
        );

        float tagX = rowStart.x + fullRowW - listPadX;
        ImVec2 statusSize = ImGui::CalcTextSize(statusText.c_str());
        tagX -= statusSize.x;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(tagX, itemMinY + (itemH - statusSize.y) * 0.5f),
            toU32(statusColor),
            statusText.c_str()
        );
        ImVec2 formatSize = ImGui::CalcTextSize(formatLabel.c_str());
        tagX -= formatSize.x + 12.0f;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(tagX, itemMinY + (itemH - formatSize.y) * 0.5f),
            toU32(ImVec4(1.0f, 0.85f, 0.35f, entry.recognized ? 1.0f : 0.55f)),
            formatLabel.c_str()
        );

        if (rowActivated && canConvert && ImGui::IsMouseDoubleClicked(0)) {
            replayConvertStatus.clear();
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    auto selectedForeign = std::find_if(engine->foreignReplays.begin(), engine->foreignReplays.end(), [&](auto const& entry) {
        return toasty::conversion::normalizedPathKey(entry.path) == replayConvertSelectedPath;
    });

    if (selectedForeign != engine->foreignReplays.end()) {
        bool converted = selectedForeign->converted || engine->convertedForeignReplaySources.count(replayConvertSelectedPath) > 0;
        ImGui::Dummy(ImVec2(0, 6));
        ImVec2 panelPos = ImGui::GetCursorScreenPos();
        float panelW = ImGui::GetContentRegionAvail().x;
        float panelH = selectedForeign->supported ? 134.0f : 82.0f;
        drawSolidRect(ImGui::GetWindowDrawList(), panelPos, ImVec2(panelPos.x + panelW, panelPos.y + panelH), theme.cornerRadius, theme, 0.38f);
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
        ImGui::TextColored(theme.getAccent(), "%s", selectedForeign->filename.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
        auto detectedText = fmt::format("{}{}", toasty::conversion::formatDisplayName(selectedForeign->format), converted ? " | Converted" : "");
        ImGui::TextUnformatted(detectedText.c_str());

        if (!selectedForeign->detail.empty()) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.35f, 1.0f));
            ImGui::TextWrapped("%s", selectedForeign->detail.c_str());
            ImGui::PopStyleColor();
        }

        if (selectedForeign->supported) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
            float targetW = (ImGui::GetContentRegionAvail().x - listPadX - 10.0f) / 2.0f;
            if (Widgets::PillButton("TTR2##convertTarget", replayConvertTargetTTR, targetW, theme, anim)) {
                replayConvertTargetTTR = true;
            }
            ImGui::SameLine(0, 10);
            if (Widgets::PillButton("GDR##convertTarget", !replayConvertTargetTTR, targetW, theme, anim)) {
                replayConvertTargetTTR = false;
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - listPadX);
            ImGui::InputText("##convertOutputName", replayConvertNameBuffer, sizeof(replayConvertNameBuffer));

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
            bool canStartConversion = !replayConvertRunning;
            if (!canStartConversion) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
            }
            bool convertClicked = Widgets::StyledButton(replayConvertRunning ? "Converting..." : "Convert", ImVec2(ImGui::GetContentRegionAvail().x - listPadX, 28.0f), theme, anim, 6.0f);
            if (!canStartConversion) {
                ImGui::PopStyleVar();
            }
            if (convertClicked && canStartConversion) {
                auto sourcePath = selectedForeign->path;
                auto target = replayConvertTargetTTR ? toasty::conversion::ConversionTarget::TTR : toasty::conversion::ConversionTarget::GDR;
                std::string requestedName = replayConvertNameBuffer;
                std::string author = GJAccountManager::get() ? GJAccountManager::get()->m_username : "";
                auto outputDirectory = getReplayDirectoryPath();
                replayConvertRunning = true;
                replayConvertStatusOk = false;
                replayConvertStatus = trString("Converting...");
                replayConvertWarnings.clear();
                replayConvertShowStandaloneStatus = false;
                replayConvertMarkSourceOnComplete = true;
                replayConvertSelectOutputOnComplete = false;
                replayConvertSourceKeyOnComplete = replayConvertSelectedPath;
                replayConvertFuture = std::async(std::launch::async, [sourcePath, target, requestedName, author, outputDirectory]() {
                    return toasty::conversion::convertReplay(sourcePath, target, requestedName, author, outputDirectory);
                });
            }
        }

        if (!replayConvertShowStandaloneStatus && !replayConvertStatus.empty()) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
            ImGui::PushStyleColor(ImGuiCol_Text, replayConvertStatusOk ? ImVec4(0.3f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.7f, 0.25f, 1.0f));
            ImGui::TextWrapped("%s", replayConvertStatus.c_str());
            ImGui::PopStyleColor();
        }
        if (!replayConvertShowStandaloneStatus) {
            for (auto const& warning : replayConvertWarnings) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
                ImGui::TextWrapped("%s", warning.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::Dummy(ImVec2(0, 4));
    }

    static std::string convertibleFormatsText = [] {
        std::string text;
        for (auto format : toasty::conversion::supportedFormats()) {
            if (!text.empty()) {
                text += ", ";
            }
            text += toasty::conversion::formatDisplayName(format);
        }
        return text;
    }();

    ImGui::Dummy(ImVec2(0, 6));
    Widgets::SectionHeader("Convertible Formats", theme);
    ImVec2 formatsPos = ImGui::GetCursorScreenPos();
    float formatsW = ImGui::GetContentRegionAvail().x;
    float formatsWrapW = std::max(40.0f, formatsW - listPadX * 2.0f);
    float formatsTextH = ImGui::CalcTextSize(convertibleFormatsText.c_str(), nullptr, false, formatsWrapW).y;
    float formatsH = std::max(54.0f, formatsTextH + listPadY * 2.0f + 4.0f);
    float formatsCursorY = ImGui::GetCursorPosY();
    drawSolidRect(ImGui::GetWindowDrawList(), formatsPos, ImVec2(formatsPos.x + formatsW, formatsPos.y + formatsH), theme.cornerRadius, theme, 0.34f);
    ImGui::SetCursorPosY(formatsCursorY + listPadY);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + listPadX);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + formatsWrapW);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.72f));
    ImGui::TextUnformatted(convertibleFormatsText.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPosY(formatsCursorY + formatsH);
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
            ImGui::TextUnformatted(engine->activeTTR && engine->activeTTR->loadedFromLegacyFormat() ? "(TTR)" : "(TTR2)");
            ImGui::PopStyleColor();
        }
        if (auto* accuracyTag = getAccuracyTag(loadedAccuracyMode)) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, getAccuracyTagColor(loadedAccuracyMode));
            ImGui::Text("(%s)", accuracyTag);
            ImGui::PopStyleColor();
        }
        if (engine->activeMacroPlatformerMode()) {
            auto platformerText = trString("PLAT");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, getPlatformerTagColor());
            ImGui::Text("(%s)", platformerText.c_str());
            ImGui::PopStyleColor();
        }
        auto loadedActions = trFormat("Actions: {count}", fmt::arg("count", actionCount));
        ImGui::TextUnformatted(loadedActions.c_str());
        if (engine->ttrMode && engine->activeTTR) {
            auto anchorsText = trFormat("Anchors: {count}", fmt::arg("count", engine->activeTTR->anchors.size()));
            ImGui::TextUnformatted(anchorsText.c_str());
        }


    }

    // Keybinds for replay control
    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Keybinds", theme);
    Widgets::KeybindButton("Toggle Playback", &keybinds.replayToggle, theme, anim);
    ImGui::Dummy(ImVec2(0, 4));
    Widgets::KeybindButton("Menu Toggle", &keybinds.menu, theme, anim);
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
            TrajectoryPredictionService::get().markDirty();
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
    if (Widgets::StyledButton("Apply###spd", ImVec2(78, 28), theme, anim)) {
        engine->gameSpeed = tempGameSpeed;
        TrajectoryPredictionService::get().markDirty();
    }

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Features", theme);

    bool frameAdvanceBefore = engine->tickStepping;
    if (Widgets::ModuleCardBegin("Frame Advance", "Pause and step frame-by-frame",
        &engine->tickStepping, theme, anim, &keybinds.frameAdvance)) {
        Widgets::KeybindButton("Step Key", &keybinds.frameStep, theme, anim);
        Widgets::ModuleCardEnd();
    }
    if (engine->tickStepping != frameAdvanceBefore) {
        engine->setFrameStepEnabled(engine->tickStepping, PlayLayer::get());
    }

    if (Widgets::ModuleCardBegin("Speedhack Audio", "Apply speed changes to game audio",
        &engine->audioPitchEnabled, theme, anim, &keybinds.audioPitch)) {
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Layout Mode", "Remove all decorations",
        &engine->layoutMode, theme, anim, &keybinds.layoutMode)) {
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Disable Shaders", "Suppress level shader effects",
        &engine->disableShaders, theme, anim, &keybinds.disableShaders)) {
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
        Widgets::ToggleSwitch("Auto-Enable While Recording / Playing", &engine->autoSafeMode, theme, anim);
        ImGui::TextColored(ImVec4(theme.textSecondary),
            "Force Safe Mode on whenever a macro is recording or playing back, even if the toggle above is off.");
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Show Trajectory", "Display predicted player path",
        &engine->pathPreview, theme, anim, &keybinds.trajectory)) {
        auto setTrajectoryLength = [&](int length) {
            int sanitizedLength = ReplayEngine::sanitizeTrajectoryLength(length);
            if (engine->pathLength != sanitizedLength) {
                engine->pathLength = sanitizedLength;
                TrajectoryPredictionService::get().markDirty();
            }
        };

        setTrajectoryLength(engine->pathLength);

        int sliderLength = engine->pathLength;
        if (Widgets::StyledSliderInt(
            "Trajectory Length",
            &sliderLength,
            ReplayEngine::kTrajectoryLengthMin,
            ReplayEngine::kTrajectoryLengthSliderMax,
            theme
        )) {
            setTrajectoryLength(sliderLength);
        }

        ImGui::Dummy(ImVec2(0, 4));
        int exactLength = engine->pathLength;
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 25, 200));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);
        if (ImGui::InputInt("##trajectory_length_exact", &exactLength, 0, 0)) {
            setTrajectoryLength(exactLength);
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();

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
        float hitRate = engine->noclipAccuracyPercent();

        ImVec4 hitColor;
        if (hitRate >= 90.0f) hitColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        else if (hitRate >= 70.0f) hitColor = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
        else hitColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        imguiTextTr("Accuracy:");
        ImGui::SameLine();
        ImGui::TextColored(hitColor, "%.2f%%", hitRate);
        auto deathFrameText = trFormat(
            "Deaths: {deaths} | Unsafe: {unsafe} | Frames: {frames}",
            fmt::arg("deaths", engine->noclipDeathEvents),
            fmt::arg("unsafe", engine->noclipUnsafeFrames),
            fmt::arg("frames", engine->noclipTotalFrames)
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

static constexpr const char* kAudioCodecIds[]    = { "aac",  "libopus", "flac",  "libmp3lame", "pcm_s16le", "ac3",    nullptr };
static constexpr const char* kAudioCodecLabels[] = { "AAC", "Opus",    "FLAC",  "MP3",        "PCM",       "AC-3", "Custom..." };
static constexpr int kAudioCodecCustomIdx = 6;

static int audioCodecComboIndex(const char* codec) {
    if (!codec || codec[0] == '\0' || std::string(codec) == "aac") return 0;
    for (int i = 0; kAudioCodecIds[i]; ++i)
        if (std::string(codec) == kAudioCodecIds[i]) return i;
    return kAudioCodecCustomIdx;
}

// FLAC, Opus, and PCM are not supported in the mp4 container
static bool audioCodecNeedsMkv(const char* codec) {
    if (!codec || !codec[0]) return false;
    std::string c(codec);
    return c == "flac" || c == "libopus" || c == "pcm_s16le";
}

void MenuInterface::drawRenderPresetsSection() {
    bool isExp = ReplayEngine::get()->useNewRenderer;
    auto* mod = Mod::get();
    float inputW = ImGui::GetContentRegionAvail().x * 0.45f;

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Presets", theme);

    if (presetListDirty) {
        auto presetsDir = mod->getSaveDir() / "presets";
        presetNames = RenderPresetIO::listNames(presetsDir);
        if (presetSelectedIndex >= (int)presetNames.size())
            presetSelectedIndex = presetNames.empty() ? -1 : (int)presetNames.size() - 1;
        presetListDirty = false;
    }

    {
        bool hasSelection = presetSelectedIndex >= 0 && presetSelectedIndex < (int)presetNames.size();
        const char* previewName = hasSelection ? presetNames[presetSelectedIndex].c_str() : "(none)";
        float btnW = 56.0f;
        float sp = ImGui::GetStyle().ItemSpacing.x;

        imguiTextTr("Preset");
        ImGui::SameLine(inputW);
        float remaining = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(remaining - (btnW + sp) * 2);
        if (ImGui::BeginCombo("##presetCombo", previewName)) {
            for (int i = 0; i < (int)presetNames.size(); i++) {
                bool sel = (presetSelectedIndex == i);
                if (ImGui::Selectable(presetNames[i].c_str(), sel))
                    presetSelectedIndex = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection);
        if (Widgets::StyledButton("Load##preset", ImVec2(btnW, 0), theme, anim)) {
            auto presetsDir = mod->getSaveDir() / "presets";
            auto preset = RenderPresetIO::load(RenderPresetIO::pathForName(presetsDir, presetNames[presetSelectedIndex]));
            if (preset) {
                if (isExp) {
                    expConfig = preset->toRenderConfig();
                    expConfig.gpuEncoder = probeGpuEncoder(expConfig.codecFamily);
                    snprintf(expRenderFpsBuf, sizeof(expRenderFpsBuf), "%u", expConfig.fps);
                    {
                        auto rp = resolve(expConfig);
                        snprintf(expAdvCodecBuf,      sizeof(expAdvCodecBuf),      "%s", expConfig.codec.value_or(rp.codec).c_str());
                        snprintf(expAdvCrfBuf,        sizeof(expAdvCrfBuf),        "%d", expConfig.crf.value_or(rp.crf));
                        snprintf(expAdvExtraArgsBuf,  sizeof(expAdvExtraArgsBuf),  "%s", expConfig.extraArgs.value_or(rp.extraArgs).c_str());
                        snprintf(expAdvAudioCodecBuf, sizeof(expAdvAudioCodecBuf), "%s", expConfig.audioCodec.value_or(rp.audioCodec).c_str());
                    }
                    snprintf(expAdvMaxBitrateBuf,    sizeof(expAdvMaxBitrateBuf),  "%s", expConfig.maxBitrate.value_or("").c_str());
                    snprintf(expAdvExtBuf,           sizeof(expAdvExtBuf),         "%s", expConfig.ext.value_or(".mp4").c_str());
                    snprintf(expAdvVideoArgsBuf,     sizeof(expAdvVideoArgsBuf),   "%s", expConfig.videoArgs.value_or(kDefaultVideoArgs).c_str());
                    snprintf(expAdvAudioArgsBuf,     sizeof(expAdvAudioArgsBuf),   "%s", expConfig.audioArgs.value_or("").c_str());
                    snprintf(expAdvSecondsAfterBuf,  sizeof(expAdvSecondsAfterBuf), "%g", expConfig.secondsAfter);
                    saveRenderConfig(expConfig);
                } else {
                    applyRenderPreset(*preset);
                }
                presetError.clear();
            } else {
                presetError = "Failed to load preset.";
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!hasSelection);
        if (Widgets::StyledButton("Delete##preset", ImVec2(btnW, 0), theme, anim)) {
            auto presetsDir = mod->getSaveDir() / "presets";
            std::error_code ec;
            std::filesystem::remove(RenderPresetIO::pathForName(presetsDir, presetNames[presetSelectedIndex]), ec);
            presetSelectedIndex = -1;
            presetListDirty = true;
            presetError = ec ? "Failed to delete preset." : "";
        }
        ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0, 2));
        imguiTextTr("New Name");
        ImGui::SameLine(inputW);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - sp);
        ImGui::InputText("##presetNameInput", presetNameBuf, sizeof(presetNameBuf));
        ImGui::SameLine();
        ImGui::BeginDisabled(presetNameBuf[0] == '\0');
        if (Widgets::StyledButton("Save##preset", ImVec2(btnW, 0), theme, anim) && presetNameBuf[0] != '\0') {
            auto presetsDir = mod->getSaveDir() / "presets";
            RenderPreset preset;
            if (isExp) {
                preset = RenderPreset::fromRenderConfig(expConfig, presetNameBuf);
            } else {
                preset = captureRenderPreset();
                preset.name = presetNameBuf;
            }
            if (RenderPresetIO::save(RenderPresetIO::pathForName(presetsDir, preset.name), preset)) {
                presetNameBuf[0] = '\0';
                presetListDirty = true;
                presetError.clear();
            } else {
                presetError = "Failed to save preset.";
            }
        }
        ImGui::EndDisabled();

        if (!presetError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            ImGui::TextUnformatted(presetError.c_str());
            ImGui::PopStyleColor();
        }
    }
}

void MenuInterface::drawRenderAudioSection() {
    bool isExp = ReplayEngine::get()->useNewRenderer;
    auto* mod = Mod::get();

    bool&  includeAudio  = isExp ? expConfig.includeAudio  : renderIncludeAudio;
    bool&  includeClicks = isExp ? expConfig.includeClicks : renderIncludeClicks;
    float& musicVol      = isExp ? expConfig.musicVol      : renderMusicVol;
    float& sfxVol        = isExp ? expConfig.sfxVol        : renderSfxVol;

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Audio", theme);

    if (Widgets::ToggleSwitch("Include Audio", &includeAudio, theme, anim)) {
        if (isExp) saveRenderConfig(expConfig);
        else mod->setSavedValue("render_include_audio", includeAudio);
    }
    if (Widgets::ToggleSwitch("Include Click Sounds", &includeClicks, theme, anim)) {
        if (isExp) saveRenderConfig(expConfig);
        else mod->setSavedValue("render_include_clicks", includeClicks);
    }
    if (includeClicks) {
        auto* csm = ClickSoundManager::get();
        if (csm->p1Pack.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.3f, 0.8f));
            imguiTextWrappedTr("Configure a click pack in the Clicks tab first.");
            ImGui::PopStyleColor();
        }
    }
    if (Widgets::StyledSliderFloat("Music Volume", &musicVol, 0.f, 1.f, theme)) {
        if (isExp) saveRenderConfig(expConfig);
        else mod->setSavedValue("render_music_volume", (double)musicVol);
    }
    if (Widgets::StyledSliderFloat("Click Volume", &sfxVol, 0.f, 1.f, theme)) {
        if (isExp) saveRenderConfig(expConfig);
        else mod->setSavedValue("render_sfx_volume", (double)sfxVol);
    }
}

void MenuInterface::drawRenderDisplaySection() {
    bool isExp = ReplayEngine::get()->useNewRenderer;
    auto* mod = Mod::get();

    bool& hideEndscreen    = isExp ? expConfig.hideEndscreen    : renderHideEndscreen;
    bool& hideLevelComplete = isExp ? expConfig.hideLevelComplete : renderHideLevelComplete;

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Display", theme);

    if (Widgets::ToggleSwitch("Hide End Screen", &hideEndscreen, theme, anim)) {
        if (isExp) saveRenderConfig(expConfig);
        else mod->setSavedValue("render_hide_endscreen", hideEndscreen);
    }
    if (Widgets::ToggleSwitch("Hide Level Complete", &hideLevelComplete, theme, anim)) {
        if (isExp) saveRenderConfig(expConfig);
        else mod->setSavedValue("render_hide_levelcomplete", hideLevelComplete);
    }
}

void MenuInterface::drawRenderTab() {
    if (ReplayEngine::get()->useNewRenderer) {
        drawExpRenderTab();
    }

    if (showMkvWarning) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420, 0));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(theme.getAccent().x, theme.getAccent().y, theme.getAccent().z, 0.5f));

        ImGui::Begin("##mkvWarning", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
        ImGui::TextUnformatted("Warning: mp4 doesn't support this audio codec");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextWrapped("FLAC, Opus, and PCM audio are not supported in the mp4 container. Change the extension to .mkv?");
        ImGui::Dummy(ImVec2(0, 12));

        float btnW = (ImGui::GetContentRegionAvail().x - 12) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.20f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.65f, 0.25f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.45f, 0.16f, 1.0f));
        if (ImGui::Button("Change to .mkv", ImVec2(btnW, 34))) {
            showMkvWarning = false;
            if (mkvWarningIsExp) {
                snprintf(expAdvExtBuf, sizeof(expAdvExtBuf), "%s", ".mkv");
                expConfig.ext = ".mkv";
                saveRenderConfig(expConfig);
            } else {
                snprintf(renderExtBuf, sizeof(renderExtBuf), "%s", ".mkv");
                Mod::get()->setSavedValue("render_file_extension", std::string(renderExtBuf));
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 12);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.15f, 0.15f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.20f, 0.20f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.10f, 0.10f, 1.0f));
        if (ImGui::Button("Keep .mp4", ImVec2(btnW, 34))) {
            showMkvWarning = false;
            const char* revert = (mkvWarningPrevCodec[0] && !audioCodecNeedsMkv(mkvWarningPrevCodec))
                ? mkvWarningPrevCodec : "aac";
            if (mkvWarningIsExp) {
                snprintf(expAdvAudioCodecBuf, sizeof(expAdvAudioCodecBuf), "%s", revert);
                expConfig.audioCodec = std::optional<std::string>(revert);
                saveRenderConfig(expConfig);
            } else {
                snprintf(renderAudioCodecBuf, sizeof(renderAudioCodecBuf), "%s", revert);
                Mod::get()->setSavedValue("render_audio_codec", std::string(renderAudioCodecBuf));
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    if (ReplayEngine::get()->useNewRenderer) return;

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

    drawRenderPresetsSection();

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
            snprintf(backupAudioArgsBuf,    sizeof(backupAudioArgsBuf),    "%s", renderAudioArgsBuf);
            snprintf(backupAudioCodecBuf,   sizeof(backupAudioCodecBuf),   "%s", renderAudioCodecBuf);
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
            snprintf(renderAudioArgsBuf,    sizeof(renderAudioArgsBuf),    "%s", backupAudioArgsBuf);
            snprintf(renderAudioCodecBuf,   sizeof(renderAudioCodecBuf),   "%s", backupAudioCodecBuf);
            snprintf(renderSecondsAfterBuf, sizeof(renderSecondsAfterBuf), "%s", backupSecondsAfterBuf);

            mod->setSavedValue("render_codec", std::string(backupCodecBuf));
            mod->setSavedValue("render_bitrate", std::string(backupBitrateBuf));
            mod->setSavedValue("render_file_extension", std::string(backupExtBuf));
            mod->setSavedValue("render_args", std::string(backupArgsBuf));
            mod->setSavedValue("render_video_args", std::string(backupVideoArgsBuf));
            mod->setSavedValue("render_audio_args", std::string(backupAudioArgsBuf));
            mod->setSavedValue("render_audio_codec", std::string(backupAudioCodecBuf));
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
            snprintf(backupAudioArgsBuf,    sizeof(backupAudioArgsBuf),    "%s", renderAudioArgsBuf);
            snprintf(backupAudioCodecBuf,   sizeof(backupAudioCodecBuf),   "%s", renderAudioCodecBuf);
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

    drawRenderAudioSection();
    drawRenderDisplaySection();

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

    {
        int acodecIdx = audioCodecComboIndex(renderAudioCodecBuf);
        imguiTextTr("Audio Codec");
        ImGui::SameLine(inputW);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##renderACodecCombo", kAudioCodecLabels[acodecIdx])) {
            for (int i = 0; i <= kAudioCodecCustomIdx; ++i) {
                bool sel = (acodecIdx == i);
                if (ImGui::Selectable(kAudioCodecLabels[i], sel)) {
                    guardAdvancedEdit();
                    if (advancedWarningAccepted) {
                        char prevCodec[64];
                        snprintf(prevCodec, sizeof(prevCodec), "%s", renderAudioCodecBuf);
                        if (i < kAudioCodecCustomIdx)
                            snprintf(renderAudioCodecBuf, sizeof(renderAudioCodecBuf), "%s", kAudioCodecIds[i]);
                        else
                            renderAudioCodecBuf[0] = '\0';
                        mod->setSavedValue("render_audio_codec", std::string(renderAudioCodecBuf));
                        if (audioCodecNeedsMkv(renderAudioCodecBuf) && std::string(renderExtBuf) == ".mp4") {
                            showMkvWarning = true;
                            mkvWarningIsExp = false;
                            snprintf(mkvWarningPrevCodec, sizeof(mkvWarningPrevCodec), "%s", prevCodec);
                        }
                    }
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (acodecIdx == kAudioCodecCustomIdx) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##renderACodecCustom", renderAudioCodecBuf, sizeof(renderAudioCodecBuf))) {
                guardAdvancedEdit();
                if (advancedWarningAccepted)
                    mod->setSavedValue("render_audio_codec", std::string(renderAudioCodecBuf));
            }
        }
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
    if (Widgets::ModuleCard("Autoclicker", "Auto-click at configurable intervals", &ac->enabled, theme, anim, &keybinds.autoclicker))
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

    ImGui::TextUnformatted(trString("Mode").c_str());
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##autoclick_mode", trString(getAutoclickerModeLabel(ac->mode)).c_str())) {
        for (AutoclickerMode mode : { AutoclickerMode::Legacy, AutoclickerMode::Timed }) {
            bool selected = ac->mode == mode;
            auto label = trString(getAutoclickerModeLabel(mode));
            if (ImGui::Selectable(label.c_str(), selected)) {
                ac->mode = mode;
                ac->reset();
                mod->setSavedValue("ac_mode", static_cast<int>(ac->mode));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    float cps = 0.0f;
    if (ac->isTimedMode()) {
        if (Widgets::StyledSliderFloat("Target CPS", &ac->targetCps, 1.0f, 20000.0f, theme, true)) {
            ac->targetCps = std::clamp(ac->targetCps, 1.0f, 20000.0f);
            ac->reset();
            mod->setSavedValue("ac_target_cps", static_cast<double>(ac->targetCps));
        }
        if (Widgets::StyledSliderFloat("Hold Ratio", &ac->holdRatio, 0.05f, 0.95f, theme, true)) {
            ac->holdRatio = std::clamp(ac->holdRatio, 0.05f, 0.95f);
            ac->reset();
            mod->setSavedValue("ac_hold_ratio", static_cast<double>(ac->holdRatio));
        }
        cps = ac->timedClicksPerSecond();
    } else {
        if (Widgets::StyledSliderInt("Hold Ticks", &ac->holdTicks, 1, 120, theme)) {
            ac->holdTicks = std::clamp(ac->holdTicks, 1, 120);
            ac->reset();
            mod->setSavedValue("ac_hold_ticks", ac->holdTicks);
        }
        if (Widgets::StyledSliderInt("Release Ticks", &ac->releaseTicks, 1, 120, theme)) {
            ac->releaseTicks = std::clamp(ac->releaseTicks, 1, 120);
            ac->reset();
            mod->setSavedValue("ac_release_ticks", ac->releaseTicks);
        }
        cps = ac->legacyClicksPerSecond(eng->tickRate);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(theme.textSecondary));
    auto cpsText = ac->isTimedMode()
        ? trFormat(
            "~{cps} clicks/sec with fractional substeps",
            fmt::arg("cps", fmt::format("{:.0f}", cps))
        )
        : trFormat(
            "~{cps} clicks/sec at {tps} TPS",
            fmt::arg("cps", fmt::format("{:.1f}", cps)),
            fmt::arg("tps", fmt::format("{:.0f}", eng->tickRate))
        );
    ImGui::TextUnformatted(cpsText.c_str());
    if (ac->isTimedMode()) {
        imguiTextWrappedTr("Timed mode records fractional offsets without raising TPS.");
    }
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
        if (!csm->availablePacks.empty()) clickPackIndex = std::clamp(clickPackIndex, 0, (int)csm->availablePacks.size() - 1);
        else clickPackIndex = 0;
        if (!csm->availablePacksP2.empty()) clickPackIndexP2 = std::clamp(clickPackIndexP2, 0, (int)csm->availablePacksP2.size() - 1);
        else clickPackIndexP2 = 0;
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

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Keybind", theme);
    Widgets::KeybindButton("Toggle Click Sounds", &keybinds.clickSounds, theme, anim);
}

void MenuInterface::drawSettingsTab() {
    auto* eng = ReplayEngine::get();
    auto* mod = Mod::get();
    auto* csm = ClickSoundManager::get();

    Widgets::SectionHeader("Customization", theme);

    imguiTextTr("Language");
    ImGui::SetNextItemWidth(-1);
    {
        using toasty::lang::UiLanguage;
        static constexpr UiLanguage languages[] = {
            UiLanguage::Auto,
            UiLanguage::English,
            UiLanguage::Spanish,
            UiLanguage::French,
            UiLanguage::Vietnamese,
            UiLanguage::Chinese,
        };

        UiLanguage configuredLanguage = toasty::lang::getConfiguredLanguage();
        std::string preview = std::string(toasty::lang::getLanguageDisplayName(configuredLanguage));
        if (ImGui::BeginCombo("##uiLanguage", preview.c_str())) {
            for (auto language : languages) {
                std::string displayName = std::string(toasty::lang::getLanguageDisplayName(language));
                bool selected = configuredLanguage == language;
                if (ImGui::Selectable(displayName.c_str(), selected)) {
                    mod->setSettingValue<std::string>(
                        "ui_language",
                        std::string(toasty::lang::getLanguageSettingValue(language))
                    );
                    toasty::lang::refresh();
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    if (Widgets::ToggleSwitch("Pride Logo", &prideLogoEnabled, theme, anim)) {
        ensureLogoTexture();
        saveSettings();
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

    bool cbsEnabled = eng->selectedAccuracyMode == AccuracyMode::CBS;
    bool accuracyChanged = false;

    if (eng->engineMode != MODE_DISABLED) ImGui::BeginDisabled();
    if (Widgets::ToggleSwitch("CBS", &cbsEnabled, theme, anim)) {
        eng->selectedAccuracyMode = cbsEnabled ? AccuracyMode::CBS : AccuracyMode::Vanilla;
        accuracyChanged = true;
    }
    if (eng->engineMode != MODE_DISABLED) ImGui::EndDisabled();

    {
        ImGui::Dummy(ImVec2(0, 6));

        auto cbfLabel = trString("CBF (Buy Pro)");
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr float switchW = 44.0f;
        constexpr float switchH = 22.0f;
        constexpr float switchR = switchH * 0.5f;
        float labelW = ImGui::CalcTextSize(cbfLabel.c_str()).x;

        ImGui::InvisibleButton("##cbfProOnlyDisabled", ImVec2(switchW + 12.0f + labelW, switchH));
        if (ImGui::IsItemHovered()) {
            auto tooltip = trString("Buy Pro to use CBF.");
            ImGui::SetTooltip("%s", tooltip.c_str());
        }

        dl->AddRectFilled(
            pos,
            ImVec2(pos.x + switchW, pos.y + switchH),
            toU32(ImVec4(0.16f, 0.16f, 0.18f, 0.36f)),
            switchR
        );
        dl->AddRect(
            pos,
            ImVec2(pos.x + switchW, pos.y + switchH),
            toU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)),
            switchR,
            0,
            1.0f
        );
        dl->AddCircleFilled(
            ImVec2(pos.x + switchR, pos.y + switchR),
            switchR - 2.0f,
            toU32(ImVec4(0.43f, 0.43f, 0.47f, 0.74f))
        );
        dl->AddText(
            ImVec2(pos.x + switchW + 12.0f, pos.y + 2.0f),
            toU32(withAlpha(theme.textSecondary, 0.52f)),
            cbfLabel.c_str()
        );
    }

    if (eng->engineMode != MODE_DISABLED) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        imguiTextWrappedTr("Stop recording or playback before changing accuracy mode.");
        ImGui::PopStyleColor();
    }

    if (accuracyChanged) {
        ReplayEngine::applyRuntimeAccuracyMode(eng->selectedAccuracyMode);
        saveSettings();
    }

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Advanced", theme);

    Widgets::ModuleCard("FastPlayback", "Start playback without restarting the level", &eng->fastPlayback, theme, anim);
    if (Widgets::ModuleCard("Autosave", "Automatically save completed recordings", &eng->completionAutosave, theme, anim)) {
        if (eng->completionAutosave && eng->persistenceMode) {
            eng->setPersistenceMode(false);
        }
        mod->setSavedValue("eng_completion_autosave", eng->completionAutosave);
    }
    if (eng->engineMode != MODE_DISABLED) ImGui::BeginDisabled();
    if (Widgets::ModuleCard("Persistence Mode", "Keep failed attempts before the completed run", &eng->persistenceMode, theme, anim)) {
        eng->setPersistenceMode(eng->persistenceMode);
    }
    if (eng->engineMode != MODE_DISABLED) ImGui::EndDisabled();
    if (Widgets::ModuleCard(
        "Mute Left/Right Clicks",
        "Disable click sounds for platformer left and right inputs",
        &csm->muteLeftRightClicks,
        theme,
        anim
    )) {
        mod->setSavedValue("click_mute_left_right", csm->muteLeftRightClicks);
    }
    bool prevRenderWatermarkEnabled = renderWatermarkEnabled;
    if (Widgets::ModuleCardBegin("Render Watermark", "Add a watermark into rendered video only", &renderWatermarkEnabled, theme, anim)) {
        renderWatermarkFont = sanitizeRenderWatermarkFont(renderWatermarkFont);
        renderWatermarkCorner = sanitizeRenderWatermarkCorner(renderWatermarkCorner);
        renderWatermarkScale = sanitizeClamped(renderWatermarkScale, 0.25f, 3.0f, 1.0f);

        imguiTextTr("Watermark Font");
        ImGui::SetNextItemWidth(-1);
        auto fontPreview = trString(getRenderWatermarkFontLabel(renderWatermarkFont));
        if (ImGui::BeginCombo("##renderWatermarkFont", fontPreview.c_str())) {
            for (int fontOption = RENDER_WATERMARK_FONT_NORMAL_PUSAB; fontOption <= RENDER_WATERMARK_FONT_GOLD_PUSAB; ++fontOption) {
                bool selected = renderWatermarkFont == fontOption;
                auto label = trString(getRenderWatermarkFontLabel(fontOption));
                if (ImGui::Selectable(label.c_str(), selected)) {
                    renderWatermarkFont = fontOption;
                    mod->setSavedValue("render_watermark_font", renderWatermarkFont);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Dummy(ImVec2(0, 6));
        imguiTextTr("Watermark Position");
        ImGui::SetNextItemWidth(-1);
        auto positionPreview = trString(getRenderWatermarkCornerLabel(renderWatermarkCorner));
        if (ImGui::BeginCombo("##renderWatermarkCorner", positionPreview.c_str())) {
            for (int cornerOption = RENDER_WATERMARK_CORNER_TOP_LEFT; cornerOption <= RENDER_WATERMARK_CORNER_BOTTOM_RIGHT; ++cornerOption) {
                bool selected = renderWatermarkCorner == cornerOption;
                auto label = trString(getRenderWatermarkCornerLabel(cornerOption));
                if (ImGui::Selectable(label.c_str(), selected)) {
                    renderWatermarkCorner = cornerOption;
                    mod->setSavedValue("render_watermark_corner", renderWatermarkCorner);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Dummy(ImVec2(0, 6));
        if (Widgets::StyledSliderFloat("Watermark Size", &renderWatermarkScale, 0.25f, 3.0f, theme, true)) {
            renderWatermarkScale = sanitizeClamped(renderWatermarkScale, 0.25f, 3.0f, 1.0f);
            mod->setSavedValue("render_watermark_scale", renderWatermarkScale);
        }

        Widgets::ModuleCardEnd();
    }
    if (renderWatermarkEnabled != prevRenderWatermarkEnabled) {
        mod->setSavedValue("render_watermark_enabled", renderWatermarkEnabled);
    }

    ImGui::Dummy(ImVec2(0, 12));
    if (Widgets::StyledButton("Reset to Defaults", ImVec2(-1, 32), theme, anim)) {
        theme.resetDefaults();
        anim.animSpeed = 8.0f;
        anim.openDirection = ANIM_CENTER;
        eng->fastPlayback = false;
        eng->disableShaders = false;
        eng->persistenceMode = false;
        eng->selectedAccuracyMode = AccuracyMode::Vanilla;
        ReplayEngine::applyRuntimeAccuracyMode(eng->selectedAccuracyMode);
        csm->muteLeftRightClicks = false;
        ambientWavesEnabled = true;
        renderWatermarkEnabled = false;
        renderWatermarkFont = RENDER_WATERMARK_FONT_NORMAL_PUSAB;
        renderWatermarkCorner = RENDER_WATERMARK_CORNER_BOTTOM_RIGHT;
        renderWatermarkScale = 1.0f;
        saveSettings();
    }

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Experimental", theme);

    bool expRendererPrev = mod->getSavedValue<bool>("experimental_new_renderer", false);
    bool expRenderer = expRendererPrev;
    if (Widgets::ToggleSwitch("New Render Engine [!] beta", &expRenderer, theme, anim)) {
        if (expRenderer) {
            ImGui::OpenPopup("##ExpRendererWarn");
        } else {
            mod->setSavedValue("experimental_new_renderer", false);
            expRendererRestartNotice = true;
        }
    }
    if (expRendererRestartNotice) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
        imguiTextTr("Restart Geometry Dash to apply this change.");
        ImGui::PopStyleColor();
    }

    constexpr float kExpWarnRounding = 0.0f;
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kExpWarnRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 150));
    if (ImGui::BeginPopupModal("##ExpRendererWarn", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        drawPopupChrome(*this, "WARNING", kExpWarnRounding);
        imguiTextTr("Experimental Render Engine");
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
        ImGui::TextUnformatted("[!] This feature is in beta.");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
        ImGui::TextWrapped(
            "The new render engine replaces the existing encode pipeline and may behave "
            "differently or have bugs. Geometry Dash must be restarted to apply the change.");
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 10));
        float halfW = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
        if (Widgets::StyledButton("Enable Anyway##expWarnOk", ImVec2(halfW, 30.0f), theme, anim, 6.0f)) {
            mod->setSavedValue("experimental_new_renderer", true);
            expRendererRestartNotice = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(0, 8);
        if (Widgets::StyledButton("Cancel##expWarnCancel", ImVec2(halfW, 30.0f), theme, anim, 6.0f)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Credits", theme);
    if (Widgets::StyledButton("View Credits", ImVec2(-1, 32), theme, anim)) {
        ImGui::OpenPopup("Credits##settingsCredits");
    }

    constexpr float kCreditsPopupRounding = 0.0f;
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kCreditsPopupRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginPopupModal(
        "Credits##settingsCredits",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
    )) {
        auto creditsTitle = trString("Credits");
        drawPopupChrome(*this, creditsTitle.c_str(), kCreditsPopupRounding);

        Widgets::SectionHeader("Fellow Devs", theme);

        struct CreditEntry {
            const char* name;
            const char* url;
            const char* buttonId;
        };

        static constexpr std::array<CreditEntry, 3> credits = {{
            {"ThisisLinh", "https://github.com/linhisreal", "GitHub##creditsLinh"},
            {"Nigel", "https://github.com/guccimanefan", "GitHub##creditsNigel"},
            {"Human", "https://github.com/MEME-KING16", "GitHub##creditsHuman"},
        }};

        auto drawCreditRow = [&](CreditEntry const& credit) {
            ImVec2 rowPos = ImGui::GetCursorScreenPos();
            float rowW = ImGui::GetContentRegionAvail().x;
            constexpr float rowH = 46.0f;
            ImVec2 rowMax(rowPos.x + rowW, rowPos.y + rowH);

            drawSolidRect(ImGui::GetWindowDrawList(), rowPos, rowMax, theme.cornerRadius, theme, 0.44f);

            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 12.0f, rowPos.y + 8.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textPrimary);
            ImGui::TextUnformatted(credit.name);
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 12.0f, rowPos.y + 27.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
            ImGui::TextUnformatted(credit.url);
            ImGui::PopStyleColor();

            constexpr float buttonW = 82.0f;
            constexpr float buttonH = 26.0f;
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + rowW - buttonW - 10.0f, rowPos.y + 10.0f));
            if (Widgets::StyledButton(credit.buttonId, ImVec2(buttonW, buttonH), theme, anim, 6.0f)) {
                geode::utils::web::openLinkInBrowser(credit.url);
            }

            ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowMax.y + 6.0f));
        };

        for (auto const& credit : credits) {
            drawCreditRow(credit);
        }

        ImGui::Dummy(ImVec2(0, 2));
        ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
        ImGui::TextWrapped(
            "%s",
            "Thank you to my loyal devs, and to all the supporters who have kept ToastyReplay alive and running, your contributions mean the world to me. If you want to contribute to this project, buying ToastyReplay Pro helps support me, and my devs."
        );
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 8));
        if (Widgets::StyledButton("Open ToastyReplay Website##creditsWebsite", ImVec2(-1, 30.0f), theme, anim, 6.0f)) {
            geode::utils::web::openLinkInBrowser("https://toastyreplay.xyz/");
        }
        ImGui::Dummy(ImVec2(0, 4));
        if (Widgets::StyledButton("Ok##creditsClose", ImVec2(-1, 28.0f), theme, anim, 6.0f)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
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

        auto uploadMacroLabel = [engine](std::string const& macroName) {
            std::string label = macroName;
            if (engine->ttr2Macros.count(macroName)) {
                label += "  [TTR2]";
            } else if (engine->ttrMacros.count(macroName)) {
                label += "  [TTR]";
            } else {
                label += "  [GDR]";
            }
            if (engine->cbsMacros.count(macroName)) label += " [CBS]";
            if (engine->legacyCbsMacros.count(macroName) && !engine->ttr2Macros.count(macroName)) label += " [PLAYBACK ONLY]";
            if (engine->platformerMacros.count(macroName)) {
                label += " [";
                label += trString("PLAT");
                label += "]";
            }
            return label;
        };

        auto emptyMacroPreview = trString("Select a macro...");
        std::string selectedMacroPreview;
        const char* preview = emptyMacroPreview.c_str();
        if (selectedUploadMacro >= 0) {
            selectedMacroPreview = uploadMacroLabel(macros[selectedUploadMacro]);
            preview = selectedMacroPreview.c_str();
        }

        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 25, 200));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(25, 25, 30, 240));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::BeginCombo("##macro_select", preview)) {
            for (int i = 0; i < static_cast<int>(macros.size()); i++) {
                
                if (engine->incompatibleMacros.count(macros[i])) continue;

                bool selected = (selectedUploadMacro == i);
                std::string label = uploadMacroLabel(macros[i]);

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

        bool selectedLegacyCBS = selectedUploadMacro >= 0 &&
            engine->legacyCbsMacros.count(macros[selectedUploadMacro]) &&
            !engine->ttr2Macros.count(macros[selectedUploadMacro]);
        bool canUpload = selectedUploadMacro >= 0 &&
            !selectedLegacyCBS &&
            online->uploadState != OnlineClient::PENDING &&
            online->canUploadMacros();
        if (!canUpload) ImGui::BeginDisabled();
        if (Widgets::StyledButton("Upload to Discord", ImVec2(-1, 32), theme, anim)) {
            std::string comment(uploadCommentBuf);
            online->uploadMacro(macros[selectedUploadMacro], comment);
            uploadCommentBuf[0] = '\0';
        }
        if (!canUpload) ImGui::EndDisabled();
        if (selectedLegacyCBS) {
            ImGui::Dummy(ImVec2(0, 4));
            auto legacyText = trString("Legacy CBS macros are playback only. Re-record in TTR2 CBS mode for exact timing.");
            ImGui::TextWrapped("%s", legacyText.c_str());
        }
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

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Upgrade to Pro", theme);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, theme.textSecondary);
    imguiTextWrappedTr("Create or sign in to your ToastyReplay account to upgrade.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 6));
    if (Widgets::StyledButton("Open ToastyReplay Website", ImVec2(-1, 32), theme, anim)) {
        geode::utils::web::openLinkInBrowser("https://toastyreplay.xyz/");
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
    mod->setSavedValue("pride_logo", prideLogoEnabled);

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
    mod->setSavedValue("key_disable_shaders", keybinds.disableShaders);
    mod->setSavedValue("key_click_sounds", keybinds.clickSounds);

    mod->setSavedValue("hack_auto_safe_mode", eng->autoSafeMode);
    mod->setSavedValue("hack_hitboxes", eng->showHitboxes);
    mod->setSavedValue("hack_hitbox_death", eng->hitboxOnDeath);
    mod->setSavedValue("hack_hitbox_trail", eng->hitboxTrail);
    mod->setSavedValue("hack_hitbox_trail_len", eng->hitboxTrailLength);
    mod->setSavedValue("hack_trajectory", eng->pathPreview);
    eng->pathLength = ReplayEngine::sanitizeTrajectoryLength(eng->pathLength);
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
    mod->setSavedValue("hack_disable_shaders", eng->disableShaders);
    mod->setSavedValue("hack_no_mirror_rec_only", eng->noMirrorRecordingOnly);
    mod->setSavedValue("hack_fast_playback", eng->fastPlayback);

    auto* ac = Autoclicker::get();
    mod->setSavedValue("ac_enabled", ac->enabled);
    mod->setSavedValue("ac_player1", ac->player1);
    mod->setSavedValue("ac_player2", ac->player2);
    mod->setSavedValue("ac_mode", static_cast<int>(ac->mode));
    mod->setSavedValue("ac_hold_ticks", ac->holdTicks);
    mod->setSavedValue("ac_release_ticks", ac->releaseTicks);
    mod->setSavedValue("ac_target_cps", (double)ac->targetCps);
    mod->setSavedValue("ac_hold_ratio", (double)ac->holdRatio);
    mod->setSavedValue("ac_only_holding", ac->onlyWhileHolding);
    mod->setSavedValue("key_autoclicker", keybinds.autoclicker);

    mod->setSavedValue("eng_accuracy_mode", static_cast<int>(eng->selectedAccuracyMode));
    mod->setSavedValue("eng_tick_rate", (float)eng->tickRate);
    mod->setSavedValue("eng_speed", (float)eng->gameSpeed);
    mod->setSavedValue("eng_ttr_mode", eng->ttrMode);
    mod->setSavedValue("eng_completion_autosave", eng->completionAutosave);
    mod->setSavedValue("eng_persistence_mode", eng->persistenceMode);

    mod->setSavedValue("render_width", (int64_t)std::atoi(renderWidthBuf));
    mod->setSavedValue("render_height", (int64_t)std::atoi(renderHeightBuf));
    mod->setSavedValue("render_fps", (int64_t)std::atoi(renderFpsBuf));
    mod->setSavedValue("render_codec", std::string(renderCodecBuf));
    mod->setSavedValue("render_bitrate", std::string(renderBitrateBuf));
    mod->setSavedValue("render_file_extension", std::string(renderExtBuf));
    mod->setSavedValue("render_args", std::string(renderArgsBuf));
    mod->setSavedValue("render_video_args", std::string(renderVideoArgsBuf));
    mod->setSavedValue("render_audio_args",  std::string(renderAudioArgsBuf));
    mod->setSavedValue("render_audio_codec", std::string(renderAudioCodecBuf));
    mod->setSavedValue("render_seconds_after", std::string(renderSecondsAfterBuf));
    mod->setSavedValue("render_include_audio", renderIncludeAudio);
    mod->setSavedValue("render_include_clicks", renderIncludeClicks);
    mod->setSavedValue("render_sfx_volume", (double)renderSfxVol);
    mod->setSavedValue("render_music_volume", (double)renderMusicVol);
    mod->setSavedValue("render_hide_endscreen", renderHideEndscreen);
    mod->setSavedValue("render_hide_levelcomplete", renderHideLevelComplete);
    mod->setSavedValue("render_watermark_enabled", renderWatermarkEnabled);
    mod->setSavedValue("render_watermark_font", renderWatermarkFont);
    mod->setSavedValue("render_watermark_corner", renderWatermarkCorner);
    mod->setSavedValue("render_watermark_scale", renderWatermarkScale);

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
    mod->setSavedValue("click_mute_left_right", csm->muteLeftRightClicks);
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

    auto audioCodecStr = mod->getSavedValue<std::string>("render_audio_codec", "aac");
    if (audioCodecStr.empty()) audioCodecStr = "aac";
    snprintf(renderAudioCodecBuf, sizeof(renderAudioCodecBuf), "%s", audioCodecStr.c_str());

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
    snprintf(backupAudioArgsBuf,    sizeof(backupAudioArgsBuf),    "%s", renderAudioArgsBuf);
    snprintf(backupAudioCodecBuf,   sizeof(backupAudioCodecBuf),   "%s", renderAudioCodecBuf);
    snprintf(backupSecondsAfterBuf, sizeof(backupSecondsAfterBuf), "%s", renderSecondsAfterBuf);

    showAdvancedWarning = false;
    advancedWarningAccepted = false;
    renderBufsInit = true;
}

// has to make another one (this is exprimental render engine)
void MenuInterface::applyRenderPreset(RenderPreset const& preset) {
    auto* mod = Mod::get();

    snprintf(renderWidthBuf,      sizeof(renderWidthBuf),      "%d",  preset.width);
    snprintf(renderHeightBuf,     sizeof(renderHeightBuf),     "%d",  preset.height);
    snprintf(renderFpsBuf,        sizeof(renderFpsBuf),        "%d",  preset.fps);
    snprintf(renderCodecBuf,      sizeof(renderCodecBuf),      "%s",  preset.codec.c_str());
    snprintf(renderBitrateBuf,    sizeof(renderBitrateBuf),    "%s",  preset.bitrate.c_str());
    snprintf(renderExtBuf,        sizeof(renderExtBuf),        "%s",  preset.ext.c_str());
    snprintf(renderArgsBuf,       sizeof(renderArgsBuf),       "%s",  preset.extraArgs.c_str());
    snprintf(renderVideoArgsBuf,  sizeof(renderVideoArgsBuf),  "%s",  preset.videoArgs.c_str());
    snprintf(renderAudioArgsBuf,  sizeof(renderAudioArgsBuf),  "%s",  preset.audioArgs.c_str());
    snprintf(renderAudioCodecBuf, sizeof(renderAudioCodecBuf), "%s",
        preset.audioCodec.empty() ? "aac" : preset.audioCodec.c_str());
    snprintf(renderSecondsAfterBuf, sizeof(renderSecondsAfterBuf), "%g", preset.secondsAfter);

    renderIncludeAudio      = preset.includeAudio;
    renderIncludeClicks     = preset.includeClicks;
    renderSfxVol            = preset.sfxVol;
    renderMusicVol          = preset.musicVol;
    renderHideEndscreen     = preset.hideEndscreen;
    renderHideLevelComplete = preset.hideLevelComplete;

    mod->setSavedValue("render_width",              (int64_t)preset.width);
    mod->setSavedValue("render_height",             (int64_t)preset.height);
    mod->setSavedValue("render_fps",                (int64_t)preset.fps);
    mod->setSavedValue("render_codec",              preset.codec);
    mod->setSavedValue("render_bitrate",            preset.bitrate);
    mod->setSavedValue("render_file_extension",     preset.ext);
    mod->setSavedValue("render_args",               preset.extraArgs);
    mod->setSavedValue("render_video_args",         preset.videoArgs);
    mod->setSavedValue("render_audio_args",         preset.audioArgs);
    mod->setSavedValue("render_audio_codec",        preset.audioCodec.empty() ? std::string("aac") : preset.audioCodec);
    mod->setSavedValue("render_seconds_after",      preset.secondsAfter > 0 ? std::string(renderSecondsAfterBuf) : std::string("0"));
    mod->setSavedValue("render_include_audio",      preset.includeAudio);
    mod->setSavedValue("render_include_clicks",     preset.includeClicks);
    mod->setSavedValue("render_sfx_volume",         (double)preset.sfxVol);
    mod->setSavedValue("render_music_volume",       (double)preset.musicVol);
    mod->setSavedValue("render_hide_endscreen",     preset.hideEndscreen);
    mod->setSavedValue("render_hide_levelcomplete", preset.hideLevelComplete);

    struct ResPreset { const char* name; int width; int height; };
    static const ResPreset resPresets[] = {
        { "720p  (1280x720)",   1280,  720 },
        { "1080p (1920x1080)",  1920, 1080 },
        { "1440p (2560x1440)",  2560, 1440 },
        { "4K    (3840x2160)",  3840, 2160 },
    };
    renderPresetIndex = 1;
    for (int i = 0; i < static_cast<int>(sizeof(resPresets) / sizeof(resPresets[0])); i++) {
        if (resPresets[i].width == preset.width && resPresets[i].height == preset.height) {
            renderPresetIndex = i;
            break;
        }
    }

    snprintf(backupCodecBuf,        sizeof(backupCodecBuf),        "%s", renderCodecBuf);
    snprintf(backupBitrateBuf,      sizeof(backupBitrateBuf),      "%s", renderBitrateBuf);
    snprintf(backupExtBuf,          sizeof(backupExtBuf),          "%s", renderExtBuf);
    snprintf(backupArgsBuf,         sizeof(backupArgsBuf),         "%s", renderArgsBuf);
    snprintf(backupVideoArgsBuf,    sizeof(backupVideoArgsBuf),    "%s", renderVideoArgsBuf);
    snprintf(backupAudioArgsBuf,    sizeof(backupAudioArgsBuf),    "%s", renderAudioArgsBuf);
    snprintf(backupAudioCodecBuf,   sizeof(backupAudioCodecBuf),   "%s", renderAudioCodecBuf);
    snprintf(backupSecondsAfterBuf, sizeof(backupSecondsAfterBuf), "%s", renderSecondsAfterBuf);

    advancedWarningAccepted = true;
    showAdvancedWarning = false;
}

RenderPreset MenuInterface::captureRenderPreset() {
    RenderPreset p;
    if (auto w = toasty::parseInteger<int>(renderWidthBuf))     p.width  = *w;
    if (auto h = toasty::parseInteger<int>(renderHeightBuf))    p.height = *h;
    if (auto f = toasty::parseInteger<int>(renderFpsBuf))       p.fps    = *f;
    p.codec      = renderCodecBuf;
    p.bitrate    = renderBitrateBuf;
    p.ext        = renderExtBuf;
    p.extraArgs  = renderArgsBuf;
    p.videoArgs  = renderVideoArgsBuf;
    p.audioArgs  = renderAudioArgsBuf;
    p.audioCodec = (renderAudioCodecBuf[0] && std::string(renderAudioCodecBuf) != "aac")
                   ? renderAudioCodecBuf : "";
    float after = 3.0f;
    auto* begin = renderSecondsAfterBuf;
    std::from_chars(begin, begin + strlen(begin), after);
    p.secondsAfter      = after;
    p.includeAudio  = renderIncludeAudio;
    p.includeClicks = renderIncludeClicks;
    p.sfxVol        = renderSfxVol;
    p.musicVol      = renderMusicVol;
    return p;
}

void MenuInterface::loadExpRenderSettings() {
    expConfig = loadRenderConfig();

    auto renderName = Mod::get()->getSavedValue<std::string>("render_name", "");
    snprintf(expRenderNameBuf, sizeof(expRenderNameBuf), "%s", renderName.c_str());
    snprintf(expRenderFpsBuf, sizeof(expRenderFpsBuf), "%u", expConfig.fps);

    {
        auto rp = resolve(expConfig);
        snprintf(expAdvCodecBuf,      sizeof(expAdvCodecBuf),      "%s", expConfig.codec.value_or(rp.codec).c_str());
        snprintf(expAdvCrfBuf,        sizeof(expAdvCrfBuf),        "%d", expConfig.crf.value_or(rp.crf));
        snprintf(expAdvExtraArgsBuf,  sizeof(expAdvExtraArgsBuf),  "%s", expConfig.extraArgs.value_or(rp.extraArgs).c_str());
        snprintf(expAdvAudioCodecBuf, sizeof(expAdvAudioCodecBuf), "%s", expConfig.audioCodec.value_or(rp.audioCodec).c_str());
    }
    snprintf(expAdvMaxBitrateBuf,   sizeof(expAdvMaxBitrateBuf),  "%s", expConfig.maxBitrate.value_or("").c_str());
    snprintf(expAdvExtBuf,          sizeof(expAdvExtBuf),         "%s", expConfig.ext.value_or(".mp4").c_str());
    snprintf(expAdvVideoArgsBuf,    sizeof(expAdvVideoArgsBuf),   "%s", expConfig.videoArgs.value_or(kDefaultVideoArgs).c_str());
    snprintf(expAdvAudioArgsBuf,    sizeof(expAdvAudioArgsBuf),   "%s", expConfig.audioArgs.value_or("").c_str());
    snprintf(expAdvSecondsAfterBuf, sizeof(expAdvSecondsAfterBuf), "%g", expConfig.secondsAfter);

    expConfigInit = true;
}

void MenuInterface::drawExpRenderTab() {
    auto* engine = ReplayEngine::get();
    auto* mod = Mod::get();
    Renderer& r = engine->renderer;

    if (!expConfigInit)
        loadExpRenderSettings();

    if (!expGpuProbed) {
        expConfig.gpuEncoder = probeGpuEncoder(expConfig.codecFamily);
        expGpuProbed = true;
    }

    struct ResOption { const char* label; unsigned width; unsigned height; };
    static constexpr ResOption kResOptions[] = {
        { "720p  (1280x720)",   1280,  720 },
        { "1080p (1920x1080)",  1920, 1080 },
        { "1440p (2560x1440)",  2560, 1440 },
        { "4K    (3840x2160)",  3840, 2160 },
    };
    static constexpr const char* kTierLabels[] = { "Fast", "Balanced", "Quality", "Lossless" };

    float inputW = ImGui::GetContentRegionAvail().x * 0.45f;

    Widgets::SectionHeader("Render", theme);

    imguiTextTr("Name");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##expRenderName", expRenderNameBuf, sizeof(expRenderNameBuf)))
        mod->setSavedValue("render_name", std::string(expRenderNameBuf));

    ImGui::Dummy(ImVec2(0, 4));

    if (r.recording) {
        Widgets::StatusBadge("Rendering", ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::Dummy(ImVec2(0, 4));
        if (Widgets::StyledButton("Stop Render", ImVec2(ImGui::GetContentRegionAvail().x, 36), theme, anim))
            r.toggle();
    } else {
        if (Widgets::StyledButton("Start Render", ImVec2(ImGui::GetContentRegionAvail().x, 36), theme, anim)) {
            if (auto v = toasty::parseInteger<unsigned>(expRenderFpsBuf))
                expConfig.fps = *v;

            expConfig.codec     = expAdvCodecBuf[0]      ? std::optional<std::string>(expAdvCodecBuf)      : std::nullopt;
            expConfig.maxBitrate = expAdvMaxBitrateBuf[0] ? std::optional<std::string>(expAdvMaxBitrateBuf) : std::nullopt;
            expConfig.ext       = expAdvExtBuf[0]         ? std::optional<std::string>(expAdvExtBuf)        : std::nullopt;
            expConfig.extraArgs  = expAdvExtraArgsBuf[0]  ? std::optional<std::string>(expAdvExtraArgsBuf)  : std::nullopt;
            expConfig.videoArgs  = expAdvVideoArgsBuf[0]  ? std::optional<std::string>(expAdvVideoArgsBuf)  : std::nullopt;
            expConfig.audioArgs  = expAdvAudioArgsBuf[0]  ? std::optional<std::string>(expAdvAudioArgsBuf)  : std::nullopt;
            expConfig.audioCodec = expAdvAudioCodecBuf[0] ? std::optional<std::string>(expAdvAudioCodecBuf) : std::nullopt;
            if (expAdvCrfBuf[0]) {
                if (auto v = toasty::parseInteger<int>(expAdvCrfBuf))
                    expConfig.crf = *v;
            } else {
                expConfig.crf = std::nullopt;
            }
            expConfig.secondsAfter = 3.0f;
            if (auto v = geode::utils::numFromString<float>(expAdvSecondsAfterBuf))
                expConfig.secondsAfter = v.unwrapOr(3.0f);

            saveRenderConfig(expConfig);
            engine->renderer.m_pendingConfig = expConfig;
            engine->renderer.toggle();
        }
    }

    drawRenderPresetsSection();

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Resolution", theme);

    int resIndex = 1;
    for (int i = 0; i < 4; i++) {
        if (kResOptions[i].width == expConfig.width && kResOptions[i].height == expConfig.height) {
            resIndex = i;
            break;
        }
    }
    imguiTextTr("Resolution");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##expResCombo", kResOptions[resIndex].label)) {
        for (int i = 0; i < 4; i++) {
            bool sel = (resIndex == i);
            if (ImGui::Selectable(kResOptions[i].label, sel)) {
                expConfig.width  = kResOptions[i].width;
                expConfig.height = kResOptions[i].height;
                saveRenderConfig(expConfig);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    imguiTextTr("FPS");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##expFps", expRenderFpsBuf, sizeof(expRenderFpsBuf), ImGuiInputTextFlags_CharsDecimal)) {
        if (auto v = toasty::parseInteger<unsigned>(expRenderFpsBuf))
            expConfig.fps = *v;
        saveRenderConfig(expConfig);
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Encoding", theme);

    int tierIdx = static_cast<int>(expConfig.tier);
    imguiTextTr("Quality");
    ImGui::SameLine(inputW);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##expQuality", kTierLabels[tierIdx])) {
        for (int i = 0; i < 4; i++) {
            bool sel = (tierIdx == i);
            if (ImGui::Selectable(kTierLabels[i], sel)) {
                expConfig.tier = static_cast<RenderQualityTier>(i);
                if (expConfig.tier == RenderQualityTier::Lossless)
                    expConfig.useGpu = false;
                else if (expConfig.tier == RenderQualityTier::Fast || expConfig.tier == RenderQualityTier::Balanced)
                    expConfig.useGpu = !expConfig.gpuEncoder.empty();
                expConfig.codec     = std::nullopt;
                expConfig.crf       = std::nullopt;
                expConfig.extraArgs = std::nullopt;
                {
                    auto rp = resolve(expConfig);
                    snprintf(expAdvCodecBuf,     sizeof(expAdvCodecBuf),     "%s", rp.codec.c_str());
                    snprintf(expAdvCrfBuf,       sizeof(expAdvCrfBuf),       "%d", rp.crf);
                    snprintf(expAdvExtraArgsBuf, sizeof(expAdvExtraArgsBuf), "%s", rp.extraArgs.c_str());
                }
                saveRenderConfig(expConfig);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    bool gpuAvail = !expConfig.gpuEncoder.empty();
    bool isLossless = expConfig.tier == RenderQualityTier::Lossless;
    bool isAv1 = expConfig.codecFamily == RenderCodecFamily::AV1;
#ifdef GEODE_IS_WINDOWS
    auto ffmpegSetting = Mod::get()->getSettingValue<std::filesystem::path>("ffmpeg_path");
    bool ffmpegAvail = false;
    if (!ffmpegSetting.empty()) {
        std::error_code ec;
        ffmpegAvail = std::filesystem::exists(ffmpegSetting, ec);
    } else {
        wchar_t ffmpegFound[MAX_PATH] = {};
        ffmpegAvail = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, ffmpegFound, nullptr) > 0;
    }
#else
    bool ffmpegAvail = true;
#endif
    bool av1NeedsExe = isAv1 && !ffmpegAvail;

    if (av1NeedsExe) expConfig.useGpu = false;
    imguiTextTr("Use GPU");
    ImGui::SameLine(inputW);
    ImGui::BeginDisabled(!gpuAvail || isLossless || av1NeedsExe);
    if (Widgets::ToggleSwitch("##gpuToggle", &expConfig.useGpu, theme, anim)) {
        expConfig.codec = std::nullopt; expConfig.crf = std::nullopt; expConfig.extraArgs = std::nullopt;
        auto rp = resolve(expConfig);
        snprintf(expAdvCodecBuf,     sizeof(expAdvCodecBuf),     "%s", rp.codec.c_str());
        snprintf(expAdvCrfBuf,       sizeof(expAdvCrfBuf),       "%d", rp.crf);
        snprintf(expAdvExtraArgsBuf, sizeof(expAdvExtraArgsBuf), "%s", rp.extraArgs.c_str());
        saveRenderConfig(expConfig);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 8.0f);
    float toggleHeight = 22.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (toggleHeight - ImGui::GetTextLineHeight()) * 0.5f);
    if (isLossless || av1NeedsExe)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    else if (gpuAvail)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.85f, 0.4f, 1.0f));
    else
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    if (isLossless)
        ImGui::TextUnformatted("[cpu only]");
    else if (av1NeedsExe)
        ImGui::TextUnformatted("[needs ffmpeg.exe]");
    else if (gpuAvail)
        ImGui::TextUnformatted(expConfig.gpuEncoder.c_str());
    else
        ImGui::TextUnformatted("[unavailable]");
    ImGui::PopStyleColor();

    // Codec family picker: H.264 / AV1
    imguiTextTr("Codec Family");
    ImGui::SameLine(inputW);
    {
        float pillW = (ImGui::GetContentRegionAvail().x - 4.0f) * 0.5f;
        bool isH264 = expConfig.codecFamily == RenderCodecFamily::H264;
        auto refreshTierBufs = [&]() {
            expConfig.codec = std::nullopt; expConfig.crf = std::nullopt; expConfig.extraArgs = std::nullopt;
            auto rp = resolve(expConfig);
            snprintf(expAdvCodecBuf,     sizeof(expAdvCodecBuf),     "%s", rp.codec.c_str());
            snprintf(expAdvCrfBuf,       sizeof(expAdvCrfBuf),       "%d", rp.crf);
            snprintf(expAdvExtraArgsBuf, sizeof(expAdvExtraArgsBuf), "%s", rp.extraArgs.c_str());
        };
        if (Widgets::PillButton("H.264", isH264, pillW, theme, anim)) {
            if (!isH264) {
                expConfig.codecFamily = RenderCodecFamily::H264;
                expConfig.gpuEncoder  = probeGpuEncoder(RenderCodecFamily::H264);
                expGpuProbed = false;
                refreshTierBufs();
                saveRenderConfig(expConfig);
            }
        }
        ImGui::SameLine(0, 4);
        if (Widgets::PillButton("AV1", !isH264, pillW, theme, anim)) {
            if (isH264) {
                expConfig.codecFamily = RenderCodecFamily::AV1;
                expConfig.gpuEncoder  = probeGpuEncoder(RenderCodecFamily::AV1);
                expGpuProbed = false;
                refreshTierBufs();
                saveRenderConfig(expConfig);
            }
        }
    }

    drawRenderAudioSection();
    drawRenderDisplaySection();

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
    bool headerOpen = ImGui::CollapsingHeader("Advanced##expRender", expAdvancedOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleColor();
    expAdvancedOpen = headerOpen;

    if (headerOpen) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        imguiTextWrappedTr("These fields override the selected quality preset. Leave blank to use preset defaults.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));

        auto advInput = [&](const char* label, const char* id, char* buf, size_t sz) {
            imguiTextTr(label);
            ImGui::SameLine(inputW);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText(id, buf, sz);
        };

        advInput("Codec",        "##expAdvCodec",    expAdvCodecBuf,      sizeof(expAdvCodecBuf));
        advInput("CRF",          "##expAdvCrf",      expAdvCrfBuf,        sizeof(expAdvCrfBuf));
        advInput("Max Bitrate",  "##expAdvBitrate",  expAdvMaxBitrateBuf, sizeof(expAdvMaxBitrateBuf));
        advInput("Extension",    "##expAdvExt",      expAdvExtBuf,        sizeof(expAdvExtBuf));
        advInput("Extra Args",   "##expAdvArgs",     expAdvExtraArgsBuf,  sizeof(expAdvExtraArgsBuf));
        advInput("Video Filter", "##expAdvVArgs",    expAdvVideoArgsBuf,  sizeof(expAdvVideoArgsBuf));
        if (ImGui::IsItemDeactivatedAfterEdit() && !expAdvVideoArgsBuf[0])
            snprintf(expAdvVideoArgsBuf, sizeof(expAdvVideoArgsBuf), "%s", kDefaultVideoArgs);
        {
            int acodecIdx = audioCodecComboIndex(expAdvAudioCodecBuf);
            imguiTextTr("Audio Codec");
            ImGui::SameLine(inputW);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##expAdvACodecCombo", kAudioCodecLabels[acodecIdx])) {
                for (int i = 0; i <= kAudioCodecCustomIdx; ++i) {
                    bool sel = (acodecIdx == i);
                    if (ImGui::Selectable(kAudioCodecLabels[i], sel)) {
                        char prevCodec[64];
                        snprintf(prevCodec, sizeof(prevCodec), "%s", expAdvAudioCodecBuf);
                        if (i < kAudioCodecCustomIdx)
                            snprintf(expAdvAudioCodecBuf, sizeof(expAdvAudioCodecBuf), "%s", kAudioCodecIds[i]);
                        else
                            expAdvAudioCodecBuf[0] = '\0';
                        expConfig.audioCodec = expAdvAudioCodecBuf[0]
                            ? std::optional<std::string>(expAdvAudioCodecBuf) : std::nullopt;
                        if (audioCodecNeedsMkv(expAdvAudioCodecBuf) && std::string(expAdvExtBuf) == ".mp4") {
                            showMkvWarning = true;
                            mkvWarningIsExp = true;
                            snprintf(mkvWarningPrevCodec, sizeof(mkvWarningPrevCodec), "%s", prevCodec);
                        }
                        saveRenderConfig(expConfig);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (acodecIdx == kAudioCodecCustomIdx) {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##expAdvACodecCustom", expAdvAudioCodecBuf, sizeof(expAdvAudioCodecBuf))) {
                    expConfig.audioCodec = expAdvAudioCodecBuf[0]
                        ? std::optional<std::string>(expAdvAudioCodecBuf) : std::nullopt;
                    saveRenderConfig(expConfig);
                }
            }
        }
        advInput("Audio Args",   "##expAdvAArgs",    expAdvAudioArgsBuf,  sizeof(expAdvAudioArgsBuf));
        advInput("Seconds After","##expAdvSecs",     expAdvSecondsAfterBuf, sizeof(expAdvSecondsAfterBuf));

        ImGui::Dummy(ImVec2(0, 4));
        bool apiAvail = Loader::get()->isModLoaded("eclipse.ffmpeg-api");
        Widgets::StatusBadge(
            apiAvail ? "FFmpeg API Available" : "FFmpeg API Not Found",
            apiAvail ? ImVec4(0.3f, 0.85f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f)
        );
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Rendered Videos", theme);

    std::filesystem::path renderFolder = mod->getSettingValue<std::filesystem::path>("render_folder");
    if (renderFolder.empty() || toasty::pathToUtf8(renderFolder).find("{gd_dir}") != std::string::npos)
        renderFolder = geode::dirs::getGameDir() / "renders";

    std::vector<std::filesystem::directory_entry> renderFiles;
    if (std::filesystem::exists(renderFolder)) {
        for (auto& entry : std::filesystem::directory_iterator(renderFolder))
            if (entry.is_regular_file()) renderFiles.push_back(entry);
        std::sort(renderFiles.begin(), renderFiles.end(), [](auto& a, auto& b) {
            return a.last_write_time() > b.last_write_time();
        });
    }

    float rvPadY = 8.0f, rvPadX = 10.0f;
    float rvRowH = ImGui::GetTextLineHeight() + 10.0f;
    float rvListH = std::max(80.0f, std::min(200.0f, (float)renderFiles.size() * rvRowH + rvPadY * 2.0f));
    ImVec2 rvListPos = ImGui::GetCursorScreenPos();
    float rvListW = ImGui::GetContentRegionAvail().x;
    drawSolidRect(ImGui::GetWindowDrawList(), rvListPos,
        ImVec2(rvListPos.x + rvListW, rvListPos.y + rvListH), theme.cornerRadius, theme, 0.55f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##ExpRVList", ImVec2(-1, rvListH), false);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rvPadX);
    ImGui::Dummy(ImVec2(0, rvPadY));

    if (renderFiles.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rvPadX);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
        imguiTextTr("No rendered videos");
        ImGui::PopStyleColor();
    }

    for (int i = 0; i < (int)renderFiles.size(); i++) {
        auto& entry = renderFiles[i];
        std::string name = toasty::pathToUtf8(entry.path().stem());
        std::string ext  = toasty::pathToUtf8(entry.path().extension());
        ImGui::PushID(i);

        float fullRowW = ImGui::GetContentRegionAvail().x;

        auto fileSize = entry.file_size();
        std::string sizeStr;
        if (fileSize >= 1024 * 1024 * 1024)
            sizeStr = fmt::format("{:.1f}GB", fileSize / (1024.0 * 1024.0 * 1024.0));
        else if (fileSize >= 1024 * 1024)
            sizeStr = fmt::format("{:.1f}MB", fileSize / (1024.0 * 1024.0));
        else
            sizeStr = fmt::format("{:.0f}KB", fileSize / 1024.0);

        auto ftime  = entry.last_write_time();
        auto stime  = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        auto tt     = std::chrono::system_clock::to_time_t(stime);
        std::tm tmv{};
        localtime_s(&tmv, &tt);
        std::string dateStr = fmt::format("{:02d}/{:02d}", tmv.tm_mon + 1, tmv.tm_mday);

        std::string resBadge;
        ImVec4 badgeColor(0.5f, 0.5f, 0.5f, 1.0f);
        {
            std::regex resRx("_(\\d+)x(\\d+)_");
            std::smatch m;
            if (std::regex_search(name, m, resRx)) {
                int pw = 0, ph = 0;
                if (auto w = toasty::parseInteger<int>(m[1].str())) pw = *w;
                if (auto h = toasty::parseInteger<int>(m[2].str())) ph = *h;
                if      (pw == 1280 && ph == 720)  { resBadge = "720p";  badgeColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); }
                else if (pw == 1920 && ph == 1080) { resBadge = "1080p"; badgeColor = ImVec4(0.3f, 0.85f, 0.4f, 1.0f); }
                else if (pw == 2560 && ph == 1440) { resBadge = "1440p"; badgeColor = ImVec4(0.9f, 0.7f, 0.2f, 1.0f); }
                else if (pw == 3840 && ph == 2160) { resBadge = "4K";   badgeColor = ImVec4(0.9f, 0.3f, 0.3f, 1.0f); }
                else if (pw > 0 && ph > 0) resBadge = fmt::format("{}x{}", pw, ph);
            }
        }

        float btnW   = 26.0f;
        float dateW  = ImGui::CalcTextSize(dateStr.c_str()).x + 8.0f;
        float sizeW  = ImGui::CalcTextSize(sizeStr.c_str()).x + 8.0f;
        float badgeW = resBadge.empty() ? 0.0f : (ImGui::CalcTextSize(resBadge.c_str()).x + 18.0f);
        float rightW = badgeW + sizeW + dateW + btnW * 2 + rvPadX;
        float maxNameW = std::max(40.0f, fullRowW - rightW - rvPadX);

        std::string rowLabel = name + ext;
        if (ImGui::CalcTextSize(rowLabel.c_str()).x > maxNameW) {
            std::string clipped = rowLabel;
            while (!clipped.empty() && ImGui::CalcTextSize((clipped + "...").c_str()).x > maxNameW)
                clipped.pop_back();
            rowLabel = clipped + "...";
        }

        bool isDelConfirm = (expRvDeleteConfirm == i);
        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        if (isDelConfirm)
            ImGui::GetWindowDrawList()->AddRectFilled(
                rowStart, ImVec2(rowStart.x + fullRowW, rowStart.y + rvRowH),
                IM_COL32(150, 20, 20, 70), 4.0f);

        ImGui::Selectable("##rvrow", false, ImGuiSelectableFlags_AllowOverlap,
            ImVec2(fullRowW - btnW * 2 - 6.0f, rvRowH));

        float textY = rowStart.y + (rvRowH - ImGui::GetTextLineHeight()) * 0.5f;

        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rowStart.x + rvPadX, textY), theme.getTextU32(), rowLabel.c_str());

        float tagX = rowStart.x + fullRowW - rvPadX - btnW * 2 - 6.0f;

        tagX -= dateW;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(tagX, textY), theme.getTextSecondaryU32(), dateStr.c_str());

        tagX -= sizeW;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(tagX, textY), theme.getTextSecondaryU32(), sizeStr.c_str());

        if (!resBadge.empty()) {
            ImVec2 bts  = ImGui::CalcTextSize(resBadge.c_str());
            float bPadX = 6.0f, bPadY = 2.0f;
            float bW    = bts.x + bPadX * 2, bH = bts.y + bPadY * 2;
            tagX -= bW + 8.0f;
            float bY = rowStart.y + (rvRowH - bH) * 0.5f;
            ImVec4 bgCol(badgeColor.x * 0.25f, badgeColor.y * 0.25f, badgeColor.z * 0.25f, 0.85f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(tagX, bY), ImVec2(tagX + bW, bY + bH), toU32(bgCol), 999.0f);
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(tagX, bY), ImVec2(tagX + bW, bY + bH), toU32(withAlpha(badgeColor, 0.95f)), 999.0f, 0, 1.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(tagX + bPadX, bY + bPadY), toU32(withAlpha(badgeColor, 0.98f)), resBadge.c_str());
        }

        // Open button
        ImGui::SameLine(fullRowW - btnW * 2 - 4.0f);
        ImGui::InvisibleButton("##rvopen", ImVec2(btnW, rvRowH));
        {
            bool hov = ImGui::IsItemHovered();
            bool act = ImGui::IsItemClicked();
            ImVec2 bMin = ImGui::GetItemRectMin(), bMax = ImGui::GetItemRectMax();
            if (hov)
                ImGui::GetWindowDrawList()->AddRectFilled(bMin, bMax, IM_COL32(80, 200, 120, 40), 4.0f);
            ImU32 openCol = hov ? IM_COL32(120, 230, 140, 255) : IM_COL32(100, 200, 110, 180);
            ImVec2 sym = ImGui::CalcTextSize(">");
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(bMin.x + (btnW - sym.x) * 0.5f, bMin.y + (rvRowH - sym.y) * 0.5f),
                openCol, ">");
            if (act) {
                expRvDeleteConfirm = -1;
#ifdef _WIN32
                ShellExecuteW(nullptr, L"open", entry.path().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
            }
            if (hov) ImGui::SetTooltip("Open");
        }

        // Delete button
        ImGui::SameLine(fullRowW - btnW);
        ImGui::InvisibleButton("##rvdel", ImVec2(btnW, rvRowH));
        {
            bool hov = ImGui::IsItemHovered();
            bool act = ImGui::IsItemClicked();
            ImVec2 bMin = ImGui::GetItemRectMin(), bMax = ImGui::GetItemRectMax();
            if (isDelConfirm)
                ImGui::GetWindowDrawList()->AddRectFilled(bMin, bMax, IM_COL32(180, 30, 30, 120), 4.0f);
            else if (hov)
                ImGui::GetWindowDrawList()->AddRectFilled(bMin, bMax, IM_COL32(200, 60, 60, 40), 4.0f);
            ImU32 delCol = isDelConfirm ? IM_COL32(255, 100, 100, 255)
                         : hov          ? IM_COL32(230, 80, 80, 255)
                                        : IM_COL32(180, 60, 60, 180);
            ImVec2 sym = ImGui::CalcTextSize("x");
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(bMin.x + (btnW - sym.x) * 0.5f, bMin.y + (rvRowH - sym.y) * 0.5f),
                delCol, "x");
            if (act) {
                if (isDelConfirm) {
                    std::error_code ec;
                    std::filesystem::remove(entry.path(), ec);
                    expRvDeleteConfirm = -1;
                } else {
                    expRvDeleteConfirm = i;
                }
            }
            if (hov) ImGui::SetTooltip(isDelConfirm ? "Click again to confirm" : "Delete");
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

void MenuInterface::loadSettings() {
    auto* mod = Mod::get();
    auto* eng = ReplayEngine::get();
    eng->useNewRenderer = mod->getSavedValue<bool>("experimental_new_renderer", false);
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
    prideLogoEnabled = mod->getSavedValue<bool>("pride_logo", false);

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
    keybinds.disableShaders = mod->getSavedValue<int>("key_disable_shaders", 0);
    keybinds.clickSounds = mod->getSavedValue<int>("key_click_sounds", 0);

    auto* ac = Autoclicker::get();
    ac->enabled = mod->getSavedValue<bool>("ac_enabled", false);
    ac->player1 = mod->getSavedValue<bool>("ac_player1", true);
    ac->player2 = mod->getSavedValue<bool>("ac_player2", false);
    ac->mode = sanitizeAutoclickerMode(mod->getSavedValue<int>("ac_mode", 0));
    ac->holdTicks = mod->getSavedValue<int>("ac_hold_ticks", 1);
    ac->releaseTicks = mod->getSavedValue<int>("ac_release_ticks", 1);
    ac->targetCps = static_cast<float>(mod->getSavedValue<double>("ac_target_cps", 1000.0));
    ac->holdRatio = static_cast<float>(mod->getSavedValue<double>("ac_hold_ratio", 0.5));
    ac->onlyWhileHolding = mod->getSavedValue<bool>("ac_only_holding", false);
    ac->holdTicks = std::clamp(ac->holdTicks, 1, 120);
    ac->releaseTicks = std::clamp(ac->releaseTicks, 1, 120);
    ac->targetCps = std::clamp(ac->targetCps, 1.0f, 20000.0f);
    ac->holdRatio = std::clamp(ac->holdRatio, 0.05f, 0.95f);
    ac->reset();

    eng->showHitboxes = mod->getSavedValue<bool>("hack_hitboxes", false);
    eng->hitboxOnDeath = mod->getSavedValue<bool>("hack_hitbox_death", false);
    eng->hitboxTrail = mod->getSavedValue<bool>("hack_hitbox_trail", false);
    eng->hitboxTrailLength = mod->getSavedValue<int>("hack_hitbox_trail_len", 240);
    eng->pathPreview = mod->getSavedValue<bool>("hack_trajectory", false);
    eng->pathLength = ReplayEngine::sanitizeTrajectoryLength(
        mod->getSavedValue<int>("hack_trajectory_len", ReplayEngine::kTrajectoryLengthDefault)
    );
    eng->collisionBypass = mod->getSavedValue<bool>("hack_noclip", false);
    eng->noclipDeathFlash = mod->getSavedValue<bool>("hack_noclip_flash", true);
    eng->noclipDeathColorR = mod->getSavedValue<float>("hack_noclip_color_r", 1.0f);
    eng->noclipDeathColorG = mod->getSavedValue<float>("hack_noclip_color_g", 0.0f);
    eng->noclipDeathColorB = mod->getSavedValue<float>("hack_noclip_color_b", 0.0f);
    eng->rngLocked = mod->getSavedValue<bool>("hack_rng_lock", false);
    eng->rngSeedVal = mod->getSavedValue<int>("hack_rng_seed", 1);
    eng->protectedMode = mod->getSavedValue<bool>("hack_safe_mode", false);
    eng->autoSafeMode = mod->getSavedValue<bool>("hack_auto_safe_mode", false);
    eng->audioPitchEnabled = mod->getSavedValue<bool>("hack_audio_pitch", true);
    eng->noMirrorEffect = mod->getSavedValue<bool>("hack_no_mirror", false);
    eng->layoutMode = mod->getSavedValue<bool>("hack_layout_mode", false);
    eng->disableShaders = mod->getSavedValue<bool>("hack_disable_shaders", false);
    eng->noMirrorRecordingOnly = mod->getSavedValue<bool>("hack_no_mirror_rec_only", false);
    eng->fastPlayback = mod->getSavedValue<bool>("hack_fast_playback", false);
    eng->selectedAccuracyMode = sanitizeAccuracyMode(mod->getSavedValue<int>("eng_accuracy_mode", 0));
    eng->tickRate = mod->getSavedValue<float>("eng_tick_rate", 240.f);
    eng->gameSpeed = mod->getSavedValue<float>("eng_speed", 1.0f);
    eng->ttrMode = mod->getSavedValue<bool>("eng_ttr_mode", true);
    eng->completionAutosave = mod->getSavedValue<bool>("eng_completion_autosave", true);
    eng->persistenceMode = mod->getSavedValue<bool>("eng_persistence_mode", false);
    if (eng->persistenceMode) {
        eng->completionAutosave = false;
    }
    ReplayEngine::applyRuntimeAccuracyMode(eng->selectedAccuracyMode);

    tempTickRate = (float)eng->tickRate;
    tempGameSpeed = (float)eng->gameSpeed;

    windowSize.x = sanitizeClamped(mod->getSavedValue<float>("window_size_w", 580.0f), 480.0f, 2000.0f, 580.0f);
    windowSize.y = sanitizeClamped(mod->getSavedValue<float>("window_size_h", 540.0f), 400.0f, 2000.0f, 540.0f);
    mainSubTab = std::clamp(mod->getSavedValue<int>("main_sub_tab", 0), 0, 2);
    renderWatermarkEnabled = mod->getSavedValue<bool>("render_watermark_enabled", false);
    renderWatermarkFont = sanitizeRenderWatermarkFont(mod->getSavedValue<int>("render_watermark_font", RENDER_WATERMARK_FONT_NORMAL_PUSAB));
    renderWatermarkCorner = sanitizeRenderWatermarkCorner(mod->getSavedValue<int>("render_watermark_corner", RENDER_WATERMARK_CORNER_BOTTOM_RIGHT));
    renderWatermarkScale = sanitizeClamped(mod->getSavedValue<float>("render_watermark_scale", 1.0f), 0.25f, 3.0f, 1.0f);

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
    csm->muteLeftRightClicks = mod->getSavedValue<bool>("click_mute_left_right", false);
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
        ImVec4 a = theme.getAccent();
        auto versionText = toasty::branding::versionText();
        toasty::watermark::PlaybackWatermarkFrame frame;
        frame.structSize = sizeof(frame);
        frame.drawList = ImGui::GetForegroundDrawList();
        frame.font = wmFont;
        frame.fontSize = wmFont ? wmFont->FontSize : ImGui::GetFontSize();
        frame.displaySize = io.DisplaySize;
        frame.accent = a;
        frame.alpha = menuVisible ? 0.6f * anim.easeOutCubic(anim.openProgress) : 0.6f;
        frame.menuVisible = menuVisible;
        frame.playbackVisible = inPlayback;
        frame.productName = toasty::branding::kProductName;
        frame.versionText = versionText.c_str();
        frame.editionLabel = toasty::branding::kEditionLabel;
        toasty::watermark::drawPlaybackWatermark(frame);
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
    toasty::lang::initialize();

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
    glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
    ImVector<ImWchar> glyphRanges;
    glyphBuilder.BuildRanges(&glyphRanges);

    auto cjkPath = Mod::get()->getResourcesDir() / "cjk.ttf";
    bool hasCjk = std::filesystem::exists(cjkPath);
    auto cjkUtf8 = toasty::pathToUtf8(cjkPath);
    const char* cjkFont = hasCjk ? cjkUtf8.c_str() : nullptr;

    if (cjkFont)
        io.Fonts->TexDesiredWidth = 4096;
    const ImWchar* cjkRanges = cjkFont ? io.Fonts->GetGlyphRangesChineseFull() : nullptr;

    if (bodyFont) {
        fontBody = io.Fonts->AddFontFromFileTTF(bodyFont, 19.0f, nullptr, glyphRanges.Data);
        if (cjkFont) {
            ImFontConfig cjkCfg; cjkCfg.MergeMode = true; cjkCfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(cjkFont, 19.0f, &cjkCfg, cjkRanges);
        }
        fontSmall = io.Fonts->AddFontFromFileTTF(bodyFont, 16.0f, nullptr, glyphRanges.Data);
        if (cjkFont) {
            ImFontConfig cjkCfg; cjkCfg.MergeMode = true; cjkCfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(cjkFont, 16.0f, &cjkCfg, cjkRanges);
        }
    } else {
        fontBody  = io.Fonts->AddFontDefault();
        fontSmall = io.Fonts->AddFontDefault();
    }

    if (headFont) {
        fontHeading = io.Fonts->AddFontFromFileTTF(headFont, 24.0f, nullptr, glyphRanges.Data);
        fontTitle   = io.Fonts->AddFontFromFileTTF(headFont, 32.0f, nullptr, glyphRanges.Data);
    } else {
        fontHeading = io.Fonts->AddFontDefault();
        fontTitle   = io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
    loadSettings();
    theme.applyToImGuiStyle();
    captureReplayDirectoryTimestamp();
    replayListDirty = true;
    replayRefreshQueued = true;

    ensureLogoTexture();
}

void MenuInterface::ensureLogoTexture() {
    auto* mod = Mod::get();
    auto logoPath = toasty::pride::resolveLogoAssetPath(
        prideLogoEnabled,
        {
            mod->getResourcesDir() / "logo-pride.png",
            mod->getPackagePath() / "logo-pride.png",
            mod->getTempDir() / "logo-pride.png"
        },
        {
            mod->getPackagePath() / "logo.png",
            mod->getTempDir() / "logo.png"
        }
    );

    if (logoPath.empty()) {
        return;
    }

    auto pathStr = toasty::pathToUtf8(logoPath);
    auto* texture = CCTextureCache::sharedTextureCache()->addImage(pathStr.c_str(), false);
    if (!texture || texture == logoTexture) {
        return;
    }

    if (logoTexture) {
        logoTexture->release();
    }
    logoTexture = texture;
    logoTexture->retain();
}

$on_mod(Loaded) {
    ImGuiCocos::get().setup([] {
        MenuInterface::get()->initialize();
    }).draw([] {
        MenuInterface::get()->drawInterface();
    });
}

#include <Geode/modify/MenuLayer.hpp>

class $modify(FirstRunMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;

        auto* mod = Mod::get();
        if (!mod->getSavedValue<bool>("first_run_shown", false)) {
            mod->setSavedValue("first_run_shown", true);
            Notification::create("Press B to open ToastyReplay!", NotificationIcon::Info, 4.0f)->show();
        }

        return true;
    }
};
