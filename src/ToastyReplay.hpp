#ifndef _ToastyReplay_hpp
#define _ToastyReplay_hpp

#include "replay.hpp"
#include "ttr_format.hpp"
#include "core/cbf_integration.hpp"
#include "hacks/trajectory.hpp"
#include "render/renderer.hpp"

#include <Geode/Bindings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

enum MacroMode {
    MODE_DISABLED,
    MODE_CAPTURE,
    MODE_EXECUTE
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
    static constexpr double kBaseTickRate = 240.0;

    struct QueuedMacroCommand {
        int button = 0;
        bool down = false;
        bool player2 = false;
        double timestamp = 0.0;
    };

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
    bool noMirrorEffect = false;
    bool noMirrorRecordingOnly = false;
    bool fastPlayback = false;
    bool layoutMode = false;
    bool userInputIgnored = false;
    bool macroInputActive = false;
    bool protectedMode = false;
    bool pathPreview = false;
    bool collisionBypass = false;
    bool collisionLimitActive = false;
    float collisionThreshold = 0.0f;
    bool noclipDeathBlocked = false;
    bool noclipDeathFlash = true;
    float noclipFlashAlpha = 0.0f;
    float noclipDeathColorR = 1.0f;
    float noclipDeathColorG = 0.0f;
    float noclipDeathColorB = 0.0f;

    bool safeMode = false;
    bool showTrajectory = false;
    bool trajectoryBothSides = false;
    bool creatingTrajectory = false;

    bool showHitboxes = false;
    bool hitboxOnDeath = false;
    bool hitboxTrail = false;
    int hitboxTrailLength = 240;

    int bypassedCollisions = 0;
    int totalTickCount = 0;

    bool rngLocked = false;
    unsigned int rngSeedVal = 1;
    uintptr_t capturedRngState = 0;

    int pathLength = 312;

    double gameSpeed = 1.0;
    double tickRate = 240.0;
    MacroSequence* activeMacro = nullptr;

    TTRMacro* activeTTR = nullptr;
    bool ttrMode = true;

    std::unordered_map<CheckpointObject*, CheckpointStateBundle> storedRestorePoints;
    int lastTickIndex = 0;
    int respawnTickIndex = -1;
    size_t executeIndex = 0;
    size_t playbackAnchorIndex = 0;
    bool captureIgnored = false;
    bool levelRestarting = false;
    bool initialRun = false;
    bool pendingPlaybackStart = false;
    bool anchorReconciliation = false;
    int anchorInterval = 240;
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

    AccuracyMode selectedAccuracyMode = AccuracyMode::Vanilla;
    int tickStartStep = 0;
    int lastStepDelta = -1;
    uint64_t tickStartMicros = 0;
    uint64_t stepStartMicros = 0;
    double stepDurationMicros = 0.0;
    std::deque<uint64_t> pendingRawInputMicros;
    std::deque<QueuedMacroCommand> queuedMacroCommands;

    int tickOffset = 0;
    bool startPosActive = false;
    std::string startPosWarning;

    std::vector<std::string> storedMacros;
    std::unordered_set<std::string> incompatibleMacros;
    std::unordered_set<std::string> cbsMacros;
    std::unordered_set<std::string> cbfMacros;
    std::unordered_set<std::string> ttrMacros;

    int hotkey_tickStep = 0x56;
    int hotkey_audioPitch = 0;
    int hotkey_protected = 0;
    int hotkey_pathPreview = 0;
    int hotkey_collision = 0;
    int hotkey_rngLock = 0;
    int hotkey_hitboxes = 0;

