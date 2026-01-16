#pragma once

#include <Geode/Geode.hpp>
#include <set>

using namespace geode::prelude;

class TrajectoryNode : public CCDrawNode {
public:
    static TrajectoryNode* create() {
        auto ret = new TrajectoryNode();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// Object IDs that should be interacted with during trajectory
const std::set<int> gamemodePortalIDs = {
    12,   // Cube portal
    13,   // Ship portal
    47,   // Ball portal
    111,  // UFO portal
    660,  // Wave portal
    745,  // Robot portal
    1331, // Spider portal
    1933  // Swing portal
};

const std::set<int> gravityPortalIDs = {
    10,   // Blue gravity portal (normal)
    11,   // Yellow gravity portal (flipped)
    2926  // Toggle gravity portal
};

const std::set<int> speedPortalIDs = {
    200,  // Half speed
    201,  // Normal speed
    202,  // Double speed
    203,  // Triple speed
    1334  // Quadruple speed
};

const std::set<int> sizePortalIDs = {
    99,   // Mini portal
    101   // Big portal
};

const std::set<int> mirrorPortalIDs = {
    45,   // Mirror portal on
    46    // Mirror portal off
};

const std::set<int> dualPortalIDs = {
    286,  // Dual portal on
    287   // Dual portal off
};

const std::set<int> teleportPortalIDs = {
    747   // Teleport portal
};

const std::set<int> ringIDs = {
    36,   // Yellow jump ring
    84,   // Pink jump ring
    141,  // Red jump ring
    1022, // Green ring
    1330, // Black ring (dash)
    1704, // Purple ring (spider)
    1751, // Rebound ring
    3004, // Green dash ring
    3005, // Pink dash ring
    3027  // Blue dash ring
};

const std::set<int> padIDs = {
    35,   // Yellow pad
    67,   // Pink pad
    140,  // Red pad
    1332, // Black pad (spider)
    1333, // Purple pad
    3016  // Blue pad
};

const std::set<int> dashOrbIDs = {
    1704, // Dash orb
    3005, // Pink dash ring
    3004  // Green dash ring
};

// Object types that should allow collision during trajectory
const std::set<int> trajectoryObjectTypes = {
    0,  // Solid
    5,  // Slope
    7,  // Hazard (for death detection)
};

// Collectibles to ignore
const std::set<int> collectibleIDs = {
    1329, // User coin
    142,  // Secret coin
    1614, // Custom orb
    1587, // Item trigger
    1275  // Pickup trigger
};

class ShowTrajectory {
public:
    static ShowTrajectory& get() {
        static ShowTrajectory instance;
        return instance;
    }

    bool creatingTrajectory = false;
    bool cancelTrajectory = false;
    
    PlayerObject* fakePlayer1 = nullptr;
    PlayerObject* fakePlayer2 = nullptr;
    
    cocos2d::ccColor4F color1 = {0.0f, 1.0f, 0.0f, 1.0f};
    cocos2d::ccColor4F color2 = {1.0f, 0.0f, 0.0f, 1.0f};
    cocos2d::ccColor4F color3 = {1.0f, 1.0f, 0.0f, 1.0f};
    
    int length = 240;
    float delta = 1.0f / 240.0f;
    float deathRotation = 0.0f;
    
    cocos2d::CCPoint player1Trajectory[2000];
    cocos2d::CCPoint player2Trajectory[2000];
    
    // Track what objects have been activated during trajectory
    std::set<GameObject*> activatedObjects;
    
    static void trajectoryOff();
    static void updateTrajectory(PlayLayer* pl);
    static void createTrajectory(PlayLayer* pl, PlayerObject* fakePlayer, PlayerObject* realPlayer, bool hold, bool inverted = false);
    static void drawPlayerHitbox(PlayerObject* player, CCDrawNode* drawNode);
    static std::vector<cocos2d::CCPoint> getVertices(PlayerObject* player, cocos2d::CCRect rect, float rotation);
    static void updateMergedColor();
    
    static void handlePortal(PlayerObject* player, GameObject* obj);
    static void handleRing(PlayerObject* player, RingObject* ring, PlayLayer* pl);
    static void handlePad(PlayerObject* player, GameObject* pad);
    static void handleDash(PlayerObject* player, DashRingObject* dashRing);
    
    static bool shouldInteractWithObject(GameObject* obj);
    static bool isGamemodePortal(int id);
    static bool isGravityPortal(int id);
    static bool isSpeedPortal(int id);
    static bool isSizePortal(int id);
    static bool isRing(int id);
    static bool isPad(int id);
    static bool isDashOrb(int id);
    
    static cocos2d::CCDrawNode* trajectoryNode();
    
private:
    ShowTrajectory() = default;
};
