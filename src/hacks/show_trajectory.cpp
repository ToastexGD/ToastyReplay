#include "show_trajectory.hpp"
#include "../checkpoint_handler.hpp"
#include "../ToastyReplay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>

PathVisualizer& visualizer = PathVisualizer::get();

$execute {
    visualizer.pressColor = ccc4FFromccc3B(Mod::get()->getSavedValue<cocos2d::ccColor3B>("trajectory_color1", {0, 255, 0}));
    visualizer.releaseColorVal = ccc4FFromccc3B(Mod::get()->getSavedValue<cocos2d::ccColor3B>("trajectory_color2", {255, 0, 0}));
    visualizer.frameCount = Mod::get()->getSavedValue<int>("trajectory_length", 240);
    visualizer.refreshMergedColor();
}

void PathVisualizer::disable() {
    if (visualizer.visualizerNode()) {
        visualizer.visualizerNode()->clear();
        visualizer.visualizerNode()->setVisible(false);
    }
}

bool PathVisualizer::isModePortal(int id) {
    return modePortalIdentifiers.contains(id);
}

bool PathVisualizer::isGravityFlip(int id) {
    return gravityFlipIdentifiers.contains(id);
}

bool PathVisualizer::isVelocityPortal(int id) {
    return velocityPortalIdentifiers.contains(id);
}

bool PathVisualizer::isScalePortal(int id) {
    return scalePortalIdentifiers.contains(id);
}

bool PathVisualizer::isJumpRing(int id) {
    return jumpRingIdentifiers.contains(id);
}

bool PathVisualizer::isBouncePad(int id) {
    return bouncePadIdentifiers.contains(id);
}

bool PathVisualizer::isDashRing(int id) {
    return dashRingIdentifiers.contains(id);
}

bool PathVisualizer::shouldProcessObject(GameObject* obj) {
    if (!obj) return false;

    int id = obj->m_objectID;

    if (isModePortal(id) || isGravityFlip(id) || isVelocityPortal(id) || isScalePortal(id))
        return true;

    if (isJumpRing(id) || isBouncePad(id))
        return true;

    if (pathObjectCategories.contains(static_cast<int>(obj->m_objectType)))
        return true;

    return false;
}

void PathVisualizer::processPortal(PlayerObject* player, GameObject* obj) {
    if (!obj || !player) return;

    int id = obj->m_objectID;

    if (id == 101) {
        player->togglePlayerScale(true, true);
    } else if (id == 99) {
        player->togglePlayerScale(false, true);
    }

    else if (id == 200) player->m_playerSpeed = 0.7f;
    else if (id == 201) player->m_playerSpeed = 0.9f;
    else if (id == 202) player->m_playerSpeed = 1.1f;
    else if (id == 203) player->m_playerSpeed = 1.3f;
    else if (id == 1334) player->m_playerSpeed = 1.6f;

    else if (id == 10) {
        if (player->m_isUpsideDown) {
            player->flipGravity(false, true);
        }
    } else if (id == 11) {
        if (!player->m_isUpsideDown) {
            player->flipGravity(true, true);
        }
    }

    else if (id == 12) {
        player->toggleFlyMode(false, false);
        player->toggleBirdMode(false, false);
        player->toggleRollMode(false, false);
        player->toggleDartMode(false, false);
        player->toggleRobotMode(false, false);
        player->toggleSpiderMode(false, false);
        player->toggleSwingMode(false, false);
    }
    else if (id == 13) {
        player->toggleFlyMode(true, true);
    }
    else if (id == 47) {
        player->toggleRollMode(true, true);
    }
    else if (id == 111) {
        player->toggleBirdMode(true, true);
    }
    else if (id == 660) {
        player->toggleDartMode(true, true);
    }
    else if (id == 745) {
        player->toggleRobotMode(true, true);
    }
    else if (id == 1331) {
        player->toggleSpiderMode(true, true);
    }
    else if (id == 1933) {
        player->toggleSwingMode(true, true);
    }
}

