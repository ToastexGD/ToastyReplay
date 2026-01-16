#include "show_trajectory.hpp"
#include "../practice_fixes/practice_fixes.hpp"
#include "../includes.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>

ShowTrajectory& t = ShowTrajectory::get();

$execute {
    t.color1 = ccc4FFromccc3B(Mod::get()->getSavedValue<cocos2d::ccColor3B>("trajectory_color1", {0, 255, 0}));
    t.color2 = ccc4FFromccc3B(Mod::get()->getSavedValue<cocos2d::ccColor3B>("trajectory_color2", {255, 0, 0}));
    t.length = Mod::get()->getSavedValue<int>("trajectory_length", 240);
    t.updateMergedColor();
}

void ShowTrajectory::trajectoryOff() {
    if (t.trajectoryNode()) {
        t.trajectoryNode()->clear();
        t.trajectoryNode()->setVisible(false);
    }
}

bool ShowTrajectory::isGamemodePortal(int id) {
    return gamemodePortalIDs.contains(id);
}

bool ShowTrajectory::isGravityPortal(int id) {
    return gravityPortalIDs.contains(id);
}

bool ShowTrajectory::isSpeedPortal(int id) {
    return speedPortalIDs.contains(id);
}

bool ShowTrajectory::isSizePortal(int id) {
    return sizePortalIDs.contains(id);
}

bool ShowTrajectory::isRing(int id) {
    return ringIDs.contains(id);
}

bool ShowTrajectory::isPad(int id) {
    return padIDs.contains(id);
}

bool ShowTrajectory::isDashOrb(int id) {
    return dashOrbIDs.contains(id);
}

bool ShowTrajectory::shouldInteractWithObject(GameObject* obj) {
    if (!obj) return false;
    
    int id = obj->m_objectID;
    
    // Allow portals
    if (isGamemodePortal(id) || isGravityPortal(id) || isSpeedPortal(id) || isSizePortal(id))
        return true;
    
    // Allow rings and pads
    if (isRing(id) || isPad(id))
        return true;
    
    // Allow collision objects (solids, slopes)
    if (trajectoryObjectTypes.contains(static_cast<int>(obj->m_objectType)))
        return true;
    
    return false;
}

void ShowTrajectory::handlePortal(PlayerObject* player, GameObject* obj) {
    if (!obj || !player) return;
    
    int id = obj->m_objectID;
    
    // Size portals
    if (id == 101) { // Big
        player->togglePlayerScale(true, true);
    } else if (id == 99) { // Mini
        player->togglePlayerScale(false, true);
    }
    
    // Speed portals
    else if (id == 200) player->m_playerSpeed = 0.7f;
    else if (id == 201) player->m_playerSpeed = 0.9f;
    else if (id == 202) player->m_playerSpeed = 1.1f;
    else if (id == 203) player->m_playerSpeed = 1.3f;
    else if (id == 1334) player->m_playerSpeed = 1.6f;
    
    // Gravity portals
    else if (id == 10) { // Normal gravity
        if (player->m_isUpsideDown) {
            player->flipGravity(false, true);
        }
    } else if (id == 11) { // Flipped gravity
        if (!player->m_isUpsideDown) {
            player->flipGravity(true, true);
        }
    }
    
    // Gamemode portals
    else if (id == 12) { // Cube
        player->toggleFlyMode(false, false);
        player->toggleBirdMode(false, false);
        player->toggleRollMode(false, false);
        player->toggleDartMode(false, false);
        player->toggleRobotMode(false, false);
        player->toggleSpiderMode(false, false);
        player->toggleSwingMode(false, false);
    }
    else if (id == 13) { // Ship
        player->toggleFlyMode(true, true);
    }
    else if (id == 47) { // Ball
        player->toggleRollMode(true, true);
    }
    else if (id == 111) { // UFO
        player->toggleBirdMode(true, true);
    }
    else if (id == 660) { // Wave
        player->toggleDartMode(true, true);
    }
    else if (id == 745) { // Robot
        player->toggleRobotMode(true, true);
    }
    else if (id == 1331) { // Spider
        player->toggleSpiderMode(true, true);
    }
    else if (id == 1933) { // Swing
        player->toggleSwingMode(true, true);
    }
}

