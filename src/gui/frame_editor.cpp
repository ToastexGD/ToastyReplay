#include "gui/frame_editor.hpp"
#include "gui/gui.hpp"
#include "i18n/localization.hpp"
#include "ToastyReplay.hpp"
#include "ttr_format.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fmt/format.h>

static ImVec4 feWithAlpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

static ImVec4 feBrighten(const ImVec4& c, float amount) {
    return ImVec4(
        std::clamp(c.x + amount, 0.0f, 1.0f),
        std::clamp(c.y + amount, 0.0f, 1.0f),
        std::clamp(c.z + amount, 0.0f, 1.0f),
        c.w
    );
}

static ImU32 feToU32(const ImVec4& c) {
    return ImGui::ColorConvertFloat4ToU32(c);
}

static float feSmoothStep(float current, float target, float speed, float dt) {
    return current + (target - current) * std::min(1.0f, dt * speed);
}

static float feEaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static void feDrawSolidRect(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, const ThemeEngine& theme, float alpha, bool border = true) {
    ImVec4 fill(theme.cardColor.x, theme.cardColor.y, theme.cardColor.z, theme.cardColor.w * alpha);
    dl->AddRectFilled(min, max, feToU32(fill), rounding);
    if (border)
        dl->AddRect(min, max, theme.getAccentU32(0.18f * alpha), rounding, 0, 1.0f);
}

static std::string feTr(std::string_view key) {
    return std::string(toasty::i18n::tr(key));
}

template <class... Args>
static std::string feTrf(std::string_view key, Args&&... args) {
    return toasty::i18n::trf(key, std::forward<Args>(args)...);
}

void FrameEditor::computeP2Color(const ImVec4& accent) {
    float r = accent.x, g = accent.y, b = accent.z;
    float cmax = std::max({r, g, b});
    float cmin = std::min({r, g, b});
    float delta = cmax - cmin;
    float h = 0.0f, s = 0.0f, v = cmax;

    if (delta > 0.0001f) {
        s = delta / cmax;
        if (cmax == r) h = std::fmod((g - b) / delta, 6.0f);
        else if (cmax == g) h = (b - r) / delta + 2.0f;
        else h = (r - g) / delta + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }

    h += 0.5f;
    if (h > 1.0f) h -= 1.0f;

    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rr, gg, bb;
    float hh = h * 6.0f;
    if (hh < 1.0f) { rr = c; gg = x; bb = 0; }
    else if (hh < 2.0f) { rr = x; gg = c; bb = 0; }
    else if (hh < 3.0f) { rr = 0; gg = c; bb = x; }
    else if (hh < 4.0f) { rr = 0; gg = x; bb = c; }
    else if (hh < 5.0f) { rr = x; gg = 0; bb = c; }
    else { rr = c; gg = 0; bb = x; }

    p2Color = ImVec4(rr + m, gg + m, bb + m, 1.0f);
}

void FrameEditor::openTTR(const std::string& name, TTRMacro* macro) {
    if (!macro) return;
    active = true;
    dirty = false;
    macroName = name;
    format = EditorFormat::TTR;
    framerate = macro->framerate;
    twoPlayerMode = macro->twoPlayerMode;
    confirmingDiscard = false;

    if (cachedTTR) { delete cachedTTR; cachedTTR = nullptr; }
    if (cachedGDR) { delete cachedGDR; cachedGDR = nullptr; }
    cachedTTR = new TTRMacro(*macro);

    inputs.clear();
    inputs.reserve(macro->inputs.size());
    for (size_t i = 0; i < macro->inputs.size(); i++) {
        auto& src = macro->inputs[i];
        EditorInput ei;
        ei.frame = src.tick;
        ei.actionType = src.actionType;
        ei.player2 = src.isPlayer2();
        ei.pressed = src.isPressed();
        ei.stepOffset = src.stepOffset;
        ei.originalIndex = i;
        inputs.push_back(ei);
    }

    originalInputs = inputs;
    maxFrame = 0;
    for (auto& inp : inputs) {
        if (inp.frame > maxFrame) maxFrame = inp.frame;
    }
    maxFrame = std::max(maxFrame + 120, (int32_t)240);

    rebuildSegments();

    scrollX = 0.0f;
    targetScrollX = 0.0f;
    pixelsPerFrame = 4.0f;
    targetPixelsPerFrame = 4.0f;
    selectedSegment = -1;
    hoveredSegment = -1;
    dragMode = DragMode::None;
    openAnimation = 0.0f;

    undoStack.clear();
    undoIndex = -1;

    goToFrameBuf[0] = '\0';
    selectedFrameBuf[0] = '\0';
}

void FrameEditor::openGDR(const std::string& name, MacroSequence* macro) {
    if (!macro) return;
    active = true;
    dirty = false;
    macroName = name;
    format = EditorFormat::GDR;
    framerate = macro->framerate;
    twoPlayerMode = false;
    confirmingDiscard = false;

    if (cachedTTR) { delete cachedTTR; cachedTTR = nullptr; }
    if (cachedGDR) { delete cachedGDR; cachedGDR = nullptr; }
    cachedGDR = new MacroSequence(*macro);

    inputs.clear();
    inputs.reserve(macro->inputs.size());
    for (size_t i = 0; i < macro->inputs.size(); i++) {
        auto& src = macro->inputs[i];
        EditorInput ei;
        ei.frame = static_cast<int32_t>(src.frame);
        ei.actionType = src.button;
        ei.player2 = src.player2;
        ei.pressed = src.down;
        ei.stepOffset = src.stepOffset;
        ei.originalIndex = i;
        inputs.push_back(ei);
    }

    originalInputs = inputs;
    maxFrame = 0;
    for (auto& inp : inputs) {
        if (inp.frame > maxFrame) maxFrame = inp.frame;
    }
    maxFrame = std::max(maxFrame + 120, (int32_t)240);

    rebuildSegments();

    scrollX = 0.0f;
    targetScrollX = 0.0f;
    pixelsPerFrame = 4.0f;
    targetPixelsPerFrame = 4.0f;
    selectedSegment = -1;
    hoveredSegment = -1;
    dragMode = DragMode::None;
    openAnimation = 0.0f;

    undoStack.clear();
    undoIndex = -1;

    goToFrameBuf[0] = '\0';
    selectedFrameBuf[0] = '\0';
}