void PathVisualizer::processRing(PlayerObject* player, RingObject* ring, PlayLayer* pl) {
    if (!ring || !player || !pl) return;

    int id = ring->m_objectID;

    if (visualizer.triggeredObjects.contains(ring)) return;
    visualizer.triggeredObjects.insert(ring);

    if (id == 36) {
        player->m_yVelocity = player->m_isUpsideDown ? -16.0 : 16.0;
    }
    else if (id == 84) {
        player->m_yVelocity = player->m_isUpsideDown ? -12.0 : 12.0;
    }
    else if (id == 141) {
        player->m_yVelocity = player->m_isUpsideDown ? -20.0 : 20.0;
    }
    else if (id == 1022) {
        player->flipGravity(!player->m_isUpsideDown, true);
        player->m_yVelocity = player->m_isUpsideDown ? -16.0 : 16.0;
    }
    else if (id == 1330 || isDashRing(id)) {
        player->m_yVelocity = player->m_isUpsideDown ? -14.0 : 14.0;
    }
    else if (id == 1704 && player->m_isSpider) {
        player->flipGravity(!player->m_isUpsideDown, true);
    }
}

void PathVisualizer::processPad(PlayerObject* player, GameObject* pad) {
    if (!pad || !player) return;

    int id = pad->m_objectID;

    if (visualizer.triggeredObjects.contains(pad)) return;
    visualizer.triggeredObjects.insert(pad);

    if (id == 35) {
        player->m_yVelocity = player->m_isUpsideDown ? -15.0 : 15.0;
        player->m_isOnGround = false;
    }
    else if (id == 67) {
        player->m_yVelocity = player->m_isUpsideDown ? -11.0 : 11.0;
        player->m_isOnGround = false;
    }
    else if (id == 140) {
        player->m_yVelocity = player->m_isUpsideDown ? -19.0 : 19.0;
        player->m_isOnGround = false;
    }
    else if (id == 1332 && player->m_isSpider) {
        player->flipGravity(!player->m_isUpsideDown, true);
    }
}

void PathVisualizer::refresh(PlayLayer* pl) {
    if (!visualizer.shadowPlayer1 || !visualizer.shadowPlayer2) return;

    auto* engine = ReplayEngine::get();

    engine->safeMode = true;
    visualizer.buildingPath = true;
    engine->creatingTrajectory = true;
    visualizer.triggeredObjects.clear();

    visualizer.visualizerNode()->setVisible(true);
    visualizer.visualizerNode()->clear();

    if (visualizer.shadowPlayer1 && pl->m_player1) {
        buildPath(pl, visualizer.shadowPlayer1, pl->m_player1, true);
        visualizer.triggeredObjects.clear();
        buildPath(pl, visualizer.shadowPlayer2, pl->m_player1, false);

        if (engine->trajectoryBothSides) {
            visualizer.triggeredObjects.clear();
            buildPath(pl, visualizer.shadowPlayer1, pl->m_player1, true, true);
            visualizer.triggeredObjects.clear();
            buildPath(pl, visualizer.shadowPlayer2, pl->m_player1, false, true);
        }
    }

    if (pl->m_gameState.m_isDualMode && pl->m_player2) {
        visualizer.triggeredObjects.clear();
        buildPath(pl, visualizer.shadowPlayer2, pl->m_player2, true);
        visualizer.triggeredObjects.clear();
        buildPath(pl, visualizer.shadowPlayer1, pl->m_player2, false);

        if (engine->trajectoryBothSides) {
            visualizer.triggeredObjects.clear();
            buildPath(pl, visualizer.shadowPlayer2, pl->m_player2, true, true);
            visualizer.triggeredObjects.clear();
            buildPath(pl, visualizer.shadowPlayer1, pl->m_player2, false, true);
        }
    }

    visualizer.buildingPath = false;
    engine->creatingTrajectory = false;
}

