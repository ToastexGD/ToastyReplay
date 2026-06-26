#include "gui/cocos/tr_frame_editor_popup.hpp"

#include "gui/cocos/cells/cells.hpp"
#include "utils.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace toasty::frontend {

namespace {
    constexpr float kPopupWidth = 420.f;
    constexpr float kPopupHeight = 300.f;
    constexpr float kScrollWidth = 394.f;
    constexpr float kScrollHeight = 250.f;
    constexpr int kMaxVisibleRows = 80;

    const char* const kActionNames[4] = { "Jump", "Left", "Right", "Button 0" };
    const int kActionTypes[4] = { 1, 2, 3, 0 };

    int actionIndexFor(int actionType) {
        for (int i = 0; i < 4; ++i) {
            if (kActionTypes[i] == actionType) {
                return i;
            }
        }
        return 0;
    }

    std::string actionName(int actionType) {
        for (int i = 0; i < 4; ++i) {
            if (kActionTypes[i] == actionType) {
                return kActionNames[i];
            }
        }
        return "Button " + std::to_string(actionType);
    }

    std::string segmentKind(HoldSegment const& seg) {
        if (!seg.hasRelease) {
            return "Press";
        }
        if (seg.endFrame - seg.startFrame <= 1) {
            return "Tap";
        }
        return "Hold";
    }

    CCMenuItemSpriteExtra* makeSmallButton(const char* label, const char* bg, std::function<void()> callback) {
        auto* spr = ButtonSprite::create(label, 30, 0, 0.5f, false, "bigFont.fnt", bg, 24.f);
        return geode::cocos::CCMenuItemExt::createSpriteExtra(spr, [callback](CCMenuItemSpriteExtra*) {
            callback();
        });
    }

    CCNode* makeButtonRow(std::vector<CCMenuItemSpriteExtra*> const& items) {
        auto* row = CCNode::create();
        row->setContentSize({ kCellWidth, 30.f });
        row->setAnchorPoint({ 0.5f, 0.5f });
        auto* menu = CCMenu::create();
        menu->setContentSize({ kCellWidth, 30.f });
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({ 0.5f, 0.5f });
        menu->setPosition({ kCellWidth * 0.5f, 15.f });
        for (auto* item : items) {
            menu->addChild(item);
        }
        menu->setLayout(RowLayout::create()->setGap(8.f)->setAxisAlignment(AxisAlignment::Center)->setAutoScale(false));
        row->addChild(menu);
        return row;
    }
}

TRFrameEditorPopup::~TRFrameEditorPopup() {
    m_editor.close();
}

bool TRFrameEditorPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight, "GJ_square01.png")) {
        return false;
    }
    this->setID("cocos-frame-editor"_spr);
    if (m_closeBtn) {
        m_closeBtn->setScale(0.8f);
    }

    auto* title = CCLabelBMFont::create("Macro Editor", "goldFont.fnt");
    title->setScale(0.55f);
    title->setPosition({ kPopupWidth * 0.5f, kPopupHeight - 20.f });
    m_mainLayer->addChild(title, 10);

    auto* panel = CCScale9Sprite::create("GJ_square02.png");
    panel->setContentSize({ 404.f, kScrollHeight + 8.f });
    panel->setPosition({ kPopupWidth * 0.5f, kScrollHeight * 0.5f + 14.f });
    panel->setColor(ccc3(0, 0, 0));
    panel->setOpacity(55);
    m_mainLayer->addChild(panel, 1);

    m_scroll = ScrollLayer::create({ kScrollWidth, kScrollHeight });
    m_scroll->setPosition({ (kPopupWidth - kScrollWidth) * 0.5f, 10.f });
    m_scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
    m_mainLayer->addChild(m_scroll, 2);

    rebuild();
    return true;
}

void TRFrameEditorPopup::rebuild() {
    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildrenWithCleanup(true);

    buildToolbar(content);
    buildInspector(content);
    buildFilters(content);
    buildAddPanel(content);
    buildSegmentList(content);

    content->updateLayout();
    m_scroll->scrollToTop();
}

