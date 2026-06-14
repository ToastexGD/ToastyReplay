#include "render/renderer.hpp"
#include "lang/localization.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "render/subprocess.hpp"
#include "gui/gui.hpp"
#include "hacks/physicsbypass.hpp"
#include "utils.hpp"

#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/CCParticleSystemQuad.hpp>
#include <Geode/modify/AudioEffectsLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/utils/web.hpp>

#include <thread>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cctype>
#include <fstream>
#include <cmath>
#include <string_view>
#include <fmt/format.h>

using namespace geode::prelude;

static constexpr int kApiMixSampleRate = 44100;
static constexpr int kMobileMp4AudioChannels = 2;
static constexpr int kMobileMp4AudioBitrateKbps = 192;
static constexpr double kRenderAudioFadeDuration = 2.0;
static constexpr double kRenderAudioFadeOutLead = 3.5;

struct RenderClock {
    double accumulated = 0.0;
    double frameDelta = 0.0;
    double baseline = 0.0;
    void advance(double renderTime) {
        accumulated = renderTime + (accumulated - baseline) - frameDelta;
    }
    void reset() { accumulated = 0.0; frameDelta = 0.0; baseline = 0.0; }
};

static RenderClock s_renderClock;

static std::string trString(std::string_view key) {
    return std::string(toasty::lang::tr(key));
}

template <class... Args>
static std::string trFormat(std::string_view key, Args&&... args) {
    return toasty::lang::trf(key, std::forward<Args>(args)...);
}

template <class T>
static T loadSavedValueWithFallback(
    Mod* mod,
    std::string_view canonicalKey,
    T defaultValue,
    std::initializer_list<std::string_view> legacyKeys = {}
) {
    std::string canonicalKeyStr(canonicalKey);
    if (mod->hasSavedValue(canonicalKeyStr)) {
        return mod->getSavedValue<T>(canonicalKeyStr, defaultValue);
    }

    for (auto legacyKey : legacyKeys) {
        std::string legacyKeyStr(legacyKey);
        if (mod->hasSavedValue(legacyKeyStr)) {
            return mod->getSavedValue<T>(legacyKeyStr, defaultValue);
        }
    }

    return defaultValue;
}

static int sanitizeRenderWatermarkFont(int value) {
    return std::clamp(
        value,
        static_cast<int>(RENDER_WATERMARK_FONT_NORMAL_PUSAB),
        static_cast<int>(RENDER_WATERMARK_FONT_GOLD_PUSAB)
    );
}

static int sanitizeRenderWatermarkCorner(int value) {
    return std::clamp(
        value,
        static_cast<int>(RENDER_WATERMARK_CORNER_TOP_LEFT),
        static_cast<int>(RENDER_WATERMARK_CORNER_BOTTOM_RIGHT)
    );
}

static float sanitizeRenderWatermarkScale(float value) {
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, 0.25f, 3.0f);
}

static bool pathExists(std::filesystem::path const& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec) && !ec;
}

static bool pathContainsMarker(std::filesystem::path const& path, std::string_view marker) {
    return toasty::pathToUtf8(path).find(marker) != std::string::npos;
}

static bool shouldUseMobileMp4Audio(std::filesystem::path const& outputPath) {
    auto extension = outputPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });

    return extension == ".mp4" || extension == ".m4v";
}

static bool writeRawAudioWav(
    std::filesystem::path const& outputPath,
    std::span<float const> rawAudio,
    uint32_t sampleRate
) {
    std::ofstream wavFile(outputPath, std::ios::binary);
    if (!wavFile.is_open()) {
        return false;
    }

    uint32_t dataSize = static_cast<uint32_t>(rawAudio.size() * sizeof(int16_t));
    uint32_t fileSize = 36 + dataSize;
    uint16_t channels = 2;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;

    wavFile.write("RIFF", 4);
    wavFile.write(reinterpret_cast<const char*>(&fileSize), 4);
    wavFile.write("WAVE", 4);
    wavFile.write("fmt ", 4);
    uint32_t fmtSize = 16;
    wavFile.write(reinterpret_cast<const char*>(&fmtSize), 4);
    uint16_t audioFormat = 1;
    wavFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
    wavFile.write(reinterpret_cast<const char*>(&channels), 2);
    wavFile.write(reinterpret_cast<const char*>(&sampleRate), 4);
    wavFile.write(reinterpret_cast<const char*>(&byteRate), 4);
    wavFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
    wavFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    wavFile.write("data", 4);
    wavFile.write(reinterpret_cast<const char*>(&dataSize), 4);

    for (float sample : rawAudio) {
        int16_t s = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        wavFile.write(reinterpret_cast<const char*>(&s), 2);
    }

    return wavFile.good();
}

static void cleanupTempFile(
    std::filesystem::path const& filePath,
    std::string_view label,
    int attempts = 5,
    std::chrono::milliseconds retryDelay = std::chrono::milliseconds(200)
) {
    if (filePath.empty()) {
        return;
    }

    for (int attempt = 0; attempt < attempts; ++attempt) {
        std::error_code ec;
        std::filesystem::remove(filePath, ec);
        if (!ec) {
            return;
        }

        if (attempt + 1 == attempts) {
            log::warn("Failed to remove {} '{}': {}", label, toasty::pathToUtf8(filePath), ec.message());
            return;
        }

        std::this_thread::sleep_for(retryDelay);
    }
}

static size_t getInterleavedStereoSampleCount(double durationSec, int sampleRate) {
    if (durationSec <= 0.0 || sampleRate <= 0) {
        return 0;
    }

    return static_cast<size_t>(std::ceil(durationSec * static_cast<double>(sampleRate))) * 2;
}

static void fitAudioToDuration(std::vector<float>& rawAudio, double durationSec, int sampleRate) {
    size_t expectedSamples = getInterleavedStereoSampleCount(durationSec, sampleRate);
    if (expectedSamples == 0) {
        rawAudio.clear();
        return;
    }

    rawAudio.resize(expectedSamples, 0.0f);
}

static void applyRenderAudioFades(
    std::vector<float>& rawAudio,
    int channels,
    int sampleRate,
    bool fadeIn,
    bool fadeOut,
    double totalTime,
    double timeAfter
) {
    if (rawAudio.empty() || channels <= 0 || sampleRate <= 0) {
        return;
    }

    size_t frameCount = rawAudio.size() / static_cast<size_t>(channels);
    if (frameCount == 0) {
        return;
    }

    auto applyFadeRange = [&](size_t startFrame, size_t fadeFrames, bool fadeUp) {
        if (fadeFrames == 0 || startFrame >= frameCount) {
            return;
        }

        size_t endFrame = std::min(startFrame + fadeFrames, frameCount);
        size_t span = endFrame - startFrame;
        if (span == 0) {
            return;
        }

        for (size_t frame = startFrame; frame < endFrame; ++frame) {
            float factor = 1.0f;
            if (span > 1) {
                float step = static_cast<float>(frame - startFrame) / static_cast<float>(span - 1);
                factor = fadeUp ? step : (1.0f - step);
            } else if (!fadeUp) {
                factor = 0.0f;
            }

            size_t sampleIndex = frame * static_cast<size_t>(channels);
            for (int channel = 0; channel < channels; ++channel) {
                rawAudio[sampleIndex + static_cast<size_t>(channel)] *= factor;
            }
        }
    };

    size_t fadeFrames = static_cast<size_t>(std::ceil(kRenderAudioFadeDuration * static_cast<double>(sampleRate)));
    if (fadeIn) {
        applyFadeRange(0, fadeFrames, true);
    }

    double safeTimeAfter = std::clamp(timeAfter, 0.0, totalTime);
    double fadeOutStart = totalTime - safeTimeAfter - kRenderAudioFadeOutLead;
    if (fadeOut && fadeOutStart > 0.0) {
        size_t fadeOutFrame = static_cast<size_t>(std::floor(fadeOutStart * static_cast<double>(sampleRate)));
        applyFadeRange(fadeOutFrame, fadeFrames, false);
    }
}

