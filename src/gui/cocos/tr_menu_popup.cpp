#include "gui/cocos/tr_menu_popup.hpp"

#include "gui/cocos/cells/cells.hpp"
#include "gui/cocos/tr_frame_editor_popup.hpp"
#include "gui/cocos/tr_replay_actions_popup.hpp"
#include "gui/cocos/frontend.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "hacks/autoclicker.hpp"
#include "online/online_client.hpp"
#include "trajectory/trajectory.hpp"
#include "render/render_preset.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace toasty::frontend {

namespace {
    constexpr float kPopupWidth = 420.f;
    constexpr float kPopupHeight = 280.f;
    constexpr int kTabCount = 6;
    const char* const kTabs[kTabCount] = { "Main", "Render", "Clicks", "Autoclicker", "Settings", "Online" };

    constexpr float kTabWidth = 62.f;
    constexpr float kTabHeight = 30.f;
    constexpr float kTabPitch = 66.f;
    constexpr float kTabRowY = -52.f;

    CCNode* makePillButton(const char* label, ccColor3B color, float width, float height) {
        auto* container = CCNode::create();
        container->setContentSize({ width, height });
        container->setAnchorPoint({ 0.5f, 0.5f });

        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        CCSize natural = bg->getContentSize();
        constexpr float inset = 8.f;
        bg->setCapInsets(CCRect(inset, inset, std::max(1.f, natural.width - inset * 2.f), std::max(1.f, natural.height - inset * 2.f)));
        bg->setContentSize({ width, height });
        bg->setPosition({ width * 0.5f, height * 0.5f });
        bg->setColor(color);
        container->addChild(bg);

        auto* text = CCLabelBMFont::create(label, "bigFont.fnt");
        text->setAnchorPoint({ 0.5f, 0.5f });
        text->setPosition({ width * 0.5f, height * 0.5f });
        text->limitLabelWidth(width - 10.f, 0.42f, 0.18f);
        container->addChild(text);

        return container;
    }
}

bool TRMenuPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight, "GJ_square01.png")) {
        return false;
    }

    this->setID("cocos-menu"_spr);

    if (m_closeBtn) {
        m_closeBtn->setScale(0.8f);
    }

    {
        auto* title = CCLabelBMFont::create("ToastyReplay", "goldFont.fnt");
        title->setScale(0.6f);
        title->setAnchorPoint({ 0.f, 0.5f });
        float titleWidth = title->getScaledContentSize().width;

        CCSprite* logo = CCSprite::create("toastyreplay-logo.png"_spr);
        float logoWidth = 0.f;
        float gap = 0.f;
        if (logo) {
            logo->setScale(20.f / std::max(1.f, logo->getContentSize().height));
            logo->setAnchorPoint({ 0.f, 0.5f });
            logoWidth = logo->getScaledContentSize().width;
            gap = 8.f;
        }

        float totalWidth = logoWidth + gap + titleWidth;
        float startX = (kPopupWidth - totalWidth) * 0.5f;
        float headerY = kPopupHeight - 24.f;

        if (logo) {
            logo->setPosition({ startX, headerY });
            m_mainLayer->addChild(logo, 10);
            startX += logoWidth + gap;
        }
        title->setPosition({ startX, headerY });
        m_mainLayer->addChild(title, 10);
    }

    auto* panel = CCScale9Sprite::create("GJ_square02.png");
    panel->setContentSize({ 404.f, 192.f });
    panel->setPosition({ kPopupWidth * 0.5f, 100.f });
    panel->setColor(ccc3(0, 0, 0));
    panel->setOpacity(55);
    m_mainLayer->addChild(panel, 1);

    m_scroll = ScrollLayer::create({ 394.f, 182.f });
    m_scroll->setPosition({ (kPopupWidth - 394.f) * 0.5f, 9.f });
    m_scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
    m_mainLayer->addChild(m_scroll, 2);

    m_tabMenu = CCMenu::create();
    m_tabMenu->setContentSize(m_size);
    m_tabMenu->ignoreAnchorPointForPosition(false);
    m_tabMenu->setAnchorPoint({ 0.5f, 0.5f });
    m_tabMenu->setPosition({ m_size.width * 0.5f, m_size.height * 0.5f });
    m_mainLayer->addChild(m_tabMenu, 100);

    switchTab(0);
    return true;
}

void TRMenuPopup::buildTabBar() {
    m_tabMenu->removeAllChildrenWithCleanup(true);

    float startX = -kTabPitch * (kTabCount - 1) * 0.5f;

    for (int i = 0; i < kTabCount; ++i) {
        bool active = (i == m_activeTab);

        auto* container = CCNode::create();
        container->setContentSize({ kTabWidth, kTabHeight });
        container->setAnchorPoint({ 0.5f, 0.5f });

        const char* tex = active ? "GJ_button_01.png" : "GJ_button_04.png";
        auto* bg = CCScale9Sprite::create(tex);
        CCSize natural = bg->getContentSize();
        constexpr float inset = 10.f;
        bg->setCapInsets(CCRect(inset, inset, std::max(1.f, natural.width - inset * 2.f), std::max(1.f, natural.height - inset * 2.f)));
        bg->setContentSize({ kTabWidth, kTabHeight });
        bg->setPosition({ kTabWidth * 0.5f, kTabHeight * 0.5f });
        container->addChild(bg);

        auto* label = CCLabelBMFont::create(kTabs[i], "bigFont.fnt");
        label->setAnchorPoint({ 0.5f, 0.5f });
        label->setPosition({ kTabWidth * 0.5f, kTabHeight * 0.5f });
        label->setColor(ccc3(255, 255, 255));
        label->limitLabelWidth(kTabWidth - 12.f, 0.46f, 0.18f);
        container->addChild(label);

        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(container, [this, i](CCMenuItemSpriteExtra*) {
            this->switchTab(i);
        });
        m_tabMenu->addChildAtPosition(item, Anchor::Top, ccp(startX + i * kTabPitch, kTabRowY));
    }
}

void TRMenuPopup::switchTab(int index) {
    m_preserveScroll = (index == m_activeTab) && !m_subTabChanged;
    m_subTabChanged = false;
    m_activeTab = index;
    buildTabBar();
    buildTabContent(index);
}

void TRMenuPopup::addSub(CCNode* content, CCNode* cell, std::string const& ownerId) {
    if (!cell) {
        return;
    }
    static_cast<TRCell*>(cell)->applySubStyle();
    content->addChild(cell);
    if (!m_pendingExpandId.empty() && ownerId == m_pendingExpandId) {
        m_animateCells.push_back(cell);
    }
}

