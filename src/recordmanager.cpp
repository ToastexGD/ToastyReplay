#include "ToastyReplay.hpp"
#include "replay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelSelectLayer.hpp>
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

        // REMOVED: mgr->safeMode = true; - Safe Mode is now independent
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

        if (mgr->state == RECORD) {
            // Clear the current replay inputs to start fresh
            if (mgr->currentReplay) {
                mgr->currentReplay->inputs.clear();
            } else {
                mgr->createNewReplay(m_level);
            }
        }
        
        // Reset noclip accuracy tracking on level reset
        mgr->noclipDeaths = 0;
        mgr->noclipTotalFrames = 0;
        
        // Update seed for consistent RNG (pass true for restart)
        updateSeed(true);

        PlayLayer::resetLevel();

        // Setup the new recording from the start
        if (mgr->state == RECORD && mgr->currentReplay) {
            mgr->currentReplay->levelInfo.id = m_level->m_levelID;
            mgr->currentReplay->addInput(m_gameState.m_currentProgress, static_cast<int>(PlayerButton::Jump), false, false);
            m_player1->m_isDashing = false;

            if (m_gameState.m_isDualMode && m_levelSettings->m_twoPlayerMode) {
                mgr->currentReplay->addInput(m_gameState.m_currentProgress, static_cast<int>(PlayerButton::Jump), false, false);
                m_player2->m_isDashing = false;
            }
        }
    }

    void levelComplete() {
        ToastyReplay* mgr = ToastyReplay::get();
        mgr->safeMode = false;
        if (mgr->state == RECORD && mgr->currentReplay) {
            bool hasClicks = false;
            for (auto& input : mgr->currentReplay->inputs) {
                if (input.down) {
                    hasClicks = true;
                    break;
                }
            }

            if (hasClicks) {
                mgr->currentReplay->name = fmt::format("{} - 100%", mgr->currentReplay->levelInfo.name);
                if (mgr->lastUnsavedReplay) delete mgr->lastUnsavedReplay;
                mgr->lastUnsavedReplay = mgr->currentReplay;
                mgr->currentReplay = nullptr;
            } else {
                delete mgr->currentReplay;
                mgr->currentReplay = nullptr;
            }
            mgr->state = NONE;  // Reset recording state to prevent UI glitch
        }
        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        ToastyReplay* mgr = ToastyReplay::get();
        
        if (mgr->noclip) {
            // Track noclip deaths for accuracy
            mgr->noclipDeaths++;
            
            // Check if accuracy limit is enabled and if we're below the threshold
            if (mgr->noclipAccuracyEnabled && mgr->noclipAccuracyLimit > 0.0f && mgr->noclipTotalFrames > 0) {
                float accuracy = 100.0f * (1.0f - (float)mgr->noclipDeaths / (float)mgr->noclipTotalFrames);
                if (accuracy < mgr->noclipAccuracyLimit) {
                    // Accuracy too low, actually kill the player
                    // Reset accuracy for next attempt
                    mgr->noclipDeaths = 0;
                    mgr->noclipTotalFrames = 0;
                    
                    mgr->noclip = false; // Temporarily disable noclip
                    PlayLayer::destroyPlayer(p0, p1);
                    mgr->noclip = true; // Re-enable noclip
                    return;
                }
            }
            return;
        }
        
        // Reset noclip accuracy when player actually dies (without noclip)
        mgr->noclipDeaths = 0;
        mgr->noclipTotalFrames = 0;
        
        if (mgr->state == RECORD && mgr->currentReplay) {
            bool hasClicks = false;
            for (auto& input : mgr->currentReplay->inputs) {
                if (input.down) {
                    hasClicks = true;
                    break;
                }
            }

            if (hasClicks) {
                int percent = this->getCurrentPercentInt();
                mgr->currentReplay->name = fmt::format("{} - {}%", mgr->currentReplay->levelInfo.name, percent);
                if (mgr->lastUnsavedReplay) delete mgr->lastUnsavedReplay;
                mgr->lastUnsavedReplay = mgr->currentReplay;
                mgr->currentReplay = nullptr;
                mgr->state = NONE;
            }
            // If no clicks, do nothing - let resetLevel handle clearing inputs
            // Recording state stays active, replay stays active
        }

        PlayLayer::destroyPlayer(p0, p1);
    }

};

class $modify(MenuLayer) {
    void onExit() {
        ToastyReplay::get()->clearUnsavedReplays();
        MenuLayer::onExit();
    }
};

class $modify(LevelSelectLayer) {
    void onExit() {
        ToastyReplay::get()->clearUnsavedReplays();
        LevelSelectLayer::onExit();
    }
};