static int getActiveMacroEndTick(ReplayEngine const* engine) {
    if (!engine) {
        return 0;
    }

    return engine->activeMacroEndTick();
}

static int getRenderProgressPercent(Renderer const& renderer, PlayLayer* playLayer) {
    if (!playLayer) {
        return 0;
    }

    auto* engine = ReplayEngine::get();
    if (engine && engine->isPersistencePlaybackActive()) {
        int totalTicks = engine->persistencePlaybackTotalTicks();
        if (totalTicks > 0) {
            int currentTick = renderer.getCurrentFrame();
            double progress = static_cast<double>(currentTick) / static_cast<double>(totalTicks);
            int percent = std::clamp(static_cast<int>(std::round(progress * 100.0)), 0, 100);
            if (!playLayer->m_hasCompletedLevel && !playLayer->m_levelEndAnimationStarted) {
                percent = std::min(percent, 99);
            }
            return percent;
        }
    }

    bool platformer = playLayer->m_levelSettings && playLayer->m_levelSettings->m_platformerMode;
    if (!platformer) {
        return std::clamp(playLayer->getCurrentPercentInt(), 0, 100);
    }

    int endTick = getActiveMacroEndTick(engine);
    if (endTick > renderer.levelStartFrame) {
        int currentTick = renderer.getCurrentFrame();
        if (engine) {
            currentTick += engine->tickOffset;
        }

        double progress = static_cast<double>(currentTick - renderer.levelStartFrame) /
            static_cast<double>(endTick - renderer.levelStartFrame);
        int percent = std::clamp(static_cast<int>(std::round(progress * 100.0)), 0, 100);
        if (!playLayer->m_hasCompletedLevel && !playLayer->m_levelEndAnimationStarted) {
            percent = std::min(percent, 99);
        }
        return percent;
    }

    int percent = std::clamp(playLayer->getCurrentPercentInt(), 0, 100);
    if (percent >= 100 && !playLayer->m_hasCompletedLevel && !playLayer->m_levelEndAnimationStarted) {
        return 0;
    }
    return percent;
}

static std::vector<float> resampleInterleavedAudio(
    std::vector<float> const& input,
    int channels,
    int sourceRate,
    int targetRate
) {
    if (input.empty() || channels <= 0 || sourceRate <= 0 || targetRate <= 0 || sourceRate == targetRate) {
        return input;
    }

    size_t sourceFrames = input.size() / static_cast<size_t>(channels);
    if (sourceFrames == 0) {
        return {};
    }

    double ratio = static_cast<double>(targetRate) / static_cast<double>(sourceRate);
    size_t targetFrames = static_cast<size_t>(std::ceil(static_cast<double>(sourceFrames) * ratio));
    std::vector<float> output(targetFrames * static_cast<size_t>(channels), 0.0f);

    for (size_t frame = 0; frame < targetFrames; ++frame) {
        double sourceFrame = static_cast<double>(frame) / ratio;
        size_t frame0 = static_cast<size_t>(sourceFrame);
        double frac = sourceFrame - static_cast<double>(frame0);
        size_t frame1 = std::min(frame0 + 1, sourceFrames - 1);

        for (int channel = 0; channel < channels; ++channel) {
            float sample0 = input[frame0 * channels + channel];
            float sample1 = input[frame1 * channels + channel];
            output[frame * channels + channel] = static_cast<float>(sample0 * (1.0 - frac) + sample1 * frac);
        }
    }

    return output;
}

#ifdef GEODE_IS_WINDOWS
static std::filesystem::path resolveFfmpegExecutable(std::filesystem::path const& configuredPath) {
    if (!configuredPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(configuredPath, ec) && configuredPath.filename() == "ffmpeg.exe") {
            return configuredPath;
        }
    }

    wchar_t resolvedPath[MAX_PATH] = {};
    DWORD resolvedLength = SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, resolvedPath, nullptr);
    if (resolvedLength == 0 || resolvedLength >= MAX_PATH) {
        return {};
    }

    return std::filesystem::path(resolvedPath);
}

static bool isUsableFfmpegExecutable(std::filesystem::path const& ffmpegPath) {
    if (ffmpegPath.empty()) {
        return false;
    }

    if (!pathExists(ffmpegPath)) {
        return false;
    }

    std::string command = fmt::format(
        "\"{}\" -hide_banner -loglevel error -version",
        toasty::pathToUtf8(ffmpegPath)
    );

    auto probe = Subprocess(command);
    if (!probe.isRunning()) {
        log::warn("Ignoring unusable ffmpeg.exe '{}': failed to start", toasty::pathToUtf8(ffmpegPath));
        return false;
    }

    int exitCode = probe.close();
    if (exitCode != 0) {
        log::warn("Ignoring unusable ffmpeg.exe '{}': exited with code {}", toasty::pathToUtf8(ffmpegPath), exitCode);
        return false;
    }

    return true;
}

static std::filesystem::path resolveUsableFfmpegExecutable(std::filesystem::path const& configuredPath) {
    std::filesystem::path resolvedPath = resolveFfmpegExecutable(configuredPath);
    if (!isUsableFfmpegExecutable(resolvedPath)) {
        return {};
    }

    return resolvedPath;
}

static bool tryMuxAudioWithExe(
    std::filesystem::path const& ffmpegPath,
    std::filesystem::path const& audioInput,
    std::filesystem::path const& videoInput,
    std::filesystem::path const& outputPath,
    float audioOffset,
    double totalTime,
    bool fadeIn,
    bool fadeOut,
    float timeAfter,
    float audioVolume,
    std::string extraAudioArgs
) {
    if (ffmpegPath.empty()) {
        return false;
    }

    if (!pathExists(ffmpegPath) || !pathExists(audioInput) || !pathExists(videoInput)) {
        return false;
    }

    std::string fadeInString;
    if (fadeIn) {
        fadeInString = ",afade=t=in:d=2";
    }

    std::string fadeOutString;
    double fadeOutStart = totalTime - timeAfter - 3.5;
    if (fadeOut && fadeOutStart > 0.0) {
        fadeOutString = fmt::format(",afade=t=out:d=2:st={:.3f}", fadeOutStart);
    }

    if (!extraAudioArgs.empty()) {
        extraAudioArgs += " ";
    }

    std::string audioEncodeArgs = "-c:a aac";
    if (shouldUseMobileMp4Audio(outputPath)) {
        audioEncodeArgs += fmt::format(
            " -profile:a aac_low -b:a {}k -ac {} -ar {} -movflags +faststart",
            kMobileMp4AudioBitrateKbps,
            kMobileMp4AudioChannels,
            kApiMixSampleRate
        );
    }

    std::string command = fmt::format(
        "\"{}\" -y -ss {:.6f} -i \"{}\" -i \"{}\" -t {:.6f} -map 1:v -map 0:a -c:v copy {} {}-af \"adelay=0|0{}{},volume={:.2f}\" \"{}\"",
        toasty::pathToUtf8(ffmpegPath),
        audioOffset,
        toasty::pathToUtf8(audioInput),
        toasty::pathToUtf8(videoInput),
        totalTime,
        audioEncodeArgs,
        extraAudioArgs,
        fadeInString,
        fadeOutString,
        audioVolume,
        toasty::pathToUtf8(outputPath)
    );

    log::info("Executing (Audio): {}", command);
    auto proc = Subprocess(command);
    int exitCode = proc.close();
    if (exitCode != 0) {
        log::error("ffmpeg.exe audio mux failed with exit code {}", exitCode);
        return false;
    }

    return true;
}
#endif