void TRMenuPopup::buildTabContent(int index) {
    auto* content = m_scroll->m_contentLayer;
    float prevScrollY = content->getPositionY();
    float prevContentHeight = content->getContentHeight();
    content->removeAllChildrenWithCleanup(true);
    m_animateCells.clear();

    if (index == 0) {
        buildMainTab(content);
    } else if (index == 1) {
        buildRenderTab(content);
    } else if (index == 2) {
        buildClicksTab(content);
    } else if (index == 3) {
        buildAutoclickerTab(content);
    } else if (index == 4) {
        buildSettingsTab(content);
    } else if (index == 5) {
        buildOnlineTab(content);
    } else {
        content->addChild(SectionHeaderCell::create(std::string(kTabs[index]) + " - coming soon"));
    }

    content->updateLayout();
    if (m_preserveScroll) {
        float newContentHeight = content->getContentHeight();
        content->setPositionY(prevScrollY + (prevContentHeight - newContentHeight));
    } else {
        m_scroll->scrollToTop();
    }

    if (!m_animateCells.empty()) {
        int order = 0;
        for (auto* cell : m_animateCells) {
            float targetX = cell->getPositionX();
            float targetY = cell->getPositionY();
            cell->setPositionX(targetX - 28.f);
            auto* slide = CCEaseOut::create(CCMoveTo::create(0.18f, ccp(targetX, targetY)), 2.0f);
            cell->runAction(CCSequence::create(CCDelayTime::create(0.025f * order), slide, nullptr));
            ++order;
        }
    }
    m_animateCells.clear();
    m_pendingExpandId.clear();
}

void TRMenuPopup::buildMainTab(CCNode* content) {
    auto* row = CCNode::create();
    row->setContentSize({ kCellWidth, 26.f });
    row->setAnchorPoint({ 0.5f, 0.5f });

    auto* subMenu = CCMenu::create();
    subMenu->setContentSize({ 0.f, 0.f });
    subMenu->setPosition({ kCellWidth * 0.5f, 13.f });
    row->addChild(subMenu);

    bool separatoryEnabled = false;
    std::vector<const char*> subNames = { "Replay", "Hacks", "Tools" };
    int subCount = static_cast<int>(subNames.size());
    m_mainSubTab = std::clamp(m_mainSubTab, 0, subCount - 1);

    constexpr float subPitch = 66.f;
    for (int i = 0; i < subCount; ++i) {
        bool active = (i == m_mainSubTab);
        auto* pill = makePillButton(subNames[i], active ? ccc3(95, 190, 240) : ccc3(135, 140, 152), 60.f, 22.f);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(pill, [this, i](CCMenuItemSpriteExtra*) {
            m_mainSubTab = i;
            m_subTabChanged = true;
            switchTab(0);
        });
        item->setPosition({ (static_cast<float>(i) - (subCount - 1) * 0.5f) * subPitch, 0.f });
        subMenu->addChild(item);
    }
    content->addChild(row);

    auto* divider = CCNode::create();
    divider->setContentSize({ kCellWidth, 10.f });
    divider->setAnchorPoint({ 0.5f, 0.5f });
    auto* line = CCLayerColor::create(ccc4(255, 255, 255, 45), kCellWidth - 36.f, 1.5f);
    line->setPosition({ 18.f, 4.f });
    divider->addChild(line);
    content->addChild(divider);

    if (m_mainSubTab == 1) {
        buildHacksSection(content);
    } else if (m_mainSubTab == 2) {
        buildToolsSection(content);
    } else if (m_mainSubTab == 3 && separatoryEnabled) {
        buildSeparatorySection(content);
    } else {
        buildReplaySection(content);
    }
}

void TRMenuPopup::buildHacksSection(CCNode* content) {
    content->addChild(SectionHeaderCell::create("Hacks tab is not yet available in the Cocos UI"));
}

void TRMenuPopup::addHackSubOptions(CCNode* content, std::string const& id) {
    (void)content; (void)id;
}

void TRMenuPopup::buildToolsSection(CCNode* content) {
    content->addChild(SectionHeaderCell::create("Tools tab is not yet available in the Cocos UI"));
}

void TRMenuPopup::buildScreenLabelsSection(CCNode* content) {
    content->addChild(SectionHeaderCell::create("Screen labels are not available in Free"));
}

