#include "ToastyReplay.hpp"
#include "core/start_position_policy.hpp"
#include "core/checkpoint_handler.hpp"
#include "hacks/physicsbypass.hpp"
#include "trajectory/trajectory.hpp"

#include <Geode/modify/CheckpointObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <random>

using namespace geode::prelude;

namespace {
    void incrementSaturated(int& value) {
        if (value < std::numeric_limits<int>::max()) {
            ++value;
        }
    }

    void triggerNoclipDeathFlash(ReplayEngine* engine) {
        if (!engine || !engine->noclipDeathFlash) {
            return;
        }

        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) {
            return;
        }

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

    bool isNoclipAccuracyFrameActive(PlayLayer* playLayer, ReplayEngine* engine) {
        if (!playLayer || !engine || !engine->collisionBypass) {
            return false;
        }
        if (!engine->noclipPlayer1 && !engine->noclipPlayer2) {
            return false;
        }
        if (TrajectoryPredictionService::get().isActiveSimulation()) {
            return false;
        }

        bool playerDead = (playLayer->m_player1 && playLayer->m_player1->m_isDead)
            || (playLayer->m_player2 && playLayer->m_player2->m_isDead);
        return !playerDead && !playLayer->m_hasCompletedLevel && !playLayer->m_levelEndAnimationStarted;
    }

    bool shouldNoclipPlayer(PlayLayer* playLayer, PlayerObject* player, ReplayEngine* engine) {
        if (!playLayer || !player || !engine || !engine->collisionBypass) {
            return false;
        }
        if (player == playLayer->m_player1) {
            return engine->noclipPlayer1;
        }
        if (player == playLayer->m_player2) {
            return engine->noclipPlayer2;
        }
        return false;
    }

    void clearPendingNoclipFrame(ReplayEngine* engine) {
        if (!engine) {
            return;
        }

        engine->noclipWouldDieThisFrame = false;
        engine->noclipDeadLastFrame = false;
        engine->noclipLastDeathPlayer = nullptr;
        engine->noclipLastDeathObject = nullptr;
    }

    void processNoclipAccuracyFrame(PlayLayer* playLayer) {
        auto* engine = ReplayEngine::get();

        if (!isNoclipAccuracyFrameActive(playLayer, engine)) {
            clearPendingNoclipFrame(engine);
            return;
        }

        incrementSaturated(engine->noclipTotalFrames);

        bool unsafeFrame = engine->noclipWouldDieThisFrame;
        if (unsafeFrame) {
            incrementSaturated(engine->noclipUnsafeFrames);
            if (!engine->noclipDeadLastFrame) {
                incrementSaturated(engine->noclipDeathEvents);
                triggerNoclipDeathFlash(engine);
            }
        }

        engine->noclipDeadLastFrame = unsafeFrame;
        engine->noclipWouldDieThisFrame = false;

        auto* deathPlayer = engine->noclipLastDeathPlayer ? engine->noclipLastDeathPlayer : playLayer->m_player1;
        auto* deathObject = engine->noclipLastDeathObject;
        engine->noclipLastDeathPlayer = nullptr;
        engine->noclipLastDeathObject = nullptr;

        if (!unsafeFrame || !engine->collisionLimitActive || engine->collisionThreshold <= 0.0f) {
            return;
        }

        if (engine->noclipAccuracyPercent() < engine->collisionThreshold) {
            bool restoreNoclip = engine->collisionBypass;
            engine->collisionBypass = false;
            engine->clearFrameStepState();
            engine->resetNoclipAccuracyStats();
            playLayer->destroyPlayer(deathPlayer, deathObject);
            engine->collisionBypass = restoreNoclip;
        }
    }
}

static uint32_t computeLockedSeed(uint32_t seedSalt, int currentProgress) {
    std::mt19937 mixer(static_cast<unsigned>(seedSalt + currentProgress));
    std::uniform_int_distribution<uint64_t> dist(10000, 999999999);
    return static_cast<uint32_t>(dist(mixer));
}

static uint32_t computeFallbackSeed() {
    std::random_device device;
    std::mt19937 mixer(device());
    std::uniform_int_distribution<uint64_t> dist(1000, 999999999);
    return static_cast<uint32_t>(dist(mixer));
}