static std::vector<float> decodeSongToRaw(std::filesystem::path const& filePath, float offsetSec, float durationSec, float volume) {
    std::vector<float> result;
    auto* system = FMODAudioEngine::sharedEngine()->m_system;
    FMOD::Sound* sound = nullptr;
    auto filePathUtf8 = toasty::pathToUtf8(filePath);

    if (system->createSound(filePathUtf8.c_str(), FMOD_CREATESAMPLE, nullptr, &sound) != FMOD_OK || !sound)
        return result;

    FMOD_SOUND_FORMAT format;
    int channels, bits;
    sound->getFormat(nullptr, &format, &channels, &bits);

    float freq;
    sound->getDefaults(&freq, nullptr);
    int sampleRate = static_cast<int>(freq);
    if (sampleRate <= 0 || channels <= 0) { sound->release(); return result; }

    unsigned int pcmSamples, totalBytes;
    sound->getLength(&pcmSamples, FMOD_TIMEUNIT_PCM);
    sound->getLength(&totalBytes, FMOD_TIMEUNIT_PCMBYTES);

    void* ptr1 = nullptr; void* ptr2 = nullptr;
    unsigned int len1 = 0, len2 = 0;
    if (sound->lock(0, totalBytes, &ptr1, &ptr2, &len1, &len2) != FMOD_OK || !ptr1) {
        sound->release();
        return result;
    }

    auto offset = static_cast<unsigned int>(std::max(0.f, offsetSec) * sampleRate);
    auto duration = static_cast<unsigned int>(durationSec * sampleRate);
    if (offset >= pcmSamples) { sound->unlock(ptr1, ptr2, len1, len2); sound->release(); return result; }
    if (offset + duration > pcmSamples) duration = pcmSamples - offset;

    size_t start = static_cast<size_t>(offset) * channels;
    size_t count = static_cast<size_t>(duration) * channels;
    result.resize(count);

    if (format == FMOD_SOUND_FORMAT_PCM16) {
        auto* s = static_cast<const int16_t*>(ptr1);
        for (size_t i = 0; i < count; i++) result[i] = s[start + i] / 32768.f;
    } else if (format == FMOD_SOUND_FORMAT_PCMFLOAT) {
        auto* s = static_cast<const float*>(ptr1);
        std::copy(s + start, s + start + count, result.begin());
    } else if (format == FMOD_SOUND_FORMAT_PCM32) {
        auto* s = static_cast<const int32_t*>(ptr1);
        for (size_t i = 0; i < count; i++) result[i] = s[start + i] / 2147483648.f;
    } else {
        result.clear();
    }

    sound->unlock(ptr1, ptr2, len1, len2);

    if (!result.empty() && channels == 1) {
        std::vector<float> stereo(result.size() * 2);
        for (size_t i = 0; i < result.size(); i++) {
            stereo[i * 2] = result[i];
            stereo[i * 2 + 1] = result[i];
        }
        result = std::move(stereo);
        channels = 2;
    }

    
    int systemRate;
    system->getSoftwareFormat(&systemRate, nullptr, nullptr);
    if (!result.empty() && systemRate > 0 && sampleRate != systemRate) {
        result = resampleInterleavedAudio(result, channels, sampleRate, systemRate);
    }

    sound->release();

    for (auto& s : result) s *= volume;

    log::info("Decoded song: {}Hz {}ch fmt={} offset={:.2f}s dur={:.2f}s -> {} samples (sysRate={})",
        sampleRate, channels, static_cast<int>(format), offsetSec, durationSec, result.size(), systemRate);

    return result;
}

static bool isPersistenceRender(ReplayEngine const* engine) {
    return engine
        && engine->ttrMode
        && engine->activeTTR
        && !engine->activeTTR->persistenceAttempts.empty();
}

static std::vector<float> decodePersistenceSongToRaw(
    TTRMacro const& macro,
    std::filesystem::path const& songFile,
    float songOffset,
    double totalDuration,
    double tickRate,
    float volume
) {
    std::vector<float> output;
    double elapsed = 0.0;

    auto appendSegment = [&](double duration) {
        if (duration <= 0.0) {
            return;
        }
        auto segment = decodeSongToRaw(songFile, std::max(songOffset, 0.0f), static_cast<float>(duration), volume);
        if (!segment.empty()) {
            output.insert(output.end(), segment.begin(), segment.end());
        }
    };

    double safeTickRate = std::max(1.0, tickRate);
    for (auto const& attempt : macro.persistenceAttempts) {
        double duration = static_cast<double>(std::max(1, attempt.deathTick)) / safeTickRate;
        appendSegment(duration);
        elapsed += duration;
    }

    appendSegment(std::max(0.0, totalDuration - elapsed));
    return output;
}

void FrameCaptureService::configure(size_t bytesPerFrame, size_t maxBufferedFrames) {
    std::lock_guard lock(m_lock);
    m_bytesPerFrame = bytesPerFrame;
    m_maxBufferedFrames = std::max<size_t>(1, maxBufferedFrames);
    m_pendingFrames.clear();
    m_pendingChanged.notify_all();
}

void FrameCaptureService::clear() {
    std::lock_guard lock(m_lock);
    m_pendingFrames.clear();
    m_pendingChanged.notify_all();
}

bool FrameCaptureService::hasPendingFrame() const {
    std::lock_guard lock(m_lock);
    return !m_pendingFrames.empty();
}

std::vector<uint8_t> FrameCaptureService::createFrameBuffer() const {
    if (m_bytesPerFrame == 0) {
        return {};
    }
    return std::vector<uint8_t>(m_bytesPerFrame, 0);
}

bool FrameCaptureService::submit(std::vector<uint8_t>&& frame) {
    std::unique_lock lock(m_lock);
    m_pendingChanged.wait(lock, [&] {
        auto* engine = ReplayEngine::get();
        return m_pendingFrames.size() < m_maxBufferedFrames
            || !engine
            || !engine->renderer.recording;
    });

    auto* engine = ReplayEngine::get();
    if (engine && !engine->renderer.recording) {
        return false;
    }

    m_pendingFrames.push_back(std::move(frame));
    lock.unlock();
    m_pendingChanged.notify_all();
    return true;
}

std::vector<uint8_t> FrameCaptureService::takeFrame() {
    std::unique_lock lock(m_lock);
    if (m_pendingFrames.empty()) {
        return {};
    }

    auto frame = std::move(m_pendingFrames.front());
    m_pendingFrames.pop_front();
    lock.unlock();
    m_pendingChanged.notify_all();
    return frame;
}

int Renderer::getCurrentFrame() const {
    auto* engine = ReplayEngine::get();
    return engine ? engine->renderTimelineTick() : 0;
}

float Renderer::getTPS() const {
    return static_cast<float>(ReplayEngine::get()->tickRate);
}

void RenderTexture::begin() {
#if !defined(GEODE_IS_IOS)
    end();

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);

    texture = new CCTexture2D();
    {
        unsigned char* data = static_cast<unsigned char*>(calloc(width * height * 3, 1));
        if (!data) return;
        texture->initWithData(data, kCCTexture2DPixelFormat_RGB888, width, height,
            CCSize(static_cast<float>(width), static_cast<float>(height)));
        free(data);
    }

    glBindTexture(GL_TEXTURE_2D, texture->getName());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGetIntegerv(GL_RENDERBUFFER_BINDING, &old_rbo);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->getName(), 0);

    glBindRenderbuffer(GL_RENDERBUFFER, old_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
#endif
}

