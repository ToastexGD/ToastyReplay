#ifndef _trajectory_hpp
#define _trajectory_hpp

#include <Geode/Geode.hpp>
#include <vector>
#include <unordered_set>

using namespace geode::prelude;

// Object IDs for portals that affect trajectory
const std::unordered_set<int> portalIDs = { 101, 99, 11, 10, 200, 201, 202, 203, 1334 };

// Collectible IDs to ignore during trajectory
const std::unordered_set<int> collectibleIDs = { 1329,1275,1587,1589,1598,1614,3601,4401,4402,4403,4404,4405,4406,4407,4408,4409,4410,4411,4412,4413,4414,4415,4416,4417,4418,4419,4420,4421,4422,4423,4424,4425,4426,4427,4428,4429,4430,4431,4432,4433,4434,4435,4436,4437,4438,4439,4440,4441,4442,4443,4444,4445,4446,4447,4448,4449,4450,4451,4452,4453,4454,4455,4456,4457,4458,4459,4460,4461,4462,4463,4464,4465,4466,4467,4468,4469,4470,4471,4472,4473,4474,4475,4476,4477,4478,4479,4480,4481,4482,4483,4484,4485,4486,4487,4488,4538,4489,4490,4491,4492,4493,4494,4495,4496,4497,4537,4498,4499,4500,4501,4502,4503,4504,4505,4506,4507,4508,4509,4510,4511,4512,4513,4514,4515,4516,4517,4518,4519,4520,4521,4522,4523,4524,4525,4526,4527,4528,4529,4530,4531,4532,4533,4534,4535,4536,4539 };

// Object types to keep active: 0 = solid, 2 = hazard, 47 = orb/ring, 25 = pad
const std::unordered_set<int> objectTypes = { 0, 2, 47, 25 };

struct PlayerData {
    CCPoint position;
    CCPoint lastPosition;
    double yVelocity;
    float rotation;
    bool isUpsideDown;
    bool isOnSlope;
    bool wasOnSlope;
    bool isShip;
    bool isBird;
    bool isBall;
    bool isDart;
    bool isRobot;
    bool isSpider;
    bool isSwing;
    float vehicleSize;
    float playerSpeed;
    bool isOnGround;
    bool isDashing;
    float gravityMod;
    GameObjectType objectType;
};

class TrajectoryNode : public cocos2d::CCDrawNode {
public:
    static TrajectoryNode* create() {
        TrajectoryNode* ret = new TrajectoryNode();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }

        delete ret;
        return nullptr;
    }
};

class ShowTrajectory {
public:
    static auto& get() {
        static ShowTrajectory instance;
        return instance;
    }

    void updateMergedColor();
    static void trajectoryOff();
    static cocos2d::CCDrawNode* trajectoryNode();
    static void updateTrajectory(PlayLayer* pl);
    static void createTrajectory(PlayLayer* pl, PlayerObject* fakePlayer, PlayerObject* realPlayer, bool hold, bool inverted = false);
    static void drawPlayerHitbox(PlayerObject* player, CCDrawNode* drawNode);
    static std::vector<cocos2d::CCPoint> getVertices(PlayerObject* player, cocos2d::CCRect rect, float rotation);
    static void handlePortal(PlayerObject* player, int id);
    static PlayerData savePlayerData(PlayerObject* player);
    static void applyPlayerData(PlayerObject* player, PlayerData data);

    static cocos2d::ccColor4F ccc4FFromccc3B(cocos2d::ccColor3B color) {
        return ccc4f(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f);
    }

    static cocos2d::ccColor3B ccc3BFromccc4F(cocos2d::ccColor4F color) {
        return ccc3((int)(color.r * 255), (int)(color.g * 255), (int)(color.b * 255));
    }

    PlayerObject* fakePlayer1 = nullptr;
    PlayerObject* fakePlayer2 = nullptr;

    bool creatingTrajectory = false;
    bool cancelTrajectory = false;

    float deathRotation = 0.f;
    float delta = 1.f / 240.f;

    int length = 312;

    // Green for click trajectory, red for release trajectory, yellow for overlap
    cocos2d::ccColor4F color1 = ccc4f(0.29f, 0.89f, 0.33f, 1.f);  // Green
    cocos2d::ccColor4F color2 = ccc4f(0.51f, 0.03f, 0.03f, 1.f);  // Red
    cocos2d::ccColor4F color3 = ccc4f(1.f, 1.f, 0.f, 1.f);        // Yellow (merged)

    cocos2d::CCPoint player1Trajectory[480];
    cocos2d::CCPoint player2Trajectory[480];
};

#endif
