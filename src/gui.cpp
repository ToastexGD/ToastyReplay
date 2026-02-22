#include "gui.hpp"
#include "ToastyReplay.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/LoadingLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <cmath>
#include <algorithm>

using namespace geode::prelude;

static ImVec4 lerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t
    );
}

ImVec4 ThemeEngine::computeCycleColor(float rate) {
    static float hueVal = 0.0f;
    hueVal += rate * 0.0002f;
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

ImVec4 ThemeEngine::getAccent() {
    return cyclingAccent ? computeCycleColor(cycleRate) : accentColor;
}

ImU32 ThemeEngine::getAccentU32(float alpha) {
    ImVec4 c = getAccent();
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 ThemeEngine::getAccentDimU32(float factor) {
    ImVec4 c = getAccent();
    c.x *= factor; c.y *= factor; c.z *= factor;
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 ThemeEngine::getTextU32() { return ImGui::ColorConvertFloat4ToU32(textPrimary); }
ImU32 ThemeEngine::getTextSecondaryU32() { return ImGui::ColorConvertFloat4ToU32(textSecondary); }
ImU32 ThemeEngine::getCardU32() { return ImGui::ColorConvertFloat4ToU32(cardColor); }

void ThemeEngine::applyToImGuiStyle() {
    ImGuiStyle* s = &ImGui::GetStyle();
    ImVec4 accent = getAccent();

    s->WindowPadding = ImVec2(16, 16);
    s->WindowRounding = cornerRadius;
    s->FramePadding = ImVec2(8, 6);
    s->FrameRounding = 6.0f;
    s->ItemSpacing = ImVec2(10, 6);
    s->ItemInnerSpacing = ImVec2(8, 6);
    s->IndentSpacing = 20.0f;
    s->ScrollbarSize = 8.0f;
    s->ScrollbarRounding = 4.0f;
    s->GrabMinSize = 6.0f;
    s->GrabRounding = 3.0f;
    s->WindowBorderSize = 0.0f;

    ImVec4 winBg = bgColor;
    winBg.w = windowAlpha;

    s->Colors[ImGuiCol_Text] = textPrimary;
    s->Colors[ImGuiCol_TextDisabled] = textSecondary;
    s->Colors[ImGuiCol_WindowBg] = winBg;
    s->Colors[ImGuiCol_PopupBg] = ImVec4(bgColor.x + 0.02f, bgColor.y + 0.02f, bgColor.z + 0.02f, windowAlpha);
    s->Colors[ImGuiCol_Border] = ImVec4(0.2f, 0.2f, 0.22f, 0.3f);
    s->Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    s->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.0f);
    s->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.18f, 1.0f);
    s->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    s->Colors[ImGuiCol_CheckMark] = accent;
    s->Colors[ImGuiCol_SliderGrab] = accent;
    s->Colors[ImGuiCol_SliderGrabActive] = accent;
    s->Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.13f, 0.16f, 1.0f);
    s->Colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x * 0.3f, accent.y * 0.3f, accent.z * 0.3f, 1.0f);
    s->Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x * 0.5f, accent.y * 0.5f, accent.z * 0.5f, 1.0f);
    s->Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.2f);
    s->Colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.3f);
    s->Colors[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.4f);
    s->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.07f, 0.3f);
    s->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3f, 0.3f, 0.35f, 0.4f);
    s->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.4f, 0.4f, 0.45f, 0.6f);
    s->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5f, 0.5f, 0.55f, 0.8f);
    s->Colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    s->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

void ThemeEngine::resetDefaults() {
    accentColor = ImVec4(0.65f, 0.20f, 0.85f, 1.0f);
    bgColor = ImVec4(0.08f, 0.08f, 0.10f, 0.92f);
    cardColor = ImVec4(0.12f, 0.12f, 0.15f, 1.0f);
    textPrimary = ImVec4(0.93f, 0.93f, 0.95f, 1.0f);
    textSecondary = ImVec4(0.55f, 0.55f, 0.60f, 1.0f);
    windowAlpha = 0.92f;
    cornerRadius = 10.0f;
    textScale = 1.0f;
    cyclingAccent = false;
    cycleRate = 1.0f;
    blurStrength = 0.5f;
    cursorGlow = true;
    glowRadius = 120.0f;
}

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

namespace Widgets {

bool ToggleSwitch(const char* label, bool* value, ThemeEngine& theme, AnimationState& anim) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float width = 36.0f;
    const float height = 18.0f;
    const float radius = height * 0.5f;
    float labelWidth = ImGui::CalcTextSize(label).x;

    ImGui::InvisibleButton(label, ImVec2(width + 10.0f + labelWidth, height));
    ImGuiID id = ImGui::GetItemID();
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *value = !*value;

    float& animT = anim.toggleAnims[id];
    float target = *value ? 1.0f : 0.0f;
    animT += (target - animT) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);

    ImVec4 trackCol = lerpColor(ImVec4(0.25f, 0.25f, 0.28f, 1.0f), theme.getAccent(), animT);
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
        ImGui::ColorConvertFloat4ToU32(trackCol), radius);

    float knobX = pos.x + radius + animT * (width - height);
    dl->AddCircleFilled(ImVec2(knobX, pos.y + radius), radius - 2.0f, IM_COL32(255, 255, 255, 240));

    dl->AddText(ImVec2(pos.x + width + 10, pos.y + 1), theme.getTextU32(), label);

    return clicked;
}

