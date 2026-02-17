#include "trajectory.hpp"
#include "ToastyReplay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/HardStreak.hpp>

PathPreviewSystem& pathSystem = PathPreviewSystem::get();

cocos2d::ccColor4F convertToColor4F(cocos2d::ccColor3B rgb) {
    return ccc4f(rgb.r / 255.f, rgb.g / 255.f, rgb.b / 255.f, 1.f);
}

void PathPreviewSystem::disablePreview() {
    auto* node = getDrawNode();
    if (node) {
        node->clear();
        node->setVisible(false);
    }
}

SimulatedPlayerState PathPreviewSystem::capturePlayerState(PlayerObject* player) {
    SimulatedPlayerState state;
    state.position = player->getPosition();
    state.priorPosition = player->m_lastPosition;
    state.verticalVelocity = player->m_yVelocity;
    state.rotation = player->getRotation();
    state.gravityInverted = player->m_isUpsideDown;
    state.onIncline = player->m_isOnSlope;
    state.wasOnIncline = player->m_wasOnSlope;
    state.inShipMode = player->m_isShip;
    state.inUfoMode = player->m_isBird;
    state.inBallMode = player->m_isBall;
    state.inWaveMode = player->m_isDart;
    state.inRobotMode = player->m_isRobot;
    state.inSpiderMode = player->m_isSpider;
    state.inSwingMode = player->m_isSwing;
    state.scale = player->m_vehicleSize;
    state.movementSpeed = player->m_playerSpeed;
    state.grounded = player->m_isOnGround;
    state.dashing = player->m_isDashing;
    state.gravityFactor = player->m_gravityMod;
    state.objType = player->m_objectType;
    return state;
}

void PathPreviewSystem::applySimulatedState(PlayerObject* player, SimulatedPlayerState state) {
    player->setPosition(state.position);
    player->m_lastPosition = state.priorPosition;
    player->m_yVelocity = state.verticalVelocity;
    player->setRotation(state.rotation);
    player->m_isUpsideDown = state.gravityInverted;
    player->m_isOnSlope = state.onIncline;
    player->m_wasOnSlope = state.wasOnIncline;
    player->m_isShip = state.inShipMode;
    player->m_isBird = state.inUfoMode;
    player->m_isBall = state.inBallMode;
    player->m_isDart = state.inWaveMode;
    player->m_isRobot = state.inRobotMode;
    player->m_isSpider = state.inSpiderMode;
    player->m_isSwing = state.inSwingMode;
    player->m_vehicleSize = state.scale;
    player->m_playerSpeed = state.movementSpeed;
    player->m_isOnGround = state.grounded;
    player->m_isDashing = state.dashing;
    player->m_gravityMod = state.gravityFactor;
    player->m_objectType = state.objType;
}

void PathPreviewSystem::refreshPreview(PlayLayer* pl) {
    if (!pathSystem.simulatedPlayer1 || !pathSystem.simulatedPlayer2) return;

    pathSystem.generatingPath = true;

    auto* node = pathSystem.getDrawNode();
    node->setVisible(true);
    node->clear();

    if (pathSystem.simulatedPlayer1 && pl->m_player1) {
        generatePath(pl, pathSystem.simulatedPlayer1, pl->m_player1, true);
        generatePath(pl, pathSystem.simulatedPlayer2, pl->m_player1, false);
    }

    if (pl->m_gameState.m_isDualMode && pl->m_player2) {
        generatePath(pl, pathSystem.simulatedPlayer2, pl->m_player2, true);
        generatePath(pl, pathSystem.simulatedPlayer1, pl->m_player2, false);
    }

    pathSystem.generatingPath = false;
}

