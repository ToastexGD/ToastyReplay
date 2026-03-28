#pragma once

#include "ffmpeg_events.hpp"

#include <Geode/Geode.hpp>

#include <deque>
#include <filesystem>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

enum RenderAudioMode {
    AUDIO_OFF = 0,
    AUDIO_SONG = 1
};

class FrameCaptureService {
public:
    void configure(size_t bytesPerFrame, size_t maxBufferedFrames = 2);
    void clear();
    bool hasPendingFrame() const;
    std::vector<uint8_t> createFrameBuffer() const;
    bool submit(std::vector<uint8_t>&& frame);
    std::vector<uint8_t> takeFrame();

private:
    size_t m_bytesPerFrame = 0;
    size_t m_maxBufferedFrames = 2;
    mutable std::mutex m_lock;
    std::condition_variable m_pendingChanged;
    std::deque<std::vector<uint8_t>> m_pendingFrames;
};

struct EncodeSession {
    bool usingApi = false;
    std::filesystem::path outputFile;
    std::filesystem::path tempOutputFile;

    void reset() {
        usingApi = false;
        outputFile.clear();
        tempOutputFile.clear();
    }
};

struct AudioCaptureService {
    int sampleRate = 44100;
    std::vector<float> rawMixBuffer;

    void reset() {
        sampleRate = 44100;
        rawMixBuffer.clear();
    }
};

class RenderTexture {
public:
    unsigned width = 0;
    unsigned height = 0;
    int old_fbo = 0;
    int old_rbo = 0;
    unsigned fbo = 0;
    cocos2d::CCTexture2D* texture = nullptr;

    void begin();
    void end();
    void capture(FrameCaptureService& frameCapture, cocos2d::CCNode* overlay = nullptr);
};

class Renderer {
public:
    Renderer()
        : width(1920),
          height(1080),
          fps(60) {}

    bool levelFinished = false;
    bool recording = false;
    bool pause = false;
    int audioMode = AUDIO_OFF;
    float ogMusicVol = 1.f;
    float ogSFXVol = 1.f;
    float sfxVolume = 1.f;
    float musicVolume = 1.f;

    bool usingApi = false;
    bool dontRender = false;
    bool isPlatformer = false;
    bool includeClickSounds = false;
    int finishFrame = 0;
    int levelStartFrame = 0;

    float stopAfter = 3.f;
    float timeAfter = 0.f;
    unsigned width;
    unsigned height;
    unsigned fps;
    double lastFrame_t = 0;
    double extra_t = 0;

    RenderTexture renderTex;
    FrameCaptureService frameCapture;
    EncodeSession encodeSession;
    AudioCaptureService audioCapture;
    std::string codec;
    std::string bitrate = "30M";
    std::string extraArgs;
    std::string videoArgs;
    std::string extraAudioArgs;
    std::string path;
    std::string ffmpegPath;
    std::unordered_set<int> renderedFrames;

    FMODAudioEngine* fmod = nullptr;
    cocos2d::CCSize ogRes = { 0, 0 };
    float ogScaleX = 1.f;
    float ogScaleY = 1.f;

    cocos2d::CCLabelBMFont* watermarkLabel = nullptr;
    cocos2d::CCLabelBMFont* progressLabel = nullptr;

    void captureFrame();
    void changeRes(bool original);

    void start();
    void stop(int frame = 0);
    void handleRecording(PlayLayer* playLayer, int frame);

    bool toggle();
    bool shouldUseAPI();

    int getCurrentFrame() const;
    float getTPS() const;
};
