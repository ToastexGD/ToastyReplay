#ifndef _ToastyReplay_hpp
#define _ToastyReplay_hpp

#include "utils.hpp"
#include "lang/localization.hpp"
#include "conversion/macro_converter.hpp"
#include "format/replay.hpp"
#include "format/ttr_format.hpp"
#include "hacks/autoclicker.hpp"
#include "trajectory/trajectory.hpp"
#include "render/renderer.hpp"

#include <Geode/Bindings.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace toasty::branding {
    inline constexpr const char* kProductName = "ToastyReplay";
    inline constexpr const char* kEditionLabel = "FREE";

    inline std::string versionText() {
        return std::string("v") + MOD_VERSION;
    }

    inline std::string versionBadge() {
        return versionText() + " " + kEditionLabel;
    }

    inline std::string baseBrand() {
        return std::string(kProductName) + " " + versionText();
    }

    inline std::string fullBrand() {
        return std::string(kProductName) + " " + versionBadge();
    }
}

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

struct QueuedSubstepInput {
    float offset = 0.0f;
    int button = 1;
    bool player2 = false;
    bool pressed = false;
    bool macroInput = false;
};

struct QueuedSubstepSegment {
    float deltaFactor = 1.0f;
    bool endStep = true;
    bool hasActionAfter = false;
    QueuedSubstepInput actionAfter;
};

class ReplayEngine {
public:
    struct QueuedMacroCommand {
        int button = 0;
        bool down = false;
        bool player2 = false;
        double timestamp = 0.0;
    };

    struct QueuedCaptureCommand {
        int button = 0;
        bool down = false;
        bool player2 = false;
        double timestamp = 0.0;
    };

    static constexpr double kBaseTickRate = 240.0;
    static constexpr int kTrajectoryLengthMin = 1;
    static constexpr int kTrajectoryLengthDefault = 312;
    static constexpr int kTrajectoryLengthSliderMax = 2400;
    static constexpr int kTrajectoryLengthMax = 60000;
    static constexpr int kTrajectoryPredictionStepMax = 250000;

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
    bool disableShaders = false;
    bool macroInputActive = false;
    bool protectedMode = false;
    bool autoSafeMode = false;
    bool pathPreview = false;
    bool collisionBypass = false;
    bool collisionLimitActive = false;
    float collisionThreshold = 0.0f;
    bool noclipDeathFlash = true;
    float noclipFlashAlpha = 0.0f;
    float noclipDeathColorR = 1.0f;
    float noclipDeathColorG = 0.0f;
    float noclipDeathColorB = 0.0f;

    bool safeMode = false;
    bool completionAutosave = true;
    bool persistenceMode = false;
    bool showTrajectory = false;
    bool trajectoryBothSides = false;
    bool creatingTrajectory = false;

    bool showHitboxes = false;
    bool hitboxOnDeath = false;
    bool hitboxTrail = false;
    int hitboxTrailLength = 240;

    int noclipTotalFrames = 0;
    int noclipUnsafeFrames = 0;
    int noclipDeathEvents = 0;
    bool noclipWouldDieThisFrame = false;
    bool noclipDeadLastFrame = false;
    PlayerObject* noclipLastDeathPlayer = nullptr;
    GameObject* noclipLastDeathObject = nullptr;

    bool rngLocked = false;
    unsigned int rngSeedVal = 1;
    uintptr_t capturedRngState = 0;

    int pathLength = kTrajectoryLengthDefault;

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
    std::array<std::array<bool, 3>, 2> checkpointResumedHolds = {};
    bool activeButtons[6] = { false, false, false, false, false, false };
    bool priorButtonState[6] = { false, false, false, false, false, false };
    bool lateralInputPending[2] = { false, false };
    bool dashCancelIgnored[2] = { false, false };
    bool simulatingPath = false;
    int sessionCounter = 0;
    int lastSaveTick = 0;
    bool substepMidStep = false;
    bool substepMacroDispatch = false;
    float substepCaptureOffset = -1.0f;
    std::deque<QueuedSubstepSegment> queuedSubstepSegments;
    std::deque<QueuedMacroCommand> queuedMacroCommands;
    std::deque<QueuedCaptureCommand> queuedCaptureCommands;
    bool cbsCaptureProcessingQueue = false;
    bool cbsPlaybackProcessingQueue = false;
    bool unsavedGuardActive = false;
    bool replayAccuracyEnvironmentActive = false;
    bool savedClickBetweenSteps = false;
    bool savedLayerClickBetweenSteps = false;
    bool hasSavedLayerClickBetweenSteps = false;
    bool playbackTickRateOverrideActive = false;
    double savedPlaybackTickRate = kBaseTickRate;
    TTRAttemptSegment persistenceCaptureAttempt;
    bool persistenceCaptureDeathRecorded = false;
    size_t persistencePlaybackAttemptIndex = 0;
    bool persistencePlaybackDeathPending = false;
    int persistenceRenderBaseTick = 0;

