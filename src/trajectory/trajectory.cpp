#include "ToastyReplay.hpp"
#include "trajectory.hpp"
#include "trajectory_physics.hpp"

#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>

namespace {
    struct ScopedPredictionLayerState {
        PlayLayer* layer;
        GJGameState gameState;
        PlayerObject* player1;
        PlayerObject* player2;
        GameObject* player1CollisionBlock;
        GameObject* player2CollisionBlock;

        explicit ScopedPredictionLayerState(PlayLayer* playLayer)
            : layer(playLayer),
              gameState(playLayer->m_gameState),
              player1(playLayer->m_player1),
              player2(playLayer->m_player2),
              player1CollisionBlock(playLayer->m_player1CollisionBlock),
              player2CollisionBlock(playLayer->m_player2CollisionBlock) {}

        ScopedPredictionLayerState(ScopedPredictionLayerState const&) = delete;
        ScopedPredictionLayerState& operator=(ScopedPredictionLayerState const&) = delete;

        ~ScopedPredictionLayerState() {
            restore();
        }

        void restore() {
            if (!layer) {
                return;
            }

            layer->m_gameState = gameState;
            layer->m_player1 = player1;
            layer->m_player2 = player2;
            layer->m_player1CollisionBlock = player1CollisionBlock;
            layer->m_player2CollisionBlock = player2CollisionBlock;
            layer = nullptr;
        }
    };

    struct ScopedPredictionCameraState {
        PlayLayer* layer;
        float cameraZoom;
        float targetCameraZoom;
        cocos2d::CCPoint cameraOffset;
        float cameraAngle;
        float targetCameraAngle;
        cocos2d::CCPoint cameraPosition;
        cocos2d::CCPoint cameraPosition2;
        cocos2d::CCPoint cameraStepDiff;
        bool cameraShakeEnabled;
        float cameraShakeFactor;
        float cameraFlip;
        float cameraWidthOffset;
        float cameraHeightOffset;
        float cameraUnzoomedHeightOffset;
        float targetCameraHeightOffset;
        bool calculateTargetHeightOffset;
        float cameraWidth;
        float cameraHeight;
        float cameraUnzoomedX;
        float halfCameraWidth;

        explicit ScopedPredictionCameraState(PlayLayer* playLayer)
            : layer(playLayer),
              cameraZoom(playLayer ? playLayer->m_gameState.m_cameraZoom : 0.0f),
              targetCameraZoom(playLayer ? playLayer->m_gameState.m_targetCameraZoom : 0.0f),
              cameraOffset(playLayer ? playLayer->m_gameState.m_cameraOffset : cocos2d::CCPoint { 0.0f, 0.0f }),
              cameraAngle(playLayer ? playLayer->m_gameState.m_cameraAngle : 0.0f),
              targetCameraAngle(playLayer ? playLayer->m_gameState.m_targetCameraAngle : 0.0f),
              cameraPosition(playLayer ? playLayer->m_gameState.m_cameraPosition : cocos2d::CCPoint { 0.0f, 0.0f }),
              cameraPosition2(playLayer ? playLayer->m_gameState.m_cameraPosition2 : cocos2d::CCPoint { 0.0f, 0.0f }),
              cameraStepDiff(playLayer ? playLayer->m_gameState.m_cameraStepDiff : cocos2d::CCPoint { 0.0f, 0.0f }),
              cameraShakeEnabled(playLayer ? playLayer->m_gameState.m_cameraShakeEnabled : false),
              cameraShakeFactor(playLayer ? playLayer->m_gameState.m_cameraShakeFactor : 0.0f),
              cameraFlip(playLayer ? playLayer->m_cameraFlip : 0.0f),
              cameraWidthOffset(playLayer ? playLayer->m_cameraWidthOffset : 0.0f),
              cameraHeightOffset(playLayer ? playLayer->m_cameraHeightOffset : 0.0f),
              cameraUnzoomedHeightOffset(playLayer ? playLayer->m_cameraUnzoomedHeightOffset : 0.0f),
              targetCameraHeightOffset(playLayer ? playLayer->m_targetCameraHeightOffset : 0.0f),
              calculateTargetHeightOffset(playLayer ? playLayer->m_calculateTargetHeightOffset : false),
              cameraWidth(playLayer ? playLayer->m_cameraWidth : 0.0f),
              cameraHeight(playLayer ? playLayer->m_cameraHeight : 0.0f),
              cameraUnzoomedX(playLayer ? playLayer->m_cameraUnzoomedX : 0.0f),
              halfCameraWidth(playLayer ? playLayer->m_halfCameraWidth : 0.0f) {}

        ScopedPredictionCameraState(ScopedPredictionCameraState const&) = delete;
        ScopedPredictionCameraState& operator=(ScopedPredictionCameraState const&) = delete;

        ~ScopedPredictionCameraState() {
            restore();
        }

        void restore() {
            if (!layer) {
                return;
            }

            layer->m_gameState.m_cameraZoom = cameraZoom;
            layer->m_gameState.m_targetCameraZoom = targetCameraZoom;
            layer->m_gameState.m_cameraOffset = cameraOffset;
            layer->m_gameState.m_cameraAngle = cameraAngle;
            layer->m_gameState.m_targetCameraAngle = targetCameraAngle;
            layer->m_gameState.m_cameraPosition = cameraPosition;
            layer->m_gameState.m_cameraPosition2 = cameraPosition2;
            layer->m_gameState.m_cameraStepDiff = cameraStepDiff;
            layer->m_gameState.m_cameraShakeEnabled = cameraShakeEnabled;
            layer->m_gameState.m_cameraShakeFactor = cameraShakeFactor;

            layer->m_cameraFlip = cameraFlip;
            layer->m_cameraWidthOffset = cameraWidthOffset;
            layer->m_cameraHeightOffset = cameraHeightOffset;
            layer->m_cameraUnzoomedHeightOffset = cameraUnzoomedHeightOffset;
            layer->m_targetCameraHeightOffset = targetCameraHeightOffset;
            layer->m_calculateTargetHeightOffset = calculateTargetHeightOffset;
            layer->m_cameraWidth = cameraWidth;
            layer->m_cameraHeight = cameraHeight;
            layer->m_cameraUnzoomedX = cameraUnzoomedX;
            layer->m_halfCameraWidth = halfCameraWidth;
            layer = nullptr;
        }
    };

