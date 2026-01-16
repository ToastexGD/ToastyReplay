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

const std::set<int> gamemodePortalIDs = {
    12,
    13,
    47,
    111,
    660,
    745,
    1331,
    1933
};

const std::set<int> gravityPortalIDs = {
    10,
    11,
    2926
};

const std::set<int> speedPortalIDs = {
    200,
    201,
    202,
    203,
    1334
};

const std::set<int> sizePortalIDs = {
    99,
    101
};

const std::set<int> mirrorPortalIDs = {
    45,
    46
};

const std::set<int> dualPortalIDs = {
    286,
    287
};

const std::set<int> teleportPortalIDs = {
    747
};

const std::set<int> ringIDs = {
    36,
    84,
    141,
    1022,
    1330,
    1704,
    1751,
    3004,
    3005,
    3027
};

const std::set<int> padIDs = {
    35,
    67,
    140,
    1332,
    1333,
    3016
};

const std::set<int> dashOrbIDs = {
    1704,
    3005,
    3004
};

const std::set<int> trajectoryObjectTypes = {
    0,
    5,
    7,
};

const std::set<int> collectibleIDs = {
    1329,
    142,
    1614,
    1587,
    1275
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
