#include "ToastyReplay.hpp"
#include <Geode/modify/FMODAudioEngine.hpp>
using namespace geode::prelude;

class $modify(AudioSpeedController, FMODAudioEngine) {
    struct Fields {
        float lastPitchValue = 1.f;
    };

    void update(float delta) {
        FMODAudioEngine::update(delta);

        auto* replay = ToastyReplay::get();
        float targetPitch = replay->speedHackAudio ? replay->speed : 1.f;

        if (targetPitch == m_fields->lastPitchValue) return;

        m_fields->lastPitchValue = targetPitch;

        FMOD::ChannelGroup* masterGroup = nullptr;
        if (m_system->getMasterChannelGroup(&masterGroup) != FMOD_OK) return;

        masterGroup->setPitch(targetPitch);
    }
};
