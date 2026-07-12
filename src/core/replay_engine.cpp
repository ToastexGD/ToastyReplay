#include "ToastyReplay.hpp"
#include "core/checkpoint_handler.hpp"
#include "core/gameplay_layer.hpp"
#include "hacks/autoclicker.hpp"
#include "gui/gui.hpp"
#include "gui/frame_editor.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

void refreshRngState(bool isRestart = false);

namespace {
    constexpr double kQueuedCommandMatchTolerance = 0.001;
    constexpr double kTimestampEpsilon = 0.000001;

    struct PlaybackInputView {
        int tick = 0;
        int button = 1;
        bool player2 = false;
        bool pressed = false;
        float stepOffset = 0.0f;
        double cbsTimeOffset = -1.0;

        bool hasExactCbsTime() const {
            return std::isfinite(cbsTimeOffset) && cbsTimeOffset >= 0.0;
        }
    };

    cocos2d::CCPoint g_substepP1Position = { 0.f, 0.f };
    cocos2d::CCPoint g_substepP2Position = { 0.f, 0.f };
    float g_substepRotationDelta = 0.0f;
    bool g_substepP1Split = false;
    bool g_substepP2Split = false;

    namespace tick_util {
        int current(GJBaseGameLayer* layer, ReplayEngine* engine) {
            auto* playLayer = PlayLayer::get();
            if (!playLayer) return 0;
            int tick = static_cast<int>(playLayer->m_gameState.m_levelTime * engine->runtimeTickRate());
            return std::max(0, tick + 1);
        }
    }

    static bool resolveFlipState() {
        auto* playLayer = PlayLayer::get();
        if (!playLayer) return GameManager::get()->getGameVariable("0010");
        if (playLayer->m_levelSettings->m_platformerMode) return false;
        return GameManager::get()->getGameVariable("0010");
    }

    class DeferredDispatcher {
        GJBaseGameLayer* layer;
        ReplayEngine* engine;
        int currentTick;
        bool twoPlayers;
    public:
        DeferredDispatcher(GJBaseGameLayer* l, ReplayEngine* e, int tick)
            : layer(l), engine(e), currentTick(tick),
              twoPlayers(l->m_levelSettings->m_twoPlayerMode) {}

        bool tryFireMain(int playerSlot) {
            if (engine->deferredInputTick[playerSlot] != currentTick) return false;
            engine->deferredInputTick[playerSlot] = -1;
            bool player2 = twoPlayers ? (playerSlot == 0) : false;
            layer->GJBaseGameLayer::handleButton(true, 1, player2);
            return true;
        }

        bool tryFireMainRelease(int playerSlot) {
            if (engine->deferredReleaseA[playerSlot] != currentTick) return false;
            engine->deferredReleaseA[playerSlot] = -1;
            bool player2 = twoPlayers ? (playerSlot == 0) : false;
            layer->GJBaseGameLayer::handleButton(false, 1, player2);
            return true;
        }

        bool tryFireLateralRelease(int playerSlot, int buttonSlot) {
            if (engine->deferredReleaseB[playerSlot][buttonSlot] != currentTick) return false;
            engine->deferredReleaseB[playerSlot][buttonSlot] = -1;
            bool player2 = twoPlayers ? (playerSlot == 0) : false;
            layer->GJBaseGameLayer::handleButton(false, buttonSlot == 0 ? 2 : 3, player2);
            return true;
        }
    };

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

    static bool matchesQueuedMacroCommand(
        PlayerButtonCommand const& command,
        ReplayEngine::QueuedMacroCommand const& queued
    ) {
        if (static_cast<int>(command.m_button) != queued.button) return false;
        if (command.m_isPush != queued.down) return false;
        if (command.m_isPlayer2 != queued.player2) return false;
        return std::abs(command.m_timestamp - queued.timestamp) <= kQueuedCommandMatchTolerance;
    }

    static bool matchesQueuedMacroButton(
        int button,
        bool down,
        bool player2,
        ReplayEngine::QueuedMacroCommand const& queued
    ) {
        return button == queued.button && down == queued.down && player2 == queued.player2;
    }

    static float sanitizeCbsStepOffset(float offset) {
        if (!std::isfinite(offset)) {
            return 0.0f;
        }
        return std::max(0.0f, offset);
    }

    static double sanitizeCbsTimeOffset(double offset) {
        if (!std::isfinite(offset) || offset < 0.0) {

            return -1.0;
        }
        return offset;
    }

