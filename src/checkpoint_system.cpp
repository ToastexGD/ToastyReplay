#include "ToastyReplay.hpp"
#include "replay.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CheckpointObject.hpp>
#include <random>
using namespace geode::prelude;

void refreshRngState(bool isLevelStart = false) {
    ReplayEngine* engine = ReplayEngine::get();
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return;

    if (engine->rngLocked) {
        uint64_t computedSeed;

        if (!pl->m_player1->m_isDead) {
            std::mt19937 gen(engine->rngSeedVal + pl->m_gameState.m_currentProgress);
            std::uniform_int_distribution<uint64_t> dist(10000, 999999999);
            computedSeed = dist(gen);
        } else {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uint64_t> dist(1000, 999999999);
            computedSeed = dist(gen);
        }

        GameToolbox::fast_srand(computedSeed);
    }

    if (isLevelStart && engine->engineMode == MODE_CAPTURE) {
        engine->capturedRngState = GameToolbox::getfast_srand();
    }
}

static void resetInputTracking() {
    auto* engine = ReplayEngine::get();

    engine->skipTickIndex = -1;
    engine->skipActionTick = -1;

    engine->deferredReleaseA[0] = -1;
    engine->deferredReleaseA[1] = -1;

    engine->deferredInputTick[0] = -1;
    engine->deferredInputTick[1] = -1;

    engine->lateralInputPending[0] = false;
    engine->lateralInputPending[1] = false;
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++)
            engine->deferredReleaseB[x][y] = -1;
    }
}

static void truncateMacroData(int tick) {
    auto* engine = ReplayEngine::get();
    if (!engine->activeMacro) return;

    auto& actions = engine->activeMacro->inputs;
    if (!actions.empty()) {
        while (!actions.empty() && actions.back().tick >= tick) {
            actions.pop_back();
        }
    }

    auto& corrections = engine->activeMacro->corrections;
    if (!corrections.empty()) {
        while (!corrections.empty() && corrections.back().tick >= tick) {
            corrections.pop_back();
        }
    }
}

class $modify(RestorePointHandler, CheckpointObject) {
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

        auto* engine = ReplayEngine::get();
        PlayLayer* pl = PlayLayer::get();

        if (!pl || engine->engineMode != MODE_CAPTURE) return ret;

        int tick = static_cast<int>(pl->m_gameState.m_levelTime * engine->tickRate);
        tick++;

        engine->storedRestorePoints[cp] = {
            tick,
            PhysicsSnapshot(),
            PhysicsSnapshot(),
            GameToolbox::getfast_srand(),
            engine->lastTickIndex
        };

        return ret;
    }
};

class $modify(MacroPlayLayer, PlayLayer) {
    bool init(GJGameLevel* lvl, bool useReplay, bool dontCreateObjects) {
        ReplayEngine* engine = ReplayEngine::get();

        if (engine->engineMode == MODE_CAPTURE) {
            engine->initializeMacro(lvl);
        }

        return PlayLayer::init(lvl, useReplay, dontCreateObjects);
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        ReplayEngine* engine = ReplayEngine::get();

        bool inPractice = m_isPracticeMode;
        int tick = m_gameState.m_currentProgress;

        refreshRngState(true);

        engine->executeIndex = 0;
        engine->correctionIndex = 0;
        engine->levelRestarting = false;

        if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
            if (!inPractice) {
                engine->activeMacro->inputs.clear();
                engine->activeMacro->corrections.clear();
                engine->storedRestorePoints.clear();

                if (m_player1 && m_player2) {
                    resetInputTracking();

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
            } else if (tick <= 1 || engine->storedRestorePoints.empty()) {
                engine->activeMacro->inputs.clear();
                engine->activeMacro->corrections.clear();
                engine->storedRestorePoints.clear();

                if (m_player1 && m_player2) {
                    resetInputTracking();

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

        auto* engine = ReplayEngine::get();

        if (engine->engineMode == MODE_CAPTURE) {
            if (!engine->storedRestorePoints.contains(cp)) return PlayLayer::loadFromCheckpoint(cp);

            int tick = engine->storedRestorePoints[cp].tick;

            resetInputTracking();

            truncateMacroData(tick);

            engine->skipActionTick = tick + 1;
            engine->lastTickIndex = engine->storedRestorePoints[cp].priorTick;

            if (engine->rngLocked) {
                GameToolbox::fast_srand(engine->storedRestorePoints[cp].rngState);
            }

            PlayLayer::loadFromCheckpoint(cp);

            return;
        }

        PlayLayer::loadFromCheckpoint(cp);
    }

    void levelComplete() {
        ReplayEngine* engine = ReplayEngine::get();

        if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
            engine->activeMacro->name = fmt::format("{} - 100%", engine->activeMacro->levelInfo.name);
        }

        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* p0, GameObject* p1) {
        ReplayEngine* engine = ReplayEngine::get();

        if (engine->collisionBypass) {
            engine->bypassedCollisions++;
            engine->noclipDeathBlocked = true;

            if (engine->collisionLimitActive && engine->collisionThreshold > 0.0f && engine->totalTickCount > 0) {
                float hitRate = 100.0f * (1.0f - (float)engine->bypassedCollisions / (float)engine->totalTickCount);
                if (hitRate < engine->collisionThreshold) {
                    engine->bypassedCollisions = 0;
                    engine->totalTickCount = 0;

                    engine->collisionBypass = false;
                    engine->noclipDeathBlocked = false;
                    PlayLayer::destroyPlayer(p0, p1);
                    engine->collisionBypass = true;
                    return;
                }
            }
            return;
        }

        engine->bypassedCollisions = 0;
        engine->totalTickCount = 0;

        PlayLayer::destroyPlayer(p0, p1);
    }

    void onQuit() {
        ReplayEngine* engine = ReplayEngine::get();

        if (engine->engineMode == MODE_CAPTURE && engine->activeMacro) {
            delete engine->activeMacro;
            engine->activeMacro = nullptr;
            engine->engineMode = MODE_DISABLED;
        }

        PlayLayer::onQuit();
    }
};

class $modify(NoclipBaseLayer, GJBaseGameLayer) {
    int checkCollisions(PlayerObject* player, float dt, bool ignoreDamage) {
        auto* engine = ReplayEngine::get();

        if (engine->collisionBypass) {
            engine->noclipDeathBlocked = false;
            int result = GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
            engine->noclipDeathBlocked = false;
            return result;
        }

        return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
    }
};
