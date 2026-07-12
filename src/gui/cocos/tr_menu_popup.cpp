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
#include "lang/localization.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace toasty::frontend {

namespace {
    constexpr float kPopupWidth = 480.f;
    constexpr float kPopupHeight = 304.f;
    constexpr int kTabCount = 6;
    const char* const kTabs[kTabCount] = { "Main", "Render", "Clicks", "Autoclicker", "Settings", "Online" };
    constexpr float kTabWidth = 88.f;
    constexpr float kTabHeight = 33.f;
    constexpr float kTabPitch = 38.72f;
    constexpr float kTabX = 54.f;
    constexpr float kTabStartY = 225.8f;
    std::atomic<std::uint64_t> g_menuRevision = 0;

    std::string localized(std::string_view text) {
        return std::string(toasty::lang::tr(text));
    }

    ccColor3B toColor(CocosColor const& color) {
        return ccc3(color[0], color[1], color[2]);
    }

    CCScale9Sprite* makeSurface(float width, float height, ccColor3B color, GLubyte opacity) {
        auto* surface = CCScale9Sprite::create("GJ_square05.png");
        CCSize natural = surface->getContentSize();
        constexpr float inset = 8.f;
        surface->setCapInsets(CCRect(inset, inset, std::max(1.f, natural.width - inset * 2.f), std::max(1.f, natural.height - inset * 2.f)));
        surface->setContentSize({ width, height });
        surface->setColor(color);
        surface->setOpacity(opacity);
        return surface;
    }

    ccColor4B rgbSettingToCC(int red, int green, int blue) {
        return ccc4(
            static_cast<GLubyte>(std::clamp(red, 0, 255)),
            static_cast<GLubyte>(std::clamp(green, 0, 255)),
            static_cast<GLubyte>(std::clamp(blue, 0, 255)),
            255
        );
    }

    CCNode* makePillButton(const char* label, ccColor3B color, float width, float height) {
        auto* container = CCNode::create();
        container->setContentSize({ width, height });
        container->setAnchorPoint({ 0.5f, 0.5f });

        auto* bg = makeSurface(width, height, color, 255);
        bg->setPosition({ width * 0.5f, height * 0.5f });
        container->addChild(bg);

        auto displayLabel = localized(label);
        auto* text = CCLabelBMFont::create(displayLabel.c_str(), "bigFont.fnt");
        text->setAnchorPoint({ 0.5f, 0.5f });
        text->setPosition({ width * 0.5f, height * 0.5f });
        text->limitLabelWidth(width - 10.f, 0.42f, 0.18f);
        container->addChild(text);

        return container;
    }

    CCNode* makeSegmentButton(const char* label, bool active, float width, float height) {
        auto theme = cocosTheme();
        auto* container = CCNode::create();
        container->setContentSize({ width, height });
        container->setAnchorPoint({ 0.5f, 0.5f });

        if (active) {
            auto* selected = makeSurface(width - 3.f, height - 3.f, toColor(theme.subCell), 255);
            selected->setPosition({ width * 0.5f, height * 0.5f + 1.f });
            container->addChild(selected);

            auto accent = toColor(theme.accent);
            auto* underline = CCLayerColor::create(ccc4(accent.r, accent.g, accent.b, 255), width - 14.f, 2.f);
            underline->setPosition({ 7.f, 2.f });
            container->addChild(underline);
        }

        auto displayLabel = localized(label);
        auto* text = CCLabelBMFont::create(displayLabel.c_str(), "bigFont.fnt");
        text->setColor(toColor(active ? theme.sectionText : theme.mutedText));
        text->limitLabelWidth(width - 10.f, 0.38f, 0.2f);
        text->setPosition({ width * 0.5f, height * 0.5f + 1.f });
        container->addChild(text);
        return container;
    }

