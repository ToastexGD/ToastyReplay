#pragma once

#include "ffmpeg_events.hpp"
#include "render_config.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <deque>
#include <filesystem>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
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
    void notifyStop();
    bool hasPendingFrame() const;
    std::vector<uint8_t> acquireBuffer();
    bool submit(std::vector<uint8_t>&& frame);
    std::vector<uint8_t> takeFrame();
    void releaseBuffer(std::vector<uint8_t>&& frame);

private:
    size_t m_bytesPerFrame = 0;
    size_t m_maxBufferedFrames = 2;
    mutable std::mutex m_lock;
    std::condition_variable m_pendingChanged;
    std::deque<std::vector<uint8_t>> m_pendingFrames;
    std::vector<std::vector<uint8_t>> m_freeList;
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

struct RenderSoundEvent {
    std::string path;
    float time;
    float volume;
    float speed;        // pitch multiplier (1.0 = normal; ignored in mix for now)
};

class RenderTexture {
public:
    unsigned width = 0;
    unsigned height = 0;
    int old_fbo = 0;
    int old_rbo = 0;
    unsigned fbo = 0;
    bool nv12 = false;
    cocos2d::CCTexture2D* texture = nullptr;

    void begin(bool wantNv12, FrameCaptureService& frameCapture);
    void end();
    void capture(FrameCaptureService& frameCapture, cocos2d::CCNode* overlay = nullptr);
    void flushPbo(FrameCaptureService& frameCapture);

private:
    static constexpr int kPboRingDepth = 3;
    GLuint  m_pbos[kPboRingDepth] = {};
    int     m_pboCount      = kPboRingDepth;
    int     m_pboFrameCount = 0;
    size_t  m_pboSize       = 0;
    // NV12 pass: [0] = Y plane (R8, w×h), [1] = UV plane (RG8, w/2×h/2)
    GLuint  m_prog[2]     = {};
    GLuint  m_planeFbo[2] = {};
    GLuint  m_planeTex[2] = {};
    // diagnostic: per-phase capture cost, to see where 4K time goes
    double  m_dbgVisitSec = 0.0;
    double  m_dbgMapSec   = 0.0;
    double  m_dbgCopySec  = 0.0;
    int     m_dbgFrames   = 0;

    // Async copy worker: the per-frame PBO->buffer memcpy is the only capture cost
    // that grows with resolution, so at 4K it stalls the render thread. The worker
    // does that copy (and the encode-queue submit) while the render thread keeps going;
    // each PBO is unmapped on the render thread only after its copy finishes.
    struct CopyJob { const uint8_t* src; int pboIndex; bool nv12; unsigned w, h; size_t size; };
    std::thread             m_copyThread;
    std::mutex              m_copyMutex;
    std::condition_variable m_copyCv;
    std::deque<CopyJob>     m_copyJobs;
    bool                    m_copyStop      = false;
    int                     m_copyInFlight  = 0;
    bool                    m_pboCopyBusy[kPboRingDepth] = {};  // guarded by m_copyMutex
    bool                    m_pboMapped[kPboRingDepth]   = {};  // render thread only
    FrameCaptureService*    m_copyTarget    = nullptr;

    bool initNv12Pass();
    void destroyNv12Pass();
    void runNv12Pass();
    void startCopyWorker(FrameCaptureService& frameCapture);
    void stopCopyWorker();
    void releasePbo(int idx);
    void submitPbo(int pboIndex, FrameCaptureService& frameCapture, const char* abortMsg);
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
    bool hideEndscreen = false;
    bool hideLevelComplete = false;
    int finishFrame = 0;
    int levelStartFrame = 0;
    int cadenceLogFrames = 0;  // diagnostic: output frames emitted, for cadence logging
    bool clockPrimed = false;
    bool leadInFixEligible = false;
    double leadInSeconds = 0.0;

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
    std::string audioCodec = "aac";
    std::filesystem::path path;
    std::filesystem::path ffmpegPath;
    std::vector<bool> m_capturedFrameSet;
    std::vector<RenderSoundEvent> m_capturedSounds;

    FMODAudioEngine* fmod = nullptr;
    cocos2d::CCSize ogRes = { 0, 0 };
    float ogScaleX = 1.f;
    float ogScaleY = 1.f;

    cocos2d::CCLabelBMFont* watermarkLabel = nullptr;
    cocos2d::CCLabelBMFont* progressLabel = nullptr;

    void captureFrame();
    void changeRes(bool original);

    void start();
    void start(const RenderConfig& config);
    void stop(int frame = 0);
    void handleRecording(PlayLayer* playLayer, int frame);

    bool toggle();
    bool shouldUseAPI();

    int getCurrentFrame() const;
    float getTPS() const;

    std::optional<RenderConfig> m_pendingConfig;

private:
    std::thread m_encodeThread;
    // true from thread launch until runEncodeLoop fully returns (frames drained,
    // audio muxed). The thread is detached and keeps touching shared render state
    // after recording=false, so a new render must not start while this is set.
    std::atomic<bool> m_encodeActive{false};
    std::optional<ResolvedEncodeParams> m_resolvedParams;
    std::string m_extOverride;

    bool resolveEncoder();
    void restoreAudioVolumes();
    bool isLevelComplete();
    void startFromPending();
    void runEncodeLoop(std::filesystem::path songFile, float songOffset, bool fadeIn, bool fadeOut, std::string extension, int64_t bitrateApi);
};