void ShowTrajectory::handleRing(PlayerObject* player, RingObject* ring, PlayLayer* pl) {
    if (!ring || !player || !pl) return;
    
    int id = ring->m_objectID;
    
    // Skip if already activated this trajectory
    if (t.activatedObjects.contains(ring)) return;
    t.activatedObjects.insert(ring);
    
    // Yellow ring
    if (id == 36) {
        player->m_yVelocity = player->m_isUpsideDown ? -16.0 : 16.0;
    }
    // Pink ring
    else if (id == 84) {
        player->m_yVelocity = player->m_isUpsideDown ? -12.0 : 12.0;
    }
    // Red ring
    else if (id == 141) {
        player->m_yVelocity = player->m_isUpsideDown ? -20.0 : 20.0;
    }
    // Green ring (gravity)
    else if (id == 1022) {
        player->flipGravity(!player->m_isUpsideDown, true);
        player->m_yVelocity = player->m_isUpsideDown ? -16.0 : 16.0;
    }
    // Black ring (dash)
    else if (id == 1330 || isDashOrb(id)) {
        // Simplified dash handling - just give velocity boost
        player->m_yVelocity = player->m_isUpsideDown ? -14.0 : 14.0;
    }
    // Spider ring
    else if (id == 1704 && player->m_isSpider) {
        player->flipGravity(!player->m_isUpsideDown, true);
    }
}

void ShowTrajectory::handlePad(PlayerObject* player, GameObject* pad) {
    if (!pad || !player) return;
    
    int id = pad->m_objectID;
    
    // Skip if already activated this trajectory
    if (t.activatedObjects.contains(pad)) return;
    t.activatedObjects.insert(pad);
    
    // Yellow pad
    if (id == 35) {
        player->m_yVelocity = player->m_isUpsideDown ? -15.0 : 15.0;
        player->m_isOnGround = false;
    }
    // Pink pad
    else if (id == 67) {
        player->m_yVelocity = player->m_isUpsideDown ? -11.0 : 11.0;
        player->m_isOnGround = false;
    }
    // Red pad
    else if (id == 140) {
        player->m_yVelocity = player->m_isUpsideDown ? -19.0 : 19.0;
        player->m_isOnGround = false;
    }
    // Spider pad
    else if (id == 1332 && player->m_isSpider) {
        player->flipGravity(!player->m_isUpsideDown, true);
    }
}

void ShowTrajectory::updateTrajectory(PlayLayer* pl) {
    if (!t.fakePlayer1 || !t.fakePlayer2) return;

    auto& g = Global::get();
    
    g.safeMode = true;
    t.creatingTrajectory = true;
    g.creatingTrajectory = true;
    t.activatedObjects.clear();

    t.trajectoryNode()->setVisible(true);
    t.trajectoryNode()->clear();

    if (t.fakePlayer1 && pl->m_player1) {
        createTrajectory(pl, t.fakePlayer1, pl->m_player1, true);
        t.activatedObjects.clear();
        createTrajectory(pl, t.fakePlayer2, pl->m_player1, false);

        if (g.trajectoryBothSides) {
            t.activatedObjects.clear();
            createTrajectory(pl, t.fakePlayer1, pl->m_player1, true, true);
            t.activatedObjects.clear();
            createTrajectory(pl, t.fakePlayer2, pl->m_player1, false, true);
        }
    }

    if (pl->m_gameState.m_isDualMode && pl->m_player2) {
        t.activatedObjects.clear();
        createTrajectory(pl, t.fakePlayer2, pl->m_player2, true);
        t.activatedObjects.clear();
        createTrajectory(pl, t.fakePlayer1, pl->m_player2, false);

        if (g.trajectoryBothSides) {
            t.activatedObjects.clear();
            createTrajectory(pl, t.fakePlayer2, pl->m_player2, true, true);
            t.activatedObjects.clear();
            createTrajectory(pl, t.fakePlayer1, pl->m_player2, false, true);
        }
    }

    t.creatingTrajectory = false;
    g.creatingTrajectory = false;
}