void TRMenuPopup::buildReplaySection(CCNode* content) {
    auto* engine = ReplayEngine::get();
    if (!engine) {
        content->addChild(SectionHeaderCell::create("Engine unavailable"));
        return;
    }

    auto* modeRow = CCNode::create();
    modeRow->setContentSize({ kCellWidth, 34.f });
    modeRow->setAnchorPoint({ 0.5f, 0.5f });
    auto* modeMenu = CCMenu::create();
    modeMenu->setContentSize({ 0.f, 0.f });
    modeMenu->setPosition({ kCellWidth * 0.5f, 17.f });
    modeRow->addChild(modeMenu);

    bool capturing = engine->engineMode == MODE_CAPTURE;
    bool playing = engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart;
    bool disabled = engine->engineMode == MODE_DISABLED;

    auto addMode = [&](const char* label, bool active, ccColor3B activeColor, std::function<void()> action, int slot) {
        auto* pill = makePillButton(label, active ? activeColor : ccc3(135, 140, 152), 84.f, 30.f);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(pill, [this, action](CCMenuItemSpriteExtra*) {
            action();
            switchTab(0);
        });
        item->setPosition({ (slot - 1) * 92.f, 0.f });
        modeMenu->addChild(item);
    };

    addMode("Disable", disabled, ccc3(225, 80, 80), []() {
        auto* e = ReplayEngine::get();
        e->pendingPlaybackStart = false;
        if (e->engineMode == MODE_CAPTURE) {
            e->discardActiveMacro();
            e->engineMode = MODE_DISABLED;
        } else if (e->engineMode == MODE_EXECUTE) {
            e->haltExecution();
        } else {
            e->engineMode = MODE_DISABLED;
        }
        e->clearStartPosWarning();
    }, 0);

    addMode("Record", capturing, ccc3(80, 200, 105), []() {
        auto* e = ReplayEngine::get();
        if (e->engineMode == MODE_CAPTURE) {
            return;
        }
        auto* layer = PlayLayer::get();
        if ((PlayLayer::get() != nullptr) && layer && layer->m_level) {
            bool previous = e->ttrMode;
            e->ttrMode = true;
            if (!e->beginCapture(layer->m_level)) {
                e->ttrMode = previous;
            }
        }
    }, 1);

    addMode("Playback", playing, ccc3(80, 150, 235), []() {
        auto* e = ReplayEngine::get();
        if ((e->engineMode == MODE_EXECUTE || e->pendingPlaybackStart)) {
            e->haltExecution();
            e->clearStartPosWarning();
        } else if (e->engineMode == MODE_CAPTURE) {
            e->setStartPosWarningKey("Stop recording before toggling playback.");
        } else if (e->hasMacroInputs()) {
            e->requestExecutionStart();
        } else {
            e->setStartPosWarningKey("Load a macro before toggling playback.");
        }
    }, 2);

    content->addChild(modeRow);

    content->addChild(ToggleCell::create("Intentional Death", "Keep progress through deaths during playback", engine->persistenceMode, [](bool value) {
        auto* e = ReplayEngine::get();
        e->persistenceMode = value;
        e->setPersistenceMode(value);
        toasty::frontend::persistSettings();
    }));

    std::string status;
    if (engine->engineMode == MODE_CAPTURE) {
        status = "Recording: " + engine->getMacroName();
    } else if (engine->hasMacro()) {
        status = "Loaded: " + engine->getMacroName();
        if (engine->pendingPlaybackStart) {
            status += "  [PENDING]";
        }
    } else {
        status = "No macro loaded";
    }
    content->addChild(SectionHeaderCell::create(status));

    if (engine->hasMacro() && engine->engineMode != MODE_CAPTURE) {
        content->addChild(InputCell::create("Tick Offset", std::to_string(engine->tickOffset), "0", false, [](std::string const& text) {
            if (auto value = toasty::parseInteger<int>(text)) {
                ReplayEngine::get()->tickOffset = *value;
            }
        }));
        if (engine->startPosActive) {
            content->addChild(SectionHeaderCell::create("Start Pos active (offset applied)"));
        }
    }

    if (engine->hasMacro()) {
        content->addChild(InputCell::create("Macro Name", engine->getMacroName(), "name", false, [](std::string const& text) {
            auto* e = ReplayEngine::get();
            if (e->ttrMode && e->activeTTR && e->activeTTR->name != text) {
                e->activeTTR->name = text;
                e->markActiveMacroDirty();
            }
        }));
        content->addChild(ButtonCell::create("Save Current Macro", "Save", [this]() {
            auto* e = ReplayEngine::get();
            if (e->saveActiveMacro()) {
                Notification::create("Saved macro", NotificationIcon::Success, 0.9f)->show();
                e->reloadMacroList();
            } else {
                Notification::create("Could not save macro", NotificationIcon::Error, 1.0f)->show();
            }
            switchTab(0);
        }));
    }

    content->addChild(SectionHeaderCell::create("Saved Macros"));

    engine->reloadMacroList();

    content->addChild(InputCell::create("Search", m_replayMacroFilter, "name", false, [this](std::string const& text) {
        m_replayMacroFilter = text;
    }));
    content->addChild(ButtonCell::create(
        m_replayMacroFilter.empty() ? "Filter" : "Filter (active)",
        m_replayMacroFilter.empty() ? "Apply" : "Clear",
        [this]() {
            m_replayPage = 0;
            if (!m_replayMacroFilter.empty()) {
                m_replayMacroFilter.clear();
            }
            switchTab(0);
        }));

    namespace fs = std::filesystem;
    auto directory = ReplayStorage::getReplayDirectoryPath();
    std::error_code ec;
    std::vector<std::pair<std::string, bool>> macros;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) {
            continue;
        }
        std::string ext = toasty::pathToUtf8(it->path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        bool isTTR = (ext == ".ttr" || ext == ".ttr2" || ext == ".ttr3");
        bool isGDR = (ext == ".gdr");
        if (!isTTR && !isGDR) {
            continue;
        }
        macros.push_back({ toasty::pathToUtf8(it->path().stem()), isTTR });
    }
    std::sort(macros.begin(), macros.end(), [](auto const& a, auto const& b) {
        return a.first < b.first;
    });

    std::string filterLower = m_replayMacroFilter;
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (macros.empty()) {
        content->addChild(SectionHeaderCell::create("(no saved macros yet)"));
        return;
    }

    auto badgesFor = [engine](std::string const& name, bool isTTR) {
        std::string badges;
        if (engine->ttr3Macros.count(name)) {
            badges += "  [TTR3]";
        } else if (engine->ttr2Macros.count(name)) {
            badges += "  [TTR2]";
        } else if (isTTR) {
            badges += "  [TTR]";
        } else {
            badges += "  [GDR]";
        }
        if (engine->cbsMacros.count(name)) {
            badges += " [CBS]";
        }
        if (engine->platformerMacros.count(name)) {
            badges += " [PLAT]";
        }
        return badges;
    };

    bool macroLoaded = engine->engineMode != MODE_CAPTURE && engine->hasMacro();
    std::string loadedName = macroLoaded ? engine->getMacroName() : std::string();

    std::vector<std::pair<std::string, bool>> filtered;
    filtered.reserve(macros.size());
    for (auto const& macro : macros) {
        if (!filterLower.empty()) {
            std::string nameLower = macro.first;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (nameLower.find(filterLower) == std::string::npos) {
                continue;
            }
        }
        filtered.push_back(macro);
    }

    if (filtered.empty()) {
        content->addChild(SectionHeaderCell::create("(no macros match search)"));
        return;
    }

    constexpr int kReplayPageSize = 15;
    int totalPages = (static_cast<int>(filtered.size()) + kReplayPageSize - 1) / kReplayPageSize;
    m_replayPage = std::clamp(m_replayPage, 0, std::max(0, totalPages - 1));
    int pageStart = m_replayPage * kReplayPageSize;
    int pageEnd = std::min(static_cast<int>(filtered.size()), pageStart + kReplayPageSize);

    for (int idx = pageStart; idx < pageEnd; ++idx) {
        std::string name = filtered[idx].first;
        bool isTTR = filtered[idx].second;

        bool loaded = macroLoaded && name == loadedName;

        auto* row = CCNode::create();
        row->setContentSize({ kCellWidth, 30.f });
        row->setAnchorPoint({ 0.5f, 0.5f });

        auto* bg = CCScale9Sprite::create("GJ_square05.png");
        CCSize natural = bg->getContentSize();
        constexpr float inset = 8.f;
        bg->setCapInsets(CCRect(inset, inset, std::max(1.f, natural.width - inset * 2.f), std::max(1.f, natural.height - inset * 2.f)));
        bg->setContentSize({ kCellWidth, 30.f });
        bg->setPosition({ kCellWidth * 0.5f, 15.f });
        bg->setColor(loaded ? ccc3(40, 80, 50) : ccc3(0, 0, 0));
        bg->setOpacity(loaded ? 120 : 60);
        row->addChild(bg, -1);

        std::string text = (loaded ? "* " : "") + name + badgesFor(name, isTTR);
        auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->setScale(0.42f);
        label->limitLabelWidth(kCellWidth - 150.f, 0.42f, 0.18f);
        label->setPosition({ 12.f, 15.f });
        row->addChild(label);

        auto* menu = CCMenu::create();
        menu->setContentSize({ 130.f, 30.f });
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({ 1.f, 0.5f });
        menu->setPosition({ kCellWidth - 8.f, 15.f });

        auto* loadSpr = ButtonSprite::create("Load", 30, 0, 0.5f, false, "bigFont.fnt", "GJ_button_01.png", 24.f);
        auto* loadItem = geode::cocos::CCMenuItemExt::createSpriteExtra(loadSpr, [this, name, isTTR](CCMenuItemSpriteExtra*) {
            auto* e = ReplayEngine::get();
            if (e->engineMode == MODE_CAPTURE) {
                Notification::create("Stop recording first", NotificationIcon::Warning, 0.8f)->show();
                return;
            }
            auto loadMacro = [this, name, isTTR]() {
                auto* eng = ReplayEngine::get();
                eng->pendingPlaybackStart = false;
                bool ok = false;
                if (isTTR) {
                    if (TTRMacro* loaded = TTRMacro::loadFromDisk(name)) {
                        eng->discardActiveMacro();
                        eng->activeTTR = loaded;
                        eng->ttrMode = true;
                        eng->clearActiveMacroDirty();
                        ok = true;
                    }
                } else {
                    if (MacroSequence* loaded = MacroSequence::loadFromDisk(name)) {
                        eng->discardActiveMacro();
                        eng->activeMacro = loaded;
                        eng->ttrMode = false;
                        eng->clearActiveMacroDirty();
                        ok = true;
                    }
                }
                Notification::create(ok ? ("Loaded " + name) : ("Failed to load " + name), ok ? NotificationIcon::Success : NotificationIcon::Error, 0.9f)->show();
                switchTab(0);
            };
            e->runWithUnsavedMacroGuard(std::move(loadMacro));
        });
        menu->addChild(loadItem);

        auto* actionsSpr = ButtonSprite::create("Actions", 30, 0, 0.5f, false, "bigFont.fnt", "GJ_button_04.png", 24.f);
        auto* actionsItem = geode::cocos::CCMenuItemExt::createSpriteExtra(actionsSpr, [this, name, isTTR](CCMenuItemSpriteExtra*) {
            auto onChanged = [this]() { switchTab(0); };
            auto onUpload = [this, name]() {
                auto* e = ReplayEngine::get();
                e->reloadMacroList();
                auto found = std::find(e->storedMacros.begin(), e->storedMacros.end(), name);
                if (found != e->storedMacros.end()) {
                    m_uploadMacroIndex = static_cast<int>(std::distance(e->storedMacros.begin(), found));
                }
                switchTab(5);
            };
            auto* popup = TRReplayActionsPopup::create(name, isTTR, onChanged, onUpload);
            if (popup) {
                popup->show();
            }
        });
        menu->addChild(actionsItem);

        menu->setLayout(RowLayout::create()->setGap(8.f)->setAxisAlignment(AxisAlignment::End)->setAutoScale(false));
        row->addChild(menu);
        content->addChild(row);
    }

    if (totalPages > 1) {
        content->addChild(SectionHeaderCell::create("Page " + std::to_string(m_replayPage + 1) + " / " + std::to_string(totalPages)));

        auto* pageRow = CCNode::create();
        pageRow->setContentSize({ kCellWidth, 30.f });
        pageRow->setAnchorPoint({ 0.5f, 0.5f });
        auto* pageMenu = CCMenu::create();
        pageMenu->setContentSize({ kCellWidth, 30.f });
        pageMenu->ignoreAnchorPointForPosition(false);
        pageMenu->setAnchorPoint({ 0.5f, 0.5f });
        pageMenu->setPosition({ kCellWidth * 0.5f, 15.f });

        auto* prevSpr = ButtonSprite::create("< Prev", 60, 0, 0.5f, false, "bigFont.fnt", "GJ_button_04.png", 24.f);
        auto* prevItem = geode::cocos::CCMenuItemExt::createSpriteExtra(prevSpr, [this](CCMenuItemSpriteExtra*) {
            if (m_replayPage > 0) {
                --m_replayPage;
                switchTab(0);
            }
        });
        pageMenu->addChild(prevItem);

        auto* nextSpr = ButtonSprite::create("Next >", 60, 0, 0.5f, false, "bigFont.fnt", "GJ_button_04.png", 24.f);
        auto* nextItem = geode::cocos::CCMenuItemExt::createSpriteExtra(nextSpr, [this, totalPages](CCMenuItemSpriteExtra*) {
            if (m_replayPage < totalPages - 1) {
                ++m_replayPage;
                switchTab(0);
            }
        });
        pageMenu->addChild(nextItem);

        pageMenu->setLayout(RowLayout::create()->setGap(12.f)->setAxisAlignment(AxisAlignment::Center)->setAutoScale(false));
        pageRow->addChild(pageMenu);
        content->addChild(pageRow);
    }
}

