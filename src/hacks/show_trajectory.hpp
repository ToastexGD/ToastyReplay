#pragma once

#include <Geode/Geode.hpp>
#include <set>

using namespace geode::prelude;

class VisualizerNode : public CCDrawNode {
public:
    static VisualizerNode* create() {
        auto node = new VisualizerNode();
        if (node && node->init()) {
            node->autorelease();
            return node;
        }
        CC_SAFE_DELETE(node);
        return nullptr;
    }
};

const std::set<int> modePortalIdentifiers = {
    12,
    13,
    47,
    111,
    660,
    745,
    1331,
    1933
};

const std::set<int> gravityFlipIdentifiers = {
    10,
    11,
    2926
};

const std::set<int> velocityPortalIdentifiers = {
    200,
    201,
    202,
    203,
    1334
};

const std::set<int> scalePortalIdentifiers = {
    99,
    101
};

const std::set<int> flipPortalIdentifiers = {
    45,
    46
};

const std::set<int> splitPortalIdentifiers = {
    286,
    287
};

const std::set<int> warpPortalIdentifiers = {
    747
};

const std::set<int> jumpRingIdentifiers = {
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

const std::set<int> bouncePadIdentifiers = {
    35,
    67,
    140,
    1332,
    1333,
    3016
};

const std::set<int> dashRingIdentifiers = {
    1704,
    3005,
    3004
};

const std::set<int> pathObjectCategories = {
    0,
    5,
    7,
};

const std::set<int> collectableIdentifiers = {
    1329,
    142,
    1614,
    1587,
    1275
};

class PathVisualizer {
public:
    static PathVisualizer& get() {
        static PathVisualizer singleton;
        return singleton;
    }

    bool buildingPath = false;
    bool pathAborted = false;

    PlayerObject* shadowPlayer1 = nullptr;
    PlayerObject* shadowPlayer2 = nullptr;

    cocos2d::ccColor4F pressColor = {0.0f, 1.0f, 0.0f, 1.0f};
    cocos2d::ccColor4F releaseColorVal = {1.0f, 0.0f, 0.0f, 1.0f};
    cocos2d::ccColor4F mergedColor = {1.0f, 1.0f, 0.0f, 1.0f};

    int frameCount = 240;
    float frameDelta = 1.0f / 240.0f;
    float deathAngle = 0.0f;

    cocos2d::CCPoint p1Coords[2000];
    cocos2d::CCPoint p2Coords[2000];

    std::set<GameObject*> triggeredObjects;

    static void disable();
    static void refresh(PlayLayer* pl);
    static void buildPath(PlayLayer* pl, PlayerObject* shadow, PlayerObject* actual, bool pressing, bool flipped = false);
    static void renderBounds(PlayerObject* player, CCDrawNode* node);
    static std::vector<cocos2d::CCPoint> computeVertices(PlayerObject* player, cocos2d::CCRect bounds, float angle);
    static void refreshMergedColor();

    static void processPortal(PlayerObject* player, GameObject* obj);
    static void processRing(PlayerObject* player, RingObject* ring, PlayLayer* pl);
    static void processPad(PlayerObject* player, GameObject* pad);
    static void processDash(PlayerObject* player, DashRingObject* dashObj);

    static bool shouldProcessObject(GameObject* obj);
    static bool isModePortal(int objId);
    static bool isGravityFlip(int objId);
    static bool isVelocityPortal(int objId);
    static bool isScalePortal(int objId);
    static bool isJumpRing(int objId);
    static bool isBouncePad(int objId);
    static bool isDashRing(int objId);

    static cocos2d::CCDrawNode* visualizerNode();

private:
    PathVisualizer() = default;
};