    void reloadMacroList() {
        storedMacros.clear();
        incompatibleMacros.clear();
        cbsMacros.clear();
        cbfMacros.clear();
        ttrMacros.clear();
        auto directory = geode::prelude::Mod::get()->getSaveDir() / "replays";
        if (!std::filesystem::exists(directory)) {
            return;
        }

        for (auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            std::string stem = entry.path().stem().string();
            auto extension = entry.path().extension();

            if (extension == ".ttr") {
                std::ifstream input(entry.path(), std::ios::binary);
                if (!input.is_open()) {
                    storedMacros.push_back(stem);
                    incompatibleMacros.insert(stem);
                    continue;
                }

                char header[10] = {};
                input.read(header, 10);
                input.close();
                if (header[0] == 'T' && header[1] == 'T' && header[2] == 'R' && header[3] == '\0') {
                    storedMacros.push_back(stem);
                    ttrMacros.insert(stem);
                    uint32_t flags = 0;
                    std::memcpy(&flags, header + 6, sizeof(uint32_t));
                    if ((flags & TTR_FLAG_ACCURACY_CBF) != 0) {
                        cbfMacros.insert(stem);
                    } else if ((flags & TTR_FLAG_ACCURACY_CBS) != 0) {
                        cbsMacros.insert(stem);
                    }
                } else {
                    storedMacros.push_back(stem);
                    incompatibleMacros.insert(stem);
                }
                continue;
            }

            if (extension != ".gdr") {
                storedMacros.push_back(stem);
                incompatibleMacros.insert(stem);
                continue;
            }

            std::ifstream input(entry.path(), std::ios::binary);
            if (!input.is_open()) {
                storedMacros.push_back(stem);
                incompatibleMacros.insert(stem);
                continue;
            }

            input.seekg(0, std::ios::end);
            auto fileSize = input.tellg();
            input.seekg(0, std::ios::beg);
            std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
            input.read(reinterpret_cast<char*>(bytes.data()), fileSize);
            input.close();

            try {
                MacroSequence temp = MacroSequence::importData(bytes);
                storedMacros.push_back(stem);
                if (temp.accuracyMode == AccuracyMode::CBF) {
                    cbfMacros.insert(stem);
                } else if (temp.accuracyMode == AccuracyMode::CBS) {
                    cbsMacros.insert(stem);
                }
            } catch (...) {
                storedMacros.push_back(stem);
                incompatibleMacros.insert(stem);
            }
        }
    }