void TRFrameEditorPopup::buildToolbar(CCNode* content) {
    std::vector<CCMenuItemSpriteExtra*> items;

    items.push_back(makeSmallButton("< Back", "GJ_button_04.png", [this]() {
        requestBack();
    }));
    items.push_back(makeSmallButton("Save", "GJ_button_01.png", [this]() {
        bool wasDirty = m_editor.dirty;
        m_editor.save();
        Notification::create(wasDirty ? "Saved" : "No changes", wasDirty ? NotificationIcon::Success : NotificationIcon::Info, 0.9f)->show();
        rebuild();
    }));
    items.push_back(makeSmallButton("Undo", "GJ_button_04.png", [this]() {
        if (m_editor.undoIndex >= 0) {
            m_editor.undo();
            rebuild();
        }
    }));
    items.push_back(makeSmallButton("Redo", "GJ_button_04.png", [this]() {
        if (m_editor.undoIndex + 2 < static_cast<int>(m_editor.undoStack.size())) {
            m_editor.redo();
            rebuild();
        }
    }));

    content->addChild(makeButtonRow(items));

    std::string status = m_editor.macroName + "  -  " + std::to_string(m_editor.inputs.size()) + " inputs";
    if (m_editor.dirty) {
        status += "  *";
    }
    content->addChild(SectionHeaderCell::create(status));
}

void TRFrameEditorPopup::buildInspector(CCNode* content) {
    int sel = m_editor.selectedSegment;
    if (sel < 0 || sel >= static_cast<int>(m_editor.segments.size())) {
        content->addChild(SectionHeaderCell::create("Selected: tap a row below"));
        return;
    }

    auto seg = m_editor.segments[sel];
    bool hasRelease = seg.hasRelease && seg.releaseIndex != seg.pressIndex;

    content->addChild(SectionHeaderCell::create("Selected Input"));
    content->addChild(InputCell::create("Start frame", std::to_string(seg.startFrame), "0", true, [this](std::string const& text) {
        if (auto value = toasty::parseInteger<int>(text)) {
            m_editor.setSelectedStartFrame(*value);
        }
    }));
    if (hasRelease) {
        content->addChild(InputCell::create("End frame", std::to_string(seg.endFrame), "0", true, [this](std::string const& text) {
            if (auto value = toasty::parseInteger<int>(text)) {
                m_editor.setSelectedEndFrame(*value);
            }
        }));
    }

    std::vector<std::string> actionOptions(kActionNames, kActionNames + 4);
    content->addChild(ComboCell::create("Button", actionOptions, actionIndexFor(seg.actionType), [this](int idx) {
        m_editor.setSelectedActionType(kActionTypes[idx]);
        rebuild();
    }));
    content->addChild(ComboCell::create("Player", { "P1", "P2" }, seg.player2 ? 1 : 0, [this](int idx) {
        m_editor.setSelectedPlayer(idx == 1);
        rebuild();
    }));

    std::vector<CCMenuItemSpriteExtra*> nudge;
    nudge.push_back(makeSmallButton("-10", "GJ_button_04.png", [this]() { m_editor.nudgeSelected(-10); rebuild(); }));
    nudge.push_back(makeSmallButton("-1", "GJ_button_04.png", [this]() { m_editor.nudgeSelected(-1); rebuild(); }));
    nudge.push_back(makeSmallButton("+1", "GJ_button_04.png", [this]() { m_editor.nudgeSelected(1); rebuild(); }));
    nudge.push_back(makeSmallButton("+10", "GJ_button_04.png", [this]() { m_editor.nudgeSelected(10); rebuild(); }));
    content->addChild(makeButtonRow(nudge));

    std::vector<CCMenuItemSpriteExtra*> actions;
    actions.push_back(makeSmallButton("Duplicate", "GJ_button_04.png", [this]() { m_editor.duplicateSelectedSegment(); rebuild(); }));
    if (!hasRelease) {
        actions.push_back(makeSmallButton("Add Release", "GJ_button_04.png", [this]() { m_editor.createReleaseForSelected(); rebuild(); }));
    }
    actions.push_back(makeSmallButton("Delete", "GJ_button_06.png", [this]() { m_editor.deleteSelectedSegment(); rebuild(); }));
    content->addChild(makeButtonRow(actions));
}