void FrameEditor::close() {
    active = false;
    dirty = false;
    inputs.clear();
    originalInputs.clear();
    segments.clear();
    undoStack.clear();
    undoIndex = -1;
    selectedSegment = -1;
    hoveredSegment = -1;
    confirmingDiscard = false;
    if (cachedTTR) { delete cachedTTR; cachedTTR = nullptr; }
    if (cachedGDR) { delete cachedGDR; cachedGDR = nullptr; }
}

bool FrameEditor::isActive() const {
    return active;
}

int FrameEditor::findSegmentByPressIndex(size_t pressIdx) const {
    for (int i = 0; i < (int)segments.size(); i++) {
        if (segments[i].pressIndex == pressIdx) return i;
    }
    return -1;
}

void FrameEditor::rebuildSegments() {
    segments.clear();

    std::vector<size_t> sorted(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) sorted[i] = i;
    std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
        return inputs[a].frame < inputs[b].frame;
    });

    int maxPlayer = twoPlayerMode ? 1 : 0;
    for (int player = 0; player <= maxPlayer; player++) {
        for (int act = 0; act <= 3; act++) {
            bool isP2 = (player == 1);
            int openPress = -1;
            int32_t openFrame = 0;

            for (size_t si = 0; si < sorted.size(); si++) {
                size_t idx = sorted[si];
                auto& inp = inputs[idx];
                bool inputIsP2 = twoPlayerMode ? inp.player2 : false;
                if (inputIsP2 != isP2 || inp.actionType != act) continue;

                if (inp.pressed) {
                    if (openPress >= 0) {
                        HoldSegment seg;
                        seg.startFrame = openFrame;
                        seg.endFrame = inp.frame;
                        seg.player2 = isP2;
                        seg.actionType = act;
                        seg.pressIndex = openPress;
                        seg.releaseIndex = idx;
                        seg.hasRelease = false;

                        segments.push_back(seg);
                    }
                    openPress = static_cast<int>(idx);
                    openFrame = inp.frame;
                } else {
                    if (openPress >= 0) {
                        HoldSegment seg;
                        seg.startFrame = openFrame;
                        seg.endFrame = inp.frame;
                        seg.player2 = isP2;
                        seg.actionType = act;
                        seg.pressIndex = openPress;
                        seg.releaseIndex = idx;
                        seg.hasRelease = true;

                        segments.push_back(seg);
                        openPress = -1;
                    }
                }
            }

            if (openPress >= 0) {
                HoldSegment seg;
                seg.startFrame = openFrame;
                seg.endFrame = maxFrame;
                seg.player2 = isP2;
                seg.actionType = act;
                seg.pressIndex = openPress;
                seg.releaseIndex = openPress;
                seg.hasRelease = false;
                segments.push_back(seg);
            }
        }
    }

    std::sort(segments.begin(), segments.end(), [](const HoldSegment& a, const HoldSegment& b) {
        return a.startFrame < b.startFrame;
    });
}

void FrameEditor::pushUndo() {
    if (undoIndex < (int)undoStack.size() - 1) {
        undoStack.erase(undoStack.begin() + undoIndex + 1, undoStack.end());
    }
    UndoEntry entry;
    entry.inputs = inputs;
    undoStack.push_back(std::move(entry));
    undoIndex = static_cast<int>(undoStack.size()) - 1;

    if (undoStack.size() > 200) {
        undoStack.erase(undoStack.begin());
        undoIndex--;
    }
}

void FrameEditor::undo() {
    if (undoIndex < 0) return;
    if (undoIndex == (int)undoStack.size() - 1) {
        UndoEntry current;
        current.inputs = inputs;
        undoStack.push_back(std::move(current));
    }
    inputs = undoStack[undoIndex].inputs;
    undoIndex--;
    rebuildSegments();
    dirty = true;
    selectedSegment = -1;
}

void FrameEditor::redo() {
    if (undoIndex + 2 >= (int)undoStack.size()) return;
    undoIndex++;
    inputs = undoStack[undoIndex + 1].inputs;
    rebuildSegments();
    dirty = true;
    selectedSegment = -1;
}

void FrameEditor::applyToTTR() {
    if (!cachedTTR) {
        log::error("[FrameEditor] applyToTTR: cachedTTR is null, cannot save");
        return;
    }

    cachedTTR->inputs.clear();
    cachedTTR->inputs.reserve(inputs.size());

    std::vector<size_t> order(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return inputs[a].frame < inputs[b].frame;
    });

    for (size_t idx : order) {
        auto& ei = inputs[idx];
        TTRInput ti;
        ti.tick = ei.frame;
        ti.actionType = static_cast<uint8_t>(ei.actionType);
        ti.flags = 0;
        ti.setPlayer2(ei.player2);
        ti.setPressed(ei.pressed);
        ti.stepOffset = ei.stepOffset;
        cachedTTR->inputs.push_back(ti);
    }

    cachedTTR->anchors.clear();
    cachedTTR->checkpoints.clear();
    cachedTTR->persist();
    macroName = cachedTTR->name;

    auto* engine = ReplayEngine::get();
    if (engine && engine->ttrMode && engine->activeTTR) {
        engine->activeTTR->name = cachedTTR->name;
        engine->activeTTR->persistedName = cachedTTR->persistedName;
        engine->activeTTR->inputs = cachedTTR->inputs;
        engine->activeTTR->duration = cachedTTR->duration;
        engine->activeTTR->anchors.clear();
        engine->activeTTR->checkpoints.clear();
        engine->executeIndex = 0;
        engine->playbackAnchorIndex = 0;
        ReplayEngine::applyRuntimeAccuracyMode(AccuracyMode::Vanilla);
    }

    originalInputs = inputs;
    dirty = false;
}