    void beginCapture(GJGameLevel* level) {
        pendingPlaybackStart = false;
        engineMode = MODE_CAPTURE;
        anchorReconciliation = true;
        resetTimingTracking();
        AccuracyRuntime::applyRuntimeAccuracyMode(selectedAccuracyMode);

        if (ttrMode) {
            initializeTTRMacro(level);
            if (activeTTR) {
                activeTTR->accuracyMode = selectedAccuracyMode;
            }
            if (activeTTR) {
                if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->m_startPosObject) {
                    activeTTR->recordedFromStartPos = true;
                    activeTTR->startPosX = playLayer->m_startPosObject->getPositionX();
                    activeTTR->startPosY = playLayer->m_startPosObject->getPositionY();
                }
            }
        } else {
            initializeMacro(level);
            if (activeMacro) {
                activeMacro->accuracyMode = selectedAccuracyMode;
            }
            if (activeMacro) {
                if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->m_startPosObject) {
                    activeMacro->recordedFromStartPos = true;
                    activeMacro->startPosX = playLayer->m_startPosObject->getPositionX();
                    activeMacro->startPosY = playLayer->m_startPosObject->getPositionY();
                }
            }
        }
    }

    void initializeMacro(GJGameLevel* level) {
        if (activeMacro) {
            delete activeMacro;
        }
        activeMacro = new MacroSequence();
        if (level) {
            activeMacro->levelInfo.id = level->m_levelID;
            activeMacro->levelInfo.name = level->m_levelName;
            activeMacro->name = level->m_levelName;
        }
        activeMacro->framerate = tickRate;
    }

    void initializeTTRMacro(GJGameLevel* level) {
        if (activeTTR) {
            delete activeTTR;
        }
        activeTTR = new TTRMacro();
        if (level) {
            activeTTR->levelId = level->m_levelID;
            activeTTR->levelName = level->m_levelName;
            activeTTR->name = level->m_levelName;
        }
        activeTTR->framerate = tickRate;
        if (auto* playLayer = PlayLayer::get()) {
            activeTTR->platformerMode = playLayer->m_levelSettings->m_platformerMode;
            activeTTR->twoPlayerMode = playLayer->m_levelSettings->m_twoPlayerMode;
        }
        activeTTR->rngLocked = rngLocked;
        activeTTR->rngSeed = rngSeedVal;
    }

    bool validateExecutionRequest() {
        bool hasTTR = ttrMode && activeTTR && !activeTTR->inputs.empty();
        bool hasGDR = !ttrMode && activeMacro && !activeMacro->inputs.empty();
        if (!hasTTR && !hasGDR) {
            return false;
        }

        if (ttrMode && activeTTR) {
            if (activeTTR->accuracyMode == AccuracyMode::CBF && !AccuracyRuntime::isSyzziCBFAvailable()) {
                startPosWarning = "CBF playback requires syzzi.click_between_frames.";
                return false;
            }
        } else if (activeMacro) {
            if (activeMacro->accuracyMode == AccuracyMode::CBF && !AccuracyRuntime::isSyzziCBFAvailable()) {
                startPosWarning = "CBF playback requires syzzi.click_between_frames.";
                return false;
            }
        }

        return true;
    }

    bool armExecutionState() {
        if (!validateExecutionRequest()) {
            pendingPlaybackStart = false;
            return false;
        }

        pendingPlaybackStart = false;
        engineMode = MODE_EXECUTE;
        executeIndex = 0;
        playbackAnchorIndex = 0;
        initialRun = true;
        respawnTickIndex = -1;
        tickOffset = 0;
        startPosActive = false;
        startPosWarning.clear();
        anchorReconciliation = true;
        resetTimingTracking();

        if (!ttrMode && activeMacro) {
            anchorInterval = activeMacro->savedAnchorInterval > 0
                ? activeMacro->savedAnchorInterval
                : static_cast<int>(tickRate);
        }

        AccuracyRuntime::applyRuntimeAccuracyMode(activeMacroAccuracyMode());
        return true;
    }

    bool beginExecutionImmediate() {
        if (!armExecutionState()) {
            engineMode = MODE_DISABLED;
            return false;
        }

        if (auto* playLayer = PlayLayer::get()) {
            if (auto* endLayer = playLayer->getChildByType<EndLevelLayer>(0)) {
                endLayer->removeFromParentAndCleanup(true);
            }
            if (playLayer->m_isPracticeMode) {
                playLayer->togglePracticeMode(false);
            }
            playLayer->resetLevel();
        }
        return engineMode == MODE_EXECUTE;
    }

    bool requestExecutionStart() {
        if (!validateExecutionRequest()) {
            pendingPlaybackStart = false;
            return false;
        }

        auto* playLayer = PlayLayer::get();
        if (!playLayer || fastPlayback) {
            return beginExecutionImmediate();
        }

        bool runActive = !playLayer->m_isPracticeMode &&
            !playLayer->m_levelEndAnimationStarted &&
            playLayer->m_player1 &&
            !playLayer->m_player1->m_isDead;

        if (!runActive) {
            return beginExecutionImmediate();
        }

        startPosWarning.clear();
        pendingPlaybackStart = true;
        return true;
    }

    bool armPendingPlaybackStart(PlayLayer* playLayer) {
        if (!pendingPlaybackStart || engineMode == MODE_EXECUTE || !playLayer) {
            return false;
        }

        if (auto* endLayer = playLayer->getChildByType<EndLevelLayer>(0)) {
            endLayer->removeFromParentAndCleanup(true);
        }

        return armExecutionState();
    }

    void haltExecution() {
        pendingPlaybackStart = false;
        resetTimingTracking();
        AccuracyRuntime::applyRuntimeAccuracyMode(selectedAccuracyMode);
        engineMode = MODE_DISABLED;
    }

    bool hasMacro() const {
        return ttrMode ? (activeTTR != nullptr) : (activeMacro != nullptr);
    }

    bool hasMacroInputs() const {
        if (ttrMode) {
            return activeTTR && !activeTTR->inputs.empty();
        }
        return activeMacro && !activeMacro->inputs.empty();
    }

    bool hasAnchorData() const {
        auto const* anchors = activeAnchors();
        return anchors && !anchors->empty();
    }

    std::string getMacroName() const {
        if (ttrMode && activeTTR) return activeTTR->name;
        if (!ttrMode && activeMacro) return activeMacro->name;
        return "";
    }

    AccuracyMode activeMacroAccuracyMode() const {
        if (ttrMode) {
            return activeTTR ? activeTTR->accuracyMode : AccuracyMode::Vanilla;
        }
        return activeMacro ? activeMacro->accuracyMode : AccuracyMode::Vanilla;
    }

    double runtimeTickRate() const {
        return std::max(1.0, tickRate);
    }

    double effectiveTimeScale() const {
        return gameSpeed * (kBaseTickRate / runtimeTickRate());
    }

    float fixedSimulationDelta() const {
        return static_cast<float>(1.0 / runtimeTickRate());
    }

    void resetTimingTracking() {
        tickStartStep = 0;
        lastStepDelta = -1;
        respawnTickIndex = -1;
        tickStartMicros = 0;
        stepStartMicros = 0;
        stepDurationMicros = 0.0;
        pendingRawInputMicros.clear();
        queuedMacroCommands.clear();
    }

    void beginStepTimingWindow(uint64_t stepStart, double stepDuration, bool newTick) {
        stepStartMicros = stepStart;
        stepDurationMicros = std::max(stepDuration, 1.0);
        if (newTick || tickStartMicros == 0) {
            tickStartMicros = stepStart;
        }
    }

    void queueRawInputTimestamp(uint64_t micros) {
        pendingRawInputMicros.push_back(micros);
        while (pendingRawInputMicros.size() > 32) {
            pendingRawInputMicros.pop_front();
        }
    }

    float consumeRawInputPhase(float fallbackPhase) {
        while (!pendingRawInputMicros.empty()) {
            uint64_t micros = pendingRawInputMicros.front();
            pendingRawInputMicros.pop_front();

            if (tickStartMicros == 0 || stepDurationMicros <= 0.0 || micros < tickStartMicros) {
                continue;
            }

            double elapsed = static_cast<double>(micros - tickStartMicros);
            double phase = elapsed / stepDurationMicros;
            return static_cast<float>(std::clamp(phase, 0.0, static_cast<double>(fallbackPhase) + 0.999));
        }

        return fallbackPhase;
    }

    void queueMacroCommand(int button, bool down, bool player2, double timestamp) {
        queuedMacroCommands.push_back(QueuedMacroCommand {
            button,
            down,
            player2,
            timestamp,
        });
    }

    std::vector<PlaybackAnchor>* activeAnchors() {
        if (ttrMode) {
            return activeTTR ? &activeTTR->anchors : nullptr;
        }
        return activeMacro ? &activeMacro->anchors : nullptr;
    }

    std::vector<PlaybackAnchor> const* activeAnchors() const {
        if (ttrMode) {
            return activeTTR ? &activeTTR->anchors : nullptr;
        }
        return activeMacro ? &activeMacro->anchors : nullptr;
    }

    AnchorRngState captureRngState() const {
        AnchorRngState rng;
        rng.fastRandState = GameToolbox::getfast_srand();
        rng.locked = rngLocked;
        rng.seed = rngSeedVal;
        return rng;
    }

    Renderer renderer;

    static ReplayEngine* get() {
        static auto* singleton = new ReplayEngine();
        return singleton;
    }

    void triggerAudio(bool secondPlayer, int actionType, bool pressed);

    void resetDeferredInputState();
    void truncateRecordedDataAfter(int tick);
    void prepareCaptureRestore(int tick);
    void restoreLockedRngState(uintptr_t rngState);
    void pruneStoredRestorePoints(PlayLayer* playLayer, int restoredTick);

    void processHotkeys();
};

#endif
