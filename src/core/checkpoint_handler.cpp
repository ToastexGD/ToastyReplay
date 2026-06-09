#include "core/checkpoint_handler.hpp"
#include "hacks/autoclicker.hpp"

#include <cmath>

namespace {
    static uint8_t encodeButtonHoldMask(PlayerObject* player) {
        if (!player) {
            return 0;
        }
        uint8_t mask = 0;
        if (player->m_holdingButtons[1]) mask |= 1 << 0;
        if (player->m_holdingButtons[2]) mask |= 1 << 1;
        if (player->m_holdingButtons[3]) mask |= 1 << 2;
        return mask;
    }

    static void applyButtonHoldMask(PlayerObject* player, uint8_t mask) {
        if (!player) {
            return;
        }
        player->m_holdingButtons[1] = (mask & (1 << 0)) != 0;
        player->m_holdingButtons[2] = (mask & (1 << 1)) != 0;
        player->m_holdingButtons[3] = (mask & (1 << 2)) != 0;
    }

    static void deactivateActiveVehicleVisual(PlayerObject* player) {
        if (player->m_isShip)   player->toggleFlyMode(false, true);
        if (player->m_isBall)   player->toggleRollMode(false, true);
        if (player->m_isBird)   player->toggleBirdMode(false, true);
        if (player->m_isDart)   player->toggleDartMode(false, true);
        if (player->m_isSpider) player->toggleSpiderMode(false, true);
        if (player->m_isSwing)  player->toggleSwingMode(false, true);
        if (player->m_isRobot)  player->toggleRobotMode(false, true);
    }

    static void reconcileVehicleVisual(PlayerObject* player, PlayerStateBundle const& state) {
        bool sameMode = player->m_isShip == state.flags.ship
            && player->m_isBird == state.flags.bird
            && player->m_isBall == state.flags.ball
            && player->m_isDart == state.flags.wave
            && player->m_isRobot == state.flags.robot
            && player->m_isSpider == state.flags.spider
            && player->m_isSwing == state.flags.swing;
        if (sameMode) {
            return;
        }

        deactivateActiveVehicleVisual(player);

        if (state.flags.ship)        player->toggleFlyMode(true, true);
        else if (state.flags.ball)   player->toggleRollMode(true, true);
        else if (state.flags.bird)   player->toggleBirdMode(true, true);
        else if (state.flags.wave)   player->toggleDartMode(true, true);
        else if (state.flags.spider) player->toggleSpiderMode(true, true);
        else if (state.flags.swing)  player->toggleSwingMode(true, true);
        else if (state.flags.robot)  player->toggleRobotMode(true, true);
    }
}

uint8_t InputStateRestorer::captureLatchMask(PlayerObject* player) {
    return encodeButtonHoldMask(player);
}

void InputStateRestorer::restoreInputState(PlayLayer* playLayer, PlaybackAnchor const& anchor) {
    if (!playLayer || !playLayer->m_player1) {
        return;
    }

    applyButtonHoldMask(playLayer->m_player1, anchor.player1LatchMask);
    playLayer->m_player1->m_holdingLeft = anchor.player1.flags.holdingLeft;
    playLayer->m_player1->m_holdingRight = anchor.player1.flags.holdingRight;

    if (anchor.hasPlayer2 && playLayer->m_player2) {
        applyButtonHoldMask(playLayer->m_player2, anchor.player2LatchMask);
        playLayer->m_player2->m_holdingLeft = anchor.player2.flags.holdingLeft;
        playLayer->m_player2->m_holdingRight = anchor.player2.flags.holdingRight;
    }
}

void InputStateRestorer::restoreInputState(PlayLayer* playLayer, CheckpointStateBundle const& checkpoint) {
    if (!playLayer || !playLayer->m_player1) {
        return;
    }

    applyButtonHoldMask(playLayer->m_player1, checkpoint.player1LatchMask);
    playLayer->m_player1->m_holdingLeft = checkpoint.player1.flags.holdingLeft;
    playLayer->m_player1->m_holdingRight = checkpoint.player1.flags.holdingRight;

    if (playLayer->m_player2) {
        applyButtonHoldMask(playLayer->m_player2, checkpoint.player2LatchMask);
        playLayer->m_player2->m_holdingLeft = checkpoint.player2.flags.holdingLeft;
        playLayer->m_player2->m_holdingRight = checkpoint.player2.flags.holdingRight;
    }
}