    struct ScopedReplaySimulationState {
        ReplayEngine* engine;
        bool previousSimulatingPath;

        explicit ScopedReplaySimulationState(ReplayEngine* replayEngine)
            : engine(replayEngine),
              previousSimulatingPath(replayEngine ? replayEngine->simulatingPath : false) {
            if (engine) {
                engine->simulatingPath = true;
            }
        }

        ScopedReplaySimulationState(ScopedReplaySimulationState const&) = delete;
        ScopedReplaySimulationState& operator=(ScopedReplaySimulationState const&) = delete;

        ~ScopedReplaySimulationState() {
            restore();
        }

        void restore() {
            if (!engine) {
                return;
            }

            engine->simulatingPath = previousSimulatingPath;
            engine = nullptr;
        }
    };

    class TrajectoryDrawNode final : public cocos2d::CCDrawNode {
    public:
        static TrajectoryDrawNode* create();
    };

    void applyPreviewButtonState(PlayerObject* player, PlayerButton button, bool holding);
    bool hasClassicDualPreview(PlayLayer* playLayer);
    bool hasTrueTwoPlayerPreview(PlayLayer* playLayer);
    bool hasAnySecondPreview(PlayLayer* playLayer);
}

TrajectoryPredictionService& TrajectoryPredictionService::get() {
    static TrajectoryPredictionService service;
    return service;
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

    void applyPreviewButtonState(PlayerObject* player, PlayerButton button, bool holding) {
        if (!player) {
            return;
        }

        int buttonIndex = static_cast<int>(button);
        player->m_holdingButtons[buttonIndex] = holding;

        if (button == PlayerButton::Jump) {
            if (holding) {
                player->m_padRingRelated = false;
                player->m_ringRelatedSet.clear();
                player->m_jumpBuffered = true;
                player->m_stateRingJump = true;
                player->m_wasJumpBuffered = false;
                player->m_stateJumpBuffered = 1;
                player->m_stateRingJump2 = true;
                player->m_touchedRing = false;
                player->m_touchedCustomRing = false;
                player->m_touchedGravityPortal = false;
                player->m_maybeTouchedBreakableBlock = false;
            } else {
                player->m_jumpBuffered = false;
                player->m_stateRingJump = false;
                player->m_wasJumpBuffered = false;
                player->m_stateJumpBuffered = 0;
                player->m_stateRingJump2 = false;
            }
            return;
        }

        if (button == PlayerButton::Left) {
            player->m_holdingLeft = holding;
            if (holding) {
                player->m_leftPressedFirst = true;
                player->m_holdingRight = false;
                player->m_holdingButtons[static_cast<int>(PlayerButton::Right)] = false;
            } else if (player->m_holdingRight) {
                player->m_leftPressedFirst = false;
            }
            return;
        }

        if (button == PlayerButton::Right) {
            player->m_holdingRight = holding;
            if (holding) {
                player->m_leftPressedFirst = false;
                player->m_holdingLeft = false;
                player->m_holdingButtons[static_cast<int>(PlayerButton::Left)] = false;
            } else if (player->m_holdingLeft) {
                player->m_leftPressedFirst = true;
            }
        }
    }

    bool hasClassicDualPreview(PlayLayer* playLayer) {
        return playLayer
            && playLayer->m_player2
            && playLayer->m_gameState.m_isDualMode
            && (!playLayer->m_levelSettings || !playLayer->m_levelSettings->m_twoPlayerMode);
    }

    bool hasTrueTwoPlayerPreview(PlayLayer* playLayer) {
        return playLayer
            && playLayer->m_player2
            && playLayer->m_gameState.m_isDualMode
            && playLayer->m_levelSettings
            && playLayer->m_levelSettings->m_twoPlayerMode;
    }

    bool hasAnySecondPreview(PlayLayer* playLayer) {
        return hasClassicDualPreview(playLayer) || hasTrueTwoPlayerPreview(playLayer);
    }

    float sanitizedTimeWarp(PlayLayer* playLayer) {
        if (!playLayer || !std::isfinite(playLayer->m_gameState.m_timeWarp)) {
            return 1.0f;
        }

        return std::max(0.001f, playLayer->m_gameState.m_timeWarp);
    }

    int predictionFrameCount(PlayLayer* playLayer) {
        auto* engine = ReplayEngine::get();
        double predictionRate = std::max(engine->runtimeTickRate(), ReplayEngine::kBaseTickRate);
        double timeWarp = static_cast<double>(sanitizedTimeWarp(playLayer));
        double baseLength = static_cast<double>(ReplayEngine::sanitizeTrajectoryLength(engine->pathLength));
        double steps = std::ceil((baseLength * predictionRate) / (ReplayEngine::kBaseTickRate * timeWarp));
        if (!std::isfinite(steps) || steps <= 0.0) {
            return 0;
        }

        return static_cast<int>(std::clamp(steps, 0.0, static_cast<double>(ReplayEngine::kTrajectoryPredictionStepMax)));
    }

    void advancePredictionClock(PlayLayer* playLayer, PlayerObject* player) {
        if (!playLayer || !player) {
            return;
        }

        auto* engine = ReplayEngine::get();
        double physicsDelta = 1.0 / engine->runtimeTickRate();
        playLayer->m_gameState.m_totalTime += physicsDelta;
        playLayer->m_gameState.m_unkDouble3 += physicsDelta / sanitizedTimeWarp(playLayer);
        playLayer->m_gameState.m_currentProgress++;
        playLayer->m_gameState.m_unkUint5 += static_cast<int>(std::round(sanitizedTimeWarp(playLayer) * 1000.0f));
        player->m_totalTime += physicsDelta;
    }
}

