#ifndef _ToastyReplay_hpp
#define _ToastyReplay_hpp
#include "replay.hpp"
#include <Geode/Bindings.hpp>
#include <vector>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

enum RecordingState {
    NONE, RECORD, PLAYBACK
};

enum ErrorCode {
    ERROR_NONE,
    KEY_NOT_FOUND_ERROR,
    KEY_INVALID_ERROR,
    CURL_FAILED_ERROR,
    INVALID_HWID_ERROR,
    UNKNOWN_ERROR,
    KEY_LINKED_DIFFERENT_COMPUTER_ERROR
};

class ToastyReplay {
public:
    RecordingState state = NONE;
    ErrorCode error = ERROR_NONE;

    bool fmodified = false;

    float extraTPS = 0.f;

    bool disableRender = false;
    bool ignoreBypass = false;
    bool justLoaded = false;
    bool ignoreInput = false;
    bool frameAdvance = false;
    bool doAdvance = false;
    bool stepFrame = false;
    bool internalRenderer = false;
    bool prevFrameAdvance = false;
    bool frameAdvanceKeyPressed = false;
    bool speedHackAudio = true;
    bool ignoreManualInput = false;
    bool isReplayInput = false;
    bool safeMode = false;
    bool showTrajectory = false;
    bool noclip = false;
    bool noclipAccuracyEnabled = false;
    float noclipAccuracyLimit = 0.0f;
    
    int noclipDeaths = 0;
    int noclipTotalFrames = 0;
    
    bool seedEnabled = false;
    unsigned int seedValue = 1;
    uintptr_t macroSeed = 0;

    int trajectoryLength = 312;
    
    double speed = 1;
    double tps = 240.f;
    ReplayData* currentReplay = nullptr;

    std::unordered_map<CheckpointObject*, CheckpointData> checkpoints;
    int previousFrame = 0;
    int respawnFrame = -1;
    size_t playbackIndex = 0;
    size_t frameFixIndex = 0;
    bool ignoreRecordAction = false;
    bool restart = false;
    bool firstAttempt = false;
    bool frameFixes = false;
    bool inputFixes = false;
    int frameFixesLimit = 240;
    int ignoreFrame = -1;
    int ignoreJumpButton = -1;
    int delayedFrameReleaseMain[2] = { -1, -1 };
    int delayedFrameInput[2] = { -1, -1 };
    int delayedFrameRelease[2][2] = { { -1, -1 }, { -1, -1 } };
    bool heldButtons[6] = { false, false, false, false, false, false };
    bool wasHolding[6] = { false, false, false, false, false, false };
    bool addSideHoldingMembers[2] = { false, false };
    bool ignoreStopDashing[2] = { false, false };
    bool creatingTrajectory = false;
    int currentSession = 0;
    int lastAutoSaveFrame = 0;

    std::vector<std::string> savedReplays;
    
    int keybind_frameAdvance = 0x56;
    int keybind_speedhackAudio = 0;
    int keybind_safeMode = 0;
    int keybind_trajectory = 0;
    int keybind_noclip = 0;
    int keybind_seed = 0;

    void refreshReplays() {
        savedReplays.clear();
        auto dir = geode::prelude::Mod::get()->getSaveDir() / "replays";
        if (std::filesystem::exists(dir)) {
            for (auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".gdr") {
                    savedReplays.push_back(entry.path().stem().string());
                }
            }
        }
    }

    void startRecording(GJGameLevel* level) {
        state = RECORD;
        createNewReplay(level);
    }

    void createNewReplay(GJGameLevel* level) {
        if (currentReplay) delete currentReplay;
        currentReplay = new ReplayData();
        if (level) {
            currentReplay->levelInfo.id = level->m_levelID;
            currentReplay->levelInfo.name = level->m_levelName;
            currentReplay->name = level->m_levelName;
        }
        currentReplay->framerate = tps;
    }

    void startPlayback() {
        if (!currentReplay || currentReplay->inputs.empty()) return;

        state = PLAYBACK;
        playbackIndex = 0;
        frameFixIndex = 0;
        firstAttempt = true;
        respawnFrame = -1;

        PlayLayer* pl = PlayLayer::get();
        if (pl) {
            if (pl->m_isPracticeMode) {
                pl->togglePracticeMode(false);
            }
            pl->resetLevel();
        }
    }

    void stopPlayback() {
        state = NONE;
    }

    static auto* get() {
        static ToastyReplay* instance = new ToastyReplay();
        return instance;
    }

    void playSound(bool p2, int button, bool down);

    void handleKeybinds();
};
#endif
