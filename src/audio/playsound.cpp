#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
using namespace geode::prelude;

void ReplayEngine::triggerAudio(bool secondPlayer, int actionType, bool pressed) {
    auto* csm = ClickSoundManager::get();
    if (!csm->enabled) return;
    if (actionType != 1) return;
    if (engineMode == MODE_EXECUTE && !csm->playDuringPlayback) return;
    csm->playClick(pressed, secondPlayer);
}

class $modify(ClickPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        auto* csm = ClickSoundManager::get();
        if (csm->enabled && csm->backgroundNoiseEnabled)
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
