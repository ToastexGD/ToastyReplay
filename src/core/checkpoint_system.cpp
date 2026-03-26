#include "ToastyReplay.hpp"
#include "core/checkpoint_handler.hpp"
#include "hacks/physicsbypass.hpp"
#include "hacks/trajectory.hpp"

#include <Geode/modify/CheckpointObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fmt/format.h>
#include <random>

using namespace geode::prelude;

void refreshRngState(bool isLevelStart) {
    auto* engine = ReplayEngine::get();
    auto* playLayer = PlayLayer::get();
    if (!playLayer) {
        return;
    }

    if (engine->rngLocked) {
        uint64_t computedSeed = 0;
        if (!playLayer->m_player1->m_isDead) {
            std::mt19937 generator(engine->rngSeedVal + playLayer->m_gameState.m_currentProgress);
            std::uniform_int_distribution<uint64_t> distribution(10000, 999999999);
            computedSeed = distribution(generator);
        } else {
            std::random_device device;
            std::mt19937 generator(device());
            std::uniform_int_distribution<uint64_t> distribution(1000, 999999999);
            computedSeed = distribution(generator);
        }

        GameToolbox::fast_srand(computedSeed);
    }

    if (isLevelStart && engine->engineMode == MODE_CAPTURE) {
        engine->capturedRngState = GameToolbox::getfast_srand();
    }
}

void ReplayEngine::resetDeferredInputState() {
    skipTickIndex = -1;
    skipActionTick = -1;
    deferredReleaseA[0] = -1;
    deferredReleaseA[1] = -1;
    deferredInputTick[0] = -1;
    deferredInputTick[1] = -1;
    lateralInputPending[0] = false;
    lateralInputPending[1] = false;
    for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
        for (int holdIndex = 0; holdIndex < 2; ++holdIndex) {
            deferredReleaseB[playerIndex][holdIndex] = -1;
        }
    }
}

void ReplayEngine::truncateRecordedDataAfter(int tick) {
    if (ttrMode && activeTTR) {
        activeTTR->truncateAfter(tick);
        return;
    }

    if (activeMacro) {
        activeMacro->truncateAfter(tick);
    }
}

void ReplayEngine::prepareCaptureRestore(int tick) {
    resetDeferredInputState();
    resetTimingTracking();
    truncateRecordedDataAfter(tick);
    skipActionTick = tick + 1;
    tickAccumulator = 0.0f;
}

void ReplayEngine::restoreLockedRngState(uintptr_t rngState) {
    if (rngLocked) {
        GameToolbox::fast_srand(rngState);
    }
}

