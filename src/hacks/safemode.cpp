#include "ToastyReplay.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJGameLevel.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace {

static bool safeModeEnabled() {
    auto* engine = ReplayEngine::get();
    if (!engine) return false;
    if (engine->protectedMode) return true;
    if (engine->autoSafeMode && engine->engineMode != MODE_DISABLED) return true;
    return false;
}

struct TestModeOverride {
    PlayLayer* m_layer;
    bool m_original;

    TestModeOverride(PlayLayer* layer, bool activate)
        : m_layer(layer), m_original(layer->m_isTestMode) {
        if (activate) m_layer->m_isTestMode = true;
    }

    ~TestModeOverride() {
        m_layer->m_isTestMode = m_original;
    }
};

}

class $modify(GuardedGJGameLevel, GJGameLevel) {
    void savePercentage(int p0, bool p1, int p2, int p3, bool p4) {
        if (!safeModeEnabled())
            GJGameLevel::savePercentage(p0, p1, p2, p3, p4);
    }
};

class $modify(GuardedEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();

        if (!safeModeEnabled()) return;

        auto* container = CCNodeRGBA::create();
        container->setID("safemode-badge"_spr);
        container->setAnchorPoint({ 0.f, 0.f });
        container->setScale(0.5f);
        container->setCascadeOpacityEnabled(true);
        container->setOpacity(180);

        auto* icon = CCSprite::createWithSpriteFrameName("GJ_lock_001.png");
        if (!icon || icon->getContentSize().width <= 0.f) {
            icon = CCSprite::createWithSpriteFrameName("GJ_padlockOpen_001.png");
        }
        if (!icon || icon->getContentSize().width <= 0.f) {
            icon = CCSprite::create("GJ_lockGray_001.png");
        }
        icon->setAnchorPoint({ 0.f, 0.5f });
        icon->setPosition({ 0.f, 10.f });
        container->addChild(icon);

        auto* label = CCLabelBMFont::create("Safe Mode Active", "goldFont.fnt");
        label->setAnchorPoint({ 0.f, 0.5f });
        label->setPosition({ icon->getContentSize().width + 4.f, 10.f });
        container->addChild(label);

        container->setContentSize({
            icon->getContentSize().width + 4.f + label->getContentSize().width,
            20.f
        });
        container->setPosition({ 4.f, this->getContentSize().height - 18.f });

        addChild(container);
    }

    void onHideLayer(CCObject* obj) {
        EndLevelLayer::onHideLayer(obj);

        auto* node = getChildByID("safemode-badge"_spr);
        if (node) node->setVisible(!node->isVisible());
    }
};

class $modify(GuardedPlayLayer, PlayLayer) {
    struct Fields {
        float respawnTimer = -1.0f;
        bool respawnScheduled = false;
    };

    void showNewBest(bool p0, int p1, int p2, bool p3, bool p4, bool p5) {
        if (!safeModeEnabled())
            PlayLayer::showNewBest(p0, p1, p2, p3, p4, p5);
    }

    void levelComplete() {
        auto* engine = ReplayEngine::get();
        {
            TestModeOverride guard(this, engine && engine->protectedMode);
            PlayLayer::levelComplete();
        }

        if (engine && engine->completionAutosave && engine->engineMode == MODE_CAPTURE) {
            engine->saveActiveMacro();
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        PlayLayer::destroyPlayer(player, obj);
        auto* engine = ReplayEngine::get();
        if (!engine || !engine->respawnTimeOverrideEnabled) {
            return;
        }
        if (this->m_isPracticeMode || this->m_hasCompletedLevel || this->m_levelEndAnimationStarted) {
            return;
        }
        if (this->m_player1 && !this->m_player1->m_isDead) {
            return;
        }
        float const delaySeconds = std::clamp(engine->respawnTimeOverrideMs, 0, 10000) / 1000.0f;
        m_fields->respawnTimer = delaySeconds;
        m_fields->respawnScheduled = true;
        if (delaySeconds <= 0.0f) {
            m_fields->respawnTimer = -1.0f;
            m_fields->respawnScheduled = false;
            this->resetLevel();
        }
    }

    void delayedResetLevel() {
        if (m_fields->respawnScheduled) {
            return;
        }
        PlayLayer::delayedResetLevel();
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (m_fields->respawnScheduled && m_fields->respawnTimer >= 0.0f) {
            m_fields->respawnTimer -= dt;
            if (m_fields->respawnTimer <= 0.0f) {
                m_fields->respawnScheduled = false;
                m_fields->respawnTimer = -1.0f;
                this->resetLevel();
            }
        }
    }

    void resetLevel() {
        m_fields->respawnScheduled = false;
        m_fields->respawnTimer = -1.0f;
        PlayLayer::resetLevel();
    }
};