void PathVisualizer::buildPath(PlayLayer* pl, PlayerObject* shadow, PlayerObject* actual, bool pressing, bool flipped) {
    bool isSecondPlayer = pl->m_player2 == actual;

    PhysicsSnapshot playerData = PlayerStateRestorer::captureState(actual);
    PlayerStateRestorer::restoreState(shadow, playerData, false, true);

    visualizer.pathAborted = false;

    for (int i = 0; i < visualizer.frameCount; i++) {
        CCPoint prevPos = shadow->getPosition();

        if (pressing) {
            if (isSecondPlayer)
                visualizer.p2Coords[i] = prevPos;
            else
                visualizer.p1Coords[i] = prevPos;
        }

        shadow->m_collisionLogTop->removeAllObjects();
        shadow->m_collisionLogBottom->removeAllObjects();
        shadow->m_collisionLogLeft->removeAllObjects();
        shadow->m_collisionLogRight->removeAllObjects();

        pl->checkCollisions(shadow, visualizer.frameDelta, false);

        if (visualizer.pathAborted) {
            shadow->updatePlayerScale();
            renderBounds(shadow, visualizer.visualizerNode());
            break;
        }

        if (i == 0) {
            if (pressing) {
                shadow->pushButton(static_cast<PlayerButton>(1));
            } else {
                shadow->releaseButton(static_cast<PlayerButton>(1));
            }

            if (pl->m_levelSettings->m_platformerMode) {
                bool goingLeft = flipped ? !actual->m_isGoingLeft : actual->m_isGoingLeft;
                if (goingLeft) {
                    shadow->pushButton(static_cast<PlayerButton>(2));
                } else {
                    shadow->pushButton(static_cast<PlayerButton>(3));
                }
            }
        }

        shadow->update(visualizer.frameDelta);
        shadow->updateRotation(visualizer.frameDelta);
        shadow->updatePlayerScale();

        cocos2d::ccColor4F lineColor = pressing ? visualizer.pressColor : visualizer.releaseColorVal;

        if (!pressing) {
            CCPoint* compareArr = isSecondPlayer ? visualizer.p2Coords : visualizer.p1Coords;
            if (compareArr[i] == prevPos) {
                lineColor = visualizer.mergedColor;
            }
        }

        if (i >= visualizer.frameCount - 40) {
            lineColor.a = static_cast<float>(visualizer.frameCount - i) / 40.f;
        }

        visualizer.visualizerNode()->drawSegment(prevPos, shadow->getPosition(), 0.6f, lineColor);
    }
}

void PathVisualizer::renderBounds(PlayerObject* player, CCDrawNode* node) {
    cocos2d::CCRect bigRect = player->getObjectRect();
    cocos2d::CCRect smallRect = player->getObjectRect(0.3f, 0.3f);

    std::vector<cocos2d::CCPoint> vertices = computeVertices(player, bigRect, visualizer.deathAngle);
    node->drawPolygon(&vertices[0], 4, ccc4f(visualizer.releaseColorVal.r, visualizer.releaseColorVal.g, visualizer.releaseColorVal.b, 0.2f), 0.5f, visualizer.releaseColorVal);

    vertices = computeVertices(player, smallRect, visualizer.deathAngle);
    node->drawPolygon(&vertices[0], 4, ccc4f(visualizer.mergedColor.r, visualizer.mergedColor.g, visualizer.mergedColor.b, 0.2f), 0.35f, ccc4f(visualizer.mergedColor.r, visualizer.mergedColor.g, visualizer.mergedColor.b, 0.55f));
}

