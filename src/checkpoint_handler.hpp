#pragma once

#include "ToastyReplay.hpp"
#include "replay.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

const int actionMapping[6] = { 1, 2, 3, 1, 2, 3 };

const std::map<int, int> actionIndexLookup[2] = { { {1, 0}, {2, 1}, {3, 2} }, { {1, 3}, {2, 4}, {3, 5} } };

const int lateralActions[4] = { 1, 2, 4, 5 };

struct ActionRecord {
    int actionType;
    bool secondPlayer;
    bool pressed;
};

class InputStateRestorer {
public:
    static void restoreInputState(PlayLayer* pl, PhysicsSnapshot p1State, PhysicsSnapshot p2State, int tick);

    static void removeActions(int tick);

    static std::vector<ActionRecord> detectActions();

    static std::vector<int> correctInputs(std::vector<ActionRecord> detectedActions, PlayLayer* pl, PhysicsSnapshot p1State, PhysicsSnapshot p2State, int tick);
};

class PlayerStateRestorer {
public:
    static void restoreState(PlayerObject* player, PhysicsSnapshot state, bool isSecondPlayer, bool isSimulated = false);

    static PhysicsSnapshot captureState(PlayerObject* player);
};