void FrameEditor::applyToGDR() {
    if (!cachedGDR) return;

    cachedGDR->inputs.clear();
    cachedGDR->inputs.reserve(inputs.size());

    std::vector<size_t> order(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return inputs[a].frame < inputs[b].frame;
    });

    for (size_t idx : order) {
        auto& ei = inputs[idx];
        MacroAction ma(ei.frame, ei.actionType, ei.player2, ei.pressed, ei.stepOffset);
        cachedGDR->inputs.push_back(ma);
    }

    cachedGDR->anchors.clear();
    AccuracyMode accMode = cachedGDR->accuracyMode;
    int anchorInt = cachedGDR->savedAnchorInterval;
    cachedGDR->persist(accMode, anchorInt);
    macroName = cachedGDR->name;

    auto* engine = ReplayEngine::get();
    if (engine && !engine->ttrMode && engine->activeMacro) {
        engine->activeMacro->name = cachedGDR->name;
        engine->activeMacro->persistedName = cachedGDR->persistedName;
        engine->activeMacro->inputs = cachedGDR->inputs;
        engine->activeMacro->anchors.clear();
        engine->executeIndex = 0;
        engine->playbackAnchorIndex = 0;
        ReplayEngine::applyRuntimeAccuracyMode(AccuracyMode::Vanilla);
    }

    originalInputs = inputs;
    dirty = false;
}

int32_t FrameEditor::frameAtPixel(float pixelX, float canvasOriginX) const {
    return static_cast<int32_t>(std::floor((pixelX - canvasOriginX) / pixelsPerFrame + scrollX));
}

float FrameEditor::pixelAtFrame(int32_t frame, float canvasOriginX) const {
    return canvasOriginX + (static_cast<float>(frame) - scrollX) * pixelsPerFrame;
}

int32_t FrameEditor::visibleFrameStart() const {
    return std::max(0, static_cast<int32_t>(std::floor(scrollX)));
}

int32_t FrameEditor::visibleFrameEnd(float canvasWidth) const {
    return static_cast<int32_t>(std::ceil(scrollX + canvasWidth / pixelsPerFrame));
}

void FrameEditor::handleZoom(float mouseX, float canvasOriginX, float canvasWidth) {
    float wheel = ImGui::GetIO().MouseWheel;
    if (std::abs(wheel) < 0.01f) return;

    float mouseFrame = (mouseX - canvasOriginX) / pixelsPerFrame + scrollX;
    targetPixelsPerFrame *= (1.0f + wheel * 0.15f);
    targetPixelsPerFrame = std::clamp(targetPixelsPerFrame, 0.5f, 40.0f);
    targetScrollX = mouseFrame - (mouseX - canvasOriginX) / targetPixelsPerFrame;
    targetScrollX = std::max(targetScrollX, 0.0f);
}

void FrameEditor::handleDrag(float mouseX, float canvasOriginX) {
    if (dragMode == DragMode::PanBackground) {
        float dx = mouseX - dragStartMouseX;
        targetScrollX = dragStartScrollX - dx / pixelsPerFrame;
        targetScrollX = std::max(targetScrollX, 0.0f);
        scrollX = targetScrollX;
        if (!ImGui::IsMouseDown(0)) {
            dragMode = DragMode::None;
        }
    } else if (dragMode == DragMode::MoveSegment) {
        if (dragPressIdx < inputs.size()) {
            int32_t currentFrame = frameAtPixel(mouseX, canvasOriginX);
            int32_t delta = currentFrame - dragStartFrame;

            int32_t newStart = dragOriginalFrame + delta;
            if (newStart < 0) {
                delta = -dragOriginalFrame;
                newStart = 0;
            }

            inputs[dragPressIdx].frame = dragOriginalFrame + delta;
            if (dragReleaseIdx != dragPressIdx && dragReleaseIdx < inputs.size()) {
                inputs[dragReleaseIdx].frame = dragReleaseOrigFrame + delta;
                if (inputs[dragReleaseIdx].frame < 0) inputs[dragReleaseIdx].frame = 0;
            }
            dirty = true;
            rebuildSegments();
            selectedSegment = findSegmentByPressIndex(dragPressIdx);
        }
        if (!ImGui::IsMouseDown(0)) {
            dragMode = DragMode::None;
        }
    } else if (dragMode == DragMode::MoveEdgeLeft) {
        if (dragPressIdx < inputs.size()) {
            int32_t newFrame = frameAtPixel(mouseX, canvasOriginX);
            newFrame = std::max(newFrame, (int32_t)0);
            if (dragReleaseIdx != dragPressIdx && dragReleaseIdx < inputs.size()) {
                int32_t releaseFrame = inputs[dragReleaseIdx].frame;
                if (newFrame >= releaseFrame) newFrame = releaseFrame - 1;
            }
            inputs[dragPressIdx].frame = newFrame;
            dirty = true;
            rebuildSegments();
            selectedSegment = findSegmentByPressIndex(dragPressIdx);
        }
        if (!ImGui::IsMouseDown(0)) {
            dragMode = DragMode::None;
        }
    } else if (dragMode == DragMode::MoveEdgeRight) {
        if (dragReleaseIdx < inputs.size() && dragReleaseIdx != dragPressIdx) {
            int32_t newFrame = frameAtPixel(mouseX, canvasOriginX);
            newFrame = std::max(newFrame, (int32_t)0);
            int32_t pressFrame = inputs[dragPressIdx].frame;
            if (newFrame <= pressFrame) newFrame = pressFrame + 1;
            inputs[dragReleaseIdx].frame = newFrame;
            dirty = true;
            rebuildSegments();
            selectedSegment = findSegmentByPressIndex(dragPressIdx);
        }
        if (!ImGui::IsMouseDown(0)) {
            dragMode = DragMode::None;
        }
    } else if (dragMode == DragMode::OverviewPan) {
        if (!ImGui::IsMouseDown(0)) {
            dragMode = DragMode::None;
        }
    }
}