std::vector<cocos2d::CCPoint> PathVisualizer::computeVertices(PlayerObject* player, cocos2d::CCRect bounds, float angle) {
    std::vector<cocos2d::CCPoint> vertices = {
        ccp(bounds.getMinX(), bounds.getMaxY()),
        ccp(bounds.getMaxX(), bounds.getMaxY()),
        ccp(bounds.getMaxX(), bounds.getMinY()),
        ccp(bounds.getMinX(), bounds.getMinY())
    };

    cocos2d::CCPoint center = ccp(
        (bounds.getMinX() + bounds.getMaxX()) / 2.f,
        (bounds.getMinY() + bounds.getMaxY()) / 2.f
    );

    float size = bounds.getMaxX() - bounds.getMinX();

    if ((static_cast<int>(size) == 18 || static_cast<int>(size) == 5) && player->getScale() == 1.f) {
        for (auto& vertex : vertices) {
            vertex.x = center.x + (vertex.x - center.x) / 0.6f;
            vertex.y = center.y + (vertex.y - center.y) / 0.6f;
        }
    }

    if ((static_cast<int>(size) == 7 || static_cast<int>(size) == 30 ||
         static_cast<int>(size) == 29 || static_cast<int>(size) == 9) && player->getScale() != 1.f) {
        for (auto& vertex : vertices) {
            vertex.x = center.x + (vertex.x - center.x) * 0.6f;
            vertex.y = center.y + (vertex.y - center.y) * 0.6f;
        }
    }

    if (player->m_isDart) {
        for (auto& vertex : vertices) {
            vertex.x = center.x + (vertex.x - center.x) * 0.3f;
            vertex.y = center.y + (vertex.y - center.y) * 0.3f;
        }
    }

    float radians = CC_DEGREES_TO_RADIANS(angle * -1.f);
    for (auto& vertex : vertices) {
        float x = vertex.x - center.x;
        float y = vertex.y - center.y;

        vertex.x = center.x + (x * cos(radians)) - (y * sin(radians));
        vertex.y = center.y + (x * sin(radians)) + (y * cos(radians));
    }

    return vertices;
}

void PathVisualizer::refreshMergedColor() {
    cocos2d::ccColor4F newColor = {0.f, 0.f, 0.f, 1.f};

    newColor.r = (pressColor.r + releaseColorVal.r) / 2.f;
    newColor.b = (pressColor.b + releaseColorVal.b) / 2.f;
    newColor.g = (pressColor.g + releaseColorVal.g) / 2.f;

    newColor.r = std::min(1.f, newColor.r + 0.45f);
    newColor.g = std::min(1.f, newColor.g + 0.45f);
    newColor.b = std::min(1.f, newColor.b + 0.45f);

    mergedColor = newColor;
}

cocos2d::CCDrawNode* PathVisualizer::visualizerNode() {
    static VisualizerNode* instance = nullptr;

    if (!instance) {
        instance = VisualizerNode::create();
        instance->retain();

        cocos2d::ccBlendFunc blendFunc;
        blendFunc.src = GL_SRC_ALPHA;
        blendFunc.dst = GL_ONE_MINUS_SRC_ALPHA;
        instance->setBlendFunc(blendFunc);
    }

    return instance;
}

class $modify(PathVisualizerPlayLayer, PlayLayer) {

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!visualizer.visualizerNode() || visualizer.buildingPath) return;

        if (ReplayEngine::get()->showTrajectory) {
            PathVisualizer::refresh(this);
        }
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        visualizer.shadowPlayer1 = nullptr;
        visualizer.shadowPlayer2 = nullptr;
        visualizer.pathAborted = false;
        visualizer.buildingPath = false;

        visualizer.shadowPlayer1 = PlayerObject::create(1, 1, this, this, true);
        visualizer.shadowPlayer1->retain();
        visualizer.shadowPlayer1->setPosition({0, 105});
        visualizer.shadowPlayer1->setVisible(false);
        m_objectLayer->addChild(visualizer.shadowPlayer1);

        visualizer.shadowPlayer2 = PlayerObject::create(1, 1, this, this, true);
        visualizer.shadowPlayer2->retain();
        visualizer.shadowPlayer2->setPosition({0, 105});
        visualizer.shadowPlayer2->setVisible(false);
        m_objectLayer->addChild(visualizer.shadowPlayer2);

        m_objectLayer->addChild(visualizer.visualizerNode(), 500);
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        if (visualizer.buildingPath || player == visualizer.shadowPlayer1 || player == visualizer.shadowPlayer2) {
            visualizer.deathAngle = player->getRotation();
            visualizer.pathAborted = true;
            return;
        }

        PlayLayer::destroyPlayer(player, gameObject);
    }

    void onQuit() {
        if (visualizer.visualizerNode())
            visualizer.visualizerNode()->clear();

        visualizer.shadowPlayer1 = nullptr;
        visualizer.shadowPlayer2 = nullptr;
        visualizer.pathAborted = false;
        visualizer.buildingPath = false;

        PlayLayer::onQuit();
    }

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (!visualizer.buildingPath)
            PlayLayer::playEndAnimationToPos(p0);
    }
};