bool StyledButton(const char* label, ImVec2 size, ThemeEngine& theme, AnimationState& anim) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = 28;

    ImGui::InvisibleButton(label, size);
    ImGuiID id = ImGui::GetItemID();
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();

    float& hoverT = anim.hoverAnims[id];
    hoverT += ((hovered ? 1.0f : 0.0f) - hoverT) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);

    ImVec4 accent = theme.getAccent();
    ImVec4 btnColor = lerpColor(
        ImVec4(0.13f, 0.13f, 0.16f, 1.0f),
        ImVec4(accent.x * 0.25f, accent.y * 0.25f, accent.z * 0.25f, 1.0f),
        hoverT
    );
    if (held) { btnColor.x += 0.05f; btnColor.y += 0.05f; btnColor.z += 0.05f; }

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
        ImGui::ColorConvertFloat4ToU32(btnColor), 6.0f);

    if (hoverT > 0.01f) {
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
            theme.getAccentU32(0.3f * hoverT), 6.0f, 0, 1.0f);
    }

    const char* displayEnd = label;
    while (*displayEnd) {
        if (displayEnd[0] == '#' && displayEnd[1] == '#') break;
        displayEnd++;
    }
    ImVec2 textSize = ImGui::CalcTextSize(label, displayEnd);
    dl->AddText(
        ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f),
        theme.getTextU32(), label, displayEnd
    );

    return clicked;
}

bool StyledSliderFloat(const char* label, float* value, float vmin, float vmax, ThemeEngine& theme) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float availWidth = ImGui::GetContentRegionAvail().x;

    char valBuf[64];
    snprintf(valBuf, sizeof(valBuf), "%.2f", *value);
    dl->AddText(pos, theme.getTextSecondaryU32(), label);
    ImVec2 valSize = ImGui::CalcTextSize(valBuf);
    dl->AddText(ImVec2(pos.x + availWidth - valSize.x, pos.y), theme.getTextU32(), valBuf);
    ImGui::Dummy(ImVec2(0, 20));

    pos = ImGui::GetCursorScreenPos();
    const float trackH = 4.0f;
    const float knobR = 7.0f;
    float trackY = pos.y + knobR;

    dl->AddRectFilled(
        ImVec2(pos.x, trackY - trackH / 2), ImVec2(pos.x + availWidth, trackY + trackH / 2),
        IM_COL32(40, 40, 45, 255), trackH / 2
    );

    float frac = (*value - vmin) / (vmax - vmin);
    float fillX = pos.x + frac * availWidth;
    dl->AddRectFilled(
        ImVec2(pos.x, trackY - trackH / 2), ImVec2(fillX, trackY + trackH / 2),
        theme.getAccentU32(), trackH / 2
    );

    for (int i = 2; i >= 0; i--) {
        dl->AddCircleFilled(ImVec2(fillX, trackY), knobR + i * 3.0f, theme.getAccentU32(0.06f * (3 - i)));
    }

    dl->AddCircleFilled(ImVec2(fillX, trackY), knobR, IM_COL32(255, 255, 255, 240));
    dl->AddCircle(ImVec2(fillX, trackY), knobR, theme.getAccentU32(0.5f), 0, 1.5f);

    ImGui::InvisibleButton(label, ImVec2(availWidth, knobR * 2 + 4));
    if (ImGui::IsItemActive()) {
        float mouseX = ImGui::GetIO().MousePos.x;
        *value = vmin + std::clamp((mouseX - pos.x) / availWidth, 0.0f, 1.0f) * (vmax - vmin);
        return true;
    }
    return false;
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

    dl->AddText(pos, theme.getAccentU32(), text);
    float textH = ImGui::CalcTextSize(text).y;
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + textH + 3), ImVec2(pos.x + width, pos.y + textH + 4),
        theme.getAccentU32(0.3f)
    );

    ImGui::Dummy(ImVec2(0, textH + 10));
}

bool ModuleCard(const char* name, const char* description, bool* enabled,
                ThemeEngine& theme, AnimationState& anim) {
    ImGuiID id = ImGui::GetID(name);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    float height = description ? 50.0f : 36.0f;

    bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + width, pos.y + height));
    float& hoverT = anim.hoverAnims[id];
    hoverT += ((hovered ? 1.0f : 0.0f) - hoverT) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);

    ImVec4 cardBase = theme.cardColor;
    cardBase.x += hoverT * 0.03f;
    cardBase.y += hoverT * 0.03f;
    cardBase.z += hoverT * 0.03f;
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
        ImGui::ColorConvertFloat4ToU32(cardBase), 6.0f);

    if (*enabled) {
        dl->AddRectFilled(pos, ImVec2(pos.x + 3, pos.y + height), theme.getAccentU32(), 3.0f);
    }

    ImU32 nameCol = *enabled ? theme.getAccentU32() : theme.getTextU32();
    dl->AddText(ImVec2(pos.x + 14, pos.y + (description ? 8 : 10)), nameCol, name);

    if (description) {
        dl->AddText(ImVec2(pos.x + 14, pos.y + 28), theme.getTextSecondaryU32(), description);
    }

    float toggleW = 36.0f, toggleH = 18.0f;
    float toggleX = pos.x + width - toggleW - 12;
    float toggleY = pos.y + (height - toggleH) / 2;
    float toggleR = toggleH * 0.5f;

    float& toggleT = anim.toggleAnims[id];
    toggleT += ((*enabled ? 1.0f : 0.0f) - toggleT) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);

    ImVec4 trackCol = lerpColor(ImVec4(0.25f, 0.25f, 0.28f, 1.0f), theme.getAccent(), toggleT);
    dl->AddRectFilled(
        ImVec2(toggleX, toggleY), ImVec2(toggleX + toggleW, toggleY + toggleH),
        ImGui::ColorConvertFloat4ToU32(trackCol), toggleR
    );

    float knobX = toggleX + toggleR + toggleT * (toggleW - toggleH);
    dl->AddCircleFilled(ImVec2(knobX, toggleY + toggleR), toggleR - 2.0f, IM_COL32(255, 255, 255, 240));

    ImGui::InvisibleButton(name, ImVec2(width, height));
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *enabled = !*enabled;

    ImGui::Dummy(ImVec2(0, 4));
    return clicked;
}

static const void* activeModuleKey = nullptr;

