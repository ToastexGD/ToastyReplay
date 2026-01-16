#ifndef _ToastyReplay_hpp
#define _ToastyReplay_hpp
#include "replay.hpp"
#include <Geode/Bindings.hpp>
#include <vector>
#include <string>

using namespace geode::prelude;

enum zState {
    NONE, RECORD, PLAYBACK
};

enum zError {
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
    zState state = NONE;
    zError error = ERROR_NONE;

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
    zReplay* currentReplay = nullptr;

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
        currentReplay = new zReplay();
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
        
        // Trigger level reset when starting playback
        PlayLayer* pl = PlayLayer::get();
        if (pl) {
            // If in practice mode, exit it first to reset from start
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
