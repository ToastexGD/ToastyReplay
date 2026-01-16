#include "ToastyReplay.hpp"
#include "replay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <random>
using namespace geode::prelude;

// Seed address for Windows
#ifdef GEODE_IS_WINDOWS
const uintptr_t seedAddr = 0x6a4e20;
#endif

void updateSeed(bool isRestart = false) {
    ToastyReplay* mgr = ToastyReplay::get();
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return;

    if (mgr->seedEnabled) {
        int finalSeed;

        if (!pl->m_player1->m_isDead) {
            std::mt19937 generator(mgr->seedValue + pl->m_gameState.m_currentProgress);
            std::uniform_int_distribution<int> distribution(10000, 999999999);
            finalSeed = distribution(generator);
        } else {
            std::random_device rd;
            std::mt19937 generator(rd());
            std::uniform_int_distribution<int> distribution(1000, 999999999);
            finalSeed = distribution(generator);
        }

#ifdef GEODE_IS_WINDOWS
        *(uintptr_t*)((char*)geode::base::get() + seedAddr) = finalSeed;
#else
        GameToolbox::fast_srand(finalSeed);
#endif
    }

    // Store seed when recording starts/restarts
    if (isRestart && mgr->state == RECORD) {
#ifdef GEODE_IS_WINDOWS
        mgr->macroSeed = *(uintptr_t*)((char*)geode::base::get() + seedAddr);
#else
        mgr->macroSeed = 0;
#endif
    }
}

class $modify(GJBaseGameLayer) {
    void handleButton(bool down, int button, bool p1) {
        ToastyReplay* mgr = ToastyReplay::get();

        // Only ignore manual input, not replay input
        if (mgr->state == PLAYBACK && mgr->ignoreManualInput && !mgr->isReplayInput) {
            return;
        }

        GJBaseGameLayer::handleButton(down, button, p1);

        if (mgr->state == RECORD && mgr->currentReplay) {
            bool p2 = !p1 && m_levelSettings->m_twoPlayerMode && m_gameState.m_isDualMode;
            mgr->currentReplay->addInput(m_gameState.m_currentProgress - 1, button, p2, down);
        }
    }
};

class $modify(PlayLayer) {
    bool init(GJGameLevel* lvl, bool useReplay, bool dontCreateObjects) {        
        ToastyReplay* mgr = ToastyReplay::get();
        
        if (mgr->state == RECORD) {
            mgr->createNewReplay(lvl);
        }

        return PlayLayer::init(lvl, useReplay, dontCreateObjects);
    }

    void resetLevel() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Store if we're in practice mode before reset
        bool inPractice = m_isPracticeMode;
        
        PlayLayer::resetLevel();
        
        // If NOT in practice mode and recording, clear all inputs (fresh start)
        if (!inPractice && mgr->state == RECORD && mgr->currentReplay) {
            mgr->currentReplay->inputs.clear();
        }
        // Note: In practice mode, input purging is handled by practicemode.cpp
    }

    void levelComplete() {
        ToastyReplay* mgr = ToastyReplay::get();
        mgr->safeMode = false;
        
        // On level complete, update the replay name
        if (mgr->state == RECORD && mgr->currentReplay) {
            mgr->currentReplay->name = fmt::format("{} - 100%", mgr->currentReplay->levelInfo.name);
        }
        
        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        ToastyReplay* mgr = ToastyReplay::get();
        
        if (mgr->noclip) {
            mgr->noclipDeaths++;
            
            if (mgr->noclipAccuracyEnabled && mgr->noclipAccuracyLimit > 0.0f && mgr->noclipTotalFrames > 0) {
                float accuracy = 100.0f * (1.0f - (float)mgr->noclipDeaths / (float)mgr->noclipTotalFrames);
                if (accuracy < mgr->noclipAccuracyLimit) {
                    mgr->noclipDeaths = 0;
                    mgr->noclipTotalFrames = 0;
                    
                    mgr->noclip = false;
                    PlayLayer::destroyPlayer(p0, p1);
                    mgr->noclip = true;
                    return;
                }
            }
            return;
        }
        
        mgr->noclipDeaths = 0;
        mgr->noclipTotalFrames = 0;

        PlayLayer::destroyPlayer(p0, p1);
    }

    void onQuit() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        if (mgr->state == RECORD && mgr->currentReplay) {
            delete mgr->currentReplay;
            mgr->currentReplay = nullptr;
            mgr->state = NONE;
        }
        
        PlayLayer::onQuit();
    }
};