    CCNode* makeNavButton(int index, bool active) {
        auto theme = cocosTheme();
        auto* container = CCNode::create();
        container->setContentSize({ kTabWidth, kTabHeight });
        container->setAnchorPoint({ 0.5f, 0.5f });

        auto* border = makeSurface(kTabWidth, kTabHeight, toColor(active ? theme.accent : theme.cellBorder), active ? 255 : 210);
        border->setPosition({ kTabWidth * 0.5f, kTabHeight * 0.5f });
        container->addChild(border);

        auto* bg = makeSurface(kTabWidth - 2.f, kTabHeight - 2.f, toColor(active ? theme.subCell : theme.cell), 255);
        bg->setPosition({ kTabWidth * 0.5f, kTabHeight * 0.5f });
        container->addChild(bg);

        auto displayLabel = localized(kTabs[index]);
        auto* label = CCLabelBMFont::create(displayLabel.c_str(), "bigFont.fnt");
        label->setAnchorPoint({ 0.5f, 0.5f });
        label->setColor(toColor(active ? theme.sectionText : theme.mutedText));
        label->limitLabelWidth(kTabWidth - 14.f, 0.42f, 0.22f);
        label->setPosition({ kTabWidth * 0.5f, kTabHeight * 0.5f });
        container->addChild(label);
        return container;
    }
}

bool TRMenuPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight, "GJ_square01.png")) {
        return false;
    }

    this->setID("cocos-menu"_spr);

    auto theme = cocosTheme();

    if (m_bgSprite) {
        m_bgSprite->setColor(toColor(theme.shell));
        m_bgSprite->setOpacity(255);
    }

    if (m_closeBtn) {
        m_closeBtn->setScale(0.72f);
    }

    auto* header = makeSurface(kPopupWidth - 16.f, 42.f, toColor(theme.header), 250);
    header->setPosition({ kPopupWidth * 0.5f, kPopupHeight - 25.f });
    m_mainLayer->addChild(header, 1);

    float titleX = 20.f;
    if (auto* logo = CCSprite::create("toastyreplay-logo.png"_spr)) {
        logo->setScale(27.f / std::max(1.f, logo->getContentSize().height));
        logo->setPosition({ 27.f, kPopupHeight - 25.f });
        m_mainLayer->addChild(logo, 3);
        titleX = 47.f;
    }

    auto* title = CCLabelBMFont::create("ToastyReplay", "bigFont.fnt");
    title->setScale(0.48f);
    title->setAnchorPoint({ 0.f, 0.5f });
    title->setPosition({ titleX, kPopupHeight - 25.f });
    m_mainLayer->addChild(title, 3);

    auto* editionBadge = makePillButton("FREE", toColor(theme.accent), 64.f, 20.f);
    editionBadge->setPosition({ kPopupWidth - 52.f, kPopupHeight - 25.f });
    m_mainLayer->addChild(editionBadge, 3);

    auto* navPanel = makeSurface(92.f, 238.f, toColor(theme.navigation), 245);
    navPanel->setPosition({ 54.f, 129.f });
    m_mainLayer->addChild(navPanel, 1);

    auto* panelBorder = makeSurface(366.f, 238.f, toColor(theme.cellBorder), 225);
    panelBorder->setPosition({ 289.f, 129.f });
    m_mainLayer->addChild(panelBorder, 1);

    auto* panel = makeSurface(346.f, 236.f, toColor(theme.content), 250);
    panel->setPosition({ 279.f, 129.f });
    m_mainLayer->addChild(panel, 2);

    auto* scrollGutter = makeSurface(16.f, 232.f, toColor(theme.navigation), 245);
    scrollGutter->setPosition({ 462.f, 129.f });
    m_mainLayer->addChild(scrollGutter, 2);

    m_scroll = ScrollLayer::create({ 336.f, 226.f });
    m_scroll->setPosition({ 111.f, 16.f });
    m_scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(4.f));
    m_mainLayer->addChild(m_scroll, 3);

    auto* scrollbar = Scrollbar::create(m_scroll);
    scrollbar->setPosition({ 462.f, 129.f });
    scrollbar->setScale(0.88f);
    m_mainLayer->addChild(scrollbar, 4);

    m_tabMenu = CCMenu::create();
    m_tabMenu->setContentSize(m_size);
    m_tabMenu->ignoreAnchorPointForPosition(false);
    m_tabMenu->setAnchorPoint({ 0.5f, 0.5f });
    m_tabMenu->setPosition({ m_size.width * 0.5f, m_size.height * 0.5f });
    m_mainLayer->addChild(m_tabMenu, 100);

    m_seenRevision = g_menuRevision.load(std::memory_order_relaxed);
    schedule(schedule_selector(TRMenuPopup::refreshIfNeeded), 0.05f);
    applyTab(0);
    return true;
}

void TRMenuPopup::refreshIfNeeded(float) {
    auto revision = g_menuRevision.load(std::memory_order_relaxed);
    if (revision == m_seenRevision) {
        return;
    }
    m_seenRevision = revision;
    if (m_activeTab >= 0) {
        switchTab(m_activeTab);
    }
}

