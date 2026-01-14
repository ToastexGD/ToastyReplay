#include "ToastyReplay.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>

using namespace geode::prelude;

class $modify(SafeModePlayLayer, PlayLayer) {
    void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
        if (!ToastyReplay::get()->safeMode)
            PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
    }

    void levelComplete() {
        ToastyReplay* mgr = ToastyReplay::get();
        bool wasTestMode = m_isTestMode;

        if (mgr->safeMode)
            m_isTestMode = true;

        if (m_isPracticeMode)
            mgr->safeMode = false;

        PlayLayer::levelComplete();

        m_isTestMode = wasTestMode;
    }
};

class $modify(SafeModeEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        ToastyReplay* mgr = ToastyReplay::get();

        if (!mgr->safeMode) return;

        CCLabelBMFont* lbl = CCLabelBMFont::create("Safe Mode Active", "goldFont.fnt");
        lbl->setPosition({ 3.5, 10 });
        lbl->setOpacity(155);
        lbl->setID("safe-mode-label"_spr);
        lbl->setScale(0.55f);
        lbl->setAnchorPoint({ 0, 0.5 });

        addChild(lbl);
    }

    void onHideLayer(CCObject* obj) {
        EndLevelLayer::onHideLayer(obj);

        if (CCNode* lbl = getChildByID("safe-mode-label"_spr))
            lbl->setVisible(!lbl->isVisible());
    }
};

class $modify(SafeModeGJGameLevel, GJGameLevel) {
    void savePercentage(int p0, bool p1, int p2, int p3, bool p4) {
        if (!ToastyReplay::get()->safeMode)
            GJGameLevel::savePercentage(p0, p1, p2, p3, p4);
    }
};
