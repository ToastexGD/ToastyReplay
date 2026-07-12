#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace toasty::frontend {

class TRMenuPopup : public geode::Popup {
protected:
    cocos2d::CCMenu* m_tabMenu = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    int m_activeTab = -1;
    int m_mainSubTab = 0;
    bool m_clickPacksScanned = false;
    bool m_replayListLoaded = false;
    bool m_onlineMacrosLoaded = false;
    int m_uploadMacroIndex = 0;
    std::string m_uploadComment;
    std::string m_issueTitle;
    std::string m_issueDesc;
    std::string m_toolsTps;
    std::string m_toolsSpeed;
    std::string m_replayMacroFilter;
    int m_replayPage = 0;
    std::string m_renderPresetName;
    int m_renderPresetIndex = 0;
    int m_separatoryP1Index = 0;
    int m_separatoryP2Index = 0;
    int m_separatoryOutputAccuracy = 0;
    std::string m_separatoryOutputName;
    bool m_preserveScroll = false;
    bool m_subTabChanged = false;
    int m_pendingTab = -1;
    bool m_tabSwitchQueued = false;
    std::uint64_t m_seenRevision = 0;

    bool init() override;

    void buildTabBar();
    void switchTab(int index);
    void applyTab(int index);
    void buildTabContent(int index);
    void buildMainTab(cocos2d::CCNode* content);
    void buildReplaySection(cocos2d::CCNode* content);
    void buildSeparatorySection(cocos2d::CCNode* content);
    void buildHacksSection(cocos2d::CCNode* content);
    void addHackSubOptions(cocos2d::CCNode* content, std::string const& id);
    void addSub(cocos2d::CCNode* content, cocos2d::CCNode* cell, std::string const& ownerId);
    void buildToolsSection(cocos2d::CCNode* content);
    void buildScreenLabelsSection(cocos2d::CCNode* content);
    void buildRenderTab(cocos2d::CCNode* content);
    void buildClicksTab(cocos2d::CCNode* content);
    void buildAutoclickerTab(cocos2d::CCNode* content);
    void buildSettingsTab(cocos2d::CCNode* content);
    void buildOnlineTab(cocos2d::CCNode* content);
    void refreshOnlineWhilePolling(float dt);
    void refreshIfNeeded(float dt);

public:
    static TRMenuPopup* create();
    static void toggle();
    static bool isOpen();
    static void refreshOpenMenu();
    static void reopen();
};

}
