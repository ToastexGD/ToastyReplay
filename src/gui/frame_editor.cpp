#include "gui/frame_editor.hpp"
#include "gui/frame_editor_commit_model.hpp"
#include "gui/gui.hpp"
#include "lang/localization.hpp"
#include "ToastyReplay.hpp"
#include "format/ttr_format.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
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
    return std::string(toasty::lang::tr(key));
}

template <class... Args>
static std::string feTrf(std::string_view key, Args&&... args) {
    return toasty::lang::trf(key, std::forward<Args>(args)...);
}

static bool feParseInt(char const* text, int& out) {
    if (!text) return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '\0') return false;

    errno = 0;
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (text == end || errno == ERANGE) return false;
    while (end && (*end == ' ' || *end == '\t')) ++end;
    if (end && *end != '\0') return false;

    out = static_cast<int>(std::clamp<long>(value, 0, std::numeric_limits<int>::max()));
    return true;
}

static std::string feActionLabel(int actionType) {
    if (actionType == static_cast<int>(PlayerButton::Jump)) return feTr("Jump");
    if (actionType == static_cast<int>(PlayerButton::Left)) return feTr("Left");
    if (actionType == static_cast<int>(PlayerButton::Right)) return feTr("Right");
    return feTrf("Button {id}", fmt::arg("id", actionType));
}

static ImVec4 feActionColor(int actionType, ImVec4 base) {
    if (actionType == static_cast<int>(PlayerButton::Left)) return feBrighten(base, -0.06f);
    if (actionType == static_cast<int>(PlayerButton::Right)) return feBrighten(base, 0.08f);
    if (actionType == 0) return feBrighten(base, -0.14f);
    return base;
}

static std::string feSegmentKind(const HoldSegment& seg) {
    if (!seg.hasRelease) return feTr("Press");
    if (seg.endFrame - seg.startFrame <= 1) return feTr("Tap");
    return feTr("Hold");
}

static std::string feFrameTimeLabel(int32_t frame, double framerate) {
    if (framerate <= 0.0) return "0.000s";
    return fmt::format("{:.3f}s", static_cast<double>(frame) / framerate);
}

static int feInputActionFromComboIndex(int index) {
    switch (index) {
        case 0: return static_cast<int>(PlayerButton::Jump);
        case 1: return static_cast<int>(PlayerButton::Left);
        case 2: return static_cast<int>(PlayerButton::Right);
        case 3: return 0;
        default: return static_cast<int>(PlayerButton::Jump);
    }
}

static std::string feActionComboLabel(int actionType) {
    if (actionType == 0) return feTr("Button 0");
    return feActionLabel(actionType);
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
    recomputeMaxFrame();

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
    selectedEndFrameBuf[0] = '\0';
    selectedDurationBuf[0] = '\0';
    filterFrameBuf[0] = '\0';
    std::snprintf(addFrameBuf, sizeof(addFrameBuf), "0");
    std::snprintf(addDurationBuf, sizeof(addDurationBuf), "1");
    filterPlayer = 0;
    filterAction = -1;
    showPressRows = true;
    showHoldRows = true;
    addActionType = static_cast<int>(PlayerButton::Jump);
    addPlayer = 0;
}

void FrameEditor::openGDR(const std::string& name, MacroSequence* macro) {
    if (!macro) return;
    active = true;
    dirty = false;
    macroName = name;
    format = EditorFormat::GDR;
    framerate = macro->framerate;
    twoPlayerMode = std::any_of(macro->inputs.begin(), macro->inputs.end(), [](auto const& input) {
        return input.player2;
    });
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
    recomputeMaxFrame();

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
    selectedEndFrameBuf[0] = '\0';
    selectedDurationBuf[0] = '\0';
    filterFrameBuf[0] = '\0';
    std::snprintf(addFrameBuf, sizeof(addFrameBuf), "0");
    std::snprintf(addDurationBuf, sizeof(addDurationBuf), "1");
    filterPlayer = 0;
    filterAction = -1;
    showPressRows = true;
    showHoldRows = true;
    addActionType = static_cast<int>(PlayerButton::Jump);
    addPlayer = 0;
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
    selectedFrameBuf[0] = '\0';
    selectedEndFrameBuf[0] = '\0';
    selectedDurationBuf[0] = '\0';
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

bool FrameEditor::hasPlayer2Inputs() const {
    return std::any_of(inputs.begin(), inputs.end(), [](EditorInput const& input) {
        return input.player2;
    });
}

void FrameEditor::recomputeMaxFrame() {
    maxFrame = 0;
    for (auto const& input : inputs) {
        maxFrame = std::max(maxFrame, input.frame);
    }
    maxFrame = std::max(maxFrame + 120, static_cast<int32_t>(240));
}

void FrameEditor::syncSelectionBuffers() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) {
        selectedFrameBuf[0] = '\0';
        selectedEndFrameBuf[0] = '\0';
        selectedDurationBuf[0] = '\0';
        return;
    }

    auto const& seg = segments[selectedSegment];
    std::snprintf(selectedFrameBuf, sizeof(selectedFrameBuf), "%d", seg.startFrame);
    if (seg.hasRelease) {
        std::snprintf(selectedEndFrameBuf, sizeof(selectedEndFrameBuf), "%d", seg.endFrame);
        std::snprintf(selectedDurationBuf, sizeof(selectedDurationBuf), "%d", std::max<int32_t>(0, seg.endFrame - seg.startFrame));
    } else {
        selectedEndFrameBuf[0] = '\0';
        selectedDurationBuf[0] = '\0';
    }
}