bool TrajectoryPredictionService::watchChanged(PredictionWatchKey const& lhs, PredictionWatchKey const& rhs) {
    bool motionChanged = lhs.position.x != rhs.position.x
        || lhs.position.y != rhs.position.y
        || lhs.verticalVelocity != rhs.verticalVelocity
        || lhs.rotation != rhs.rotation
        || lhs.movementSpeed != rhs.movementSpeed
        || lhs.scale != rhs.scale
        || lhs.gravityInverted != rhs.gravityInverted
        || lhs.grounded != rhs.grounded
        || lhs.dashing != rhs.dashing;

    bool formChanged = lhs.inShipMode != rhs.inShipMode
        || lhs.inUfoMode != rhs.inUfoMode
        || lhs.inBallMode != rhs.inBallMode
        || lhs.inWaveMode != rhs.inWaveMode
        || lhs.inRobotMode != rhs.inRobotMode
        || lhs.inSpiderMode != rhs.inSpiderMode
        || lhs.inSwingMode != rhs.inSwingMode
        || lhs.isGoingLeft != rhs.isGoingLeft
        || lhs.isSideways != rhs.isSideways;

    bool modeChanged = lhs.jumpBuffered != rhs.jumpBuffered
        || lhs.platformer != rhs.platformer
        || lhs.dualMode != rhs.dualMode
        || lhs.twoPlayerMode != rhs.twoPlayerMode
        || lhs.reverseRelated != rhs.reverseRelated
        || lhs.stateForce != rhs.stateForce
        || lhs.stateDartSlide != rhs.stateDartSlide
        || lhs.stateFlipGravity != rhs.stateFlipGravity
        || lhs.gravityMod != rhs.gravityMod
        || lhs.timeWarp != rhs.timeWarp
        || lhs.pathLength != rhs.pathLength
        || lhs.tickRate != rhs.tickRate
        || lhs.lastPortalPos.x != rhs.lastPortalPos.x
        || lhs.lastPortalPos.y != rhs.lastPortalPos.y
        || lhs.lastActivatedPortal != rhs.lastActivatedPortal;

    return motionChanged || formChanged || modeChanged;
}