void ShowTrajectory::createTrajectory(PlayLayer* pl, PlayerObject* fakePlayer, PlayerObject* realPlayer, bool hold, bool inverted) {
    bool player2 = pl->m_player2 == realPlayer;

    // Copy player state
    PlayerData playerData = PlayerPracticeFixes::saveData(realPlayer);
    PlayerPracticeFixes::applyData(fakePlayer, playerData, false, true);

    t.cancelTrajectory = false;

    for (int i = 0; i < t.length; i++) {
        CCPoint prevPos = fakePlayer->getPosition();

        if (hold) {
            if (player2)
                t.player2Trajectory[i] = prevPos;
            else
                t.player1Trajectory[i] = prevPos;
        }

        // Clear collision logs
        fakePlayer->m_collisionLogTop->removeAllObjects();
        fakePlayer->m_collisionLogBottom->removeAllObjects();
        fakePlayer->m_collisionLogLeft->removeAllObjects();
        fakePlayer->m_collisionLogRight->removeAllObjects();

        // Run collision check
        pl->checkCollisions(fakePlayer, t.delta, false);

        if (t.cancelTrajectory) {
            fakePlayer->updatePlayerScale();
            drawPlayerHitbox(fakePlayer, t.trajectoryNode());
            break;
        }

        // Handle input on first frame
        if (i == 0) {
            if (hold) {
                fakePlayer->pushButton(static_cast<PlayerButton>(1));
            } else {
                fakePlayer->releaseButton(static_cast<PlayerButton>(1));
            }
            
            if (pl->m_levelSettings->m_platformerMode) {
                bool goingLeft = inverted ? !realPlayer->m_isGoingLeft : realPlayer->m_isGoingLeft;
                if (goingLeft) {
                    fakePlayer->pushButton(static_cast<PlayerButton>(2));
                } else {
                    fakePlayer->pushButton(static_cast<PlayerButton>(3));
                }
            }
        }

        // Update player physics
        fakePlayer->update(t.delta);
        fakePlayer->updateRotation(t.delta);
        fakePlayer->updatePlayerScale();

        // Determine line color
        cocos2d::ccColor4F color = hold ? t.color1 : t.color2;

        if (!hold) {
            CCPoint* compareArr = player2 ? t.player2Trajectory : t.player1Trajectory;
            if (compareArr[i] == prevPos) {
                color = t.color3;
            }
        }

        // Fade out near the end
        if (i >= t.length - 40) {
            color.a = static_cast<float>(t.length - i) / 40.f;
        }

        t.trajectoryNode()->drawSegment(prevPos, fakePlayer->getPosition(), 0.6f, color);
    }
}

void ShowTrajectory::drawPlayerHitbox(PlayerObject* player, CCDrawNode* drawNode) {
    cocos2d::CCRect bigRect = player->getObjectRect();
    cocos2d::CCRect smallRect = player->getObjectRect(0.3f, 0.3f);

    std::vector<cocos2d::CCPoint> vertices = getVertices(player, bigRect, t.deathRotation);
    drawNode->drawPolygon(&vertices[0], 4, ccc4f(t.color2.r, t.color2.g, t.color2.b, 0.2f), 0.5f, t.color2);

    vertices = getVertices(player, smallRect, t.deathRotation);
    drawNode->drawPolygon(&vertices[0], 4, ccc4f(t.color3.r, t.color3.g, t.color3.b, 0.2f), 0.35f, ccc4f(t.color3.r, t.color3.g, t.color3.b, 0.55f));
}