void FrameEditor::selectSegment(int segmentIndex) {
    if (segmentIndex < 0 || segmentIndex >= static_cast<int>(segments.size())) {
        clearSelection();
        return;
    }
    selectedSegment = segmentIndex;
    syncSelectionBuffers();
}

void FrameEditor::clearSelection() {
    selectedSegment = -1;
    syncSelectionBuffers();
}

void FrameEditor::markEdited(size_t preferredPressIndex) {
    recomputeMaxFrame();
    rebuildSegments();
    dirty = true;
    if (preferredPressIndex != std::numeric_limits<size_t>::max()) {
        selectedSegment = findSegmentByPressIndex(preferredPressIndex);
    }
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) {
        selectedSegment = -1;
    }
    syncSelectionBuffers();
}

void FrameEditor::rebuildSegments() {
    segments.clear();

    std::vector<size_t> sorted(inputs.size());
    for (size_t i = 0; i < inputs.size(); i++) sorted[i] = i;
    std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
        if (inputs[a].frame != inputs[b].frame) return inputs[a].frame < inputs[b].frame;
        return a < b;
    });

    std::vector<int> actionTypes;
    actionTypes.reserve(inputs.size() + 4);
    for (int act = 0; act <= 3; act++) {
        actionTypes.push_back(act);
    }
    for (auto const& input : inputs) {
        if (std::find(actionTypes.begin(), actionTypes.end(), input.actionType) == actionTypes.end()) {
            actionTypes.push_back(input.actionType);
        }
    }
    std::sort(actionTypes.begin(), actionTypes.end());

    bool hasP2 = twoPlayerMode || hasPlayer2Inputs();
    int maxPlayer = hasP2 ? 1 : 0;
    for (int player = 0; player <= maxPlayer; player++) {
        for (int act : actionTypes) {
            bool isP2 = (player == 1);
            int openPress = -1;
            int32_t openFrame = 0;

            for (size_t si = 0; si < sorted.size(); si++) {
                size_t idx = sorted[si];
                auto& inp = inputs[idx];
                bool inputIsP2 = hasP2 ? inp.player2 : false;
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

    std::stable_sort(segments.begin(), segments.end(), [](const HoldSegment& a, const HoldSegment& b) {
        if (a.startFrame != b.startFrame) return a.startFrame < b.startFrame;
        return a.pressIndex < b.pressIndex;
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
    recomputeMaxFrame();
    rebuildSegments();
    dirty = true;
    clearSelection();
}

void FrameEditor::redo() {
    if (undoIndex + 2 >= (int)undoStack.size()) return;
    undoIndex++;
    inputs = undoStack[undoIndex + 1].inputs;
    recomputeMaxFrame();
    rebuildSegments();
    dirty = true;
    clearSelection();
}

void FrameEditor::save() {
    commitPendingSelectionEdits();
    if (!dirty) return;
    if (format == EditorFormat::TTR) applyToTTR();
    else applyToGDR();
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

    if (std::any_of(inputs.begin(), inputs.end(), [](EditorInput const& input) { return input.player2; })) {
        cachedTTR->twoPlayerMode = true;
    }
    cachedTTR->anchors.clear();
    cachedTTR->checkpoints.clear();
    cachedTTR->exactCbsTiming = false;
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

void FrameEditor::deleteSelectedSegment() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;

    pushUndo();
    auto seg = segments[selectedSegment];
    std::vector<size_t> toRemove;
    toRemove.push_back(seg.pressIndex);
    if (seg.hasRelease && seg.releaseIndex != seg.pressIndex) {
        toRemove.push_back(seg.releaseIndex);
    }
    std::sort(toRemove.rbegin(), toRemove.rend());
    for (size_t index : toRemove) {
        if (index < inputs.size()) {
            inputs.erase(inputs.begin() + index);
        }
    }
    clearSelection();
    markEdited();
}

void FrameEditor::duplicateSelectedSegment() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;

    auto seg = segments[selectedSegment];
    if (seg.pressIndex >= inputs.size()) return;

    pushUndo();
    EditorInput press = inputs[seg.pressIndex];
    press.frame = std::max<int32_t>(0, press.frame + 1);
    press.originalIndex = inputs.size();
    size_t newPressIndex = inputs.size();
    inputs.push_back(press);

    if (seg.hasRelease && seg.releaseIndex != seg.pressIndex && seg.releaseIndex < inputs.size()) {
        EditorInput release = inputs[seg.releaseIndex];
        release.frame = std::max<int32_t>(press.frame + 1, release.frame + 1);
        release.originalIndex = inputs.size();
        inputs.push_back(release);
    }

    if (press.player2) twoPlayerMode = true;
    markEdited(newPressIndex);
}

void FrameEditor::nudgeSelected(int32_t delta) {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size()) || delta == 0) return;
    auto seg = segments[selectedSegment];
    if (seg.pressIndex >= inputs.size()) return;

    pushUndo();
    int32_t appliedDelta = delta;
    if (inputs[seg.pressIndex].frame + appliedDelta < 0) {
        appliedDelta = -inputs[seg.pressIndex].frame;
    }

    inputs[seg.pressIndex].frame += appliedDelta;
    if (seg.hasRelease && seg.releaseIndex != seg.pressIndex && seg.releaseIndex < inputs.size()) {
        inputs[seg.releaseIndex].frame = std::max<int32_t>(0, inputs[seg.releaseIndex].frame + appliedDelta);
        if (inputs[seg.releaseIndex].frame <= inputs[seg.pressIndex].frame) {
            inputs[seg.releaseIndex].frame = inputs[seg.pressIndex].frame + 1;
        }
    }

    markEdited(seg.pressIndex);
}

void FrameEditor::setSelectedStartFrame(int32_t frame) {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;
    auto seg = segments[selectedSegment];
    if (seg.pressIndex >= inputs.size()) return;

    frame = std::max<int32_t>(0, frame);
    if (inputs[seg.pressIndex].frame == frame) return;
    pushUndo();
    if (toasty::frame_editor::commitSelectedStartFrame(inputs, seg, frame)) {
        markEdited(seg.pressIndex);
    }
}

void FrameEditor::setSelectedEndFrame(int32_t frame) {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;
    auto seg = segments[selectedSegment];
    if (!seg.hasRelease || seg.releaseIndex == seg.pressIndex || seg.releaseIndex >= inputs.size() || seg.pressIndex >= inputs.size()) return;

    frame = std::max<int32_t>(inputs[seg.pressIndex].frame + 1, frame);
    if (inputs[seg.releaseIndex].frame == frame) return;
    pushUndo();
    if (toasty::frame_editor::commitSelectedEndFrame(inputs, seg, frame)) {
        markEdited(seg.pressIndex);
    }
}

bool FrameEditor::commitSelectedStartFrameText() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return false;
    auto seg = segments[selectedSegment];
    if (seg.pressIndex >= inputs.size()) return false;

    int32_t frame = 0;
    if (!toasty::frame_editor::parseNonNegativeFrameText(selectedFrameBuf, frame)) return false;
    frame = std::max<int32_t>(0, frame);
    if (inputs[seg.pressIndex].frame == frame) return false;
    pushUndo();
    if (!toasty::frame_editor::commitSelectedStartFrame(inputs, seg, frame)) return false;
    markEdited(seg.pressIndex);
    return true;
}