bool ModuleCardBegin(const char* name, const char* description, bool* enabled,
                     ThemeEngine& theme, AnimationState& anim) {
    bool clicked = ModuleCard(name, description, enabled, theme, anim);

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
    float childH = data.height > 0.0f ? data.height * t : 300.0f;
    ImGui::BeginChild(childId, ImVec2(-1, childH), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Indent(14);
    ImGui::Dummy(ImVec2(0, 2));
    return true;
}

void ModuleCardEnd() {
    ImGui::Dummy(ImVec2(0, 2));
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
    ImVec2 textSize = ImGui::CalcTextSize(text);
    float padX = 8, padY = 4;

    ImVec4 bgCol(color.x * 0.2f, color.y * 0.2f, color.z * 0.2f, 0.8f);
    dl->AddRectFilled(pos, ImVec2(pos.x + textSize.x + padX * 2, pos.y + textSize.y + padY * 2),
        ImGui::ColorConvertFloat4ToU32(bgCol), 4.0f);
    dl->AddRect(pos, ImVec2(pos.x + textSize.x + padX * 2, pos.y + textSize.y + padY * 2),
        ImGui::ColorConvertFloat4ToU32(color), 4.0f, 0, 1.0f);
    dl->AddText(ImVec2(pos.x + padX, pos.y + padY), ImGui::ColorConvertFloat4ToU32(color), text);

    ImGui::Dummy(ImVec2(textSize.x + padX * 2, textSize.y + padY * 2));
}

bool PillButton(const char* label, bool active, float width, ThemeEngine& theme, AnimationState& anim) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float height = 28.0f;

    ImGui::InvisibleButton(label, ImVec2(width, height));
    ImGuiID id = ImGui::GetItemID();
    bool clicked = ImGui::IsItemClicked();
    bool hovered = ImGui::IsItemHovered();

    float& hoverT = anim.hoverAnims[id];
    hoverT += ((hovered ? 1.0f : 0.0f) - hoverT) * std::min(1.0f, ImGui::GetIO().DeltaTime * 12.0f);

    if (active) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
            theme.getAccentU32(0.85f), 14.0f);
    } else {
        ImVec4 base(0.15f, 0.15f, 0.18f, 1.0f);
        base.x += hoverT * 0.05f; base.y += hoverT * 0.05f; base.z += hoverT * 0.05f;
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
            ImGui::ColorConvertFloat4ToU32(base), 14.0f);
    }

    ImVec2 textSize = ImGui::CalcTextSize(label);
    ImVec2 textPos(pos.x + (width - textSize.x) * 0.5f, pos.y + (height - textSize.y) * 0.5f);
    dl->AddText(textPos, active ? IM_COL32(255, 255, 255, 255) : theme.getTextSecondaryU32(), label);

    return clicked;
}

void KeybindButton(const char* label, int* keyCode, ThemeEngine& theme, AnimationState& anim) {
    MenuInterface* ui = MenuInterface::get();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float width = ImGui::GetContentRegionAvail().x;
    float height = 32.0f;

    bool isRebinding = (ui->rebindTarget == keyCode);
    std::string keyText = isRebinding ? "..." : getKeyName(*keyCode);

    dl->AddText(ImVec2(pos.x + 8, pos.y + (height - ImGui::CalcTextSize(label).y) / 2),
        theme.getTextU32(), label);

    ImVec2 txtSize = ImGui::CalcTextSize(keyText.c_str());
    float btnW = std::max(txtSize.x + 20.0f, 50.0f);
    float btnH = 24.0f;
    float btnX = pos.x + width - btnW - 8;
    float btnY = pos.y + (height - btnH) / 2;

    ImU32 btnCol = isRebinding ? theme.getAccentU32(0.3f) : IM_COL32(25, 25, 30, 255);
    dl->AddRectFilled(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH), btnCol, 4.0f);
    dl->AddRect(ImVec2(btnX, btnY), ImVec2(btnX + btnW, btnY + btnH),
        isRebinding ? theme.getAccentU32(0.6f) : IM_COL32(50, 50, 55, 255), 4.0f, 0, 1.0f);
    dl->AddText(ImVec2(btnX + (btnW - txtSize.x) / 2, btnY + (btnH - txtSize.y) / 2),
        isRebinding ? theme.getAccentU32() : theme.getTextU32(), keyText.c_str());

    ImGui::InvisibleButton(label, ImVec2(width, height));
    if (ImGui::IsItemClicked(0)) {
        if (isRebinding)
            ui->rebindTarget = nullptr;
        else
            ui->rebindTarget = keyCode;
    }
    if (ImGui::IsItemClicked(1)) {
        *keyCode = 0;
        if (isRebinding)
            ui->rebindTarget = nullptr;
    }
}

}

void MenuInterface::switchTab(int newTab) {
    if (newTab == activeTab) return;
    previousTab = activeTab;
    activeTab = newTab;
    anim.tabTransition = 0.0f;
}

void MenuInterface::drawBackdrop() {
    if (anim.openProgress <= 0.0f) return;

    float t = anim.easeOutCubic(anim.openProgress);
    ImDrawList* bgDraw = ImGui::GetBackgroundDrawList();
    ImVec2 ds = ImGui::GetIO().DisplaySize;

    float blur = theme.blurStrength;
    if (blur > 0.0f) {
        int layers = 4 + (int)(blur * 8.0f);
        float baseAlpha = blur * 0.18f;

        ImVec4 bg = theme.bgColor;
        int br = (int)(bg.x * 255), bg_ = (int)(bg.y * 255), bb = (int)(bg.z * 255);

        for (int i = layers; i >= 1; i--) {
            float layerAlpha = baseAlpha * t * ((float)i / (float)layers);
            bgDraw->AddRectFilled(ImVec2(0, 0), ds,
                IM_COL32(br, bg_, bb, (int)(layerAlpha * 255)));
        }

        float overlayAlpha = (0.3f + blur * 0.4f) * t;
        bgDraw->AddRectFilled(ImVec2(0, 0), ds,
            IM_COL32(br, bg_, bb, (int)(overlayAlpha * 255)));
    } else {
        bgDraw->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(100 * t)));
    }

    float vigSize = 250.0f;
    ImU32 vigCol = IM_COL32(0, 0, 0, (int)(50 * t));
    ImU32 trans = IM_COL32(0, 0, 0, 0);
    bgDraw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, vigSize), vigCol, vigCol, trans, trans);
    bgDraw->AddRectFilledMultiColor(ImVec2(0, ds.y - vigSize), ds, trans, trans, vigCol, vigCol);
}