void RenderTexture::end() {
#if !defined(GEODE_IS_IOS)
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    if (texture) {
        texture->release();
        texture = nullptr;
    }
#endif
}

void RenderTexture::capture(FrameCaptureService& frameCapture, cocos2d::CCNode* overlay) {
    CCDirector* director = CCDirector::sharedDirector();
    PlayLayer* pl = PlayLayer::get();
    if (!pl || !fbo) return;

#if !defined(GEODE_IS_IOS)
    auto frame = frameCapture.createFrameBuffer();
    if (frame.empty()) return;

    glViewport(0, 0, width, height);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    pl->visit();

    if (overlay) {
        overlay->visit();
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, frame.data());
    if (!frameCapture.submit(std::move(frame))) {
        log::warn("Render frame capture aborted before frame submission completed");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
    director->setViewport();
#endif
}

void Renderer::captureFrame() {
    if (!recording) {
        return;
    }
    renderTex.capture(frameCapture, watermarkLabel);
}

static cocos2d::CCSize computeRenderResolution(unsigned w, unsigned h) {
    return CCSize(320.f * (static_cast<float>(w) / static_cast<float>(h)), 320.f);
}

void Renderer::changeRes(bool og) {
    if (!og && (width == 0 || height == 0)) return changeRes(true);

    cocos2d::CCEGLView* view = cocos2d::CCEGLView::get();

    if (og) {
        CCDirector::sharedDirector()->m_obWinSizeInPoints = ogRes;
        view->setDesignResolutionSize(ogRes.width, ogRes.height, ResolutionPolicy::kResolutionExactFit);
        view->m_fScaleX = ogScaleX;
        view->m_fScaleY = ogScaleY;
    } else {
        cocos2d::CCSize res = computeRenderResolution(width, height);
        float scaleX = static_cast<float>(width) / res.width;
        float scaleY = static_cast<float>(height) / res.height;
        CCDirector::sharedDirector()->m_obWinSizeInPoints = res;
        view->setDesignResolutionSize(res.width, res.height, ResolutionPolicy::kResolutionExactFit);
        view->m_fScaleX = scaleX;
        view->m_fScaleY = scaleY;
    }
}

bool Renderer::shouldUseAPI() {
#ifdef GEODE_IS_WINDOWS
    bool foundApi = Loader::get()->isModLoaded("eclipse.ffmpeg-api");
    return foundApi;
#else
    return true;
#endif
}

bool Renderer::resolveEncoder() {
    bool foundApi = Loader::get()->isModLoaded("eclipse.ffmpeg-api");
    if (foundApi) return true;
#ifdef GEODE_IS_WINDOWS
    std::filesystem::path exePath = Mod::get()->getSettingValue<std::filesystem::path>("ffmpeg_path");
    std::filesystem::path resolved = resolveUsableFfmpegExecutable(exePath);
    if (!resolved.empty()) {
        ffmpegPath = resolved;
        return true;
    }
#endif
    return false;
}

bool Renderer::toggle() {
    auto* engine = ReplayEngine::get();

    if (engine->renderer.recording) {
        engine->renderer.stop(engine->renderer.getCurrentFrame());
        return true;
    }

    engine->renderer.usingApi = shouldUseAPI();

    if (!engine->renderer.resolveEncoder()) {
#ifdef GEODE_IS_WINDOWS
        auto popupTitle = trString("Error");
        auto popupMessage = trString("Couldn't find <cl>FFmpeg</c>. Pick the ffmpeg.exe in this mod's settings or install the FFmpeg API mod.\nOpen the download page?");
        auto cancelText = trString("Cancel");
        auto yesText = trString("Yes");
        geode::createQuickPopup(
            popupTitle.c_str(),
            popupMessage.c_str(),
            cancelText.c_str(), yesText.c_str(),
            [](auto, bool btn2) {
                if (btn2) {
                    auto infoTitle = trString("Info");
                    auto infoMessage = trString("Extract the archive, then point the setting at <cl>ffmpeg.exe</c> inside its 'bin' subfolder.");
                    auto okText = trString("Ok");
                    FLAlertLayer::create(infoTitle.c_str(), infoMessage.c_str(), okText.c_str())->show();
                    utils::web::openLinkInBrowser("https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z");
                }
            }
        );
#else
        auto errorTitle = trString("Error");
        auto errorMessage = trString("Rendering needs the <cl>FFmpeg API</c> mod. Install it to continue.");
        auto okText = trString("Ok");
        FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
#endif
        return false;
    }

    if (!PlayLayer::get()) {
        auto warningTitle = trString("Warning");
        auto warningMessage = trString("<cl>Enter a level</c> before starting a render.");
        auto okText = trString("Ok");
        FLAlertLayer::create(warningTitle.c_str(), warningMessage.c_str(), okText.c_str())->show();
        return false;
    }

    std::filesystem::path renderPath = Mod::get()->getSettingValue<std::filesystem::path>("render_folder");
    if (renderPath.empty() || pathContainsMarker(renderPath, "{gd_dir}")) {
        renderPath = dirs::getGameDir() / "renders";
    }
    if (pathExists(renderPath)) {
        engine->renderer.start();
    } else {
        if (utils::file::createDirectoryAll(renderPath).isOk())
            engine->renderer.start();
        else {
            auto errorTitle = trString("Error");
            auto errorMessage = trString("Failed to prepare the output directory.");
            auto okText = trString("Ok");
            FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
            return false;
        }
    }

    return true;
}

void Renderer::start() {
    PlayLayer* pl = PlayLayer::get();
    if (!pl) return;

    auto* engine = ReplayEngine::get();
    engine->clearFrameStepState();
    Mod* mod = Mod::get();
    fmod = FMODAudioEngine::sharedEngine();

    fps = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_fps", 60));
    codec = mod->getSavedValue<std::string>("render_codec", "");
    if (codec.empty()) codec = "libx264";
    bitrate = mod->getSavedValue<std::string>("render_bitrate", "40") + "M";
    extraArgs = loadSavedValueWithFallback<std::string>(mod, "render_args", "-pix_fmt yuv420p", {"render_extra_args"});
    videoArgs = mod->getSavedValue<std::string>("render_video_args", "colorspace=all=bt709:iall=bt470bg:fast=1");
    extraAudioArgs = mod->getSavedValue<std::string>("render_audio_args", "");
    sfxVolume = static_cast<float>(mod->getSavedValue<double>("render_sfx_volume", 1.0));
    musicVolume = static_cast<float>(mod->getSavedValue<double>("render_music_volume", 1.0));
    log::info("Render audio: musicVolume={:.3f}, clickVolume={:.3f}, includeAudio={}, includeClicks={}",
        musicVolume, sfxVolume,
        loadSavedValueWithFallback<bool>(mod, "render_include_audio", true, {"render_record_audio", "render_capture_audio"}),
        mod->getSavedValue<bool>("render_include_clicks", false));
    stopAfter = static_cast<float>(
        geode::utils::numFromString<float>(
            loadSavedValueWithFallback<std::string>(mod, "render_seconds_after", "3", {"render_after_seconds"})
        ).unwrapOr(3.f));
    audioMode = AUDIO_OFF;

    std::string extension = loadSavedValueWithFallback<std::string>(mod, "render_file_extension", ".mp4", {"render_extension"});

    if (loadSavedValueWithFallback<bool>(mod, "render_include_audio", true, {"render_record_audio", "render_capture_audio"})) {
        audioMode = AUDIO_SONG;
    }

    includeClickSounds = mod->getSavedValue<bool>("render_include_clicks", false);

    width = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_width", 1920));
    height = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_height", 1080));

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::string customName = mod->getSavedValue<std::string>("render_name", "");
    std::string filename;
    if (!customName.empty()) {
        filename = fmt::format("{}_{}{}",
            customName, std::to_string(timestamp), extension);
    } else {
        filename = fmt::format("render_{}_{}x{}_{}{}",
            std::string_view(pl->m_level->m_levelName), width, height, std::to_string(timestamp), extension);
    }
    std::filesystem::path renderFolder = Mod::get()->getSettingValue<std::filesystem::path>("render_folder");
    if (renderFolder.empty() || pathContainsMarker(renderFolder, "{gd_dir}")) {
        renderFolder = dirs::getGameDir() / "renders";
    }
    path = renderFolder / filename;

    if (width % 2 != 0) width++;
    if (height % 2 != 0) height++;

    renderTex.width = width;
    renderTex.height = height;
    ogRes = cocos2d::CCEGLView::get()->getDesignResolutionSize();
    ogScaleX = cocos2d::CCEGLView::get()->m_fScaleX;
    ogScaleY = cocos2d::CCEGLView::get()->m_fScaleY;

    dontRender = true;
    recording = true;
    engine->syncFrameStepAudio(fmod);
    levelFinished = false;
    timeAfter = 0.f;
    finishFrame = 0;
    levelStartFrame = 0;
    lastFrame_t = extra_t = 0;
    s_renderClock.reset();

    encodeSession.reset();
    audioCapture.reset();
    frameCapture.configure(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 8);
    m_capturedFrameSet.clear();
    renderTex.begin();
    changeRes(false);
    resetSimulationTimingState();

    if (watermarkLabel) {
        watermarkLabel->release();
        watermarkLabel = nullptr;
    }
    if (mod->getSavedValue<bool>("render_watermark_enabled", false)) {
        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        int watermarkFont = sanitizeRenderWatermarkFont(
            mod->getSavedValue<int>("render_watermark_font", RENDER_WATERMARK_FONT_NORMAL_PUSAB)
        );
        int watermarkCorner = sanitizeRenderWatermarkCorner(
            mod->getSavedValue<int>("render_watermark_corner", RENDER_WATERMARK_CORNER_BOTTOM_RIGHT)
        );
        float watermarkScale = sanitizeRenderWatermarkScale(
            mod->getSavedValue<float>("render_watermark_scale", 1.0f)
        );
        std::string wmText = toasty::branding::fullBrand();
        const char* fontFile = watermarkFont == RENDER_WATERMARK_FONT_GOLD_PUSAB
            ? "goldFont.fnt"
            : "bigFont.fnt";
        float baseScale = watermarkFont == RENDER_WATERMARK_FONT_GOLD_PUSAB ? 0.36f : 0.30f;
        cocos2d::CCPoint anchor = ccp(1.0f, 0.0f);
        cocos2d::CCPoint position = ccp(winSize.width - 12.0f, 12.0f);

        switch (watermarkCorner) {
            case RENDER_WATERMARK_CORNER_TOP_LEFT:
                anchor = ccp(0.0f, 1.0f);
                position = ccp(12.0f, winSize.height - 12.0f);
                break;
            case RENDER_WATERMARK_CORNER_TOP_RIGHT:
                anchor = ccp(1.0f, 1.0f);
                position = ccp(winSize.width - 12.0f, winSize.height - 12.0f);
                break;
            case RENDER_WATERMARK_CORNER_BOTTOM_LEFT:
                anchor = ccp(0.0f, 0.0f);
                position = ccp(12.0f, 12.0f);
                break;
            default:
                break;
        }

        watermarkLabel = cocos2d::CCLabelBMFont::create(wmText.c_str(), fontFile);
        if (watermarkLabel) {
            watermarkLabel->setAnchorPoint(anchor);
            watermarkLabel->setPosition(position);
            watermarkLabel->setScale(baseScale * watermarkScale);
            watermarkLabel->setOpacity(150);
            watermarkLabel->retain();
        }
    }

    if (progressLabel) {
        progressLabel->removeFromParentAndCleanup(true);
        progressLabel = nullptr;
    }
    {
        auto progressText = trFormat("Rendering... {percent}%", fmt::arg("percent", 0));
        progressLabel = cocos2d::CCLabelBMFont::create(progressText.c_str(), "goldFont.fnt");
        progressLabel->setPosition(ccp(ogRes.width / 2.0f, ogRes.height - 20.0f));
        progressLabel->setScale(0.55f);
        progressLabel->setOpacity(200);
        progressLabel->setZOrder(9999);
        auto* scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
        if (scene) scene->addChild(progressLabel);
    }

    if (!pl->m_levelEndAnimationStarted && pl->m_isPaused) {
        if (engine->engineMode == MODE_EXECUTE && engine->hasMacro()) {
        } else if (engine->engineMode == MODE_CAPTURE) {
        }
    }

    if (engine->engineMode == MODE_EXECUTE && engine->hasMacro()) {
        engine->beginExecutionImmediate();
    }

    if (audioMode != AUDIO_OFF) {
        fmod->m_globalChannel->getVolume(&ogSFXVol);
        fmod->m_backgroundMusicChannel->getVolume(&ogMusicVol);
    }

    std::filesystem::path songFile = toasty::stringToPath(pl->m_level->getAudioFileName());
    if (pl->m_level->m_songID == 0) {
        auto resolved = cocos2d::CCFileUtils::sharedFileUtils()->fullPathForFilename(
            toasty::pathToUtf8(songFile).c_str(),
            false
        );
        songFile = toasty::stringToPath(resolved);
    } else {
        auto writablePath = toasty::stringToPath(cocos2d::CCFileUtils::sharedFileUtils()->getWritablePath());
        auto fullPath = writablePath / songFile;
        if (pathExists(fullPath)) {
            songFile = fullPath;
        } else {
            auto resolved = cocos2d::CCFileUtils::sharedFileUtils()->fullPathForFilename(
                toasty::pathToUtf8(songFile).c_str(),
                false
            );
            auto resolvedPath = toasty::stringToPath(resolved);
            if (pathExists(resolvedPath)) {
                songFile = resolvedPath;
            }
        }
    }

    log::info("Song file: {} (exists: {})", toasty::pathToUtf8(songFile), pathExists(songFile));

    float songOffset = pl->m_levelSettings->m_songOffset +
        (static_cast<float>(levelStartFrame) / getTPS());
    bool fadeIn = pl->m_levelSettings->m_fadeIn;
    bool fadeOut = pl->m_levelSettings->m_fadeOut;
    int64_t bitrateApi = geode::utils::numFromString<int64_t>(
        mod->getSavedValue<std::string>("render_bitrate", "40")).unwrapOr(40) * 1000000;

    if (includeClickSounds) {
        int systemRate = 44100;
        fmod->m_system->getSoftwareFormat(&systemRate, nullptr, nullptr);
        ClickSoundManager::get()->preDecodeForRender(systemRate);
    }

    m_encodeThread = std::thread(&Renderer::runEncodeLoop, this, songFile, songOffset, fadeIn, fadeOut, extension, bitrateApi);
    m_encodeThread.detach();
}

void Renderer::runEncodeLoop(std::filesystem::path songFile, float songOffset, bool fadeIn, bool fadeOut, std::string extension, int64_t bitrateApi) {
        ffmpeg::RenderSettings settings;
        settings.m_pixelFormat = ffmpeg::PixelFormat::RGB24;
        settings.m_codec = codec;
        settings.m_bitrate = bitrateApi;
        settings.m_width = width;
        settings.m_height = height;
        settings.m_fps = fps;
        settings.m_outputFile = path;
        encodeSession.outputFile = path;
        settings.m_colorspaceFilters = "";

        log::info("Render settings: codec={}, bitrate={}, {}x{} @{}fps, output={}",
            settings.m_codec, settings.m_bitrate, settings.m_width, settings.m_height, settings.m_fps,
            toasty::pathToUtf8(settings.m_outputFile));

        auto availableCodecs = ffmpeg::events::Recorder::getAvailableCodecs();
        std::string codecList;
        for (auto& c : availableCodecs) codecList += c + ", ";
        log::info("Available codecs ({}): {}", availableCodecs.size(), codecList);

        if (!availableCodecs.empty() && std::find(availableCodecs.begin(), availableCodecs.end(), settings.m_codec) == availableCodecs.end()) {
            log::warn("Codec '{}' not found in available codecs, trying fallbacks...", settings.m_codec);
            std::vector<std::string> fallbacks = {"libx264", "h264", "libx264rgb", "mpeg4", "libvpx-vp9"};
            bool found = false;
            for (auto& fb : fallbacks) {
                if (std::find(availableCodecs.begin(), availableCodecs.end(), fb) != availableCodecs.end()) {
                    log::info("Using fallback codec: {}", fb);
                    settings.m_codec = fb;
                    found = true;
                    break;
                }
            }
            if (!found && !availableCodecs.empty()) {
                settings.m_codec = availableCodecs[0];
                log::info("Using first available codec: {}", settings.m_codec);
            }
        }

        std::string localCodec = codec;
        std::string localBitrate = bitrate;
        std::string localExtraArgs = extraArgs;
        std::string localVideoArgs = videoArgs;

        bool useApiForEncoding = usingApi;

        {
#ifdef GEODE_IS_WINDOWS
        Subprocess process;
#endif
        ffmpeg::events::Recorder recorder;

        if (useApiForEncoding) {
            auto res = recorder.init(settings);
            if (res.isErr()) {
                log::error("FFmpeg init error: {}", res.unwrapErr());
#ifdef GEODE_IS_WINDOWS
                if (pathExists(ffmpegPath)) {
                    useApiForEncoding = false;
                    Loader::get()->queueInMainThread([] {
                        Notification::create(trString("FFmpeg API not loaded — falling back to ffmpeg.exe"), NotificationIcon::Warning)->show();
                    });
                } else {
#endif
                    Loader::get()->queueInMainThread([this] {
                        auto errorTitle = trString("Error");
                        auto errorMessage = trString("The FFmpeg API encoder didn't initialize.");
                        auto okText = trString("Ok");
                        FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                        stop();
                    });
                    audioMode = AUDIO_OFF;
                    recording = false;
                    return;
#ifdef GEODE_IS_WINDOWS
                }
#endif
            }
        }
        
        if (!useApiForEncoding) {
#ifdef GEODE_IS_WINDOWS
            if (!localCodec.empty()) localCodec = "-c:v " + localCodec + " ";
            if (!localBitrate.empty()) localBitrate = "-b:v " + localBitrate + " ";
            if (localExtraArgs.empty()) localExtraArgs = "-pix_fmt yuv420p";
            if (localVideoArgs.empty()) localVideoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";

            std::string command = fmt::format(
                "\"{}\" -y -f rawvideo -pix_fmt rgb24 -s {}x{} -r {} -i - {}{}{} -vf \"vflip,{}\" -an \"{}\"",
                toasty::pathToUtf8(ffmpegPath),
                std::to_string(width),
                std::to_string(height),
                std::to_string(fps),
                localCodec,
                localBitrate,
                localExtraArgs,
                localVideoArgs,
                toasty::pathToUtf8(path)
            );

            log::info("Executing: {}", command);
            process = Subprocess(command);
#endif
        }

        while (recording || pause || frameCapture.hasPendingFrame()) {
            auto frame = frameCapture.takeFrame();
            if (!frame.empty()) {
                auto writeFrame = [&](std::vector<uint8_t> const& data) {
                    if (useApiForEncoding) {
                        auto res = recorder.writeFrame(data);
                        if (res.isErr()) {
                            Loader::get()->queueInMainThread([this] {
                                auto errorTitle = trString("Error");
                                auto errorMessage = trString("Frame submission to the FFmpeg API failed.");
                                auto okText = trString("Ok");
                                FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                                stop();
                            });
                            audioMode = AUDIO_OFF;
                            recording = false;
                            return false;
                        }
                    }
#ifdef GEODE_IS_WINDOWS
                    else {
                        process.writeStdin(data.data(), data.size());
                    }
#endif
                    return true;
                };
                if (!writeFrame(frame)) break;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        auto finishEncoder = [&]() -> bool {
            if (useApiForEncoding) {
                recorder.stop();
                return true;
            }
#ifdef GEODE_IS_WINDOWS
            if (process.close()) {
                Loader::get()->queueInMainThread([] {
                    auto errorTitle = trString("Error");
                    auto errorMessage = trString("Saving the rendered video failed.");
                    auto okText = trString("Ok");
                    FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                });
                return false;
            }
#endif
            return true;
        };
        if (!finishEncoder()) return;
        } 

        bool preferExeAudioMux = false;
#ifdef GEODE_IS_WINDOWS
        preferExeAudioMux = pathExists(ffmpegPath);
#endif

        auto* engine = ReplayEngine::get();
        bool persistenceRender = isPersistenceRender(engine);
        float nonNegativeSongOffset = std::max(songOffset, 0.0f);
        bool needsRawSongAudio = !preferExeAudioMux;
        if (!needsRawSongAudio) {
            needsRawSongAudio = includeClickSounds
                || musicVolume != 1.0f
                || nonNegativeSongOffset > 0.0f
                || fadeIn
                || persistenceRender
                || fadeOut;
        }

        audioCapture.rawMixBuffer.clear();
        auto& rawAudio = audioCapture.rawMixBuffer;
        if (needsRawSongAudio && audioMode == AUDIO_SONG && pathExists(songFile)) {
            if (persistenceRender && engine && engine->activeTTR) {
                rawAudio = decodePersistenceSongToRaw(
                    *engine->activeTTR,
                    songFile,
                    nonNegativeSongOffset,
                    lastFrame_t,
                    engine->runtimeTickRate(),
                    musicVolume
                );
            } else {
                rawAudio = decodeSongToRaw(songFile, nonNegativeSongOffset, lastFrame_t, musicVolume);
            }
        }

        audioCapture.sampleRate = 44100;
        int& rawAudioSampleRate = audioCapture.sampleRate;
        if (FMODAudioEngine::sharedEngine() && FMODAudioEngine::sharedEngine()->m_system) {
            FMODAudioEngine::sharedEngine()->m_system->getSoftwareFormat(&rawAudioSampleRate, nullptr, nullptr);
        }

        if (includeClickSounds) {
            auto* csm = ClickSoundManager::get();

            std::vector<MacroAction> ttrConvertedActions;
            bool hasInputs = false;
            std::vector<MacroAction>* inputsPtr = nullptr;
            double macroFramerate = 240.0;

            if (engine->ttrMode && engine->activeTTR &&
                (!engine->activeTTR->inputs.empty() || !engine->activeTTR->persistenceAttempts.empty())) {
                ttrConvertedActions = persistenceRender
                    ? engine->activeTTR->toPersistenceMacroActions()
                    : engine->activeTTR->toMacroActions();
                inputsPtr = &ttrConvertedActions;
                macroFramerate = engine->activeTTR->framerate;
                hasInputs = !ttrConvertedActions.empty();
            } else if (engine->activeMacro && !engine->activeMacro->inputs.empty()) {
                inputsPtr = &engine->activeMacro->inputs;
                macroFramerate = engine->activeMacro->framerate;
                hasInputs = true;
            }

            if (hasInputs && inputsPtr && (!csm->p1Pack.empty() || (csm->separateP2Clicks && !csm->p2Pack.empty()))) {
                float macroTickRate = static_cast<float>(engine->tickRate);
                bool trueTwoPlayerMode = false;
                if (engine->ttrMode && engine->activeTTR) {
                    trueTwoPlayerMode = engine->activeTTR->twoPlayerMode;
                } else if (auto* playLayer = PlayLayer::get()) {
                    trueTwoPlayerMode = playLayer->m_levelSettings && playLayer->m_levelSettings->m_twoPlayerMode;
                }

                auto& inputs = *inputsPtr;
                log::info("Click render: engineTPS={:.1f}, macroFramerate={:.1f}, lastFrame_t={:.3f}, sampleRate={}, "
                    "levelStartFrame={}, inputCount={}, firstFrame={}, lastFrame={}",
                    engine->tickRate, macroFramerate, lastFrame_t, rawAudioSampleRate,
                    levelStartFrame, inputs.size(), inputs.front().frame, inputs.back().frame);
                auto clickAudio = csm->generateClickAudio(
                    inputs,
                    macroTickRate,
                    static_cast<float>(lastFrame_t),
                    rawAudioSampleRate,
                    levelStartFrame,
                    false,
                    trueTwoPlayerMode);

                if (!clickAudio.empty()) {
                    if (sfxVolume != 1.0f) {
                        for (auto& sample : clickAudio) {
                            sample *= sfxVolume;
                        }
                    }

                    if (rawAudio.empty()) {
                        rawAudio = std::move(clickAudio);
                    } else {
                        size_t mixLen = std::min(rawAudio.size(), clickAudio.size());
                        for (size_t i = 0; i < mixLen; i++) {
                            rawAudio[i] = std::clamp(rawAudio[i] + clickAudio[i], -1.0f, 1.0f);
                        }
                        if (clickAudio.size() > rawAudio.size()) {
                            rawAudio.insert(rawAudio.end(),
                                clickAudio.begin() + rawAudio.size(), clickAudio.end());
                        }
                    }
                }
            }
        }

        if (!rawAudio.empty()) {
            fitAudioToDuration(rawAudio, lastFrame_t, rawAudioSampleRate);
            applyRenderAudioFades(rawAudio, 2, rawAudioSampleRate, fadeIn, fadeOut, lastFrame_t, timeAfter);
        }

        Loader::get()->queueInMainThread([] {
            Notification::create(trString("Finalizing render..."), NotificationIcon::Loading)->show();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool hasRawAudio = !rawAudio.empty();
        bool hasAudio = hasRawAudio || (!persistenceRender && audioMode == AUDIO_SONG && (musicVolume > 0.f && pathExists(songFile)));
        log::info("Audio mux: hasRawAudio={}, rawAudio.size={}, hasAudio={}, musicVolume={:.3f}, useApi={}",
            hasRawAudio, rawAudio.size(), hasAudio, musicVolume, useApiForEncoding);
        if (audioMode == AUDIO_SONG && !hasRawAudio && !pathExists(songFile) && !includeClickSounds) {
            log::error("Song file not found and no raw audio captured: {}", toasty::pathToUtf8(songFile));
        }

        if (!hasAudio) {
            Loader::get()->queueInMainThread([this] {
                Notification::create(trString("Render done without audio"), NotificationIcon::Success)->show();
                if (Mod::get()->getSavedValue<bool>("render_hide_endscreen", false)) {
                    if (PlayLayer* pl = PlayLayer::get())
                        if (EndLevelLayer* layer = pl->getChildByType<EndLevelLayer>(0))
                            layer->setVisible(true);
                }
            });
            return;
        }

        std::filesystem::path tempPath = path.parent_path() /
            ("temp_" + toasty::pathToUtf8(path.filename()));
        std::filesystem::path rawAudioPath;
        auto ensureRawAudioFile = [&]() -> bool {
            if (rawAudioPath.empty()) {
                rawAudioPath = path.parent_path() /
                    fmt::format("temp_audio_{}.wav", std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());

                if (!writeRawAudioWav(rawAudioPath, rawAudio, static_cast<uint32_t>(rawAudioSampleRate))) {
                    log::error("Failed to write temporary render audio '{}'", toasty::pathToUtf8(rawAudioPath));
                    rawAudioPath.clear();
                    return false;
                }
            }
            return true;
        };

        double totalTime = lastFrame_t;
        bool audioMuxed = false;

        if (!preferExeAudioMux && useApiForEncoding) {
            if (hasRawAudio) {
                if (rawAudioSampleRate != kApiMixSampleRate) {
                    log::info(
                        "Resampling raw render audio for API mux: {}Hz -> {}Hz",
                        rawAudioSampleRate,
                        kApiMixSampleRate
                    );
                    rawAudio = resampleInterleavedAudio(rawAudio, 2, rawAudioSampleRate, kApiMixSampleRate);
                    rawAudioSampleRate = kApiMixSampleRate;
                    fitAudioToDuration(rawAudio, totalTime, rawAudioSampleRate);
                }

                auto rawRes = ffmpeg::events::AudioMixer::mixVideoRaw(path, rawAudio, tempPath);
                if (rawRes.isOk()) {
                    audioMuxed = true;
                    log::info("Audio mux succeeded via API raw audio");
                } else {
                    log::error("Audio mux via API raw audio failed: {}", rawRes.unwrapErr());

                    if (ensureRawAudioFile()) {
                        auto fileRes = ffmpeg::events::AudioMixer::mixVideoAudio(path, rawAudioPath, tempPath);
                        if (fileRes.isOk()) {
                            audioMuxed = true;
                            log::info("Audio mux succeeded via API WAV file fallback");
                        } else {
                            log::error("Audio mux via API WAV fallback failed: {}", fileRes.unwrapErr());
                        }
                    }
                }
            } else {
                auto fileRes = ffmpeg::events::AudioMixer::mixVideoAudio(path, songFile, tempPath);
                if (fileRes.isOk()) {
                    audioMuxed = true;
                    log::info("Audio mux succeeded via API song file");
                } else {
                    log::error("Audio mux via API song file failed: {}", fileRes.unwrapErr());
                }
            }
        }

#ifdef GEODE_IS_WINDOWS
        if (!audioMuxed && pathExists(ffmpegPath)) {
            std::filesystem::path audioInput;
            float audioOffset = 0.0f;
            float audioVolume = 1.0f;

            if (hasRawAudio) {
                if (!ensureRawAudioFile()) {
                    Loader::get()->queueInMainThread([] {
                        auto errorTitle = trString("Error");
                        auto errorMessage = trString("Couldn't write the temporary audio file.");
                        auto okText = trString("Ok");
                        FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                    });
                    return;
                }
                audioInput = rawAudioPath;
            } else {
                audioInput = songFile;
                audioOffset = nonNegativeSongOffset;
                audioVolume = musicVolume;
            }

            audioMuxed = tryMuxAudioWithExe(
                ffmpegPath,
                audioInput,
                path,
                tempPath,
                audioOffset,
                totalTime,
                fadeIn,
                fadeOut,
                timeAfter,
                audioVolume,
                extraAudioArgs
            );
        }
#endif

        if (!audioMuxed) {
            Loader::get()->queueInMainThread([] {
                auto errorTitle = trString("Error");
                auto errorMessage = trString("Audio mux step failed.");
                auto okText = trString("Ok");
                FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
            });
            if (!rawAudioPath.empty()) cleanupTempFile(rawAudioPath, "temporary render audio");
            return;
        }

        if (!rawAudioPath.empty()) cleanupTempFile(rawAudioPath, "temporary render audio");

        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec) log::warn("Failed to remove old render file: {}", ec.message());
        else {
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) log::warn("Failed to rename temp render file: {}", ec.message());
            else log::info("Render with audio saved to: {}", toasty::pathToUtf8(path));
        }

        if (!ec) {
            Loader::get()->queueInMainThread([] {
                Notification::create(trString("Render complete with audio"), NotificationIcon::Success)->show();
            });
        } else {
            Loader::get()->queueInMainThread([] {
                Notification::create(trString("Couldn't save render with audio"), NotificationIcon::Error)->show();
            });
        }

}

void Renderer::restoreAudioVolumes() {
    if (audioMode != AUDIO_OFF) {
        fmod->m_globalChannel->setVolume(ogSFXVol);
        fmod->m_backgroundMusicChannel->setVolume(ogMusicVol);
    }
}

bool Renderer::isLevelComplete() {
    if (PlayLayer* pl = PlayLayer::get()) {
        return pl->m_levelEndAnimationStarted;
    }
    return false;
}

void Renderer::stop(int frame) {
    m_capturedFrameSet.clear();
    finishFrame = getCurrentFrame();
    recording = false;
    timeAfter = 0.f;

    restoreAudioVolumes();
    ClickSoundManager::get()->decodedClickCache.clear();

    if (isLevelComplete()) {
        finishFrame = 0;
        levelFinished = true;
    } else if (!PlayLayer::get()) {
        audioMode = AUDIO_OFF;
    }

    pause = false;
    encodeSession.reset();
    changeRes(true);
    renderTex.end();
    resetSimulationTimingState();

    if (watermarkLabel) {
        watermarkLabel->release();
        watermarkLabel = nullptr;
    }
    if (progressLabel) {
        progressLabel->removeFromParentAndCleanup(true);
        progressLabel = nullptr;
    }
}

void Renderer::handleRecording(PlayLayer* pl, int frame) {
    if (!pl) return stop(frame);
    isPlatformer = pl->m_levelSettings ? pl->m_levelSettings->m_platformerMode : false;
    if (dontRender) return;
    auto* engine = ReplayEngine::get();

    if (progressLabel) {
        int pct = getRenderProgressPercent(*this, pl);
        auto progressText = trFormat("Rendering... {percent}%", fmt::arg("percent", pct));
        progressLabel->setString(progressText.c_str());
    }

    bool expectedPersistenceDeath = engine
        && engine->isPersistencePlaybackFailureAttempt()
        && engine->persistencePlaybackDeathPending;
    if (!pl->m_player1 || (pl->m_player1->m_isDead && !expectedPersistenceDeath)) {
        log::info("Stopping render because the player died at frame {}", frame);
        stop(frame);
        return;
    }

    if (frame < static_cast<int>(m_capturedFrameSet.size()) && m_capturedFrameSet[frame] && frame > 10)
        return;

    if (frame >= static_cast<int>(m_capturedFrameSet.size())) m_capturedFrameSet.resize(frame + 64);
    m_capturedFrameSet[frame] = true;

    if (!pl->m_hasCompletedLevel || timeAfter < stopAfter) {
        float tickDt = 1.f / static_cast<float>(getTPS());

        if (pl->m_hasCompletedLevel) {
            timeAfter += tickDt;
            levelFinished = true;
        }

        double renderTime = static_cast<double>(std::max(0, frame)) / static_cast<double>(getTPS());
        s_renderClock.frameDelta = 1.0 / static_cast<double>(fps);
        double time = renderTime + extra_t - lastFrame_t;
        if (time >= s_renderClock.frameDelta) {
            int localFrame = engine ? engine->renderLocalTick() : frame;
            int correctMusicTime = static_cast<int>((localFrame / getTPS()
                + pl->m_levelSettings->m_songOffset) * 1000);
            correctMusicTime += fmod->m_musicOffset;

            int64_t syncThreshold = std::max<int64_t>(50, 1500 / static_cast<int64_t>(fps));
            int64_t musicTimeDelta = static_cast<int64_t>(fmod->getMusicTimeMS(0)) - static_cast<int64_t>(correctMusicTime);
            if (std::abs(musicTimeDelta) >= syncThreshold)
                fmod->setMusicTimeMS(correctMusicTime, true, 0);

            while (time >= s_renderClock.frameDelta) {
                captureFrame();
                time -= s_renderClock.frameDelta;
            }
            extra_t = time;
            lastFrame_t = renderTime;
        }
    } else {
        stop(frame);
    }
}

static void hideLevelCompleteText(CCNode* parent, bool forceHide) {
    for (CCNode* node : CCArrayExt<CCNode*>(parent->getChildren())) {
        CCSprite* spr = typeinfo_cast<CCSprite*>(node);
        if (!spr) continue;
        if (!isSpriteFrameName(spr, "GJ_levelComplete_001.png")) continue;
        spr->setScale(spr->getScale() * 0.999f);
        if (forceHide) {
            spr->setVisible(false);
        }
    }
}

class $modify(RenderPlayLayer, PlayLayer) {
    void resetLevel() {
        resetSimulationTimingState();
        PlayLayer::resetLevel();
        auto* engine = ReplayEngine::get();
        if (engine->renderer.recording)
            engine->renderer.dontRender = false;
        resetSimulationTimingState();
    }

    void showCompleteText() {
        PlayLayer::showCompleteText();
        auto* engine = ReplayEngine::get();
        bool hideLevelComplete = engine->renderer.recording &&
            m_levelEndAnimationStarted &&
            Mod::get()->getSavedValue<bool>("render_hide_levelcomplete", false);

        hideLevelCompleteText(this, hideLevelComplete);
    }
};

class $modify(RenderEndLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!PlayLayer::get()) return;
        auto* engine = ReplayEngine::get();
        if (engine->renderer.recording && PlayLayer::get()->m_levelEndAnimationStarted &&
            Mod::get()->getSavedValue<bool>("render_hide_endscreen", false)) {
            this->runAction(CCSequence::createWithTwoActions(
                CCDelayTime::create(0.f),
                CCCallFunc::create(this, callfunc_selector(RenderEndLayer::hideForRender))
            ));
        }
    }

    void hideForRender() {
        setVisible(false);
    }
};

class $modify(RenderFMOD, FMODAudioEngine) {
    int playEffect(gd::string path, float speed, float p2, float volume) {
        auto* engine = ReplayEngine::get();
        if (std::string_view(path) == "explode_11.ogg" && engine->renderer.recording) return 0;

        return FMODAudioEngine::playEffect(path, speed, p2, volume);
    }
};

class $modify(RenderAudioEffects, AudioEffectsLayer) {
    void audioStep(float dt) {
        if (ReplayEngine::get()->renderer.recording) {
            m_audioScale = 0.0f;
            m_audioPulseMod = 0.0f;
            return;
        }
        AudioEffectsLayer::audioStep(dt);
    }
};

class $modify(RenderBaseLayer, GJBaseGameLayer) {
    void updateAudioVisualizer() {
        if (ReplayEngine::get()->renderer.recording) return;
        GJBaseGameLayer::updateAudioVisualizer();
    }
};

class $modify(RenderParticle, CCParticleSystemQuad) {
    static CCParticleSystemQuad* create(const char* v1, bool v2) {
        CCParticleSystemQuad* ret = CCParticleSystemQuad::create(v1, v2);
        auto* engine = ReplayEngine::get();
        if (!engine->renderer.recording) return ret;

        if (std::string_view(v1) == "levelComplete01.plist" &&
            Mod::get()->getSavedValue<bool>("render_hide_levelcomplete", false))
            ret->setVisible(false);

        return ret;
    }
};