    static PlaybackInputView makePlaybackInput(TTRInput const& input) {
        return {
            input.tick,
            static_cast<int>(input.actionType),
            input.isPlayer2(),
            input.isPressed(),
            input.stepOffset,
            sanitizeCbsTimeOffset(input.cbsTimeOffset)
        };
    }

    static PlaybackInputView makePlaybackInput(MacroAction const& input) {
        return {
            static_cast<int>(input.frame),
            input.button,
            input.player2,
            input.down,
            input.stepOffset,
            -1.0f
        };
    }

    static float clampSubstepOffset(float offset) {
        if (!std::isfinite(offset)) {
            return 0.0f;
        }
        return std::clamp(offset, 0.0f, 1.0f);
    }

    static void clearCollisionLog(PlayerObject* player) {
        if (!player) {
            return;
        }

        player->m_collisionLogTop->removeAllObjects();
        player->m_collisionLogBottom->removeAllObjects();
        player->m_collisionLogLeft->removeAllObjects();
        player->m_collisionLogRight->removeAllObjects();
        player->m_lastCollisionLeft = -1;
        player->m_lastCollisionRight = -1;
        player->m_lastCollisionBottom = -1;
        player->m_lastCollisionTop = -1;
    }

    static bool shouldSplitQueuedSubsteps(PlayerObject* player) {
        if (!player) {
            return false;
        }

        return player->m_isOnGround
            || player->m_touchingRings->count() > 0
            || player->m_isDashing
            || player->m_isDart
            || player->m_isBird
            || player->m_isShip
            || player->m_isSwing;
    }

    static bool shouldBlockDeferredCapture(
        GJBaseGameLayer* layer,
        ReplayEngine* engine,
        int button,
        bool hold,
        bool player2,
        int tick
    ) {
        if (engine->skipTickIndex != -1 && hold) {
            return true;
        }

        if (button != 1) {
            return false;
        }

        int playerIndex = deferredSlotForPlayer(layer->m_levelSettings->m_twoPlayerMode, player2);
        bool delayedInputQueued = engine->deferredInputTick[playerIndex] != -1;
        bool delayedReleaseQueued = engine->deferredReleaseA[playerIndex] != -1;

        if (!delayedInputQueued && engine->skipActionTick != tick && !delayedReleaseQueued) {
            return false;
        }

        bool resumedPlayer2 = keepSecondPlayerSlot(layer) ? player2 : false;
        int resumedSlot = resumedPlayer2 ? 1 : 0;

        if (hold && engine->isCheckpointHoldResumed(resumedSlot, button)) {
            return true;
        }
        if (engine->skipActionTick >= tick) {
            engine->deferredInputTick[playerIndex] = engine->skipActionTick + 1;
        }
        return true;
    }

    static void resetQueuedSubstepVisualState() {
        g_substepP1Split = false;
        g_substepP2Split = false;
        g_substepRotationDelta = 0.0f;
    }
}

void ReplayEngine::dispatchQueuedSubstepInput(PlayLayer* playLayer, QueuedSubstepInput const& input) {
    if (!playLayer) {
        return;
    }

    bool player2 = input.player2;
    if (input.macroInput && resolveFlipState()) {
        player2 = !player2;
    }

    if (input.macroInput) {
        bool previousSubstepMacroDispatch = substepMacroDispatch;
        substepMacroDispatch = true;
        playLayer->GJBaseGameLayer::handleButton(input.pressed, input.button, player2);
        substepMacroDispatch = previousSubstepMacroDispatch;
        if (!simulatingPath) {
            triggerAudio(player2, input.button, input.pressed);
        }
        return;
    }

    auto* ac = Autoclicker::get();
    bool previousAutoclickerInput = ac->isAutoclickerInput;
    float previousCaptureOffset = substepCaptureOffset;
    ac->isAutoclickerInput = true;
    substepCaptureOffset = input.offset;
    playLayer->handleButton(input.pressed, input.button, player2);
    substepCaptureOffset = previousCaptureOffset;
    ac->isAutoclickerInput = previousAutoclickerInput;
}

