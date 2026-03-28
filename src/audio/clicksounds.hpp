#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <string>
#include <filesystem>
#include <random>
#include <unordered_map>
#include <mutex>
#include <chrono>

using namespace geode::prelude;

struct ClickPack {
    std::string name;
    std::vector<std::string> hardClicks;
    std::vector<std::string> softClicks;
    std::vector<std::string> hardReleases;
    std::vector<std::string> softReleases;
    std::vector<std::string> releases;
    std::vector<std::string> noiseFiles;

    float hardVolume = 1.0f;
    float softVolume = 0.5f;
    float releaseVolume = 0.8f;

    int hardCount() const { return static_cast<int>(hardClicks.size()); }
    int softCount() const { return static_cast<int>(softClicks.size()); }
    int releaseCount() const { return static_cast<int>(hardReleases.size() + softReleases.size() + releases.size()); }
    int noiseCount() const { return static_cast<int>(noiseFiles.size()); }
    bool empty() const {
        return hardClicks.empty() && softClicks.empty() &&
            hardReleases.empty() && softReleases.empty() &&
            releases.empty();
    }
};

struct MacroAction;

class ClickSoundManager {
public:
    static ClickSoundManager* get();

    bool enabled = false;
    bool playDuringPlayback = true;
    bool separateP2Clicks = false;

    float softness = 0.5f;
    float clickDelayMin = 0.0f;
    float clickDelayMax = 0.0f;

    bool backgroundNoiseEnabled = false;
    float backgroundNoiseVolume = 0.5f;

    ClickPack p1Pack;
    ClickPack p2Pack;

    std::string activePackName;
    std::string activePackNameP2;

    std::vector<std::string> availablePacks;
    std::vector<std::string> availablePacksP2;

    void scanClickPacks();
    void scanClickPacksP2();
    void loadClickPack(const std::string& packName, ClickPack& target, bool isP2 = false);
    void playClick(bool pressed, bool isPlayer2);
    void startBackgroundNoise();
    void stopBackgroundNoise();
    void shutdown();
    void updatePendingClicks();
    void clearPendingClicks();
    void openClickFolder();
    void openClickFolderP2();

    std::vector<float> generateClickAudio(
        const std::vector<MacroAction>& actions,
        float tickRate, float duration, int sampleRate,
        int startTick = 0, bool applyDelay = false,
        bool trueTwoPlayerMode = false);

    void preDecodeForRender(int sampleRate);
    std::unordered_map<std::string, std::vector<float>> decodedClickCache;

    std::filesystem::path getClicksDir() const;
    std::filesystem::path getClicksP2Dir() const;

    FMOD::Channel* bgNoiseChannel = nullptr;

private:
    struct PendingClick {
        bool pressed = false;
        bool isPlayer2 = false;
        std::chrono::steady_clock::time_point playAt;
    };

    FMOD::ChannelGroup* channelGroup = nullptr;
    FMOD::Sound* bgNoiseSound = nullptr;
    std::unordered_map<std::string, FMOD::Sound*> soundCache;
    std::mt19937 rng{std::random_device{}()};
    std::vector<PendingClick> pendingClicks;
    std::mutex pendingClickMutex;

    void ensureChannelGroup();
    bool shouldUseP2Pack(bool requestedPlayer2, bool trueTwoPlayerMode) const;
    std::string pickRandomFile(const std::vector<std::string>& files);
    void playFile(const std::string& path, float volume);
    void playResolvedClick(bool pressed, bool isPlayer2);
    FMOD::Sound* getCachedSound(const std::string& path);
    void clearSoundCache();
    std::vector<float> decodeClickToRaw(const std::string& filePath, int targetSampleRate);
};