std::vector<cocos2d::CCPoint> ShowTrajectory::getVertices(PlayerObject* player, cocos2d::CCRect rect, float rotation) {
    std::vector<cocos2d::CCPoint> vertices = {
        ccp(rect.getMinX(), rect.getMaxY()),
        ccp(rect.getMaxX(), rect.getMaxY()),
        ccp(rect.getMaxX(), rect.getMinY()),
        ccp(rect.getMinX(), rect.getMinY())
    };

    cocos2d::CCPoint center = ccp(
        (rect.getMinX() + rect.getMaxX()) / 2.f,
        (rect.getMinY() + rect.getMaxY()) / 2.f
    );

    float size = rect.getMaxX() - rect.getMinX();

    // Adjust for hitbox scaling
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

    // Wave hitbox adjustment
    if (player->m_isDart) {
        for (auto& vertex : vertices) {
            vertex.x = center.x + (vertex.x - center.x) * 0.3f;
            vertex.y = center.y + (vertex.y - center.y) * 0.3f;
        }
    }

    // Apply rotation
    float angle = CC_DEGREES_TO_RADIANS(rotation * -1.f);
    for (auto& vertex : vertices) {
        float x = vertex.x - center.x;
        float y = vertex.y - center.y;

        vertex.x = center.x + (x * cos(angle)) - (y * sin(angle));
        vertex.y = center.y + (x * sin(angle)) + (y * cos(angle));
    }

    return vertices;
}

void ShowTrajectory::updateMergedColor() {
    cocos2d::ccColor4F newColor = {0.f, 0.f, 0.f, 1.f};

    newColor.r = (color1.r + color2.r) / 2.f;
    newColor.b = (color1.b + color2.b) / 2.f;
    newColor.g = (color1.g + color2.g) / 2.f;

    newColor.r = std::min(1.f, newColor.r + 0.45f);
    newColor.g = std::min(1.f, newColor.g + 0.45f);
    newColor.b = std::min(1.f, newColor.b + 0.45f);

    color3 = newColor;
}

cocos2d::CCDrawNode* ShowTrajectory::trajectoryNode() {
    static TrajectoryNode* instance = nullptr;

    if (!instance) {
        instance = TrajectoryNode::create();
        instance->retain();

        cocos2d::ccBlendFunc blendFunc;
        blendFunc.src = GL_SRC_ALPHA;
        blendFunc.dst = GL_ONE_MINUS_SRC_ALPHA;
        instance->setBlendFunc(blendFunc);
    }

    return instance;
}

// Hooks

class $modify(TrajectoryPlayLayer, PlayLayer) {

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!t.trajectoryNode() || t.creatingTrajectory) return;

        if (Global::get().showTrajectory) {
            ShowTrajectory::updateTrajectory(this);
        }
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        t.fakePlayer1 = nullptr;
        t.fakePlayer2 = nullptr;
        t.cancelTrajectory = false;
        t.creatingTrajectory = false;

        t.fakePlayer1 = PlayerObject::create(1, 1, this, this, true);
        t.fakePlayer1->retain();
        t.fakePlayer1->setPosition({0, 105});
        t.fakePlayer1->setVisible(false);
        m_objectLayer->addChild(t.fakePlayer1);

        t.fakePlayer2 = PlayerObject::create(1, 1, this, this, true);
        t.fakePlayer2->retain();
        t.fakePlayer2->setPosition({0, 105});
        t.fakePlayer2->setVisible(false);
        m_objectLayer->addChild(t.fakePlayer2);

        m_objectLayer->addChild(t.trajectoryNode(), 500);
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        if (t.creatingTrajectory || player == t.fakePlayer1 || player == t.fakePlayer2) {
            t.deathRotation = player->getRotation();
            t.cancelTrajectory = true;
            return;
        }

        PlayLayer::destroyPlayer(player, gameObject);
    }

    void onQuit() {
        if (t.trajectoryNode())
            t.trajectoryNode()->clear();

        t.fakePlayer1 = nullptr;
        t.fakePlayer2 = nullptr;
        t.cancelTrajectory = false;
        t.creatingTrajectory = false;

        PlayLayer::onQuit();
    }

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (!t.creatingTrajectory)
            PlayLayer::playEndAnimationToPos(p0);
    }
};