    bool frameStepAudioActive = false;
    bool frameStepAudioWasPaused = false;
    bool frameStepMusicSeekPending = false;
    int frameStepPendingMusicTimeMs = 0;

    AccuracyMode selectedAccuracyMode = AccuracyMode::Vanilla;
    int tickStartStep = 0;
    int lastStepDelta = -1;
    double tickStartTimestamp = 0.0;

    int tickOffset = 0;
    bool startPosActive = false;
    std::string startPosWarning;
    bool startPosWarningIsKey = false;

    std::vector<std::string> storedMacros;
    std::unordered_set<std::string> incompatibleMacros;
    std::unordered_set<std::string> cbsMacros;
    std::unordered_set<std::string> platformerMacros;
    std::unordered_set<std::string> ttrMacros;
    std::unordered_set<std::string> ttr2Macros;
    std::unordered_set<std::string> legacyTtrMacros;
    std::unordered_set<std::string> legacyCbsMacros;
    std::vector<toasty::conversion::DetectedReplay> foreignReplays;
    std::unordered_set<std::string> convertedForeignReplaySources;
    std::unordered_map<std::string, std::string> convertedMacroSources;

    int hotkey_tickStep = 0x56;
    int hotkey_audioPitch = 0;
    int hotkey_protected = 0;
    int hotkey_pathPreview = 0;
    int hotkey_collision = 0;
    int hotkey_rngLock = 0;
    int hotkey_hitboxes = 0;

    void clearStartPosWarning() {
        startPosWarning.clear();
        startPosWarningIsKey = false;
    }

    void resetNoclipAccuracyStats() {
        noclipTotalFrames = 0;
        noclipUnsafeFrames = 0;
        noclipDeathEvents = 0;
        noclipWouldDieThisFrame = false;
        noclipDeadLastFrame = false;
        noclipLastDeathPlayer = nullptr;
        noclipLastDeathObject = nullptr;
    }

    void markNoclipUnsafeFrame(PlayerObject* player, GameObject* object) {
        noclipWouldDieThisFrame = true;
        if (player) {
            noclipLastDeathPlayer = player;
        }
        if (object) {
            noclipLastDeathObject = object;
        }
    }

    float noclipAccuracyPercent() const {
        if (noclipTotalFrames <= 0) {
            return 100.0f;
        }

        int unsafeFrames = std::clamp(noclipUnsafeFrames, 0, noclipTotalFrames);
        float percent = 100.0f * (1.0f - static_cast<float>(unsafeFrames) / static_cast<float>(noclipTotalFrames));
        return std::clamp(percent, 0.0f, 100.0f);
    }

    void setStartPosWarningKey(std::string_view key) {
        startPosWarning = std::string(key);
        startPosWarningIsKey = true;
    }

    bool hasStartPosWarning() const {
        return !startPosWarning.empty();
    }

    std::string getStartPosWarningText() const {
        if (startPosWarning.empty()) {
            return {};
        }
        if (startPosWarningIsKey) {
            return std::string(toasty::lang::tr(startPosWarning));
        }
        return startPosWarning;
    }

