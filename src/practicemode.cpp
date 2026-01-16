// A simple practice mode implementation for the free version
#include "ToastyReplay.hpp"
#include "replay.hpp"
#include "utils.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GameObject.hpp>
using namespace geode::prelude;

struct Checkpoint {
    std::vector<std::vector<std::byte>> savedPairs;
    int inputCount;  // Number of inputs at this checkpoint
    int frame;       // Frame number at this checkpoint
};

std::vector<Checkpoint> checkpoints;

const std::array memberPairs = {
    makeMemberPair(&PlayerObject::m_yVelocity),
};

class $modify(PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        checkpoints.clear();
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void togglePracticeMode(bool practice) {
        checkpoints.clear();
        PlayLayer::togglePracticeMode(practice);
    }

    CheckpointObject* createCheckpoint() {
        ToastyReplay* mgr = ToastyReplay::get();
        Checkpoint cp;
        
        for (auto pair : memberPairs) {
            if (m_player1) cp.savedPairs.push_back(getValue(m_player1, pair));
            if (m_player2) cp.savedPairs.push_back(getValue(m_player2, pair));
        }
        
        // Store the current input count and frame at this checkpoint
        if (mgr->state == RECORD && mgr->currentReplay) {
            cp.inputCount = static_cast<int>(mgr->currentReplay->inputs.size());
            cp.frame = m_gameState.m_currentProgress;
        } else {
            cp.inputCount = 0;
            cp.frame = 0;
        }
        
        checkpoints.push_back(cp);

        return PlayLayer::createCheckpoint();
    }

    void removeCheckpoint(bool p0) {
        if (!checkpoints.empty()) {
            checkpoints.pop_back();
        }
        
        return PlayLayer::removeCheckpoint(p0);
    }

    void resetLevel() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Check if we're loading from a checkpoint (not a full reset)
        bool loadingFromCheckpoint = m_isPracticeMode && !checkpoints.empty();
        
        // If recording and loading from checkpoint, purge inputs recorded after the checkpoint
        if (loadingFromCheckpoint && mgr->state == RECORD && mgr->currentReplay) {
            Checkpoint& cp = checkpoints.back();
            
            // Remove inputs that were recorded after this checkpoint
            if (cp.inputCount < static_cast<int>(mgr->currentReplay->inputs.size())) {
                mgr->currentReplay->inputs.resize(cp.inputCount);
                log::info("Purged inputs after checkpoint. Keeping {} inputs.", cp.inputCount);
            }
        }
        
        PlayLayer::resetLevel();

        if (checkpoints.empty()) {
            return;
        }

        Checkpoint cp = checkpoints.back();

        int i = 0;
        for (auto pair : memberPairs) {
            if (m_player1) restoreValue(m_player1, pair, cp.savedPairs[i++]);
            if (m_player2) restoreValue(m_player2, pair, cp.savedPairs[i++]);
        }
    }

    void onExit() {
        PlayLayer::onExit();
        checkpoints.clear();
    }
};
