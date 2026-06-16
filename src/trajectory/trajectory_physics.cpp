#include "trajectory_physics.hpp"

#include "trajectory.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace {
    struct ActivationState {
        bool activated;
        bool activatedByPlayer1;
        bool activatedByPlayer2;
        bool isActivated;
        bool isDisabled;
        bool isDisabled2;
        bool hasNoEffects;
    };

    struct ActivationScope {
        EffectGameObject* object = nullptr;
        ActivationState state {};

        explicit ActivationScope(EffectGameObject* target) : object(target) {
            if (!object) {
                return;
            }

            state = {
                object->m_activated,
                object->m_activatedByPlayer1,
                object->m_activatedByPlayer2,
                object->m_isActivated,
                object->m_isDisabled,
                object->m_isDisabled2,
                object->m_hasNoEffects
            };
            object->m_hasNoEffects = true;
        }

        ~ActivationScope() {
            if (!object) {
                return;
            }

            object->m_activated = state.activated;
            object->m_activatedByPlayer1 = state.activatedByPlayer1;
            object->m_activatedByPlayer2 = state.activatedByPlayer2;
            object->m_isActivated = state.isActivated;
            object->m_isDisabled = state.isDisabled;
            object->m_isDisabled2 = state.isDisabled2;
            object->m_hasNoEffects = state.hasNoEffects;
        }
    };

    std::unordered_map<PlayerObject*, std::unordered_set<uintptr_t>> g_activatedObjects;
    thread_local bool g_callingNative = false;

    struct NativeCallScope {
        bool previous;

        NativeCallScope() : previous(g_callingNative) {
            g_callingNative = true;
        }

        ~NativeCallScope() {
            g_callingNative = previous;
        }
    };

    static bool ignoresActivationHistory(PlayerObject* player, EffectGameObject* object) {
        if (!player || !object) {
            return false;
        }

        if (!player->m_isPlatformer && object->m_isMultiActivate) {
            return true;
        }

        return player->m_isPlatformer && object->m_isNoMultiActivate;
    }

    static bool objectActivatedForRealPlayer(PlayerObject* player, EffectGameObject* object) {
        if (!player || !object || ignoresActivationHistory(player, object)) {
            return false;
        }

        return player->m_isSecondPlayer ? object->m_activatedByPlayer2 : object->m_activatedByPlayer1;
    }

    static bool shouldSkipObject(GameObject* object) {
        if (!object) {
            return true;
        }

        return object->m_objectType == GameObjectType::Decoration
            || object->m_objectType == GameObjectType::CollisionObject
            || object->m_objectType == GameObjectType::SecretCoin
            || object->m_objectType == GameObjectType::UserCoin
            || object->m_objectType == GameObjectType::Collectible
            || object->m_isGroupDisabled
            || object->m_isDisabled
            || object->m_objectID == 286
            || object->m_objectID == 287;
    }

    static bool isNativeCollisionObject(GameObjectType type) {
        return type == GameObjectType::Solid
            || type == GameObjectType::Breakable
            || type == GameObjectType::Hazard
            || type == GameObjectType::AnimatedHazard
            || type == GameObjectType::Slope;
    }

    static bool isRing(GameObjectType type) {
        switch (type) {
            case GameObjectType::YellowJumpRing:
            case GameObjectType::PinkJumpRing:
            case GameObjectType::GravityRing:
            case GameObjectType::GreenRing:
            case GameObjectType::RedJumpRing:
            case GameObjectType::DropRing:
            case GameObjectType::DashRing:
            case GameObjectType::GravityDashRing:
            case GameObjectType::SpiderOrb:
            case GameObjectType::CustomRing:
            case GameObjectType::TeleportOrb:
                return true;
            default:
                return false;
        }
    }

    static bool isPad(GameObjectType type) {
        switch (type) {
            case GameObjectType::YellowJumpPad:
            case GameObjectType::PinkJumpPad:
            case GameObjectType::RedJumpPad:
            case GameObjectType::GravityPad:
            case GameObjectType::SpiderPad:
                return true;
            default:
                return false;
        }
    }

    static constexpr std::array<GameObjectType, 8> kModePortalTypes = {
        GameObjectType::CubePortal,
        GameObjectType::ShipPortal,
        GameObjectType::BallPortal,
        GameObjectType::UfoPortal,
        GameObjectType::WavePortal,
        GameObjectType::SpiderPortal,
        GameObjectType::SwingPortal,
        GameObjectType::RobotPortal
    };

    static bool isModePortal(GameObjectType type) {
        for (auto t : kModePortalTypes) {
            if (t == type) return true;
        }
        return false;
    }

    static constexpr std::array<std::pair<int, float>, 5> kSpeedPortalTable = {{
        {200, 0.7f},
        {201, 0.9f},
        {202, 1.1f},
        {203, 1.3f},
        {1334, 1.6f}
    }};

    static std::optional<float> lookupSpeedPortalSpeed(int objectID) {
        for (auto [id, speed] : kSpeedPortalTable) {
            if (id == objectID) return speed;
        }
        return std::nullopt;
    }

    static bool overlapsPlayer(GJBaseGameLayer* layer, PlayerObject* player, GameObject* object) {
        if (!layer || !player || !object) {
            return false;
        }

        cocos2d::CCRect objectRect = object->m_objectType == GameObjectType::Slope
            ? object->getObjectRect(2.0f, 2.0f)
            : object->getObjectRect();

        bool overlaps = object->m_objectRadius <= 0.0f
            ? player->getObjectRect().intersectsRect(objectRect)
            : layer->GJBaseGameLayer::playerCircleCollision(player, object);
        if (!overlaps) {
            return false;
        }

        if (object->m_shouldUseOuterOb
            && (!layer->m_levelSettings || !layer->m_levelSettings->m_fixRadiusCollision || object->m_objectRadius <= 0.0f)) {
            auto* objectBox = object->getOrientedBox();
            player->updateOrientedBox();
            auto* playerBox = static_cast<GameObject*>(player)->getOrientedBox();
            return objectBox && playerBox && objectBox->overlaps1Way(playerBox);
        }

        return true;
    }

    static void setLastActivation(PlayerObject* player, EffectGameObject* object) {
        if (!player || !object) {
            return;
        }

        player->m_lastPortalPos = object->getPosition();
        player->m_lastActivatedPortal = object;
        toasty::trajectory::physics::rememberActivation(player, object);
    }

    enum class VehicleMode {
        Cube,
        Ship,
        Ball,
        Ufo,
        Wave,
        Spider,
        Swing,
        Robot
    };

    static void clearAllVehicleFlags(PlayerObject* player) {
        player->m_isShip = false;
        player->m_isBird = false;
        player->m_isBall = false;
        player->m_isDart = false;
        player->m_isRobot = false;
        player->m_isSpider = false;
        player->m_isSwing = false;
    }

    static void setMode(PlayerObject* player, VehicleMode mode) {
        clearAllVehicleFlags(player);
        switch (mode) {
            case VehicleMode::Cube:
                player->switchedToMode(GameObjectType::CubePortal);
                break;
            case VehicleMode::Ship:
                player->toggleFlyMode(true, true);
                break;
            case VehicleMode::Ball:
                player->toggleRollMode(true, true);
                break;
            case VehicleMode::Ufo:
                player->toggleBirdMode(true, true);
                break;
            case VehicleMode::Wave:
                player->toggleDartMode(true, true);
                break;
            case VehicleMode::Spider:
                player->toggleSpiderMode(true, true);
                break;
            case VehicleMode::Swing:
                player->toggleSwingMode(true, true);
                break;
            case VehicleMode::Robot:
                player->toggleRobotMode(true, true);
                break;
        }
    }

    static VehicleMode vehicleModeForPortalType(GameObjectType type) {
        switch (type) {
            case GameObjectType::ShipPortal:   return VehicleMode::Ship;
            case GameObjectType::BallPortal:   return VehicleMode::Ball;
            case GameObjectType::UfoPortal:    return VehicleMode::Ufo;
            case GameObjectType::WavePortal:   return VehicleMode::Wave;
            case GameObjectType::SpiderPortal: return VehicleMode::Spider;
            case GameObjectType::SwingPortal:  return VehicleMode::Swing;
            case GameObjectType::RobotPortal:  return VehicleMode::Robot;
            default:                           return VehicleMode::Cube;
        }
    }

    static void activateModePortal(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* portal) {
        if (!layer || !player || !portal || !toasty::trajectory::physics::canActivate(player, portal)) {
            return;
        }

        auto position = player->getPosition();
        layer->GJBaseGameLayer::playerWillSwitchMode(player, portal);

        setMode(player, vehicleModeForPortalType(portal->m_objectType));

        player->setPosition(position);
        if (player->m_iconSprite) {
            player->m_iconSprite->setPosition(position);
        }
        if (player->m_vehicleSprite) {
            player->m_vehicleSprite->setPosition(position);
        }
        setLastActivation(player, portal);
    }

    static void activateGravityPad(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* pad) {
        if (!layer || !player || !pad || !toasty::trajectory::physics::canActivate(player, pad)) {
            return;
        }

        bool facingDown = player->m_isSideways ? pad->isFacingLeft() : pad->isFacingDown();
        if (player->m_isUpsideDown != facingDown) {
            return;
        }

        ActivationScope scope(pad);
        if (pad->m_isReverse) {
            player->reversePlayer(pad);
        }
        setLastActivation(player, pad);
        NativeCallScope nativeCall;
        layer->GJBaseGameLayer::gravBumpPlayer(player, pad);
        player->m_padRingRelated = true;
    }

    static void activatePad(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* pad) {
        if (!layer || !player || !pad || !toasty::trajectory::physics::canActivate(player, pad)) {
            return;
        }

        if (pad->m_objectType == GameObjectType::GravityPad) {
            activateGravityPad(layer, player, pad);
            return;
        }

        ActivationScope scope(pad);
        setLastActivation(player, pad);
        NativeCallScope nativeCall;
        layer->GJBaseGameLayer::bumpPlayer(player, pad);
    }

    static void activateRing(GJBaseGameLayer* layer, PlayerObject* player, RingObject* ring) {
        if (!layer || !player || !ring || !toasty::trajectory::physics::canActivate(player, ring)) {
            return;
        }

        if (player->m_touchingRings && !player->m_touchingRings->containsObject(ring)) {
            player->m_touchingRings->addObject(ring);
        }
        player->m_touchedRings.insert(ring->m_uniqueID);

        if (player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing || ring->m_isSpawnOnly) {
            return;
        }

        toasty::trajectory::physics::ringJump(player, ring);
    }

    static void activateSpecialObject(GJBaseGameLayer* layer, PlayerObject* player, EffectGameObject* object) {
        if (!layer || !player || !object) {
            return;
        }

        switch (object->m_objectID) {
            case 1859:
                player->m_stateHitHead = 2;
                break;
            case 1755:
                player->m_stateDartSlide = 2;
                break;
            case 1813:
                player->m_stateNoAutoJump = 2;
                break;
            case 1829:
                if (player->m_isDashing) {
                    toasty::trajectory::physics::stopDashing(player);
                    player->m_jumpBuffered = false;
                }
                break;
            case 2866:
                player->m_stateFlipGravity = 2;
                break;
            case 2069:
            case 3645: {
                auto* forceBlock = static_cast<ForceBlockGameObject*>(object);
                int forceID = forceBlock->m_forceID;
                if (forceID > 0) {
                    auto it = player->m_jumpPadRelated.find(forceID);
                    if (it != player->m_jumpPadRelated.end() && it->second) {
                        break;
                    }
                    player->m_jumpPadRelated.insert({ forceID, true });
                }

                player->m_stateForce = 2;
                player->m_stateForceVector += forceBlock->calculateForceToTarget(player);
                setLastActivation(player, object);
                break;
            }
            default:
                break;
        }
    }

    static void processInteractiveObject(GJBaseGameLayer* layer, PlayerObject* player, GameObject* object) {
        if (!layer || !player || !object || !overlapsPlayer(layer, player, object)) {
            return;
        }

        auto* effect = static_cast<EffectGameObject*>(object);
        switch (object->m_objectType) {
            case GameObjectType::InverseGravityPortal:
                if (toasty::trajectory::physics::canActivate(player, effect)) {
                    setLastActivation(player, effect);
                    toasty::trajectory::physics::flipGravity(layer, player, true);
                }
                break;
            case GameObjectType::NormalGravityPortal:
                if (toasty::trajectory::physics::canActivate(player, effect)) {
                    setLastActivation(player, effect);
                    toasty::trajectory::physics::flipGravity(layer, player, false);
                }
                break;
            case GameObjectType::GravityTogglePortal:
                if (toasty::trajectory::physics::canActivate(player, effect)) {
                    setLastActivation(player, effect);
                    toasty::trajectory::physics::flipGravity(layer, player, !player->m_isUpsideDown);
                }
                break;
            case GameObjectType::TeleportPortal:
                if (toasty::trajectory::physics::canActivate(player, effect)) {
                    toasty::trajectory::physics::teleportPlayer(layer, static_cast<TeleportPortalObject*>(object), player);
                    setLastActivation(player, effect);
                }
                break;
            case GameObjectType::MiniSizePortal:
                if (toasty::trajectory::physics::canActivate(player, effect)) {
                    player->togglePlayerScale(true, true);
                    player->updatePlayerScale();
                    setLastActivation(player, effect);
                }
                break;
            case GameObjectType::RegularSizePortal:
                if (toasty::trajectory::physics::canActivate(player, effect)) {
                    player->togglePlayerScale(false, true);
                    player->updatePlayerScale();
                    setLastActivation(player, effect);
                }
                break;
            case GameObjectType::Special:
                activateSpecialObject(layer, player, effect);
                break;
            case GameObjectType::Modifier:
            case GameObjectType::EnterEffectObject:
                if (effect->m_isTouchTriggered) {
                    toasty::trajectory::physics::triggerObject(effect, layer, player);
                }
                break;
            default: {
                auto speedOpt = lookupSpeedPortalSpeed(object->m_objectID);
                if (speedOpt.has_value() && toasty::trajectory::physics::canActivate(player, effect)) {
                    player->updateTimeMod(speedOpt.value(), true);
                    setLastActivation(player, effect);
                } else if (isRing(object->m_objectType)) {
                    activateRing(layer, player, static_cast<RingObject*>(object));
                } else if (isPad(object->m_objectType)) {
                    activatePad(layer, player, effect);
                } else if (isModePortal(object->m_objectType)) {
                    activateModePortal(layer, player, effect);
                }
                break;
            }
        }
    }
}