int FrameEditor::hitTestSegment(ImVec2 mousePos, ImVec2 lanesOrigin, float lanesWidth, float lanesHeight, int& edgeOut) const {
    edgeOut = 0;
    float laneH = lanesHeight * 0.5f;
    float barInset = laneH * 0.15f;

    for (int i = static_cast<int>(segments.size()) - 1; i >= 0; i--) {
        auto& seg = segments[i];
        float leftX = pixelAtFrame(seg.startFrame, lanesOrigin.x);
        float rightX = pixelAtFrame(seg.endFrame, lanesOrigin.x);

        if (rightX < lanesOrigin.x || leftX > lanesOrigin.x + lanesWidth) continue;

        float laneTop = seg.player2 ? lanesOrigin.y + laneH : lanesOrigin.y;
        float barTop = laneTop + barInset;
        float barBot = laneTop + laneH - barInset;

        if (mousePos.y < barTop || mousePos.y > barBot) continue;
        if (mousePos.x < leftX - 4.0f || mousePos.x > rightX + 4.0f) continue;

        float edgeThreshold = std::max(6.0f, pixelsPerFrame * 0.5f);
        edgeThreshold = std::min(edgeThreshold, (rightX - leftX) * 0.3f);

        if (mousePos.x <= leftX + edgeThreshold) {
            edgeOut = -1;
        } else if (mousePos.x >= rightX - edgeThreshold) {
            edgeOut = 1;
        }
        return i;
    }
    return -1;
}