void TRFrameEditorPopup::buildFilters(CCNode* content) {
    content->addChild(SectionHeaderCell::create("Filters"));
    content->addChild(ComboCell::create("Player", { "All", "P1", "P2" }, m_editor.filterPlayer, [this](int idx) {
        m_editor.filterPlayer = idx;
        rebuild();
    }));

    std::vector<std::string> actionFilter = { "All", "Jump", "Left", "Right", "Button 0" };
    int filterIndex = 0;
    if (m_editor.filterAction >= 0) {
        filterIndex = actionIndexFor(m_editor.filterAction) + 1;
    }
    content->addChild(ComboCell::create("Button", actionFilter, filterIndex, [this](int idx) {
        m_editor.filterAction = (idx == 0) ? -1 : kActionTypes[idx - 1];
        rebuild();
    }));
    content->addChild(ToggleCell::create("Show Presses", "", m_editor.showPressRows, [this](bool value) {
        m_editor.showPressRows = value;
        rebuild();
    }));
    content->addChild(ToggleCell::create("Show Holds", "", m_editor.showHoldRows, [this](bool value) {
        m_editor.showHoldRows = value;
        rebuild();
    }));
    content->addChild(InputCell::create("Jump to Frame", std::string(m_editor.filterFrameBuf), "any", true, [this](std::string const& text) {
        std::strncpy(m_editor.filterFrameBuf, text.c_str(), sizeof(m_editor.filterFrameBuf) - 1);
        m_editor.filterFrameBuf[sizeof(m_editor.filterFrameBuf) - 1] = '\0';
    }));
    content->addChild(ButtonCell::create(
        m_editor.filterFrameBuf[0] != '\0' ? "Frame Filter (active)" : "Frame Filter",
        m_editor.filterFrameBuf[0] != '\0' ? "Clear" : "Apply",
        [this]() {
            if (m_editor.filterFrameBuf[0] != '\0') {
                m_editor.filterFrameBuf[0] = '\0';
            }
            rebuild();
        }));
}

void TRFrameEditorPopup::buildAddPanel(CCNode* content) {
    content->addChild(SectionHeaderCell::create("Add Input"));
    content->addChild(InputCell::create("Frame", std::string(m_editor.addFrameBuf), "0", true, [this](std::string const& text) {
        std::snprintf(m_editor.addFrameBuf, sizeof(m_editor.addFrameBuf), "%s", text.c_str());
    }));
    content->addChild(InputCell::create("Duration", std::string(m_editor.addDurationBuf), "1", true, [this](std::string const& text) {
        std::snprintf(m_editor.addDurationBuf, sizeof(m_editor.addDurationBuf), "%s", text.c_str());
    }));

    std::vector<std::string> actionOptions(kActionNames, kActionNames + 4);
    content->addChild(ComboCell::create("Button", actionOptions, actionIndexFor(m_editor.addActionType), [this](int idx) {
        m_editor.addActionType = kActionTypes[idx];
    }));
    content->addChild(ComboCell::create("Player", { "P1", "P2" }, m_editor.addPlayer, [this](int idx) {
        m_editor.addPlayer = idx;
    }));

    std::vector<CCMenuItemSpriteExtra*> items;
    items.push_back(makeSmallButton("Add Tap", "GJ_button_01.png", [this]() {
        m_editor.addSegmentFromControls(true);
        rebuild();
    }));
    items.push_back(makeSmallButton("Add Press", "GJ_button_01.png", [this]() {
        m_editor.addSegmentFromControls(false);
        rebuild();
    }));
    content->addChild(makeButtonRow(items));
}

CCNode* TRFrameEditorPopup::makeSegmentRow(int segmentIndex) {
    auto const& seg = m_editor.segments[segmentIndex];
    bool selected = (segmentIndex == m_editor.selectedSegment);

    auto* container = CCNode::create();
    container->setContentSize({ kCellWidth, 22.f });
    container->setAnchorPoint({ 0.5f, 0.5f });

    auto* bg = CCLayerColor::create(
        selected ? ccc4(70, 130, 200, 150) : ccc4(0, 0, 0, 90),
        kCellWidth, 22.f);
    container->addChild(bg);

    std::string text = "#" + std::to_string(segmentIndex + 1) + "  f" + std::to_string(seg.startFrame);
    if (seg.hasRelease) {
        text += "-" + std::to_string(seg.endFrame);
    } else {
        text += "+";
    }
    text += "  " + actionName(seg.actionType);
    text += "  " + std::string(seg.player2 ? "P2" : "P1");
    text += "  " + segmentKind(seg);

    auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setAnchorPoint({ 0.f, 0.5f });
    label->setScale(0.4f);
    label->limitLabelWidth(kCellWidth - 20.f, 0.4f, 0.16f);
    label->setPosition({ 12.f, 11.f });
    label->setColor(seg.player2 ? ccc3(120, 190, 255) : ccc3(245, 245, 245));
    container->addChild(label);

    auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(container, [this, segmentIndex](CCMenuItemSpriteExtra*) {
        m_editor.selectSegment(segmentIndex);
        rebuild();
    });
    auto* menu = CCMenu::create();
    menu->setContentSize({ kCellWidth, 22.f });
    menu->ignoreAnchorPointForPosition(false);
    menu->setAnchorPoint({ 0.5f, 0.5f });
    menu->setPosition({ kCellWidth * 0.5f, 11.f });
    item->setPosition({ kCellWidth * 0.5f, 11.f });
    menu->addChild(item);

    return menu;
}