namespace toasty::trajectory::physics {
    bool isCallingNative() {
        return g_callingNative;
    }

    void clearSimulationState() {
        g_activatedObjects.clear();
    }

    bool hasActivated(PlayerObject* player, EffectGameObject* object) {
        if (!player || !object || ignoresActivationHistory(player, object)) {
            return false;
        }

        auto playerIt = g_activatedObjects.find(player);
        if (playerIt == g_activatedObjects.end()) {
            return false;
        }

        return playerIt->second.contains(reinterpret_cast<uintptr_t>(object));
    }

    void rememberActivation(PlayerObject* player, EffectGameObject* object) {
        if (!player || !object || ignoresActivationHistory(player, object)) {
            return;
        }

        g_activatedObjects[player].insert(reinterpret_cast<uintptr_t>(object));
    }

    bool canActivate(PlayerObject* player, EffectGameObject* object) {
        if (!player || !object) {
            return false;
        }

        if (!TrajectoryPredictionService::get().ownsPreviewPlayer(player)) {
            return false;
        }

        if (hasActivated(player, object) || objectActivatedForRealPlayer(player, object)) {
            return false;
        }

        return true;
    }

    void collisionCheckObjects(
        GJBaseGameLayer* layer,
        PlayerObject* player,
        gd::vector<GameObject*>* objects,
        int objectCount,
        float dt
    ) {
        if (!layer || !player || !objects || objectCount <= 0) {
            return;
        }

        gd::vector<GameObject*> nativeSingle;
        nativeSingle.reserve(1);

        for (int index = 0; index < objectCount; ++index) {
            auto* object = (*objects)[index];
            if (shouldSkipObject(object)) {
                continue;
            }

            if (isNativeCollisionObject(object->m_objectType)) {
                if (object->m_objectType == GameObjectType::Breakable) {
                    continue;
                }
                nativeSingle.clear();
                nativeSingle.push_back(object);
                NativeCallScope nativeCall;
                layer->GJBaseGameLayer::collisionCheckObjects(player, &nativeSingle, 1, dt);
                if (TrajectoryPredictionService::get().isTraceCancelled()) {
                    return;
                }
                continue;
            }

            processInteractiveObject(layer, player, object);
            if (TrajectoryPredictionService::get().isTraceCancelled()) {
                return;
            }
        }
    }