PredictionWatchKey TrajectoryPredictionService::buildWatchKey(PlayerObject* player) {
    auto* playLayer = PlayLayer::get();
    auto* engine = ReplayEngine::get();

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
        player->m_jumpBuffered,
        player->m_isPlatformer,
        playLayer && playLayer->m_gameState.m_isDualMode,
        playLayer && playLayer->m_levelSettings && playLayer->m_levelSettings->m_twoPlayerMode,
        player->m_reverseRelated,
        player->m_stateForce,
        player->m_stateDartSlide,
        player->m_stateFlipGravity,
        player->m_gravityMod,
        playLayer ? sanitizedTimeWarp(playLayer) : 1.0f,
        engine ? engine->pathLength : ReplayEngine::kTrajectoryLengthDefault,
        engine ? engine->runtimeTickRate() : ReplayEngine::kBaseTickRate,
        player->m_lastPortalPos,
        player->m_lastActivatedPortal
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
        player->m_maybeReverseAcceleration,
        player->m_isDead,
        player->m_isSecondPlayer,
        player->m_isBeingSpawnedByDualPortal,
        player->m_isPlatformer,
        player->m_isLocked,
        player->m_controlsDisabled,
        player->m_inputsLocked,
        player->m_hasEverJumped,
        player->m_hasEverHitRing,
        player->m_isOutOfBounds
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
        player->m_maybeLastGroundObject,
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
        player->m_maybeIsColliding,
        player->m_collidedTopMinY,
        player->m_collidedBottomMaxY,
        player->m_collidedLeftMaxX,
        player->m_collidedRightMinX,
        player->m_yVelocityRelated,
        player->m_scaleXRelated2,
        player->m_scaleXRelated3,
        player->m_scaleXRelated4,
        player->m_scaleXRelated5,
        player->m_wasTeleported,
        player->m_unk669,
        player->m_unk50C,
        player->m_unk510
    };

    state.dynamics = {
        player->m_gravity,
        player->m_yStart,
        player->m_speedMultiplier,
        player->m_accelerationOrSpeed,
        player->m_snapDistance,
        player->m_physDeltaRelated,
        player->m_blackOrbRelated,
        player->m_platformerXVelocity,
        player->m_platformerVelocityRelated,
        player->m_lastLandTime,
        player->m_gameModeChangedTime,
        player->m_lastJumpTime,
        player->m_lastFlipTime,
        player->m_lastSpiderFlipTime,
        player->m_dashRing,
        player->m_dashX,
        player->m_dashY,
        player->m_dashAngle,
        player->m_dashStartTime,
        player->m_dashFireFrame,
        player->m_groundObjectMaterial,
        player->m_rotationSpeed,
        player->m_xVelocityRelated,
        player->m_xVelocityRelated2,
        player->m_shipRotation,
        player->m_lastGroundedPos,
        player->m_objectSnappedTo,
        player->m_jumpBuffered,
        player->m_wasJumpBuffered,
        player->m_wasRobotJump,
        player->m_stateJumpBuffered,
        player->m_stateRingJump,
        player->m_stateRingJump2,
        player->m_touchedGravityPortal,
        player->m_maybeTouchedBreakableBlock,
        player->m_isAccelerating,
        player->m_maybeIsBoosted,
        player->m_decreaseBoostSlide,
        player->m_maybeHasStopped,
        player->m_isMoving,
        player->m_platformerMovingLeft,
        player->m_platformerMovingRight,
        player->m_isOnIce,
        player->m_affectedByForces,
        player->m_stateOnGround,
        player->m_stateBoostX,
        player->m_stateBoostY,
        player->m_stateForce,
        player->m_stateForceVector,
        player->m_maybeStateForce2,
        player->m_stateScale,
        player->m_stateNoAutoJump,
        player->m_stateDartSlide,
        player->m_stateHitHead,
        player->m_stateFlipGravity,
        player->m_stateNoStickX,
        player->m_stateNoStickY,
        player->m_scaleXRelated,
        player->m_scaleXRelatedTime,
        player->m_maybeSlidingTime,
        player->m_maybeSlidingStartTime,
        player->m_changedDirectionsTime,
        player->m_maybeChangedDirectionAngle,
        player->m_somethingPlayerSpeedTime,
        player->m_playerSpeedAC,
        player->m_yVelocityRelated3,
        player->m_fallStartY,
        player->m_followRelated,
        player->m_playerFollowFloats,
        player->m_unk838,
        player->m_fixRobotJump,
        player->m_unkA29,
        player->m_disablePlayerSqueeze,
        player->m_ignoreDamage,
        player->m_enable22Changes
    };

    state.input = {
        player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)],
        player->m_holdingButtons[static_cast<int>(PlayerButton::Left)],
        player->m_holdingButtons[static_cast<int>(PlayerButton::Right)],
        player->m_holdingLeft,
        player->m_holdingRight,
        player->m_leftPressedFirst
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
    player->m_isDead = state.form.isDead;
    player->m_isSecondPlayer = state.form.isSecondPlayer;
    player->m_isBeingSpawnedByDualPortal = state.form.isBeingSpawnedByDualPortal;
    player->m_isPlatformer = state.form.isPlatformer;
    player->m_isLocked = state.form.isLocked;
    player->m_controlsDisabled = state.form.controlsDisabled;
    player->m_inputsLocked = state.form.inputsLocked;
    player->m_hasEverJumped = state.form.hasEverJumped;
    player->m_hasEverHitRing = state.form.hasEverHitRing;
    player->m_isOutOfBounds = state.form.isOutOfBounds;

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
    player->m_maybeLastGroundObject = state.collision.maybeLastGroundObject;
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
    player->m_collidedTopMinY = state.collision.collidedTopMinY;
    player->m_collidedBottomMaxY = state.collision.collidedBottomMaxY;
    player->m_collidedLeftMaxX = state.collision.collidedLeftMaxX;
    player->m_collidedRightMinX = state.collision.collidedRightMinX;
    player->m_yVelocityRelated = state.collision.yVelocityRelated;
    player->m_scaleXRelated2 = state.collision.scaleXRelated2;
    player->m_scaleXRelated3 = state.collision.scaleXRelated3;
    player->m_scaleXRelated4 = state.collision.scaleXRelated4;
    player->m_scaleXRelated5 = state.collision.scaleXRelated5;
    player->m_wasTeleported = state.collision.wasTeleported;
    player->m_unk669 = state.collision.collisionPrimed;
    player->m_unk50C = state.collision.previousCollisionBottom;
    player->m_unk510 = state.collision.previousCollisionTop;

    player->m_gravity = state.dynamics.gravity;
    player->m_yStart = state.dynamics.yStart;
    player->m_speedMultiplier = state.dynamics.speedMultiplier;
    player->m_accelerationOrSpeed = state.dynamics.accelerationOrSpeed;
    player->m_snapDistance = state.dynamics.snapDistance;
    player->m_physDeltaRelated = state.dynamics.physDeltaRelated;
    player->m_blackOrbRelated = state.dynamics.blackOrbRelated;
    player->m_platformerXVelocity = state.dynamics.platformerXVelocity;
    player->m_platformerVelocityRelated = state.dynamics.platformerVelocityRelated;
    player->m_lastLandTime = state.dynamics.lastLandTime;
    player->m_gameModeChangedTime = state.dynamics.gameModeChangedTime;
    player->m_lastJumpTime = state.dynamics.lastJumpTime;
    player->m_lastFlipTime = state.dynamics.lastFlipTime;
    player->m_lastSpiderFlipTime = state.dynamics.lastSpiderFlipTime;
    player->m_dashRing = state.dynamics.dashRing;
    player->m_dashX = state.dynamics.dashX;
    player->m_dashY = state.dynamics.dashY;
    player->m_dashAngle = state.dynamics.dashAngle;
    player->m_dashStartTime = state.dynamics.dashStartTime;
    player->m_dashFireFrame = state.dynamics.dashFireFrame;
    player->m_groundObjectMaterial = state.dynamics.groundObjectMaterial;
    player->m_rotationSpeed = state.dynamics.rotationSpeed;
    player->m_xVelocityRelated = state.dynamics.xVelocityRelated;
    player->m_xVelocityRelated2 = state.dynamics.xVelocityRelated2;
    player->m_shipRotation = state.dynamics.shipRotation;
    player->m_lastGroundedPos = state.dynamics.lastGroundedPos;
    player->m_objectSnappedTo = nullptr;
    player->m_jumpBuffered = state.dynamics.jumpBuffered;
    player->m_wasJumpBuffered = state.dynamics.wasJumpBuffered;
    player->m_wasRobotJump = state.dynamics.wasRobotJump;
    player->m_stateJumpBuffered = state.dynamics.stateJumpBuffered;
    player->m_stateRingJump = state.dynamics.stateRingJump;
    player->m_stateRingJump2 = state.dynamics.stateRingJump2;
    player->m_touchedGravityPortal = state.dynamics.touchedGravityPortal;
    player->m_maybeTouchedBreakableBlock = state.dynamics.maybeTouchedBreakableBlock;
    player->m_isAccelerating = state.dynamics.isAccelerating;
    player->m_maybeIsBoosted = state.dynamics.maybeIsBoosted;
    player->m_decreaseBoostSlide = state.dynamics.decreaseBoostSlide;
    player->m_maybeHasStopped = state.dynamics.maybeHasStopped;
    player->m_isMoving = state.dynamics.isMoving;
    player->m_platformerMovingLeft = state.dynamics.platformerMovingLeft;
    player->m_platformerMovingRight = state.dynamics.platformerMovingRight;
    player->m_isOnIce = state.dynamics.isOnIce;
    player->m_affectedByForces = state.dynamics.affectedByForces;
    player->m_stateOnGround = state.dynamics.stateOnGround;
    player->m_stateBoostX = state.dynamics.stateBoostX;
    player->m_stateBoostY = state.dynamics.stateBoostY;
    player->m_stateForce = state.dynamics.stateForce;
    player->m_stateForceVector = state.dynamics.stateForceVector;
    player->m_maybeStateForce2 = state.dynamics.maybeStateForce2;
    player->m_stateScale = state.dynamics.stateScale;
    player->m_stateNoAutoJump = state.dynamics.stateNoAutoJump;
    player->m_stateDartSlide = state.dynamics.stateDartSlide;
    player->m_stateHitHead = state.dynamics.stateHitHead;
    player->m_stateFlipGravity = state.dynamics.stateFlipGravity;
    player->m_stateNoStickX = state.dynamics.stateNoStickX;
    player->m_stateNoStickY = state.dynamics.stateNoStickY;
    player->m_scaleXRelated = state.dynamics.scaleXRelated;
    player->m_scaleXRelatedTime = state.dynamics.scaleXRelatedTime;
    player->m_maybeSlidingTime = state.dynamics.maybeSlidingTime;
    player->m_maybeSlidingStartTime = state.dynamics.maybeSlidingStartTime;
    player->m_changedDirectionsTime = state.dynamics.changedDirectionsTime;
    player->m_maybeChangedDirectionAngle = state.dynamics.maybeChangedDirectionAngle;
    player->m_somethingPlayerSpeedTime = state.dynamics.somethingPlayerSpeedTime;
    player->m_playerSpeedAC = state.dynamics.playerSpeedAC;
    player->m_yVelocityRelated3 = state.dynamics.yVelocityRelated3;
    player->m_fallStartY = state.dynamics.fallStartY;
    player->m_followRelated = state.dynamics.followRelated;
    player->m_playerFollowFloats = state.dynamics.playerFollowFloats;
    player->m_unk838 = state.dynamics.followAccumulator;
    player->m_fixRobotJump = state.dynamics.fixRobotJump;
    player->m_unkA29 = state.dynamics.unknownA29;
    player->m_disablePlayerSqueeze = state.dynamics.disablePlayerSqueeze;
    player->m_ignoreDamage = state.dynamics.ignoreDamage;
    player->m_enable22Changes = state.dynamics.enable22Changes;

    player->m_holdingButtons[static_cast<int>(PlayerButton::Jump)] = state.input.holdingJump;
    player->m_holdingButtons[static_cast<int>(PlayerButton::Left)] = state.input.holdingLeftButton;
    player->m_holdingButtons[static_cast<int>(PlayerButton::Right)] = state.input.holdingRightButton;
    player->m_holdingLeft = state.input.holdingLeft;
    player->m_holdingRight = state.input.holdingRight;
    player->m_leftPressedFirst = state.input.leftPressedFirst;
}
bool TrajectoryPredictionService::isActiveSimulation() const {
    return m_context.activeSimulation;
}