void TRMenuPopup::buildSeparatorySection(CCNode* content) {
    content->addChild(SectionHeaderCell::create("Separatory is a Pro feature"));
}

void TRMenuPopup::buildRenderTab(CCNode* content) {
    auto* mod = Mod::get();

    content->addChild(SectionHeaderCell::create("Output"));

    int width = static_cast<int>(mod->getSavedValue<int64_t>("render_width", 1920));
    int height = static_cast<int>(mod->getSavedValue<int64_t>("render_height", 1080));
    int fps = static_cast<int>(mod->getSavedValue<int64_t>("render_fps", 60));
    std::string bitrate = mod->getSavedValue<std::string>("render_bitrate", "40");

    content->addChild(InputCell::create("Width", std::to_string(width), "1920", true, [](std::string const& text) {
        if (auto value = toasty::parseInteger<int>(text)) {
            Mod::get()->setSavedValue<int64_t>("render_width", *value);
        }
    }));
    content->addChild(InputCell::create("Height", std::to_string(height), "1080", true, [](std::string const& text) {
        if (auto value = toasty::parseInteger<int>(text)) {
            Mod::get()->setSavedValue<int64_t>("render_height", *value);
        }
    }));
    content->addChild(InputCell::create("FPS", std::to_string(fps), "60", true, [](std::string const& text) {
        if (auto value = toasty::parseInteger<int>(text)) {
            Mod::get()->setSavedValue<int64_t>("render_fps", *value);
        }
    }));
    content->addChild(InputCell::create("Bitrate (Mbps)", bitrate, "40", true, [](std::string const& text) {
        Mod::get()->setSavedValue<std::string>("render_bitrate", text);
    }));

    content->addChild(SectionHeaderCell::create("Audio"));
    content->addChild(ToggleCell::create("Include Audio", "", mod->getSavedValue<bool>("render_include_audio", true), [](bool value) {
        Mod::get()->setSavedValue("render_include_audio", value);
    }));
    content->addChild(ToggleCell::create("Include Click Sounds", "", mod->getSavedValue<bool>("render_include_clicks", false), [](bool value) {
        Mod::get()->setSavedValue("render_include_clicks", value);
    }));
    content->addChild(SliderCell::create("SFX Volume", static_cast<float>(mod->getSavedValue<double>("render_sfx_volume", 1.0)), 0.0f, 1.0f, [](float value) {
        Mod::get()->setSavedValue<double>("render_sfx_volume", static_cast<double>(value));
    }));
    content->addChild(SliderCell::create("Music Volume", static_cast<float>(mod->getSavedValue<double>("render_music_volume", 1.0)), 0.0f, 1.0f, [](float value) {
        Mod::get()->setSavedValue<double>("render_music_volume", static_cast<double>(value));
    }));

    content->addChild(SectionHeaderCell::create("Output File"));
    content->addChild(InputCell::create("Render Name", mod->getSavedValue<std::string>("render_name", ""), "auto", false, [](std::string const& text) {
        Mod::get()->setSavedValue<std::string>("render_name", text);
    }));
    content->addChild(InputCell::create("Codec", mod->getSavedValue<std::string>("render_codec", ""), "auto", false, [](std::string const& text) {
        Mod::get()->setSavedValue<std::string>("render_codec", text);
    }));
    content->addChild(InputCell::create("Seconds After", mod->getSavedValue<std::string>("render_seconds_after", "3"), "3", true, [](std::string const& text) {
        Mod::get()->setSavedValue<std::string>("render_seconds_after", text);
    }));

    content->addChild(SectionHeaderCell::create("Endscreen"));
    content->addChild(ToggleCell::create("Hide Endscreen", "Hide the level end screen in the render", mod->getSavedValue<bool>("render_hide_endscreen", false), [](bool value) {
        Mod::get()->setSavedValue("render_hide_endscreen", value);
    }));
    content->addChild(ToggleCell::create("Hide Level Complete", "Hide the Level Complete text in the render", mod->getSavedValue<bool>("render_hide_levelcomplete", false), [](bool value) {
        Mod::get()->setSavedValue("render_hide_levelcomplete", value);
    }));

    content->addChild(SectionHeaderCell::create("Presets"));
    auto presetsDir = mod->getSaveDir() / "presets";
    auto presetNames = RenderPresetIO::listNames(presetsDir);

    content->addChild(InputCell::create("Preset Name", m_renderPresetName, "my preset", false, [this](std::string const& text) {
        m_renderPresetName = text;
    }));
    content->addChild(ButtonCell::create("Save Preset", "Save", [this]() {
        if (m_renderPresetName.empty()) {
            Notification::create("Enter a preset name", NotificationIcon::Warning, 1.0f)->show();
            return;
        }
        auto* m = Mod::get();
        RenderPreset preset;
        preset.name = m_renderPresetName;
        preset.width = static_cast<int>(m->getSavedValue<int64_t>("render_width", 1920));
        preset.height = static_cast<int>(m->getSavedValue<int64_t>("render_height", 1080));
        preset.fps = static_cast<int>(m->getSavedValue<int64_t>("render_fps", 60));
        preset.bitrate = m->getSavedValue<std::string>("render_bitrate", "30");
        preset.codec = m->getSavedValue<std::string>("render_codec", "");
        preset.secondsAfter = static_cast<float>(std::atof(m->getSavedValue<std::string>("render_seconds_after", "3").c_str()));
        preset.includeAudio = m->getSavedValue<bool>("render_include_audio", true);
        preset.includeClicks = m->getSavedValue<bool>("render_include_clicks", false);
        preset.sfxVol = static_cast<float>(m->getSavedValue<double>("render_sfx_volume", 1.0));
        preset.musicVol = static_cast<float>(m->getSavedValue<double>("render_music_volume", 1.0));
        preset.hideEndscreen = m->getSavedValue<bool>("render_hide_endscreen", false);
        preset.hideLevelComplete = m->getSavedValue<bool>("render_hide_levelcomplete", false);

        auto dir = m->getSaveDir() / "presets";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (RenderPresetIO::save(RenderPresetIO::pathForName(dir, preset.name), preset)) {
            Notification::create("Saved preset", NotificationIcon::Success, 0.9f)->show();
        } else {
            Notification::create("Could not save preset", NotificationIcon::Error, 1.0f)->show();
        }
        switchTab(1);
    }));

    if (presetNames.empty()) {
        content->addChild(SectionHeaderCell::create("(no saved presets)"));
    } else {
        m_renderPresetIndex = std::clamp(m_renderPresetIndex, 0, static_cast<int>(presetNames.size()) - 1);
        content->addChild(ComboCell::create("Saved Preset", presetNames, m_renderPresetIndex, [this](int idx) {
            m_renderPresetIndex = idx;
        }));
        content->addChild(ButtonCell::create("Load Preset", "Load", [this, presetNames]() {
            if (m_renderPresetIndex < 0 || m_renderPresetIndex >= static_cast<int>(presetNames.size())) {
                return;
            }
            auto* m = Mod::get();
            auto dir = m->getSaveDir() / "presets";
            auto loaded = RenderPresetIO::load(RenderPresetIO::pathForName(dir, presetNames[m_renderPresetIndex]));
            if (!loaded) {
                Notification::create("Could not load preset", NotificationIcon::Error, 1.0f)->show();
                return;
            }
            auto const& p = *loaded;
            m->setSavedValue<int64_t>("render_width", p.width);
            m->setSavedValue<int64_t>("render_height", p.height);
            m->setSavedValue<int64_t>("render_fps", p.fps);
            m->setSavedValue<std::string>("render_bitrate", p.bitrate);
            m->setSavedValue<std::string>("render_codec", p.codec);
            char secondsBuf[32];
            std::snprintf(secondsBuf, sizeof(secondsBuf), "%g", p.secondsAfter);
            m->setSavedValue<std::string>("render_seconds_after", secondsBuf);
            m->setSavedValue<bool>("render_include_audio", p.includeAudio);
            m->setSavedValue<bool>("render_include_clicks", p.includeClicks);
            m->setSavedValue<double>("render_sfx_volume", static_cast<double>(p.sfxVol));
            m->setSavedValue<double>("render_music_volume", static_cast<double>(p.musicVol));
            m->setSavedValue<bool>("render_hide_endscreen", p.hideEndscreen);
            m->setSavedValue<bool>("render_hide_levelcomplete", p.hideLevelComplete);
            m_renderPresetName = p.name;
            Notification::create("Loaded " + p.name, NotificationIcon::Success, 0.9f)->show();
            switchTab(1);
        }));
        content->addChild(ButtonCell::create("Delete Preset", "Delete", [this, presetNames]() {
            if (m_renderPresetIndex < 0 || m_renderPresetIndex >= static_cast<int>(presetNames.size())) {
                return;
            }
            auto dir = Mod::get()->getSaveDir() / "presets";
            std::error_code ec;
            std::filesystem::remove(RenderPresetIO::pathForName(dir, presetNames[m_renderPresetIndex]), ec);
            m_renderPresetIndex = 0;
            Notification::create("Deleted preset", NotificationIcon::Success, 0.9f)->show();
            switchTab(1);
        }));
    }

    content->addChild(SectionHeaderCell::create("Render"));
    auto* engine = ReplayEngine::get();
    bool recording = engine && engine->renderer.recording;
    content->addChild(ButtonCell::create(recording ? "Rendering..." : "Render the loaded macro", recording ? "Stop" : "Start", [this]() {
        auto* e = ReplayEngine::get();
        if (!e) {
            return;
        }
        if (!PlayLayer::get() && !e->renderer.recording) {
            Notification::create("Enter a level before rendering", NotificationIcon::Warning, 1.2f)->show();
            return;
        }
        e->renderer.toggle();
        switchTab(1);
    }));
}