bool FrameEditor::commitSelectedEndFrameText() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return false;
    auto seg = segments[selectedSegment];
    if (!seg.hasRelease || seg.releaseIndex == seg.pressIndex || seg.releaseIndex >= inputs.size() || seg.pressIndex >= inputs.size()) return false;

    int32_t frame = 0;
    if (!toasty::frame_editor::parseNonNegativeFrameText(selectedEndFrameBuf, frame)) return false;
    frame = std::max<int32_t>(inputs[seg.pressIndex].frame + 1, frame);
    if (inputs[seg.releaseIndex].frame == frame) return false;
    pushUndo();
    if (!toasty::frame_editor::commitSelectedEndFrame(inputs, seg, frame)) return false;
    markEdited(seg.pressIndex);
    return true;
}

bool FrameEditor::commitSelectedDurationText() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return false;
    auto seg = segments[selectedSegment];
    if (!seg.hasRelease || seg.releaseIndex == seg.pressIndex || seg.releaseIndex >= inputs.size() || seg.pressIndex >= inputs.size()) return false;

    int32_t duration = 0;
    if (!toasty::frame_editor::parseNonNegativeFrameText(selectedDurationBuf, duration)) return false;
    int32_t targetFrame = inputs[seg.pressIndex].frame + std::max<int32_t>(1, duration);
    if (inputs[seg.releaseIndex].frame == targetFrame) return false;
    pushUndo();
    if (!toasty::frame_editor::commitSelectedDuration(inputs, seg, duration)) return false;
    markEdited(seg.pressIndex);
    return true;
}