void FrameEditor::draw(MenuInterface& ui) {
    float dt = ImGui::GetIO().DeltaTime;
    openAnimation = feSmoothStep(openAnimation, 1.0f, 10.0f, dt);
    float animT = feEaseOutCubic(openAnimation);

    computeP2Color(ui.theme.getAccent());

    pixelsPerFrame = feSmoothStep(pixelsPerFrame, targetPixelsPerFrame, 14.0f, dt);
    scrollX = feSmoothStep(scrollX, targetScrollX, 14.0f, dt);
    scrollX = std::max(scrollX, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animT);
    float offsetY = (1.0f - animT) * 14.0f;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);

    ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
    float contentWidth = ImGui::GetContentRegionAvail().x - 10.0f;
    float contentHeight = ImGui::GetContentRegionAvail().y;

    if (inputs.empty()) {
        ImVec2 center(contentOrigin.x + contentWidth * 0.5f, contentOrigin.y + contentHeight * 0.4f);
        auto msg = feTr("No inputs to edit");
        ImVec2 textSize = ImGui::CalcTextSize(msg.c_str());
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
            ui.theme.getTextSecondaryU32(),
            msg.c_str()
        );

        ImGui::SetCursorScreenPos(ImVec2(contentOrigin.x + contentWidth * 0.5f - 50.0f, center.y + 30.0f));
        if (Widgets::StyledButton("Back##editorEmpty", ImVec2(100.0f, 28.0f), ui.theme, ui.anim, 6.0f)) {
            close();
        }
        ImGui::PopStyleVar();
        return;
    }

    constexpr float toolbarH = 38.0f;
    constexpr float overviewH = 22.0f;
    constexpr float rulerH = 18.0f;
    constexpr float scrollbarH = 12.0f;
    constexpr float detailH = 30.0f;
    constexpr float spacing = 2.0f;

    float fixedH = toolbarH + overviewH + rulerH + scrollbarH + detailH + spacing * 5;
    float lanesH = std::max(contentHeight - fixedH, 80.0f);

    float yPos = contentOrigin.y;
    drawToolbar(ui, ImVec2(contentOrigin.x, yPos), contentWidth);
    yPos += toolbarH + spacing;

    drawOverviewBar(ui, ImVec2(contentOrigin.x, yPos), contentWidth, overviewH);
    yPos += overviewH + spacing;

    ImVec2 rulerOrigin(contentOrigin.x, yPos);
    drawRuler(ui, rulerOrigin, contentWidth, rulerH);

    yPos += rulerH;

    ImVec2 lanesOrigin(contentOrigin.x, yPos);
    drawLanes(ui, lanesOrigin, contentWidth, lanesH);

    ImVec2 mousePos = ImGui::GetIO().MousePos;

    if (!confirmingDiscard) {
        ImGui::SetCursorScreenPos(lanesOrigin);
        ImGui::InvisibleButton("##lanesInput", ImVec2(contentWidth, lanesH));
        bool lanesHovered = ImGui::IsItemHovered();
        bool lanesClicked = ImGui::IsItemClicked(0);

        if (lanesHovered && dragMode == DragMode::None) {
            handleZoom(mousePos.x, lanesOrigin.x, contentWidth);
        }

        if (dragMode != DragMode::None) {
            handleDrag(mousePos.x, lanesOrigin.x);
        }

        if (lanesClicked && dragMode == DragMode::None) {
            int edge = 0;
            int hit = hitTestSegment(mousePos, lanesOrigin, contentWidth, lanesH, edge);
            if (hit >= 0) {
                selectedSegment = hit;
                auto& seg = segments[selectedSegment];
                std::snprintf(selectedFrameBuf, sizeof(selectedFrameBuf), "%d", seg.startFrame);

                dragPressIdx = seg.pressIndex;
                dragReleaseIdx = seg.releaseIndex;

                if (edge == -1) {
                    pushUndo();
                    dragMode = DragMode::MoveEdgeLeft;
                    dragStartMouseX = mousePos.x;
                } else if (edge == 1 && seg.hasRelease) {
                    pushUndo();
                    dragMode = DragMode::MoveEdgeRight;
                    dragStartMouseX = mousePos.x;
                } else {
                    pushUndo();
                    dragMode = DragMode::MoveSegment;
                    dragStartMouseX = mousePos.x;
                    dragStartFrame = frameAtPixel(mousePos.x, lanesOrigin.x);
                    dragOriginalFrame = inputs[seg.pressIndex].frame;
                    dragReleaseOrigFrame = (seg.hasRelease && seg.releaseIndex != seg.pressIndex)
                        ? inputs[seg.releaseIndex].frame : inputs[seg.pressIndex].frame;
                }
            } else {
                selectedSegment = -1;
                selectedFrameBuf[0] = '\0';
                dragMode = DragMode::PanBackground;
                dragStartMouseX = mousePos.x;
                dragStartScrollX = scrollX;
            }
        }

    }

    yPos += lanesH + spacing;
    drawScrollbar(ui, ImVec2(contentOrigin.x, yPos), contentWidth, scrollbarH);
    yPos += scrollbarH + spacing;

    drawDetailBar(ui, ImVec2(contentOrigin.x, yPos), contentWidth, detailH);

    if (!confirmingDiscard) {
        ImGuiIO& io = ImGui::GetIO();
        bool ctrlHeld = io.KeyCtrl;
        bool shiftHeld = io.KeyShift;

        if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (shiftHeld) redo();
            else undo();
        }
        if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            redo();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedSegment >= 0 && selectedSegment < (int)segments.size()) {
            pushUndo();
            auto& seg = segments[selectedSegment];
            std::vector<size_t> toRemove;
            toRemove.push_back(seg.pressIndex);
            if (seg.hasRelease && seg.releaseIndex != seg.pressIndex) {
                toRemove.push_back(seg.releaseIndex);
            }
            std::sort(toRemove.rbegin(), toRemove.rend());
            for (size_t ri : toRemove) {
                if (ri < inputs.size()) {
                    inputs.erase(inputs.begin() + ri);
                }
            }
            selectedSegment = -1;
            dirty = true;
            rebuildSegments();
        }

        if (selectedSegment >= 0 && selectedSegment < (int)segments.size() && dragMode == DragMode::None) {
            auto& seg = segments[selectedSegment];
            size_t pIdx = seg.pressIndex;
            size_t rIdx = seg.releaseIndex;
            bool hasRel = seg.hasRelease && rIdx != pIdx;

            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                if (inputs[pIdx].frame > 0) {
                    pushUndo();
                    inputs[pIdx].frame--;
                    if (hasRel && inputs[rIdx].frame > 0) {
                        inputs[rIdx].frame--;
                    }
                    dirty = true;
                    rebuildSegments();
                    selectedSegment = findSegmentByPressIndex(pIdx);
                    if (selectedSegment >= 0)
                        std::snprintf(selectedFrameBuf, sizeof(selectedFrameBuf), "%d", inputs[pIdx].frame);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                pushUndo();
                inputs[pIdx].frame++;
                if (hasRel) {
                    inputs[rIdx].frame++;
                }
                dirty = true;
                rebuildSegments();
                selectedSegment = findSegmentByPressIndex(pIdx);
                if (selectedSegment >= 0)
                    std::snprintf(selectedFrameBuf, sizeof(selectedFrameBuf), "%d", inputs[pIdx].frame);
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (dirty) {
                confirmingDiscard = true;
            } else {
                close();
            }
        }
    }

    if (confirmingDiscard) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float popW = 260.0f;
        float popH = 70.0f;
        ImVec2 popMin(contentOrigin.x + (contentWidth - popW) * 0.5f, contentOrigin.y + contentHeight * 0.5f - popH * 0.5f);
        ImVec2 popMax(popMin.x + popW, popMin.y + popH);

        dl->AddRectFilled(contentOrigin, ImVec2(contentOrigin.x + contentWidth, contentOrigin.y + contentHeight),
            feToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.4f)));
        dl->AddRectFilled(popMin, popMax, feToU32(ImVec4(0.08f, 0.08f, 0.1f, 0.95f)), 6.0f);
        dl->AddRect(popMin, popMax, ui.theme.getAccentU32(0.5f), 6.0f, 0, 1.0f);

        auto promptText = feTr("Unsaved changes. Discard?");
        ImVec2 promptSize = ImGui::CalcTextSize(promptText.c_str());
        dl->AddText(ImVec2(popMin.x + (popW - promptSize.x) * 0.5f, popMin.y + 8.0f),
            ui.theme.getTextU32(), promptText.c_str());

        ImGui::SetCursorScreenPos(ImVec2(popMin.x + 30.0f, popMin.y + 34.0f));
        if (Widgets::StyledButton("Discard##confirmDisc", ImVec2(90.0f, 26.0f), ui.theme, ui.anim, 4.0f)) {
            confirmingDiscard = false;
            close();
        }
        ImGui::SameLine(0, 20.0f);
        if (Widgets::StyledButton("Cancel##confirmCancel", ImVec2(90.0f, 26.0f), ui.theme, ui.anim, 4.0f)) {
            confirmingDiscard = false;
        }
    }

    ImGui::PopStyleVar();
}

