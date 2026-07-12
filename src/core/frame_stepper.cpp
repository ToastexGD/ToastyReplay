#include "ToastyReplay.hpp"
#include "hacks/physicsbypass.hpp"
#include "hacks/autoclicker.hpp"
#include "trajectory/trajectory.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>

#include <algorithm>
#include <cmath>

namespace {
    static void clearNativeQueuedButtons(PlayLayer* playLayer) {
        if (playLayer) {
            playLayer->m_queuedButtons.clear();
        }
    }

    static void releasePlayerButtons(PlayLayer* playLayer) {
        if (!playLayer) {
            return;
        }
        if (playLayer->m_player1) {
            playLayer->m_player1->releaseAllButtons();
        }
        if (playLayer->m_player2) {
            playLayer->m_player2->releaseAllButtons();
        }
    }
}

void ReplayEngine::clearFrameStepState() {
    singleTickStep = false;
    frameStepMusicSeekPending = false;
}

int ReplayEngine::computeFrameStepMusicTimeMS(PlayLayer* playLayer) const {
    if (!playLayer || !playLayer->m_levelSettings) {
        return 0;
    }

    double seconds = static_cast<double>(playLayer->m_gameState.m_levelTime)
        + static_cast<double>(playLayer->m_levelSettings->m_songOffset);
    int musicTime = static_cast<int>(std::llround(seconds * 1000.0));

    if (auto* audio = FMODAudioEngine::sharedEngine()) {
        musicTime += audio->m_musicOffset;
    }

    return std::max(0, musicTime);
}

void ReplayEngine::requestFrameStepMusicSync(PlayLayer* playLayer) {
    frameStepPendingMusicTimeMs = computeFrameStepMusicTimeMS(playLayer);
    frameStepMusicSeekPending = true;
}

void ReplayEngine::syncFrameStepAudio(FMODAudioEngine* audio) {
    bool shouldControlAudio = audio
        && PlayLayer::get()
        && tickStepping
        && !renderer.recording;

    auto* musicGroup = audio ? audio->m_backgroundMusicChannel : nullptr;
    if (!shouldControlAudio || !musicGroup) {
        if (frameStepAudioActive && musicGroup) {
            musicGroup->setPaused(frameStepAudioWasPaused);
        }
        frameStepAudioActive = false;
        frameStepAudioWasPaused = false;
        frameStepMusicSeekPending = false;
        return;
    }

    if (!frameStepAudioActive) {
        bool wasPaused = false;
        musicGroup->getPaused(&wasPaused);
        frameStepAudioWasPaused = wasPaused;
        frameStepAudioActive = true;
        if (!frameStepMusicSeekPending) {
            requestFrameStepMusicSync(PlayLayer::get());
        }
    }

    if (frameStepMusicSeekPending) {
        audio->setMusicTimeMS(static_cast<unsigned int>(std::max(0, frameStepPendingMusicTimeMs)), true, 0);
        frameStepMusicSeekPending = false;
    }

    musicGroup->setPaused(true);
}

void ReplayEngine::setFrameStepEnabled(bool enabled, PlayLayer* playLayer) {
    tickStepping = enabled;
    singleTickStep = false;

    if (enabled) {
        if (playLayer && !renderer.recording) {
            requestFrameStepMusicSync(playLayer);
        } else {
            frameStepMusicSeekPending = false;
        }
    } else {
        frameStepMusicSeekPending = false;
        clearFrameStepState();
        if (playLayer) {
            clearNativeQueuedButtons(playLayer);
            clearQueuedSubstepState();
            clearQueuedMacroCommands();
            queuedCaptureCommands.clear();
            cbsCaptureProcessingQueue = false;
        }
        resetTimingTracking();
        if (playLayer && engineMode == MODE_DISABLED) {
            resetDeferredInputState();
            releasePlayerButtons(playLayer);
        }
    }

    syncFrameStepAudio(FMODAudioEngine::sharedEngine());
}

namespace {
    static bool playerWantsHold(PlayerObject* player) {
        if (!player) return false;
        return player->m_holdingButtons[1];
    }

    static bool playerCanReceiveLiveInput(PlayerObject* player) {
        if (!player) return false;
        if (player->m_isDead) return false;
        return true;
    }

    static void applyJumpHoldDelta(PlayerObject* player, bool desired) {
        if (!playerCanReceiveLiveInput(player)) {
            return;
        }

        bool currentlyHeld = playerWantsHold(player);
        if (desired == currentlyHeld) {
            return;
        }

        if (desired) {
            player->pushButton(PlayerButton::Jump);
        } else {
            player->releaseButton(PlayerButton::Jump);
        }
    }
}

void ReplayEngine::bridgeUserHoldsToPlayer(PlayLayer* playLayer) {
    if (!playLayer) {
        return;
    }
    if (engineMode == MODE_EXECUTE) {
        return;
    }
    if (TrajectoryPredictionService::get().isActiveSimulation()) {
        return;
    }

    auto* ac = Autoclicker::get();
    if (!ac || ac->isAutoclickerInput) {
        return;
    }

    applyJumpHoldDelta(playLayer->m_player1, ac->userHoldingP1);

    bool dualOrTwoPlayer = playLayer->m_levelSettings && (
        playLayer->m_levelSettings->m_twoPlayerMode ||
        playLayer->m_gameState.m_isDualMode
    );
    if (dualOrTwoPlayer) {
        applyJumpHoldDelta(playLayer->m_player2, ac->userHoldingP2);
    }
}