    void reloadMacroList() {
        storedMacros.clear();
        incompatibleMacros.clear();
        cbsMacros.clear();
        platformerMacros.clear();
        ttrMacros.clear();
        ttr2Macros.clear();
        legacyTtrMacros.clear();
        legacyCbsMacros.clear();
        foreignReplays.clear();
        auto directory = geode::prelude::Mod::get()->getSaveDir() / "replays";
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec) || ec) {
            return;
        }

        std::vector<std::filesystem::path> foreignCandidates;
        std::unordered_set<std::string> usableStems;

        auto addUsable = [&](std::string const& stem, bool isTTR) {
            if (usableStems.insert(stem).second) {
                storedMacros.push_back(stem);
            }
            if (isTTR) {
                ttrMacros.insert(stem);
            }
        };

        auto readNativeTTRHeader = [&](std::filesystem::path const& path, bool isTTR2, uint32_t& flags) {
            auto bytes = ReplayStorage::readReplayBytes(path);
            if (!bytes || bytes->size() < 10) {
                return false;
            }

            auto const& header = *bytes;
            bool validMagic = isTTR2
                ? (header[0] == 'T' && header[1] == 'T' && header[2] == 'R' && header[3] == '2')
                : (header[0] == 'T' && header[1] == 'T' && header[2] == 'R' && header[3] == '\0');
            if (!validMagic) {
                return false;
            }

            std::memcpy(&flags, header.data() + 6, sizeof(uint32_t));
            return true;
        };

        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }

            auto path = it->path();
            auto stem = toasty::pathToUtf8(path.stem());
            auto extension = toasty::pathToUtf8(path.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (extension == ".ttr2" || extension == ".ttr") {
                bool isTTR2 = extension == ".ttr2";
                uint32_t flags = 0;
                if (!readNativeTTRHeader(path, isTTR2, flags)) {
                    foreignCandidates.push_back(path);
                    continue;
                }

                addUsable(stem, true);
                if (isTTR2) {
                    ttr2Macros.insert(stem);
                } else {
                    legacyTtrMacros.insert(stem);
                }
                if ((flags & TTR_FLAG_ACCURACY_CBS) != 0) {
                    cbsMacros.insert(stem);
                    if (!isTTR2) {
                        legacyCbsMacros.insert(stem);
                    }
                }
                if ((flags & TTR_FLAG_PLATFORMER) != 0) {
                    platformerMacros.insert(stem);
                }
                continue;
            }

            if (extension != ".gdr") {
                foreignCandidates.push_back(path);
                continue;
            }

            auto bytes = ReplayStorage::readReplayBytes(path);
            if (!bytes) {
                foreignCandidates.push_back(path);
                continue;
            }

            if (auto temp = MacroSequence::tryImportData(*bytes)) {
                temp->inferMissingPlatformerMode();
                addUsable(stem, false);
                if (temp->accuracyMode == AccuracyMode::CBS) {
                    cbsMacros.insert(stem);
                    legacyCbsMacros.insert(stem);
                }
                if (temp->platformerMode) {
                    platformerMacros.insert(stem);
                }
            } else {
                foreignCandidates.push_back(path);
            }
        }

        for (auto const& path : foreignCandidates) {
            auto detected = toasty::conversion::detectReplay(path, convertedForeignReplaySources, usableStems);
            foreignReplays.push_back(std::move(detected));
        }

        if (ec) {
            log::warn("Failed while scanning replay directory {}: {}", toasty::pathToUtf8(directory), ec.message());
        }
    }

    bool beginCapture(GJGameLevel* level) {
        if (Autoclicker::get()->enabled && Autoclicker::get()->isTimedMode()) {
            FLAlertLayer::create(
                nullptr,
                "ToastyReplay",
                "OK",
                nullptr,
                "Timed autoclicker cannot be recorded in normal replay mode. Disable timed autoclicker before recording."
            )->show();
            return false;
        }

        pendingPlaybackStart = false;
        engineMode = MODE_CAPTURE;
        anchorReconciliation = true;
        resetTimingTracking();
        clearPersistenceRuntimeState();
        if (persistenceMode) {
            completionAutosave = false;
        }
        clearQueuedSubstepState();
        AccuracyMode captureAccuracyMode = runtimeAccuracyModeFor(selectedAccuracyMode);
        beginReplayAccuracyEnvironment(captureAccuracyMode, false);

        if (ttrMode) {
            initializeTTRMacro(level);
            if (activeTTR) {
                activeTTR->accuracyMode = captureAccuracyMode;
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
                activeMacro->accuracyMode = captureAccuracyMode;
            }
            if (activeMacro) {
                if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->m_startPosObject) {
                    activeMacro->recordedFromStartPos = true;
                    activeMacro->startPosX = playLayer->m_startPosObject->getPositionX();
                    activeMacro->startPosY = playLayer->m_startPosObject->getPositionY();
                }
            }
        }

        return true;
    }

    void initializeMacro(GJGameLevel* level) {
        if (activeMacro) {
            delete activeMacro;
        }
        activeMacro = new MacroSequence();
        activeMacro->accuracyMode = runtimeAccuracyModeFor(selectedAccuracyMode);
        if (level) {
            activeMacro->levelInfo.id = level->m_levelID;
            activeMacro->levelInfo.name = level->m_levelName;
            activeMacro->name = level->m_levelName;
        }
        activeMacro->framerate = tickRate;
        if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->m_levelSettings) {
            activeMacro->platformerMode = playLayer->m_levelSettings->m_platformerMode;
            activeMacro->hasPlatformerModeMetadata = true;
        }
        dataModified = false;
    }

    void initializeTTRMacro(GJGameLevel* level) {
        if (activeTTR) {
            delete activeTTR;
        }
        activeTTR = new TTRMacro();
        activeTTR->accuracyMode = runtimeAccuracyModeFor(selectedAccuracyMode);
        if (level) {
            activeTTR->levelId = level->m_levelID;
            activeTTR->levelName = level->m_levelName;
            activeTTR->name = level->m_levelName;
        }
        activeTTR->framerate = tickRate;
        if (auto* playLayer = PlayLayer::get(); playLayer && playLayer->m_levelSettings) {
            activeTTR->platformerMode = playLayer->m_levelSettings->m_platformerMode;
            activeTTR->twoPlayerMode = playLayer->m_levelSettings->m_twoPlayerMode;
        }
        activeTTR->rngLocked = rngLocked;
        activeTTR->rngSeed = rngSeedVal;
        activeTTR->persistenceAttempts.clear();
        dataModified = false;
    }

    bool validateExecutionRequest() {
        bool hasTTR = ttrMode && activeTTR && (!activeTTR->inputs.empty() || !activeTTR->persistenceAttempts.empty());
        bool hasGDR = !ttrMode && activeMacro && !activeMacro->inputs.empty();
        if (!hasTTR && !hasGDR) {
            return false;
        }

        if (!validatePlaybackPlatformerMode()) {
            return false;
        }

        if (hasTTR && activeTTR->loadedFromTTR3()) {
            double requiredTps = activeTTR->maxSourceTps();
            double runtimeTps = runtimeTickRate();
            if (std::isfinite(requiredTps) && std::isfinite(runtimeTps) && requiredTps > runtimeTps + 1e-6) {
                log::warn("Playback aborted: macro requires a higher TPS than the current runtime.");
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
        refreshManualInputIgnoreState();
        executeIndex = 0;
        playbackAnchorIndex = 0;
        clearPersistenceRuntimeState();
        initialRun = true;
        respawnTickIndex = -1;
        tickOffset = 0;
        startPosActive = false;
        clearStartPosWarning();
        anchorReconciliation = true;
        resetTimingTracking();
        clearQueuedSubstepState();
        beginReplayAccuracyEnvironment(runtimeAccuracyModeFor(activeMacroAccuracyMode()), true);

        if (ttrMode && activeTTR && activeTTR->loadedFromTTR3()) {
            activeTTR->materializeTTR3RuntimeTicks(runtimeTickRate());
        }

        if (!ttrMode && activeMacro) {
            anchorInterval = activeMacro->savedAnchorInterval > 0
                ? activeMacro->savedAnchorInterval
                : static_cast<int>(activeMacroFramerate());
        }

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
            if (playLayer->m_levelSettings && playLayer->m_levelSettings->m_platformerMode) {
                playLayer->resetLevelFromStart();
            } else {
                playLayer->resetLevel();
            }
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

        clearStartPosWarning();
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
        resetDeferredInputState();
        if (auto* playLayer = PlayLayer::get()) {
            if (playLayer->m_player1) {
                playLayer->m_player1->releaseAllButtons();
            }
            if (playLayer->m_player2) {
                playLayer->m_player2->releaseAllButtons();
            }
        }
        Autoclicker::get()->reset();
        endReplayAccuracyEnvironment();
        executeIndex = 0;
        playbackAnchorIndex = 0;
        tickOffset = 0;
        startPosActive = false;
        engineMode = MODE_DISABLED;
        clearPersistenceRuntimeState();
        refreshManualInputIgnoreState();
    }

    bool hasMacro() const {
        return ttrMode ? (activeTTR != nullptr) : (activeMacro != nullptr);
    }

    bool hasMacroInputs() const {
        if (ttrMode) {
            return activeTTR && (!activeTTR->inputs.empty() || !activeTTR->persistenceAttempts.empty());
        }
        return activeMacro && !activeMacro->inputs.empty();
    }

    void setPersistenceMode(bool enabled) {
        persistenceMode = enabled;
        if (enabled) {
            completionAutosave = false;
        }
        if (auto* mod = Mod::get()) {
            mod->setSavedValue("eng_persistence_mode", persistenceMode);
            mod->setSavedValue("eng_completion_autosave", completionAutosave);
        }
    }

    void disablePersistenceModeSafeguard() {
        if (!persistenceMode) {
            return;
        }
        setPersistenceMode(false);
    }

    bool canUsePersistenceCapture() const {
        return persistenceMode && engineMode == MODE_CAPTURE && ttrMode && activeTTR;
    }

    bool hasUnsavedActiveMacro() const {
        bool hasInputs = (activeTTR && (!activeTTR->inputs.empty() || !activeTTR->persistenceAttempts.empty() || persistenceCaptureAttempt.hasData()))
            || (activeMacro && !activeMacro->inputs.empty());
        return dataModified && hasInputs;
    }

    void markActiveMacroDirty() {
        dataModified = true;
    }

    void clearActiveMacroDirty() {
        dataModified = false;
    }

    bool saveActiveMacro();
    void discardActiveMacro();
    void runWithUnsavedMacroGuard(std::function<void()> continueAction, std::string message = {});

    bool hasAnchorData() const {
        auto const* anchors = activeAnchors();
        return anchors && !anchors->empty();
    }

    bool manualInputIgnoredActive() const {
        return engineMode == MODE_EXECUTE;
    }

    void refreshManualInputIgnoreState() {
        inputIgnored = manualInputIgnoredActive();
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

    bool activeMacroPlatformerMode() const {
        if (ttrMode) {
            return activeTTR ? activeTTR->platformerMode : false;
        }
        return activeMacro ? activeMacro->platformerMode : false;
    }

    TTRAttemptSegment* activePersistencePlaybackAttempt() {
        if (engineMode != MODE_EXECUTE || !ttrMode || !activeTTR) {
            return nullptr;
        }
        if (persistencePlaybackAttemptIndex >= activeTTR->persistenceAttempts.size()) {
            return nullptr;
        }
        return &activeTTR->persistenceAttempts[persistencePlaybackAttemptIndex];
    }

    TTRAttemptSegment const* activePersistencePlaybackAttempt() const {
        if (engineMode != MODE_EXECUTE || !ttrMode || !activeTTR) {
            return nullptr;
        }
        if (persistencePlaybackAttemptIndex >= activeTTR->persistenceAttempts.size()) {
            return nullptr;
        }
        return &activeTTR->persistenceAttempts[persistencePlaybackAttemptIndex];
    }

    bool isPersistencePlaybackActive() const {
        return engineMode == MODE_EXECUTE && ttrMode && activeTTR && !activeTTR->persistenceAttempts.empty();
    }

    bool isPersistencePlaybackFailureAttempt() const {
        return activePersistencePlaybackAttempt() != nullptr;
    }

    std::vector<TTRInput>* activeTTRInputs() {
        if (!activeTTR) {
            return nullptr;
        }
        if (auto* attempt = activePersistencePlaybackAttempt()) {
            return &attempt->inputs;
        }
        return &activeTTR->inputs;
    }

    std::vector<TTRInput> const* activeTTRInputs() const {
        if (!activeTTR) {
            return nullptr;
        }
        if (auto const* attempt = activePersistencePlaybackAttempt()) {
            return &attempt->inputs;
        }
        return &activeTTR->inputs;
    }

    bool currentLevelPlatformerMode() const {
        auto* playLayer = PlayLayer::get();
        return playLayer && playLayer->m_levelSettings && playLayer->m_levelSettings->m_platformerMode;
    }

    bool validatePlaybackPlatformerMode() {
        auto* playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_levelSettings) {
            return true;
        }

        bool macroPlatformer = activeMacroPlatformerMode();
        bool levelPlatformer = currentLevelPlatformerMode();
        if (macroPlatformer == levelPlatformer) {
            return true;
        }

        setStartPosWarningKey(macroPlatformer
            ? "This macro was recorded in platformer mode. Open a platformer level to play it."
            : "This macro was recorded in classic mode. Open a classic level to play it.");
        return false;
    }

    double activeMacroFramerate() const {
        double framerate = kBaseTickRate;
        if (ttrMode && activeTTR) {
            framerate = activeTTR->framerate;
        } else if (!ttrMode && activeMacro) {
            framerate = activeMacro->framerate;
        }

        if (!std::isfinite(framerate) || framerate <= 0.0) {
            return kBaseTickRate;
        }
        return std::max(1.0, framerate);
    }

    static AccuracyMode runtimeAccuracyModeFor(AccuracyMode mode) {
        return mode == AccuracyMode::CBS ? AccuracyMode::CBS : AccuracyMode::Vanilla;
    }

    static void applyRuntimeAccuracyMode(AccuracyMode mode) {
        mode = runtimeAccuracyModeFor(mode);
        bool enabled = mode == AccuracyMode::CBS;
        if (auto* gameManager = GameManager::get()) {
            gameManager->setGameVariable("0177", enabled);
        }
        if (auto* playLayer = PlayLayer::get()) {
            playLayer->m_clickBetweenSteps = enabled;
        }
    }

    void enableCBSForLoadedMacroIfNeeded() {
        selectedAccuracyMode = runtimeAccuracyModeFor(activeMacroAccuracyMode());
    }

    void beginReplayAccuracyEnvironment(AccuracyMode mode, bool overridePlaybackTickRate) {
        if (!replayAccuracyEnvironmentActive) {
            if (auto* gameManager = GameManager::get()) {
                savedClickBetweenSteps = gameManager->getGameVariable("0177");
            }
            if (auto* playLayer = PlayLayer::get()) {
                savedLayerClickBetweenSteps = playLayer->m_clickBetweenSteps;
                hasSavedLayerClickBetweenSteps = true;
            } else {
                hasSavedLayerClickBetweenSteps = false;
            }
            replayAccuracyEnvironmentActive = true;
        }

        if (overridePlaybackTickRate && !playbackTickRateOverrideActive) {
            savedPlaybackTickRate = tickRate;
            tickRate = activeMacroFramerate();
            playbackTickRateOverrideActive = true;
        }

        applyRuntimeAccuracyMode(mode);
    }

    void endReplayAccuracyEnvironment() {
        if (playbackTickRateOverrideActive) {
            tickRate = savedPlaybackTickRate;
            playbackTickRateOverrideActive = false;
        }

        if (!replayAccuracyEnvironmentActive) {
            return;
        }

        if (auto* gameManager = GameManager::get()) {
            gameManager->setGameVariable("0177", savedClickBetweenSteps);
        }
        if (hasSavedLayerClickBetweenSteps) {
            if (auto* playLayer = PlayLayer::get()) {
                playLayer->m_clickBetweenSteps = savedLayerClickBetweenSteps;
            }
        }
        hasSavedLayerClickBetweenSteps = false;
        replayAccuracyEnvironmentActive = false;
    }

    double runtimeTickRate() const {
        return std::max(1.0, tickRate);
    }

    double trajectoryPredictionTickRate() const {
        return std::max(runtimeTickRate(), kBaseTickRate);
    }

    static int sanitizeTrajectoryLength(int length) {
        return std::clamp(length, kTrajectoryLengthMin, kTrajectoryLengthMax);
    }

    double trajectoryPredictionDuration() const {
        return static_cast<double>(sanitizeTrajectoryLength(pathLength)) / kBaseTickRate;
    }

    int trajectoryPredictionStepCountForRate(double predictionTickRate) const {
        if (!std::isfinite(predictionTickRate) || predictionTickRate <= 0.0) {
            return 0;
        }

        double stepCount = std::ceil(trajectoryPredictionDuration() * predictionTickRate);
        if (!std::isfinite(stepCount) || stepCount <= 0.0) {
            return 0;
        }

        return static_cast<int>(std::clamp(stepCount, 0.0, static_cast<double>(kTrajectoryPredictionStepMax)));
    }

    int trajectoryPredictionStepCount() const {
        return trajectoryPredictionStepCountForRate(trajectoryPredictionTickRate());
    }

    int trajectoryPredictionStepCount(float simulationDelta) const {
        double predictionTickRate = trajectoryPredictionTickRate();
        if (std::isfinite(simulationDelta) && simulationDelta > 0.0f) {
            predictionTickRate = std::max(kBaseTickRate, 60.0 / static_cast<double>(simulationDelta));
        }

        return trajectoryPredictionStepCountForRate(predictionTickRate);
    }

    float trajectorySimulationDelta() const {
        return static_cast<float>(60.0 / runtimeTickRate());
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
        tickStartTimestamp = 0.0;
        respawnTickIndex = -1;
        queuedMacroCommands.clear();
        queuedCaptureCommands.clear();
        cbsCaptureProcessingQueue = false;
        cbsPlaybackProcessingQueue = false;
    }

    void clearPersistenceRuntimeState() {
        persistenceCaptureAttempt = {};
        persistenceCaptureDeathRecorded = false;
        persistencePlaybackAttemptIndex = 0;
        persistencePlaybackDeathPending = false;
        persistenceRenderBaseTick = 0;
    }

    void resetPersistenceCaptureAttempt() {
        persistenceCaptureAttempt = {};
        persistenceCaptureDeathRecorded = false;
    }

    void recordTTRAction(int tick, int button, bool player2, bool pressed, float offset, double cbsTimeOffset) {
        if (!activeTTR) {
            return;
        }

        if (canUsePersistenceCapture()) {
            activeTTR->recordAction(persistenceCaptureAttempt.inputs, tick, button, player2, pressed, offset, cbsTimeOffset);
        } else {
            activeTTR->recordAction(tick, button, player2, pressed, offset, cbsTimeOffset);
        }
    }

    void recordTTRAnchor(int tick, PlayerObject* player1, PlayerObject* player2, bool isPlatformer, bool isDual) {
        if (!activeTTR) {
            return;
        }

        if (canUsePersistenceCapture()) {
            if (persistenceCaptureAttempt.inputs.empty()) {
                return;
            }
            activeTTR->recordAnchor(persistenceCaptureAttempt.anchors, tick, player1, player2, isPlatformer, isDual);
        } else {
            if (activeTTR->inputs.empty()) {
                return;
            }
            activeTTR->recordAnchor(tick, player1, player2, isPlatformer, isDual);
        }
    }

    void finalizePersistenceCaptureDeath(int deathTick, bool deathPlayer2) {
        if (!canUsePersistenceCapture() || persistenceCaptureDeathRecorded) {
            return;
        }

        persistenceCaptureAttempt.deathTick = std::max(1, deathTick);
        persistenceCaptureAttempt.deathPlayer2 = deathPlayer2;
        if (persistenceCaptureAttempt.hasData()) {
            activeTTR->persistenceAttempts.push_back(std::move(persistenceCaptureAttempt));
            markActiveMacroDirty();
        }
        persistenceCaptureAttempt = {};
        persistenceCaptureDeathRecorded = true;
    }

    void completePersistenceCaptureAttempt() {
        if (!canUsePersistenceCapture()) {
            return;
        }

        activeTTR->inputs = std::move(persistenceCaptureAttempt.inputs);
        activeTTR->anchors = std::move(persistenceCaptureAttempt.anchors);
        persistenceCaptureAttempt = {};
        persistenceCaptureDeathRecorded = false;
        markActiveMacroDirty();
        disablePersistenceModeSafeguard();
    }

    void markPersistencePlaybackDeathPending() {
        if (isPersistencePlaybackFailureAttempt()) {
            persistencePlaybackDeathPending = true;
        }
    }

    void advancePersistencePlaybackAfterReset() {
        if (!isPersistencePlaybackActive() || !persistencePlaybackDeathPending) {
            return;
        }

        if (auto const* attempt = activePersistencePlaybackAttempt()) {
            persistenceRenderBaseTick += std::max(1, attempt->deathTick);
        }
        ++persistencePlaybackAttemptIndex;
        persistencePlaybackDeathPending = false;
        executeIndex = 0;
        playbackAnchorIndex = 0;
        resetTimingTracking();
    }

    bool shouldTriggerPersistencePlaybackDeath(int effectiveTick) const {
        auto const* attempt = activePersistencePlaybackAttempt();
        return attempt
            && attempt->deathTick > 0
            && effectiveTick >= attempt->deathTick
            && !persistencePlaybackDeathPending;
    }

    bool shouldResetAfterPersistencePlaybackDeath(PlayLayer* playLayer) const {
        if (!playLayer || !isPersistencePlaybackFailureAttempt() || !persistencePlaybackDeathPending) {
            return false;
        }

        bool p1Dead = playLayer->m_player1 && playLayer->m_player1->m_isDead;
        bool p2Dead = playLayer->m_player2 && playLayer->m_player2->m_isDead;
        return p1Dead || p2Dead;
    }

    PlayerObject* persistencePlaybackDeathPlayer(PlayLayer* playLayer) const {
        if (!playLayer) {
            return nullptr;
        }

        auto const* attempt = activePersistencePlaybackAttempt();
        if (attempt && attempt->deathPlayer2 && playLayer->m_player2) {
            return playLayer->m_player2;
        }
        return playLayer->m_player1;
    }

    int persistenceFailedAttemptsTotalTicks() const {
        if (!activeTTR) {
            return 0;
        }

        int total = 0;
        for (auto const& attempt : activeTTR->persistenceAttempts) {
            total += std::max(1, attempt.deathTick);
        }
        return total;
    }

    int activeMacroEndTick() const {
        if (ttrMode && activeTTR && !activeTTR->inputs.empty()) {
            return activeTTR->inputs.back().tick;
        }
        if (!ttrMode && activeMacro && !activeMacro->inputs.empty()) {
            return static_cast<int>(activeMacro->inputs.back().frame);
        }
        return 0;
    }

    int persistencePlaybackTotalTicks() const {
        if (!activeTTR) {
            return 0;
        }
        return persistenceFailedAttemptsTotalTicks() + activeMacroEndTick();
    }

    int renderLocalTick() const {
        return std::max(0, lastTickIndex + tickOffset);
    }

    int renderTimelineTick() const {
        if (isPersistencePlaybackActive()) {
            return persistenceRenderBaseTick + renderLocalTick();
        }
        return lastTickIndex;
    }

    std::vector<PlaybackAnchor>* activeAnchors() {
        if (ttrMode) {
            if (auto* attempt = activePersistencePlaybackAttempt()) {
                return &attempt->anchors;
            }
            return activeTTR ? &activeTTR->anchors : nullptr;
        }
        return activeMacro ? &activeMacro->anchors : nullptr;
    }

    std::vector<PlaybackAnchor> const* activeAnchors() const {
        if (ttrMode) {
            if (auto const* attempt = activePersistencePlaybackAttempt()) {
                return &attempt->anchors;
            }
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

    void clearCheckpointResumedHolds() {
        for (auto& playerHolds : checkpointResumedHolds) {
            playerHolds.fill(false);
        }
    }

    void clearQueuedSubstepState() {
        queuedSubstepSegments.clear();
        substepMidStep = false;
        substepMacroDispatch = false;
        substepCaptureOffset = -1.0f;
    }

    void queueMacroCommand(int button, bool down, bool player2, double timestamp) {
        queuedMacroCommands.push_back({ button, down, player2, timestamp });
    }

    void clearQueuedMacroCommands() {
        queuedMacroCommands.clear();
    }

    void queueCbsCaptureCommand(int button, bool down, bool player2, double timestamp) {
        queuedCaptureCommands.push_back({ button, down, player2, timestamp });
    }

    std::optional<QueuedCaptureCommand> popCbsCaptureCommand(int button, bool down, bool player2) {
        if (!cbsCaptureProcessingQueue) {
            return std::nullopt;
        }

        auto it = std::find_if(
            queuedCaptureCommands.begin(),
            queuedCaptureCommands.end(),
            [&](QueuedCaptureCommand const& command) {
                return command.button == button
                    && command.down == down
                    && command.player2 == player2;
            }
        );
        if (it == queuedCaptureCommands.end()) {
            return std::nullopt;
        }

        auto result = *it;
        queuedCaptureCommands.erase(it);
        return result;
    }

    bool hasQueuedSubstepTick() const {
        return !queuedSubstepSegments.empty();
    }

    bool isCheckpointHoldResumed(int playerSlot, int button) const {
        if (playerSlot < 0 || playerSlot >= static_cast<int>(checkpointResumedHolds.size())) {
            return false;
        }
        if (button < 1 || button > 3) {
            return false;
        }
        return checkpointResumedHolds[playerSlot][static_cast<size_t>(button - 1)];
    }

    void setCheckpointHoldResumed(int playerSlot, int button, bool resumed) {
        if (playerSlot < 0 || playerSlot >= static_cast<int>(checkpointResumedHolds.size())) {
            return;
        }
        if (button < 1 || button > 3) {
            return;
        }
        checkpointResumedHolds[playerSlot][static_cast<size_t>(button - 1)] = resumed;
    }

    Renderer renderer;

    static ReplayEngine* get() {
        static auto* singleton = new ReplayEngine();
        return singleton;
    }

    void triggerAudio(bool secondPlayer, int actionType, bool pressed);

    void resetDeferredInputState();
    void buildQueuedSubstepSegments(
        std::vector<QueuedSubstepInput> actions,
        std::deque<QueuedSubstepSegment>& output
    ) const;
    void dispatchQueuedSubstepInput(PlayLayer* playLayer, QueuedSubstepInput const& input);
    void simulateQueuedSubstepTick(
        PlayLayer* playLayer,
        PlayerObject* player1,
        PlayerObject* player2,
        float stepDelta,
        std::deque<QueuedSubstepSegment>& segments,
        std::function<void(QueuedSubstepInput const&)> dispatchOverride = {}
    );
    void armQueuedSubstepTick(std::vector<QueuedSubstepInput> actions);
    void truncateRecordedDataAfter(int tick);
    void prepareCaptureRestore(int tick);
    void restoreLockedRngState(uintptr_t rngState);
    void pruneStoredRestorePoints(PlayLayer* playLayer, int restoredTick);
    void clearFrameStepState();
    int computeFrameStepMusicTimeMS(PlayLayer* playLayer) const;
    void requestFrameStepMusicSync(PlayLayer* playLayer);
    void syncFrameStepAudio(FMODAudioEngine* audio);
    void setFrameStepEnabled(bool enabled, PlayLayer* playLayer);
    void bridgeUserHoldsToPlayer(PlayLayer* playLayer);

    void processHotkeys();
};

#endif