void FrameEditor::drawToolbar(MenuInterface& ui, ImVec2 origin, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(origin.x + width, origin.y + 38.0f);
    feDrawSolidRect(dl, origin, max, ui.theme.cornerRadius, ui.theme, 0.6f);

    float x = origin.x + 8.0f;
    float btnY = origin.y + 5.0f;
    float btnH = 28.0f;

    ImGui::SetCursorScreenPos(ImVec2(x, btnY));
    if (Widgets::StyledButton("< Back##edBack", ImVec2(60.0f, btnH), ui.theme, ui.anim, 4.0f)) {
        if (dirty) {
            confirmingDiscard = true;
        } else {
            close();
        }
    }
    x += 68.0f;

    ImGui::SetCursorScreenPos(ImVec2(x, btnY));
    if (Widgets::StyledButton("Save##edSave", ImVec2(50.0f, btnH), ui.theme, ui.anim, 4.0f)) {
        if (dirty) {
            if (format == EditorFormat::TTR) applyToTTR();
            else applyToGDR();
        }
    }
    x += 58.0f;

    ImGui::SetCursorScreenPos(ImVec2(x, btnY));
    if (Widgets::StyledButton("Undo##edUndo", ImVec2(48.0f, btnH), ui.theme, ui.anim, 4.0f)) {
        undo();
    }
    x += 56.0f;

    ImGui::SetCursorScreenPos(ImVec2(x, btnY));
    if (Widgets::StyledButton("Redo##edRedo", ImVec2(48.0f, btnH), ui.theme, ui.anim, 4.0f)) {
        redo();
    }
    x += 56.0f;

    float nameX = origin.x + width - 8.0f;
    auto infoText = feTrf("{count} inputs", fmt::arg("count", inputs.size()));
    ImVec2 infoSize = ImGui::CalcTextSize(infoText.c_str());
    nameX -= infoSize.x;
    dl->AddText(ImVec2(nameX, origin.y + 12.0f), ui.theme.getTextSecondaryU32(), infoText.c_str());
    nameX -= 12.0f;

    const char* nameStr = macroName.c_str();
    ImVec2 nameSize = ImGui::CalcTextSize(nameStr);
    float maxNameW = nameX - origin.x - 380.0f;
    if (maxNameW > 30.0f) {
        nameX -= std::min(nameSize.x, maxNameW);
        dl->AddText(ImVec2(nameX, origin.y + 12.0f), ui.theme.getAccentU32(), nameStr);
    }

    if (dirty) {
        dl->AddText(ImVec2(nameX - 16.0f, origin.y + 10.0f), feToU32(ImVec4(1.0f, 0.8f, 0.2f, 1.0f)), "*");
    }

}

void FrameEditor::drawOverviewBar(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(origin.x + width, origin.y + height);

    dl->AddRectFilled(origin, max, feToU32(ImVec4(0.06f, 0.06f, 0.08f, 0.7f)), 3.0f);
    dl->AddRect(origin, max, ui.theme.getAccentU32(0.12f), 3.0f, 0, 1.0f);

    if (maxFrame <= 0) return;

    float barPad = 2.0f;
    float innerW = width - barPad * 2.0f;
    float halfH = (height - barPad * 2.0f) * 0.5f;
    float p1Top = origin.y + barPad;
    float p2Top = p1Top + halfH;

    ImVec4 accentColor = ui.theme.getAccent();
    for (auto& seg : segments) {
        float lx = origin.x + barPad + (static_cast<float>(seg.startFrame) / maxFrame) * innerW;
        float rx = origin.x + barPad + (static_cast<float>(seg.endFrame) / maxFrame) * innerW;
        rx = std::max(rx, lx + 1.0f);

        float top = seg.player2 ? p2Top : p1Top;
        ImVec4 col = seg.player2 ? feWithAlpha(p2Color, 0.7f) : feWithAlpha(accentColor, 0.7f);
        dl->AddRectFilled(ImVec2(lx, top), ImVec2(rx, top + halfH), feToU32(col));
    }

    float viewStart = scrollX / static_cast<float>(maxFrame) * innerW;
    float viewEnd = (scrollX + width / pixelsPerFrame) / static_cast<float>(maxFrame) * innerW;
    viewStart = std::clamp(viewStart, 0.0f, innerW);
    viewEnd = std::clamp(viewEnd, viewStart + 2.0f, innerW);

    ImVec2 vpMin(origin.x + barPad + viewStart, origin.y + 1.0f);
    ImVec2 vpMax(origin.x + barPad + viewEnd, origin.y + height - 1.0f);
    dl->AddRectFilled(vpMin, vpMax, feToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)));
    dl->AddRect(vpMin, vpMax, ui.theme.getAccentU32(0.6f), 0.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##overviewBar", ImVec2(width, height));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        float mouseRel = (ImGui::GetIO().MousePos.x - origin.x - barPad) / innerW;
        mouseRel = std::clamp(mouseRel, 0.0f, 1.0f);
        float halfView = (width / pixelsPerFrame) * 0.5f;
        targetScrollX = mouseRel * maxFrame - halfView;
        targetScrollX = std::max(targetScrollX, 0.0f);
    }
}

void FrameEditor::drawRuler(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(origin.x + width, origin.y + height);

    dl->AddRectFilled(origin, max, feToU32(ImVec4(0.07f, 0.07f, 0.09f, 0.6f)));

    int32_t vStart = visibleFrameStart();
    int32_t vEnd = visibleFrameEnd(width);

    int majorInterval, minorInterval;
    if (pixelsPerFrame > 12.0f) { majorInterval = 10; minorInterval = 1; }
    else if (pixelsPerFrame > 5.0f) { majorInterval = 50; minorInterval = 10; }
    else if (pixelsPerFrame > 2.0f) { majorInterval = 100; minorInterval = 10; }
    else if (pixelsPerFrame > 1.0f) { majorInterval = 200; minorInterval = 50; }
    else { majorInterval = 500; minorInterval = 100; }

    int32_t minorStart = (vStart / minorInterval) * minorInterval;
    for (int32_t f = minorStart; f <= vEnd; f += minorInterval) {
        if (f < 0) continue;
        float x = pixelAtFrame(f, origin.x);
        if (x < origin.x || x > origin.x + width) continue;

        bool isMajor = (f % majorInterval == 0);
        float tickH = isMajor ? height * 0.6f : height * 0.3f;
        ImU32 tickCol = isMajor ? ui.theme.getAccentU32(0.5f) : ui.theme.getAccentU32(0.2f);
        dl->AddLine(ImVec2(x, origin.y + height - tickH), ImVec2(x, origin.y + height), tickCol, 1.0f);

        if (isMajor) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", f);
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            if (x + textSize.x + 4.0f < origin.x + width) {
                dl->AddText(ImVec2(x + 3.0f, origin.y + 1.0f), ui.theme.getTextSecondaryU32(), buf);
            }
        }
    }
}