void ReplayEngine::simulateQueuedSubstepTick(
    PlayLayer* playLayer,
    PlayerObject* player1,
    PlayerObject* player2,
    float stepDelta,
    std::deque<QueuedSubstepSegment>& segments,
    std::function<void(QueuedSubstepInput const&)> dispatchOverride
) {
    if (!playLayer || !player1 || segments.empty()) {
        return;
    }

    bool isDual = player2 && (playLayer->m_gameState.m_isDualMode || playLayer->m_levelSettings->m_twoPlayerMode);

    g_substepP1Position = player1->getPosition();
    g_substepP2Position = player2 ? player2->getPosition() : cocos2d::CCPoint { 0.f, 0.f };
    g_substepP1Split = shouldSplitQueuedSubsteps(player1);
    g_substepP2Split = isDual && shouldSplitQueuedSubsteps(player2);

    substepMidStep = true;

    while (!segments.empty()) {
        auto segment = segments.front();
        segments.pop_front();

        if (segment.hasActionAfter && !segment.endStep && segment.deltaFactor <= 0.0f) {
            if (dispatchOverride) {
                dispatchOverride(segment.actionAfter);
            } else {
                dispatchQueuedSubstepInput(playLayer, segment.actionAfter);
            }
            continue;
        }

        float substepDelta = stepDelta * std::max(0.0f, segment.deltaFactor);
        g_substepRotationDelta = substepDelta;

        if (g_substepP1Split) {
            player1->PlayerObject::update(substepDelta);
            if (!segment.endStep) {
                if (!player1->m_isOnSlope || player1->m_isDart) {
                    playLayer->checkCollisions(player1, 0.0f, true);
                } else {
                    playLayer->checkCollisions(player1, stepDelta, true);
                }
                player1->PlayerObject::updateRotation(substepDelta);
                clearCollisionLog(player1);
            }
        } else if (segment.endStep) {
            player1->PlayerObject::update(stepDelta);
        }

        if (isDual && player2) {
            if (g_substepP2Split) {
                player2->PlayerObject::update(substepDelta);
                if (!segment.endStep) {
                    if (!player2->m_isOnSlope || player2->m_isDart) {
                        playLayer->checkCollisions(player2, 0.0f, true);
                    } else {
                        playLayer->checkCollisions(player2, stepDelta, true);
                    }
                    player2->PlayerObject::updateRotation(substepDelta);
                    clearCollisionLog(player2);
                }
            } else if (segment.endStep) {
                player2->PlayerObject::update(stepDelta);
            }
        }

        if (segment.hasActionAfter) {
            if (dispatchOverride) {
                dispatchOverride(segment.actionAfter);
            } else {
                dispatchQueuedSubstepInput(playLayer, segment.actionAfter);
            }
        }

        if (segment.endStep) {
            break;
        }
    }

    substepMidStep = false;
    substepCaptureOffset = -1.0f;
}

class $modify(MacroEngineBaseLayer, GJBaseGameLayer) {
    struct Fields {
        bool macroInput = false;
    };

    bool resolvePlaybackPlayer2(bool player2) {
        if (resolveFlipState()) {
            player2 = !player2;
        }

        return player2 && keepSecondPlayerSlot(this);
    }

    bool shouldUseInputOnlyTTRPlayback() {
        auto* engine = ReplayEngine::get();

        return engine &&
            engine->engineMode == MODE_EXECUTE &&
            engine->ttrMode &&
            engine->activeTTR &&
            !engine->activeTTR->inputs.empty() &&
            engine->activeTTR->anchors.empty() &&
            ReplayEngine::runtimeAccuracyModeFor(engine->activeTTR->accuracyMode) != AccuracyMode::CBS;
    }

    bool shouldUseInputOnlyGDRPlayback() {
        auto* engine = ReplayEngine::get();
        return engine &&
            engine->engineMode == MODE_EXECUTE &&
            !engine->ttrMode &&
            engine->activeMacro &&
            !engine->activeMacro->inputs.empty() &&
            engine->activeMacro->anchors.empty() &&
            ReplayEngine::runtimeAccuracyModeFor(engine->activeMacro->accuracyMode) != AccuracyMode::CBS;
    }

    void processInputOnly(bool shouldUse, std::function<void(int)> dispatch) {
        if (!shouldUse) return;
        int progress = static_cast<int>(m_gameState.m_currentProgress / 2);
        bool prev = m_fields->macroInput;
        m_fields->macroInput = true;
        dispatch(progress);
        m_fields->macroInput = prev;
    }

    void dispatchImmediatePlaybackAction(int tick, int button, bool down, bool player2) {
        auto* engine = ReplayEngine::get();
        if (tick == engine->respawnTickIndex) {
            return;
        }

        player2 = resolvePlaybackPlayer2(player2);

        m_fields->macroInput = true;
        GJBaseGameLayer::handleButton(down, button, player2);
        m_fields->macroInput = false;
        engine->triggerAudio(player2, button, down);
    }