void PathPreviewSystem::generatePath(PlayLayer* pl, PlayerObject* simPlayer, PlayerObject* actualPlayer, bool holding, bool mirrored) {
    bool isSecondPlayer = pl->m_player2 == actualPlayer;

    SimulatedPlayerState playerState = capturePlayerState(actualPlayer);
    applySimulatedState(simPlayer, playerState);

    pathSystem.pathCancelled = false;
    pathSystem.simulatingHold = holding;
    pathSystem.touchedRings.clear();

    int frameCount = ReplayEngine::get()->pathLength;

    holding ? simPlayer->pushButton(static_cast<PlayerButton>(1)) : simPlayer->releaseButton(static_cast<PlayerButton>(1));
    if (pl->m_levelSettings->m_platformerMode)
        (mirrored ? !actualPlayer->m_isGoingLeft : actualPlayer->m_isGoingLeft) ? simPlayer->pushButton(static_cast<PlayerButton>(2)) : simPlayer->pushButton(static_cast<PlayerButton>(3));

    for (int i = 0; i < frameCount; i++) {
        CCPoint prevCoord = simPlayer->getPosition();

        if (holding) {
            if (isSecondPlayer)
                pathSystem.p2PathPoints[i] = prevCoord;
            else
                pathSystem.p1PathPoints[i] = prevCoord;
        }

        simPlayer->m_collisionLogTop->removeAllObjects();
        simPlayer->m_collisionLogBottom->removeAllObjects();
        simPlayer->m_collisionLogLeft->removeAllObjects();
        simPlayer->m_collisionLogRight->removeAllObjects();

        pl->checkCollisions(simPlayer, pathSystem.tickDelta, false);

        if (pathSystem.pathCancelled) {
            simPlayer->updatePlayerScale();
            renderPlayerBounds(simPlayer, pathSystem.getDrawNode());
            break;
        }

        simPlayer->update(pathSystem.tickDelta);
        simPlayer->updateRotation(pathSystem.tickDelta);
        simPlayer->updatePlayerScale();

        cocos2d::ccColor4F lineColor = holding ? pathSystem.holdColor : pathSystem.releaseColor;

        if (!holding) {
            if ((isSecondPlayer && pathSystem.p2PathPoints[i] == prevCoord) || (!isSecondPlayer && pathSystem.p1PathPoints[i] == prevCoord))
                lineColor = pathSystem.overlapColor;
        }

        if (i >= frameCount - 40)
            lineColor.a = (frameCount - i) / 40.f;

        pathSystem.getDrawNode()->drawSegment(prevCoord, simPlayer->getPosition(), 0.6f, lineColor);
    }

    for (auto& [ring, wasActivated] : pathSystem.touchedRings) {
        ring->m_activated = wasActivated;
    }
    pathSystem.touchedRings.clear();
    pathSystem.simulatingHold = false;
}

void PathPreviewSystem::renderPlayerBounds(PlayerObject* player, CCDrawNode* drawNode) {
    cocos2d::CCRect outerBounds = player->GameObject::getObjectRect();
    cocos2d::CCRect innerBounds = player->GameObject::getObjectRect(0.3, 0.3);

    std::vector<cocos2d::CCPoint> verts = PathPreviewSystem::calculateBoundingVertices(player, outerBounds, pathSystem.collisionRotation);
    drawNode->drawPolygon(&verts[0], 4, ccc4f(pathSystem.releaseColor.r, pathSystem.releaseColor.g, pathSystem.releaseColor.b, 0.2f), 0.5, pathSystem.releaseColor);

    verts = PathPreviewSystem::calculateBoundingVertices(player, innerBounds, pathSystem.collisionRotation);
    drawNode->drawPolygon(&verts[0], 4, ccc4f(pathSystem.overlapColor.r, pathSystem.overlapColor.g, pathSystem.overlapColor.b, 0.2f), 0.35, ccc4f(pathSystem.overlapColor.r, pathSystem.overlapColor.g, pathSystem.overlapColor.b, 0.55f));
}

std::vector<cocos2d::CCPoint> PathPreviewSystem::calculateBoundingVertices(PlayerObject* player, cocos2d::CCRect bounds, float angle) {
    std::vector<cocos2d::CCPoint> verts = {
        ccp(bounds.getMinX(), bounds.getMaxY()),
        ccp(bounds.getMaxX(), bounds.getMaxY()),
        ccp(bounds.getMaxX(), bounds.getMinY()),
        ccp(bounds.getMinX(), bounds.getMinY())
    };

    cocos2d::CCPoint mid = ccp(
        (bounds.getMinX() + bounds.getMaxX()) / 2.f,
        (bounds.getMinY() + bounds.getMaxY()) / 2.f
    );

    float dimension = static_cast<int>(bounds.getMaxX() - bounds.getMinX());

    if ((dimension == 18 || dimension == 5) && player->getScale() == 1) {
        for (auto& v : verts) {
            v.x = mid.x + (v.x - mid.x) / 0.6f;
            v.y = mid.y + (v.y - mid.y) / 0.6f;
        }
    }

    if ((dimension == 7 || dimension == 30 || dimension == 29 || dimension == 9) && player->getScale() != 1) {
        for (auto& v : verts) {
            v.x = mid.x + (v.x - mid.x) * 0.6;
            v.y = mid.y + (v.y - mid.y) * 0.6f;
        }
    }

    if (player->m_isDart) {
        for (auto& v : verts) {
            v.x = mid.x + (v.x - mid.x) * 0.3f;
            v.y = mid.y + (v.y - mid.y) * 0.3f;
        }
    }

    float radians = CC_DEGREES_TO_RADIANS(angle * -1.f);
    for (auto& v : verts) {
        float dx = v.x - mid.x;
        float dy = v.y - mid.y;

        float rotatedX = mid.x + (dx * cos(radians)) - (dy * sin(radians));
        float rotatedY = mid.y + (dx * sin(radians)) + (dy * cos(radians));

        v.x = rotatedX;
        v.y = rotatedY;
    }

    return verts;
}