void FrameEditor::drawLanes(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float laneH = height * 0.5f;

    ImVec2 p1Min = origin;
    ImVec2 p1Max(origin.x + width, origin.y + laneH);
    ImVec2 p2Min(origin.x, origin.y + laneH);
    ImVec2 p2Max(origin.x + width, origin.y + height);

    dl->AddRectFilled(p1Min, p1Max, feToU32(ImVec4(0.05f, 0.05f, 0.07f, 0.55f)));
    dl->AddRectFilled(p2Min, p2Max, feToU32(ImVec4(0.04f, 0.04f, 0.06f, 0.55f)));

    dl->AddLine(ImVec2(origin.x, origin.y + laneH), ImVec2(origin.x + width, origin.y + laneH),
        ui.theme.getAccentU32(0.15f), 1.0f);

    ImVec4 accentColor = ui.theme.getAccent();

    float labelX = origin.x + 4.0f;
    if (ui.fontSmall) ImGui::PushFont(ui.fontSmall);
    auto p1Text = feTr("P1");
    auto p2Text = feTr("P2");
    dl->AddText(ImVec2(labelX, origin.y + 2.0f), feToU32(feWithAlpha(accentColor, 0.4f)), p1Text.c_str());
    dl->AddText(ImVec2(labelX, origin.y + laneH + 2.0f), feToU32(feWithAlpha(p2Color, 0.4f)), p2Text.c_str());
    if (ui.fontSmall) ImGui::PopFont();

    int32_t vStart = visibleFrameStart();
    int32_t vEnd = visibleFrameEnd(width);

    int gridInterval;
    if (pixelsPerFrame > 12.0f) gridInterval = 10;
    else if (pixelsPerFrame > 5.0f) gridInterval = 50;
    else if (pixelsPerFrame > 2.0f) gridInterval = 100;
    else gridInterval = 500;

    int32_t gridStart = (vStart / gridInterval) * gridInterval;
    for (int32_t f = gridStart; f <= vEnd; f += gridInterval) {
        if (f < 0) continue;
        float x = pixelAtFrame(f, origin.x);
        if (x < origin.x || x > origin.x + width) continue;
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height),
            ui.theme.getAccentU32(0.06f), 1.0f);
    }

    float barInset = laneH * 0.15f;
    float barH = laneH - barInset * 2.0f;

    for (int i = 0; i < (int)segments.size(); i++) {
        auto& seg = segments[i];
        float leftX = pixelAtFrame(seg.startFrame, origin.x);
        float rightX = pixelAtFrame(seg.endFrame, origin.x);

        if (rightX < origin.x || leftX > origin.x + width) continue;
        leftX = std::max(leftX, origin.x);
        rightX = std::min(rightX, origin.x + width);
        if (rightX - leftX < 1.0f) rightX = leftX + 1.0f;

        float laneTop = seg.player2 ? origin.y + laneH : origin.y;
        float barTop = laneTop + barInset;

        ImVec4 baseCol = seg.player2 ? p2Color : accentColor;
        float baseAlpha = 0.55f;

        if (seg.actionType == 1) baseCol = feBrighten(baseCol, 0.08f);
        else if (seg.actionType == 2) baseCol = feBrighten(baseCol, -0.06f);
        else if (seg.actionType == 3) baseCol = feBrighten(baseCol, -0.12f);

        bool isSelected = (i == selectedSegment);
        bool isHovered = (i == hoveredSegment);

        if (isSelected) {
            ImVec2 glowMin(leftX - 3.0f, barTop - 3.0f);
            ImVec2 glowMax(rightX + 3.0f, barTop + barH + 3.0f);
            dl->AddRectFilled(glowMin, glowMax, feToU32(feWithAlpha(baseCol, 0.12f)), 5.0f);
        }

        ImVec4 fillCol = feWithAlpha(baseCol, isSelected ? 0.80f : (isHovered ? 0.68f : baseAlpha));
        dl->AddRectFilled(ImVec2(leftX, barTop), ImVec2(rightX, barTop + barH), feToU32(fillCol), 3.0f);

        if (isSelected) {
            dl->AddRect(ImVec2(leftX, barTop), ImVec2(rightX, barTop + barH),
                feToU32(feWithAlpha(baseCol, 0.9f)), 3.0f, 0, 1.5f);
        }

        if (pixelsPerFrame > 2.0f) {
            float edgeW = std::min(4.0f, (rightX - leftX) * 0.2f);

            ImVec4 edgeCol = ImVec4(1.0f, 1.0f, 1.0f, isSelected ? 0.5f : 0.25f);
            dl->AddRectFilled(ImVec2(leftX, barTop), ImVec2(leftX + edgeW, barTop + barH),
                feToU32(edgeCol), 2.0f);

            if (seg.hasRelease) {
                dl->AddRectFilled(ImVec2(rightX - edgeW, barTop), ImVec2(rightX, barTop + barH),
                    feToU32(edgeCol), 2.0f);
            }
        }

        if (pixelsPerFrame > 6.0f && (rightX - leftX) > 40.0f) {
            char frameBuf[16];
            std::snprintf(frameBuf, sizeof(frameBuf), "%d", seg.startFrame);
            if (ui.fontSmall) ImGui::PushFont(ui.fontSmall);
            ImVec2 ts = ImGui::CalcTextSize(frameBuf);
            if (ts.x < (rightX - leftX - 4.0f)) {
                dl->AddText(ImVec2(leftX + 4.0f, barTop + (barH - ts.y) * 0.5f),
                    feToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.7f)), frameBuf);
            }
            if (ui.fontSmall) ImGui::PopFont();
        }
    }

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool mouseInLanes = mousePos.x >= origin.x && mousePos.x <= origin.x + width &&
                        mousePos.y >= origin.y && mousePos.y <= origin.y + height;
    if (mouseInLanes && dragMode == DragMode::None) {
        int edge = 0;
        hoveredSegment = hitTestSegment(mousePos, origin, width, height, edge);
    } else if (dragMode == DragMode::None) {
        hoveredSegment = -1;
    }
}