void TRMenuPopup::buildClicksTab(CCNode* content) {
    auto* csm = ClickSoundManager::get();
    auto* mod = Mod::get();

    if (!m_clickPacksScanned) {
        csm->scanClickPacks();
        csm->scanClickPacksP2();
        m_clickPacksScanned = true;
    }

    content->addChild(ToggleCell::create("Click Bot", "Generate click and release audio from inputs", csm->enabled, [](bool value) {
        ClickSoundManager::get()->enabled = value;
        Mod::get()->setSavedValue("click_enabled", value);
    }));

    content->addChild(SectionHeaderCell::create("Click Pack"));
    if (csm->availablePacks.empty()) {
        content->addChild(SectionHeaderCell::create("(no click packs found)"));
    } else {
        int packIndex = 0;
        for (int i = 0; i < static_cast<int>(csm->availablePacks.size()); ++i) {
            if (csm->availablePacks[i] == csm->activePackName) {
                packIndex = i;
                break;
            }
        }
        content->addChild(ComboCell::create("Pack", csm->availablePacks, packIndex, [this](int idx) {
            auto* manager = ClickSoundManager::get();
            if (idx < 0 || idx >= static_cast<int>(manager->availablePacks.size())) {
                return;
            }
            manager->activePackName = manager->availablePacks[idx];
            manager->loadClickPack(manager->activePackName, manager->p1Pack);
            Mod::get()->setSavedValue("click_pack", manager->activePackName);
            switchTab(2);
        }));
    }

    content->addChild(ButtonCell::create("Rescan click packs", "Refresh", [this]() {
        auto* manager = ClickSoundManager::get();
        manager->scanClickPacks();
        if (!manager->availablePacks.empty() && manager->activePackName.empty()) {
            manager->activePackName = manager->availablePacks[0];
            manager->loadClickPack(manager->activePackName, manager->p1Pack);
            Mod::get()->setSavedValue("click_pack", manager->activePackName);
        }
        switchTab(2);
    }));
    content->addChild(ButtonCell::create("Click pack folder", "Open", []() {
        ClickSoundManager::get()->openClickFolder();
    }));

    if (!csm->p1Pack.empty()) {
        content->addChild(SectionHeaderCell::create(
            "Hard " + std::to_string(csm->p1Pack.hardCount()) +
            "  Soft " + std::to_string(csm->p1Pack.softCount()) +
            "  Rel " + std::to_string(csm->p1Pack.releaseCount()) +
            "  Noise " + std::to_string(csm->p1Pack.noiseCount())));
    }

    content->addChild(SectionHeaderCell::create("Volume"));
    if (csm->p1Pack.empty()) {
        content->addChild(SectionHeaderCell::create("(select a click pack)"));
    } else {
        if (!csm->p1Pack.hardClicks.empty()) {
            content->addChild(SliderCell::create("Hard Click", csm->p1Pack.hardVolume, 0.f, 2.f, [](float value) {
                ClickSoundManager::get()->p1Pack.hardVolume = value;
                Mod::get()->setSavedValue("click_hard_vol", static_cast<double>(value));
            }));
        }
        if (!csm->p1Pack.softClicks.empty()) {
            content->addChild(SliderCell::create("Soft Click", csm->p1Pack.softVolume, 0.f, 2.f, [](float value) {
                ClickSoundManager::get()->p1Pack.softVolume = value;
                Mod::get()->setSavedValue("click_soft_vol", static_cast<double>(value));
            }));
        }
        if (csm->p1Pack.releaseCount() > 0) {
            content->addChild(SliderCell::create("Release", csm->p1Pack.releaseVolume, 0.f, 2.f, [](float value) {
                ClickSoundManager::get()->p1Pack.releaseVolume = value;
                Mod::get()->setSavedValue("click_release_vol", static_cast<double>(value));
            }));
        }
    }

    content->addChild(SectionHeaderCell::create("Behavior"));
    content->addChild(SliderCell::create("Softness", csm->softness, 0.f, 1.f, [](float value) {
        ClickSoundManager::get()->softness = value;
        Mod::get()->setSavedValue("click_softness", static_cast<double>(value));
    }));
    content->addChild(SliderCell::create("Delay Min (ms)", csm->clickDelayMin, 0.f, 100.f, [](float value) {
        auto* manager = ClickSoundManager::get();
        manager->clickDelayMin = value;
        if (manager->clickDelayMin > manager->clickDelayMax) {
            manager->clickDelayMax = manager->clickDelayMin;
        }
        Mod::get()->setSavedValue("click_delay_min", static_cast<double>(manager->clickDelayMin));
        Mod::get()->setSavedValue("click_delay_max", static_cast<double>(manager->clickDelayMax));
    }));
    content->addChild(SliderCell::create("Delay Max (ms)", csm->clickDelayMax, 0.f, 100.f, [](float value) {
        auto* manager = ClickSoundManager::get();
        manager->clickDelayMax = value;
        if (manager->clickDelayMax < manager->clickDelayMin) {
            manager->clickDelayMin = manager->clickDelayMax;
        }
        Mod::get()->setSavedValue("click_delay_min", static_cast<double>(manager->clickDelayMin));
        Mod::get()->setSavedValue("click_delay_max", static_cast<double>(manager->clickDelayMax));
    }));
    content->addChild(ToggleCell::create("Play During Playback", "Play click sounds while a macro is playing back", csm->playDuringPlayback, [](bool value) {
        ClickSoundManager::get()->playDuringPlayback = value;
        Mod::get()->setSavedValue("click_play_during_playback", value);
    }));

    content->addChild(SectionHeaderCell::create("Background Noise"));
    if (csm->p1Pack.noiseFiles.empty()) {
        content->addChild(SectionHeaderCell::create("(add a 'noise' folder to the pack)"));
    } else {
        content->addChild(ToggleCell::create("Enable Background Noise", "", csm->backgroundNoiseEnabled, [](bool value) {
            auto* manager = ClickSoundManager::get();
            manager->backgroundNoiseEnabled = value;
            Mod::get()->setSavedValue("click_bg_noise", value);
            if (value) {
                manager->startBackgroundNoise();
            } else {
                manager->stopBackgroundNoise();
            }
        }));
        content->addChild(SliderCell::create("Noise Volume", csm->backgroundNoiseVolume, 0.f, 2.f, [](float value) {
            auto* manager = ClickSoundManager::get();
            manager->backgroundNoiseVolume = value;
            Mod::get()->setSavedValue("click_bg_noise_vol", static_cast<double>(value));
            if (manager->bgNoiseChannel) {
                manager->bgNoiseChannel->setVolume(value);
            }
        }));
    }

    content->addChild(SectionHeaderCell::create("Player 2"));
    content->addChild(ToggleCell::create("Separate P2 Clicks", "Use a different click pack for Player 2", csm->separateP2Clicks, [this](bool value) {
        ClickSoundManager::get()->separateP2Clicks = value;
        Mod::get()->setSavedValue("click_separate_p2", value);
        switchTab(2);
    }));

    if (csm->separateP2Clicks) {
        content->addChild(SectionHeaderCell::create("P2 Click Pack"));
        if (csm->availablePacksP2.empty()) {
            content->addChild(SectionHeaderCell::create("(no P2 click packs found)"));
        } else {
            int packIndexP2 = 0;
            for (int i = 0; i < static_cast<int>(csm->availablePacksP2.size()); ++i) {
                if (csm->availablePacksP2[i] == csm->activePackNameP2) {
                    packIndexP2 = i;
                    break;
                }
            }
            content->addChild(ComboCell::create("P2 Pack", csm->availablePacksP2, packIndexP2, [this](int idx) {
                auto* manager = ClickSoundManager::get();
                if (idx < 0 || idx >= static_cast<int>(manager->availablePacksP2.size())) {
                    return;
                }
                manager->activePackNameP2 = manager->availablePacksP2[idx];
                manager->loadClickPack(manager->activePackNameP2, manager->p2Pack, true);
                Mod::get()->setSavedValue("click_pack_p2", manager->activePackNameP2);
                switchTab(2);
            }));
        }

        content->addChild(ButtonCell::create("Rescan P2 packs", "Refresh", [this]() {
            ClickSoundManager::get()->scanClickPacksP2();
            switchTab(2);
        }));
        content->addChild(ButtonCell::create("P2 click pack folder", "Open", []() {
            ClickSoundManager::get()->openClickFolderP2();
        }));

        if (!csm->p2Pack.empty()) {
            content->addChild(SectionHeaderCell::create(
                "Hard " + std::to_string(csm->p2Pack.hardCount()) +
                "  Soft " + std::to_string(csm->p2Pack.softCount()) +
                "  Rel " + std::to_string(csm->p2Pack.releaseCount()) +
                "  Noise " + std::to_string(csm->p2Pack.noiseCount())));

            if (!csm->p2Pack.hardClicks.empty()) {
                content->addChild(SliderCell::create("P2 Hard Click", csm->p2Pack.hardVolume, 0.f, 2.f, [](float value) {
                    ClickSoundManager::get()->p2Pack.hardVolume = value;
                    Mod::get()->setSavedValue("click_hard_vol_p2", static_cast<double>(value));
                }));
            }
            if (!csm->p2Pack.softClicks.empty()) {
                content->addChild(SliderCell::create("P2 Soft Click", csm->p2Pack.softVolume, 0.f, 2.f, [](float value) {
                    ClickSoundManager::get()->p2Pack.softVolume = value;
                    Mod::get()->setSavedValue("click_soft_vol_p2", static_cast<double>(value));
                }));
            }
            if (csm->p2Pack.releaseCount() > 0) {
                content->addChild(SliderCell::create("P2 Release", csm->p2Pack.releaseVolume, 0.f, 2.f, [](float value) {
                    ClickSoundManager::get()->p2Pack.releaseVolume = value;
                    Mod::get()->setSavedValue("click_release_vol_p2", static_cast<double>(value));
                }));
            }
        }
    }

}