void PathPreviewSystem::recalculateMergedColor() {
    cocos2d::ccColor4F blended = { 0.f, 0.f, 0.f, 1.f };

    blended.r = (holdColor.r + releaseColor.r) / 2;
    blended.b = (holdColor.b + releaseColor.b) / 2;
    blended.g = (holdColor.g + releaseColor.g) / 2;

    blended.r = std::min(1.f, blended.r + 0.45f);
    blended.g = std::min(1.f, blended.g + 0.45f);
    blended.b = std::min(1.f, blended.b + 0.45f);

    overlapColor = blended;
}

void PathPreviewSystem::processPortalInteraction(PlayerObject* player, int portalId) {
    if (!interactivePortalIds.contains(portalId)) return;

    switch (portalId) {
    case 101:
        player->togglePlayerScale(true, true);
        player->updatePlayerScale();
        break;
    case 99:
        player->togglePlayerScale(false, true);
        player->updatePlayerScale();
        break;
    case 200: player->m_playerSpeed = 0.7f; break;
    case 201: player->m_playerSpeed = 0.9f; break;
    case 202: player->m_playerSpeed = 1.1f; break;
    case 203: player->m_playerSpeed = 1.3f; break;
    case 1334: player->m_playerSpeed = 1.6f; break;
    }
}

cocos2d::CCDrawNode* PathPreviewSystem::getDrawNode() {
    static PathDrawNode* nodeInstance = nullptr;

    if (!nodeInstance) {
        nodeInstance = PathDrawNode::create();
        nodeInstance->retain();

        cocos2d::_ccBlendFunc blendConfig;
        blendConfig.src = GL_SRC_ALPHA;
        blendConfig.dst = GL_ONE_MINUS_SRC_ALPHA;

        nodeInstance->setBlendFunc(blendConfig);
    }

    return nodeInstance;
}

class $modify(PathPreviewPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!pathSystem.getDrawNode() || pathSystem.generatingPath) return;

        if (ReplayEngine::get()->pathPreview) {
            PathPreviewSystem::refreshPreview(this);
        }
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        pathSystem.simulatedPlayer1 = nullptr;
        pathSystem.simulatedPlayer2 = nullptr;
        pathSystem.pathCancelled = false;
        pathSystem.generatingPath = false;

        pathSystem.simulatedPlayer1 = PlayerObject::create(1, 1, this, this, true);
        pathSystem.simulatedPlayer1->retain();
        pathSystem.simulatedPlayer1->setPosition({ 0, 105 });
        pathSystem.simulatedPlayer1->setVisible(false);
        m_objectLayer->addChild(pathSystem.simulatedPlayer1);

        pathSystem.simulatedPlayer2 = PlayerObject::create(1, 1, this, this, true);
        pathSystem.simulatedPlayer2->retain();
        pathSystem.simulatedPlayer2->setPosition({ 0, 105 });
        pathSystem.simulatedPlayer2->setVisible(false);
        m_objectLayer->addChild(pathSystem.simulatedPlayer2);

        m_objectLayer->addChild(pathSystem.getDrawNode(), 500);
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        if (pathSystem.generatingPath || (player == pathSystem.simulatedPlayer1 || player == pathSystem.simulatedPlayer2)) {
            pathSystem.collisionRotation = player->getRotation();
            pathSystem.pathCancelled = true;
            return;
        }

        PlayLayer::destroyPlayer(player, gameObject);
    }

    void onQuit() {
        if (pathSystem.getDrawNode())
            pathSystem.getDrawNode()->clear();

        pathSystem.simulatedPlayer1 = nullptr;
        pathSystem.simulatedPlayer2 = nullptr;
        pathSystem.pathCancelled = false;
        pathSystem.generatingPath = false;

        PlayLayer::onQuit();
    }

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (!pathSystem.generatingPath)
            PlayLayer::playEndAnimationToPos(p0);
    }
};