    void queueNativeCBSPlaybackAction(
        int tick,
        int button,
        bool down,
        bool player2,
        double timestamp
    ) {
        auto* engine = ReplayEngine::get();
        if (tick == engine->respawnTickIndex) {
            return;
        }

        player2 = resolvePlaybackPlayer2(player2);

        PlayerButtonCommand command = {};
        command.m_button = static_cast<PlayerButton>(button);
        command.m_isPush = down;
        command.m_isPlayer2 = player2;
        command.m_timestamp = timestamp;
        m_queuedButtons.push_back(command);
        engine->queueMacroCommand(button, down, player2, timestamp);
    }

    void queueNativeCBSPlaybackStepAction(
        int tick,
        int button,
        bool down,
        bool player2,
        float phase,
        int stepDelta,
        float dt
    ) {
        float fractionalPhase = std::clamp(phase - static_cast<float>(stepDelta), 0.0f, 0.999f);
        double timestamp = m_timestamp + static_cast<double>(std::max(0.0f, dt)) * static_cast<double>(fractionalPhase);
        queueNativeCBSPlaybackAction(tick, button, down, player2, timestamp);
    }

    void dispatchQueuedSubstepInput(QueuedSubstepInput const& input) {
        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();
        if (!engine || !playLayer) {
            return;
        }
        engine->dispatchQueuedSubstepInput(playLayer, input);
    }

    void queueAutoclickerInputs() {
        auto* engine = ReplayEngine::get();
        auto* ac = Autoclicker::get();
        if (!engine || !ac->enabled || engine->engineMode == MODE_EXECUTE) {
            return;
        }

        std::vector<QueuedSubstepInput> queued;
        if (ac->isTimedMode()) {
            if (engine->engineMode == MODE_CAPTURE) {
                return;
            }

            auto actions = ac->buildTimedTickActions(engine->runtimeTickRate());
            queued.reserve(actions.size());
            for (auto const& action : actions) {
                queued.push_back({
                    clampSubstepOffset(action.offset),
                    1,
                    action.player2,
                    action.pressed,
                    false
                });
            }
        } else {
            auto actions = ac->processTick();
            if (actions.p1Fire) {
                queued.push_back({ 0.0f, 1, false, actions.p1Press, false });
            }
            if (actions.p2Fire) {
                queued.push_back({ 0.0f, 1, true, actions.p2Press, false });
            }
        }

        engine->armQueuedSubstepTick(std::move(queued));
    }

