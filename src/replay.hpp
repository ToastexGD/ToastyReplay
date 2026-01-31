#ifndef _replay_hpp
#define _replay_hpp

#include <Geode/Geode.hpp>
#include <gdr/gdr.hpp>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;
#define MACRO_FORMAT_VER 1.0f

struct PhysicsSnapshot {
#ifdef GEODE_IS_WINDOWS
    std::unordered_map<int, GJPointDouble> rotationObjectMap;
    std::unordered_map<int, GameObject*> rotatedObjLookup;
    std::unordered_set<int> activatedRings;
    std::unordered_set<int> ringInteractionSet;
    std::map<int, bool> padInteractionMap;
    std::map<int, bool> buttonHoldState;
#endif
    std::vector<float> followFloatValues;
    cocos2d::CCPoint position;
    float rotation;
    bool holdRight;
    bool holdLeft;
    cocos2d::CCNode* parentLayer;
    bool teleportOccurred;
    bool gravityBugFix;
    bool reverseActive;
    double preSlope_yVel;
    double dashVecX;
    double dashVecY;
    double dashRotation;
    double dashBeginTime;
    DashRingObject* activeDashRing;
    double slopeBeginTime;
    bool streakJustPlaced;
    GameObject* priorGroundObj;
    cocos2d::CCDictionary* hitLogTop;
    cocos2d::CCDictionary* hitLogBottom;
    cocos2d::CCDictionary* hitLogLeft;
    cocos2d::CCDictionary* hitLogRight;
    int lastHitBottom;
    int lastHitTop;
    int lastHitLeft;
    int lastHitRight;
    int reserved50C;
    int reserved510;
    GameObject* slopeRef2;
    GameObject* prevGroundRef;
    float inclineAngle;
    bool slidingRotated;
    bool fastCheckpoint;
    GameObject* hitObject;
    GameObject* groundObjRef;
    GameObject* leftCollider;
    GameObject* rightCollider;
    int savedTickIndex;
    double scaleFactorX2;
    double groundVertVel;
    double vertVelRelated;
    double scaleFactorX3;
    double scaleFactorX4;
    double scaleFactorX5;
    bool slopeCollision;
    bool ballSpinning;
    bool reserved669;
    GameObject* slopeRef3;
    GameObject* currentIncline;
    double field584;
    int slopeColliderId;
    bool slopeGravFlip;
    cocos2d::CCArray* particleList;
    float inclineRadians;
    float spinRate;
    float turnRate;
    bool spinning;
    bool ballSpinning2;
    bool glowActive;
    bool hidden;
    double speedFactor;
    double startY;
    double gravityVal;
    float particleDuration;
    float field648;
    double modeChangeTime;
    bool padRingFlag;
    bool reducedFX;
    bool falling;
    bool checkpointAttempt;
    bool effectsOn;
    bool blockCollisionEnabled;
    bool groundFX;
    bool shipFX;
    bool grounded3;
    bool checkpointCooldown;
    double lastCheckTime;
    double lastJumpTimestamp;
    double lastFlipTimestamp;
    double flashTimestamp;
    float flashVal;
    float flashVal1;
    double lastSpiderFlipTimestamp;
    bool flagBool5;
    bool vehicleGlow;
    bool gameVar0096;
    bool gameVar0100;
    double accelOrSpeed;
    double snapDist;
    bool ringJumpFlag;
    GameObject* snappedObj;
    CheckpointObject* queuedCheckpoint;
    int flyCheckpointAttempts;
    bool spriteFlag;
    bool landParticles0;
    float landParticleAngle;
    float landParticleY;
    int streakCount;
    double inclineRotation;
    double slopeVertVel;
    double field3d0;
    double darkOrbVal;
    bool flag3e0;
    bool flag3e1;
    bool accelerating;
    bool topSlope;
    double topCollideMinY;
    double bottomCollideMaxY;
    double leftCollideMaxX;
    double rightCollideMinX;
    bool checkpointAllowed;
    bool collisionOccurring;
    bool jumpQueued;
    bool ringJumpState;
    bool jumpWasQueued;
    bool robotJumpOccurred;
    unsigned char jumpQueuedState;
    bool ringJumpState2;
    bool ringTouched;
    bool customRingTouched;
    bool gravPortalTouched;
    bool breakableBlockTouched;
    geode::SeedValueRSV jumpAC2;
    bool padTouched;
    double vertVelocity;
    double fallVelocity;
    bool onIncline;
    bool wasOnIncline;
    float inclineVelocity;
    bool invertedIncline;
    bool shipMode;
    bool ufoMode;
    bool ballMode;
    bool waveMode;
    bool robotMode;
    bool spiderMode;
    bool invertedGravity;
    bool dead;
    bool grounded;
    bool movingLeft;
    bool sideways;
    bool swingMode;
    int reverseVal;
    double reverseSpeed;
    double reverseAccel;
    float horizVelRelated2;
    bool dashing;
    int field9e8;
    int groundMaterial;
    float playerScale;
    float moveSpeed;
    cocos2d::CCPoint shipAngle;
    cocos2d::CCPoint lastPortalCoord;
    float unusedField3;
    bool grounded2;
    double landTimestamp;
    float platVelRelated;
    bool boosted;
    double scaleXTime;
    bool boostSlideDecay;
    bool fieldA29;
    bool locked;
    bool inputsDisabled;
    cocos2d::CCPoint lastGroundCoord;
    cocos2d::CCArray* ringContacts;
    GameObject* lastPortalObj;
    bool hasJumped;
    bool streakOrRingFlag;
    cocos2d::CCPoint coord;
    bool isPlayer2;
    bool fieldA99;
    double totalElapsed;
    bool dualSpawning;
    float fieldAAC;
    float angleField1;
    float vertVelRelated3;
    bool gameVar0060;
    bool colorsSwapped;
    bool gameVar0062;
    int followVal;
    float field838;
    int groundedState;
    unsigned char stateUnk;
    unsigned char noStickX;
    unsigned char noStickY;
    unsigned char stateUnk2;
    int boostX;
    int boostY;
    int forceState2;
    int scaleState;
    double platHorizVel;
    bool leftFirst;
    double scaleXVal;
    bool hasStopped;
    float horizVelRelated;
    bool correctSlopeDir;
    bool sliding;
    double slopeForce;
    bool onIce;
    double physicsDelta;
    bool grounded4;
    int slideTime;
    double slideStartTime;
    double dirChangeTime;
    double slopeEndTimestamp;
    bool moving;
    bool platMovingLeft;
    bool platMovingRight;
    bool slidingRight;
    double dirChangeAngle;
    double unusedField2;
    bool platformerActive;
    int noAutoJumpState;
    int dartSlideState;
    int hitHeadState;
    int flipGravState;
    float gravityModifier;
    int forceState;
    cocos2d::CCPoint forceVec;
    bool forcesActive;
    float speedTimeVal;
    float speedAC;
    bool robotJumpFix;
    bool inputLocked;
    bool gameVar0123;
    int iconReqId;
    cocos2d::CCArray* field958;
    int unusedField;
    bool outOfBounds;
    float fallOriginY;
    bool squeezeDisabled;
    bool robotRun3;
    bool robotRun2;
    bool item20Flag;
    bool damageIgnored;
    bool v22Changes;
};