void TRMenuPopup::buildAutoclickerTab(CCNode* content) {
    auto* ac = Autoclicker::get();
    auto* engine = ReplayEngine::get();

    content->addChild(ToggleCell::create("Autoclicker", "Auto-click at configurable intervals", ac->enabled, [this](bool value) {
        Autoclicker::get()->enabled = value;
        Mod::get()->setSavedValue("ac_enabled", value);
        switchTab(3);
    }));

    if (ac->enabled) {
        bool paused = engine && engine->engineMode == MODE_EXECUTE;
        content->addChild(SectionHeaderCell::create(paused ? "Status: PAUSED (PLAYBACK)" : "Status: ACTIVE"));
    }

    content->addChild(SectionHeaderCell::create("Players"));
    content->addChild(ToggleCell::create("Player 1", "", ac->player1, [](bool value) {
        Autoclicker::get()->player1 = value;
        Mod::get()->setSavedValue("ac_player1", value);
    }));
    content->addChild(ToggleCell::create("Player 2", "", ac->player2, [](bool value) {
        Autoclicker::get()->player2 = value;
        Mod::get()->setSavedValue("ac_player2", value);
    }));

    content->addChild(SectionHeaderCell::create("Timing"));
    int modeIndex = ac->isTimedMode() ? 1 : 0;
    content->addChild(ComboCell::create("Mode", { "Legacy (TPS-bound)", "Timed High-CPS" }, modeIndex, [this](int idx) {
        auto* a = Autoclicker::get();
        a->mode = idx == 1 ? AutoclickerMode::Timed : AutoclickerMode::Legacy;
        a->reset();
        Mod::get()->setSavedValue("ac_mode", static_cast<int>(a->mode));
        switchTab(3);
    }));

    if (ac->isTimedMode()) {
        content->addChild(InputCell::create("Target CPS", std::to_string(static_cast<int>(ac->targetCps)), "1000", true, [](std::string const& text) {
            auto* a = Autoclicker::get();
            if (auto value = toasty::parseInteger<int>(text)) {
                a->targetCps = std::clamp(static_cast<float>(*value), 1.f, 20000.f);
                a->reset();
                Mod::get()->setSavedValue("ac_target_cps", static_cast<double>(a->targetCps));
            }
        }));
        content->addChild(SliderCell::create("Hold Ratio", ac->holdRatio, 0.05f, 0.95f, [](float value) {
            auto* a = Autoclicker::get();
            a->holdRatio = std::clamp(value, 0.05f, 0.95f);
            a->reset();
            Mod::get()->setSavedValue("ac_hold_ratio", static_cast<double>(a->holdRatio));
        }));
    } else {
        content->addChild(SliderCell::create("Hold Ticks", static_cast<float>(ac->holdTicks), 1.f, 120.f, [](float value) {
            auto* a = Autoclicker::get();
            a->holdTicks = std::clamp(static_cast<int>(std::lround(value)), 1, 120);
            a->reset();
            Mod::get()->setSavedValue("ac_hold_ticks", a->holdTicks);
        }));
        content->addChild(SliderCell::create("Release Ticks", static_cast<float>(ac->releaseTicks), 1.f, 120.f, [](float value) {
            auto* a = Autoclicker::get();
            a->releaseTicks = std::clamp(static_cast<int>(std::lround(value)), 1, 120);
            a->reset();
            Mod::get()->setSavedValue("ac_release_ticks", a->releaseTicks);
        }));
    }

    double tickRate = engine ? engine->tickRate : 240.0;
    float cps = ac->isTimedMode() ? ac->timedClicksPerSecond() : ac->legacyClicksPerSecond(tickRate);
    char cpsBuffer[64];
    if (ac->isTimedMode()) {
        std::snprintf(cpsBuffer, sizeof(cpsBuffer), "~%.0f clicks/sec", cps);
    } else {
        std::snprintf(cpsBuffer, sizeof(cpsBuffer), "~%.1f clicks/sec at %.0f TPS", cps, tickRate);
    }
    content->addChild(SectionHeaderCell::create(cpsBuffer));

    content->addChild(SectionHeaderCell::create("Options"));
    content->addChild(ToggleCell::create("Only While Holding", "Auto-click only while you hold the jump button", ac->onlyWhileHolding, [](bool value) {
        Autoclicker::get()->onlyWhileHolding = value;
        Mod::get()->setSavedValue("ac_only_holding", value);
    }));

    if (toasty::frontend::desktopKeybinds()) {
        content->addChild(SectionHeaderCell::create("Keybind"));
        if (int* keyPtr = toasty::frontend::keybindPtr("autoclicker")) {
            content->addChild(KeybindCell::create("Toggle Autoclicker", keyPtr));
        }
    }
}