    void triggerObject(EffectGameObject* object, GJBaseGameLayer* layer, PlayerObject* player) {
        if (!object || !layer || !player || !canActivate(player, object)) {
            return;
        }

        auto speedOpt = lookupSpeedPortalSpeed(object->m_objectID);
        if (speedOpt.has_value()) {
            player->updateTimeMod(speedOpt.value(), true);
        } else {
            switch (object->m_objectID) {
                case 2066:
                    if (!object->m_followCPP
                        && ((!object->m_targetPlayer2 && !player->m_isSecondPlayer)
                            || (object->m_targetPlayer2 && player->m_isSecondPlayer))) {
                        player->m_gravityMod = object->m_gravityValue;
                    }
                    break;
                case 2900: {
                    auto* rotateObject = static_cast<RotateGameplayGameObject*>(object);
                    player->rotateGameplay(
                        rotateObject->m_moveDirection,
                        rotateObject->m_groundDirection,
                        rotateObject->m_editVelocity,
                        rotateObject->m_velocityModX,
                        rotateObject->m_velocityModY,
                        rotateObject->m_overrideVelocity,
                        rotateObject->m_dontSlide
                    );
                    break;
                }
                case 3022:
                    teleportPlayer(layer, static_cast<TeleportPortalObject*>(object), player);
                    break;
                default:
                    return;
            }
        }

        rememberActivation(player, object);
    }

