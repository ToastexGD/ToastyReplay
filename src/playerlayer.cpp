#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "ToastyReplay.hpp"
#include "replay.hpp"

using namespace geode::prelude;

class $modify(ToastyPlayLayerHook, PlayLayer) {
    struct Fields {
        bool wasRecording = false;
        zReplay* savedReplay = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
            return false;
        }

        ToastyReplay* mgr = ToastyReplay::get();
        if (mgr && mgr->state == RECORD && mgr->currentReplay) {
            m_fields->wasRecording = true;
            m_fields->savedReplay = mgr->currentReplay;
        }

        return true;
    }

    void resetLevel() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Store recording state BEFORE reset
        bool shouldKeepRecording = (mgr && mgr->state == RECORD && mgr->currentReplay);
        zReplay* replayToKeep = shouldKeepRecording ? mgr->currentReplay : nullptr;
        
        PlayLayer::resetLevel();
        
        // Restore recording state AFTER reset
        if (shouldKeepRecording && mgr && replayToKeep) {
            mgr->state = RECORD;
            mgr->currentReplay = replayToKeep;
            // Clear inputs to start fresh
            mgr->currentReplay->inputs.clear();
        }
    }

    void onQuit() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Only save if we have inputs
        if (mgr && mgr->state == RECORD && mgr->currentReplay) {
            if (!mgr->currentReplay->inputs.empty()) {
                // Has inputs, save as unsaved
                if (mgr->lastUnsavedReplay) {
                    delete mgr->lastUnsavedReplay;
                }
                mgr->lastUnsavedReplay = mgr->currentReplay;
                mgr->currentReplay = nullptr;
            } else {
                // No inputs, just delete the empty replay
                delete mgr->currentReplay;
                mgr->currentReplay = nullptr;
            }
            mgr->state = NONE;
        }
        
        PlayLayer::onQuit();
    }
};