void MenuInterface::drawCursorGlow() {
}

void MenuInterface::drawTitleBar() {
    if (fontTitle) ImGui::PushFont(fontTitle);
    ImGui::TextColored(theme.getAccent(), "ToastyReplay");
    ImGui::SameLine();
    if (fontTitle) ImGui::PopFont();

    if (fontSmall) ImGui::PushFont(fontSmall);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::TextColored(ImVec4(theme.textSecondary), "v" MOD_VERSION);
    if (fontSmall) ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 4));
}

void MenuInterface::drawTabBar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    float windowWidth = ImGui::GetWindowWidth();
    float padX = ImGui::GetStyle().WindowPadding.x;

    const char* tabNames[] = { "Replay", "Tools", "Hacks", "Settings" };
    const int tabCount = 4;
    float contentWidth = windowWidth - padX * 2;
    float tabWidth = contentWidth / tabCount;
    float tabHeight = 32.0f;
    float tabY = ImGui::GetCursorScreenPos().y;

    dl->AddRectFilled(
        ImVec2(windowPos.x + padX, tabY),
        ImVec2(windowPos.x + windowWidth - padX, tabY + tabHeight),
        IM_COL32(15, 15, 18, 255), 6.0f
    );

    if (fontBody) ImGui::PushFont(fontBody);

    for (int i = 0; i < tabCount; i++) {
        ImVec2 tabMin(windowPos.x + padX + i * tabWidth, tabY);
        ImVec2 tabMax(tabMin.x + tabWidth, tabY + tabHeight);

        bool hovered = ImGui::IsMouseHoveringRect(tabMin, tabMax);
        bool isActive = (activeTab == i);

        if (isActive) {
            dl->AddRectFilled(tabMin, tabMax, IM_COL32(25, 25, 30, 255),
                i == 0 ? 6.0f : (i == tabCount - 1 ? 6.0f : 0.0f));
        } else if (hovered) {
            dl->AddRectFilled(tabMin, tabMax, IM_COL32(20, 20, 24, 255));
        }

        if (isActive) {
            dl->AddRectFilled(
                ImVec2(tabMin.x + 8, tabMax.y - 2),
                ImVec2(tabMax.x - 8, tabMax.y),
                theme.getAccentU32(), 1.0f
            );
        }

        ImVec2 textSize = ImGui::CalcTextSize(tabNames[i]);
        ImVec2 textPos(tabMin.x + (tabWidth - textSize.x) * 0.5f, tabMin.y + (tabHeight - textSize.y) * 0.5f);
        dl->AddText(textPos, isActive ? theme.getAccentU32() : theme.getTextSecondaryU32(), tabNames[i]);

        if (hovered && ImGui::IsMouseClicked(0))
            switchTab(i);
    }

    if (fontBody) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, tabHeight + 8));
}

void MenuInterface::drawTabContent() {
    float t = anim.easeOutCubic(anim.tabTransition);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.0f - t) * 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);

    if (fontBody) ImGui::PushFont(fontBody);

    switch (activeTab) {
        case 0: drawReplayTab(); break;
        case 1: drawToolsTab(); break;
        case 2: drawHacksTab(); break;
        case 3: drawSettingsTab(); break;
    }

    if (fontBody) ImGui::PopFont();
    ImGui::PopStyleVar();
}

void MenuInterface::drawStatusBar() {
    ReplayEngine* engine = ReplayEngine::get();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    float padX = ImGui::GetStyle().WindowPadding.x;
    float barH = 24.0f;
    float barY = windowPos.y + windowSize.y - barH - 8;

    dl->AddRectFilled(
        ImVec2(windowPos.x + padX, barY),
        ImVec2(windowPos.x + windowSize.x - padX, barY + barH),
        IM_COL32(12, 12, 15, 255), 4.0f
    );

    if (fontSmall) ImGui::PushFont(fontSmall);

    char statusBuf[256];
    int frame = PlayLayer::get() ? PlayLayer::get()->m_gameState.m_currentProgress : 0;
    snprintf(statusBuf, sizeof(statusBuf), "TPS: %.0f    Speed: %.2fx    Frame: %d",
        engine->tickRate, engine->gameSpeed, frame);

    ImVec2 textSize = ImGui::CalcTextSize(statusBuf);
    float textY = barY + (barH - textSize.y) / 2;
    dl->AddText(
        ImVec2(windowPos.x + padX + 10, textY),
        theme.getTextSecondaryU32(), statusBuf
    );

    if (engine->cbfRecordingEnabled) {
        float cbfX = windowPos.x + padX + 10 + textSize.x + 16;
        dl->AddText(ImVec2(cbfX, textY), IM_COL32(255, 50, 50, 255), "CBF ON");
    }

    if (fontSmall) ImGui::PopFont();
}

