#include "ToastyReplay.hpp"
#include "replay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CheckpointObject.hpp>
#include <random>
using namespace geode::prelude;

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

    if (isRestart && mgr->state == RECORD) {
#ifdef GEODE_IS_WINDOWS
        mgr->macroSeed = *(uintptr_t*)((char*)geode::base::get() + seedAddr);
#else
        mgr->macroSeed = 0;
#endif
    }
}

static void clearInputState() {
    auto* mgr = ToastyReplay::get();

    mgr->ignoreFrame = -1;
    mgr->ignoreJumpButton = -1;

    mgr->delayedFrameReleaseMain[0] = -1;
    mgr->delayedFrameReleaseMain[1] = -1;

    mgr->delayedFrameInput[0] = -1;
    mgr->delayedFrameInput[1] = -1;

    mgr->addSideHoldingMembers[0] = false;
    mgr->addSideHoldingMembers[1] = false;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++)
            mgr->delayedFrameRelease[x][y] = -1;
    }
}

static void trimReplayData(int frame) {
    auto* mgr = ToastyReplay::get();
    if (!mgr->currentReplay) return;

    auto& inputs = mgr->currentReplay->inputs;
    if (!inputs.empty()) {
        while (!inputs.empty() && inputs.back().frame >= frame) {
            inputs.pop_back();
        }
    }

    auto& frameFixes = mgr->currentReplay->frameFixes;
    if (!frameFixes.empty()) {
        while (!frameFixes.empty() && frameFixes.back().frame >= frame) {
            frameFixes.pop_back();
        }
    }
}

class $modify(CheckpointObject) {
#ifdef GEODE_IS_WINDOWS
    bool init() {
        bool ret = CheckpointObject::init();
        CheckpointObject* cp = this;
#else
    static CheckpointObject* create() {
        CheckpointObject* ret = CheckpointObject::create();
        CheckpointObject* cp = ret;
#endif

        if (!cp) return ret;

        auto* mgr = ToastyReplay::get();
        PlayLayer* pl = PlayLayer::get();

        if (!pl || mgr->state != RECORD) return ret;

        int frame = static_cast<int>(pl->m_gameState.m_levelTime * mgr->tps);
        frame++;

        mgr->checkpoints[cp] = {
            frame,
            PlayerStateData(),
            PlayerStateData(),
#ifdef GEODE_IS_WINDOWS
            *(uintptr_t*)((char*)geode::base::get() + seedAddr),
#else
            0,
#endif
            mgr->previousFrame
        };

        return ret;
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
        PlayLayer::resetLevel();

        ToastyReplay* mgr = ToastyReplay::get();

        bool inPractice = m_isPracticeMode;
        int frame = m_gameState.m_currentProgress;

        updateSeed(true);

        mgr->safeMode = false;
        mgr->playbackIndex = 0;
        mgr->frameFixIndex = 0;
        mgr->restart = false;

        if (mgr->state == RECORD && mgr->currentReplay) {
            if (!inPractice) {
                mgr->currentReplay->inputs.clear();
                mgr->currentReplay->frameFixes.clear();
                mgr->checkpoints.clear();

                if (m_player1 && m_player2) {
                    clearInputState();

                    m_player1->m_holdingRight = false;
                    m_player1->m_holdingLeft = false;
                    m_player2->m_holdingRight = false;
                    m_player2->m_holdingLeft = false;

                    if (m_levelSettings->m_platformerMode) {
                        m_player1->m_holdingButtons[2] = false;
                        m_player1->m_holdingButtons[3] = false;
                        m_player2->m_holdingButtons[2] = false;
                        m_player2->m_holdingButtons[3] = false;
                    }
                }
            } else if (frame <= 1 || mgr->checkpoints.empty()) {
                mgr->currentReplay->inputs.clear();
                mgr->currentReplay->frameFixes.clear();
                mgr->checkpoints.clear();

                if (m_player1 && m_player2) {
                    clearInputState();

                    m_player1->m_holdingRight = false;
                    m_player1->m_holdingLeft = false;
                    m_player2->m_holdingRight = false;
                    m_player2->m_holdingLeft = false;

                    if (m_levelSettings->m_platformerMode) {
                        m_player1->m_holdingButtons[2] = false;
                        m_player1->m_holdingButtons[3] = false;
                        m_player2->m_holdingButtons[2] = false;
                        m_player2->m_holdingButtons[3] = false;
                    }
                }
            }
        }
    }

    void loadFromCheckpoint(CheckpointObject* cp) {
        if (!cp) return PlayLayer::loadFromCheckpoint(cp);

        auto* mgr = ToastyReplay::get();

        if (mgr->state == RECORD) {
            if (!mgr->checkpoints.contains(cp)) return PlayLayer::loadFromCheckpoint(cp);

            int frame = mgr->checkpoints[cp].frame;

            clearInputState();

            trimReplayData(frame);

            mgr->ignoreJumpButton = frame + 1;
            mgr->previousFrame = mgr->checkpoints[cp].previousFrame;

#ifdef GEODE_IS_WINDOWS
            if (mgr->seedEnabled) {
                uintptr_t seed = mgr->checkpoints[cp].seed;
                *(uintptr_t*)((char*)geode::base::get() + seedAddr) = seed;
            }
#endif

            PlayLayer::loadFromCheckpoint(cp);

            return;
        }

        PlayLayer::loadFromCheckpoint(cp);
    }

    void levelComplete() {
        ToastyReplay* mgr = ToastyReplay::get();
        mgr->safeMode = false;
        
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