bool TrajectoryPredictionService::isTraceCancelled() const {
    return m_context.traceCancelled;
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
        previewPlayer->setPosition({ -99999.f, -99999.f });
        previewPlayer->m_playEffects = false;
        playLayer->m_objectLayer->addChild(previewPlayer);
        m_context.previewPlayers[playerIndex] = previewPlayer;
        resetPreviewStreak(previewPlayer);
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
    for (auto*& previewPlayer : m_context.previewPlayers) {
        resetPreviewStreak(previewPlayer);
        if (previewPlayer) {
            previewPlayer->removeFromParent();
            previewPlayer = nullptr;
        }
    }
    m_context.activeSimulation = false;
    m_context.traceCancelled = false;
    m_context.holdingTrace = false;
    m_context.holdPathP1.clear();
    m_context.holdPathP2.clear();
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

bool TrajectoryPredictionService::ownsPreviewStreak(HardStreak* streak) const {
    if (!streak) {
        return false;
    }

    for (auto* player : m_context.previewPlayers) {
        if (player && player->m_waveTrail == streak) {
            return true;
        }
    }

    return false;
}

void TrajectoryPredictionService::resetPreviewStreak(PlayerObject* player) {
    if (!ownsPreviewPlayer(player) || !player->m_waveTrail) {
        return;
    }

    auto* streak = player->m_waveTrail;
    streak->m_drawStreak = false;
    streak->stopStroke();
    streak->reset();
    streak->setOpacity(0);
    streak->setVisible(false);
}

void TrajectoryPredictionService::noteSimulatedDeath(PlayerObject* player) {
    if (!player) {
        return;
    }

    m_context.collisionRotation = player->getRotation();
    m_context.traceCancelled = true;
}

void TrajectoryPredictionService::recalculateOverlapColors() {
    auto blend = [](float a, float b) { return std::min(1.0f, (a + b) * 0.5f + 0.50f); };

    m_overlapColor = {
        blend(m_holdColor.r, m_releaseColor.r),
        blend(m_holdColor.g, m_releaseColor.g),
        blend(m_holdColor.b, m_releaseColor.b),
        1.0f
    };

    m_overlapColorP2 = {
        blend(m_holdColorP2.r, m_releaseColor.r),
        blend(m_holdColorP2.g, m_releaseColor.g),
        blend(m_holdColorP2.b, m_releaseColor.b),
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

    struct BoundsScaleRule { float scaleFactor; bool divide; };
    static constexpr std::array<std::pair<float, BoundsScaleRule>, 6> kBoundsRules = {{
        { 18.0f, { 0.6f, true } },
        { 5.0f,  { 0.6f, true } },
        { 7.0f,  { 0.6f, false } },
        { 30.0f, { 0.6f, false } },
        { 29.0f, { 0.6f, false } },
        { 9.0f,  { 0.6f, false } }
    }};

    for (auto const& [dim, rule] : kBoundsRules) {
        if (dimension != dim) continue;
        bool scaleIsOne = player->getScale() == 1.0f;
        if (rule.divide && scaleIsOne) {
            for (auto& vertex : vertices) {
                vertex.x = center.x + (vertex.x - center.x) / rule.scaleFactor;
                vertex.y = center.y + (vertex.y - center.y) / rule.scaleFactor;
            }
        } else if (!rule.divide && !scaleIsOne) {
            for (auto& vertex : vertices) {
                vertex.x = center.x + (vertex.x - center.x) * rule.scaleFactor;
                vertex.y = center.y + (vertex.y - center.y) * rule.scaleFactor;
            }
        }
        break;
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

void TrajectoryPredictionService::traceInputPath(
    PlayLayer* playLayer,
    PlayerObject* previewPlayer,
    PlayerObject* sourcePlayer,
    bool holdingInput,
    bool linkedDual
) {
    if (!playLayer || !previewPlayer || !sourcePlayer) {
        return;
    }

    auto* livePlayer1 = playLayer->m_player1;
    auto* livePlayer2 = playLayer->m_player2;
    bool isSecondPlayer = livePlayer2 == sourcePlayer;
    auto* linkedPreviewPlayer = linkedDual ? m_context.previewPlayers[1] : nullptr;
    auto* linkedSourcePlayer = linkedDual ? livePlayer2 : nullptr;
    linkedDual = linkedDual
        && !isSecondPlayer
        && linkedPreviewPlayer
        && linkedSourcePlayer
        && linkedPreviewPlayer != previewPlayer;

    ScopedPredictionLayerState traceState(playLayer);

    auto seedPreviewPlayer = [&](PlayerObject* targetPlayer, PlayerObject* livePlayer, bool secondPlayer) {
        if (!targetPlayer || !livePlayer) {
            return;
        }

        auto state = capturePlayerState(livePlayer);
        applyPlayerState(targetPlayer, state);
        targetPlayer->m_isSecondPlayer = secondPlayer;
        targetPlayer->m_isPlatformer = livePlayer->m_isPlatformer;
        targetPlayer->m_playEffects = false;
        targetPlayer->m_holdingButtons[1] = livePlayer->m_holdingButtons[1];
        targetPlayer->m_holdingButtons[2] = livePlayer->m_holdingButtons[2];
        targetPlayer->m_holdingButtons[3] = livePlayer->m_holdingButtons[3];
        targetPlayer->m_holdingLeft = livePlayer->m_holdingLeft;
        targetPlayer->m_holdingRight = livePlayer->m_holdingRight;
        resetPreviewStreak(targetPlayer);

        targetPlayer->m_touchedRings.clear();
        for (auto const& ringId : livePlayer->m_touchedRings) {
            targetPlayer->m_touchedRings.insert(ringId);
        }
        if (targetPlayer->m_touchingRings) {
            targetPlayer->m_touchingRings->removeAllObjects();
        }

        targetPlayer->m_potentialSlopeMap.clear();
        for (auto const& [key, value] : livePlayer->m_potentialSlopeMap) {
            targetPlayer->m_potentialSlopeMap.insert({ key, value });
        }
    };

    seedPreviewPlayer(previewPlayer, sourcePlayer, isSecondPlayer);
    if (linkedDual) {
        seedPreviewPlayer(linkedPreviewPlayer, linkedSourcePlayer, true);
    }

    if (livePlayer1 && m_context.previewPlayers[0]) {
        if (m_context.previewPlayers[0] != previewPlayer && (!linkedDual || m_context.previewPlayers[0] != linkedPreviewPlayer)) {
            seedPreviewPlayer(m_context.previewPlayers[0], livePlayer1, false);
        }
        playLayer->m_player1 = m_context.previewPlayers[0];
    }
    if (livePlayer2 && m_context.previewPlayers[1]) {
        if (m_context.previewPlayers[1] != previewPlayer && (!linkedDual || m_context.previewPlayers[1] != linkedPreviewPlayer)) {
            seedPreviewPlayer(m_context.previewPlayers[1], livePlayer2, true);
        }
        playLayer->m_player2 = m_context.previewPlayers[1];
    }

    auto* engine = ReplayEngine::get();
    float simDt = engine->trajectorySimulationDelta();
    if (!std::isfinite(simDt) || simDt <= 0.0f) {
        simDt = 1.0f;
    }

    int frameCount = predictionFrameCount(playLayer);
    if (frameCount <= 0) {
        return;
    }

    auto& holdPath = isSecondPlayer ? m_context.holdPathP2 : m_context.holdPathP1;
    auto* linkedHoldPath = linkedDual ? &m_context.holdPathP2 : nullptr;
    if (holdingInput) {
        holdPath.assign(static_cast<size_t>(frameCount), cocos2d::CCPoint { 0.0f, 0.0f });
        if (linkedHoldPath) {
            linkedHoldPath->assign(static_cast<size_t>(frameCount), cocos2d::CCPoint { 0.0f, 0.0f });
        }
    } else {
        if (holdPath.size() < static_cast<size_t>(frameCount)) {
            holdPath.resize(static_cast<size_t>(frameCount), cocos2d::CCPoint { 0.0f, 0.0f });
        }
        if (linkedHoldPath && linkedHoldPath->size() < static_cast<size_t>(frameCount)) {
            linkedHoldPath->resize(static_cast<size_t>(frameCount), cocos2d::CCPoint { 0.0f, 0.0f });
        }
    }

    m_context.traceCancelled = false;
    m_context.holdingTrace = holdingInput;
    toasty::trajectory::physics::clearSimulationState();

    applyPreviewButtonState(previewPlayer, PlayerButton::Jump, holdingInput);
    if (linkedDual) {
        applyPreviewButtonState(linkedPreviewPlayer, PlayerButton::Jump, holdingInput);
    }

    auto applyPlatformerDirection = [&](PlayerObject* targetPlayer, PlayerObject* directionSource) {
        if (!targetPlayer || !directionSource || !playLayer->m_levelSettings || !playLayer->m_levelSettings->m_platformerMode) {
            return;
        }

        if (directionSource->m_isGoingLeft) {
            applyPreviewButtonState(targetPlayer, PlayerButton::Left, true);
        } else {
            applyPreviewButtonState(targetPlayer, PlayerButton::Right, true);
        }
    };

    applyPlatformerDirection(previewPlayer, sourcePlayer);
    if (linkedDual) {
        applyPlatformerDirection(linkedPreviewPlayer, sourcePlayer);
    }

    auto* drawNode = ensureDrawNode();
    resetPreviewStreak(previewPlayer);
    if (linkedDual) {
        resetPreviewStreak(linkedPreviewPlayer);
    }

    double predictionRate = engine ? engine->runtimeTickRate() : ReplayEngine::kBaseTickRate;
    if (!std::isfinite(predictionRate) || predictionRate <= 0.0) {
        predictionRate = ReplayEngine::kBaseTickRate;
    }
    double physicsDelta = 1.0 / predictionRate;

    auto clearCollisionLogs = [](PlayerObject* player) {
        if (!player) {
            return;
        }

        player->m_collisionLogTop->removeAllObjects();
        player->m_collisionLogBottom->removeAllObjects();
        player->m_collisionLogLeft->removeAllObjects();
        player->m_collisionLogRight->removeAllObjects();
    };

    auto updatePreviewPlayer = [&](PlayerObject* player) {
        if (!player) {
            return;
        }

        clearCollisionLogs(player);
        player->update(simDt);
        player->updateRotation(simDt);
        player->updatePlayerScale();

        playLayer->checkCollisions(player, simDt, false);
        toasty::trajectory::physics::checkSpawnObjects(playLayer, player);
        if (playLayer->m_effectManager) {
            playLayer->m_effectManager->postCollisionCheck();
        }
    };

    auto drawTraceSegment = [&](PlayerObject* player, CCPoint previousPosition, bool secondPlayer, std::vector<CCPoint> const& playerHoldPath, int frameIndex) {
        if (!drawNode || !player) {
            return;
        }

        cocos2d::ccColor4F lineColor = holdingInput
            ? (secondPlayer ? m_holdColorP2 : m_holdColor)
            : m_releaseColor;

        if (!holdingInput) {
            bool overlapsHoldPath = static_cast<size_t>(frameIndex) < playerHoldPath.size()
                && playerHoldPath[static_cast<size_t>(frameIndex)] == previousPosition;
            if (overlapsHoldPath) {
                lineColor = secondPlayer ? m_overlapColorP2 : m_overlapColor;
            }
        }

        if (frameIndex >= frameCount - 40 && frameCount > 0) {
            float t = static_cast<float>(frameCount - frameIndex) / 40.0f;
            lineColor.a = t * t;
        }

        drawNode->drawSegment(previousPosition, player->getPosition(), 0.55f, lineColor);
    };

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        CCPoint previousPosition = previewPlayer->getPosition();
        CCPoint linkedPreviousPosition = linkedDual ? linkedPreviewPlayer->getPosition() : CCPoint { 0.0f, 0.0f };

        if (holdingInput) {
            holdPath[static_cast<size_t>(frameIndex)] = previousPosition;
            if (linkedHoldPath) {
                (*linkedHoldPath)[static_cast<size_t>(frameIndex)] = linkedPreviousPosition;
            }
        }

        {
            ScopedPredictionCameraState cameraState(playLayer);
            advancePredictionClock(playLayer, previewPlayer);
            updatePreviewPlayer(previewPlayer);
            if (!m_context.traceCancelled && linkedDual) {
                linkedPreviewPlayer->m_totalTime += physicsDelta;
                updatePreviewPlayer(linkedPreviewPlayer);
            }
        }

        if (m_context.traceCancelled) {
            drawPredictionBounds(previewPlayer);
            if (linkedDual) {
                drawPredictionBounds(linkedPreviewPlayer);
            }
            break;
        }

        drawTraceSegment(previewPlayer, previousPosition, isSecondPlayer, holdPath, frameIndex);
        if (linkedDual && linkedHoldPath) {
            drawTraceSegment(linkedPreviewPlayer, linkedPreviousPosition, true, *linkedHoldPath, frameIndex);
        }
    }

    m_context.holdingTrace = false;
    toasty::trajectory::physics::clearSimulationState();
    resetPreviewStreak(previewPlayer);
    if (linkedDual) {
        resetPreviewStreak(linkedPreviewPlayer);
    }
}

void TrajectoryPredictionService::rebuildPreview(PlayLayer* playLayer) {
    if (!playLayer || !m_context.previewPlayers[0]) {
        return;
    }

    auto* drawNode = ensureDrawNode();
    if (!drawNode) {
        return;
    }

    auto* livePlayer1 = playLayer->m_player1;
    auto* livePlayer2 = playLayer->m_player2;
    PlayerStateCapsule livePlayer1State {};
    PlayerStateCapsule livePlayer2State {};
    bool restoreLivePlayer1 = livePlayer1 != nullptr;
    bool restoreLivePlayer2 = livePlayer2 != nullptr;
    if (restoreLivePlayer1) {
        livePlayer1State = capturePlayerState(livePlayer1);
    }
    if (restoreLivePlayer2) {
        livePlayer2State = capturePlayerState(livePlayer2);
    }
    bool linkedDualPreview = hasClassicDualPreview(playLayer);
    bool trueTwoPlayerPreview = hasTrueTwoPlayerPreview(playLayer);
    ScopedPredictionLayerState rebuildState(playLayer);
    ScopedReplaySimulationState simulationState(ReplayEngine::get());

    m_context.activeSimulation = true;
    toasty::trajectory::physics::clearSimulationState();
    drawNode->clear();
    drawNode->setVisible(true);

    traceInputPath(playLayer, m_context.previewPlayers[0], livePlayer1, true, linkedDualPreview);
    traceInputPath(playLayer, m_context.previewPlayers[0], livePlayer1, false, linkedDualPreview);

    if (trueTwoPlayerPreview && livePlayer2 && m_context.previewPlayers[1]) {
        traceInputPath(playLayer, m_context.previewPlayers[1], livePlayer2, true, false);
        traceInputPath(playLayer, m_context.previewPlayers[1], livePlayer2, false, false);
    }

    m_context.activeSimulation = false;
    toasty::trajectory::physics::clearSimulationState();
    rebuildState.restore();
    if (restoreLivePlayer1) {
        applyPlayerState(livePlayer1, livePlayer1State);
    }
    if (restoreLivePlayer2) {
        applyPlayerState(livePlayer2, livePlayer2State);
    }
    simulationState.restore();
    m_context.dirty = false;

    if (livePlayer1) {
        m_context.watchKeys[0] = buildWatchKey(livePlayer1);
    }
    if (livePlayer2) {
        m_context.watchKeys[1] = buildWatchKey(livePlayer2);
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
    if (!needsRebuild && hasAnySecondPreview(playLayer)) {
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
    if (!layer || !player || !objects || !ownsPreviewPlayer(player)) {
        return;
    }

    toasty::trajectory::physics::collisionCheckObjects(layer, player, objects, objectCount, dt);
}

bool TrajectoryPredictionService::handleActivationCheck(PlayerObject* player, EffectGameObject* object) {
    if (!player || !object) {
        return false;
    }

    return toasty::trajectory::physics::canActivate(player, object);
}

void TrajectoryPredictionService::handleTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
    if (!player || !object) {
        return;
    }

    toasty::trajectory::physics::triggerObject(object, player->m_gameLayer, player);
}

namespace {
    static bool isSimulating() noexcept { return TrajectoryPredictionService::get().isActiveSimulation(); }
}

class $modify(TrajectoryPreviewPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        TrajectoryPredictionService::get().updatePreview(this);
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        if (!this || !m_objectLayer) {
            return;
        }
        auto& service = TrajectoryPredictionService::get();
        service.attach(this);
        service.markDirty();
    }

    void destroyPlayer(PlayerObject* player, GameObject* gameObject) {
        auto& service = TrajectoryPredictionService::get();
        if (service.ownsPreviewPlayer(player)) {
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
        if (isSimulating()) {
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
        if (toasty::trajectory::physics::isCallingNative() || !service.isActiveSimulation() || !service.ownsPreviewPlayer(player)) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }

        service.simulateCollisionBatch(this, player, objects, objectCount, dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        auto& service = TrajectoryPredictionService::get();
        if (toasty::trajectory::physics::isCallingNative() || !service.isActiveSimulation() || !service.ownsPreviewPlayer(player)) {
            return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
        }

        return service.handleActivationCheck(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(player)) {
            return;
        }

        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        auto& service = TrajectoryPredictionService::get();
        if (toasty::trajectory::physics::isCallingNative() || !service.isActiveSimulation() || !service.ownsPreviewPlayer(player)) {
            GJBaseGameLayer::playerTouchedTrigger(player, object);
            return;
        }

        service.handleTouchedTrigger(player, object);
    }

    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(player)) {
            toasty::trajectory::physics::teleportPlayer(this, object, player);
            return;
        }

        GJBaseGameLayer::teleportPlayer(object, player);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(player)) {
            toasty::trajectory::physics::flipGravity(this, player, flip);
            return;
        }

        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    void pickupItem(EffectGameObject* object) {
        if (!isSimulating()) {
            GJBaseGameLayer::pickupItem(object);
        }
    }

    void activateEventTrigger(EventLinkTrigger* object, gd::vector<int> const& remapKeys) {
        if (!isSimulating()) {
            GJBaseGameLayer::activateEventTrigger(object, remapKeys);
        }
    }

    void activateItemEditTrigger(ItemTriggerGameObject* object) {
        if (!isSimulating()) {
            GJBaseGameLayer::activateItemEditTrigger(object);
        }
    }

    void activatePersistentItemTrigger(ItemTriggerGameObject* object) {
        if (!isSimulating()) {
            GJBaseGameLayer::activatePersistentItemTrigger(object);
        }
    }

    void activateSFXTrigger(SFXTriggerGameObject* object) {
        if (!isSimulating()) {
            GJBaseGameLayer::activateSFXTrigger(object);
        }
    }

    void activateSongEditTrigger(SongTriggerGameObject* object) {
        if (!isSimulating()) {
            GJBaseGameLayer::activateSongEditTrigger(object);
        }
    }

    void gameEventTriggered(GJGameEvent event, int value1, int value2) {
        if (!isSimulating()) {
            GJBaseGameLayer::gameEventTriggered(event, value1, value2);
        }
    }
};

class $modify(TrajectoryPreviewPlayerObject, PlayerObject) {
    void update(float dt) {
        PlayerObject::update(dt);
        TrajectoryPredictionService::get().captureFrameDelta(dt);
    }

    void ringJump(RingObject* object, bool skipCheck) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            toasty::trajectory::physics::ringJump(this, object);
            return;
        }

        PlayerObject::ringJump(object, skipCheck);
    }

    void bumpPlayer(float bumpMod, int objectType, bool noEffects, GameObject* object) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            toasty::trajectory::physics::bumpPlayer(this, bumpMod, objectType, true, object);
            return;
        }

        PlayerObject::bumpPlayer(bumpMod, objectType, noEffects, object);
    }

    void propellPlayer(float yVelocity, bool noEffects, int objectType) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            toasty::trajectory::physics::propellPlayer(this, yVelocity, true, objectType);
            return;
        }

        PlayerObject::propellPlayer(yVelocity, noEffects, objectType);
    }

    void startDashing(DashRingObject* object) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            toasty::trajectory::physics::startDashing(this, object);
            return;
        }

        PlayerObject::startDashing(object);
    }

    void stopDashing() {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            toasty::trajectory::physics::stopDashing(this);
            return;
        }

        PlayerObject::stopDashing();
    }

    void flipGravity(bool flip, bool noEffects) {
        auto& service = TrajectoryPredictionService::get();
        if (!toasty::trajectory::physics::isCallingNative() && service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            toasty::trajectory::physics::flipGravity(this->m_gameLayer, this, flip);
            return;
        }

        PlayerObject::flipGravity(flip, noEffects);
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            return;
        }

        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            return;
        }

        PlayerObject::incrementJumps();
    }

    void playBumpEffect(int objectType, GameObject* player) {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            return;
        }

        PlayerObject::playBumpEffect(objectType, player);
    }

    void spawnCircle() {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            return;
        }

        PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            return;
        }

        PlayerObject::spawnDualCircle();
    }

    void addAllParticles() {
        auto& service = TrajectoryPredictionService::get();
        if (service.isActiveSimulation() && service.ownsPreviewPlayer(this)) {
            return;
        }

        PlayerObject::addAllParticles();
    }

    void activateStreak() {
        auto& service = TrajectoryPredictionService::get();
        if (service.ownsPreviewPlayer(this)) {
            service.resetPreviewStreak(this);
            return;
        }

        PlayerObject::activateStreak();
    }

    void createFadeOutDartStreak() {
        auto& service = TrajectoryPredictionService::get();
        if (service.ownsPreviewPlayer(this)) {
            service.resetPreviewStreak(this);
            return;
        }

        PlayerObject::createFadeOutDartStreak();
    }
};