void TRFrameEditorPopup::buildSegmentList(CCNode* content) {
    bool hasFrameFilter = m_editor.filterFrameBuf[0] != '\0';
    int frameFilter = hasFrameFilter ? std::atoi(m_editor.filterFrameBuf) : 0;

    std::vector<int> visible;
    visible.reserve(m_editor.segments.size());
    for (int i = 0; i < static_cast<int>(m_editor.segments.size()); ++i) {
        auto const& seg = m_editor.segments[i];
        if (m_editor.filterPlayer == 1 && seg.player2) continue;
        if (m_editor.filterPlayer == 2 && !seg.player2) continue;
        if (m_editor.filterAction >= 0 && seg.actionType != m_editor.filterAction) continue;
        if (!m_editor.showPressRows && !seg.hasRelease) continue;
        if (!m_editor.showHoldRows && seg.hasRelease) continue;
        if (hasFrameFilter && (frameFilter < seg.startFrame || frameFilter > seg.endFrame)) continue;
        visible.push_back(i);
    }

    content->addChild(SectionHeaderCell::create("Inputs (" + std::to_string(visible.size()) + " of " + std::to_string(m_editor.segments.size()) + ")"));

    if (visible.empty()) {
        content->addChild(SectionHeaderCell::create(m_editor.segments.empty() ? "(no inputs to edit)" : "(no matching inputs)"));
        return;
    }

    int totalPages = (static_cast<int>(visible.size()) + kMaxVisibleRows - 1) / kMaxVisibleRows;
    m_segmentPage = std::clamp(m_segmentPage, 0, std::max(0, totalPages - 1));
    int start = m_segmentPage * kMaxVisibleRows;
    int end = std::min(static_cast<int>(visible.size()), start + kMaxVisibleRows);

    for (int i = start; i < end; ++i) {
        content->addChild(makeSegmentRow(visible[i]));
    }

    if (totalPages > 1) {
        content->addChild(SectionHeaderCell::create("Page " + std::to_string(m_segmentPage + 1) + " / " + std::to_string(totalPages)));
        std::vector<CCMenuItemSpriteExtra*> items;
        items.push_back(makeSmallButton("< Prev", "GJ_button_04.png", [this]() {
            if (m_segmentPage > 0) {
                --m_segmentPage;
                rebuild();
            }
        }));
        items.push_back(makeSmallButton("Next >", "GJ_button_04.png", [this, totalPages]() {
            if (m_segmentPage < totalPages - 1) {
                ++m_segmentPage;
                rebuild();
            }
        }));
        content->addChild(makeButtonRow(items));
    }
}

void TRFrameEditorPopup::requestBack() {
    if (m_editor.dirty) {
        geode::createQuickPopup(
            "Discard Changes?",
            "You have unsaved edits. Discard them?",
            "Cancel",
            "Discard",
            [this](auto*, bool discard) {
                if (discard) {
                    m_editor.close();
                    this->removeFromParentAndCleanup(true);
                }
            }
        );
    } else {
        m_editor.close();
        this->removeFromParentAndCleanup(true);
    }
}

TRFrameEditorPopup* TRFrameEditorPopup::create(std::string const& name, bool isTTR) {
    auto* popup = new TRFrameEditorPopup();
    if (!popup) {
        return nullptr;
    }

    if (isTTR) {
        if (TTRMacro* loaded = TTRMacro::loadFromDisk(name)) {
            popup->m_editor.openTTR(name, loaded);
            delete loaded;
        }
    } else {
        if (MacroSequence* loaded = MacroSequence::loadFromDisk(name)) {
            popup->m_editor.openGDR(name, loaded);
            delete loaded;
        }
    }

    if (!popup->m_editor.active) {
        CC_SAFE_DELETE(popup);
        return nullptr;
    }

    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

}
