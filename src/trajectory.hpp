#ifndef _trajectory_hpp
#define _trajectory_hpp

#include <Geode/Geode.hpp>
#include <vector>
#include <unordered_set>

using namespace geode::prelude;

const std::unordered_set<int> interactivePortalIds = { 101, 99, 11, 10, 200, 201, 202, 203, 1334 };

const std::unordered_set<int> trajectoryInteractiveIds = {
    36, 84, 141, 1022, 1330, 1333, 1594, 1704,
    35, 67, 140, 1332, 3005
};

const std::unordered_set<int> pickupItemIds = { 1329,1275,1587,1589,1598,1614,3601,4401,4402,4403,4404,4405,4406,4407,4408,4409,4410,4411,4412,4413,4414,4415,4416,4417,4418,4419,4420,4421,4422,4423,4424,4425,4426,4427,4428,4429,4430,4431,4432,4433,4434,4435,4436,4437,4438,4439,4440,4441,4442,4443,4444,4445,4446,4447,4448,4449,4450,4451,4452,4453,4454,4455,4456,4457,4458,4459,4460,4461,4462,4463,4464,4465,4466,4467,4468,4469,4470,4471,4472,4473,4474,4475,4476,4477,4478,4479,4480,4481,4482,4483,4484,4485,4486,4487,4488,4538,4489,4490,4491,4492,4493,4494,4495,4496,4497,4537,4498,4499,4500,4501,4502,4503,4504,4505,4506,4507,4508,4509,4510,4511,4512,4513,4514,4515,4516,4517,4518,4519,4520,4521,4522,4523,4524,4525,4526,4527,4528,4529,4530,4531,4532,4533,4534,4535,4536,4539 };

const std::unordered_set<int> collisionObjectTypes = { 0, 2, 47, 25 };

struct SimulatedPlayerState {
    CCPoint position;
    CCPoint priorPosition;
    double verticalVelocity;
    float rotation;
    bool gravityInverted;
    bool onIncline;
    bool wasOnIncline;
    bool inShipMode;
    bool inUfoMode;
    bool inBallMode;
    bool inWaveMode;
    bool inRobotMode;
    bool inSpiderMode;
    bool inSwingMode;
    float scale;
    float movementSpeed;
    bool grounded;
    bool dashing;
    float gravityFactor;
    GameObjectType objType;
};

class PathDrawNode : public cocos2d::CCDrawNode {
public:
    static PathDrawNode* create() {
        PathDrawNode* node = new PathDrawNode();
        if (node->init()) {
            node->autorelease();
            return node;
        }

        delete node;
        return nullptr;
    }
};

class PathPreviewSystem {
public:
    static auto& get() {
        static PathPreviewSystem singleton;
        return singleton;
    }

    void recalculateMergedColor();
    static void disablePreview();
    static cocos2d::CCDrawNode* getDrawNode();
    static void refreshPreview(PlayLayer* pl);
    static void generatePath(PlayLayer* pl, PlayerObject* simPlayer, PlayerObject* actualPlayer, bool holding, bool mirrored = false);
    static void renderPlayerBounds(PlayerObject* player, CCDrawNode* node);
    static std::vector<cocos2d::CCPoint> calculateBoundingVertices(PlayerObject* player, cocos2d::CCRect bounds, float angle);
    static void processPortalInteraction(PlayerObject* player, int portalId);
    static SimulatedPlayerState capturePlayerState(PlayerObject* player);
    static void applySimulatedState(PlayerObject* player, SimulatedPlayerState state);
    static bool stateChanged(const SimulatedPlayerState& a, const SimulatedPlayerState& b);

    static cocos2d::ccColor4F colorFromRGB(cocos2d::ccColor3B rgb) {
        return ccc4f(rgb.r / 255.f, rgb.g / 255.f, rgb.b / 255.f, 1.f);
    }

    static cocos2d::ccColor3B rgbFromColor(cocos2d::ccColor4F col) {
        return ccc3((int)(col.r * 255), (int)(col.g * 255), (int)(col.b * 255));
    }

    PlayerObject* simulatedPlayer1 = nullptr;
    PlayerObject* simulatedPlayer2 = nullptr;

    bool generatingPath = false;
    bool simulatingHold = false;
    bool pathCancelled = false;


    float collisionRotation = 0.f;
    float tickDelta = 1.f / 240.f;

    int pathFrameCount = 312;

    cocos2d::ccColor4F holdColor = ccc4f(0.29f, 0.89f, 0.33f, 1.f);
    cocos2d::ccColor4F releaseColor = ccc4f(0.51f, 0.03f, 0.03f, 1.f);
    cocos2d::ccColor4F overlapColor = ccc4f(1.f, 1.f, 0.f, 1.f);

    cocos2d::CCPoint p1PathPoints[480];
    cocos2d::CCPoint p2PathPoints[480];

    SimulatedPlayerState cachedP1State{};
    SimulatedPlayerState cachedP2State{};
    bool trajectoryDirty = true;
};

#endif
