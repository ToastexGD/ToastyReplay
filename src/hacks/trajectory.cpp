#include "trajectory.hpp"
#include "ToastyReplay.hpp"

#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>

namespace {
    constexpr int kMaxTraceFrames = 480;

    const std::unordered_set<int> kInteractivePortalIds = {
        101, 99, 11, 10, 200, 201, 202, 203, 1334
    };

    struct ActivationSnapshot {
        bool activated;
        bool activatedByPlayer1;
        bool activatedByPlayer2;
        bool isActivated;
        bool isDisabled;
        bool isDisabled2;
    };

    class TrajectoryDrawNode final : public cocos2d::CCDrawNode {
    public:
        static TrajectoryDrawNode* create();
    };

    ActivationSnapshot captureActivation(EffectGameObject* object);
    void restoreActivation(EffectGameObject* object, ActivationSnapshot const& snapshot);
}

TrajectoryPredictionService& TrajectoryPredictionService::get() {
    static TrajectoryPredictionService service;
    return service;
}

bool TrajectoryPredictionService::isSimulatedPad(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::SpiderPad:
            return true;
        default:
            return false;
    }
}

bool TrajectoryPredictionService::isSimulatedOrb(GameObjectType type) {
    switch (type) {
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::DropRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::CustomRing:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

namespace {
    TrajectoryDrawNode* TrajectoryDrawNode::create() {
        auto* node = new TrajectoryDrawNode();
        if (!node->init()) {
            delete node;
            return nullptr;
        }

        node->autorelease();
        node->m_bUseArea = false;
        return node;
    }

    ActivationSnapshot captureActivation(EffectGameObject* object) {
        return {
            object->m_activated,
            object->m_activatedByPlayer1,
            object->m_activatedByPlayer2,
            object->m_isActivated,
            object->m_isDisabled,
            object->m_isDisabled2
        };
    }

    void restoreActivation(EffectGameObject* object, ActivationSnapshot const& snapshot) {
        object->m_activated = snapshot.activated;
        object->m_activatedByPlayer1 = snapshot.activatedByPlayer1;
        object->m_activatedByPlayer2 = snapshot.activatedByPlayer2;
        object->m_isActivated = snapshot.isActivated;
        object->m_isDisabled = snapshot.isDisabled;
        object->m_isDisabled2 = snapshot.isDisabled2;
    }
}

bool TrajectoryPredictionService::watchChanged(PredictionWatchKey const& lhs, PredictionWatchKey const& rhs) {
    return lhs.position.x != rhs.position.x
        || lhs.position.y != rhs.position.y
        || lhs.verticalVelocity != rhs.verticalVelocity
        || lhs.rotation != rhs.rotation
        || lhs.gravityInverted != rhs.gravityInverted
        || lhs.movementSpeed != rhs.movementSpeed
        || lhs.grounded != rhs.grounded
        || lhs.scale != rhs.scale
        || lhs.dashing != rhs.dashing
        || lhs.inShipMode != rhs.inShipMode
        || lhs.inUfoMode != rhs.inUfoMode
        || lhs.inBallMode != rhs.inBallMode
        || lhs.inWaveMode != rhs.inWaveMode
        || lhs.inRobotMode != rhs.inRobotMode
        || lhs.inSpiderMode != rhs.inSpiderMode
        || lhs.inSwingMode != rhs.inSwingMode
        || lhs.isGoingLeft != rhs.isGoingLeft
        || lhs.isSideways != rhs.isSideways
        || lhs.reverseRelated != rhs.reverseRelated;
}

PredictionWatchKey TrajectoryPredictionService::buildWatchKey(PlayerObject* player) {
    return {
        player->getPosition(),
        player->m_yVelocity,
        player->getRotation(),
        player->m_isUpsideDown,
        player->m_playerSpeed,
        player->m_isOnGround,
        player->m_vehicleSize,
        player->m_isDashing,
        player->m_isShip,
        player->m_isBird,
        player->m_isBall,
        player->m_isDart,
        player->m_isRobot,
        player->m_isSpider,
        player->m_isSwing,
        player->m_isGoingLeft,
        player->m_isSideways,
        player->m_reverseRelated
    };
}

PlayerStateCapsule TrajectoryPredictionService::capturePlayerState(PlayerObject* player) {
    PlayerStateCapsule state;

    state.motion = {
        player->getPosition(),
        player->m_lastPosition,
        player->m_yVelocity,
        player->m_yVelocityBeforeSlope,
        player->getRotation(),
        player->m_vehicleSize,
        player->m_playerSpeed,
        player->m_gravityMod,
        player->m_totalTime,
        player->m_objectType
    };

    state.form = {
        player->m_isUpsideDown,
        player->m_isOnSlope,
        player->m_wasOnSlope,
        player->m_isShip,
        player->m_isBird,
        player->m_isBall,
        player->m_isDart,
        player->m_isRobot,
        player->m_isSpider,
        player->m_isSwing,
        player->m_isOnGround,
        player->m_isDashing,
        player->m_isGoingLeft,
        player->m_isSideways,
        player->m_reverseRelated,
        player->m_maybeReverseSpeed,
        player->m_maybeReverseAcceleration
    };

    state.interaction = {
        player->m_padRingRelated,
        player->m_ringJumpRelated,
        player->m_ringRelatedSet,
        player->m_touchedRing,
        player->m_touchedCustomRing,
        player->m_touchedPad,
        player->m_lastActivatedPortal,
        player->m_lastPortalPos,
        player->m_playEffects
    };

    state.slope = {
        player->m_currentSlope,
        player->m_currentSlope2,
        player->m_currentPotentialSlope,
        player->m_slopeAngle,
        player->m_slopeAngleRadians,
        player->m_isCollidingWithSlope,
        player->m_collidingWithSlopeId,
        player->m_slopeFlipGravityRelated,
        player->m_slopeVelocity,
        player->m_currentSlopeYVelocity,
        player->m_isCurrentSlopeTop,
        player->m_slopeSlidingMaybeRotated,
        player->m_slopeRotation,
        player->m_maybeSlopeForce,
        player->m_maybeUpsideDownSlope,
        player->m_maybeGoingCorrectSlopeDirection,
        player->m_isSliding,
        player->m_isSlidingRight,
        player->m_slopeStartTime,
        player->m_slopeEndTime
    };

    state.collision = {
        player->m_lastGroundObject,
        player->m_preLastGroundObject,
        player->m_collidedObject,
        player->m_collidingWithLeft,
        player->m_collidingWithRight,
        player->m_groundYVelocity,
        player->m_lastCollisionBottom,
        player->m_lastCollisionTop,
        player->m_lastCollisionLeft,
        player->m_lastCollisionRight,
        player->m_isOnGround2,
        player->m_isOnGround3,
        player->m_isOnGround4,
        player->m_fallSpeed,
        player->m_maybeIsColliding
    };

    return state;
}

void TrajectoryPredictionService::applyPlayerState(PlayerObject* player, PlayerStateCapsule const& state) {
    player->setPosition(state.motion.position);
    player->m_lastPosition = state.motion.previousPosition;
    player->m_yVelocity = state.motion.verticalVelocity;
    player->m_yVelocityBeforeSlope = state.motion.preSlopeVelocity;
    player->setRotation(state.motion.rotation);
    player->m_vehicleSize = state.motion.scale;
    player->m_playerSpeed = state.motion.movementSpeed;
    player->m_gravityMod = state.motion.gravityFactor;
    player->m_totalTime = state.motion.totalTime;
    player->m_objectType = state.motion.objectType;

    player->m_isUpsideDown = state.form.gravityInverted;
    player->m_isOnSlope = state.form.onSlope;
    player->m_wasOnSlope = state.form.wasOnSlope;
    player->m_isShip = state.form.inShipMode;
    player->m_isBird = state.form.inUfoMode;
    player->m_isBall = state.form.inBallMode;
    player->m_isDart = state.form.inWaveMode;
    player->m_isRobot = state.form.inRobotMode;
    player->m_isSpider = state.form.inSpiderMode;
    player->m_isSwing = state.form.inSwingMode;
    player->m_isOnGround = state.form.grounded;
    player->m_isDashing = state.form.dashing;
    player->m_isGoingLeft = state.form.isGoingLeft;
    player->m_isSideways = state.form.isSideways;
    player->m_reverseRelated = state.form.reverseRelated;
    player->m_maybeReverseSpeed = state.form.reverseSpeed;
    player->m_maybeReverseAcceleration = state.form.reverseAcceleration;

    player->m_padRingRelated = state.interaction.padRingRelated;
    player->m_ringJumpRelated = state.interaction.ringJumpRelated;
    player->m_ringRelatedSet = state.interaction.ringRelatedSet;
    player->m_touchedRing = state.interaction.touchedRing;
    player->m_touchedCustomRing = state.interaction.touchedCustomRing;
    player->m_touchedPad = state.interaction.touchedPad;
    player->m_lastActivatedPortal = state.interaction.lastActivatedPortal;
    player->m_lastPortalPos = state.interaction.lastPortalPos;
    player->m_playEffects = state.interaction.playEffects;

    player->m_currentSlope = state.slope.currentSlope;
    player->m_currentSlope2 = state.slope.currentSlopeSecondary;
    player->m_currentPotentialSlope = state.slope.currentPotentialSlope;
    player->m_slopeAngle = state.slope.slopeAngle;
    player->m_slopeAngleRadians = state.slope.slopeAngleRadians;
    player->m_isCollidingWithSlope = state.slope.collidingWithSlope;
    player->m_collidingWithSlopeId = state.slope.collidingWithSlopeId;
    player->m_slopeFlipGravityRelated = state.slope.slopeFlipGravityRelated;
    player->m_slopeVelocity = state.slope.slopeVelocity;
    player->m_currentSlopeYVelocity = state.slope.currentSlopeVelocity;
    player->m_isCurrentSlopeTop = state.slope.currentSlopeTop;
    player->m_slopeSlidingMaybeRotated = state.slope.slopeSlideRotated;
    player->m_slopeRotation = state.slope.slopeRotation;
    player->m_maybeSlopeForce = state.slope.slopeForce;
    player->m_maybeUpsideDownSlope = state.slope.upsideDownSlope;
    player->m_maybeGoingCorrectSlopeDirection = state.slope.movingWithSlopeDirection;
    player->m_isSliding = state.slope.sliding;
    player->m_isSlidingRight = state.slope.slidingRight;
    player->m_slopeStartTime = state.slope.slopeStartTime;
    player->m_slopeEndTime = state.slope.slopeEndTime;

    player->m_lastGroundObject = state.collision.lastGroundObject;
    player->m_preLastGroundObject = state.collision.preLastGroundObject;
    player->m_collidedObject = state.collision.collidedObject;
    player->m_collidingWithLeft = state.collision.collidingWithLeft;
    player->m_collidingWithRight = state.collision.collidingWithRight;
    player->m_groundYVelocity = state.collision.groundYVelocity;
    player->m_lastCollisionBottom = state.collision.lastCollisionBottom;
    player->m_lastCollisionTop = state.collision.lastCollisionTop;
    player->m_lastCollisionLeft = state.collision.lastCollisionLeft;
    player->m_lastCollisionRight = state.collision.lastCollisionRight;
    player->m_isOnGround2 = state.collision.isOnGround2;
    player->m_isOnGround3 = state.collision.isOnGround3;
    player->m_isOnGround4 = state.collision.isOnGround4;
    player->m_fallSpeed = state.collision.fallSpeed;
    player->m_maybeIsColliding = state.collision.maybeColliding;
}
bool TrajectoryPredictionService::isActiveSimulation() const {
    return m_context.activeSimulation;
}

bool TrajectoryPredictionService::isProcessingOrbTouch() const {
    return m_context.processingOrbTouch;
}

void TrajectoryPredictionService::markDirty() {
    m_context.dirty = true;
}

void TrajectoryPredictionService::clearOverlay() {
    auto* drawNode = ensureDrawNode();
    if (!drawNode) {
        return;
    }

    drawNode->clear();
    drawNode->setVisible(false);
}

cocos2d::CCDrawNode* TrajectoryPredictionService::ensureDrawNode() {
    if (!m_drawNode) {
        auto* drawNode = TrajectoryDrawNode::create();
        if (!drawNode) {
            return nullptr;
        }

        drawNode->retain();
        drawNode->setBlendFunc({ GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA });
        m_drawNode = drawNode;
    }

    return m_drawNode;
}

void TrajectoryPredictionService::attach(PlayLayer* playLayer) {
    detach();
    if (!playLayer || !playLayer->m_objectLayer) {
        return;
    }

    for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
        auto* previewPlayer = PlayerObject::create(1, 1, playLayer, playLayer, true);
        if (!previewPlayer) {
            continue;
        }

        previewPlayer->setVisible(false);
        previewPlayer->setPosition({ 0.0f, 105.0f });
        playLayer->m_objectLayer->addChild(previewPlayer);
        m_context.previewPlayers[playerIndex] = previewPlayer;
    }

    if (auto* drawNode = ensureDrawNode()) {
        if (drawNode->getParent() != playLayer->m_objectLayer) {
            if (drawNode->getParent()) {
                drawNode->removeFromParent();
            }
            playLayer->m_objectLayer->addChild(drawNode, 9999);
        }
        drawNode->clear();
        drawNode->setVisible(false);
    }

    m_context.dirty = true;
    recalculateOverlapColors();
}

void TrajectoryPredictionService::detach() {
    m_context.previewPlayers[0] = nullptr;
    m_context.previewPlayers[1] = nullptr;
    m_context.activeSimulation = false;
    m_context.traceCancelled = false;
    m_context.holdingTrace = false;
    m_context.processedOrbs.clear();
    m_context.touchingPads.clear();
    m_context.frameTouchingPads.clear();
    m_context.dirty = true;

    if (m_drawNode) {
        m_drawNode->clear();
        m_drawNode->setVisible(false);
    }
}

void TrajectoryPredictionService::captureFrameDelta(float dt) {
    if (!m_context.activeSimulation) {
        m_context.stepDelta = dt;
    }
}

bool TrajectoryPredictionService::ownsPreviewPlayer(PlayerObject* player) const {
    return player && (player == m_context.previewPlayers[0] || player == m_context.previewPlayers[1]);
}

void TrajectoryPredictionService::noteSimulatedDeath(PlayerObject* player) {
    if (!player) {
        return;
    }

    m_context.collisionRotation = player->getRotation();
    m_context.traceCancelled = true;
}

void TrajectoryPredictionService::recalculateOverlapColors() {
    m_overlapColor = {
        std::min(1.0f, (m_holdColor.r + m_releaseColor.r) * 0.5f + 0.45f),
        std::min(1.0f, (m_holdColor.g + m_releaseColor.g) * 0.5f + 0.45f),
        std::min(1.0f, (m_holdColor.b + m_releaseColor.b) * 0.5f + 0.45f),
        1.0f
    };

    m_overlapColorP2 = {
        std::min(1.0f, (m_holdColorP2.r + m_releaseColor.r) * 0.5f + 0.45f),
        std::min(1.0f, (m_holdColorP2.g + m_releaseColor.g) * 0.5f + 0.45f),
        std::min(1.0f, (m_holdColorP2.b + m_releaseColor.b) * 0.5f + 0.45f),
        1.0f
    };
}

std::vector<CCPoint> TrajectoryPredictionService::buildPlayerBounds(PlayerObject* player, CCRect bounds, float angle) {
    std::vector<CCPoint> vertices = {
        ccp(bounds.getMinX(), bounds.getMaxY()),
        ccp(bounds.getMaxX(), bounds.getMaxY()),
        ccp(bounds.getMaxX(), bounds.getMinY()),
        ccp(bounds.getMinX(), bounds.getMinY())
    };

    CCPoint center = ccp(
        (bounds.getMinX() + bounds.getMaxX()) * 0.5f,
        (bounds.getMinY() + bounds.getMaxY()) * 0.5f
    );

    float dimension = static_cast<float>(static_cast<int>(bounds.getMaxX() - bounds.getMinX()));
    if ((dimension == 18.0f || dimension == 5.0f) && player->getScale() == 1.0f) {
        for (auto& vertex : vertices) {
            vertex.x = center.x + (vertex.x - center.x) / 0.6f;
            vertex.y = center.y + (vertex.y - center.y) / 0.6f;
        }
    }

    if ((dimension == 7.0f || dimension == 30.0f || dimension == 29.0f || dimension == 9.0f)
        && player->getScale() != 1.0f) {
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

    float radians = CC_DEGREES_TO_RADIANS(angle * -1.0f);
    for (auto& vertex : vertices) {
        float dx = vertex.x - center.x;
        float dy = vertex.y - center.y;
        vertex.x = center.x + (dx * cos(radians)) - (dy * sin(radians));
        vertex.y = center.y + (dx * sin(radians)) + (dy * cos(radians));
    }

    return vertices;
}

void TrajectoryPredictionService::drawPredictionBounds(PlayerObject* player) {
    auto* drawNode = ensureDrawNode();
    if (!drawNode || !player) {
        return;
    }

    CCRect outerBounds = player->GameObject::getObjectRect();
    CCRect innerBounds = player->GameObject::getObjectRect(0.3f, 0.3f);

    auto outerVertices = buildPlayerBounds(player, outerBounds, m_context.collisionRotation);
    drawNode->drawPolygon(
        outerVertices.data(),
        outerVertices.size(),
        ccc4f(m_releaseColor.r, m_releaseColor.g, m_releaseColor.b, 0.2f),
        0.5f,
        m_releaseColor
    );

    auto innerVertices = buildPlayerBounds(player, innerBounds, m_context.collisionRotation);
    drawNode->drawPolygon(
        innerVertices.data(),
        innerVertices.size(),
        ccc4f(m_overlapColor.r, m_overlapColor.g, m_overlapColor.b, 0.2f),
        0.35f,
        ccc4f(m_overlapColor.r, m_overlapColor.g, m_overlapColor.b, 0.55f)
    );
}

void TrajectoryPredictionService::applyPortalHint(PlayerObject* player, int portalId) {
    if (!player || !kInteractivePortalIds.contains(portalId)) {
        return;
    }

    switch (portalId) {
        case 101:
            player->togglePlayerScale(true, true);
            player->updatePlayerScale();
            break;
        case 99:
            player->togglePlayerScale(false, true);
            player->updatePlayerScale();
            break;
        case 200:
            player->m_playerSpeed = 0.7f;
            break;
        case 201:
            player->m_playerSpeed = 0.9f;
            break;
        case 202:
            player->m_playerSpeed = 1.1f;
            break;
        case 203:
            player->m_playerSpeed = 1.3f;
            break;
        case 1334:
            player->m_playerSpeed = 1.6f;
            break;
        case 10:
            player->m_isUpsideDown = false;
            break;
        case 11:
            player->m_isUpsideDown = true;
            break;
        default:
            break;
    }
}
void TrajectoryPredictionService::traceInputPath(
    PlayLayer* playLayer,
    PlayerObject* previewPlayer,
    PlayerObject* sourcePlayer,
    bool holdingInput
) {
    if (!playLayer || !previewPlayer || !sourcePlayer) {
        return;
    }

    auto state = capturePlayerState(sourcePlayer);
    applyPlayerState(previewPlayer, state);

    bool isSecondPlayer = playLayer->m_player2 == sourcePlayer;
    previewPlayer->m_isSecondPlayer = isSecondPlayer;
    previewPlayer->m_isPlatformer = sourcePlayer->m_isPlatformer;
    previewPlayer->m_playEffects = false;

    previewPlayer->m_touchedRings.clear();
    for (auto const& ringId : sourcePlayer->m_touchedRings) {
        previewPlayer->m_touchedRings.insert(ringId);
    }
    if (previewPlayer->m_touchingRings) {
        previewPlayer->m_touchingRings->removeAllObjects();
    }

    previewPlayer->m_potentialSlopeMap.clear();
    for (auto const& [key, value] : sourcePlayer->m_potentialSlopeMap) {
        previewPlayer->m_potentialSlopeMap.insert({ key, value });
    }

    int frameCount = std::clamp(ReplayEngine::get()->pathLength, 0, kMaxTraceFrames);
    m_context.traceCancelled = false;
    m_context.holdingTrace = holdingInput;
    m_context.touchingPads.clear();
    m_context.frameTouchingPads.clear();

    if (holdingInput) {
        previewPlayer->pushButton(static_cast<PlayerButton>(1));
    } else {
        previewPlayer->releaseButton(static_cast<PlayerButton>(1));
    }

    if (playLayer->m_levelSettings->m_platformerMode) {
        if (sourcePlayer->m_isGoingLeft) {
            previewPlayer->pushButton(static_cast<PlayerButton>(2));
        } else {
            previewPlayer->pushButton(static_cast<PlayerButton>(3));
        }
    }

    auto* drawNode = ensureDrawNode();
    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        CCPoint previousPosition = previewPlayer->getPosition();

        if (holdingInput) {
            if (isSecondPlayer) {
                m_context.holdPathP2[frameIndex] = previousPosition;
            } else {
                m_context.holdPathP1[frameIndex] = previousPosition;
            }
        }

        previewPlayer->m_collisionLogTop->removeAllObjects();
        previewPlayer->m_collisionLogBottom->removeAllObjects();
        previewPlayer->m_collisionLogLeft->removeAllObjects();
        previewPlayer->m_collisionLogRight->removeAllObjects();

        previewPlayer->update(m_context.stepDelta);
        previewPlayer->updateRotation(m_context.stepDelta);
        previewPlayer->updatePlayerScale();

        m_context.frameTouchingPads.clear();
        playLayer->checkCollisions(previewPlayer, m_context.stepDelta, false);
        m_context.touchingPads = m_context.frameTouchingPads;

        if (m_context.traceCancelled) {
            drawPredictionBounds(previewPlayer);
            break;
        }

        cocos2d::ccColor4F lineColor = holdingInput
            ? (isSecondPlayer ? m_holdColorP2 : m_holdColor)
            : m_releaseColor;

        if (!holdingInput) {
            bool overlapsHoldPath = isSecondPlayer
                ? (m_context.holdPathP2[frameIndex] == previousPosition)
                : (m_context.holdPathP1[frameIndex] == previousPosition);
            if (overlapsHoldPath) {
                lineColor = isSecondPlayer ? m_overlapColorP2 : m_overlapColor;
            }
        }

        if (frameIndex >= frameCount - 40 && frameCount > 0) {
            lineColor.a = static_cast<float>(frameCount - frameIndex) / 40.0f;
        }

        if (drawNode) {
            drawNode->drawSegment(previousPosition, previewPlayer->getPosition(), 0.6f, lineColor);
        }
    }

    m_context.touchingPads.clear();
    m_context.frameTouchingPads.clear();
    m_context.holdingTrace = false;
}

void TrajectoryPredictionService::rebuildPreview(PlayLayer* playLayer) {
    if (!playLayer || !m_context.previewPlayers[0]) {
        return;
    }

    auto* drawNode = ensureDrawNode();
    if (!drawNode) {
        return;
    }

    unsigned int savedProgress = playLayer->m_gameState.m_currentProgress;
    double savedLevelTime = playLayer->m_gameState.m_levelTime;
    double savedTotalTime = playLayer->m_gameState.m_totalTime;
    unsigned int savedCommandIndex = playLayer->m_gameState.m_commandIndex;

    m_context.activeSimulation = true;
    m_context.processedOrbs.clear();
    drawNode->clear();
    drawNode->setVisible(true);

    traceInputPath(playLayer, m_context.previewPlayers[0], playLayer->m_player1, true);
    traceInputPath(playLayer, m_context.previewPlayers[0], playLayer->m_player1, false);

    m_context.processedOrbs.clear();
    if (playLayer->m_gameState.m_isDualMode && playLayer->m_player2 && m_context.previewPlayers[1]) {
        traceInputPath(playLayer, m_context.previewPlayers[1], playLayer->m_player2, true);
        traceInputPath(playLayer, m_context.previewPlayers[1], playLayer->m_player2, false);
    }

    playLayer->m_gameState.m_currentProgress = savedProgress;
    playLayer->m_gameState.m_levelTime = savedLevelTime;
    playLayer->m_gameState.m_totalTime = savedTotalTime;
    playLayer->m_gameState.m_commandIndex = savedCommandIndex;

    m_context.activeSimulation = false;
    m_context.dirty = false;

    if (playLayer->m_player1) {
        m_context.watchKeys[0] = buildWatchKey(playLayer->m_player1);
    }
    if (playLayer->m_player2) {
        m_context.watchKeys[1] = buildWatchKey(playLayer->m_player2);
    }
}

void TrajectoryPredictionService::updatePreview(PlayLayer* playLayer) {
    if (!playLayer) {
        return;
    }

    if (!ReplayEngine::get()->pathPreview) {
        m_context.dirty = true;
        clearOverlay();
        return;
    }

    if (!m_context.previewPlayers[0]) {
        attach(playLayer);
    }

    if (m_context.activeSimulation) {
        return;
    }

    bool needsRebuild = m_context.dirty;
    if (!needsRebuild && playLayer->m_player1) {
        needsRebuild = watchChanged(buildWatchKey(playLayer->m_player1), m_context.watchKeys[0]);
    }
    if (!needsRebuild && playLayer->m_gameState.m_isDualMode && playLayer->m_player2) {
        needsRebuild = watchChanged(buildWatchKey(playLayer->m_player2), m_context.watchKeys[1]);
    }

    if (needsRebuild) {
        rebuildPreview(playLayer);
    }
}
void TrajectoryPredictionService::simulateCollisionBatch(
    GJBaseGameLayer* layer,
    PlayerObject* player,
    gd::vector<GameObject*>* objects,
    int objectCount,
    float dt
) {
    if (!layer || !player || !objects) {
        return;
    }

    std::vector<EffectGameObject*> padObjects;
    std::vector<RingObject*> orbObjects;
    gd::vector<GameObject*> filteredObjects;
    filteredObjects.reserve(objectCount);

    for (int index = 0; index < objectCount; ++index) {
        GameObject* object = (*objects)[index];
        if (!object) {
            continue;
        }

        auto type = object->m_objectType;
        if (type == GameObjectType::Solid
            || type == GameObjectType::Hazard
            || type == GameObjectType::AnimatedHazard
            || type == GameObjectType::Slope) {
            filteredObjects.push_back(object);
            continue;
        }

        if (kInteractivePortalIds.contains(object->m_objectID)) {
            filteredObjects.push_back(object);
        } else if (isSimulatedPad(type)) {
            padObjects.push_back(static_cast<EffectGameObject*>(object));
        } else if (isSimulatedOrb(type)) {
            orbObjects.push_back(static_cast<RingObject*>(object));
        }
    }

    layer->GJBaseGameLayer::collisionCheckObjects(player, &filteredObjects, static_cast<int>(filteredObjects.size()), dt);

    CCRect playerRect = player->getObjectRect();
    for (auto* object : filteredObjects) {
        if (!object || !kInteractivePortalIds.contains(object->m_objectID)) {
            continue;
        }

        if (!playerRect.intersectsRect(object->getObjectRect())) {
            continue;
        }

        applyPortalHint(player, object->m_objectID);
    }

    playerRect = player->getObjectRect();
    for (auto* pad : padObjects) {
        if (!pad || !playerRect.intersectsRect(pad->getObjectRect())) {
            continue;
        }

        m_context.frameTouchingPads.insert(pad);
        if (m_context.touchingPads.contains(pad)) {
            continue;
        }

        auto snapshot = captureActivation(pad);
        bool savedNoEffects = pad->m_hasNoEffects;
        pad->m_hasNoEffects = true;
        if (pad->m_objectType == GameObjectType::GravityPad) {
            layer->GJBaseGameLayer::gravBumpPlayer(player, pad);
        } else {
            layer->GJBaseGameLayer::bumpPlayer(player, pad);
        }
        pad->m_hasNoEffects = savedNoEffects;
        restoreActivation(pad, snapshot);
    }

    if (!m_context.holdingTrace) {
        return;
    }

    playerRect = player->getObjectRect();
    for (auto* orb : orbObjects) {
        if (!orb || m_context.processedOrbs.contains(orb)) {
            continue;
        }

        if (!playerRect.intersectsRect(orb->getObjectRect())) {
            continue;
        }

        m_context.processedOrbs.insert(orb);
        auto snapshot = captureActivation(orb);
        bool savedNoEffects = orb->m_hasNoEffects;
        orb->m_hasNoEffects = true;
        m_context.processingOrbTouch = true;
        layer->GJBaseGameLayer::playerTouchedRing(player, orb);
        m_context.processingOrbTouch = false;
        orb->m_hasNoEffects = savedNoEffects;
        restoreActivation(orb, snapshot);
    }
}

bool TrajectoryPredictionService::handleActivationCheck(PlayerObject* player, EffectGameObject* object) {
    if (!player || !object) {
        return false;
    }

    if (kInteractivePortalIds.contains(object->m_objectID)) {
        applyPortalHint(player, object->m_objectID);
        return false;
    }

    return isSimulatedPad(object->m_objectType);
}

void TrajectoryPredictionService::handleTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
    if (!player || !object) {
        return;
    }

    if (kInteractivePortalIds.contains(object->m_objectID)) {
        applyPortalHint(player, object->m_objectID);
    }
}

