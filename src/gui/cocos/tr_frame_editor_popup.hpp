#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <string>

#include "gui/frame_editor.hpp"

namespace toasty::frontend {

class TRFrameEditorPopup : public geode::Popup {
protected:
    geode::ScrollLayer* m_scroll = nullptr;
    FrameEditor m_editor;
    int m_segmentPage = 0;

    bool init() override;
    void rebuild();
    void buildToolbar(cocos2d::CCNode* content);
    void buildInspector(cocos2d::CCNode* content);
    void buildFilters(cocos2d::CCNode* content);
    void buildAddPanel(cocos2d::CCNode* content);
    void buildSegmentList(cocos2d::CCNode* content);
    cocos2d::CCNode* makeSegmentRow(int segmentIndex);
    void requestBack();

public:
    ~TRFrameEditorPopup() override;
    static TRFrameEditorPopup* create(std::string const& name, bool isTTR);
};

}
