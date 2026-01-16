#include "ToastyReplay.hpp"
#include "replay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
using namespace geode::prelude;

int currIndex = 0;
int clickBotIndex = 0;

class $modify(zGJBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float delta) {
        GJBaseGameLayer::processCommands(delta);

        ToastyReplay* mgr = ToastyReplay::get();

        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            while (currIndex < mgr->currentReplay->inputs.size() && 
                   mgr->currentReplay->inputs[currIndex].frame < m_gameState.m_currentProgress) {
                
                auto input = mgr->currentReplay->inputs[currIndex++];
                mgr->isReplayInput = true;
                GJBaseGameLayer::handleButton(input.down, input.button, !input.player2);
                mgr->isReplayInput = false;
            }
        }
    }
};

class $modify(PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();

        ToastyReplay* mgr = ToastyReplay::get();
        if (mgr->state == PLAYBACK && mgr->currentReplay) {
            mgr->tps = mgr->currentReplay->framerate;

            currIndex = 0;
            clickBotIndex = 0;

            while (currIndex < mgr->currentReplay->inputs.size() && 
                   mgr->currentReplay->inputs[currIndex].frame < m_gameState.m_currentProgress) {
                currIndex++;
                clickBotIndex++;
            }

            while (clickBotIndex < mgr->currentReplay->inputs.size() && 
                   mgr->currentReplay->inputs[clickBotIndex].frame < m_gameState.m_currentProgress) {
                clickBotIndex++;
            }
        }
    }
};
