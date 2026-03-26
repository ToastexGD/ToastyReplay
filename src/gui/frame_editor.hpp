#ifndef _frame_editor_hpp
#define _frame_editor_hpp

#include <imgui-cocos.hpp>
#include <string>
#include <vector>
#include <cstdint>

#include "ttr_format.hpp"
#include "replay.hpp"

class MenuInterface;

struct EditorInput {
    int32_t frame = 0;
    int actionType = 0;
    bool player2 = false;
    bool pressed = false;
    float stepOffset = 0.0f;
    size_t originalIndex = 0;
};

struct HoldSegment {
    int32_t startFrame = 0;
    int32_t endFrame = 0;
    bool player2 = false;
    int actionType = 0;
    size_t pressIndex = 0;
    size_t releaseIndex = 0;
    bool hasRelease = true;
};

enum class EditorFormat { TTR, GDR };

enum class DragMode { None, PanBackground, MoveSegment, MoveEdgeLeft, MoveEdgeRight, OverviewPan };

struct UndoEntry {
    std::vector<EditorInput> inputs;
};

class FrameEditor {
public:
    bool active = false;
    bool dirty = false;
    std::string macroName;
    EditorFormat format = EditorFormat::TTR;

    std::vector<EditorInput> inputs;
    std::vector<EditorInput> originalInputs;
    std::vector<HoldSegment> segments;

    double framerate = 240.0;
    int32_t maxFrame = 0;

    float scrollX = 0.0f;
    float targetScrollX = 0.0f;
    float pixelsPerFrame = 4.0f;
    float targetPixelsPerFrame = 4.0f;

    int selectedSegment = -1;
    int hoveredSegment = -1;
    int selectedEdge = 0;

    DragMode dragMode = DragMode::None;
    float dragStartMouseX = 0.0f;
    float dragStartScrollX = 0.0f;
    int dragStartFrame = 0;
    int dragOriginalFrame = 0;
    int dragReleaseOrigFrame = 0;
    size_t dragPressIdx = 0;
    size_t dragReleaseIdx = 0;

    std::vector<UndoEntry> undoStack;
    int undoIndex = -1;

    char goToFrameBuf[16] = {0};
    char selectedFrameBuf[16] = {0};

    float openAnimation = 0.0f;
    bool confirmingDiscard = false;

    bool twoPlayerMode = false;
    ImVec4 p2Color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);

    TTRMacro* cachedTTR = nullptr;
    MacroSequence* cachedGDR = nullptr;

    void openTTR(const std::string& name, TTRMacro* macro);
    void openGDR(const std::string& name, MacroSequence* macro);
    void close();
    bool isActive() const;

    void draw(MenuInterface& ui);

private:
    void rebuildSegments();
    void pushUndo();
    void undo();
    void redo();

    void applyToTTR();
    void applyToGDR();

    void drawToolbar(MenuInterface& ui, ImVec2 origin, float width);
    void drawOverviewBar(MenuInterface& ui, ImVec2 origin, float width, float height);
    void drawRuler(MenuInterface& ui, ImVec2 origin, float width, float height);
    void drawLanes(MenuInterface& ui, ImVec2 origin, float width, float height);
    void drawScrollbar(MenuInterface& ui, ImVec2 origin, float width, float height);
    void drawDetailBar(MenuInterface& ui, ImVec2 origin, float width, float height);

    void handleZoom(float mouseX, float canvasOriginX, float canvasWidth);
    void handleDrag(float mouseX, float canvasOriginX);

    int32_t frameAtPixel(float pixelX, float canvasOriginX) const;
    float pixelAtFrame(int32_t frame, float canvasOriginX) const;
    int32_t visibleFrameStart() const;
    int32_t visibleFrameEnd(float canvasWidth) const;

    void computeP2Color(const ImVec4& accent);
    int hitTestSegment(ImVec2 mousePos, ImVec2 lanesOrigin, float lanesWidth, float lanesHeight, int& edgeOut) const;
    int findSegmentByPressIndex(size_t pressIdx) const;
};

#endif
