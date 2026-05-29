#include "ToastyReplay.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

namespace {
    bool shouldDisableShaders() {
        return ReplayEngine::get()->disableShaders;
    }

    void resetShaderLayer(GJBaseGameLayer* layer) {
        if (layer && layer->m_shaderLayer) {
            layer->m_shaderLayer->resetAllShaders();
        }
    }
}

class $modify(DisableShadersBaseLayer, GJBaseGameLayer) {
    void triggerShaderCommand(ShaderGameObject* object) {
        if (shouldDisableShaders()) {
            resetShaderLayer(this);
            return;
        }

        GJBaseGameLayer::triggerShaderCommand(object);
    }

    void updateShaderLayer(float dt) {
        if (shouldDisableShaders()) {
            resetShaderLayer(this);
            return;
        }

        GJBaseGameLayer::updateShaderLayer(dt);
    }
};