class $modify(PathPreviewPauseLayer, PauseLayer) {
    void goEdit() {
        if (pathSystem.getDrawNode())
            pathSystem.getDrawNode()->clear();

        pathSystem.simulatedPlayer1 = nullptr;
        pathSystem.simulatedPlayer2 = nullptr;
        pathSystem.pathCancelled = false;
        pathSystem.generatingPath = false;

        PauseLayer::goEdit();
    }
};

class $modify(PathPreviewBaseLayer, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* p0, gd::vector<GameObject*>* objects, int p2, float p3) {
        if (pathSystem.generatingPath) {
            std::vector<GameObject*> excludedObjects;

            for (const auto& obj : *objects) {
                if (!obj) continue;

                if ((!collisionObjectTypes.contains(static_cast<int>(obj->m_objectType)) && !interactivePortalIds.contains(obj->m_objectID) && !trajectoryInteractiveIds.contains(obj->m_objectID)) || pickupItemIds.contains(obj->m_objectID)) {
                    if (obj->m_isDisabled || obj->m_isDisabled2) continue;

                    excludedObjects.push_back(obj);
                    obj->m_isDisabled = true;
                    obj->m_isDisabled2 = true;
                }
            }

            GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);

            for (const auto& obj : excludedObjects) {
                if (!obj) continue;

                obj->m_isDisabled = false;
                obj->m_isDisabled2 = false;
            }

            excludedObjects.clear();

            return;
        }

        GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);
    }

    bool canBeActivatedByPlayer(PlayerObject* p0, EffectGameObject* p1) {
        if (pathSystem.generatingPath) {
            PathPreviewSystem::processPortalInteraction(p0, p1->m_objectID);
            if (trajectoryInteractiveIds.contains(p1->m_objectID))
                return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
            return false;
        }

        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void playerTouchedRing(PlayerObject* p0, RingObject* p1) {
        if (!pathSystem.generatingPath) {
            GJBaseGameLayer::playerTouchedRing(p0, p1);
        } else if (pathSystem.simulatingHold) {
            pathSystem.touchedRings.push_back({ p1, p1->m_activated });
            GJBaseGameLayer::playerTouchedRing(p0, p1);
        }
    }

    void playerTouchedTrigger(PlayerObject* p0, EffectGameObject* p1) {
        if (!pathSystem.generatingPath)
            GJBaseGameLayer::playerTouchedTrigger(p0, p1);
        else
            PathPreviewSystem::processPortalInteraction(p0, p1->m_objectID);
    }

    void activateSFXTrigger(SFXTriggerGameObject* p0) {
        if (!pathSystem.generatingPath)
            GJBaseGameLayer::activateSFXTrigger(p0);
    }

    void activateSongEditTrigger(SongTriggerGameObject* p0) {
        if (!pathSystem.generatingPath)
            GJBaseGameLayer::activateSongEditTrigger(p0);
    }

    void gameEventTriggered(GJGameEvent p0, int p1, int p2) {
        if (!pathSystem.generatingPath)
            GJBaseGameLayer::gameEventTriggered(p0, p1, p2);
    }
};

class $modify(PathPreviewPlayerObject, PlayerObject) {

    void update(float dt) {
        PlayerObject::update(dt);
        pathSystem.tickDelta = dt;
    }

    void playSpiderDashEffect(cocos2d::CCPoint p0, cocos2d::CCPoint p1) {
        if (!pathSystem.generatingPath)
            PlayerObject::playSpiderDashEffect(p0, p1);
    }

    void incrementJumps() {
        if (!pathSystem.generatingPath)
            PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* p0, bool p1) {
        if (!pathSystem.generatingPath || pathSystem.simulatingHold)
            PlayerObject::ringJump(p0, p1);
    }
};

class $modify(PathPreviewHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint p0) {
        if (!pathSystem.generatingPath)
            HardStreak::addPoint(p0);
    }
};

class $modify(PathPreviewGameObject, GameObject) {
    void playShineEffect() {
        if (!pathSystem.generatingPath)
            GameObject::playShineEffect();
    }
};

class $modify(PathPreviewEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* p0, int p1, const gd::vector<int>* p2) {
        if (!pathSystem.generatingPath)
            EffectGameObject::triggerObject(p0, p1, p2);
    }
};