void FrameEditor::drawScrollbar(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(origin.x + width, origin.y + height);

    dl->AddRectFilled(origin, max, feToU32(ImVec4(0.05f, 0.05f, 0.07f, 0.5f)), 3.0f);

    if (maxFrame <= 0) return;

    float viewFrames = width / pixelsPerFrame;
    float totalFrames = static_cast<float>(maxFrame);
    if (totalFrames <= 0) return;

    float thumbRatio = std::min(viewFrames / totalFrames, 1.0f);
    float thumbW = std::max(thumbRatio * width, 20.0f);
    float thumbStart = (scrollX / totalFrames) * (width - thumbW);
    thumbStart = std::clamp(thumbStart, 0.0f, width - thumbW);

    ImVec2 thumbMin(origin.x + thumbStart, origin.y + 1.0f);
    ImVec2 thumbMax(origin.x + thumbStart + thumbW, origin.y + height - 1.0f);
    ImVec4 accent = ui.theme.getAccent();
    dl->AddRectFilled(thumbMin, thumbMax, feToU32(feWithAlpha(accent, 0.35f)), 3.0f);

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##scrollbar", ImVec2(width, height));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        float mouseRel = (ImGui::GetIO().MousePos.x - origin.x) / width;
        mouseRel = std::clamp(mouseRel, 0.0f, 1.0f);
        targetScrollX = mouseRel * totalFrames - viewFrames * 0.5f;
        targetScrollX = std::max(targetScrollX, 0.0f);
    }
}

void FrameEditor::drawDetailBar(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(origin.x + width, origin.y + height);
    feDrawSolidRect(dl, origin, max, ui.theme.cornerRadius, ui.theme, 0.5f);

    float x = origin.x + 10.0f;
    float textY = origin.y + (height - ImGui::GetTextLineHeight()) * 0.5f;

    if (selectedSegment >= 0 && selectedSegment < (int)segments.size()) {
        auto& seg = segments[selectedSegment];

        auto frameText = feTr("Frame:");
        dl->AddText(ImVec2(x, textY), ui.theme.getTextSecondaryU32(), frameText.c_str());
        x += ImGui::CalcTextSize(frameText.c_str()).x + 6.0f;

        ImGui::SetCursorScreenPos(ImVec2(x, origin.y + 3.0f));
        ImGui::PushItemWidth(60.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, feToU32(ImVec4(0.1f, 0.1f, 0.12f, 0.8f)));
        ImGui::PushStyleColor(ImGuiCol_Text, feToU32(ui.theme.textPrimary));
        bool submitted = ImGui::InputText("##selFrame", selectedFrameBuf, sizeof(selectedFrameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();

        if (submitted) {
            int newFrame = std::atoi(selectedFrameBuf);
            if (newFrame >= 0) {
                pushUndo();
                int32_t delta = newFrame - inputs[seg.pressIndex].frame;
                inputs[seg.pressIndex].frame = newFrame;
                if (seg.hasRelease && seg.releaseIndex != seg.pressIndex) {
                    inputs[seg.releaseIndex].frame += delta;
                }
                dirty = true;
                rebuildSegments();
            }
        }

        x += 68.0f;
        dl->AddText(ImVec2(x, textY), ui.theme.getAccentU32(0.6f), "|");
        x += 14.0f;

        auto typeStr = feTr(seg.hasRelease ? "Hold" : "Press");
        dl->AddText(ImVec2(x, textY), ui.theme.getTextSecondaryU32(), typeStr.c_str());
        x += ImGui::CalcTextSize(typeStr.c_str()).x + 10.0f;

        dl->AddText(ImVec2(x, textY), ui.theme.getAccentU32(0.6f), "|");
        x += 14.0f;

        auto playerStr = feTr(seg.player2 ? "P2" : "P1");
        ImVec4 playerCol = seg.player2 ? p2Color : ui.theme.getAccent();
        dl->AddText(ImVec2(x, textY), feToU32(playerCol), playerStr.c_str());
        x += ImGui::CalcTextSize(playerStr.c_str()).x + 10.0f;

        dl->AddText(ImVec2(x, textY), ui.theme.getAccentU32(0.6f), "|");
        x += 14.0f;

        int32_t dur = seg.endFrame - seg.startFrame;
        auto durText = feTrf("{count} frames", fmt::arg("count", dur));
        dl->AddText(ImVec2(x, textY), ui.theme.getTextSecondaryU32(), durText.c_str());
    } else {
        auto hintText = feTr("Click a segment to select it");
        dl->AddText(ImVec2(x, textY), ui.theme.getTextSecondaryU32(), hintText.c_str());
    }

    float goToX = origin.x + width - 140.0f;
    auto goToText = feTr("Go to:");
    dl->AddText(ImVec2(goToX, textY), ui.theme.getTextSecondaryU32(), goToText.c_str());
    goToX += ImGui::CalcTextSize(goToText.c_str()).x + 6.0f;

    ImGui::SetCursorScreenPos(ImVec2(goToX, origin.y + 3.0f));
    ImGui::PushItemWidth(60.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, feToU32(ImVec4(0.1f, 0.1f, 0.12f, 0.8f)));
    ImGui::PushStyleColor(ImGuiCol_Text, feToU32(ui.theme.textPrimary));
    bool goSubmitted = ImGui::InputText("##goToFrame", goToFrameBuf, sizeof(goToFrameBuf),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();

    if (goSubmitted) {
        int goFrame = std::atoi(goToFrameBuf);
        if (goFrame >= 0) {
            float viewFrames = ImGui::GetContentRegionAvail().x / pixelsPerFrame;
            targetScrollX = static_cast<float>(goFrame) - viewFrames * 0.5f;
            targetScrollX = std::max(targetScrollX, 0.0f);
        }
    }
}