class $modify(TrajectoryPreviewPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        TrajectoryPredictionService::get().updatePreview(this);
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto& service = TrajectoryPredictionService::get();
        service.attach(this);
        service.markDirty();
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() || service.ownsPreviewPlayer(player)) {
            service.noteSimulatedDeath(player);
            return;
        }

        PlayLayer::destroyPlayer(player, gameObject);
    }

    void onQuit() {
        auto& service = TrajectoryPredictionService::get();
        service.clearOverlay();
        service.detach();
        PlayLayer::onQuit();
    }

    void playEndAnimationToPos(cocos2d::CCPoint position) {
        if (TrajectoryPredictionService::get().isActiveSimulation()) {
            return;
        }

        PlayLayer::playEndAnimationToPos(position);
    }
};

class $modify(TrajectoryPreviewPauseLayer, PauseLayer) {
    void goEdit() {
        auto& service = TrajectoryPredictionService::get();
        service.clearOverlay();
        service.detach();
        PauseLayer::goEdit();
    }
};

class $modify(TrajectoryPreviewBaseLayer, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        auto& service = TrajectoryPredictionService::get();
        if (!service.isActiveSimulation()) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }

        service.simulateCollisionBatch(this, player, objects, objectCount, dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        auto& service = TrajectoryPredictionService::get();
        if (!service.isActiveSimulation()) {
            return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
        }

        return service.handleActivationCheck(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && !service.isProcessingOrbTouch()) {
            return;
        }

        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        auto& service = TrajectoryPredictionService::get();
        if (!service.isActiveSimulation()) {
            GJBaseGameLayer::playerTouchedTrigger(player, object);
            return;
        }

        service.handleTouchedTrigger(player, object);
    }
    void activateSFXTrigger(SFXTriggerGameObject* object) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            GJBaseGameLayer::activateSFXTrigger(object);
        }
    }

    void activateSongEditTrigger(SongTriggerGameObject* object) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            GJBaseGameLayer::activateSongEditTrigger(object);
        }
    }

    void gameEventTriggered(GJGameEvent event, int value1, int value2) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            GJBaseGameLayer::gameEventTriggered(event, value1, value2);
        }
    }
};