class $modify(TrajectoryPauseLayer, PauseLayer) {
    void goEdit() {
        if (t.trajectoryNode())
            t.trajectoryNode()->clear();

        t.fakePlayer1 = nullptr;
        t.fakePlayer2 = nullptr;
        t.cancelTrajectory = false;
        t.creatingTrajectory = false;

        PauseLayer::goEdit();
    }
};

class $modify(TrajectoryBGL, GJBaseGameLayer) {

    void collisionCheckObjects(PlayerObject* p0, gd::vector<GameObject*>* objects, int p2, float p3) {
        if (t.creatingTrajectory) {
            std::vector<GameObject*> disabledObjects;

            for (auto& obj : *objects) {
                if (!obj) continue;

                // Only disable objects that shouldn't be interacted with
                if (!ShowTrajectory::shouldInteractWithObject(obj)) {
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
        if (t.creatingTrajectory) {
            ShowTrajectory::handlePortal(p0, p1);
            return false;
        }

        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void playerTouchedRing(PlayerObject* p0, RingObject* p1) {
        if (t.creatingTrajectory) {
            // Handle ring in trajectory mode
            if (auto pl = PlayLayer::get()) {
                ShowTrajectory::handleRing(p0, p1, pl);
            }
            return;
        }

        GJBaseGameLayer::playerTouchedRing(p0, p1);
    }

    void playerTouchedTrigger(PlayerObject* p0, EffectGameObject* p1) {
        if (!t.creatingTrajectory) {
            GJBaseGameLayer::playerTouchedTrigger(p0, p1);
        } else {
            ShowTrajectory::handlePortal(p0, p1);
        }
    }

    void activateSFXTrigger(SFXTriggerGameObject* p0) {
        if (!t.creatingTrajectory)
            GJBaseGameLayer::activateSFXTrigger(p0);
    }

    void activateSongEditTrigger(SongTriggerGameObject* p0) {
        if (!t.creatingTrajectory)
            GJBaseGameLayer::activateSongEditTrigger(p0);
    }

    void gameEventTriggered(GJGameEvent p0, int p1, int p2) {
        if (!t.creatingTrajectory)
            GJBaseGameLayer::gameEventTriggered(p0, p1, p2);
    }
};

class $modify(TrajectoryPlayerObject, PlayerObject) {

    void update(float dt) {
        PlayerObject::update(dt);
        t.delta = dt;
    }

    void playSpiderDashEffect(cocos2d::CCPoint p0, cocos2d::CCPoint p1) {
        if (!t.creatingTrajectory)
            PlayerObject::playSpiderDashEffect(p0, p1);
    }

    void incrementJumps() {
        if (!t.creatingTrajectory)
            PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* p0, bool p1) {
        if (!t.creatingTrajectory)
            PlayerObject::ringJump(p0, p1);
    }
};

class $modify(TrajectoryHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint p0) {
        if (!t.creatingTrajectory)
            HardStreak::addPoint(p0);
    }
};

class $modify(TrajectoryGameObject, GameObject) {
    void playShineEffect() {
        if (!t.creatingTrajectory)
            GameObject::playShineEffect();
    }
};

class $modify(TrajectoryEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* p0, int p1, const gd::vector<int>* p2) {
        if (!t.creatingTrajectory)
            EffectGameObject::triggerObject(p0, p1, p2);
    }
};
