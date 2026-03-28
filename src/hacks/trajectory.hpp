#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

struct TrajectoryMotionState {
    CCPoint position;
    CCPoint previousPosition;
    double verticalVelocity;
    double preSlopeVelocity;
    float rotation;
    float scale;
    float movementSpeed;
    float gravityFactor;
    double totalTime;
    GameObjectType objectType;
};

struct TrajectoryFormState {
    bool gravityInverted;
    bool onSlope;
    bool wasOnSlope;
    bool inShipMode;
    bool inUfoMode;
    bool inBallMode;
    bool inWaveMode;
    bool inRobotMode;
    bool inSpiderMode;
    bool inSwingMode;
    bool grounded;
    bool dashing;
    bool isGoingLeft;
    bool isSideways;
    int reverseRelated;
    double reverseSpeed;
    double reverseAcceleration;
};

struct TrajectoryInteractionState {
    bool padRingRelated;
    bool ringJumpRelated;
    gd::unordered_set<int> ringRelatedSet;
    bool touchedRing;
    bool touchedCustomRing;
    bool touchedPad;
    GameObject* lastActivatedPortal;
    CCPoint lastPortalPos;
    bool playEffects;
};

struct TrajectorySlopeState {
    GameObject* currentSlope;
    GameObject* currentSlopeSecondary;
    GameObject* currentPotentialSlope;
    float slopeAngle;
    float slopeAngleRadians;
    bool collidingWithSlope;
    int collidingWithSlopeId;
    bool slopeFlipGravityRelated;
    float slopeVelocity;
    double currentSlopeVelocity;
    bool currentSlopeTop;
    bool slopeSlideRotated;
    double slopeRotation;
    double slopeForce;
    bool upsideDownSlope;
    bool movingWithSlopeDirection;
    bool sliding;
    bool slidingRight;
    double slopeStartTime;
    double slopeEndTime;
};

struct TrajectoryCollisionState {
    GameObject* lastGroundObject;
    GameObject* preLastGroundObject;
    GameObject* collidedObject;
    GameObject* collidingWithLeft;
    GameObject* collidingWithRight;
    double groundYVelocity;
    int lastCollisionBottom;
    int lastCollisionTop;
    int lastCollisionLeft;
    int lastCollisionRight;
    bool isOnGround2;
    bool isOnGround3;
    bool isOnGround4;
    double fallSpeed;
    bool maybeColliding;
    double collidedTopMinY;
    double collidedBottomMaxY;
    double collidedLeftMaxX;
    double collidedRightMinX;
    double yVelocityRelated;
    double scaleXRelated2;
};

struct TrajectoryDynamicsState {
    double gravity;
    double yStart;
    double speedMultiplier;
    double accelerationOrSpeed;
    double snapDistance;
    double physDeltaRelated;
    double blackOrbRelated;
    double platformerXVelocity;
    double lastLandTime;
    double gameModeChangedTime;
    double lastJumpTime;
    double lastFlipTime;
    double lastSpiderFlipTime;
    double dashX;
    double dashY;
    double dashAngle;
    double dashStartTime;
    float rotationSpeed;
    float xVelocityRelated;
    float xVelocityRelated2;
    CCPoint shipRotation;
    CCPoint lastGroundedPos;
    GameObject* objectSnappedTo;
    bool jumpBuffered;
    bool wasJumpBuffered;
    bool wasRobotJump;
    unsigned char stateJumpBuffered;
    bool stateRingJump;
    bool stateRingJump2;
    bool touchedGravityPortal;
    bool isAccelerating;
    bool isOnIce;
    bool affectedByForces;
    int stateOnGround;
    int stateBoostX;
    int stateBoostY;
    int stateForce;
    CCPoint stateForceVector;
    int maybeStateForce2;
    int stateScale;
    int stateNoAutoJump;
    int stateDartSlide;
    int stateHitHead;
    int stateFlipGravity;
    unsigned char stateNoStickX;
    unsigned char stateNoStickY;
};