    void checkSpawnObjects(GJBaseGameLayer* layer, PlayerObject* player) {
        if (!layer || !player || !layer->m_spawnObjects) {
            return;
        }

        auto* objects = static_cast<cocos2d::CCArray*>(
            layer->m_spawnObjects->objectForKey(layer->m_gameState.m_currentChannel)
        );
        if (!objects) {
            return;
        }

        int startIndex = layer->m_gameState.m_spawnChannelRelated0[layer->m_gameState.m_currentChannel];
        bool goingBack = layer->m_gameState.m_spawnChannelRelated1[layer->m_gameState.m_currentChannel];
        auto playerPosition = player->getPosition();

        for (int index = std::max(0, startIndex); static_cast<unsigned int>(index) < objects->count(); ++index) {
            auto* object = static_cast<SpawnTriggerGameObject*>(objects->objectAtIndex(static_cast<unsigned int>(index)));
            if (!object) {
                continue;
            }

            if (object->m_objectID != 2066 && object->m_objectID != 2900 && object->m_objectID != 3022 && !lookupSpeedPortalSpeed(object->m_objectID).has_value()) {
                continue;
            }

            auto objectPosition = object->m_speedStart;
            if (player->m_isSideways) {
                if ((goingBack && objectPosition.y < playerPosition.y) || (!goingBack && objectPosition.y > playerPosition.y)) {
                    break;
                }
            } else if ((goingBack && objectPosition.x < playerPosition.x) || (!goingBack && objectPosition.x > playerPosition.x)) {
                break;
            }

            if (!object->m_isGroupDisabled && !object->m_isTouchTriggered) {
                triggerObject(object, layer, player);
            }
        }
    }