void TRMenuPopup::buildTabBar() {
    m_tabMenu->removeAllChildrenWithCleanup(true);

    for (int i = 0; i < kTabCount; ++i) {
        bool active = (i == m_activeTab);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(makeNavButton(i, active), [this, i](CCMenuItemSpriteExtra*) {
            this->switchTab(i);
        });
        item->setPosition({ kTabX, kTabStartY - i * kTabPitch });
        m_tabMenu->addChild(item);
    }
}

void TRMenuPopup::switchTab(int index) {
    m_pendingTab = index;
    if (m_tabSwitchQueued) {
        return;
    }
    m_tabSwitchQueued = true;
    retain();
    geode::queueInMainThread([this] {
        int index = m_pendingTab;
        m_pendingTab = -1;
        m_tabSwitchQueued = false;
        if (getParent()) {
            applyTab(index);
        }
        release();
    });
}

void TRMenuPopup::applyTab(int index) {
    index = std::clamp(index, 0, kTabCount - 1);
    m_preserveScroll = (index == m_activeTab) && !m_subTabChanged;
    m_subTabChanged = false;
    m_activeTab = index;
    buildTabBar();
    buildTabContent(index);
}

void TRMenuPopup::addSub(CCNode* content, CCNode* cell, std::string const&) {
    if (!cell) {
        return;
    }
    static_cast<TRCell*>(cell)->applySubStyle();
    content->addChild(cell);
}

void TRMenuPopup::buildTabContent(int index) {
    auto* content = m_scroll->m_contentLayer;
    float prevScrollY = content->getPositionY();
    float prevContentHeight = content->getContentHeight();
    content->removeAllChildrenWithCleanup(true);

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
    }

    content->updateLayout();
    if (m_preserveScroll) {
        float newContentHeight = content->getContentHeight();
        content->setPositionY(prevScrollY + (prevContentHeight - newContentHeight));
    } else {
        m_scroll->scrollToTop();
    }

}