void MenuInterface::drawReplayTab() {
    ReplayEngine* engine = ReplayEngine::get();

    Widgets::SectionHeader("Mode", theme);

    float pillW = (ImGui::GetContentRegionAvail().x - 20) / 3.0f;

    if (Widgets::PillButton("Disable", engine->engineMode == MODE_DISABLED, pillW, theme, anim)) {
        if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
            delete engine->activeMacro;
            engine->activeMacro = nullptr;
        }
        engine->engineMode = MODE_DISABLED;
    }
    ImGui::SameLine(0, 10);
    if (Widgets::PillButton("Record", engine->engineMode == MODE_CAPTURE, pillW, theme, anim)) {
        if (PlayLayer::get())
            engine->beginCapture(PlayLayer::get()->m_level);
        else
            engine->engineMode = MODE_CAPTURE;
    }
    ImGui::SameLine(0, 10);
    if (Widgets::PillButton("Playback", engine->engineMode == MODE_EXECUTE, pillW, theme, anim)) {
        if (engine->engineMode == MODE_EXECUTE) {
            engine->haltExecution();
        } else if (engine->activeMacro) {
            engine->beginExecution();
            anim.closing = true;
            anim.opening = false;
        }
    }

    ImGui::Dummy(ImVec2(0, 8));

    if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
        Widgets::StatusBadge("RECORDING", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::SameLine();
        ImGui::Text("Inputs: %zu", engine->activeMacro->inputs.size());
        ImGui::Dummy(ImVec2(0, 4));

        if (!macroNameReady) {
            strncpy(macroNameBuffer, engine->activeMacro->name.c_str(), sizeof(macroNameBuffer) - 1);
            macroNameBuffer[sizeof(macroNameBuffer) - 1] = '\0';
            macroNameReady = true;
        }

        ImGui::Text("Macro Name:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##recordingName", macroNameBuffer, sizeof(macroNameBuffer)))
            engine->activeMacro->name = macroNameBuffer;

        ImGui::Dummy(ImVec2(0, 4));
        float btnW = (ImGui::GetContentRegionAvail().x - 10) / 2.0f;

        if (Widgets::StyledButton("Save Macro", ImVec2(btnW, 30), theme, anim)) {
            if (!engine->activeMacro->inputs.empty()) {
                int accMode = engine->positionCorrection ? 2 : (engine->inputCorrection ? 1 : 0);
                engine->activeMacro->persist(accMode, engine->correctionInterval);
                engine->reloadMacroList();
            }
        }
        ImGui::SameLine(0, 10);
        if (Widgets::StyledButton("Stop", ImVec2(btnW, 30), theme, anim)) {
            delete engine->activeMacro;
            engine->activeMacro = nullptr;
            engine->engineMode = MODE_DISABLED;
            macroNameReady = false;
        }
        ImGui::Dummy(ImVec2(0, 4));
    } else {
        macroNameReady = false;
    }

    if (engine->engineMode == MODE_EXECUTE && engine->activeMacro) {
        Widgets::StatusBadge("PLAYING", ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::SameLine();
        ImGui::Text("%s | Inputs: %zu", engine->activeMacro->name.c_str(), engine->activeMacro->inputs.size());
        ImGui::Dummy(ImVec2(0, 4));

        if (Widgets::StyledButton("Stop Playback", ImVec2(-1, 30), theme, anim))
            engine->engineMode = MODE_DISABLED;

        ImGui::Dummy(ImVec2(0, 4));
    }

    Widgets::SectionHeader("Saved Replays", theme);

    std::string currentMacroName = (engine->activeMacro && engine->engineMode != MODE_CAPTURE)
        ? engine->activeMacro->name : "Select a replay...";

    float listH = std::max(80.0f, std::min(200.0f, (float)engine->storedMacros.size() * 28.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 20, 25, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
    ImGui::BeginChild("##MacroList", ImVec2(-1, listH), true);

    if (engine->storedMacros.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
        ImGui::Text("No saved replays");
        ImGui::PopStyleColor();
    }

    auto macroListCopy = engine->storedMacros;
    for (const auto& macroName : macroListCopy) {
        bool isSelected = (engine->activeMacro && engine->engineMode != MODE_CAPTURE && engine->activeMacro->name == macroName);
        bool isIncompatible = engine->incompatibleMacros.count(macroName) > 0;
        bool isCBF = engine->cbfMacros.count(macroName) > 0;
        ImGui::PushID(macroName.c_str());

        float xBtnW = 20.0f;
        float cbfLabelW = isCBF ? ImGui::CalcTextSize("CBF").x + 8.0f : 0.0f;
        float incompatLabelW = isIncompatible ? ImGui::CalcTextSize("Incompatible").x + 8.0f : 0.0f;
        float selectableW = ImGui::GetContentRegionAvail().x - xBtnW - 8.0f - cbfLabelW - incompatLabelW;

        if (isIncompatible) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
            ImGui::Selectable(macroName.c_str(), false, ImGuiSelectableFlags_Disabled, ImVec2(selectableW, 0));
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Selectable(macroName.c_str(), isSelected, 0, ImVec2(selectableW, 0))) {
                if (engine->engineMode != MODE_CAPTURE) {
                    MacroSequence* loaded = MacroSequence::loadFromDisk(macroName);
                    if (loaded) {
                        if (engine->activeMacro) delete engine->activeMacro;
                        engine->activeMacro = loaded;
                        engine->cbfMacroLoaded = loaded->cbfEnabled;
                    }
                }
            }
        }

        float itemMinY = ImGui::GetItemRectMin().y;
        float itemMaxY = ImGui::GetItemRectMax().y;
        float itemH = itemMaxY - itemMinY;

        if (isIncompatible) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, itemMinY + (itemH - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::TextUnformatted("Incompatible");
            ImGui::PopStyleColor();
        } else if (isCBF) {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, itemMinY + (itemH - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::TextUnformatted("CBF");
            ImGui::PopStyleColor();
        }

        float listRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(listRight - xBtnW, itemMinY + (itemH - xBtnW) * 0.5f));
        if (ImGui::Button("X", ImVec2(xBtnW, xBtnW))) {
            auto dir = Mod::get()->getSaveDir() / "replays";
            bool deleted = false;
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && entry.path().stem().string() == macroName) {
                    std::filesystem::remove(entry.path());
                    deleted = true;
                    break;
                }
            }
            if (deleted) engine->reloadMacroList();
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 4));
    float btnW = (ImGui::GetContentRegionAvail().x - 10) / 2.0f;

    if (Widgets::StyledButton("Refresh", ImVec2(btnW, 28), theme, anim))
        engine->reloadMacroList();

    ImGui::SameLine(0, 10);
    if (Widgets::StyledButton("Open Folder", ImVec2(btnW, 28), theme, anim)) {
        auto dir = Mod::get()->getSaveDir() / "replays";
        if (std::filesystem::exists(dir) || std::filesystem::create_directory(dir))
            utils::file::openFolder(dir);
    }

    if (engine->activeMacro && engine->engineMode == MODE_EXECUTE && PlayLayer::get()) {
        ImGui::Dummy(ImVec2(0, 4));
        Widgets::SectionHeader("Loaded Macro", theme);
        ImGui::Text("Name: %s", engine->activeMacro->name.c_str());
        if (engine->activeMacro->cbfEnabled) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::TextUnformatted("(CBF)");
            ImGui::PopStyleColor();
        }
        ImGui::Text("Inputs: %zu", engine->activeMacro->inputs.size());

        if (engine->engineMode == MODE_EXECUTE) {
            ImGui::Dummy(ImVec2(0, 4));
            Widgets::ToggleSwitch("Ignore Manual Input", &engine->userInputIgnored, theme, anim);
        }
    }

    if (engine->activeMacro && engine->engineMode == MODE_CAPTURE) {
        ImGui::Dummy(ImVec2(0, 8));
        Widgets::SectionHeader("Accuracy", theme);

        const char* correctionModes[] = { "None", "Input Adjustments", "Frame Replacement" };
        int currentMode = engine->positionCorrection ? 2 : (engine->inputCorrection ? 1 : 0);

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##AccuracyMode", correctionModes[currentMode])) {
            if (ImGui::Selectable("None", currentMode == 0)) {
                engine->positionCorrection = false;
                engine->inputCorrection = false;
            }
            if (ImGui::Selectable("Input Adjustments", currentMode == 1)) {
                engine->positionCorrection = false;
                engine->inputCorrection = true;
            }
            if (ImGui::Selectable("Frame Replacement", currentMode == 2)) {
                engine->positionCorrection = true;
                engine->inputCorrection = false;
            }
            ImGui::EndCombo();
        }

        if (engine->positionCorrection) {
            ImGui::Dummy(ImVec2(0, 4));
            Widgets::StyledSliderInt("Replacement Rate", &engine->correctionInterval, 30, 240, theme);
        }
    }

    if (engine->engineMode == MODE_DISABLED) {
        ImGui::Dummy(ImVec2(0, 8));
        bool prevCBF = engine->cbfRecordingEnabled;
        Widgets::ToggleSwitch("CBF Recording", &engine->cbfRecordingEnabled, theme, anim);
        if (engine->cbfRecordingEnabled != prevCBF) {
            GameManager::get()->setGameVariable("0177", engine->cbfRecordingEnabled);
        }
    }
}

