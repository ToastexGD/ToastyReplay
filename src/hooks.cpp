#include <Geode/Bindings.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "ToastyReplay.hpp"

using namespace geode::prelude;

class $modify(PlayLayer) {
    void resetLevel() {
        ToastyReplay* mgr = ToastyReplay::get();
        
        // If recording, keep the state but purge data up to current frame
        if (mgr && mgr->state == RECORD && mgr->currentReplay) {
            // Purge all inputs up to current frame to start fresh
            mgr->currentReplay->purgeAfter(0);
        }
        
        PlayLayer::resetLevel();
    }
    
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        
        ToastyReplay* mgr = ToastyReplay::get();
        
        // Keep recording state active even after death
        if (mgr && mgr->state == RECORD && mgr->currentReplay) {
            // Don't change state, just let it continue recording on next attempt
        }
    }
};
