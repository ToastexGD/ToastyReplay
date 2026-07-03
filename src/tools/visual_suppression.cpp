#include "ToastyReplay.hpp"

#include <Geode/modify/CCCircleWave.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    bool noEffectsEnabled() {
        auto* engine = ReplayEngine::get();
        return engine && engine->noEffect;
    }

    bool noDeathEffectEnabled() {
        auto* engine = ReplayEngine::get();
        return engine && engine->noDeathEffect;
    }
}

class $modify(VisualSuppressionCircleWave, CCCircleWave) {
    void draw() {
        if (noEffectsEnabled()) {
            return;
        }
        CCCircleWave::draw();
    }
};

class $modify(VisualSuppressionEffectObject, EffectGameObject) {
    void playTriggerEffect() {
        if (noEffectsEnabled()) {
            return;
        }
        EffectGameObject::playTriggerEffect();
    }
};

class $modify(VisualSuppressionEndLayer, EndLevelLayer) {
    void showLayer(bool instant) {
        auto* engine = ReplayEngine::get();
        if (engine && engine->hideEndscreen) {
            return;
        }
        EndLevelLayer::showLayer(instant);
    }
};

class $modify(VisualSuppressionBaseLayer, GJBaseGameLayer) {
    void playExitDualEffect(PlayerObject* player) {
        if (m_playerDied && noDeathEffectEnabled()) {
            return;
        }
        GJBaseGameLayer::playExitDualEffect(player);
    }
};

class $modify(VisualSuppressionGameObject, GameObject) {
    void playShineEffect() {
        if (noEffectsEnabled()) {
            return;
        }
        GameObject::playShineEffect();
    }
};

class $modify(VisualSuppressionPlayerObject, PlayerObject) {
    void spawnPortalCircle(cocos2d::ccColor3B color, float startRadius) {
        if (noEffectsEnabled()) {
            return;
        }
        PlayerObject::spawnPortalCircle(color, startRadius);
    }

    void playDeathEffect() {
        if (noDeathEffectEnabled()) {
            return;
        }
        PlayerObject::playDeathEffect();
    }

    void stopDashing() {
        if (noEffectsEnabled() && m_dashFireSprite) {
            m_dashFireSprite->setScale(0.0f);
        }
        PlayerObject::stopDashing();
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (noEffectsEnabled()) {
            return;
        }
        PlayerObject::playSpiderDashEffect(from, to);
    }
};

class $modify(VisualSuppressionPlayLayer, PlayLayer) {
    void showNewBest(bool newReward, int orbs, int diamonds, bool demonKey, bool noRetry, bool noTitle) {
        auto* engine = ReplayEngine::get();
        if (engine && engine->hideNewBest) {
            return;
        }
        PlayLayer::showNewBest(newReward, orbs, diamonds, demonKey, noRetry, noTitle);
    }

    void playGravityEffect(bool flip) {
        if (noEffectsEnabled()) {
            return;
        }
        PlayLayer::playGravityEffect(flip);
    }
};