void MenuInterface::drawToolsTab() {
    ReplayEngine* engine = ReplayEngine::get();

    Widgets::SectionHeader("TPS Control", theme);
    ImGui::TextColored(theme.getAccent(), "Current: %.0f TPS", engine->tickRate);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
    ImGui::InputFloat("##tps", &tempTickRate, 0, 0, "%.0f");
    ImGui::SameLine(0, 6);
    if (Widgets::StyledButton("Apply###tps", ImVec2(78, 28), theme, anim)) {
        if (engine->engineMode == MODE_DISABLED || !PlayLayer::get())
            engine->tickRate = tempTickRate;
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Speed Control", theme);
    ImGui::TextColored(theme.getAccent(), "Current: %.2fx", engine->gameSpeed);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
    ImGui::InputFloat("##speed", &tempGameSpeed, 0, 0, "%.2f");
    ImGui::SameLine(0, 6);
    if (Widgets::StyledButton("Apply###spd", ImVec2(78, 28), theme, anim))
        engine->gameSpeed = tempGameSpeed;

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Features", theme);

    Widgets::ModuleCard("Frame Advance", "Pause and step frame-by-frame",
        &engine->tickStepping, theme, anim);

    Widgets::ModuleCard("Speedhack Audio", "Apply speed changes to game audio",
        &engine->audioPitchEnabled, theme, anim);
}

void MenuInterface::drawHacksTab() {
    ReplayEngine* engine = ReplayEngine::get();

    Widgets::ModuleCard("Safe Mode", "Prevents stats and percentage gain",
        &engine->protectedMode, theme, anim);

    if (Widgets::ModuleCardBegin("Show Trajectory", "Display predicted player path",
        &engine->pathPreview, theme, anim)) {
        Widgets::StyledSliderInt("Trajectory Length", &engine->pathLength, 50, 480, theme);
        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Show Hitboxes", "Display collision bounds for objects",
        &engine->showHitboxes, theme, anim)) {
        Widgets::ToggleSwitch("On Death Only", &engine->hitboxOnDeath, theme, anim);
        Widgets::ToggleSwitch("Draw Trail", &engine->hitboxTrail, theme, anim);

        if (engine->hitboxTrail)
            Widgets::StyledSliderInt("Trail Length", &engine->hitboxTrailLength, 10, 600, theme);

        Widgets::ModuleCardEnd();
    }

    if (Widgets::ModuleCardBegin("Noclip", "Disable collision with obstacles",
        &engine->collisionBypass, theme, anim)) {
        float hitRate = 100.0f;
        if (engine->totalTickCount > 0)
            hitRate = 100.0f * (1.0f - (float)engine->bypassedCollisions / (float)engine->totalTickCount);
        if (hitRate < 0.0f) hitRate = 0.0f;

        ImVec4 hitColor;
        if (hitRate >= 90.0f) hitColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        else if (hitRate >= 70.0f) hitColor = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
        else hitColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::Text("Accuracy: ");
        ImGui::SameLine();
        ImGui::TextColored(hitColor, "%.2f%%", hitRate);
        ImGui::Text("Deaths: %d | Frames: %d", engine->bypassedCollisions, engine->totalTickCount);

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
        &engine->rngLocked, theme, anim)) {
        if (!rngBufferInit) {
            snprintf(rngBuffer, sizeof(rngBuffer), "%u", engine->rngSeedVal);
            rngBufferInit = true;
        }

        ImGui::Text("Seed Value:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##seedValue", rngBuffer, sizeof(rngBuffer), ImGuiInputTextFlags_CharsDecimal)) {
            try {
                unsigned long long parsed = std::stoull(rngBuffer);
                engine->rngSeedVal = static_cast<unsigned int>(parsed);
            } catch (...) {
                engine->rngSeedVal = 1;
            }
        }
        Widgets::ModuleCardEnd();
    }
}

