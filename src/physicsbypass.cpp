#include "ToastyReplay.hpp"
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <imgui-cocos.hpp>
using namespace geode::prelude;

class $modify(PlayLayer) {
    void update(float dt) {
        ToastyReplay::get()->handleKeybinds();
        PlayLayer::update(dt);
    }

    void updateVisibility(float dt) {
        auto* replay = ToastyReplay::get();
        if (!replay->disableRender) {
            PlayLayer::updateVisibility(dt);
        }
    }
};

class $modify(CCScheduler) {
    void update(float dt) {
        auto* replay = ToastyReplay::get();
        float adjustedDelta = dt * replay->speed;
        return CCScheduler::update(adjustedDelta);
    }
};

class $modify(GJBaseGameLayer) {
    void update(float dt) {
        auto* replay = ToastyReplay::get();

        float targetDelta = 1.0f / replay->tps;
        replay->extraTPS += dt;

        if (replay->frameAdvance) {
            replay->extraTPS = 0.0f;

            if (replay->stepFrame) {
                replay->stepFrame = false;
                if (replay->noclip) replay->noclipTotalFrames++;
                GJBaseGameLayer::update(targetDelta);
            }
            return;
        }

        while (replay->extraTPS >= targetDelta) {
            replay->extraTPS -= targetDelta;

            bool shouldRender = (replay->extraTPS < targetDelta);
            replay->disableRender = !shouldRender;

            if (replay->noclip) replay->noclipTotalFrames++;
            GJBaseGameLayer::update(targetDelta);
        }

        replay->disableRender = false;
    }

    float getModifiedDelta(float dt) {
        GJBaseGameLayer::getModifiedDelta(dt);
        return 1.0f / ToastyReplay::get()->tps;
    }
};
