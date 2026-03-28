#include "ToastyReplay.hpp"
#include "core/checkpoint_handler.hpp"
#include "core/input_timing.hpp"
#include "hacks/autoclicker.hpp"
#include "gui/gui.hpp"
#include "gui/frame_editor.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

void refreshRngState(bool isRestart = false);

namespace {
    constexpr double kQueuedCommandMatchTolerance = 0.001;

    static int computeCurrentTick() {
        auto* playLayer = PlayLayer::get();
        if (!playLayer) return 0;

        auto* engine = ReplayEngine::get();
        int tick = static_cast<int>(playLayer->m_gameState.m_levelTime * engine->runtimeTickRate());
        return std::max(0, tick + 1);
    }

    static bool flipControls() {
        auto* playLayer = PlayLayer::get();
        if (!playLayer) return GameManager::get()->getGameVariable("0010");

        if (playLayer->m_levelSettings->m_platformerMode) return false;
        return GameManager::get()->getGameVariable("0010");
    }

    static bool hasDualPlaybackContext(PlayLayer* playLayer) {
        return playLayer && (playLayer->m_gameState.m_isDualMode || playLayer->m_levelSettings->m_twoPlayerMode);
    }

    static int deferredSlotForPlayer(bool twoPlayerMode, bool player2) {
        return twoPlayerMode ? static_cast<int>(!player2) : 0;
    }

    static bool keepSecondPlayerSlot(GJBaseGameLayer* layer) {
        return layer && (layer->m_levelSettings->m_twoPlayerMode || layer->m_gameState.m_isDualMode);
    }

    static bool isTrackedHoldButton(int button) {
        return button >= 1 && button <= 3;
    }

    static bool matchesQueuedMacroCommand(PlayerButtonCommand const& command, ReplayEngine::QueuedMacroCommand const& queued) {
        if (static_cast<int>(command.m_button) != queued.button) return false;
        if (command.m_isPush != queued.down) return false;
        if (command.m_isPlayer2 != queued.player2) return false;
        return std::abs(command.m_timestamp - queued.timestamp) <= kQueuedCommandMatchTolerance;
    }
}

class $modify(MacroEngineBaseLayer, GJBaseGameLayer) {
    struct Fields {
        bool macroInput = false;
    };

    void dispatchImmediatePlaybackAction(int tick, int button, bool down, bool player2) {
        auto* engine = ReplayEngine::get();
        if (tick == engine->respawnTickIndex) {
            return;
        }

        if (flipControls()) {
            player2 = !player2;
        }

        m_fields->macroInput = true;
        GJBaseGameLayer::handleButton(down, button, player2);
        m_fields->macroInput = false;
        engine->triggerAudio(player2, button, down);
    }

