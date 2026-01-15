#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "ToastyReplay.hpp"

using namespace geode::prelude;

class $modify(ToastyPlayLayer, PlayLayer) {
    void resetLevel() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Store the recording state before reset
        bool wasRecording = (mgr && mgr->state == RECORD);
        zReplay* recordingReplay = wasRecording ? mgr->currentReplay : nullptr;
        
        PlayLayer::resetLevel();
        
        // Restore recording state after reset
        if (wasRecording && mgr) {
            mgr->state = RECORD;
            mgr->currentReplay = recordingReplay;
            // Clear the replay data to start fresh
            if (mgr->currentReplay) {
                mgr->currentReplay->purgeAfter(0);
            }
        }
    }
    
    void onQuit() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Only create unsaved replay if we actually recorded something
        if (mgr && mgr->state == RECORD && mgr->currentReplay) {
            // Check if there are any inputs recorded
            bool hasInputs = !mgr->currentReplay->inputs.empty();
            
            if (hasInputs) {
                // Save as unsaved replay only if there were inputs
                if (mgr->lastUnsavedReplay) {
                    delete mgr->lastUnsavedReplay;
                }
                mgr->lastUnsavedReplay = mgr->currentReplay;
                mgr->currentReplay = nullptr;
            }
            
            mgr->state = NONE;
        }
        
        PlayLayer::onQuit();
    }
};
