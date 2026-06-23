#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "hacks/autoclicker.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <chrono>
using namespace geode::prelude;

namespace {
    std::chrono::steady_clock::time_point g_nextRealtimeClickAudio{};

    bool isClickAudioButton(int actionType) {
        if (actionType == static_cast<int>(PlayerButton::Jump)) {
            return true;
        }

        if (actionType != static_cast<int>(PlayerButton::Left) &&
            actionType != static_cast<int>(PlayerButton::Right)) {
            return false;
        }

        if (ClickSoundManager::get()->muteLeftRightClicks) {
            return false;
        }

        auto* playLayer = PlayLayer::get();
        return playLayer &&
            playLayer->m_levelSettings &&
            playLayer->m_levelSettings->m_platformerMode;
    }

    bool shouldThrottleRealtimeClickAudio(ReplayEngine const* engine) {
        if (!engine) {
            return false;
        }

        auto* ac = Autoclicker::get();
        if (ac->enabled && ac->isTimedMode() && engine->engineMode != MODE_EXECUTE && ac->timedClicksPerSecond() > 1000.0f) {
            return true;
        }

        return false;
    }
}

void ReplayEngine::triggerAudio(bool secondPlayer, int actionType, bool pressed) {
    auto* csm = ClickSoundManager::get();
    if (!csm->enabled) return;
    if (!isClickAudioButton(actionType)) return;
    if (simulatingPath) return;
    if (engineMode == MODE_EXECUTE && !csm->playDuringPlayback) return;

    auto now = std::chrono::steady_clock::now();
    if (shouldThrottleRealtimeClickAudio(this)) {
        if (now < g_nextRealtimeClickAudio) {
            return;
        }
        g_nextRealtimeClickAudio = now + std::chrono::microseconds(1000);
    } else {
        g_nextRealtimeClickAudio = now;
    }

    csm->playClick(pressed, secondPlayer);
}

class $modify(ClickPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        if (!this) return;
        auto* csm = ClickSoundManager::get();
        if (csm && csm->enabled && csm->backgroundNoiseEnabled)
            csm->startBackgroundNoise();
    }

    void onQuit() {
        auto* csm = ClickSoundManager::get();
        csm->clearPendingClicks();
        csm->stopBackgroundNoise();
        PlayLayer::onQuit();
    }

    void resetLevel() {
        auto* csm = ClickSoundManager::get();
        csm->clearPendingClicks();
        csm->stopBackgroundNoise();
        PlayLayer::resetLevel();
        if (csm->enabled && csm->backgroundNoiseEnabled)
            csm->startBackgroundNoise();
    }
};