struct PlayerStateCapsule {
    TrajectoryMotionState motion;
    TrajectoryFormState form;
    TrajectoryInteractionState interaction;
    TrajectorySlopeState slope;
    TrajectoryCollisionState collision;
    TrajectoryDynamicsState dynamics;
};

struct PredictionWatchKey {
    CCPoint position;
    double verticalVelocity;
    float rotation;
    bool gravityInverted;
    float movementSpeed;
    bool grounded;
    float scale;
    bool dashing;
    bool inShipMode;
    bool inUfoMode;
    bool inBallMode;
    bool inWaveMode;
    bool inRobotMode;
    bool inSpiderMode;
    bool inSwingMode;
    bool isGoingLeft;
    bool isSideways;
    int reverseRelated;
};

struct PredictionContext {
    PlayerObject* previewPlayers[2] = { nullptr, nullptr };
    bool activeSimulation = false;
    bool traceCancelled = false;
    bool holdingTrace = false;
    bool processingOrbTouch = false;
    bool dirty = true;
    float stepDelta = 1.0f / 240.0f;
    float collisionRotation = 0.0f;
    std::array<CCPoint, 480> holdPathP1 {};
    std::array<CCPoint, 480> holdPathP2 {};
    PredictionWatchKey watchKeys[2] {};
    std::unordered_set<GameObject*> processedOrbs;
    std::unordered_set<GameObject*> touchingPads;
    std::unordered_set<GameObject*> frameTouchingPads;
};

class TrajectoryPredictionService {
public:
    static TrajectoryPredictionService& get();

    bool isActiveSimulation() const;
    bool isProcessingOrbTouch() const;
    void markDirty();
    void clearOverlay();
    void attach(PlayLayer* playLayer);
    void detach();
    void updatePreview(PlayLayer* playLayer);
    void captureFrameDelta(float dt);
    void noteSimulatedDeath(PlayerObject* player);
    bool ownsPreviewPlayer(PlayerObject* player) const;

    void simulateCollisionBatch(
        GJBaseGameLayer* layer,
        PlayerObject* player,
        gd::vector<GameObject*>* objects,
        int objectCount,
        float dt
    );
    bool handleActivationCheck(PlayerObject* player, EffectGameObject* object);
    void handleTouchedTrigger(PlayerObject* player, EffectGameObject* object);

private:
    PredictionContext m_context;
    cocos2d::CCDrawNode* m_drawNode = nullptr;
    cocos2d::ccColor4F m_holdColor = ccc4f(0.29f, 0.89f, 0.33f, 1.0f);
    cocos2d::ccColor4F m_holdColorP2 = ccc4f(0.20f, 0.50f, 0.95f, 1.0f);
    cocos2d::ccColor4F m_releaseColor = ccc4f(0.51f, 0.03f, 0.03f, 1.0f);
    cocos2d::ccColor4F m_overlapColor = ccc4f(1.0f, 1.0f, 0.0f, 1.0f);
    cocos2d::ccColor4F m_overlapColorP2 = ccc4f(0.6f, 0.75f, 1.0f, 1.0f);

    static bool isSimulatedPad(GameObjectType type);
    static bool isSimulatedOrb(GameObjectType type);
    static bool watchChanged(PredictionWatchKey const& lhs, PredictionWatchKey const& rhs);

    static PredictionWatchKey buildWatchKey(PlayerObject* player);
    static PlayerStateCapsule capturePlayerState(PlayerObject* player);
    static void applyPlayerState(PlayerObject* player, PlayerStateCapsule const& state);

    void rebuildPreview(PlayLayer* playLayer);
    void traceInputPath(PlayLayer* playLayer, PlayerObject* previewPlayer, PlayerObject* sourcePlayer, bool holdingInput);
    void drawPredictionBounds(PlayerObject* player);
    void recalculateOverlapColors();
    void applyPortalHint(PlayerObject* player, int portalId);
    cocos2d::CCDrawNode* ensureDrawNode();

    static std::vector<CCPoint> buildPlayerBounds(PlayerObject* player, CCRect bounds, float angle);
};
