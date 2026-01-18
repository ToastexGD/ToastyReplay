#pragma once

#include "ToastyReplay.hpp"
#include "replay.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

const int indexButton[6] = { 1, 2, 3, 1, 2, 3 };

const std::map<int, int> buttonIndex[2] = { { {1, 0}, {2, 1}, {3, 2} }, { {1, 3}, {2, 4}, {3, 5} } };

const int sidesButtons[4] = { 1, 2, 4, 5 };

struct button {
    int button;
    bool player2;
    bool down;
};

class InputPracticeFixes {
public:
    static void applyFixes(PlayLayer* pl, PlayerStateData p1Data, PlayerStateData p2Data, int frame);

    static void eraseActions(int frame);

    static std::vector<button> findButtons();

    static std::vector<int> fixInputs(std::vector<button> foundButtons, PlayLayer* pl, PlayerStateData p1Data, PlayerStateData p2Data, int frame);
};

class PlayerPracticeFixes {
public:
    static void applyData(PlayerObject* player, PlayerStateData data, bool isPlayer2, bool isFakePlayer = false);

    static PlayerStateData saveData(PlayerObject* player);
};
