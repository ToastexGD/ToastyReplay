#include "ToastyReplay.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>

using namespace geode::prelude;

class $modify(ProtectedPlayLayer, PlayLayer) {
    void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
        if (!ReplayEngine::get()->protectedMode)
            PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
    }

    void levelComplete() {
        ReplayEngine* engine = ReplayEngine::get();
        bool originalTestMode = m_isTestMode;

        if (engine->protectedMode)
            m_isTestMode = true;

        if (m_isPracticeMode) {
        } else {
            engine->protectedMode = false;
        }

        PlayLayer::levelComplete();

        m_isTestMode = originalTestMode;
    }
};

class $modify(ProtectedEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        ReplayEngine* engine = ReplayEngine::get();

        if (!engine->protectedMode) return;

        CCLabelBMFont* indicator = CCLabelBMFont::create("Safe Mode Active", "goldFont.fnt");
        indicator->setPosition({ 3.5, 10 });
        indicator->setOpacity(155);
        indicator->setID("protected-mode-indicator"_spr);
        indicator->setScale(0.55f);
        indicator->setAnchorPoint({ 0, 0.5 });

        addChild(indicator);
    }

    void onHideLayer(CCObject* obj) {
        EndLevelLayer::onHideLayer(obj);

        if (CCNode* indicator = getChildByID("protected-mode-indicator"_spr))
            indicator->setVisible(!indicator->isVisible());
    }
};

class $modify(ProtectedGJGameLevel, GJGameLevel) {
    void savePercentage(int p0, bool p1, int p2, int p3, bool p4) {
        if (!ReplayEngine::get()->protectedMode)
            GJGameLevel::savePercentage(p0, p1, p2, p3, p4);
    }
};