struct PositionSnapshot {
    cocos2d::CCPoint coordinates = { 0.f, 0.f };
    float angle = 0.f;
    bool hasRotation = true;
};

struct PositionCorrection {
    int tick;
    PositionSnapshot player1Data;
    PositionSnapshot player2Data;
};

struct RestorePoint {
    int tick;
    PhysicsSnapshot player1State;
    PhysicsSnapshot player2State;
    uintptr_t rngState;
    int priorTick;
};

struct MacroAction : gdr::Input {
    int tick = 0;
    int actionType = 0;
    bool secondPlayer = false;
    bool pressed = false;

    MacroAction() = default;

    MacroAction(int t, int action, bool isSecondPlayer, bool isPressed)
        : Input(t, action, isSecondPlayer, isPressed), tick(t), actionType(action), secondPlayer(isSecondPlayer), pressed(isPressed) {}
};

struct MacroSequence : gdr::Replay<MacroSequence, MacroAction> {
    std::string name;
    std::vector<PositionCorrection> corrections;

    MacroSequence() : Replay("ToastyReplay", MOD_VERSION) {}

    void persist() {
        author = GJAccountManager::get()->m_username;
        duration = inputs.size() > 0 ? inputs.back().frame / framerate : 0;

        auto dir = geode::prelude::Mod::get()->getSaveDir() / "replays";
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directory(dir);
        }

        std::string safeName = name;
        std::replace(safeName.begin(), safeName.end(), ':', '_');
        std::replace(safeName.begin(), safeName.end(), '*', '_');
        std::replace(safeName.begin(), safeName.end(), '?', '_');
        std::replace(safeName.begin(), safeName.end(), '/', '_');
        std::replace(safeName.begin(), safeName.end(), '\\', '_');
        std::replace(safeName.begin(), safeName.end(), '|', '_');
        std::replace(safeName.begin(), safeName.end(), '<', '_');
        std::replace(safeName.begin(), safeName.end(), '>', '_');
        std::replace(safeName.begin(), safeName.end(), '\"', '_');

        std::ofstream output(dir / (safeName + ".gdr"), std::ios::binary);

        auto bytes = exportData(false);

        output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        output.close();
        log::info("Saved replay to {}", (dir / (safeName + ".gdr")).string());
    }

    static MacroSequence* loadFromDisk(const std::string& filename) {
        auto dir = geode::prelude::Mod::get()->getSaveDir() / "replays";
        if (std::filesystem::exists(dir) || std::filesystem::create_directory(dir)) {
            std::ifstream input(dir / (filename + ".gdr"), std::ios::binary);

            if (!input.is_open()) {
                input = std::ifstream(dir / filename, std::ios::binary);
                if (!input.is_open()) return nullptr;
            }

            input.seekg(0, std::ios::end);
            auto fileSize = input.tellg();
            input.seekg(0, std::ios::beg);

            std::vector<uint8_t> bytes(fileSize);
            input.read(reinterpret_cast<char*>(bytes.data()), fileSize);
            input.close();

            MacroSequence* result = new MacroSequence();
            *result = MacroSequence::importData(bytes);
            result->name = filename;

            return result;
        }

        return nullptr;
    }

    void truncateAfter(int tick) {
        inputs.erase(std::remove_if(inputs.begin(), inputs.end(), [tick](MacroAction& action) {
            return action.frame >= tick;
        }), inputs.end());
    }

    void recordAction(int tick, int actionType, bool secondPlayer, bool pressed) {
        log::info("Adding input: frame: {}, button: {}, player2: {}, down: {}", tick, actionType, secondPlayer, pressed);
        inputs.emplace_back(tick, actionType, secondPlayer, pressed);
    }
};

#endif