void TRMenuPopup::buildMainTab(CCNode* content) {
    auto* row = CCNode::create();
    row->setContentSize({ kCellWidth, 30.f });
    row->setAnchorPoint({ 0.5f, 0.5f });

    bool separatoryEnabled = false;
    std::vector<const char*> subNames = { "Replay", "Hacks", "Tools" };
    int subCount = static_cast<int>(subNames.size());
    m_mainSubTab = std::clamp(m_mainSubTab, 0, subCount - 1);

    float segmentWidth = subCount == 4 ? 66.f : 76.f;
    float trackWidth = segmentWidth * subCount;
    auto palette = cocosTheme();
    auto* trackBorder = makeSurface(trackWidth + 4.f, 28.f, toColor(palette.cellBorder), 235);
    trackBorder->setPosition({ kCellWidth * 0.5f, 15.f });
    row->addChild(trackBorder);
    auto* track = makeSurface(trackWidth + 2.f, 26.f, toColor(palette.navigation), 250);
    track->setPosition({ kCellWidth * 0.5f, 15.f });
    row->addChild(track);

    auto* subMenu = CCMenu::create();
    subMenu->setContentSize({ 0.f, 0.f });
    subMenu->setPosition({ kCellWidth * 0.5f, 15.f });
    row->addChild(subMenu);

    for (int i = 0; i < subCount; ++i) {
        bool active = (i == m_mainSubTab);
        auto* segment = makeSegmentButton(subNames[i], active, segmentWidth, 24.f);
        auto* item = geode::cocos::CCMenuItemExt::createSpriteExtra(segment, [this, i](CCMenuItemSpriteExtra*) {
            m_mainSubTab = i;
            m_subTabChanged = true;
            switchTab(0);
        });
        item->setPosition({ (static_cast<float>(i) - (subCount - 1) * 0.5f) * segmentWidth, 0.f });
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
    auto* engine = ReplayEngine::get();
    if (!engine) {
        content->addChild(SectionHeaderCell::create("Engine unavailable"));
        return;
    }

    struct HackRow {
        const char* id;
        const char* label;
        const char* description;
        bool value;
    };

    const HackRow rows[] = {
        { "safe_mode", "Safe Mode", "Stops the run from affecting stats", engine->protectedMode },
        { "trajectory", "Show Trajectory", "Preview the player's path", engine->pathPreview },
        { "hitboxes", "Show Hitboxes", "Show collision outlines", engine->showHitboxes },
        { "noclip", "Noclip", "Ignore lethal collisions", engine->collisionBypass },
        { "rng_lock", "RNG Lock", "Keep random behavior repeatable", engine->rngLocked },
    };

    for (auto const& row : rows) {
        std::string id = row.id;
        content->addChild(ToggleCell::create(row.label, row.description, row.value, [this, id](bool value) {
            auto* e = ReplayEngine::get();
            if (id == "safe_mode") e->protectedMode = value;
            else if (id == "trajectory") e->pathPreview = value;
            else if (id == "hitboxes") e->showHitboxes = value;
            else if (id == "noclip") e->collisionBypass = value;
            else if (id == "rng_lock") e->rngLocked = value;
            toasty::frontend::persistSettings();
            switchTab(0);
        }));
        addHackSubOptions(content, id);
    }
}

void TRMenuPopup::addHackSubOptions(CCNode* content, std::string const& id) {
    auto* engine = ReplayEngine::get();
    if (!engine) {
        return;
    }

    auto addKeybind = [this, content, id]() {
        if (!toasty::frontend::desktopKeybinds()) {
            return;
        }
        if (auto* setting = toasty::frontend::keybindSettingId(id)) {
            addSub(content, KeybindCell::create("Keybind", setting), id);
        }
    };

    if (id == "safe_mode") {
        if (!engine->protectedMode) return;
        addKeybind();
        addSub(content, ToggleCell::create("Auto Safe Mode", "Turn it on while recording or playing", engine->autoSafeMode, [](bool value) {
            ReplayEngine::get()->autoSafeMode = value;
            toasty::frontend::persistSettings();
        }), id);
        return;
    }

    if (id == "trajectory") {
        if (!engine->pathPreview) return;
        addKeybind();
        addSub(content, SliderCell::create("Path Length", static_cast<float>(engine->pathLength),
            static_cast<float>(ReplayEngine::kTrajectoryLengthMin), static_cast<float>(ReplayEngine::kTrajectoryLengthSliderMax), [](float value) {
                auto* e = ReplayEngine::get();
                e->pathLength = ReplayEngine::sanitizeTrajectoryLength(static_cast<int>(std::lround(value)));
                TrajectoryPredictionService::get().markDirty();
                Mod::get()->setSavedValue("hack_trajectory_len", e->pathLength);
            }), id);
        addSub(content, InputCell::create("Exact Length", std::to_string(engine->pathLength), "240", true, [](std::string const& text) {
            if (auto value = toasty::parseInteger<int>(text)) {
                auto* e = ReplayEngine::get();
                e->pathLength = ReplayEngine::sanitizeTrajectoryLength(*value);
                TrajectoryPredictionService::get().markDirty();
                Mod::get()->setSavedValue("hack_trajectory_len", e->pathLength);
            }
        }), id);
        return;
    }

    if (id == "hitboxes") {
        if (!engine->showHitboxes) return;
        addKeybind();
        addSub(content, ToggleCell::create("On Death Only", "Hide them until the player dies", engine->hitboxOnDeath, [](bool value) {
            ReplayEngine::get()->hitboxOnDeath = value;
            toasty::frontend::persistSettings();
        }), id);
        addSub(content, ToggleCell::create("Draw Trail", "Keep recent player hitboxes visible", engine->hitboxTrail, [this](bool value) {
            ReplayEngine::get()->hitboxTrail = value;
            toasty::frontend::persistSettings();
            switchTab(0);
        }), id);
        if (engine->hitboxTrail) {
            addSub(content, SliderCell::create("Trail Length", static_cast<float>(engine->hitboxTrailLength), 10.f, 600.f, [](float value) {
                auto* e = ReplayEngine::get();
                e->hitboxTrailLength = std::clamp(static_cast<int>(std::lround(value)), 10, 600);
                Mod::get()->setSavedValue("hack_hitbox_trail_len", e->hitboxTrailLength);
            }), id);
        }
        return;
    }

    if (id == "noclip") {
        if (!engine->collisionBypass) return;
        addKeybind();
        addSub(content, ToggleCell::create("Player 1", "Apply noclip to Player 1", engine->noclipPlayer1, [](bool value) {
            ReplayEngine::get()->noclipPlayer1 = value;
            toasty::frontend::persistSettings();
        }), id);
        addSub(content, ToggleCell::create("Player 2", "Apply noclip to Player 2", engine->noclipPlayer2, [](bool value) {
            ReplayEngine::get()->noclipPlayer2 = value;
            toasty::frontend::persistSettings();
        }), id);
        auto accuracy = toasty::noclip::formatAccuracy(engine->noclipTotalFrames, engine->noclipUnsafeFrames, engine->noclipAccuracyDecimals);
        addSub(content, SectionHeaderCell::create("Accuracy " + accuracy), id);
        addSub(content, InputCell::create("Decimal Places", std::to_string(engine->noclipAccuracyDecimals), "2", true, [](std::string const& text) {
            if (auto value = toasty::parseInteger<int>(text)) {
                ReplayEngine::get()->noclipAccuracyDecimals = std::clamp(*value, 0, 6);
                toasty::frontend::persistSettings();
            }
        }), id);
        addSub(content, ToggleCell::create("Death Hitbox", "Show the collision noclip prevented", engine->noclipHitboxOnDeath, [](bool value) {
            ReplayEngine::get()->noclipHitboxOnDeath = value;
            toasty::frontend::persistSettings();
        }), id);
        addSub(content, ToggleCell::create("Accuracy Limit", "Stop playback below this percentage", engine->collisionLimitActive, [this](bool value) {
            auto* e = ReplayEngine::get();
            e->collisionLimitActive = value;
            if (value && e->collisionThreshold < 1.f) e->collisionThreshold = 80.f;
            toasty::frontend::persistSettings();
            switchTab(0);
        }), id);
        if (engine->collisionLimitActive) {
            addSub(content, InputCell::create("Limit (%)", std::to_string(static_cast<int>(engine->collisionThreshold)), "80", true, [](std::string const& text) {
                if (auto value = toasty::parseInteger<int>(text)) {
                    ReplayEngine::get()->collisionThreshold = std::clamp(static_cast<float>(*value), 1.f, 100.f);
                    toasty::frontend::persistSettings();
                }
            }), id);
        }
        addSub(content, ToggleCell::create("Death Flash", "Flash a color when noclip saves you", engine->noclipDeathFlash, [this](bool value) {
            ReplayEngine::get()->noclipDeathFlash = value;
            toasty::frontend::persistSettings();
            switchTab(0);
        }), id);
        if (engine->noclipDeathFlash) {
            auto color = ccc4(
                static_cast<GLubyte>(std::clamp(engine->noclipDeathColorR, 0.f, 1.f) * 255.f),
                static_cast<GLubyte>(std::clamp(engine->noclipDeathColorG, 0.f, 1.f) * 255.f),
                static_cast<GLubyte>(std::clamp(engine->noclipDeathColorB, 0.f, 1.f) * 255.f),
                255);
            addSub(content, ColorCell::create("Death Color", color, [](ccColor4B picked) {
                auto* e = ReplayEngine::get();
                e->noclipDeathColorR = picked.r / 255.f;
                e->noclipDeathColorG = picked.g / 255.f;
                e->noclipDeathColorB = picked.b / 255.f;
                toasty::frontend::persistSettings();
            }), id);
        }
        return;
    }

    if (id == "rng_lock") {
        if (!engine->rngLocked) return;
        addKeybind();
        addSub(content, InputCell::create("Seed", std::to_string(engine->rngSeedVal), "1", true, [](std::string const& text) {
            auto value = toasty::parseInteger<unsigned long long>(text);
            ReplayEngine::get()->rngSeedVal = value ? static_cast<unsigned int>(*value) : 1u;
            toasty::frontend::persistSettings();
        }), id);
    }
}

void TRMenuPopup::buildToolsSection(CCNode* content) {
    auto* engine = ReplayEngine::get();
    if (!engine) {
        content->addChild(SectionHeaderCell::create("Engine unavailable"));
        return;
    }

    content->addChild(SectionHeaderCell::create("Timing"));
    content->addChild(InputCell::create("Target TPS", std::to_string(static_cast<int>(engine->tickRate)), "240", true, [this](std::string const& text) {
        m_toolsTps = text;
    }));
    content->addChild(ButtonCell::create("Apply TPS", "Apply", [this]() {
        auto* e = ReplayEngine::get();
        std::string source = m_toolsTps.empty() ? std::to_string(static_cast<int>(e->tickRate)) : m_toolsTps;
        auto value = toasty::parseInteger<int>(source);
        if (!value || *value < 1 || *value > 1000000) {
            Notification::create("TPS must be between 1 and 1,000,000", NotificationIcon::Warning, 1.2f)->show();
            return;
        }
        if (PlayLayer::get() && e->engineMode == MODE_EXECUTE) {
            Notification::create("Stop playback before changing TPS", NotificationIcon::Warning, 1.2f)->show();
            return;
        }
        e->tickRate = *value;
        Mod::get()->setSavedValue<float>("eng_tick_rate", static_cast<float>(e->tickRate));
        TrajectoryPredictionService::get().markDirty();
        m_toolsTps.clear();
        switchTab(0);
    }));

    content->addChild(InputCell::create("Game Speed", std::to_string(engine->gameSpeed).substr(0, 4), "1.0", false, [this](std::string const& text) {
        m_toolsSpeed = text;
    }));
    content->addChild(ButtonCell::create("Apply Speed", "Apply", [this]() {
        auto* e = ReplayEngine::get();
        std::string source = m_toolsSpeed.empty() ? std::to_string(e->gameSpeed) : m_toolsSpeed;
        char* end = nullptr;
        float parsed = std::strtof(source.c_str(), &end);
        if (end == source.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed < 0.01f || parsed > 1000.f) {
            Notification::create("Speed must be between 0.01x and 1000x", NotificationIcon::Warning, 1.2f)->show();
            return;
        }
        e->gameSpeed = parsed;
        Mod::get()->setSavedValue<float>("eng_speed", parsed);
        TrajectoryPredictionService::get().markDirty();
        m_toolsSpeed.clear();
        switchTab(0);
    }));

    content->addChild(ToggleCell::create("Respawn Override", "Use a custom delay after death", engine->respawnTimeOverrideEnabled, [this](bool value) {
        auto* e = ReplayEngine::get();
        e->respawnTimeOverrideEnabled = value;
        Mod::get()->setSavedValue("hack_respawn_override_enabled", value);
        switchTab(0);
    }));
    if (engine->respawnTimeOverrideEnabled) {
        addSub(content, InputCell::create("Respawn Delay (ms)", std::to_string(engine->respawnTimeOverrideMs), "1000", true, [](std::string const& text) {
            if (auto value = toasty::parseInteger<int>(text)) {
                auto* e = ReplayEngine::get();
                e->respawnTimeOverrideMs = std::clamp(*value, 0, 10000);
                Mod::get()->setSavedValue("hack_respawn_ms", e->respawnTimeOverrideMs);
            }
        }), "respawn");
    }

    content->addChild(SectionHeaderCell::create("Gameplay"));
    auto addTool = [this, content](std::string const& id, std::string const& label, std::string const& description, bool on, std::function<void(bool)> apply, std::function<void()> addOptions = nullptr) {
        content->addChild(ToggleCell::create(label, description, on, [this, apply](bool value) {
            apply(value);
            toasty::frontend::persistSettings();
            switchTab(0);
        }));
        if (!on) return;
        if (toasty::frontend::desktopKeybinds()) {
            if (auto* setting = toasty::frontend::keybindSettingId(id)) {
                addSub(content, KeybindCell::create("Keybind", setting), id);
            }
        }
        if (addOptions) addOptions();
    };

    addTool("frame_advance", "Frame Advance", "Pause and step one frame at a time", engine->tickStepping, [](bool value) {
        ReplayEngine::get()->setFrameStepEnabled(value, PlayLayer::get());
    }, [this, content]() {
        if (toasty::frontend::desktopKeybinds()) {
            if (auto* setting = toasty::frontend::keybindSettingId("frame_step")) {
                addSub(content, KeybindCell::create("Advance One Frame", setting), "frame_advance");
            }
        }
    });
    addTool("audio_pitch", "Speedhack Audio", "Match audio pitch to game speed", engine->audioPitchEnabled, [](bool value) {
        ReplayEngine::get()->audioPitchEnabled = value;
    });
    addTool("layout_mode", "Layout Mode", "Hide level decoration", engine->layoutMode, [](bool value) {
        ReplayEngine::get()->layoutMode = value;
    }, [this, content, engine]() {
        addSub(content, ColorCell::create("Background", rgbSettingToCC(engine->layoutModeBackgroundR, engine->layoutModeBackgroundG, engine->layoutModeBackgroundB), [](ccColor4B picked) {
            auto* e = ReplayEngine::get();
            e->layoutModeBackgroundR = picked.r;
            e->layoutModeBackgroundG = picked.g;
            e->layoutModeBackgroundB = picked.b;
            toasty::frontend::persistSettings();
        }), "layout_mode");
        addSub(content, ColorCell::create("Ground", rgbSettingToCC(engine->layoutModeGroundR, engine->layoutModeGroundG, engine->layoutModeGroundB), [](ccColor4B picked) {
            auto* e = ReplayEngine::get();
            e->layoutModeGroundR = picked.r;
            e->layoutModeGroundG = picked.g;
            e->layoutModeGroundB = picked.b;
            toasty::frontend::persistSettings();
        }), "layout_mode");
    });
    addTool("disable_shaders", "Disable Shaders", "Turn off level shader effects", engine->disableShaders, [](bool value) {
        ReplayEngine::get()->disableShaders = value;
    });
    addTool("no_death_effect", "No Death Effect", "Hide the death burst", engine->noDeathEffect, [](bool value) {
        ReplayEngine::get()->noDeathEffect = value;
    });
    addTool("no_effects", "No Effects", "Hide gameplay effects", engine->noEffect, [](bool value) {
        ReplayEngine::get()->noEffect = value;
    });
    addTool("hide_endscreen", "Hide Endscreen", "Skip the level endscreen", engine->hideEndscreen, [](bool value) {
        ReplayEngine::get()->hideEndscreen = value;
    });
    addTool("hide_new_best", "Hide New Best", "Skip the new best popup", engine->hideNewBest, [](bool value) {
        ReplayEngine::get()->hideNewBest = value;
    });
    addTool("no_mirror", "No Mirror Effect", "Ignore mirror portals", engine->noMirrorEffect, [](bool value) {
        ReplayEngine::get()->noMirrorEffect = value;
    }, [this, content, engine]() {
        addSub(content, ToggleCell::create("Recording Only", "Keep mirroring everywhere else", engine->noMirrorRecordingOnly, [](bool value) {
            ReplayEngine::get()->noMirrorRecordingOnly = value;
            toasty::frontend::persistSettings();
        }), "no_mirror");
    });
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
        auto* pill = makePillButton(label, active ? activeColor : toColor(cocosTheme().inactive), 84.f, 30.f);
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

    if (!m_replayListLoaded) {
        engine->reloadMacroList();
        m_replayListLoaded = true;
    }

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
        if (auto* setting = toasty::frontend::keybindSettingId("autoclicker")) {
            content->addChild(KeybindCell::create("Toggle Autoclicker", setting));
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
        toasty::frontend::setMenuFrontend(idx == 1);
        if (idx == 1) {
            TRMenuPopup::reopen();
        } else {
            geode::queueInMainThread([] {
                if (TRMenuPopup::isOpen()) TRMenuPopup::toggle();
                toasty::frontend::toggleMenu();
            });
        }
    }));

    auto themeNames = toasty::frontend::cocosThemeNames();
    auto currentTheme = toasty::frontend::cocosThemeName();
    auto themeIt = std::find(themeNames.begin(), themeNames.end(), currentTheme);
    int themeIndex = themeIt == themeNames.end() ? 0 : static_cast<int>(std::distance(themeNames.begin(), themeIt));
    content->addChild(ComboCell::create("Cocos Theme", themeNames, themeIndex, [themeNames](int idx) {
        if (idx >= 0 && idx < static_cast<int>(themeNames.size())) {
            toasty::frontend::setCocosTheme(themeNames[idx]);
            TRMenuPopup::reopen();
        }
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
            toasty::lang::refresh();
            TRMenuPopup::refreshOpenMenu();
        }
    }));

    content->addChild(SectionHeaderCell::create("Controls"));
    content->addChild(ButtonCell::create("Keybinds", "Open Manager", [] {
        toasty::frontend::openKeybindEditor();
    }));
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

void TRMenuPopup::refreshOpenMenu() {
    g_menuRevision.fetch_add(1, std::memory_order_relaxed);
}

void TRMenuPopup::reopen() {
    geode::queueInMainThread([] {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) {
            return;
        }
        if (auto* popup = scene->getChildByID("cocos-menu"_spr)) {
            popup->removeFromParentAndCleanup(true);
        }
        if (auto* popup = TRMenuPopup::create()) {
            popup->show();
        }
    });
}

}
