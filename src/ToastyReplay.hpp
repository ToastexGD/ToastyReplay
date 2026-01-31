#ifndef _ToastyReplay_hpp
#define _ToastyReplay_hpp
#include "replay.hpp"
#include <Geode/Bindings.hpp>
#include <vector>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

enum MacroMode {
    MODE_DISABLED, MODE_CAPTURE, MODE_EXECUTE
};

enum ValidationResult {
    RESULT_OK,
    RESULT_KEY_MISSING,
    RESULT_KEY_MALFORMED,
    RESULT_NETWORK_FAILURE,
    RESULT_DEVICE_MISMATCH,
    RESULT_UNEXPECTED,
    RESULT_DEVICE_CONFLICT
};

class ReplayEngine {
public:
    MacroMode engineMode = MODE_DISABLED;
    ValidationResult validationStatus = RESULT_OK;

    bool dataModified = false;

    float tickAccumulator = 0.f;

    bool renderingDisabled = false;
    bool bypassIgnored = false;
    bool recentlyInitialized = false;
    bool inputIgnored = false;
    bool tickStepping = false;
    bool pendingStep = false;
    bool singleTickStep = false;
    bool renderInternal = false;
    bool priorTickStepping = false;
    bool stepKeyActive = false;
    bool audioPitchEnabled = true;
    bool userInputIgnored = false;
    bool macroInputActive = false;
    bool protectedMode = false;
    bool pathPreview = false;
    bool collisionBypass = false;
    bool collisionLimitActive = false;
    float collisionThreshold = 0.0f;

    bool safeMode = false;
    bool showTrajectory = false;
    bool trajectoryBothSides = false;
    bool creatingTrajectory = false;

    int bypassedCollisions = 0;
    int totalTickCount = 0;

    bool rngLocked = false;
    unsigned int rngSeedVal = 1;
    uintptr_t capturedRngState = 0;

    int pathLength = 312;

    double gameSpeed = 1;
    double tickRate = 240.f;
    MacroSequence* activeMacro = nullptr;

    std::unordered_map<CheckpointObject*, RestorePoint> storedRestorePoints;
    int lastTickIndex = 0;
    int respawnTickIndex = -1;
    size_t executeIndex = 0;
    size_t correctionIndex = 0;
    bool captureIgnored = false;
    bool levelRestarting = false;
    bool initialRun = false;
    bool positionCorrection = false;
    bool inputCorrection = false;
    int correctionInterval = 240;
    int skipTickIndex = -1;
    int skipActionTick = -1;
    int deferredReleaseA[2] = { -1, -1 };
    int deferredInputTick[2] = { -1, -1 };
    int deferredReleaseB[2][2] = { { -1, -1 }, { -1, -1 } };
    bool activeButtons[6] = { false, false, false, false, false, false };
    bool priorButtonState[6] = { false, false, false, false, false, false };
    bool lateralInputPending[2] = { false, false };
    bool dashCancelIgnored[2] = { false, false };
    bool simulatingPath = false;
    int sessionCounter = 0;
    int lastSaveTick = 0;

    std::vector<std::string> storedMacros;

    int hotkey_tickStep = 0x56;
    int hotkey_audioPitch = 0;
    int hotkey_protected = 0;
    int hotkey_pathPreview = 0;
    int hotkey_collision = 0;
    int hotkey_rngLock = 0;

    void reloadMacroList() {
        storedMacros.clear();
        auto dir = geode::prelude::Mod::get()->getSaveDir() / "replays";
        if (std::filesystem::exists(dir)) {
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".gdr") {
                    storedMacros.push_back(entry.path().stem().string());
                }
            }
        }
    }

    void beginCapture(GJGameLevel* level) {
        engineMode = MODE_CAPTURE;
        initializeMacro(level);
    }

    void initializeMacro(GJGameLevel* level) {
        if (activeMacro) delete activeMacro;
        activeMacro = new MacroSequence();
        if (level) {
            activeMacro->levelInfo.id = level->m_levelID;
            activeMacro->levelInfo.name = level->m_levelName;
            activeMacro->name = level->m_levelName;
        }
        activeMacro->framerate = tickRate;
    }

    void beginExecution() {
        if (!activeMacro || activeMacro->inputs.empty()) return;

        engineMode = MODE_EXECUTE;
        executeIndex = 0;
        correctionIndex = 0;
        initialRun = true;
        respawnTickIndex = -1;

        PlayLayer* pl = PlayLayer::get();
        if (pl) {
            if (pl->m_isPracticeMode) {
                pl->togglePracticeMode(false);
            }
            pl->resetLevel();
        }
    }

    void haltExecution() {
        engineMode = MODE_DISABLED;
    }

    static auto* get() {
        static ReplayEngine* singleton = new ReplayEngine();
        return singleton;
    }

    void triggerAudio(bool secondPlayer, int actionType, bool pressed);

    void processHotkeys();
};
#endif
