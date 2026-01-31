#include "ToastyReplay.hpp"
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <imgui-cocos.hpp>
using namespace geode::prelude;

class $modify(TickControlPlayLayer, PlayLayer) {
    void update(float dt) {
        ReplayEngine::get()->processHotkeys();
        PlayLayer::update(dt);
    }

    void updateVisibility(float dt) {
        auto* engine = ReplayEngine::get();
        if (!engine->renderingDisabled) {
            PlayLayer::updateVisibility(dt);
        }
    }
};

class $modify(SpeedControlScheduler, CCScheduler) {
    void update(float dt) {
        auto* engine = ReplayEngine::get();
        float scaledDelta = dt * engine->gameSpeed;
        return CCScheduler::update(scaledDelta);
    }
};

class $modify(PhysicsControlLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto* engine = ReplayEngine::get();

        float targetDelta = 1.0f / engine->tickRate;
        engine->tickAccumulator += dt;

        if (engine->tickStepping) {
            engine->tickAccumulator = 0.0f;

            if (engine->singleTickStep) {
                engine->singleTickStep = false;
                if (engine->collisionBypass) engine->totalTickCount++;
                GJBaseGameLayer::update(targetDelta);
            }
            return;
        }

        while (engine->tickAccumulator >= targetDelta) {
            engine->tickAccumulator -= targetDelta;

            bool shouldRender = (engine->tickAccumulator < targetDelta);
            engine->renderingDisabled = !shouldRender;

            if (engine->collisionBypass) engine->totalTickCount++;
            GJBaseGameLayer::update(targetDelta);
        }

        engine->renderingDisabled = false;
    }

    float getModifiedDelta(float dt) {
        GJBaseGameLayer::getModifiedDelta(dt);
        return 1.0f / ReplayEngine::get()->tickRate;
    }
};