PlayerStateBundle PlayerStateRestorer::captureState(PlayerObject* player, bool isPlatformer, bool isDual, bool isTwoPlayer) {
    PlayerStateBundle state;
    if (!player) {
        return state;
    }

    state.motion.position = player->getPosition();
    state.motion.rotation = player->getRotation();
    state.motion.verticalVelocity = player->m_yVelocity;
    state.motion.preSlopeVerticalVelocity = player->m_yVelocityBeforeSlope;
    state.motion.horizontalVelocity = isPlatformer ? player->m_platformerXVelocity : 0.0;
    state.motion.dashX = player->m_dashX;
    state.motion.dashY = player->m_dashY;
    state.motion.dashAngle = player->m_dashAngle;
    state.motion.dashStartTime = player->m_dashStartTime;
    state.motion.slopeStartTime = player->m_slopeStartTime;
    state.motion.fallSpeed = player->m_fallSpeed;
    state.motion.slopeVelocity = player->m_slopeVelocity;
    state.motion.shipRotation = player->m_shipRotation;
    state.motion.lastPortalPosition = player->m_lastPortalPos;
    state.motion.stateForceVector = player->m_stateForceVector;

    state.flags.upsideDown = player->m_isUpsideDown;
    state.flags.holdingLeft = player->m_holdingLeft;
    state.flags.holdingRight = player->m_holdingRight;
    state.flags.platformer = isPlatformer;
    state.flags.dead = player->m_isDead;
    state.flags.ship = player->m_isShip;
    state.flags.bird = player->m_isBird;
    state.flags.ball = player->m_isBall;
    state.flags.wave = player->m_isDart;
    state.flags.robot = player->m_isRobot;
    state.flags.spider = player->m_isSpider;
    state.flags.swing = player->m_isSwing;
    state.flags.sideways = player->m_isSideways;
    state.flags.dashing = player->m_isDashing;
    state.flags.onSlope = player->m_isOnSlope;
    state.flags.wasOnSlope = player->m_wasOnSlope;
    state.flags.onGround = player->m_isOnGround;
    state.flags.goingLeft = player->m_isGoingLeft;
    state.flags.platformerMovingRight = player->m_platformerMovingRight;
    state.flags.slidingRight = player->m_isSlidingRight;
    state.flags.accelerating = player->m_isAccelerating;
    state.flags.affectedByForces = player->m_affectedByForces;
    state.flags.jumpBuffered = player->m_jumpBuffered;
    state.flags.buttonHolds[0] = player->m_holdingButtons[1];
    state.flags.buttonHolds[1] = player->m_holdingButtons[2];
    state.flags.buttonHolds[2] = player->m_holdingButtons[3];

    state.environment.gravity = player->m_gravity;
    state.environment.gravityMod = player->m_gravityMod;
    state.environment.playerSpeed = player->m_playerSpeed;
    state.environment.playerSpeedAC = player->m_playerSpeedAC;
    state.environment.speedMultiplier = player->m_speedMultiplier;
    state.environment.vehicleSize = player->m_vehicleSize;
    state.environment.reverseRelated = player->m_reverseRelated;
    state.environment.stateDartSlide = player->m_stateDartSlide;
    state.environment.stateFlipGravity = player->m_stateFlipGravity;
    state.environment.stateForce = player->m_stateForce;
    state.environment.dualContext = isDual;
    state.environment.twoPlayerContext = isTwoPlayer;
    state.environment.extendedState = true;
    return state;
}

