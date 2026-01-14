#include "ToastyReplay.hpp"
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <imgui-cocos.hpp>
using namespace geode::prelude;

class $modify(PlayLayer) {
    void update(float dt) {
        // Handle keybinds in the PlayLayer update
        ToastyReplay::get()->handleKeybinds();

        PlayLayer::update(dt);
    }

    void updateVisibility(float dt) {
        if (ToastyReplay::get()->disableRender) {
            return;
        }
        PlayLayer::updateVisibility(dt);
    }
};

class $modify(CCScheduler) {
    void update(float dt) {
        ToastyReplay* mgr = ToastyReplay::get();
        return CCScheduler::update(dt * mgr->speed);
    }
};

class $modify(GJBaseGameLayer) {
    void update(float dt) {
        ToastyReplay* mgr = ToastyReplay::get();

        mgr->extraTPS += dt;
        float newDelta = 1.f / (mgr->tps);

        if (mgr->frameAdvance) {
            mgr->extraTPS = 0;
            if (!mgr->stepFrame) return; // Freeze when frame advance is on but stepFrame is false
            mgr->stepFrame = false;      // Set stepFrame to false after processing
            // Track frames for noclip accuracy
            if (mgr->noclip) mgr->noclipTotalFrames++;
            return GJBaseGameLayer::update(newDelta); // Advance one frame
        }

        if (mgr->extraTPS >= newDelta) {
            int times = std::floor(mgr->extraTPS / newDelta);
            mgr->extraTPS -= newDelta * times;

            mgr->disableRender = true;
            for (int i = 0; i < times - 1; i++) {
                // Track frames for noclip accuracy
                if (mgr->noclip) mgr->noclipTotalFrames++;
                GJBaseGameLayer::update(newDelta);
            }

            mgr->disableRender = false;
            // Track frames for noclip accuracy
            if (mgr->noclip) mgr->noclipTotalFrames++;
            return GJBaseGameLayer::update(newDelta);
        }
    }

    float getModifiedDelta(float dt) {
        GJBaseGameLayer::getModifiedDelta(dt);

        ToastyReplay* mgr = ToastyReplay::get();
        double newDelta = 1.f / mgr->tps;

        return newDelta;
    }
};
