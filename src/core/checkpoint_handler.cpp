#include "core/checkpoint_handler.hpp"

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

    state.flags.upsideDown = player->m_isUpsideDown;
    state.flags.holdingLeft = player->m_holdingLeft;
    state.flags.holdingRight = player->m_holdingRight;
    state.flags.platformer = isPlatformer;
    state.flags.dead = player->m_isDead;
    state.flags.buttonHolds[0] = player->m_holdingButtons[1];
    state.flags.buttonHolds[1] = player->m_holdingButtons[2];
    state.flags.buttonHolds[2] = player->m_holdingButtons[3];

    state.environment.gravity = player->m_gravity;
    state.environment.dualContext = isDual;
    state.environment.twoPlayerContext = isTwoPlayer;
    return state;
}

void PlayerStateRestorer::restoreState(PlayerObject* player, PlayerStateBundle const& state) {
    if (!player) {
        return;
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
    return player->m_isUpsideDown != state.flags.upsideDown;
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
}