void PlayerStateRestorer::restoreState(PlayerObject* player, PlayerStateBundle const& state) {
    if (!player) {
        return;
    }

    if (state.environment.extendedState) {
        if (player->m_isDashing && !state.flags.dashing) {
            player->stopDashing();
        }
        reconcileVehicleVisual(player, state);
    }

    player->setPosition(state.motion.position);
    player->setRotation(state.motion.rotation);
    player->m_yVelocity = state.motion.verticalVelocity;
    player->m_yVelocityBeforeSlope = state.motion.preSlopeVerticalVelocity;
    if (state.flags.platformer) {
        player->m_platformerXVelocity = state.motion.horizontalVelocity;
    }

    player->m_isUpsideDown = state.flags.upsideDown;
    player->m_holdingLeft = state.flags.holdingLeft;
    player->m_holdingRight = state.flags.holdingRight;
    player->m_isDead = state.flags.dead;
    player->m_holdingButtons[1] = state.flags.buttonHolds[0];
    player->m_holdingButtons[2] = state.flags.buttonHolds[1];
    player->m_holdingButtons[3] = state.flags.buttonHolds[2];

    if (state.environment.gravity != 0.0) {
        player->m_gravity = state.environment.gravity;
    }

    if (state.environment.extendedState) {
        player->m_dashX = state.motion.dashX;
        player->m_dashY = state.motion.dashY;
        player->m_dashAngle = state.motion.dashAngle;
        player->m_dashStartTime = state.motion.dashStartTime;
        player->m_slopeStartTime = state.motion.slopeStartTime;
        player->m_fallSpeed = state.motion.fallSpeed;
        player->m_slopeVelocity = state.motion.slopeVelocity;
        player->m_shipRotation = state.motion.shipRotation;
        player->m_lastPortalPos = state.motion.lastPortalPosition;
        player->m_stateForceVector = state.motion.stateForceVector;

        player->m_isShip = state.flags.ship;
        player->m_isBird = state.flags.bird;
        player->m_isBall = state.flags.ball;
        player->m_isDart = state.flags.wave;
        player->m_isRobot = state.flags.robot;
        player->m_isSpider = state.flags.spider;
        player->m_isSwing = state.flags.swing;
        player->m_isSideways = state.flags.sideways;
        player->m_isDashing = state.flags.dashing;
        player->m_isOnSlope = state.flags.onSlope;
        player->m_wasOnSlope = state.flags.wasOnSlope;
        player->m_isOnGround = state.flags.onGround;
        player->m_isGoingLeft = state.flags.goingLeft;
        player->m_platformerMovingRight = state.flags.platformerMovingRight;
        player->m_isSlidingRight = state.flags.slidingRight;
        player->m_isAccelerating = state.flags.accelerating;
        player->m_affectedByForces = state.flags.affectedByForces;
        player->m_jumpBuffered = state.flags.jumpBuffered;

        player->m_gravityMod = state.environment.gravityMod;
        player->m_playerSpeed = state.environment.playerSpeed;
        player->m_playerSpeedAC = state.environment.playerSpeedAC;
        player->m_speedMultiplier = state.environment.speedMultiplier;
        player->m_vehicleSize = state.environment.vehicleSize;
        player->m_reverseRelated = state.environment.reverseRelated;
        player->m_stateDartSlide = state.environment.stateDartSlide;
        player->m_stateFlipGravity = state.environment.stateFlipGravity;
        player->m_stateForce = state.environment.stateForce;
    }
}

float PlayerStateRestorer::positionalDrift(PlayerObject* player, PlayerStateBundle const& state) {
    if (!player) {
        return 0.0f;
    }

    auto position = player->getPosition();
    float dx = position.x - state.motion.position.x;
    float dy = position.y - state.motion.position.y;
    return std::sqrt(dx * dx + dy * dy);
}

float PlayerStateRestorer::rotationDrift(PlayerObject* player, PlayerStateBundle const& state) {
    if (!player) {
        return 0.0f;
    }

    return std::abs(player->getRotation() - state.motion.rotation);
}

float PlayerStateRestorer::velocityDrift(PlayerObject* player, PlayerStateBundle const& state) {
    if (!player) {
        return 0.0f;
    }

    double yDiff = std::abs(player->m_yVelocity - state.motion.verticalVelocity);
    double slopeDiff = std::abs(player->m_yVelocityBeforeSlope - state.motion.preSlopeVerticalVelocity);
    double xDiff = state.flags.platformer
        ? std::abs(player->m_platformerXVelocity - state.motion.horizontalVelocity)
        : 0.0;
    return static_cast<float>(std::max({ yDiff, slopeDiff, xDiff }));
}

bool PlayerStateRestorer::needsReconciliation(
    PlayerObject* player,
    PlayerStateBundle const& state,
    float positionTolerance,
    float rotationTolerance,
    float velocityTolerance
) {
    if (!player) {
        return false;
    }

    if (positionalDrift(player, state) > positionTolerance) {
        return true;
    }
    if (rotationDrift(player, state) > rotationTolerance) {
        return true;
    }
    if (velocityDrift(player, state) > velocityTolerance) {
        return true;
    }
    if (player->m_isUpsideDown != state.flags.upsideDown) {
        return true;
    }
    if (!state.environment.extendedState) {
        return false;
    }
    return player->m_isShip != state.flags.ship
        || player->m_isBird != state.flags.bird
        || player->m_isBall != state.flags.ball
        || player->m_isDart != state.flags.wave
        || player->m_isRobot != state.flags.robot
        || player->m_isSpider != state.flags.spider
        || player->m_isSwing != state.flags.swing
        || player->m_isSideways != state.flags.sideways
        || player->m_isDashing != state.flags.dashing
        || player->m_isGoingLeft != state.flags.goingLeft;
}