void TRMenuPopup::refreshOnlineWhilePolling(float) {
    if (m_activeTab == 5) {
        switchTab(5);
    }
}

void TRMenuPopup::buildOnlineTab(CCNode* content) {
    auto* client = OnlineClient::get();
    content->addChild(SectionHeaderCell::create("Discord Login"));
    if (client->isLinked()) {
        content->addChild(SectionHeaderCell::create("Signed in as " + client->discordUsername));
        content->addChild(ButtonCell::create("Sign Out", "Sign Out", []() { OnlineClient::get()->unlinkAccount(); }));
    } else {
        content->addChild(ButtonCell::create("Sign In With Discord", "Sign In", []() { OnlineClient::get()->startAuthFlow(); }));
    }
}

void TRMenuPopup::buildSettingsTab(CCNode* content) {
    content->addChild(SectionHeaderCell::create("General"));
    content->addChild(ComboCell::create("Menu Style", { "ImGui", "Cocos2d" }, toasty::frontend::isCocos() ? 1 : 0, [](int idx) {
        Mod::get()->setSettingValue<std::string>("menu_frontend", idx == 1 ? std::string("Cocos2d") : std::string("ImGui"));
        Notification::create("Reopen the menu to apply", NotificationIcon::Info, 1.0f)->show();
    }));

    auto* engine = ReplayEngine::get();
    if (!engine) {
        return;
    }

    content->addChild(SectionHeaderCell::create("Input Accuracy"));
    int accIndex = std::clamp(static_cast<int>(engine->selectedAccuracyMode), 0, 1);
    auto accHolder = std::make_shared<ComboCell*>(nullptr);
    auto* accCombo = ComboCell::create("Accuracy Mode", { "Vanilla", "CBS" }, accIndex, [accHolder](int idx) {
        auto* e = ReplayEngine::get();
        auto* combo = *accHolder;
        if (e->engineMode != MODE_DISABLED) {
            Notification::create("Stop recording or playback first", NotificationIcon::Warning, 1.0f)->show();
            if (combo) {
                combo->setIndex(std::clamp(static_cast<int>(e->selectedAccuracyMode), 0, 1));
            }
            return;
        }
        e->selectedAccuracyMode = static_cast<AccuracyMode>(idx);
        ReplayEngine::applyRuntimeAccuracyMode(e->selectedAccuracyMode);
        toasty::frontend::persistSettings();
    });
    *accHolder = accCombo;
    content->addChild(accCombo);

    content->addChild(SectionHeaderCell::create("Behavior"));
    auto* csm = ClickSoundManager::get();
    content->addChild(ToggleCell::create("Fast Playback", "Start playback without restarting the level", engine->fastPlayback, [](bool value) {
        ReplayEngine::get()->fastPlayback = value;
        toasty::frontend::persistSettings();
    }));
    content->addChild(ToggleCell::create("Autosave", "Automatically save completed recordings", engine->completionAutosave, [](bool value) {
        ReplayEngine::get()->completionAutosave = value;
        toasty::frontend::persistSettings();
    }));
    content->addChild(ToggleCell::create("Mute Left/Right Clicks", "Disable click sounds for platformer left and right inputs", csm->muteLeftRightClicks, [](bool value) {
        ClickSoundManager::get()->muteLeftRightClicks = value;
        toasty::frontend::persistSettings();
    }));

    content->addChild(SectionHeaderCell::create("Watermark"));
    content->addChild(ToggleCell::create("Render Watermark", "Burn a watermark into rendered video", toasty::frontend::renderWatermarkEnabled(), [](bool value) {
        toasty::frontend::setRenderWatermarkEnabled(value);
    }));

    const std::vector<std::string> languages = { "Auto", "English", "Espanol", "Francais", "TiengViet", "Zhongwen" };
    std::string currentLanguage = Mod::get()->getSettingValue<std::string>("ui_language");
    int languageIndex = 0;
    for (size_t i = 0; i < languages.size(); ++i) {
        if (languages[i] == currentLanguage) {
            languageIndex = static_cast<int>(i);
            break;
        }
    }
    content->addChild(ComboCell::create("UI Language", languages, languageIndex, [languages](int idx) {
        if (idx >= 0 && idx < static_cast<int>(languages.size())) {
            Mod::get()->setSettingValue<std::string>("ui_language", languages[idx]);
        }
    }));

    if (toasty::frontend::desktopKeybinds()) {
        content->addChild(SectionHeaderCell::create("Keybinds"));
        for (auto const& entry : toasty::frontend::allKeybinds()) {
            if (int* ptr = toasty::frontend::keybindPtr(entry.second)) {
                content->addChild(KeybindCell::create(entry.first, ptr));
            }
        }
    }
}

TRMenuPopup* TRMenuPopup::create() {
    auto* popup = new TRMenuPopup();
    if (popup && popup->init()) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

void TRMenuPopup::toggle() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) {
        return;
    }
    if (auto* existing = typeinfo_cast<TRMenuPopup*>(scene->getChildByID("cocos-menu"_spr))) {
        existing->removeFromParentAndCleanup(true);
        return;
    }
    auto* popup = TRMenuPopup::create();
    if (popup) {
        popup->show();
    }
}

bool TRMenuPopup::isOpen() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    return scene && scene->getChildByID("cocos-menu"_spr) != nullptr;
}

}
