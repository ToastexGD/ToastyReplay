#include "ToastyReplay.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

static bool shouldBlockMirror() {
    auto* engine = ReplayEngine::get();
    if (!engine->noMirrorEffect) return false;

    if (engine->noMirrorRecordingOnly) {
        return engine->engineMode == MODE_CAPTURE;
    }

    return true;
}

class $modify(NoMirrorBaseLayer, GJBaseGameLayer) {
    void toggleFlipped(bool flip, bool noEffects) {
        if (shouldBlockMirror()) return;
        GJBaseGameLayer::toggleFlipped(flip, noEffects);
    }

    void setupLevelStart(LevelSettingsObject* settings) {
        GJBaseGameLayer::setupLevelStart(settings);

        if (!shouldBlockMirror()) return;
        if (!settings) return;

        
        if (settings->m_mirrorMode) {
            GJBaseGameLayer::toggleFlipped(false, true);
        }
    }
};