void ReplayEngine::pruneStoredRestorePoints(PlayLayer* playLayer, int restoredTick) {
    if (!playLayer || !playLayer->m_checkpointArray) {
        storedRestorePoints.clear();
        return;
    }

    while (playLayer->m_checkpointArray->count() > 0) {
        auto* checkpoint = static_cast<CheckpointObject*>(playLayer->m_checkpointArray->lastObject());
        auto it = storedRestorePoints.find(checkpoint);
        if (it == storedRestorePoints.end()) {
            playLayer->removeCheckpoint(false);
            continue;
        }

        if (it->second.tick <= restoredTick) {
            break;
        }

        storedRestorePoints.erase(it);
        playLayer->removeCheckpoint(false);
    }

    std::unordered_set<CheckpointObject*> liveCheckpoints;
    liveCheckpoints.reserve(static_cast<size_t>(playLayer->m_checkpointArray->count()));
    for (unsigned int index = 0; index < playLayer->m_checkpointArray->count(); ++index) {
        auto* checkpoint = static_cast<CheckpointObject*>(playLayer->m_checkpointArray->objectAtIndex(index));
        liveCheckpoints.insert(checkpoint);
    }

    for (auto it = storedRestorePoints.begin(); it != storedRestorePoints.end();) {
        if (!liveCheckpoints.contains(it->first) || it->second.tick > restoredTick) {
            it = storedRestorePoints.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {
    static int computeStartPosOffset(PlayLayer* playLayer, std::vector<PlaybackAnchor> const& anchors) {
        if (!playLayer->m_startPosObject || anchors.empty()) {
            return 0;
        }

        float startX = playLayer->m_startPosObject->getPositionX();
        float startY = playLayer->m_startPosObject->getPositionY();

        constexpr float threshold = 50.0f;
        for (auto const& anchor : anchors) {
            float dx = anchor.player1.motion.position.x - startX;
            float dy = anchor.player1.motion.position.y - startY;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < threshold) {
                return anchor.tick;
            }
        }

        int bestTick = 0;
        float bestDistance = FLT_MAX;
        for (auto const& anchor : anchors) {
            float dx = std::abs(anchor.player1.motion.position.x - startX);
            if (dx < bestDistance) {
                bestDistance = dx;
                bestTick = anchor.tick;
            }
        }
        return bestTick;
    }

    static size_t findFirstInputAtTick(std::vector<MacroAction>& inputs, int tick) {
        auto it = std::lower_bound(inputs.begin(), inputs.end(), tick, [](MacroAction const& action, int targetTick) {
            return static_cast<int>(action.frame) < targetTick;
        });
        return static_cast<size_t>(std::distance(inputs.begin(), it));
    }

    static size_t findFirstTTRInputAtTick(std::vector<TTRInput>& inputs, int tick) {
        auto it = std::lower_bound(inputs.begin(), inputs.end(), tick, [](TTRInput const& input, int targetTick) {
            return input.tick < targetTick;
        });
        return static_cast<size_t>(std::distance(inputs.begin(), it));
    }

    static size_t findFirstAnchorAtTick(std::vector<PlaybackAnchor> const& anchors, int tick) {
        auto it = std::lower_bound(anchors.begin(), anchors.end(), tick, [](PlaybackAnchor const& anchor, int targetTick) {
            return anchor.tick < targetTick;
        });
        return static_cast<size_t>(std::distance(anchors.begin(), it));
    }
}

class $modify(RestorePointHandler, CheckpointObject) {
#ifdef GEODE_IS_WINDOWS
    bool init() {
        bool result = CheckpointObject::init();
        CheckpointObject* checkpoint = this;
#else
    static CheckpointObject* create() {
        CheckpointObject* result = CheckpointObject::create();
        CheckpointObject* checkpoint = result;
#endif

        if (!checkpoint) {
            return result;
        }

        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();
        if (!playLayer || engine->engineMode != MODE_CAPTURE) {
            return result;
        }

        int tick = static_cast<int>(playLayer->m_gameState.m_levelTime * engine->runtimeTickRate());
        tick++;

        engine->storedRestorePoints[checkpoint] = CheckpointStateManager::capture(
            tick,
            engine->lastTickIndex,
            playLayer,
            engine->captureRngState()
        );

        return result;
    }
};

class $modify(MacroPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        auto* engine = ReplayEngine::get();
        if (engine->engineMode == MODE_CAPTURE) {
            if (engine->ttrMode) {
                engine->initializeTTRMacro(level);
            } else {
                engine->initializeMacro(level);
            }
        }
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void resetLevel() {
        auto* engine = ReplayEngine::get();
        engine->captureIgnored = true;
        resetSimulationTimingState();
        PlayLayer::resetLevel();
        resetSimulationTimingState();
        engine->captureIgnored = false;

        bool inPractice = m_isPracticeMode;
        int tick = m_gameState.m_currentProgress;

        refreshRngState(true);

        engine->executeIndex = 0;
        engine->playbackAnchorIndex = 0;
        engine->levelRestarting = false;
        engine->resetTimingTracking();
        engine->respawnTickIndex = -1;
        engine->tickAccumulator = 0.0f;
        engine->armPendingPlaybackStart(this);

        auto clearPlayerHolds = [&]() {
            if (!m_player1 || !m_player2) {
                return;
            }

            engine->resetDeferredInputState();
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
        };

        if (engine->engineMode == MODE_EXECUTE) {
            auto const* anchors = engine->activeAnchors();
            bool recordedFromStartPos = engine->ttrMode
                ? (engine->activeTTR && engine->activeTTR->recordedFromStartPos)
                : (engine->activeMacro && engine->activeMacro->recordedFromStartPos);

            if (m_startPosObject && anchors && !anchors->empty()) {
                engine->tickOffset = computeStartPosOffset(this, *anchors);
                engine->startPosActive = true;
                engine->playbackAnchorIndex = findFirstAnchorAtTick(*anchors, engine->tickOffset);
                if (engine->ttrMode && engine->activeTTR) {
                    engine->executeIndex = findFirstTTRInputAtTick(engine->activeTTR->inputs, engine->tickOffset);
                } else if (engine->activeMacro) {
                    engine->executeIndex = findFirstInputAtTick(engine->activeMacro->inputs, engine->tickOffset);
                }
                engine->startPosWarning.clear();
            } else if (!m_startPosObject && recordedFromStartPos) {
                engine->startPosWarning = "Macro was recorded from a start position. Use the same or later start position.";
                engine->haltExecution();
            } else if (m_startPosObject && (!anchors || anchors->empty())) {
                engine->startPosWarning = "Macro lacks anchor data for start position playback. Re-record to enable.";
                engine->tickOffset = 0;
                engine->startPosActive = false;
            } else {
                engine->tickOffset = 0;
                engine->startPosActive = false;
                engine->startPosWarning.clear();
            }
        }

        if (engine->engineMode == MODE_CAPTURE && engine->ttrMode && engine->activeTTR) {
            if (!inPractice || tick <= 1 || engine->storedRestorePoints.empty()) {
                engine->activeTTR->inputs.clear();
                engine->activeTTR->anchors.clear();
                engine->storedRestorePoints.clear();
                clearPlayerHolds();
            }
        } else if (engine->engineMode == MODE_CAPTURE && !engine->ttrMode && engine->activeMacro) {
            if (!inPractice || tick <= 1 || engine->storedRestorePoints.empty()) {
                engine->activeMacro->inputs.clear();
                engine->activeMacro->anchors.clear();
                engine->storedRestorePoints.clear();
                clearPlayerHolds();
            }
        }
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        if (!checkpoint) {
            resetSimulationTimingState();
            return PlayLayer::loadFromCheckpoint(checkpoint);
        }

        auto* engine = ReplayEngine::get();
        resetSimulationTimingState();
        engine->resetTimingTracking();
        engine->respawnTickIndex = -1;
        engine->tickAccumulator = 0.0f;
        if (engine->engineMode == MODE_CAPTURE) {
            auto it = engine->storedRestorePoints.find(checkpoint);
            if (it == engine->storedRestorePoints.end()) {
                PlayLayer::loadFromCheckpoint(checkpoint);
                resetSimulationTimingState();
                return;
            }

            engine->prepareCaptureRestore(it->second.tick);
            engine->lastTickIndex = it->second.priorTick;
            engine->restoreLockedRngState(it->second.rng.fastRandState);

            PlayLayer::loadFromCheckpoint(checkpoint);
            CheckpointStateManager::restore(this, it->second);
            resetSimulationTimingState();
            return;
        }

        PlayLayer::loadFromCheckpoint(checkpoint);
        resetSimulationTimingState();
    }

    void delayedResetLevel() {
        resetSimulationTimingState();
        PlayLayer::delayedResetLevel();
        resetSimulationTimingState();
    }

    void levelComplete() {
        auto* engine = ReplayEngine::get();
        if (engine->engineMode == MODE_CAPTURE) {
            if (engine->ttrMode && engine->activeTTR) {
                engine->activeTTR->name = fmt::format("{} - 100%", engine->activeTTR->levelName);
            } else if (engine->activeMacro) {
                engine->activeMacro->name = fmt::format("{} - 100%", engine->activeMacro->levelInfo.name);
            }
        }

        PlayLayer::levelComplete();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        auto* engine = ReplayEngine::get();
        if (engine->collisionBypass) {
            if (object == m_anticheatSpike) {
                PlayLayer::destroyPlayer(player, object);
                return;
            }

            if (!engine->noclipDeathBlocked) {
                engine->bypassedCollisions++;
                if (engine->noclipDeathFlash) {
                    auto* scene = CCDirector::sharedDirector()->getRunningScene();
                    if (scene) {
                        auto* overlay = static_cast<CCLayerColor*>(scene->getChildByTag(39182));
                        if (!overlay) {
                            auto winSize = CCDirector::sharedDirector()->getWinSize();
                            overlay = CCLayerColor::create(ccc4(
                                static_cast<int>(engine->noclipDeathColorR * 255),
                                static_cast<int>(engine->noclipDeathColorG * 255),
                                static_cast<int>(engine->noclipDeathColorB * 255),
                                0
                            ));
                            overlay->setContentSize(winSize);
                            overlay->setTag(39182);
                            overlay->setZOrder(9999);
                            overlay->setPosition(0, 0);
                            scene->addChild(overlay);
                        }
                        overlay->setColor(ccc3(
                            static_cast<int>(engine->noclipDeathColorR * 255),
                            static_cast<int>(engine->noclipDeathColorG * 255),
                            static_cast<int>(engine->noclipDeathColorB * 255)
                        ));
                        overlay->stopAllActions();
                        overlay->setOpacity(100);
                        overlay->runAction(CCFadeTo::create(0.25f, 0));
                    }
                }
            }
            engine->noclipDeathBlocked = true;

            if (engine->collisionLimitActive && engine->collisionThreshold > 0.0f && engine->totalTickCount > 0) {
                float hitRate = 100.0f * (1.0f - static_cast<float>(engine->bypassedCollisions) / static_cast<float>(engine->totalTickCount));
                if (hitRate < engine->collisionThreshold) {
                    engine->bypassedCollisions = 0;
                    engine->totalTickCount = 0;

                    engine->collisionBypass = false;
                    engine->noclipDeathBlocked = false;
                    PlayLayer::destroyPlayer(player, object);
                    engine->collisionBypass = true;
                    return;
                }
            }
            return;
        }

        engine->bypassedCollisions = 0;
        engine->totalTickCount = 0;
        PlayLayer::destroyPlayer(player, object);
    }

    void onQuit() {
        auto* engine = ReplayEngine::get();
        engine->pendingPlaybackStart = false;
        if (engine->engineMode == MODE_CAPTURE) {
            if (engine->ttrMode && engine->activeTTR) {
                delete engine->activeTTR;
                engine->activeTTR = nullptr;
            } else if (engine->activeMacro) {
                delete engine->activeMacro;
                engine->activeMacro = nullptr;
            }
            engine->engineMode = MODE_DISABLED;
        }

        PlayLayer::onQuit();
    }
};

class $modify(NoclipBaseLayer, GJBaseGameLayer) {
    int checkCollisions(PlayerObject* player, float dt, bool ignoreDamage) {
        if (!PlayLayer::get()) {
            return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
        }
        auto* engine = ReplayEngine::get();
        if (engine->collisionBypass) {
            engine->noclipDeathBlocked = false;
        }
        return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
    }
};