void refreshRngState(bool isLevelStart) {
    auto* engine = ReplayEngine::get();
    auto* playLayer = PlayLayer::get();
    if (!playLayer) {
        return;
    }

    if (engine->rngLocked) {
        uint64_t computedSeed = !playLayer->m_player1->m_isDead
            ? computeLockedSeed(engine->rngSeedVal, playLayer->m_gameState.m_currentProgress)
            : computeFallbackSeed();
        GameToolbox::fast_srand(computedSeed);
    }

    if (isLevelStart && engine->engineMode == MODE_CAPTURE) {
        engine->capturedRngState = GameToolbox::getfast_srand();
    }
}

static void resetDeferredInputs(ReplayEngine* engine) {
    engine->deferredReleaseA[0] = -1;
    engine->deferredReleaseA[1] = -1;
    engine->deferredInputTick[0] = -1;
    engine->deferredInputTick[1] = -1;
    engine->lateralInputPending[0] = false;
    engine->lateralInputPending[1] = false;
    for (int playerIndex = 0; playerIndex < 2; ++playerIndex) {
        for (int holdIndex = 0; holdIndex < 2; ++holdIndex) {
            engine->deferredReleaseB[playerIndex][holdIndex] = -1;
        }
    }
}

void ReplayEngine::resetDeferredInputState() {
    skipTickIndex = -1;
    skipActionTick = -1;
    clearCheckpointResumedHolds();
    clearQueuedSubstepState();
    clearQueuedMacroCommands();
    queuedCaptureCommands.clear();
    cbsCaptureProcessingQueue = false;
    resetDeferredInputs(this);
}

void ReplayEngine::buildQueuedSubstepSegments(
    std::vector<QueuedSubstepInput> actions,
    std::deque<QueuedSubstepSegment>& output
) const {
    output.clear();
    if (actions.empty()) {
        return;
    }

    float elapsed = 0.0f;
    for (auto& action : actions) {
        if (!std::isfinite(action.offset)) {
            action.offset = 0.0f;
        }
        action.offset = std::clamp(action.offset, 0.0f, 1.0f);

        QueuedSubstepSegment segment;
        segment.deltaFactor = std::max(0.0f, action.offset - elapsed);
        segment.endStep = false;
        segment.hasActionAfter = true;
        segment.actionAfter = action;
        output.push_back(segment);
        elapsed = std::max(elapsed, action.offset);
    }

    QueuedSubstepSegment finalSegment;
    finalSegment.deltaFactor = std::max(0.0f, 1.0f - elapsed);
    finalSegment.endStep = true;
    output.push_back(finalSegment);
}

void ReplayEngine::armQueuedSubstepTick(std::vector<QueuedSubstepInput> actions) {
    if (actions.empty()) {
        return;
    }

    buildQueuedSubstepSegments(std::move(actions), queuedSubstepSegments);
}

bool ReplayEngine::saveActiveMacro() {
    if ((ttrMode && activeTTR) || (!activeMacro && activeTTR)) {
        if (!activeTTR || (activeTTR->inputs.empty() && activeTTR->persistenceAttempts.empty())) {
            dataModified = false;
            return false;
        }

        activeTTR->persist();
        dataModified = false;
        reloadMacroList();
        return true;
    }

    if (activeMacro) {
        if (activeMacro->inputs.empty()) {
            dataModified = false;
            return false;
        }

        bool const useJson = selectedRecordingFormat == RecordingFormat::GDR;
        activeMacro->persist(activeMacro->accuracyMode, static_cast<int>(tickRate), useJson);
        dataModified = false;
        reloadMacroList();
        return true;
    }

    dataModified = false;
    return false;
}

void ReplayEngine::discardActiveMacro() {
    endReplayAccuracyEnvironment();

    if (activeTTR) {
        delete activeTTR;
        activeTTR = nullptr;
    }
    if (activeMacro) {
        delete activeMacro;
        activeMacro = nullptr;
    }

    dataModified = false;
    pendingPlaybackStart = false;
    executeIndex = 0;
    playbackAnchorIndex = 0;
    clearPersistenceRuntimeState();
    storedRestorePoints.clear();
    clearFrameStepState();
    clearStartPosWarning();
    resetDeferredInputState();
    resetTimingTracking();
}