    void queueCBSPlaybackInputs(int tick, int effectiveTick, int stepDelta, float dt) {
        auto* engine = ReplayEngine::get();
        if (!engine || !engine->hasMacro() || m_levelEndAnimationStarted) {
            return;
        }

        auto handleInput = [&](PlaybackInputView const& input) -> bool {
            if (input.tick > effectiveTick) {
                return false;
            }

            if (!isTrackedHoldButton(input.button) || input.tick < effectiveTick) {
                dispatchImmediatePlaybackAction(tick, input.button, input.pressed, input.player2);
                return true;
            }

            if (input.hasExactCbsTime()) {
                double targetTimestamp = engine->tickStartTimestamp + static_cast<double>(input.cbsTimeOffset);
                double nextTimestamp = m_timestamp + static_cast<double>(std::max(0.0f, dt));
                if (targetTimestamp >= nextTimestamp - kTimestampEpsilon) {
                    return false;
                }

                if (targetTimestamp < m_timestamp - kTimestampEpsilon) {
                    dispatchImmediatePlaybackAction(tick, input.button, input.pressed, input.player2);
                } else {
                    queueNativeCBSPlaybackAction(tick, input.button, input.pressed, input.player2, targetTimestamp);
                }
                return true;
            }

            float phase = sanitizeCbsStepOffset(input.stepOffset);
            if (phase >= static_cast<float>(stepDelta + 1)) {
                return false;
            }

            if (phase < static_cast<float>(stepDelta)) {
                dispatchImmediatePlaybackAction(tick, input.button, input.pressed, input.player2);
            } else {
                queueNativeCBSPlaybackStepAction(tick, input.button, input.pressed, input.player2, phase, stepDelta, dt);
            }
            return true;
        };

        if (engine->ttrMode && engine->activeTTR) {
            auto* inputListPtr = engine->activeTTRInputs();
            if (!inputListPtr) {
                return;
            }
            auto& inputList = *inputListPtr;
            while (engine->executeIndex < inputList.size()) {
                if (!handleInput(makePlaybackInput(inputList[engine->executeIndex]))) {
                    break;
                }
                ++engine->executeIndex;
            }
            return;
        }

        if (!engine->activeMacro) {
            return;
        }

        auto& inputList = engine->activeMacro->inputs;
        while (engine->executeIndex < inputList.size()) {
            if (!handleInput(makePlaybackInput(inputList[engine->executeIndex]))) {
                break;
            }
            ++engine->executeIndex;
        }
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();

        if (!playLayer) {
            return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        }

        refreshRngState();

        int tick = tick_util::current(this, engine);
        if (engine->shouldResetAfterPersistencePlaybackDeath(playLayer)) {
            if (m_levelSettings->m_platformerMode) {
                return playLayer->resetLevelFromStart();
            }
            return playLayer->resetLevel();
        }

        AccuracyMode playbackAccuracy = ReplayEngine::runtimeAccuracyModeFor(engine->activeMacroAccuracyMode());
        bool timedPlayback = engine->engineMode == MODE_EXECUTE && usesTimedAccuracy(playbackAccuracy);
        bool executingCBS = engine->engineMode == MODE_EXECUTE && playbackAccuracy == AccuracyMode::CBS;
        bool newTick = tick != engine->lastTickIndex;

        if (newTick) {
            engine->tickStartStep = m_currentStep;
            engine->tickStartTimestamp = m_timestamp;
            engine->clearQueuedSubstepState();
            queueAutoclickerInputs();
        }

        int stepDelta = std::max(0, m_currentStep - engine->tickStartStep);

        auto computeAndGuard = [&]() -> std::optional<int> {
            if (engine->engineMode != MODE_DISABLED) {
                if (tick > 2 && engine->initialRun && engine->hasMacro()) {
                    engine->initialRun = false;
                    if (!m_levelEndAnimationStarted) {
                        if (m_levelSettings->m_platformerMode) {
                            playLayer->resetLevelFromStart();
                        } else {
                            playLayer->resetLevel();
                        }
                        return std::nullopt;
                    }
                }

                if (engine->lastTickIndex == tick && tick != 0 && engine->hasMacro()) {
                    bool repeatedTimedSlice = timedPlayback && engine->lastStepDelta == stepDelta;
                    if (!timedPlayback || repeatedTimedSlice)
                        return std::nullopt;
                }
            }
            return tick;
        };

        auto handleRecordingPhase = [&](int t) {
            if (!newTick) return;
            if (engine->hasMacro() && engine->levelRestarting && !m_levelEndAnimationStarted) {
                if (m_levelSettings->m_platformerMode) {
                    playLayer->resetLevelFromStart();
                } else {
                    playLayer->resetLevel();
                }
                return;
            }
            captureTick(t);
        };

        auto handlePlaybackPhase = [&](int t) {
            if (newTick && engine->hasMacro() && engine->levelRestarting && !m_levelEndAnimationStarted) {
                if (m_levelSettings->m_platformerMode) {
                    playLayer->resetLevelFromStart();
                } else {
                    playLayer->resetLevel();
                }
                return;
            }
            executeTick(t, stepDelta);
        };

        auto tickOpt = computeAndGuard();
        if (!tickOpt) {
            return GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        }

        if (executingCBS) {
            queueCBSPlaybackInputs(tick, tick + engine->tickOffset, stepDelta, dt);
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        engine->lastTickIndex = tick;
        engine->lastStepDelta = stepDelta;

        if (engine->engineMode == MODE_DISABLED) {
            return;
        }

        if (engine->engineMode == MODE_CAPTURE) {
            handleRecordingPhase(*tickOpt);
        } else if (engine->engineMode == MODE_EXECUTE) {
            handlePlaybackPhase(*tickOpt);
        }
    }

#ifdef GEODE_IS_MACOS
    void update(float dt) {
        auto* engine = ReplayEngine::get();
        auto* playLayer = PlayLayer::get();
        if (!playLayer || !engine || engine->engineMode == MODE_DISABLED) {
            return GJBaseGameLayer::update(dt);
        }

        refreshRngState();
        int tick = tick_util::current(this, engine);
        bool newTick = tick != engine->lastTickIndex;

        if (engine->shouldResetAfterPersistencePlaybackDeath(playLayer)) {
            if (m_levelSettings->m_platformerMode) return playLayer->resetLevelFromStart();
            return playLayer->resetLevel();
        }

        if (tick > 2 && engine->initialRun && engine->hasMacro() && !m_levelEndAnimationStarted) {
            engine->initialRun = false;
            if (m_levelSettings->m_platformerMode) playLayer->resetLevelFromStart();
            else playLayer->resetLevel();
            return GJBaseGameLayer::update(dt);
        }

        if (newTick) {
            engine->tickStartStep = m_currentStep;
            engine->tickStartTimestamp = m_timestamp;
            engine->clearQueuedSubstepState();
            queueAutoclickerInputs();
            if (engine->hasMacro() && engine->levelRestarting && !m_levelEndAnimationStarted) {
                if (m_levelSettings->m_platformerMode) playLayer->resetLevelFromStart();
                else playLayer->resetLevel();
                return GJBaseGameLayer::update(dt);
            }
        }

        GJBaseGameLayer::update(dt);

        engine->lastTickIndex = tick;
        engine->lastStepDelta = std::max(0, m_currentStep - engine->tickStartStep);
    }
#endif

    void dispatchDeferredActions(int tick) {
        auto* engine = ReplayEngine::get();

        if (engine->skipTickIndex != -1 && engine->skipTickIndex < tick) {
            engine->skipTickIndex = -1;
        }

        if (engine->skipActionTick != -1 && tick > engine->skipActionTick) {
            engine->skipActionTick = -1;
        }

        DeferredDispatcher dispatcher(this, engine, tick);
        for (int slot = 0; slot < 2; ++slot) {
            dispatcher.tryFireMain(slot);
            dispatcher.tryFireMainRelease(slot);
            if (m_levelSettings->m_platformerMode) {
                dispatcher.tryFireLateralRelease(slot, 0);
                dispatcher.tryFireLateralRelease(slot, 1);
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
            if (!engine->activeTTR) {
                return;
            }

            engine->recordTTRAnchor(
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
        auto* inputListPtr = engine->activeTTRInputs();
        if (!inputListPtr) {
            return;
        }
        auto& inputList = *inputListPtr;
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

    void dispatchInputOnlyTTRInputs(int progress) {
        auto* engine = ReplayEngine::get();
        auto* inputListPtr = engine->activeTTRInputs();
        if (!inputListPtr) {
            return;
        }

        auto& inputList = *inputListPtr;

        while (engine->executeIndex < inputList.size()) {
            auto const& input = inputList[engine->executeIndex];
            if (input.tick > progress) {
                break;
            }

            bool player2 = input.isPlayer2();
            dispatchImmediatePlaybackAction(input.tick, input.actionType, input.isPressed(), player2);
            ++engine->executeIndex;
        }
    }

    void dispatchGDRInputsOnly(int progress) {
        auto* engine = ReplayEngine::get();
        auto& inputList = engine->activeMacro->inputs;

        while (engine->executeIndex < inputList.size()) {
            auto const& input = inputList[engine->executeIndex];
            int inputTick = static_cast<int>(input.frame);
            if (inputTick > progress) break;
            dispatchImmediatePlaybackAction(inputTick, input.button, input.down, input.player2);
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
            if (m_player2) m_player2->releaseAllButtons();
            return;
        }

        int effectiveTick = tick + engine->tickOffset;
        AccuracyMode playbackAccuracy = ReplayEngine::runtimeAccuracyModeFor(engine->activeMacroAccuracyMode());

        if (playbackAccuracy != AccuracyMode::CBS && !shouldUseInputOnlyTTRPlayback() && !shouldUseInputOnlyGDRPlayback()) {
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

        if (engine->shouldTriggerPersistencePlaybackDeath(effectiveTick)) {
            if (auto* playLayer = PlayLayer::get()) {
                if (auto* deathPlayer = engine->persistencePlaybackDeathPlayer(playLayer)) {
                    engine->markPersistencePlaybackDeathPending();
                    playLayer->destroyPlayer(deathPlayer, nullptr);
                }
            }
        }
    }

#ifdef GEODE_IS_MACOS
    void macSubstepDispatch() {
        auto* engine = ReplayEngine::get();
        if (!engine || engine->engineMode != MODE_EXECUTE || !engine->hasMacro()) {
            return;
        }
        int tick = tick_util::current(this, engine);
        executeTick(tick, 0);
        processInputOnly(shouldUseInputOnlyTTRPlayback(), [&](int p) { dispatchInputOnlyTTRInputs(p); });
        processInputOnly(shouldUseInputOnlyGDRPlayback(), [&](int p) { dispatchGDRInputsOnly(p); });
    }
#endif

    void processQueuedButtons(float dt, bool clearInputQueue) {
        auto* engine = ReplayEngine::get();
        bool executingCBS = engine->engineMode == MODE_EXECUTE &&
            ReplayEngine::runtimeAccuracyModeFor(engine->activeMacroAccuracyMode()) == AccuracyMode::CBS;
        AccuracyMode captureAccuracy = engine->ttrMode
            ? (engine->activeTTR ? engine->activeTTR->accuracyMode : engine->selectedAccuracyMode)
            : (engine->activeMacro ? engine->activeMacro->accuracyMode : engine->selectedAccuracyMode);
        bool capturingCBS = engine->engineMode == MODE_CAPTURE &&
            ReplayEngine::runtimeAccuracyModeFor(captureAccuracy) == AccuracyMode::CBS;
        std::deque<ReplayEngine::QueuedMacroCommand> queuedSnapshot;

        engine->queuedCaptureCommands.clear();
        engine->cbsCaptureProcessingQueue = false;
        if (capturingCBS && !engine->captureIgnored && !engine->simulatingPath) {
            for (auto const& command : m_queuedButtons) {
                int button = static_cast<int>(command.m_button);
                if (isTrackedHoldButton(button)) {
                    engine->queueCbsCaptureCommand(button, command.m_isPush, command.m_isPlayer2, command.m_timestamp);
                }
            }
            engine->cbsCaptureProcessingQueue = true;
        }

        if (executingCBS && engine->manualInputIgnoredActive()) {
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

        if (executingCBS) {
            queuedSnapshot = engine->queuedMacroCommands;
        }

        bool previousMacroInput = m_fields->macroInput;
        bool previousCbsPlaybackProcessingQueue = engine->cbsPlaybackProcessingQueue;
        m_fields->macroInput = previousMacroInput ||
            (executingCBS && engine->manualInputIgnoredActive() && !queuedSnapshot.empty());
        engine->cbsPlaybackProcessingQueue = executingCBS;
#ifdef GEODE_IS_MACOS
        if (!engine->simulatingPath && !engine->substepMidStep) {
            macSubstepDispatch();
        }
#endif
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);

        processInputOnly(shouldUseInputOnlyTTRPlayback(), [&](int p) { dispatchInputOnlyTTRInputs(p); });
        processInputOnly(shouldUseInputOnlyGDRPlayback(), [&](int p) { dispatchGDRInputsOnly(p); });

        m_fields->macroInput = previousMacroInput;
        engine->cbsPlaybackProcessingQueue = previousCbsPlaybackProcessingQueue;
#ifdef GEODE_IS_MACOS
        if (engine->engineMode == MODE_CAPTURE && !engine->simulatingPath && !engine->substepMidStep) {
            captureTick(tick_util::current(this, engine));
        }
#endif
        engine->cbsCaptureProcessingQueue = false;
        engine->queuedCaptureCommands.clear();

        if (!executingCBS) {
            engine->clearQueuedMacroCommands();
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
        if (!toasty::gameplay::isGameplayActive()) {
            return GJBaseGameLayer::handleButton(hold, button, player2);
        }

        auto* engine = ReplayEngine::get();
        bool executingCBS = engine->engineMode == MODE_EXECUTE &&
            ReplayEngine::runtimeAccuracyModeFor(engine->activeMacroAccuracyMode()) == AccuracyMode::CBS;
        bool queuedMacroInput = executingCBS && engine->cbsPlaybackProcessingQueue && std::any_of(
            engine->queuedMacroCommands.begin(),
            engine->queuedMacroCommands.end(),
            [&](ReplayEngine::QueuedMacroCommand const& queued) {
                return matchesQueuedMacroButton(button, hold, player2, queued);
            }
        );
        bool macroInput = m_fields->macroInput || engine->substepMacroDispatch || queuedMacroInput;

        if (button == 1 && !macroInput && !Autoclicker::get()->isAutoclickerInput) {
            Autoclicker::get()->trackUserInput(hold, player2);
        }

        if (engine->engineMode == MODE_DISABLED) {
            GJBaseGameLayer::handleButton(hold, button, player2);
            engine->triggerAudio(player2, button, hold);
            return;
        }

        if (engine->engineMode == MODE_EXECUTE) {
            if (engine->manualInputIgnoredActive() && !macroInput) {
                return;
            }
            GJBaseGameLayer::handleButton(hold, button, player2);
            if (!macroInput) {
                engine->triggerAudio(player2, button, hold);
            }
            return;
        }

        if (shouldBlockDeferredCapture(this, engine, button, hold, player2, tick_util::current(this, engine))) {
            return;
        }

        if (engine->engineMode != MODE_CAPTURE || !engine->hasMacro()) {
            GJBaseGameLayer::handleButton(hold, button, player2);
            engine->triggerAudio(player2, button, hold);
            return;
        }

        bool recordPlayer2 = player2;
        if (!keepSecondPlayerSlot(this)) {
            recordPlayer2 = false;
        }

        int recordSlot = recordPlayer2 ? 1 : 0;
        PlayerObject* targetPlayer = recordPlayer2 ? m_player2 : m_player1;
        bool trackHoldTransition = isTrackedHoldButton(button) && targetPlayer;
        bool wasHeld = trackHoldTransition ? targetPlayer->m_holdingButtons[button] : false;
        AccuracyMode accuracyMode = engine->ttrMode
            ? (engine->activeTTR ? engine->activeTTR->accuracyMode : engine->selectedAccuracyMode)
            : (engine->activeMacro ? engine->activeMacro->accuracyMode : engine->selectedAccuracyMode);
        bool recordingCBS = ReplayEngine::runtimeAccuracyModeFor(accuracyMode) == AccuracyMode::CBS;
        auto exactCbsCommand = recordingCBS
            ? engine->popCbsCaptureCommand(button, hold, player2)
            : std::optional<ReplayEngine::QueuedCaptureCommand> {};

        GJBaseGameLayer::handleButton(hold, button, player2);

        if (trackHoldTransition && !hold && wasHeld && engine->isCheckpointHoldResumed(recordSlot, button)) {
            engine->setCheckpointHoldResumed(recordSlot, button, false);
        }

        bool realTransition = !trackHoldTransition || (hold ? !wasHeld : wasHeld);
        if (!realTransition) {
            return;
        }

        if (!engine->captureIgnored && !engine->simulatingPath && !m_player1->m_isDead) {
            int recordTick = tick_util::current(this, engine);
            float stepOffset = 0.0f;
            double cbsTimeOffset = -1.0;
            float basePhase = static_cast<float>(std::max(0, m_currentStep - engine->tickStartStep));

            if (usesStepBasedAccuracy(accuracyMode)) {
                stepOffset = basePhase;
            }
            if (exactCbsCommand) {
                double offset = exactCbsCommand->timestamp - engine->tickStartTimestamp;
                if (std::isfinite(offset) && offset >= 0.0) {
                    cbsTimeOffset = offset;
                }
            } else if (recordingCBS) {
                double offset = m_timestamp - engine->tickStartTimestamp;
                if (std::isfinite(offset) && offset >= 0.0) {
                    cbsTimeOffset = offset;
                }
            }

            if (engine->ttrMode && engine->activeTTR) {
                if (!usesTimedAccuracy(engine->activeTTR->accuracyMode)) {
                    stepOffset = 0.0f;
                    cbsTimeOffset = -1.0;
                }
                engine->recordTTRAction(recordTick, button, recordPlayer2, hold, stepOffset, cbsTimeOffset);
            } else if (engine->activeMacro) {
                if (!usesTimedAccuracy(engine->activeMacro->accuracyMode)) {
                    stepOffset = 0.0f;
                }
                engine->activeMacro->recordAction(recordTick, button, recordPlayer2, hold, stepOffset);
            }

            engine->markActiveMacroDirty();
            engine->macroInputActive = true;
            engine->triggerAudio(recordPlayer2, button, hold);
        }
    }
};

class $modify(MacroEnginePlayerObject, PlayerObject) {
    void update(float stepDelta) {
        auto* playLayer = PlayLayer::get();
        auto* engine = ReplayEngine::get();

        if (!playLayer || !engine || engine->simulatingPath || !engine->hasQueuedSubstepTick()) {
            if (!engine || !engine->substepMidStep) {
                resetQueuedSubstepVisualState();
            }
            return PlayerObject::update(stepDelta);
        }

        if (engine->substepMidStep || this != playLayer->m_player1) {
            return PlayerObject::update(stepDelta);
        }

        engine->simulateQueuedSubstepTick(
            playLayer,
            playLayer->m_player1,
            playLayer->m_player2,
            stepDelta,
            engine->queuedSubstepSegments
        );
    }

    void updateRotation(float t) {
        auto* playLayer = PlayLayer::get();
        auto* engine = ReplayEngine::get();

        if (playLayer && engine && !engine->substepMidStep && this == playLayer->m_player1 && g_substepP1Split) {
            PlayerObject::updateRotation(g_substepRotationDelta);
            g_substepP1Split = false;
            return;
        }

        if (playLayer && engine && !engine->substepMidStep && playLayer->m_player2 && this == playLayer->m_player2 && g_substepP2Split) {
            PlayerObject::updateRotation(g_substepRotationDelta);
            g_substepP2Split = false;
            return;
        }

        PlayerObject::updateRotation(t);
    }
};