    void ringJump(PlayerObject* player, RingObject* ring) {
        if (!player || !ring) {
            return;
        }

        ActivationScope scope(ring);
        NativeCallScope nativeCall;
        player->PlayerObject::ringJump(ring, true);
        rememberActivation(player, ring);
    }

    void bumpPlayer(PlayerObject* player, float force, int objectType, bool noEffects, GameObject* object) {
        if (!player) {
            return;
        }

        auto* effectObject = typeinfo_cast<EffectGameObject*>(object);
        ActivationScope scope(effectObject);
        NativeCallScope nativeCall;
        player->PlayerObject::bumpPlayer(force, objectType, true, object);
        if (effectObject) {
            rememberActivation(player, effectObject);
        }
    }

    void propellPlayer(PlayerObject* player, float force, bool noEffects, int objectType) {
        if (!player) {
            return;
        }

        NativeCallScope nativeCall;
        player->PlayerObject::propellPlayer(force, true, objectType);
    }

    void startDashing(PlayerObject* player, DashRingObject* object) {
        if (!player || !object) {
            return;
        }

        ActivationScope scope(object);
        NativeCallScope nativeCall;
        player->PlayerObject::startDashing(object);
        rememberActivation(player, object);
    }

    void stopDashing(PlayerObject* player) {
        if (!player) {
            return;
        }

        NativeCallScope nativeCall;
        player->PlayerObject::stopDashing();
    }

    void teleportPlayer(GJBaseGameLayer* layer, TeleportPortalObject* object, PlayerObject* player) {
        if (!layer || !object || !player) {
            return;
        }

        ActivationScope scope(object);
        NativeCallScope nativeCall;
        layer->GJBaseGameLayer::teleportPlayer(object, player);
        rememberActivation(player, object);
    }

    void flipGravity(GJBaseGameLayer* layer, PlayerObject* player, bool gravity) {
        if (!layer || !player || player->m_isUpsideDown == gravity) {
            return;
        }

        NativeCallScope nativeCall;
        layer->GJBaseGameLayer::flipGravity(player, gravity, true);
    }
}