    size_t queueCBFInputsBeforeStep(int tick, int effectiveTick, int stepDelta, float dt) {
        auto* engine = ReplayEngine::get();
        size_t queuedCount = 0;

        auto queueTimedInput = [&](int button, bool down, bool player2, float phase) {
            if (tick == engine->respawnTickIndex) {
                return;
            }

            if (flipControls()) {
                player2 = !player2;
            }

            float fractionalPhase = std::clamp(phase - static_cast<float>(stepDelta), 0.0f, 0.999f);
            double timestamp = m_timestamp + static_cast<double>(dt) * static_cast<double>(fractionalPhase);
            PlayerButtonCommand command = {};
            command.m_button = static_cast<PlayerButton>(button);
            command.m_isPush = down;
            command.m_isPlayer2 = player2;
            command.m_timestamp = timestamp;
            m_queuedButtons.push_back(command);
            engine->queueMacroCommand(button, down, player2, timestamp);
            ++queuedCount;
        };

        if (engine->ttrMode && engine->activeTTR) {
            auto& inputList = engine->activeTTR->inputs;
            while (engine->executeIndex < inputList.size()) {
                auto const& input = inputList[engine->executeIndex];
                if (input.tick > effectiveTick) {
                    break;
                }

                if (input.tick == effectiveTick && input.stepOffset >= static_cast<float>(stepDelta + 1)) {
                    break;
                }

                bool player2 = input.isPlayer2();
                if (input.tick < effectiveTick || input.stepOffset < static_cast<float>(stepDelta)) {
                    dispatchImmediatePlaybackAction(tick, input.actionType, input.isPressed(), player2);
                } else {
                    queueTimedInput(input.actionType, input.isPressed(), player2, input.stepOffset);
                }

                ++engine->executeIndex;
            }
            return queuedCount;
        }

        if (!engine->activeMacro) {
            return 0;
        }

        auto& inputList = engine->activeMacro->inputs;
        while (engine->executeIndex < inputList.size()) {
            auto const& input = inputList[engine->executeIndex];
            int inputTick = static_cast<int>(input.frame);
            if (inputTick > effectiveTick) {
                break;
            }

            if (inputTick == effectiveTick && input.stepOffset >= static_cast<float>(stepDelta + 1)) {
                break;
            }

            if (inputTick < effectiveTick || input.stepOffset < static_cast<float>(stepDelta)) {
                dispatchImmediatePlaybackAction(tick, input.button, input.down, input.player2);
            } else {
                queueTimedInput(input.button, input.down, input.player2, input.stepOffset);
            }

            ++engine->executeIndex;
        }
        return queuedCount;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();

        if (!playLayer) {
            return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        }

        refreshRngState();

        int tick = computeCurrentTick();
        AccuracyMode playbackAccuracy = engine->activeMacroAccuracyMode();
        bool timedPlayback = engine->engineMode == MODE_EXECUTE && usesTimedAccuracy(playbackAccuracy);
        bool newTick = tick != engine->lastTickIndex;

        if (newTick) {
            engine->tickStartStep = m_currentStep;
        }

        int stepDelta = std::max(0, m_currentStep - engine->tickStartStep);
        engine->beginStepTimingWindow(InputTiming::nowMicros(), static_cast<double>(dt) * 1'000'000.0, newTick);

        if (engine->engineMode != MODE_DISABLED) {
            if (tick > 2 && engine->initialRun && engine->hasMacro()) {
                engine->initialRun = false;
                if (!m_levelEndAnimationStarted) {
                    if (m_levelSettings->m_platformerMode) {
                        return playLayer->resetLevelFromStart();
                    }
                    return playLayer->resetLevel();
                }
            }

            if (engine->lastTickIndex == tick && tick != 0 && engine->hasMacro()) {
                bool repeatedTimedSlice = timedPlayback && engine->lastStepDelta == stepDelta;
                if (!timedPlayback || repeatedTimedSlice)
                    return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            }
        }

        if (engine->engineMode == MODE_EXECUTE && playbackAccuracy == AccuracyMode::CBF) {
            queueCBFInputsBeforeStep(tick, tick + engine->tickOffset, stepDelta, dt);
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (engine->engineMode == MODE_DISABLED) {
            return;
        }

        engine->lastTickIndex = tick;
        engine->lastStepDelta = stepDelta;

        if (newTick) {
            if (engine->hasMacro() && engine->levelRestarting && !m_levelEndAnimationStarted) {
                if (m_levelSettings->m_platformerMode) {
                    return playLayer->resetLevelFromStart();
                }
                return playLayer->resetLevel();
            }

            if (engine->engineMode == MODE_CAPTURE) {
                captureTick(tick);
            }
        }

        if (engine->engineMode == MODE_EXECUTE) {
            executeTick(tick, stepDelta);
        }
    }

    void dispatchDeferredActions(int tick) {
        auto* engine = ReplayEngine::get();
        bool twoPlayers = m_levelSettings->m_twoPlayerMode;

        if (engine->skipTickIndex != -1 && engine->skipTickIndex < tick) {
            engine->skipTickIndex = -1;
        }

        for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
            if (engine->deferredInputTick[playerIndex] == tick) {
                engine->deferredInputTick[playerIndex] = -1;
                bool player2 = twoPlayers ? (playerIndex == 0) : false;
                GJBaseGameLayer::handleButton(true, 1, player2);
            }
        }

        if (engine->skipActionTick != -1 && tick > engine->skipActionTick) {
            engine->skipActionTick = -1;
        }

        for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
            if (engine->deferredReleaseA[playerIndex] == tick) {
                engine->deferredReleaseA[playerIndex] = -1;
                bool player2 = twoPlayers ? (playerIndex == 0) : false;
                GJBaseGameLayer::handleButton(false, 1, player2);
            }

            if (!m_levelSettings->m_platformerMode) {
                continue;
            }

            for (int holdIndex = 0; holdIndex < 2; ++holdIndex) {
                if (engine->deferredReleaseB[playerIndex][holdIndex] == tick) {
                    engine->deferredReleaseB[playerIndex][holdIndex] = -1;
                    bool player2 = twoPlayers ? (playerIndex == 0) : false;
                    GJBaseGameLayer::handleButton(false, holdIndex == 0 ? 2 : 3, player2);
                }
            }
        }
    }

    void captureTick(int tick) {
        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();
        if (!engine->hasMacro() || !playLayer) {
            return;
        }

        dispatchDeferredActions(tick);

        if (engine->ttrMode) {
            if (!engine->activeTTR || engine->activeTTR->inputs.empty()) {
                return;
            }

            engine->activeTTR->recordAnchor(
                tick,
                m_player1,
                m_player2,
                m_levelSettings->m_platformerMode,
                hasDualPlaybackContext(playLayer)
            );
            return;
        }

        if (!engine->anchorReconciliation || !engine->activeMacro || engine->activeMacro->inputs.empty()) {
            return;
        }

        engine->activeMacro->recordAnchor(
            AnchorReconciler::captureAnchor(
                tick,
                playLayer,
                m_player1,
                m_player2,
                engine->captureRngState()
            )
        );
    }

    void dispatchTTRInputs(int tick, int effectiveTick, int stepDelta) {
        auto* engine = ReplayEngine::get();
        auto& inputList = engine->activeTTR->inputs;
        AccuracyMode accuracyMode = engine->activeTTR->accuracyMode;

        while (engine->executeIndex < inputList.size()) {
            auto const& input = inputList[engine->executeIndex];
            if (input.tick > effectiveTick) {
                break;
            }

            if (accuracyMode == AccuracyMode::CBS &&
                input.tick == effectiveTick &&
                static_cast<int>(input.stepOffset) > stepDelta) {
                break;
            }

            bool player2 = input.isPlayer2();
            dispatchImmediatePlaybackAction(tick, input.actionType, input.isPressed(), player2);
            ++engine->executeIndex;
        }
    }

    void dispatchGDRInputs(int tick, int effectiveTick, int stepDelta) {
        auto* engine = ReplayEngine::get();
        auto& inputList = engine->activeMacro->inputs;
        AccuracyMode accuracyMode = engine->activeMacro->accuracyMode;

        while (engine->executeIndex < inputList.size()) {
            auto const& input = inputList[engine->executeIndex];
            int inputTick = static_cast<int>(input.frame);
            if (inputTick > effectiveTick) {
                break;
            }

            if (accuracyMode == AccuracyMode::CBS &&
                inputTick == effectiveTick &&
                static_cast<int>(input.stepOffset) > stepDelta) {
                break;
            }

            dispatchImmediatePlaybackAction(tick, input.button, input.down, input.player2);
            ++engine->executeIndex;
        }
    }

    void reconcileAnchors(int effectiveTick) {
        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();
        auto* anchors = engine->activeAnchors();
        if (!engine->anchorReconciliation || !playLayer || !anchors) {
            return;
        }

        while (engine->playbackAnchorIndex < anchors->size()) {
            auto const& anchor = anchors->at(engine->playbackAnchorIndex);
            if (anchor.tick > effectiveTick) {
                break;
            }

            bool forceBoundarySync = effectiveTick == anchor.tick;
            AnchorReconciler::reconcile(playLayer, m_player1, m_player2, anchor, forceBoundarySync);
            ++engine->playbackAnchorIndex;
        }
    }

    void executeTick(int tick, int stepDelta) {
        auto* engine = ReplayEngine::get();
        if (m_levelEndAnimationStarted || !engine->hasMacro()) {
            return;
        }

        if (m_player1->m_isDead) {
            m_player1->releaseAllButtons();
            m_player2->releaseAllButtons();
            return;
        }

        int effectiveTick = tick + engine->tickOffset;

        if (engine->activeMacroAccuracyMode() != AccuracyMode::CBF) {
            m_fields->macroInput = true;
            if (engine->ttrMode && engine->activeTTR) {
                dispatchTTRInputs(tick, effectiveTick, stepDelta);
            } else if (engine->activeMacro) {
                dispatchGDRInputs(tick, effectiveTick, stepDelta);
            }
            m_fields->macroInput = false;
        }

        engine->respawnTickIndex = -1;
        reconcileAnchors(effectiveTick);
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        auto* engine = ReplayEngine::get();
        bool executingCBF = engine->engineMode == MODE_EXECUTE &&
            engine->activeMacroAccuracyMode() == AccuracyMode::CBF;
        std::deque<ReplayEngine::QueuedMacroCommand> queuedSnapshot;

        if (executingCBF && engine->userInputIgnored) {
            gd::vector<PlayerButtonCommand> filteredCommands;
            filteredCommands.reserve(m_queuedButtons.size());
            auto pendingForFilter = engine->queuedMacroCommands;

            for (auto const& command : m_queuedButtons) {
                auto it = std::find_if(
                    pendingForFilter.begin(),
                    pendingForFilter.end(),
                    [&](ReplayEngine::QueuedMacroCommand const& queued) {
                        return matchesQueuedMacroCommand(command, queued);
                    }
                );
                if (it != pendingForFilter.end()) {
                    filteredCommands.push_back(command);
                    pendingForFilter.erase(it);
                }
            }

            m_queuedButtons.swap(filteredCommands);
        }

        if (executingCBF) {
            queuedSnapshot = engine->queuedMacroCommands;
        }

        m_fields->macroInput = executingCBF && !queuedSnapshot.empty();
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        m_fields->macroInput = false;

        if (!executingCBF) {
            engine->queuedMacroCommands.clear();
            return;
        }

        std::deque<ReplayEngine::QueuedMacroCommand> stillQueued;
        auto unmatchedSnapshot = queuedSnapshot;

        for (auto const& command : m_queuedButtons) {
            auto it = std::find_if(
                unmatchedSnapshot.begin(),
                unmatchedSnapshot.end(),
                [&](ReplayEngine::QueuedMacroCommand const& queued) {
                    return matchesQueuedMacroCommand(command, queued);
                }
            );
            if (it == unmatchedSnapshot.end()) {
                continue;
            }

            stillQueued.push_back(*it);
            unmatchedSnapshot.erase(it);
        }

        for (auto const& processed : unmatchedSnapshot) {
            engine->triggerAudio(processed.player2, processed.button, processed.down);
        }

        engine->queuedMacroCommands.swap(stillQueued);
    }

    void handleButton(bool hold, int button, bool player2) {
        if (!PlayLayer::get()) {
            return GJBaseGameLayer::handleButton(hold, button, player2);
        }

        auto* engine = ReplayEngine::get();

        if (button == 1 && !m_fields->macroInput && !Autoclicker::get()->isAutoclickerInput) {
            Autoclicker::get()->trackUserInput(hold, player2);
        }

        if (engine->engineMode == MODE_DISABLED) {
            GJBaseGameLayer::handleButton(hold, button, player2);
            engine->triggerAudio(player2, button, hold);
            return;
        }

        if (engine->engineMode == MODE_EXECUTE) {
            if (engine->userInputIgnored && !m_fields->macroInput) {
                return;
            }
            return GJBaseGameLayer::handleButton(hold, button, player2);
        }

        if (engine->skipTickIndex != -1 && hold) {
            return;
        }

        int tick = computeCurrentTick();
        bool resumedPlayer2 = keepSecondPlayerSlot(this) ? player2 : false;
        int resumedSlot = resumedPlayer2 ? 1 : 0;
        int playerIndex = deferredSlotForPlayer(m_levelSettings->m_twoPlayerMode, player2);
        bool delayedInputQueued = engine->deferredInputTick[playerIndex] != -1;
        bool delayedReleaseQueued = engine->deferredReleaseA[playerIndex] != -1;

        if ((delayedInputQueued || engine->skipActionTick == tick || delayedReleaseQueued) && button == 1) {
            if (hold && engine->isCheckpointHoldResumed(resumedSlot, button)) {
                return;
            }
            if (engine->skipActionTick >= tick) {
                engine->deferredInputTick[playerIndex] = engine->skipActionTick + 1;
            }
            return;
        }

        if (engine->engineMode != MODE_CAPTURE || !engine->hasMacro()) {
            return GJBaseGameLayer::handleButton(hold, button, player2);
        }

        bool recordPlayer2 = player2;
        if (!keepSecondPlayerSlot(this)) {
            recordPlayer2 = false;
        }

        int recordSlot = recordPlayer2 ? 1 : 0;
        PlayerObject* targetPlayer = recordPlayer2 ? m_player2 : m_player1;
        bool trackHoldTransition = isTrackedHoldButton(button) && targetPlayer;
        bool wasHeld = trackHoldTransition ? targetPlayer->m_holdingButtons[button] : false;

        GJBaseGameLayer::handleButton(hold, button, player2);

        if (trackHoldTransition && !hold && wasHeld && engine->isCheckpointHoldResumed(recordSlot, button)) {
            engine->setCheckpointHoldResumed(recordSlot, button, false);
        }

        bool realTransition = !trackHoldTransition || (hold ? !wasHeld : wasHeld);
        if (!realTransition) {
            return;
        }

        if (!engine->captureIgnored && !engine->simulatingPath && !m_player1->m_isDead) {
            int recordTick = tick;
            float stepOffset = 0.0f;
            float basePhase = static_cast<float>(std::max(0, m_currentStep - engine->tickStartStep));
            AccuracyMode accuracyMode = engine->selectedAccuracyMode;

            if (accuracyMode == AccuracyMode::CBS) {
                stepOffset = basePhase;
            } else if (accuracyMode == AccuracyMode::CBF) {
                stepOffset = engine->consumeRawInputPhase(basePhase);
            }

            if (engine->ttrMode && engine->activeTTR) {
                if (!usesTimedAccuracy(engine->activeTTR->accuracyMode)) {
                    stepOffset = 0.0f;
                }
                engine->activeTTR->recordAction(recordTick, button, recordPlayer2, hold, stepOffset);
            } else if (engine->activeMacro) {
                if (!usesTimedAccuracy(engine->activeMacro->accuracyMode)) {
                    stepOffset = 0.0f;
                }
                engine->activeMacro->recordAction(recordTick, button, recordPlayer2, hold, stepOffset);
            }

            engine->macroInputActive = true;
            engine->triggerAudio(recordPlayer2, button, hold);
        }
    }
};