bool FrameEditor::commitPendingSelectionEdits() {
    bool changed = false;
    changed = commitSelectedStartFrameText() || changed;
    changed = commitSelectedEndFrameText() || changed;
    changed = commitSelectedDurationText() || changed;
    return changed;
}

void FrameEditor::setSelectedActionType(int actionType) {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;
    auto seg = segments[selectedSegment];
    if (seg.pressIndex >= inputs.size()) return;

    pushUndo();
    inputs[seg.pressIndex].actionType = actionType;
    if (seg.hasRelease && seg.releaseIndex != seg.pressIndex && seg.releaseIndex < inputs.size()) {
        inputs[seg.releaseIndex].actionType = actionType;
    }
    markEdited(seg.pressIndex);
}

void FrameEditor::setSelectedPlayer(bool player2) {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;
    auto seg = segments[selectedSegment];
    if (seg.pressIndex >= inputs.size()) return;

    pushUndo();
    inputs[seg.pressIndex].player2 = player2;
    if (seg.hasRelease && seg.releaseIndex != seg.pressIndex && seg.releaseIndex < inputs.size()) {
        inputs[seg.releaseIndex].player2 = player2;
    }
    if (player2) twoPlayerMode = true;
    markEdited(seg.pressIndex);
}

void FrameEditor::createReleaseForSelected() {
    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) return;
    auto seg = segments[selectedSegment];
    if (seg.hasRelease || seg.pressIndex >= inputs.size()) return;

    pushUndo();
    EditorInput release = inputs[seg.pressIndex];
    release.frame = release.frame + 1;
    release.pressed = false;
    release.originalIndex = inputs.size();
    inputs.push_back(release);
    markEdited(seg.pressIndex);
}

void FrameEditor::addSegmentFromControls(bool includeRelease) {
    int frame = 0;
    int duration = 1;
    if (!feParseInt(addFrameBuf, frame)) return;
    if (!feParseInt(addDurationBuf, duration)) duration = 1;

    frame = std::max(0, frame);
    duration = std::max(1, duration);

    pushUndo();
    EditorInput press;
    press.frame = frame;
    press.actionType = addActionType;
    press.player2 = addPlayer == 1;
    press.pressed = true;
    press.stepOffset = 0.0f;
    press.originalIndex = inputs.size();
    size_t pressIndex = inputs.size();
    inputs.push_back(press);

    if (includeRelease) {
        EditorInput release = press;
        release.frame = frame + duration;
        release.pressed = false;
        release.originalIndex = inputs.size();
        inputs.push_back(release);
    }

    if (press.player2) twoPlayerMode = true;
    markEdited(pressIndex);
}

void FrameEditor::selectNearestFrame(int32_t frame) {
    if (segments.empty()) return;

    int bestIndex = -1;
    int32_t bestDistance = std::numeric_limits<int32_t>::max();
    for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
        auto const& seg = segments[i];
        int32_t distance = 0;
        if (frame < seg.startFrame) distance = seg.startFrame - frame;
        else if (frame > seg.endFrame) distance = frame - seg.endFrame;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
            if (distance == 0) break;
        }
    }
    selectSegment(bestIndex);
}