PlaybackAnchor AnchorReconciler::captureAnchor(
    int tick,
    PlayLayer* playLayer,
    PlayerObject* player1,
    PlayerObject* player2,
    AnchorRngState const& rng
) {
    PlaybackAnchor anchor;
    bool isPlatformer = playLayer && playLayer->m_levelSettings
        ? playLayer->m_levelSettings->m_platformerMode
        : false;
    bool isTwoPlayer = playLayer && playLayer->m_levelSettings
        ? playLayer->m_levelSettings->m_twoPlayerMode
        : false;
    bool isDual = playLayer
        ? (playLayer->m_gameState.m_isDualMode || isTwoPlayer)
        : false;

    anchor.tick = tick;
    anchor.hasPlayer2 = isDual;
    anchor.rng = rng;
    anchor.player1 = PlayerStateRestorer::captureState(player1, isPlatformer, isDual, isTwoPlayer);
    anchor.player1LatchMask = InputStateRestorer::captureLatchMask(player1);
    if (isDual) {
        anchor.player2 = PlayerStateRestorer::captureState(player2, isPlatformer, isDual, isTwoPlayer);
        anchor.player2LatchMask = InputStateRestorer::captureLatchMask(player2);
    }
    return anchor;
}

bool AnchorReconciler::reconcile(
    PlayLayer* playLayer,
    PlayerObject* player1,
    PlayerObject* player2,
    PlaybackAnchor const& anchor,
    bool force
) {
    bool changed = false;
    bool syncPlayer1 = force || PlayerStateRestorer::needsReconciliation(player1, anchor.player1);
    bool syncPlayer2 = anchor.hasPlayer2 && (force || PlayerStateRestorer::needsReconciliation(player2, anchor.player2));

    if (syncPlayer1) {
        PlayerStateRestorer::restoreState(player1, anchor.player1);
        changed = true;
    }

    if (syncPlayer2) {
        PlayerStateRestorer::restoreState(player2, anchor.player2);
        changed = true;
    }

    if (changed) {
        InputStateRestorer::restoreInputState(playLayer, anchor);
    }

    return changed;
}

CheckpointStateBundle CheckpointStateManager::capture(
    int tick,
    int priorTick,
    PlayLayer* playLayer,
    AnchorRngState const& rng
) {
    CheckpointStateBundle checkpoint;
    checkpoint.tick = tick;
    checkpoint.priorTick = priorTick;
    checkpoint.rng = rng;

    if (!playLayer) {
        return checkpoint;
    }

    bool isPlatformer = playLayer->m_levelSettings && playLayer->m_levelSettings->m_platformerMode;
    bool isTwoPlayer = playLayer->m_levelSettings && playLayer->m_levelSettings->m_twoPlayerMode;
    bool isDual = playLayer->m_gameState.m_isDualMode || isTwoPlayer;

    checkpoint.player1 = PlayerStateRestorer::captureState(playLayer->m_player1, isPlatformer, isDual, isTwoPlayer);
    checkpoint.player2 = PlayerStateRestorer::captureState(playLayer->m_player2, isPlatformer, isDual, isTwoPlayer);
    checkpoint.player1LatchMask = InputStateRestorer::captureLatchMask(playLayer->m_player1);
    checkpoint.player2LatchMask = InputStateRestorer::captureLatchMask(playLayer->m_player2);
    auto* autoclicker = Autoclicker::get();
    checkpoint.timedAutoclickerActive = autoclicker->enabled && autoclicker->isTimedMode();
    checkpoint.timedAutoclicker[0] = {
        autoclicker->captureTimedPlayerState(false).holding,
        autoclicker->captureTimedPlayerState(false).userHolding,
        autoclicker->captureTimedPlayerState(false).timeUntilNextEdge
    };
    checkpoint.timedAutoclicker[1] = {
        autoclicker->captureTimedPlayerState(true).holding,
        autoclicker->captureTimedPlayerState(true).userHolding,
        autoclicker->captureTimedPlayerState(true).timeUntilNextEdge
    };
    return checkpoint;
}

void CheckpointStateManager::restore(PlayLayer* playLayer, CheckpointStateBundle const& checkpoint) {
    if (!playLayer) {
        return;
    }

    PlayerStateRestorer::restoreState(playLayer->m_player1, checkpoint.player1);
    if (playLayer->m_player2) {
        PlayerStateRestorer::restoreState(playLayer->m_player2, checkpoint.player2);
    }
    InputStateRestorer::restoreInputState(playLayer, checkpoint);

    auto* autoclicker = Autoclicker::get();
    if (checkpoint.timedAutoclickerActive) {
        autoclicker->restoreTimedPlayerState(false, {
            checkpoint.timedAutoclicker[0].holding,
            checkpoint.timedAutoclicker[0].userHolding,
            checkpoint.timedAutoclicker[0].timeUntilNextEdge
        });
        autoclicker->restoreTimedPlayerState(true, {
            checkpoint.timedAutoclicker[1].holding,
            checkpoint.timedAutoclicker[1].userHolding,
            checkpoint.timedAutoclicker[1].timeUntilNextEdge
        });
    } else if (autoclicker->isTimedMode()) {
        autoclicker->reset();
    }
}