class $modify(PathVisualizerPauseLayer, PauseLayer) {
    void goEdit() {
        if (visualizer.visualizerNode())
            visualizer.visualizerNode()->clear();

        visualizer.shadowPlayer1 = nullptr;
        visualizer.shadowPlayer2 = nullptr;
        visualizer.pathAborted = false;
        visualizer.buildingPath = false;

        PauseLayer::goEdit();
    }
};

class $modify(PathVisualizerBaseLayer, GJBaseGameLayer) {

    void collisionCheckObjects(PlayerObject* p0, gd::vector<GameObject*>* objects, int p2, float p3) {
        if (visualizer.buildingPath) {
            std::vector<GameObject*> disabledObjects;

            for (auto& obj : *objects) {
                if (!obj) continue;

                if (!PathVisualizer::shouldProcessObject(obj)) {
                    if (obj->m_isDisabled || obj->m_isDisabled2) continue;

                    disabledObjects.push_back(obj);
                    obj->m_isDisabled = true;
                    obj->m_isDisabled2 = true;
                }
            }

            GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);

            for (auto& obj : disabledObjects) {
                if (!obj) continue;
                obj->m_isDisabled = false;
                obj->m_isDisabled2 = false;
            }

            return;
        }

        GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);
    }

    bool canBeActivatedByPlayer(PlayerObject* p0, EffectGameObject* p1) {
        if (visualizer.buildingPath) {
            PathVisualizer::processPortal(p0, p1);
            return false;
        }

        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void playerTouchedRing(PlayerObject* p0, RingObject* p1) {
        if (visualizer.buildingPath) {
            if (auto pl = PlayLayer::get()) {
                PathVisualizer::processRing(p0, p1, pl);
            }
            return;
        }

        GJBaseGameLayer::playerTouchedRing(p0, p1);
    }

    void playerTouchedTrigger(PlayerObject* p0, EffectGameObject* p1) {
        if (!visualizer.buildingPath) {
            GJBaseGameLayer::playerTouchedTrigger(p0, p1);
        } else {
            PathVisualizer::processPortal(p0, p1);
        }
    }

    void activateSFXTrigger(SFXTriggerGameObject* p0) {
        if (!visualizer.buildingPath)
            GJBaseGameLayer::activateSFXTrigger(p0);
    }

    void activateSongEditTrigger(SongTriggerGameObject* p0) {
        if (!visualizer.buildingPath)
            GJBaseGameLayer::activateSongEditTrigger(p0);
    }

    void gameEventTriggered(GJGameEvent p0, int p1, int p2) {
        if (!visualizer.buildingPath)
            GJBaseGameLayer::gameEventTriggered(p0, p1, p2);
    }
};

class $modify(PathVisualizerPlayerObject, PlayerObject) {

    void update(float dt) {
        PlayerObject::update(dt);
        visualizer.frameDelta = dt;
    }

    void playSpiderDashEffect(cocos2d::CCPoint p0, cocos2d::CCPoint p1) {
        if (!visualizer.buildingPath)
            PlayerObject::playSpiderDashEffect(p0, p1);
    }

    void incrementJumps() {
        if (!visualizer.buildingPath)
            PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* p0, bool p1) {
        if (!visualizer.buildingPath)
            PlayerObject::ringJump(p0, p1);
    }
};

class $modify(PathVisualizerHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint p0) {
        if (!visualizer.buildingPath)
            HardStreak::addPoint(p0);
    }
};

class $modify(PathVisualizerGameObject, GameObject) {
    void playShineEffect() {
        if (!visualizer.buildingPath)
            GameObject::playShineEffect();
    }
};

class $modify(PathVisualizerEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* p0, int p1, const gd::vector<int>* p2) {
        if (!visualizer.buildingPath)
            EffectGameObject::triggerObject(p0, p1, p2);
    }
};
