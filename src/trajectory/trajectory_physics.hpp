#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace toasty::trajectory::physics {
    bool isCallingNative();
    void clearSimulationState();

    bool canActivate(PlayerObject* player, EffectGameObject* object);
    void rememberActivation(PlayerObject* player, EffectGameObject* object);
    bool hasActivated(PlayerObject* player, EffectGameObject* object);

    void collisionCheckObjects(
        GJBaseGameLayer* layer,
        PlayerObject* player,
        gd::vector<GameObject*>* objects,
        int objectCount,
        float dt
    );

    void checkSpawnObjects(GJBaseGameLayer* layer, PlayerObject* player);
    void triggerObject(EffectGameObject* object, GJBaseGameLayer* layer, PlayerObject* player);

    void ringJump(PlayerObject* player, RingObject* ring);
    void bumpPlayer(PlayerObject* player, float force, int objectType, bool noEffects, GameObject* object);
    void propellPlayer(PlayerObject* player, float force, bool noEffects, int objectType);
    void startDashing(PlayerObject* player, DashRingObject* object);
    void stopDashing(PlayerObject* player);
    void teleportPlayer(GJBaseGameLayer* layer, TeleportPortalObject* object, PlayerObject* player);
    void flipGravity(GJBaseGameLayer* layer, PlayerObject* player, bool gravity);
}