void ReplayEngine::truncateRecordedDataAfter(int tick) {
    if (ttrMode && activeTTR) {
        if (canUsePersistenceCapture()) {
            while (!persistenceCaptureAttempt.inputs.empty() && persistenceCaptureAttempt.inputs.back().tick >= tick) {
                persistenceCaptureAttempt.inputs.pop_back();
            }
            while (!persistenceCaptureAttempt.anchors.empty() && persistenceCaptureAttempt.anchors.back().tick >= tick) {
                persistenceCaptureAttempt.anchors.pop_back();
            }
            return;
        }

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
    enum class UnsavedMacroChoice {
        Save,
        Discard,
        Cancel,
    };

    static std::string trString(std::string_view key) {
        return std::string(toasty::lang::tr(key));
    }

    static std::vector<std::string> wrapPopupMessage(std::string message) {
        std::vector<std::string> lines;
        constexpr size_t maxLineLength = 32;
        constexpr size_t maxLines = 3;

        while (message.size() > maxLineLength && lines.size() + 1 < maxLines) {
            auto split = message.rfind(' ', maxLineLength);
            if (split == std::string::npos || split < maxLineLength / 2) {
                split = message.find(' ', maxLineLength);
            }
            if (split == std::string::npos) {
                break;
            }

            lines.push_back(message.substr(0, split));
            message.erase(0, split + 1);
        }

        if (!message.empty()) {
            lines.push_back(std::move(message));
        }
        return lines;
    }

    static bool canUsePusabBody(std::string const& message) {
        return std::all_of(message.begin(), message.end(), [](unsigned char ch) {
            return ch < 128;
        });
    }

    class UnsavedMacroPopup : public Popup {
    protected:
        std::function<void(UnsavedMacroChoice)> m_onChoice;
        bool m_resolved = false;

        bool init(std::string message, std::function<void(UnsavedMacroChoice)> onChoice) {
            if (!Popup::init(350.f, 185.f, "square01_001.png")) {
                return false;
            }

            m_onChoice = std::move(onChoice);
            this->setTitle(trString("Unsaved Macro"));

            auto lines = wrapPopupMessage(message);
            auto const* bodyFont = canUsePusabBody(message) ? "bigFont.fnt" : "chatFont.fnt";
            auto const lineSpacing = canUsePusabBody(message) ? 22.0f : 20.0f;
            auto const lineScale = canUsePusabBody(message) ? 0.45f : 0.82f;
            auto const minLineScale = canUsePusabBody(message) ? 0.34f : 0.68f;
            auto const firstLineY = 22.0f + (static_cast<float>(lines.size()) - 1.0f) * lineSpacing * 0.5f;

            for (size_t i = 0; i < lines.size(); ++i) {
                auto* label = CCLabelBMFont::create(lines[i].c_str(), bodyFont);
                label->setAnchorPoint(ccp(0.5f, 0.5f));
                label->setColor(ccc3(235, 245, 255));
                label->limitLabelWidth(m_size.width - 72.0f, lineScale, minLineScale);
                m_mainLayer->addChildAtPosition(
                    label,
                    Anchor::Center,
                    ccp(0.0f, firstLineY - static_cast<float>(i) * lineSpacing)
                );
            }

            auto makeButton = [&](char const* key, char const* background, cocos2d::SEL_MenuHandler callback) {
                auto* sprite = ButtonSprite::create(trString(key).c_str(), "goldFont.fnt", background, 0.8f);
                sprite->setScale(0.78f);
                return CCMenuItemSpriteExtra::create(sprite, this, callback);
            };

            auto* saveBtn = makeButton("Save", "GJ_button_01.png", menu_selector(UnsavedMacroPopup::onSave));
            auto* discardBtn = makeButton("Discard", "GJ_button_05.png", menu_selector(UnsavedMacroPopup::onDiscard));
            auto* cancelBtn = makeButton("Cancel", "GJ_button_04.png", menu_selector(UnsavedMacroPopup::onCancel));

            m_buttonMenu->addChildAtPosition(saveBtn, Anchor::Bottom, ccp(-106.0f, 30.0f));
            m_buttonMenu->addChildAtPosition(discardBtn, Anchor::Bottom, ccp(0.0f, 30.0f));
            m_buttonMenu->addChildAtPosition(cancelBtn, Anchor::Bottom, ccp(106.0f, 30.0f));
            return true;
        }

        void resolve(UnsavedMacroChoice choice) {
            if (m_resolved) {
                return;
            }
            m_resolved = true;
            if (m_onChoice) {
                m_onChoice(choice);
            }
            Popup::onClose(nullptr);
        }

        void onSave(CCObject*) {
            resolve(UnsavedMacroChoice::Save);
        }

        void onDiscard(CCObject*) {
            resolve(UnsavedMacroChoice::Discard);
        }

        void onCancel(CCObject*) {
            resolve(UnsavedMacroChoice::Cancel);
        }

        void onClose(CCObject* sender) override {
            if (!m_resolved && m_onChoice) {
                m_onChoice(UnsavedMacroChoice::Cancel);
            }
            m_resolved = true;
            Popup::onClose(sender);
        }

    public:
        static UnsavedMacroPopup* create(std::string message, std::function<void(UnsavedMacroChoice)> onChoice) {
            auto* popup = new UnsavedMacroPopup();
            if (popup->init(std::move(message), std::move(onChoice))) {
                popup->autorelease();
                return popup;
            }
            delete popup;
            return nullptr;
        }
    };

    static int computePlaybackTick(PlayLayer* playLayer) {
        if (!playLayer) {
            return 0;
        }

        auto* engine = ReplayEngine::get();
        int tick = static_cast<int>(playLayer->m_gameState.m_levelTime * engine->runtimeTickRate());
        return std::max(0, tick + 1);
    }

    static int computeStartPosTargetTick(PlayLayer* playLayer, std::vector<PlaybackAnchor> const& anchors) {
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

    static bool isLatchHeld(uint8_t mask, int button) {
        if (button < 1 || button > 3) {
            return false;
        }
        return (mask & (1 << (button - 1))) != 0;
    }

    static void clearCheckpointHoldBaseline(PlayerObject* player, int button) {
        if (!player) {
            return;
        }

        player->m_holdingButtons[button] = false;
        if (button == 2) {
            player->m_holdingLeft = false;
        } else if (button == 3) {
            player->m_holdingRight = false;
        }
    }

    static void resumeCheckpointHold(PlayerObject* player, ReplayEngine* engine, uint8_t mask, int button, int playerSlot) {
        if (!player || !engine || !isLatchHeld(mask, button)) {
            return;
        }

        clearCheckpointHoldBaseline(player, button);
        player->pushButton(static_cast<PlayerButton>(button));
        engine->setCheckpointHoldResumed(playerSlot, button, true);
    }

    static void restoreCheckpointHolds(PlayLayer* playLayer, ReplayEngine* engine, CheckpointStateBundle const& checkpoint) {
        if (!playLayer || !engine || !playLayer->m_player1 || !playLayer->m_levelSettings) {
            return;
        }

        resumeCheckpointHold(playLayer->m_player1, engine, checkpoint.player1LatchMask, 1, 0);
        if (playLayer->m_levelSettings->m_platformerMode) {
            resumeCheckpointHold(playLayer->m_player1, engine, checkpoint.player1LatchMask, 2, 0);
            resumeCheckpointHold(playLayer->m_player1, engine, checkpoint.player1LatchMask, 3, 0);
        }

        bool hasSecondPlayer = playLayer->m_player2 &&
            (playLayer->m_gameState.m_isDualMode || playLayer->m_levelSettings->m_twoPlayerMode);
        if (!hasSecondPlayer) {
            return;
        }

        resumeCheckpointHold(playLayer->m_player2, engine, checkpoint.player2LatchMask, 1, 1);
        if (playLayer->m_levelSettings->m_platformerMode) {
            resumeCheckpointHold(playLayer->m_player2, engine, checkpoint.player2LatchMask, 2, 1);
            resumeCheckpointHold(playLayer->m_player2, engine, checkpoint.player2LatchMask, 3, 1);
        }
    }
}

void ReplayEngine::runWithUnsavedMacroGuard(std::function<void()> continueAction, std::string message) {
    if (!continueAction) {
        return;
    }

    if (!hasUnsavedActiveMacro()) {
        continueAction();
        return;
    }

    if (unsavedGuardActive) {
        return;
    }

    if (message.empty()) {
        message = std::string(toasty::lang::tr("You have an unsaved macro. Save before continuing?"));
    }

    auto* popup = UnsavedMacroPopup::create(
        std::move(message),
        [this, continueAction = std::move(continueAction)](UnsavedMacroChoice choice) mutable {
            unsavedGuardActive = false;
            if (choice == UnsavedMacroChoice::Cancel) {
                return;
            }
            if (choice == UnsavedMacroChoice::Save) {
                saveActiveMacro();
            }
            continueAction();
        }
    );

    if (!popup) {
        log::warn("Failed to create unsaved macro popup");
        return;
    }

    unsavedGuardActive = true;
    popup->show();
}

class $modify(RestorePointHandler, CheckpointObject) {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
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
        if (!playLayer
            || engine->engineMode != MODE_CAPTURE
            || TrajectoryPredictionService::get().isActiveSimulation()) {
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
        engine->resetNoclipAccuracyStats();
        if (engine->engineMode == MODE_CAPTURE) {
            if (engine->ttrMode) {
                engine->initializeTTRMacro(level);
            } else {
                engine->initializeMacro(level);
            }
        }
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void applyReplayResetState(bool inPractice, int tick) {
        auto* engine = ReplayEngine::get();

        refreshRngState(true);

        engine->executeIndex = 0;
        engine->playbackAnchorIndex = 0;
        engine->levelRestarting = false;
        engine->resetTimingTracking();
        engine->respawnTickIndex = -1;
        engine->tickAccumulator = 0.0f;
        engine->armPendingPlaybackStart(this);
        if (engine->engineMode == MODE_CAPTURE) {
            bool fromStartPosition = toasty::start_position::shouldRecordFromStartPosition(
                m_startPosObject != nullptr,
                getCurrentPercentInt()
            );
            auto applyStartPosition = [&](auto* macro) {
                if (!macro) return;
                macro->recordedFromStartPos = fromStartPosition;
                macro->startPosX = fromStartPosition ? m_startPosObject->getPositionX() : 0.0f;
                macro->startPosY = fromStartPosition ? m_startPosObject->getPositionY() : 0.0f;
            };
            if (engine->ttrMode) {
                applyStartPosition(engine->activeTTR);
            } else {
                applyStartPosition(engine->activeMacro);
            }
        }
        if (engine->engineMode == MODE_EXECUTE) {
            engine->advancePersistencePlaybackAfterReset();
        }

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

            float recStartX = engine->ttrMode
                ? (engine->activeTTR ? engine->activeTTR->startPosX : 0.f)
                : (engine->activeMacro ? engine->activeMacro->startPosX : 0.f);
            float recStartY = engine->ttrMode
                ? (engine->activeTTR ? engine->activeTTR->startPosY : 0.f)
                : (engine->activeMacro ? engine->activeMacro->startPosY : 0.f);
            bool recordedAtLevelStart = recordedFromStartPos &&
                toasty::start_position::isAtLevelStart(recStartX, m_levelLength);

            bool startPosMatch = false;
            if (m_startPosObject && recordedFromStartPos) {
                float dx = m_startPosObject->getPositionX() - recStartX;
                float dy = m_startPosObject->getPositionY() - recStartY;
                startPosMatch = std::sqrt(dx * dx + dy * dy) < 50.0f;
            }

            if (m_startPosObject && startPosMatch) {
                engine->tickOffset = 0;
                engine->startPosActive = true;
                if (anchors && !anchors->empty()) {
                    engine->playbackAnchorIndex = findFirstAnchorAtTick(*anchors, 0);
                } else {
                    engine->playbackAnchorIndex = 0;
                }
                if (engine->ttrMode && engine->activeTTR) {
                    engine->executeIndex = 0;
                } else if (engine->activeMacro) {
                    engine->executeIndex = 0;
                }
                engine->clearStartPosWarning();
            } else if (m_startPosObject && anchors && !anchors->empty()) {
                int targetTick = computeStartPosTargetTick(this, *anchors);
                int currentTick = computePlaybackTick(this);
                engine->tickOffset = targetTick - currentTick;
                engine->startPosActive = true;
                engine->playbackAnchorIndex = findFirstAnchorAtTick(*anchors, targetTick);
                if (engine->ttrMode && engine->activeTTR) {
                    if (auto* inputList = engine->activeTTRInputs()) {
                        engine->executeIndex = findFirstTTRInputAtTick(*inputList, targetTick);
                    } else {
                        engine->executeIndex = 0;
                    }
                } else if (engine->activeMacro) {
                    engine->executeIndex = findFirstInputAtTick(engine->activeMacro->inputs, targetTick);
                }
                engine->clearStartPosWarning();
            } else if (!m_startPosObject && recordedFromStartPos && !recordedAtLevelStart) {
                engine->setStartPosWarningKey("Macro was recorded from a start position. Use the same or later start position.");
                engine->haltExecution();
            } else if (m_startPosObject && (!anchors || anchors->empty())) {
                engine->setStartPosWarningKey("Macro lacks anchor data for start position playback. Re-record to enable.");
                engine->tickOffset = 0;
                engine->startPosActive = false;
            } else {
                engine->tickOffset = 0;
                engine->startPosActive = false;
                engine->clearStartPosWarning();
            }
        }

        if (engine->engineMode == MODE_CAPTURE && engine->ttrMode && engine->activeTTR && engine->persistenceMode) {
            if (!inPractice || tick <= 1 || engine->storedRestorePoints.empty()) {
                engine->resetPersistenceCaptureAttempt();
                engine->storedRestorePoints.clear();
                clearPlayerHolds();
            }
        } else if (engine->engineMode == MODE_CAPTURE && engine->ttrMode && engine->activeTTR) {
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

    void resetLevel() {
        auto* engine = ReplayEngine::get();
        engine->clearFrameStepState();
        engine->resetNoclipAccuracyStats();
        engine->captureIgnored = true;
        resetSimulationTimingState();
        PlayLayer::resetLevel();
        resetSimulationTimingState();
        engine->captureIgnored = false;

        applyReplayResetState(m_isPracticeMode, m_gameState.m_currentProgress);
    }

    void resetLevelFromStart() {
        auto* engine = ReplayEngine::get();
        engine->clearFrameStepState();
        engine->resetNoclipAccuracyStats();
        engine->captureIgnored = true;
        resetSimulationTimingState();
        PlayLayer::resetLevelFromStart();
        resetSimulationTimingState();
        engine->captureIgnored = false;

        applyReplayResetState(m_isPracticeMode, m_gameState.m_currentProgress);
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        if (TrajectoryPredictionService::get().isActiveSimulation()) {
            return PlayLayer::loadFromCheckpoint(checkpoint);
        }

        if (!checkpoint) {
            auto* engine = ReplayEngine::get();
            engine->clearFrameStepState();
            engine->resetNoclipAccuracyStats();
            resetSimulationTimingState();
            return PlayLayer::loadFromCheckpoint(checkpoint);
        }

        auto* engine = ReplayEngine::get();
        resetSimulationTimingState();
        engine->clearFrameStepState();
        engine->resetNoclipAccuracyStats();
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
            restoreCheckpointHolds(this, engine, it->second);
            engine->bridgeUserHoldsToPlayer(this);
            resetSimulationTimingState();
            return;
        }

        PlayLayer::loadFromCheckpoint(checkpoint);
        engine->bridgeUserHoldsToPlayer(this);
        resetSimulationTimingState();
    }

    void delayedResetLevel() {
        auto* engine = ReplayEngine::get();
        engine->clearFrameStepState();
        engine->resetNoclipAccuracyStats();
        resetSimulationTimingState();
        PlayLayer::delayedResetLevel();
        resetSimulationTimingState();
    }

    void levelComplete() {
        auto* engine = ReplayEngine::get();
        bool wasExecuting = engine->engineMode == MODE_EXECUTE;
        bool wasCapturingPersistence = engine->canUsePersistenceCapture();
        engine->clearFrameStepState();

        if (wasCapturingPersistence) {
            engine->completePersistenceCaptureAttempt();
        }

        PlayLayer::levelComplete();

        if (wasExecuting) {
            engine->disablePersistenceModeSafeguard();
            engine->haltExecution();
        } else if (wasCapturingPersistence) {
            engine->disablePersistenceModeSafeguard();
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (TrajectoryPredictionService::get().isActiveSimulation()) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }

        auto* engine = ReplayEngine::get();
        int deathTick = computePlaybackTick(this);
        bool deathPlayer2 = player && player == m_player2;
        bool persistenceDeath = engine->canUsePersistenceCapture() || engine->isPersistencePlaybackFailureAttempt();
        if (engine->canUsePersistenceCapture()) {
            engine->finalizePersistenceCaptureDeath(deathTick, deathPlayer2);
        } else if (engine->isPersistencePlaybackFailureAttempt()) {
            engine->markPersistencePlaybackDeathPending();
        }

        if (shouldNoclipPlayer(this, player, engine) && !persistenceDeath) {
            if (object == m_anticheatSpike) {
                engine->clearFrameStepState();
                PlayLayer::destroyPlayer(player, object);
                return;
            }

            engine->markNoclipUnsafeFrame(player, object);
            return;
        }

        engine->resetNoclipAccuracyStats();
        engine->clearFrameStepState();
        PlayLayer::destroyPlayer(player, object);
    }

    void onQuit() {
        auto* engine = ReplayEngine::get();
        auto quitLevel = [this, engine]() {
            engine->clearFrameStepState();
            engine->resetNoclipAccuracyStats();
            engine->pendingPlaybackStart = false;
            if (engine->engineMode == MODE_CAPTURE) {
                engine->discardActiveMacro();
                engine->engineMode = MODE_DISABLED;
            } else if (engine->engineMode == MODE_EXECUTE) {
                engine->haltExecution();
            }
            engine->disablePersistenceModeSafeguard();
            PlayLayer::onQuit();
        };

        if (engine->engineMode == MODE_CAPTURE) {
            engine->runWithUnsavedMacroGuard(
                std::move(quitLevel),
                trString("You have an unsaved macro. Save before exiting the level?")
            );
        } else {
            quitLevel();
        }
    }
};

class $modify(RecordingGuardPauseLayer, PauseLayer) {
    struct Fields {
        bool bypassGuard = false;
    };

    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("PauseLayer::goEdit", Priority::Early)) {
            log::warn("Failed to set hook priority for PauseLayer::goEdit");
        }
    }

    bool shouldGuardStartPosExit() const {
        auto* engine = ReplayEngine::get();
        return engine && engine->engineMode == MODE_CAPTURE && engine->hasUnsavedActiveMacro();
    }

    void goEdit() {
        if (m_fields->bypassGuard) {
            m_fields->bypassGuard = false;
            return PauseLayer::goEdit();
        }

        if (!shouldGuardStartPosExit()) {
            return PauseLayer::goEdit();
        }

        if (auto* engine = ReplayEngine::get()) {
            engine->runWithUnsavedMacroGuard(
                [this]() {
                    m_fields->bypassGuard = true;
                    PauseLayer::goEdit();
                },
                trString("You have an unsaved macro. Save before switching start position?")
            );
        }
    }

    void onQuit(cocos2d::CCObject* sender) {
        if (m_fields->bypassGuard) {
            m_fields->bypassGuard = false;
            return PauseLayer::onQuit(sender);
        }
        PauseLayer::onQuit(sender);
    }
};

class $modify(NoclipBaseLayer, GJBaseGameLayer) {
#ifdef GEODE_IS_MACOS
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        processNoclipAccuracyFrame(PlayLayer::get());
    }
#else
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        processNoclipAccuracyFrame(PlayLayer::get());
    }
#endif
};
