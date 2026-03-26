#pragma once

#include "replay.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

class InputStateRestorer {
public:
    static uint8_t captureLatchMask(PlayerObject* player);
    static void restoreInputState(PlayLayer* playLayer, PlaybackAnchor const& anchor);
    static void restoreInputState(PlayLayer* playLayer, CheckpointStateBundle const& checkpoint);
};

class PlayerStateRestorer {
public:
    static PlayerStateBundle captureState(PlayerObject* player, bool isPlatformer, bool isDual, bool isTwoPlayer);
    static void restoreState(PlayerObject* player, PlayerStateBundle const& state);

    static float positionalDrift(PlayerObject* player, PlayerStateBundle const& state);
    static float rotationDrift(PlayerObject* player, PlayerStateBundle const& state);
    static float velocityDrift(PlayerObject* player, PlayerStateBundle const& state);
    static bool needsReconciliation(
        PlayerObject* player,
        PlayerStateBundle const& state,
        float positionTolerance = 0.01f,
        float rotationTolerance = 0.1f,
        float velocityTolerance = 0.01f
    );
};

class AnchorReconciler {
public:
    static PlaybackAnchor captureAnchor(
        int tick,
        PlayLayer* playLayer,
        PlayerObject* player1,
        PlayerObject* player2,
        AnchorRngState const& rng
    );

    static bool reconcile(
        PlayLayer* playLayer,
        PlayerObject* player1,
        PlayerObject* player2,
        PlaybackAnchor const& anchor,
        bool force = false
    );
};

class CheckpointStateManager {
public:
    static CheckpointStateBundle capture(
        int tick,
        int priorTick,
        PlayLayer* playLayer,
        AnchorRngState const& rng
    );

    static void restore(PlayLayer* playLayer, CheckpointStateBundle const& checkpoint);
};