class $modify(TrajectoryPreviewPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        TrajectoryPredictionService::get().captureFrameDelta(dt);
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayerObject::playSpiderDashEffect(from, to);
        }
    }

    void incrementJumps() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayerObject::incrementJumps();
        }
    }

    void playBumpEffect(int objectType, GameObject* player) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayerObject::playBumpEffect(objectType, player);
        }
    }

    void spawnCircle() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayerObject::spawnCircle();
        }
    }

    void spawnDualCircle() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayerObject::spawnDualCircle();
        }
    }

    void addAllParticles() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayerObject::addAllParticles();
        }
    }
};

class $modify(TrajectoryPreviewHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint point) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            HardStreak::addPoint(point);
        }
    }
};

class $modify(TrajectoryPreviewGameObject, GameObject) {
    void playShineEffect() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            GameObject::playShineEffect();
        }
    }

    void activateObject() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            GameObject::activateObject();
        }
    }
};

class $modify(TrajectoryPreviewEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int unk, const gd::vector<int>* groups) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            EffectGameObject::triggerObject(layer, unk, groups);
        }
    }

    void triggerActivated(float value) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            EffectGameObject::triggerActivated(value);
        }
    }

    void playTriggerEffect() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            EffectGameObject::playTriggerEffect();
        }
    }
};
class $modify(TrajectoryPreviewRingObject, RingObject) {
    void spawnCircle() {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            RingObject::spawnCircle();
        }
    }

    void powerOnObject(int state) {
        if (!TrajectoryPredictionService::get().isActiveSimulation()) {
            RingObject::powerOnObject(state);
        }
    }
};
