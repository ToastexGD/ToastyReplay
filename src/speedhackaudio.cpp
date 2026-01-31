#include "ToastyReplay.hpp"
#include <Geode/modify/FMODAudioEngine.hpp>
using namespace geode::prelude;

class $modify(PitchControlAudio, FMODAudioEngine) {
    struct Fields {
        float cachedPitch = 1.f;
    };

    void update(float delta) {
        FMODAudioEngine::update(delta);

        auto* engine = ReplayEngine::get();
        float desiredPitch = engine->audioPitchEnabled ? engine->gameSpeed : 1.f;

        if (desiredPitch == m_fields->cachedPitch) return;

        m_fields->cachedPitch = desiredPitch;

        FMOD::ChannelGroup* masterChannel = nullptr;
        if (m_system->getMasterChannelGroup(&masterChannel) != FMOD_OK) return;

        masterChannel->setPitch(desiredPitch);
    }
};