void MenuInterface::drawSettingsTab() {
    Widgets::SectionHeader("Accent Color", theme);

    if (!theme.cyclingAccent) {
        ImGui::ColorEdit4("##accentColor", (float*)&theme.accentColor,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
    }
    Widgets::ToggleSwitch("RGB Cycling", &theme.cyclingAccent, theme, anim);

    if (theme.cyclingAccent) {
        ImGui::Dummy(ImVec2(0, 4));
        Widgets::StyledSliderFloat("Cycle Speed", &theme.cycleRate, 0.1f, 5.0f, theme);
    }

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Colors", theme);

    ImGui::Text("Background");
    ImGui::ColorEdit4("##bgColor", (float*)&theme.bgColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Text("Card Color");
    ImGui::ColorEdit4("##cardColor", (float*)&theme.cardColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Text("Text Color");
    ImGui::ColorEdit4("##textColor", (float*)&theme.textPrimary,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    ImGui::Dummy(ImVec2(0, 8));
    Widgets::SectionHeader("Appearance", theme);

    Widgets::StyledSliderFloat("Menu Opacity", &theme.windowAlpha, 0.1f, 1.0f, theme);
    ImGui::Dummy(ImVec2(0, 8));
    Widgets::StyledSliderFloat("Backdrop Blur", &theme.blurStrength, 0.0f, 1.0f, theme);
    ImGui::Dummy(ImVec2(0, 8));
    Widgets::ToggleSwitch("Cursor Glow", &theme.cursorGlow, theme, anim);
    if (theme.cursorGlow) {
        ImGui::Dummy(ImVec2(0, 4));
        Widgets::StyledSliderFloat("Glow Size", &theme.glowRadius, 40.0f, 250.0f, theme);
    }
    ImGui::Dummy(ImVec2(0, 8));
    Widgets::StyledSliderFloat("Corner Radius", &theme.cornerRadius, 0.0f, 20.0f, theme);
    ImGui::Dummy(ImVec2(0, 8));
    Widgets::StyledSliderFloat("Animation Speed", &anim.animSpeed, 2.0f, 20.0f, theme);

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Text("Open Animation");
    const char* animDirNames[] = { "Center", "From Left", "From Right", "From Top", "From Bottom" };
    int currentDir = (int)anim.openDirection;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##animDir", &currentDir, animDirNames, 5))
        anim.openDirection = (AnimDirection)currentDir;

    ImGui::Dummy(ImVec2(0, 12));
    Widgets::SectionHeader("Keybinds", theme);

    if (fontSmall) ImGui::PushFont(fontSmall);
    ImGui::TextColored(theme.textSecondary, "Click to rebind, right-click to clear");
    if (fontSmall) ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4));

    Widgets::KeybindButton("Menu Toggle", &keybinds.menu, theme, anim);
    Widgets::KeybindButton("Frame Advance", &keybinds.frameAdvance, theme, anim);
    Widgets::KeybindButton("Frame Step", &keybinds.frameStep, theme, anim);
    Widgets::KeybindButton("Replay Toggle", &keybinds.replayToggle, theme, anim);
    Widgets::KeybindButton("Noclip", &keybinds.noclip, theme, anim);
    Widgets::KeybindButton("Safe Mode", &keybinds.safeMode, theme, anim);
    Widgets::KeybindButton("Trajectory", &keybinds.trajectory, theme, anim);
    Widgets::KeybindButton("Audio Pitch", &keybinds.audioPitch, theme, anim);
    Widgets::KeybindButton("RNG Lock", &keybinds.rngLock, theme, anim);
    Widgets::KeybindButton("Hitboxes", &keybinds.hitboxes, theme, anim);

    ImGui::Dummy(ImVec2(0, 12));
    if (Widgets::StyledButton("Reset to Defaults", ImVec2(-1, 32), theme, anim)) {
        theme.resetDefaults();
        anim.animSpeed = 8.0f;
        anim.openDirection = ANIM_CENTER;
        keybinds = KeybindSet();
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

void MenuInterface::saveSettings() {
    auto* mod = Mod::get();
    auto* eng = ReplayEngine::get();

    saveColor("theme_accent", theme.accentColor);
    saveColor("theme_bg", theme.bgColor);
    saveColor("theme_card", theme.cardColor);
    saveColor("theme_text", theme.textPrimary);
    saveColor("theme_text2", theme.textSecondary);
    mod->setSavedValue("theme_opacity", theme.windowAlpha);
    mod->setSavedValue("theme_radius", theme.cornerRadius);
    mod->setSavedValue("theme_cycling", theme.cyclingAccent);
    mod->setSavedValue("theme_cycle_rate", theme.cycleRate);
    mod->setSavedValue("theme_blur", theme.blurStrength);
    mod->setSavedValue("theme_cursor_glow", theme.cursorGlow);
    mod->setSavedValue("theme_glow_radius", theme.glowRadius);

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
    mod->setSavedValue("eng_tick_rate", (float)eng->tickRate);
    mod->setSavedValue("eng_speed", (float)eng->gameSpeed);
}

void MenuInterface::loadSettings() {
    auto* mod = Mod::get();
    auto* eng = ReplayEngine::get();

    theme.accentColor = loadColor("theme_accent", ImVec4(0.65f, 0.20f, 0.85f, 1.0f));
    theme.bgColor = loadColor("theme_bg", ImVec4(0.08f, 0.08f, 0.10f, 0.92f));
    theme.cardColor = loadColor("theme_card", ImVec4(0.12f, 0.12f, 0.15f, 1.0f));
    theme.textPrimary = loadColor("theme_text", ImVec4(0.93f, 0.93f, 0.95f, 1.0f));
    theme.textSecondary = loadColor("theme_text2", ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    theme.windowAlpha = mod->getSavedValue<float>("theme_opacity", 0.92f);
    theme.cornerRadius = mod->getSavedValue<float>("theme_radius", 10.0f);
    theme.cyclingAccent = mod->getSavedValue<bool>("theme_cycling", false);
    theme.cycleRate = mod->getSavedValue<float>("theme_cycle_rate", 1.0f);
    theme.blurStrength = mod->getSavedValue<float>("theme_blur", 0.5f);
    theme.cursorGlow = mod->getSavedValue<bool>("theme_cursor_glow", true);
    theme.glowRadius = mod->getSavedValue<float>("theme_glow_radius", 120.0f);

    anim.animSpeed = mod->getSavedValue<float>("anim_speed", 8.0f);
    anim.openDirection = (AnimDirection)mod->getSavedValue<int>("anim_direction", ANIM_CENTER);

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
    eng->tickRate = mod->getSavedValue<float>("eng_tick_rate", 240.f);
    eng->gameSpeed = mod->getSavedValue<float>("eng_speed", 1.0f);

    tempTickRate = (float)eng->tickRate;
    tempGameSpeed = (float)eng->gameSpeed;
}

void MenuInterface::drawMainWindow() {
    float t = anim.easeOutCubic(anim.openProgress);
    ImGuiIO& io = ImGui::GetIO();

    float winW = 580.0f, winH = 540.0f;
    bool animating = anim.opening || anim.closing || anim.openProgress < 1.0f;

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
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.cornerRadius);

    ImGui::Begin("##ToastyReplay", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (!animating) {
        windowPos = ImGui::GetWindowPos();
    }

    drawCursorGlow();

    drawTitleBar();
    drawTabBar();

    float statusBarH = 36.0f;
    float remainH = ImGui::GetContentRegionAvail().y - statusBarH;
    ImGui::BeginChild("##TabContent", ImVec2(0, remainH), false);
    drawTabContent();
    ImGui::EndChild();

    drawStatusBar();

    ImGui::End();
    ImGui::PopStyleVar(2);
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
        saveSettings();
        if (previouslyShown && engine) engine->reloadMacroList();
        previouslyShown = false;
        auto pl = PlayLayer::get();
        if (pl && !pl->m_isPaused)
            PlatformToolbox::hideCursor();
    }

    bool shouldShowWatermark = shown || anim.openProgress > 0.0f ||
        (PlayLayer::get() && engine && engine->engineMode == MODE_EXECUTE);
    if (shouldShowWatermark) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y - 30),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##watermark", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (fontSmall) ImGui::PushFont(fontSmall);
        ImVec4 a = theme.getAccent();
        ImGui::TextColored(ImVec4(a.x, a.y, a.z, 0.6f), "ToastyReplay v" MOD_VERSION);
        if (fontSmall) ImGui::PopFont();

        ImGui::End();
    }

    if (!shown && anim.openProgress <= 0.0f)
        return;

    if (shown && !previouslyShown && engine) {
        engine->reloadMacroList();
        previouslyShown = true;
    }

    if (shown && !anim.closing)
        PlatformToolbox::showCursor();

    theme.applyToImGuiStyle();
    drawBackdrop();
    drawMainWindow();
}

void MenuInterface::initialize() {
    ImGuiIO& io = ImGui::GetIO();

    auto boldPath = (Mod::get()->getResourcesDir() / "Roboto-Bold.ttf").string();
    auto regularPath = (Mod::get()->getResourcesDir() / "Roboto-Regular.ttf").string();

    bool hasRegular = std::filesystem::exists(regularPath);
    bool hasBold = std::filesystem::exists(boldPath);

    const char* bodyFont = hasRegular ? regularPath.c_str() : (hasBold ? boldPath.c_str() : nullptr);
    const char* headFont = hasBold ? boldPath.c_str() : nullptr;

    if (bodyFont) {
        fontBody = io.Fonts->AddFontFromFileTTF(bodyFont, 16.0f);
        fontSmall = io.Fonts->AddFontFromFileTTF(bodyFont, 13.0f);
    } else {
        fontBody = io.Fonts->AddFontDefault();
        fontSmall = io.Fonts->AddFontDefault();
    }

    if (headFont) {
        fontHeading = io.Fonts->AddFontFromFileTTF(headFont, 20.0f);
        fontTitle = io.Fonts->AddFontFromFileTTF(headFont, 28.0f);
    } else {
        fontHeading = io.Fonts->AddFontDefault();
        fontTitle = io.Fonts->AddFontDefault();
    }

    io.Fonts->Build();
    loadSettings();
    theme.applyToImGuiStyle();
}

$on_mod(Loaded) {
    ImGuiCocos::get().setup([] {
        MenuInterface::get()->initialize();
    }).draw([] {
        MenuInterface::get()->drawInterface();
    });
}
