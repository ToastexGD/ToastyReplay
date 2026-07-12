#include "core/gameplay_layer.hpp"
#include "ToastyReplay.hpp"

#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/Bindings.hpp>

using namespace geode::prelude;

namespace {
    void onEditorPlaytestStart(LevelEditorLayer* editor) {
        if (!editor || !editor->m_level) return;

        auto* engine = ReplayEngine::get();
        if (!engine) return;

        if (engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart) {
            engine->requestExecutionStart();
        }
    }

    void onEditorPlaytestStop(LevelEditorLayer* editor) {
        static_cast<void>(editor);
        auto* engine = ReplayEngine::get();
        if (!engine) return;

        if (engine->engineMode == MODE_CAPTURE) {
            engine->discardActiveMacro();
            engine->engineMode = MODE_DISABLED;
        }
        if (engine->engineMode == MODE_EXECUTE || engine->pendingPlaybackStart) {
            engine->haltExecution();
            engine->clearStartPosWarning();
        }
    }
}

class $modify(ToastyReplayEditorPlaytest, LevelEditorLayer) {
    void onPlaytest() {
        LevelEditorLayer::onPlaytest();
        toasty::gameplay::setEditorPlaytestActive(true);
        onEditorPlaytestStart(this);
    }

    void onStopPlaytest() {
        onEditorPlaytestStop(this);
        toasty::gameplay::setEditorPlaytestActive(false);
        LevelEditorLayer::onStopPlaytest();
    }
};