void FrameEditor::handleEditorShortcuts() {
    if (confirmingDiscard) return;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    bool ctrlHeld = io.KeyCtrl;
    bool shiftHeld = io.KeyShift;

    if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (shiftHeld) redo();
        else undo();
    }
    if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        redo();
    }
    if (ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_D)) {
        duplicateSelectedSegment();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        deleteSelectedSegment();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        nudgeSelected(shiftHeld ? -10 : -1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        nudgeSelected(shiftHeld ? 10 : 1);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (dirty) {
            confirmingDiscard = true;
        } else {
            close();
        }
    }
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

    constexpr float toolbarH = 38.0f;
    constexpr float filterH = 38.0f;
    constexpr float editorH = 132.0f;
    constexpr float spacing = 6.0f;

    float fixedH = toolbarH + filterH + editorH + spacing * 3;
    float listH = std::max(contentHeight - fixedH, 120.0f);

    float yPos = contentOrigin.y;
    drawToolbar(ui, ImVec2(contentOrigin.x, yPos), contentWidth);
    yPos += toolbarH + spacing;

    drawFilterBar(ui, ImVec2(contentOrigin.x, yPos), contentWidth, filterH);
    yPos += filterH + spacing;

    drawSegmentList(ui, ImVec2(contentOrigin.x, yPos), contentWidth, listH);
    yPos += listH + spacing;

    float addW = std::clamp(contentWidth * 0.34f, 210.0f, 300.0f);
    float inspectorW = contentWidth - addW - spacing;
    if (inspectorW < 260.0f) {
        inspectorW = std::max(160.0f, contentWidth * 0.58f);
        addW = std::max(120.0f, contentWidth - inspectorW - spacing);
    }
    drawInspector(ui, ImVec2(contentOrigin.x, yPos), inspectorW, editorH);
    drawAddPanel(ui, ImVec2(contentOrigin.x + inspectorW + spacing, yPos), addW, editorH);

    handleEditorShortcuts();

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
        commitPendingSelectionEdits();
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

void FrameEditor::drawFilterBar(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    feDrawSolidRect(dl, origin, ImVec2(origin.x + width, origin.y + height), ui.theme.cornerRadius, ui.theme, 0.55f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, origin.y + 8.0f));
    if (ui.fontSmall) ImGui::PushFont(ui.fontSmall);
    ImGui::TextColored(ui.theme.textSecondary, "%s", feTr("Frame").c_str());
    if (ui.fontSmall) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
    ImGui::InputText("##editorFilterFrame", filterFrameBuf, sizeof(filterFrameBuf), ImGuiInputTextFlags_CharsDecimal);

    ImGui::SameLine(0, 12.0f);
    auto playerPreview = filterPlayer == 1 ? feTr("P1") : (filterPlayer == 2 ? feTr("P2") : feTr("All Players"));
    ImGui::SetNextItemWidth(116.0f);
    if (ImGui::BeginCombo("##editorFilterPlayer", playerPreview.c_str())) {
        if (ImGui::Selectable(feTr("All Players").c_str(), filterPlayer == 0)) filterPlayer = 0;
        if (ImGui::Selectable(feTr("P1").c_str(), filterPlayer == 1)) filterPlayer = 1;
        if (ImGui::Selectable(feTr("P2").c_str(), filterPlayer == 2)) filterPlayer = 2;
        ImGui::EndCombo();
    }

    ImGui::SameLine(0, 8.0f);
    auto actionPreview = filterAction < 0 ? feTr("All Buttons") : feActionComboLabel(filterAction);
    ImGui::SetNextItemWidth(126.0f);
    if (ImGui::BeginCombo("##editorFilterAction", actionPreview.c_str())) {
        if (ImGui::Selectable(feTr("All Buttons").c_str(), filterAction < 0)) filterAction = -1;
        for (int i = 0; i < 4; ++i) {
            int action = feInputActionFromComboIndex(i);
            auto label = feActionComboLabel(action);
            if (ImGui::Selectable(label.c_str(), filterAction == action)) {
                filterAction = action;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0, 8.0f);
    ImGui::Checkbox(feTr("Presses").c_str(), &showPressRows);
    ImGui::SameLine(0, 8.0f);
    ImGui::Checkbox(feTr("Holds").c_str(), &showHoldRows);
    ImGui::PopStyleVar();

    float rightX = origin.x + width - 160.0f;
    if (rightX > ImGui::GetCursorScreenPos().x + 16.0f) {
        ImGui::SetCursorScreenPos(ImVec2(rightX, origin.y + 8.0f));
        ImGui::SetNextItemWidth(72.0f);
        bool submitted = ImGui::InputText("##editorGoFrame", goToFrameBuf, sizeof(goToFrameBuf),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine(0, 6.0f);
        bool clicked = Widgets::StyledButton("Go##editorGo", ImVec2(54.0f, 24.0f), ui.theme, ui.anim, 4.0f);
        if (submitted || clicked) {
            int frame = 0;
            if (feParseInt(goToFrameBuf, frame)) {
                selectNearestFrame(frame);
                std::snprintf(filterFrameBuf, sizeof(filterFrameBuf), "%d", frame);
            }
        }
    }
}

void FrameEditor::drawSegmentList(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    feDrawSolidRect(dl, origin, ImVec2(origin.x + width, origin.y + height), ui.theme.cornerRadius, ui.theme, 0.42f);

    std::vector<int> visible;
    visible.reserve(segments.size());

    int frameFilter = 0;
    bool hasFrameFilter = feParseInt(filterFrameBuf, frameFilter);
    for (int i = 0; i < static_cast<int>(segments.size()); ++i) {
        auto const& seg = segments[i];
        if (filterPlayer == 1 && seg.player2) continue;
        if (filterPlayer == 2 && !seg.player2) continue;
        if (filterAction >= 0 && seg.actionType != filterAction) continue;
        if (!showPressRows && !seg.hasRelease) continue;
        if (!showHoldRows && seg.hasRelease) continue;
        if (hasFrameFilter && (frameFilter < seg.startFrame || frameFilter > seg.endFrame)) continue;
        visible.push_back(i);
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 6.0f, origin.y + 6.0f));
    ImVec2 tableSize(std::max(20.0f, width - 12.0f), std::max(20.0f, height - 12.0f));
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, feToU32(feWithAlpha(ui.theme.getAccent(), 0.16f)));
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, feToU32(ImVec4(0.04f, 0.04f, 0.055f, 0.25f)));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, feToU32(ImVec4(0.08f, 0.08f, 0.10f, 0.24f)));
    if (ImGui::BeginTable("##macroEditorList", 9, flags, tableSize)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 42.0f);
        ImGui::TableSetupColumn(feTr("Start").c_str(), ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn(feTr("End").c_str(), ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn(feTr("Duration").c_str(), ImGuiTableColumnFlags_WidthFixed, 82.0f);
        ImGui::TableSetupColumn(feTr("Time").c_str(), ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn(feTr("Player").c_str(), ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn(feTr("Button").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(feTr("Kind").c_str(), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn(feTr("Offset").c_str(), ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                int segIndex = visible[row];
                auto const& seg = segments[segIndex];
                bool selected = segIndex == selectedSegment;
                auto playerColor = seg.player2 ? p2Color : ui.theme.getAccent();
                auto actionColor = feActionColor(seg.actionType, playerColor);

                ImGui::TableNextRow(ImGuiTableRowFlags_None, 26.0f);
                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ui.theme.getAccentU32(0.20f));
                }

                ImGui::TableSetColumnIndex(0);
                std::string rowLabel = fmt::format("{}##seg{}", row + 1, segIndex);
                if (ImGui::Selectable(rowLabel.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                    ImVec2(0.0f, 22.0f))) {
                    selectSegment(segIndex);
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(feTr("Duplicate").c_str())) {
                        selectSegment(segIndex);
                        duplicateSelectedSegment();
                    }
                    if (ImGui::MenuItem(feTr("Delete").c_str())) {
                        selectSegment(segIndex);
                        deleteSelectedSegment();
                    }
                    if (ImGui::MenuItem(feTr("Focus Frame").c_str())) {
                        std::snprintf(filterFrameBuf, sizeof(filterFrameBuf), "%d", seg.startFrame);
                        selectSegment(segIndex);
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", seg.startFrame);
                ImGui::TableSetColumnIndex(2);
                if (seg.hasRelease) ImGui::Text("%d", seg.endFrame);
                else ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(3);
                if (seg.hasRelease) ImGui::Text("%d", std::max<int32_t>(0, seg.endFrame - seg.startFrame));
                else ImGui::TextDisabled("--");
                ImGui::TableSetColumnIndex(4);
                auto timeText = feFrameTimeLabel(seg.startFrame, framerate);
                ImGui::TextUnformatted(timeText.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextColored(playerColor, "%s", feTr(seg.player2 ? "P2" : "P1").c_str());
                ImGui::TableSetColumnIndex(6);
                auto actionLabel = feActionLabel(seg.actionType);
                ImGui::TextColored(actionColor, "%s", actionLabel.c_str());
                ImGui::TableSetColumnIndex(7);
                auto kind = feSegmentKind(seg);
                ImGui::TextUnformatted(kind.c_str());
                ImGui::TableSetColumnIndex(8);
                float offset = 0.0f;
                if (seg.pressIndex < inputs.size()) offset = inputs[seg.pressIndex].stepOffset;
                if (std::abs(offset) > 0.0001f) ImGui::Text("%.2f", offset);
                else ImGui::TextDisabled("0");
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleColor(3);

    if (visible.empty()) {
        auto msg = segments.empty() ? feTr("No inputs to edit") : feTr("No matching inputs");
        ImVec2 textSize = ImGui::CalcTextSize(msg.c_str());
        dl->AddText(
            ImVec2(origin.x + (width - textSize.x) * 0.5f, origin.y + height * 0.5f - textSize.y * 0.5f),
            ui.theme.getTextSecondaryU32(),
            msg.c_str()
        );
    }
}

void FrameEditor::drawInspector(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    feDrawSolidRect(dl, origin, ImVec2(origin.x + width, origin.y + height), ui.theme.cornerRadius, ui.theme, 0.55f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, origin.y + 8.0f));
    ImGui::TextColored(ui.theme.getAccent(), "%s", feTr("Selected Input").c_str());

    if (selectedSegment < 0 || selectedSegment >= static_cast<int>(segments.size())) {
        auto hint = feTr("Select a row to edit it");
        dl->AddText(ImVec2(origin.x + 10.0f, origin.y + 42.0f), ui.theme.getTextSecondaryU32(), hint.c_str());
        return;
    }

    auto seg = segments[selectedSegment];
    bool hasRelease = seg.hasRelease && seg.releaseIndex != seg.pressIndex;
    float rowY = origin.y + 34.0f;
    float x = origin.x + 10.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));

    ImGui::SetCursorScreenPos(ImVec2(x, rowY));
    ImGui::Text("%s", feTr("Start").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    bool startSubmitted = ImGui::InputText("##selectedStart", selectedFrameBuf, sizeof(selectedFrameBuf),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
    if (startSubmitted || ImGui::IsItemDeactivatedAfterEdit()) {
        commitSelectedStartFrameText();
    }

    ImGui::SameLine(0, 10.0f);
    ImGui::Text("%s", feTr("End").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasRelease);
    ImGui::SetNextItemWidth(70.0f);
    bool endSubmitted = ImGui::InputText("##selectedEnd", selectedEndFrameBuf, sizeof(selectedEndFrameBuf),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
    if (endSubmitted || ImGui::IsItemDeactivatedAfterEdit()) {
        commitSelectedEndFrameText();
    }
    ImGui::EndDisabled();

    ImGui::SameLine(0, 10.0f);
    ImGui::Text("%s", feTr("Duration").c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasRelease);
    ImGui::SetNextItemWidth(70.0f);
    bool durationSubmitted = ImGui::InputText("##selectedDuration", selectedDurationBuf, sizeof(selectedDurationBuf),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal);
    if (durationSubmitted || ImGui::IsItemDeactivatedAfterEdit()) {
        commitSelectedDurationText();
    }
    ImGui::EndDisabled();

    rowY += 34.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, rowY));
    ImGui::Text("%s", feTr("Button").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(118.0f);
    auto actionPreview = feActionComboLabel(seg.actionType);
    if (ImGui::BeginCombo("##selectedAction", actionPreview.c_str())) {
        for (int i = 0; i < 4; ++i) {
            int action = feInputActionFromComboIndex(i);
            auto label = feActionComboLabel(action);
            if (ImGui::Selectable(label.c_str(), seg.actionType == action)) {
                setSelectedActionType(action);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0, 10.0f);
    ImGui::Text("%s", feTr("Player").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(82.0f);
    auto playerPreview = feTr(seg.player2 ? "P2" : "P1");
    if (ImGui::BeginCombo("##selectedPlayer", playerPreview.c_str())) {
        if (ImGui::Selectable(feTr("P1").c_str(), !seg.player2)) setSelectedPlayer(false);
        if (ImGui::Selectable(feTr("P2").c_str(), seg.player2)) setSelectedPlayer(true);
        ImGui::EndCombo();
    }

    ImGui::SameLine(0, 10.0f);
    ImGui::Text("%s", feTr("Kind").c_str());
    ImGui::SameLine();
    auto kind = feSegmentKind(seg);
    ImGui::TextColored(ui.theme.textSecondary, "%s", kind.c_str());

    rowY += 36.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, rowY));
    if (Widgets::StyledButton("-10##nudgeLeft10", ImVec2(42.0f, 25.0f), ui.theme, ui.anim, 4.0f)) nudgeSelected(-10);
    ImGui::SameLine(0, 5.0f);
    if (Widgets::StyledButton("-1##nudgeLeft1", ImVec2(36.0f, 25.0f), ui.theme, ui.anim, 4.0f)) nudgeSelected(-1);
    ImGui::SameLine(0, 5.0f);
    if (Widgets::StyledButton("+1##nudgeRight1", ImVec2(36.0f, 25.0f), ui.theme, ui.anim, 4.0f)) nudgeSelected(1);
    ImGui::SameLine(0, 5.0f);
    if (Widgets::StyledButton("+10##nudgeRight10", ImVec2(42.0f, 25.0f), ui.theme, ui.anim, 4.0f)) nudgeSelected(10);
    ImGui::SameLine(0, 10.0f);
    if (Widgets::StyledButton("Duplicate##selectedDuplicate", ImVec2(82.0f, 25.0f), ui.theme, ui.anim, 4.0f)) duplicateSelectedSegment();
    ImGui::SameLine(0, 5.0f);
    if (!hasRelease) {
        if (Widgets::StyledButton("Add Release##selectedRelease", ImVec2(86.0f, 25.0f), ui.theme, ui.anim, 4.0f)) createReleaseForSelected();
        ImGui::SameLine(0, 5.0f);
    }
    if (Widgets::StyledButton("Delete##selectedDelete", ImVec2(64.0f, 25.0f), ui.theme, ui.anim, 4.0f)) deleteSelectedSegment();

    ImGui::PopStyleVar();
}

void FrameEditor::drawAddPanel(MenuInterface& ui, ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    feDrawSolidRect(dl, origin, ImVec2(origin.x + width, origin.y + height), ui.theme.cornerRadius, ui.theme, 0.55f);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, origin.y + 8.0f));
    ImGui::TextColored(ui.theme.getAccent(), "%s", feTr("Add Input").c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));

    float rowY = origin.y + 34.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, rowY));
    ImGui::Text("%s", feTr("Frame").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(68.0f);
    ImGui::InputText("##addFrame", addFrameBuf, sizeof(addFrameBuf), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine(0, 8.0f);
    ImGui::Text("%s", feTr("Duration").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(58.0f);
    ImGui::InputText("##addDuration", addDurationBuf, sizeof(addDurationBuf), ImGuiInputTextFlags_CharsDecimal);

    rowY += 34.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, rowY));
    ImGui::SetNextItemWidth(std::max(90.0f, width * 0.48f));
    auto actionPreview = feActionComboLabel(addActionType);
    if (ImGui::BeginCombo("##addAction", actionPreview.c_str())) {
        for (int i = 0; i < 4; ++i) {
            int action = feInputActionFromComboIndex(i);
            auto label = feActionComboLabel(action);
            if (ImGui::Selectable(label.c_str(), addActionType == action)) {
                addActionType = action;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 8.0f);
    ImGui::SetNextItemWidth(72.0f);
    auto playerPreview = feTr(addPlayer == 1 ? "P2" : "P1");
    if (ImGui::BeginCombo("##addPlayer", playerPreview.c_str())) {
        if (ImGui::Selectable(feTr("P1").c_str(), addPlayer == 0)) addPlayer = 0;
        if (ImGui::Selectable(feTr("P2").c_str(), addPlayer == 1)) addPlayer = 1;
        ImGui::EndCombo();
    }

    rowY += 36.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 10.0f, rowY));
    float buttonW = std::max(72.0f, (width - 28.0f) * 0.5f);
    if (Widgets::StyledButton("Add Tap##addTap", ImVec2(buttonW, 25.0f), ui.theme, ui.anim, 4.0f)) {
        addSegmentFromControls(true);
    }
    ImGui::SameLine(0, 8.0f);
    if (Widgets::StyledButton("Add Press##addPress", ImVec2(buttonW, 25.0f), ui.theme, ui.anim, 4.0f)) {
        addSegmentFromControls(false);
    }

    ImGui::PopStyleVar();

    if (width < 260.0f) {
        auto caption = feTr("Tap creates a press and release");
        dl->AddText(ImVec2(origin.x + 10.0f, origin.y + height - 18.0f), ui.theme.getTextSecondaryU32(), caption.c_str());
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

        if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
            commitSelectedStartFrameText();
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