class $modify(TrajectoryPreviewHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint point) {
        auto& service = TrajectoryPredictionService::get();
        if (service.ownsPreviewStreak(this)) {
            return;
        }

        HardStreak::addPoint(point);
    }

    void updateStroke(float dt) {
        auto& service = TrajectoryPredictionService::get();
        if (service.ownsPreviewStreak(this)) {
            return;
        }

        HardStreak::updateStroke(dt);
    }
};

class $modify(TrajectoryPreviewGameObject, GameObject) {
    void playShineEffect() {
        if (!isSimulating()) {
            GameObject::playShineEffect();
        }
    }

    void activateObject() {
        if (!isSimulating()) {
            GameObject::activateObject();
        }
    }
};

class $modify(TrajectoryPreviewEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int unk, const gd::vector<int>* groups) {
        if (!isSimulating()) {
            EffectGameObject::triggerObject(layer, unk, groups);
        }
    }

    void triggerActivated(float value) {
        if (!isSimulating()) {
            EffectGameObject::triggerActivated(value);
        }
    }

    void playTriggerEffect() {
        if (!isSimulating()) {
            EffectGameObject::playTriggerEffect();
        }
    }
};
class $modify(TrajectoryPreviewRingObject, RingObject) {
    void triggerActivated(float xPosition) {
        if (!isSimulating()) {
            RingObject::triggerActivated(xPosition);
        }
    }

    void spawnCircle() {
        if (!isSimulating()) {
            RingObject::spawnCircle();
        }
    }

    void powerOnObject(int state) {
        if (!isSimulating()) {
            RingObject::powerOnObject(state);
        }
    }
};
