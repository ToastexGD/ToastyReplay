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
#include <future>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <cmath>
#include <string_view>
#include <unordered_map>
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
    auto extension = toasty::pathToUtf8(outputPath.extension());
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
    std::string extraAudioArgs,
    std::string audioCodec
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

    bool isAac = audioCodec.empty() || audioCodec == "aac";
    std::string audioEncodeArgs = "-c:a " + (audioCodec.empty() ? std::string("aac") : audioCodec)
        + " -strict experimental";
    if (isAac && shouldUseMobileMp4Audio(outputPath)) {
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

    log::debug("Starting audio mux command: {}", command);
    auto stderrLogPath = (Mod::get()->getSaveDir() / "last_audiomux_ffmpeg.log").wstring();
    auto proc = Subprocess(command, stderrLogPath);
    int exitCode = proc.close();
    if (exitCode != 0) {
        log::error("ffmpeg.exe audio mux failed with exit code {} (see last_audiomux_ffmpeg.log)", exitCode);
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

    return result;
}

static std::vector<float> decodeEffectToRaw(const std::string& resolvedPath) {
    auto p = toasty::stringToPath(resolvedPath);
    if (!pathExists(p)) return {};
    return decodeSongToRaw(p, 0.0f, 9999.f, 1.0f);
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
    m_freeList.clear();

    if (bytesPerFrame > 0) {
        size_t poolSize = m_maxBufferedFrames + 2;
        m_freeList.reserve(poolSize);
        for (size_t i = 0; i < poolSize; ++i)
            m_freeList.emplace_back(bytesPerFrame);
    }
    m_pendingChanged.notify_all();
}

void FrameCaptureService::clear() {
    std::lock_guard lock(m_lock);
    m_pendingFrames.clear();
    m_pendingChanged.notify_all();
}

void FrameCaptureService::releasePool() {
    std::lock_guard lock(m_lock);
    m_pendingFrames.clear();
    m_pendingFrames.shrink_to_fit();
    m_freeList.clear();
    m_freeList.shrink_to_fit();
    m_pendingChanged.notify_all();
}

void FrameCaptureService::notifyStop() {
    m_pendingChanged.notify_all();
}

bool FrameCaptureService::hasPendingFrame() const {
    std::lock_guard lock(m_lock);
    return !m_pendingFrames.empty();
}

std::vector<uint8_t> FrameCaptureService::acquireBuffer() {
    std::lock_guard lock(m_lock);
    if (m_bytesPerFrame == 0) {
        return {};
    }
    if (!m_freeList.empty()) {
        auto buf = std::move(m_freeList.back());
        m_freeList.pop_back();
        buf.resize(m_bytesPerFrame);
        return buf;
    }
    return std::vector<uint8_t>(m_bytesPerFrame);
}

void FrameCaptureService::releaseBuffer(std::vector<uint8_t>&& frame) {
    if (frame.capacity() == 0) return;
    std::lock_guard lock(m_lock);
    if (m_freeList.size() < m_maxBufferedFrames + 2)
        m_freeList.push_back(std::move(frame));
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
    m_pendingChanged.wait(lock, [&] {
        auto* engine = ReplayEngine::get();
        return !m_pendingFrames.empty()
            || !engine
            || !engine->renderer.recording;
    });
    if (m_pendingFrames.empty()) return {};

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
    return static_cast<float>(ReplayEngine::get()->runtimeTickRate());
}

static void copyFlippedRows(uint8_t* dst, const uint8_t* src, unsigned width, unsigned height) {
    size_t rowBytes = static_cast<size_t>(width) * 3;
    for (unsigned r = 0; r < height; ++r)
        std::memcpy(dst + static_cast<size_t>(height - 1 - r) * rowBytes,
                    src + static_cast<size_t>(r) * rowBytes, rowBytes);
}

#ifdef GEODE_IS_WINDOWS

static const char* kNv12VertSrc = R"(
attribute vec2 aPos;
varying vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = vec2(aPos.x * 0.5 + 0.5, 0.5 - aPos.y * 0.5);
}
)";

static const char* kYPlaneFragSrc = R"(
uniform sampler2D tex;
varying vec2 vUV;
void main() {
    vec3 c = texture2D(tex, vUV).rgb;
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    gl_FragColor = vec4(vec3(0.0627451 + 0.8588235 * y), 1.0);
}
)";

static const char* kUVPlaneFragSrc = R"(
uniform sampler2D tex;
varying vec2 vUV;
void main() {
    vec3 c = texture2D(tex, vUV).rgb;
    float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float u = 0.5019608 + 0.4733945 * (c.b - y);
    float v = 0.5019608 + 0.5578170 * (c.r - y);
    gl_FragColor = vec4(u, v, 0.0, 1.0);
}
)";

static GLuint compileNv12Shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512] = {};
        glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
        log::warn("NV12 shader compile failed: {}", buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint linkNv12Program(const char* fragSrc) {
    GLuint vs = compileNv12Shader(GL_VERTEX_SHADER, kNv12VertSrc);
    GLuint fs = compileNv12Shader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512] = {};
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        log::warn("NV12 program link failed: {}", buf);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}
#endif

bool RenderTexture::initNv12Pass() {
#ifdef GEODE_IS_WINDOWS
    if (!glBindVertexArray || width < 2 || height < 2) return false;
    while (glGetError() != GL_NO_ERROR) {}

    m_prog[0] = linkNv12Program(kYPlaneFragSrc);
    m_prog[1] = linkNv12Program(kUVPlaneFragSrc);
    if (!m_prog[0] || !m_prog[1]) { destroyNv12Pass(); return false; }

    GLint prevProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    for (GLuint prog : m_prog) {
        glUseProgram(prog);
        glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    }
    glUseProgram(static_cast<GLuint>(prevProg));

    const GLint    internalFmt[2] = { GL_R8, GL_RG8 };
    const GLenum   fmt[2]         = { GL_RED, GL_RG };
    const unsigned planeW[2]      = { width, width / 2 };
    const unsigned planeH[2]      = { height, height / 2 };
    glGenTextures(2, m_planeTex);
    glGenFramebuffers(2, m_planeFbo);
    for (int p = 0; p < 2; ++p) {
        glBindTexture(GL_TEXTURE_2D, m_planeTex[p]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt[p], planeW[p], planeH[p], 0,
                     fmt[p], GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, m_planeFbo[p]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_planeTex[p], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D, 0);
            destroyNv12Pass();
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) { destroyNv12Pass(); return false; }
    return true;
#else
    return false;
#endif
}

void RenderTexture::destroyNv12Pass() {
#ifdef GEODE_IS_WINDOWS
    for (GLuint& p : m_prog)     { if (p) glDeleteProgram(p); p = 0; }
    for (GLuint& f : m_planeFbo) { if (f) glDeleteFramebuffers(1, &f); f = 0; }
    for (GLuint& t : m_planeTex) { if (t) glDeleteTextures(1, &t); t = 0; }
#endif
}

void RenderTexture::runNv12Pass() {
#ifdef GEODE_IS_WINDOWS
    GLint prevProg = 0, prevArrayBuf = 0, prevActiveTex = 0, prevTex0 = 0, prevVao = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex0);
    GLboolean blendOn   = glIsEnabled(GL_BLEND);
    GLboolean scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depthOn   = glIsEnabled(GL_DEPTH_TEST);
    GLint attrib0On = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attrib0On);

    if (prevVao) glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, texture->getName());
    if (!attrib0On) glEnableVertexAttribArray(0);

    static const GLfloat kFullscreenTri[] = { -1.f, -1.f, 3.f, -1.f, -1.f, 3.f };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kFullscreenTri);
    for (int p = 0; p < 2; ++p) {
        glUseProgram(m_prog[p]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_planeFbo[p]);
        glViewport(0, 0, p ? width / 2 : width, p ? height / 2 : height);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    if (!attrib0On) glDisableVertexAttribArray(0);
    glUseProgram(static_cast<GLuint>(prevProg));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));
    if (prevVao) glBindVertexArray(static_cast<GLuint>(prevVao));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex0));
    glActiveTexture(static_cast<GLenum>(prevActiveTex));
    if (blendOn)   glEnable(GL_BLEND);
    if (scissorOn) glEnable(GL_SCISSOR_TEST);
    if (depthOn)   glEnable(GL_DEPTH_TEST);

    ccGLInvalidateStateCache();
#endif
}

void RenderTexture::begin(bool wantNv12, FrameCaptureService& frameCapture) {
#ifdef GEODE_IS_WINDOWS
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

    nv12 = wantNv12 && initNv12Pass();
    if (wantNv12 && !nv12)
        log::warn("GPU NV12 capture unavailable, falling back to rgb24");
    if (nv12) {

        glBindTexture(GL_TEXTURE_2D, texture->getName());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBindRenderbuffer(GL_RENDERBUFFER, old_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);

    m_pboSize       = static_cast<size_t>(width) * static_cast<size_t>(height) * (nv12 ? 3 : 6) / 2;
    m_pboCount      = kPboRingDepth;
    m_pboFrameCount = 0;
    m_dbgVisitSec = m_dbgMapSec = m_dbgCopySec = 0.0;
    m_dbgFrames = 0;
    glGenBuffers(m_pboCount, m_pbos);
    for (int i = 0; i < m_pboCount; ++i) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(m_pboSize), nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (m_pbos[0]) startCopyWorker(frameCapture);
#else

    end();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    texture = new CCTexture2D();
    {
#ifdef GEODE_IS_ANDROID
        unsigned char* data = static_cast<unsigned char*>(calloc(static_cast<size_t>(width) * height * 4, 1));
        if (!data) return;
        texture->initWithData(data, kCCTexture2DPixelFormat_RGBA8888, width, height,
            CCSize(static_cast<float>(width), static_cast<float>(height)));
        free(data);
#else
        unsigned char* data = static_cast<unsigned char*>(calloc(static_cast<size_t>(width) * height * 3, 1));
        if (!data) return;
        texture->initWithData(data, kCCTexture2DPixelFormat_RGB888, width, height,
            CCSize(static_cast<float>(width), static_cast<float>(height)));
        free(data);
#endif
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
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("Render framebuffer is incomplete at {}x{}", width, height);
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
        texture->release();
        texture = nullptr;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, old_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
    nv12 = false;
#endif
}

void RenderTexture::startCopyWorker(FrameCaptureService& frameCapture) {
#ifdef GEODE_IS_WINDOWS
    m_copyTarget   = &frameCapture;
    m_copyStop     = false;
    m_copyInFlight = 0;
    for (int i = 0; i < m_pboCount; ++i) { m_pboCopyBusy[i] = false; m_pboMapped[i] = false; }
    m_copyThread = std::thread([this] {
        for (;;) {
            CopyJob job;
            {
                std::unique_lock<std::mutex> lk(m_copyMutex);
                m_copyCv.wait(lk, [this] { return !m_copyJobs.empty() || m_copyStop; });
                if (m_copyJobs.empty()) return;
                job = m_copyJobs.front();
                m_copyJobs.pop_front();
            }
            auto frame = m_copyTarget->acquireBuffer();
            auto copyStart = std::chrono::steady_clock::now();
            if (!frame.empty()) {
                if (job.nv12)
                    std::memcpy(frame.data(), job.src, job.size);
                else
                    copyFlippedRows(frame.data(), job.src, job.w, job.h);
            }
            double copySec = std::chrono::duration<double>(std::chrono::steady_clock::now() - copyStart).count();
            {
                std::lock_guard<std::mutex> lk(m_copyMutex);
                m_pboCopyBusy[job.pboIndex] = false;
                m_dbgCopySec += copySec;
            }
            m_copyCv.notify_all();
            if (!frame.empty()) m_copyTarget->submit(std::move(frame));
            {
                std::lock_guard<std::mutex> lk(m_copyMutex);
                --m_copyInFlight;
            }
            m_copyCv.notify_all();
        }
    });
#endif
}

void RenderTexture::stopCopyWorker() {
#ifdef GEODE_IS_WINDOWS
    if (!m_copyThread.joinable()) return;
    { std::lock_guard<std::mutex> lk(m_copyMutex); m_copyStop = true; }
    m_copyCv.notify_all();
    m_copyThread.join();
    m_copyJobs.clear();
    m_copyTarget = nullptr;
#endif
}

void RenderTexture::releasePbo(int idx) {
#ifdef GEODE_IS_WINDOWS
    if (!m_pboMapped[idx]) return;
    {
        std::unique_lock<std::mutex> lk(m_copyMutex);
        m_copyCv.wait(lk, [this, idx] { return !m_pboCopyBusy[idx]; });
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[idx]);
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    m_pboMapped[idx] = false;
#endif
}

void RenderTexture::end() {
#ifdef GEODE_IS_WINDOWS
    stopCopyWorker();
    for (int i = 0; i < m_pboCount; ++i) {
        if (m_pboMapped[i] && m_pbos[i]) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[i]);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            m_pboMapped[i] = false;
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (m_pbos[0]) {
        glDeleteBuffers(m_pboCount, m_pbos);
        for (int i = 0; i < m_pboCount; ++i) m_pbos[i] = 0;
    }
    m_pboFrameCount = 0;
    destroyNv12Pass();
    if (fbo) {
        glDeleteFramebuffers(1, &fbo);
        fbo = 0;
    }
    if (texture) {
        texture->release();
        texture = nullptr;
    }
#else
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

void RenderTexture::capture(FrameCaptureService& frameCapture, cocos2d::CCNode* overlay, bool reuseLastScene) {
    CCDirector* director = CCDirector::sharedDirector();
    PlayLayer* pl = PlayLayer::get();
    if (!pl || !fbo) return;

#ifdef GEODE_IS_WINDOWS
    glViewport(0, 0, width, height);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (!reuseLastScene) {
        auto visitStart = std::chrono::steady_clock::now();
        pl->visit();
        if (overlay) overlay->visit();

        if (nv12) runNv12Pass();
        m_dbgVisitSec += std::chrono::duration<double>(std::chrono::steady_clock::now() - visitStart).count();
        ++m_dbgFrames;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    if (m_pbos[0]) {
        int writeIdx = m_pboFrameCount % m_pboCount;
        releasePbo(writeIdx);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[writeIdx]);
        if (nv12) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_planeFbo[0]);
            glReadPixels(0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, m_planeFbo[1]);
            glReadPixels(0, 0, width / 2, height / 2, GL_RG, GL_UNSIGNED_BYTE,
                reinterpret_cast<void*>(static_cast<size_t>(width) * height));
        } else {
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        }

        if (m_pboFrameCount >= m_pboCount - 1) {
            int readIdx = (m_pboFrameCount - (m_pboCount - 1)) % m_pboCount;
            submitPbo(readIdx, frameCapture);
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        ++m_pboFrameCount;
    } else {
        auto frame = frameCapture.acquireBuffer();
        if (!frame.empty()) {
            if (nv12) {
                glBindFramebuffer(GL_FRAMEBUFFER, m_planeFbo[0]);
                glReadPixels(0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, frame.data());
                glBindFramebuffer(GL_FRAMEBUFFER, m_planeFbo[1]);
                glReadPixels(0, 0, width / 2, height / 2, GL_RG, GL_UNSIGNED_BYTE,
                    frame.data() + static_cast<size_t>(width) * height);
            } else {
                glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, frame.data());
                size_t rowBytes = static_cast<size_t>(width) * 3;
                std::vector<uint8_t> tmp(rowBytes);
                for (unsigned r = 0; r < height / 2; ++r) {
                    uint8_t* a = frame.data() + static_cast<size_t>(r) * rowBytes;
                    uint8_t* b = frame.data() + static_cast<size_t>(height - 1 - r) * rowBytes;
                    std::memcpy(tmp.data(), a, rowBytes);
                    std::memcpy(a, b, rowBytes);
                    std::memcpy(b, tmp.data(), rowBytes);
                }
            }
            frameCapture.submit(std::move(frame));
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
    director->setViewport();
#else

    glViewport(0, 0, width, height);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (!reuseLastScene) {
        pl->visit();
        if (overlay) overlay->visit();
    }
    auto frame = frameCapture.acquireBuffer();
    if (!frame.empty()) {
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        size_t rowBytes = static_cast<size_t>(width) * 3;
#ifdef GEODE_IS_ANDROID

        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        const size_t px = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < px; ++i) {
            frame[i * 3 + 0] = rgba[i * 4 + 0];
            frame[i * 3 + 1] = rgba[i * 4 + 1];
            frame[i * 3 + 2] = rgba[i * 4 + 2];
        }
#else
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, frame.data());
#endif
        std::vector<uint8_t> tmp(rowBytes);
        for (unsigned r = 0; r < height / 2; ++r) {
            uint8_t* a = frame.data() + static_cast<size_t>(r) * rowBytes;
            uint8_t* b = frame.data() + static_cast<size_t>(height - 1 - r) * rowBytes;
            std::memcpy(tmp.data(), a, rowBytes);
            std::memcpy(a, b, rowBytes);
            std::memcpy(b, tmp.data(), rowBytes);
        }
        frameCapture.submit(std::move(frame));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
    director->setViewport();
#endif
}

void RenderTexture::submitPbo(int pboIndex, FrameCaptureService& frameCapture) {
#ifdef GEODE_IS_WINDOWS
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_pbos[pboIndex]);
    auto mapStart = std::chrono::steady_clock::now();
    void* ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(m_pboSize), GL_MAP_READ_BIT);
    m_dbgMapSec += std::chrono::duration<double>(std::chrono::steady_clock::now() - mapStart).count();
    if (!ptr) return;

    if (m_copyThread.joinable()) {
        {
            std::lock_guard<std::mutex> lk(m_copyMutex);
            m_pboCopyBusy[pboIndex] = true;
            ++m_copyInFlight;
            m_copyJobs.push_back({ static_cast<const uint8_t*>(ptr), pboIndex, nv12, width, height, m_pboSize });
        }
        m_pboMapped[pboIndex] = true;
        m_copyCv.notify_all();
        return;
    }
    auto frame = frameCapture.acquireBuffer();
    if (!frame.empty()) {
        auto copyStart = std::chrono::steady_clock::now();
        if (nv12)
            std::memcpy(frame.data(), ptr, m_pboSize);
        else
            copyFlippedRows(frame.data(), static_cast<const uint8_t*>(ptr), width, height);
        m_dbgCopySec += std::chrono::duration<double>(std::chrono::steady_clock::now() - copyStart).count();
        frameCapture.submit(std::move(frame));
    }
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
#endif
}

void Renderer::captureFrame(bool reuseLastScene) {
    if (!recording) {
        return;
    }
    renderTex.capture(frameCapture, watermarkLabel, reuseLastScene);
}

void RenderTexture::flushPbo(FrameCaptureService& frameCapture) {
#ifdef GEODE_IS_WINDOWS
    if (!m_pbos[0] || m_pboFrameCount == 0) return;

    int readDone = std::max(0, m_pboFrameCount - (m_pboCount - 1));
    for (int f = readDone; f < m_pboFrameCount; ++f)
        submitPbo(f % m_pboCount, frameCapture);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    {
        std::unique_lock<std::mutex> lk(m_copyMutex);
        m_copyCv.wait(lk, [this] { return m_copyInFlight == 0; });
    }
    for (int i = 0; i < m_pboCount; ++i) releasePbo(i);
    m_pboFrameCount = 0;

#endif
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
    auto exePath = Mod::get()->getSettingValue<std::filesystem::path>("ffmpeg_path");
    if (!resolveUsableFfmpegExecutable(exePath).empty()) return false;
    return Loader::get()->isModLoaded("eclipse.ffmpeg-api");
#else
    return true;
#endif
}

bool Renderer::resolveEncoder() {
#ifdef GEODE_IS_WINDOWS
    auto exePath = Mod::get()->getSettingValue<std::filesystem::path>("ffmpeg_path");
    auto resolved = resolveUsableFfmpegExecutable(exePath);
    if (!resolved.empty()) { ffmpegPath = resolved; return true; }
    if (Loader::get()->isModLoaded("eclipse.ffmpeg-api")) return true;
    return false;
#else
    return true;
#endif
}

bool Renderer::toggle() {
    auto* engine = ReplayEngine::get();

    if (engine->renderer.recording) {
        engine->renderer.stop(engine->renderer.getCurrentFrame());
        return true;
    }

    if (engine->renderer.m_encodeActive.load(std::memory_order_acquire)) {
        Notification::create(
            trString("The previous render is still finalizing. Wait a moment and try again."),
            NotificationIcon::Warning
        )->show();
        return false;
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
        engine->renderer.startFromPending();
    } else {
        if (utils::file::createDirectoryAll(renderPath).isOk())
            engine->renderer.startFromPending();
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

void Renderer::startFromPending() {
    if (m_pendingConfig.has_value()) {
        auto cfg = std::move(*m_pendingConfig);
        m_pendingConfig.reset();
        start(cfg);
    } else {
        start();
    }
}

#ifdef GEODE_IS_WINDOWS
static bool isValidCodecName(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    return true;
}

static std::string buildQualitySection(const ResolvedEncodeParams& rp) {
    bool isNvenc = rp.codec.find("nvenc")  != std::string::npos;
    bool isAmf   = rp.codec.find("amf")    != std::string::npos;
    bool isQsv   = rp.codec.find("qsv")    != std::string::npos;
    bool isVpx   = rp.codec.find("libvpx") != std::string::npos;
    bool isVvc   = rp.codec.find("vvenc")  != std::string::npos;
    std::string q;
    if (isNvenc)
        q = fmt::format("-rc vbr -cq {} -b:v 0", rp.crf);
    else if (isAmf)
        q = fmt::format("-rc cqp -qp_i {} -qp_p {} -qp_b {}", rp.crf, rp.crf, rp.crf);
    else if (isQsv) {
        bool isAv1Qsv = rp.codec.find("av1") != std::string::npos;
        int qsvQ = isAv1Qsv ? std::max(1, (rp.crf * 51 + 32) / 63) : rp.crf;
        q = fmt::format("-global_quality {} -b:v 0", qsvQ);
    } else if (isVpx) {
        q = fmt::format("-crf {} -b:v 0", rp.crf);
        if (!rp.x264Preset.empty())
            q += fmt::format(" -deadline good -cpu-used {}", rp.x264Preset);
        if (rp.codec.find("vp9") != std::string::npos)
            q += " -row-mt 1";
        return q;
    } else if (isVvc) {
        q = fmt::format("-qp {}", rp.crf);
        if (!rp.x264Preset.empty())
            q += fmt::format(" -preset {}", rp.x264Preset);
        return q;
    } else
        q = fmt::format("-crf {}", rp.crf);
    if (!rp.x264Preset.empty())
        q += fmt::format(" -preset {}", rp.x264Preset);
    if (!rp.maxBitrate.empty())
        q += fmt::format(" -maxrate {} -bufsize {}", rp.maxBitrate, rp.maxBitrate);
    return q;
}

static bool testCodec(const std::filesystem::path& exe, const std::string& codec,
                      const std::string& qualityArgs = "") {
    if (!isValidCodecName(codec)) return false;
    auto cmd = fmt::format(
        "\"{}\" -nostdin -f lavfi -i color=black:size=256x256:rate=1 -frames:v 1 -c:v {} {} -f null NUL",
        toasty::pathToUtf8(exe), codec, qualityArgs
    );
    int len = MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, nullptr, 0);
    if (len <= 0) return false;
    std::wstring wcmd(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, cmd.c_str(), -1, wcmd.data(), len);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE hNull = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = hNull;
    si.hStdOutput = hNull;
    si.hStdError  = hNull;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    if (!ok) return false;
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}
#endif

void Renderer::start(const RenderConfig& config) {
    m_resolvedParams = resolve(config);

    fps               = config.fps;
    width             = config.width;
    height            = config.height;
    sfxVolume         = config.sfxVol;
    musicVolume       = config.musicVol;
    stopAfter         = config.secondsAfter;
    includeClickSounds = config.includeClicks;
    audioMode         = config.includeAudio ? AUDIO_SONG : AUDIO_OFF;
    hideEndscreen     = config.hideEndscreen;
    hideLevelComplete = config.hideLevelComplete;

    bool familyNeedsExe = config.codecFamily == RenderCodecFamily::AV1
                       || config.codecFamily == RenderCodecFamily::VP8
                       || config.codecFamily == RenderCodecFamily::VP9
                       || config.codecFamily == RenderCodecFamily::VVC;
    if (config.tier == RenderQualityTier::Lossless || familyNeedsExe) {
#ifdef GEODE_IS_WINDOWS
        if (ffmpegPath.empty()) {
            auto exePath = Mod::get()->getSettingValue<std::filesystem::path>("ffmpeg_path");
            ffmpegPath = resolveUsableFfmpegExecutable(exePath);
        }
        if (ffmpegPath.empty()) {
            m_resolvedParams.reset();
            Loader::get()->queueInMainThread([] {
                auto title = trString("Error");
                auto msg = trString("AV1, VP8/VP9, H.266 and Lossless require <cl>ffmpeg.exe</c>. Configure the path in this mod's settings or switch to H264.");
                auto ok = trString("Ok");
                FLAlertLayer::create(title.c_str(), msg.c_str(), ok.c_str())->show();
            });
            return;
        }
#endif
        usingApi = false;
    }

#ifdef GEODE_IS_WINDOWS
    if (!usingApi && !ffmpegPath.empty()) {
        const std::string gpuCodec = m_resolvedParams->codec;
        bool isGpuCodec = gpuCodec.find("nvenc") != std::string::npos
                       || gpuCodec.find("amf")   != std::string::npos
                       || gpuCodec.find("qsv")   != std::string::npos;
        auto withTuning = [](const ResolvedEncodeParams& p) {
            auto q = buildQualitySection(p);
            if (!p.tuning.empty()) q += " " + p.tuning;
            return q;
        };

        if (isGpuCodec) {
            if (!testCodec(ffmpegPath, gpuCodec, withTuning(*m_resolvedParams))) {
                if (!m_resolvedParams->tuning.empty()
                    && testCodec(ffmpegPath, gpuCodec, buildQualitySection(*m_resolvedParams))) {
                    m_resolvedParams->tuning.clear();
                    Loader::get()->queueInMainThread([] {
                        Notification::create(trString("Encoder tuning unsupported - using standard GPU settings"), NotificationIcon::Warning)->show();
                    });
                } else {
                    RenderConfig cpuCfg = config;
                    cpuCfg.useGpu = false;
                    cpuCfg.gpuEncoder.clear();
                    m_resolvedParams = resolve(cpuCfg);
                    const std::string cpuCodec = m_resolvedParams->codec;
                    if (!testCodec(ffmpegPath, cpuCodec, withTuning(*m_resolvedParams))) {
                        m_resolvedParams.reset();
                        Loader::get()->queueInMainThread([cpuCodec] {
                            auto title = trString("Error");
                            auto msg = fmt::format("Codec <cl>{}</c> not available in your ffmpeg.exe. Use a full-featured build.", cpuCodec);
                            auto ok = trString("Ok");
                            FLAlertLayer::create(title.c_str(), msg.c_str(), ok.c_str())->show();
                        });
                        return;
                    }
                    Loader::get()->queueInMainThread([gpuCodec, cpuCodec] {
                        auto msg = fmt::format("GPU encoder {} not supported, using {}", gpuCodec, cpuCodec);
                        Notification::create(msg, NotificationIcon::Warning)->show();
                    });
                }
            }
        } else if (!testCodec(ffmpegPath, gpuCodec, withTuning(*m_resolvedParams))) {
            m_resolvedParams.reset();
            Loader::get()->queueInMainThread([gpuCodec] {
                auto title = trString("Error");
                auto msg = fmt::format("Codec <cl>{}</c> not available in your ffmpeg.exe. Use a full-featured build.", gpuCodec);
                auto ok = trString("Ok");
                FLAlertLayer::create(title.c_str(), msg.c_str(), ok.c_str())->show();
            });
            return;
        }
    }
#endif

    codec           = m_resolvedParams->codec;
    extraArgs       = m_resolvedParams->extraArgs;
    videoArgs       = m_resolvedParams->videoArgs;
    extraAudioArgs  = m_resolvedParams->audioArgs;
    audioCodec      = m_resolvedParams->audioCodec;
    m_extOverride   = m_resolvedParams->ext;

    if (config.codecFamily == RenderCodecFamily::H265 && usingApi)
        m_extOverride = ".mkv";

    start();
}

void Renderer::start() {
    PlayLayer* pl = PlayLayer::get();
    if (!pl || !pl->m_level || !pl->m_levelSettings) return;

    auto* engine = ReplayEngine::get();
    engine->clearFrameStepState();
    Mod* mod = Mod::get();
    fmod = FMODAudioEngine::sharedEngine();
    if (!fmod) {
        m_resolvedParams.reset();
        Loader::get()->queueInMainThread([] {
            Notification::create(trString("Render aborted: audio engine unavailable."), NotificationIcon::Error)->show();
        });
        return;
    }

    std::string extension;

    if (!m_resolvedParams.has_value()) {
        fps    = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_fps", 60));
        codec  = mod->getSavedValue<std::string>("render_codec", "");
        if (codec.empty()) codec = "libx264";
        bitrate = mod->getSavedValue<std::string>("render_bitrate", "40") + "M";
        extraArgs    = loadSavedValueWithFallback<std::string>(mod, "render_args", "-pix_fmt yuv420p", {"render_extra_args"});
        videoArgs    = mod->getSavedValue<std::string>("render_video_args", "colorspace=all=bt709:iall=bt470bg:fast=1");
        extraAudioArgs = mod->getSavedValue<std::string>("render_audio_args", "");
        auto savedAudioCodec = mod->getSavedValue<std::string>("render_audio_codec", "");
        audioCodec = savedAudioCodec.empty() ? "aac" : savedAudioCodec;
        sfxVolume    = static_cast<float>(mod->getSavedValue<double>("render_sfx_volume", 1.0));
        musicVolume  = static_cast<float>(mod->getSavedValue<double>("render_music_volume", 1.0));
        stopAfter    = static_cast<float>(
            geode::utils::numFromString<float>(
                loadSavedValueWithFallback<std::string>(mod, "render_seconds_after", "3", {"render_after_seconds"})
            ).unwrapOr(3.f));
        extension = loadSavedValueWithFallback<std::string>(mod, "render_file_extension", ".mp4", {"render_extension"});
        if (extension == ".mp4") {
            for (const char* c : { "libopus", "flac" })
                if (audioCodec == c) { extension = ".mkv"; break; }
        }
        audioMode = loadSavedValueWithFallback<bool>(mod, "render_include_audio", true, {"render_record_audio", "render_capture_audio"})
            ? AUDIO_SONG : AUDIO_OFF;
        includeClickSounds = mod->getSavedValue<bool>("render_include_clicks", false);
        width  = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_width", 1920));
        height = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_height", 1080));
        hideEndscreen     = mod->getSavedValue<bool>("render_hide_endscreen", false);
        hideLevelComplete = mod->getSavedValue<bool>("render_hide_levelcomplete", false);
    } else {
        extension = m_extOverride;
        m_extOverride.clear();
    }

    cadenceLogFrames = 0;
    lastProgressPercent = -1;

    std::string customName = mod->getSavedValue<std::string>("render_name", "");
    std::string baseName = customName.empty()
        ? fmt::format("render_{}_{}x{}", std::string_view(pl->m_level->m_levelName), width, height)
        : customName;

    std::filesystem::path renderFolder = Mod::get()->getSettingValue<std::filesystem::path>("render_folder");
    if (renderFolder.empty() || pathContainsMarker(renderFolder, "{gd_dir}"))
        renderFolder = dirs::getGameDir() / "renders";

    std::error_code ec;
    std::filesystem::create_directories(renderFolder, ec);

    auto candidatePath = [&](int n) {
        std::string name = n <= 1 ? baseName + extension
                                  : fmt::format("{} ({}){}", baseName, n, extension);
        return renderFolder / name;
    };
    int counter = 1;
    while (std::filesystem::exists(candidatePath(counter))) ++counter;
    path = candidatePath(counter);

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
    clockPrimed = false;
    leadInSeconds = 0.0;
    s_renderClock.reset();

    encodeSession.reset();
    audioCapture.reset();
    m_capturedFrameSet.clear();
    m_capturedSounds.clear();

    bool wantNv12 = m_resolvedParams.has_value() && !usingApi
        && m_resolvedParams->codec.find("rgb") == std::string::npos;
    renderTex.begin(wantNv12, frameCapture);

    size_t bytesPerFrame = static_cast<size_t>(width) * static_cast<size_t>(height)
        * (renderTex.nv12 ? 3 : 6) / 2;
    constexpr size_t kFrameQueueBudget = 96ull * 1024 * 1024;
    size_t queueDepth = bytesPerFrame ? kFrameQueueBudget / bytesPerFrame : 8;
    queueDepth = std::clamp<size_t>(queueDepth, 3, 8);
    frameCapture.configure(bytesPerFrame, queueDepth);
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
        auto progressText = trFormat("Rendering... {percent}%", toasty::lang::arg("percent", 0));
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

    float songOffset = pl->m_levelSettings->m_songOffset +
        (static_cast<float>(levelStartFrame) / getTPS());
    bool fadeIn = pl->m_levelSettings->m_fadeIn;
    bool fadeOut = pl->m_levelSettings->m_fadeOut;

    bool preferExe = false;
#ifdef GEODE_IS_WINDOWS
    preferExe = pathExists(ffmpegPath);
#endif
    bool willMixRawAudio = audioMode == AUDIO_SONG
        && (!preferExe || includeClickSounds || musicVolume != 1.0f
            || std::max(songOffset, 0.0f) > 0.0f || fadeIn || fadeOut
            || isPersistenceRender(ReplayEngine::get()));
    leadInFixEligible = !willMixRawAudio;
    int64_t bitrateApi = m_resolvedParams.has_value()
        ? m_resolvedParams->apiBitrate
        : geode::utils::numFromString<int64_t>(
            mod->getSavedValue<std::string>("render_bitrate", "40")).unwrapOr(40) * 1000000;

    if (includeClickSounds) {
        int systemRate = 44100;
        fmod->m_system->getSoftwareFormat(&systemRate, nullptr, nullptr);
        ClickSoundManager::get()->preDecodeForRender(systemRate);
    }

    m_encodeActive.store(true, std::memory_order_release);
    m_encodeThread = std::thread(&Renderer::runEncodeLoop, this, songFile, songOffset, fadeIn, fadeOut, extension, bitrateApi);
    m_encodeThread.detach();
}

void Renderer::runEncodeLoop(std::filesystem::path songFile, float songOffset, bool fadeIn, bool fadeOut, std::string extension, int64_t bitrateApi) {
        struct EncodeActiveGuard {
            Renderer* self;
            ~EncodeActiveGuard() {
                self->frameCapture.releasePool();
                self->m_encodeActive.store(false, std::memory_order_release);
            }
        } encodeActiveGuard{ this };

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
        settings.m_doVerticalFlip = false;

        auto availableCodecs = ffmpeg::events::Recorder::getAvailableCodecs();

        if (!availableCodecs.empty() && std::find(availableCodecs.begin(), availableCodecs.end(), settings.m_codec) == availableCodecs.end()) {
            auto requestedCodec = settings.m_codec;
            std::vector<std::string> fallbacks;
            bool wantHevc = settings.m_codec.find("hevc") != std::string::npos
                         || settings.m_codec.find("265")  != std::string::npos;
            if (wantHevc)
                fallbacks = {"hevc_nvenc", "hevc_amf", "hevc_qsv", "libx265"};
            for (const char* fb : {"libx264", "h264", "libx264rgb", "mpeg4", "libvpx-vp9"})
                fallbacks.emplace_back(fb);
            bool found = false;
            for (auto& fb : fallbacks) {
                if (std::find(availableCodecs.begin(), availableCodecs.end(), fb) != availableCodecs.end()) {

                    settings.m_codec = fb;
                    found = true;
                    break;
                }
            }
            if (!found && !availableCodecs.empty()) {
                settings.m_codec = availableCodecs[0];

            }
            log::warn("Codec '{}' is unavailable, using '{}'", requestedCodec, settings.m_codec);
        }

        if (m_resolvedParams.has_value()) {
            if (!usingApi)
                settings.m_codec = m_resolvedParams->codec;
            settings.m_bitrate = m_resolvedParams->apiBitrate;
        }

        std::string localCodec = codec;
        std::string localBitrate = bitrate;
        std::string localExtraArgs = extraArgs;
        std::string localVideoArgs = videoArgs;

        bool useApiForEncoding = usingApi;

        std::future<std::vector<float>> songDecodeFuture;
        {
            auto* eng = ReplayEngine::get();
            bool persistence = isPersistenceRender(eng);
            bool preferExe = false;
#ifdef GEODE_IS_WINDOWS
            preferExe = pathExists(ffmpegPath);
#endif
            float off = std::max(songOffset, 0.0f);
            bool needRaw = !preferExe || includeClickSounds || musicVolume != 1.0f
                || off > 0.0f || fadeIn || fadeOut || persistence;
            if (needRaw && !persistence && audioMode == AUDIO_SONG && pathExists(songFile)) {
                auto sf = songFile;
                float vol = musicVolume;
                songDecodeFuture = std::async(std::launch::async, [sf, off, vol] {
                    return decodeSongToRaw(sf, off, 9999.f, vol);
                });
            }
        }

        {
#ifdef GEODE_IS_WINDOWS
        Subprocess process;
#endif
        ffmpeg::events::Recorder recorder;

        if (useApiForEncoding) {
            auto res = recorder.init(settings);
            if (res.isErr()) {
#ifdef GEODE_IS_WINDOWS
                if (pathExists(ffmpegPath)) {
                    log::warn("FFmpeg API initialization failed, using ffmpeg.exe: {}", res.unwrapErr());
                    useApiForEncoding = false;
                    Loader::get()->queueInMainThread([] {
                        Notification::create(trString("FFmpeg API not loaded - falling back to ffmpeg.exe"), NotificationIcon::Warning)->show();
                    });
                } else {
#endif
                    log::error("Could not initialize the FFmpeg encoder: {}", res.unwrapErr());
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
            std::string command;
            if (m_resolvedParams.has_value()) {
                auto const& rp = *m_resolvedParams;

                std::string qualitySection = buildQualitySection(rp);
                if (!rp.tuning.empty()) qualitySection += " " + rp.tuning;
                {
                    bool isIsoBmff = rp.ext == ".mp4" || rp.ext == ".mov" || rp.ext == ".m4v";
                    bool isHevc = rp.codec.find("hevc") != std::string::npos
                               || rp.codec.find("265")  != std::string::npos;
                    if (isHevc && isIsoBmff)
                        qualitySection += " -tag:v hvc1";
                    else if (rp.codec.find("vvenc") != std::string::npos && isIsoBmff)
                        qualitySection += " -tag:v vvc1";
                }

                bool nv12Pipe = renderTex.nv12;
                std::string vfArg;
                std::string colorTags;
                if (nv12Pipe) {

                    bool userFilter = !rp.videoArgs.empty() && rp.videoArgs != kDefaultVideoArgs;
                    if (userFilter) vfArg = fmt::format("-vf \"{}\" ", rp.videoArgs);
                    colorTags = userFilter
                        ? (rp.colorTags.empty() ? "" : rp.colorTags + " ")
                        : (rp.colorFix ? std::string(kFastColorTags) + " " : "");
                } else {
                    if (!rp.videoArgs.empty()) vfArg = fmt::format("-vf \"{}\" ", rp.videoArgs);
                    if (!rp.colorTags.empty()) colorTags = rp.colorTags + " ";
                }
                command = fmt::format(
                    "\"{}\" -y -f rawvideo -pix_fmt {} -s {}x{} -r {} -thread_queue_size 512 -i - -c:v {} {} {} {}{}-an -r {} \"{}\"",
                    toasty::pathToUtf8(ffmpegPath),
                    nv12Pipe ? "nv12" : "rgb24",
                    width, height, fps,
                    rp.codec, qualitySection, rp.extraArgs,
                    vfArg, colorTags,
                    fps,
                    toasty::pathToUtf8(path)
                );
            } else {
                if (!localCodec.empty()) localCodec = "-c:v " + localCodec + " ";
                if (!localBitrate.empty()) localBitrate = "-b:v " + localBitrate + " ";
                if (localExtraArgs.empty()) localExtraArgs = "-pix_fmt yuv420p";
                if (localVideoArgs.empty()) localVideoArgs = "colorspace=all=bt709:iall=bt470bg:fast=1";

                command = fmt::format(
                    "\"{}\" -y -f rawvideo -pix_fmt rgb24 -s {}x{} -r {} -thread_queue_size 512 -i - {}{}{} -vf \"{}\" -an -r {} \"{}\"",
                    toasty::pathToUtf8(ffmpegPath),
                    std::to_string(width), std::to_string(height), std::to_string(fps),
                    localCodec, localBitrate, localExtraArgs,
                    localVideoArgs,
                    fps,
                    toasty::pathToUtf8(path)
                );
            }

            auto stderrLogPath = (Mod::get()->getSaveDir() / "last_render_ffmpeg.log").wstring();
            process = Subprocess(command, stderrLogPath);
#endif
        }
        auto encodeWallStart = std::chrono::steady_clock::now();
        int  framesEncoded = 0;
        double waitSeconds = 0.0;

        while (recording || pause || frameCapture.hasPendingFrame()) {
            auto waitStart = std::chrono::steady_clock::now();
            auto frame = frameCapture.takeFrame();
            waitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - waitStart).count();
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
                    else if (!process.writeStdin(data.data(), data.size())) {
                        log::error("FFmpeg subprocess stopped accepting frames, aborting render");
                        Loader::get()->queueInMainThread([this] {
                            auto errorTitle = trString("Error");
                            auto errorMessage = trString("The FFmpeg process exited unexpectedly. Check your encoder settings.");
                            auto okText = trString("Ok");
                            FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                            stop();
                        });
                        audioMode = AUDIO_OFF;
                        recording = false;
                        return false;
                    }
#endif
                    return true;
                };
                if (!writeFrame(frame)) break;
                frameCapture.releaseBuffer(std::move(frame));
                ++framesEncoded;
            }
        }

        auto finishEncoder = [&]() -> bool {
            if (useApiForEncoding) {
                recorder.stop();
                return true;
            }
#ifdef GEODE_IS_WINDOWS
            if (int exitCode = process.close()) {
                log::error("FFmpeg exited with code {}; see {}", exitCode,
                    toasty::pathToUtf8(Mod::get()->getSaveDir() / "last_render_ffmpeg.log"));
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
            } else if (songDecodeFuture.valid()) {
                rawAudio = songDecodeFuture.get();
            } else {
                rawAudio = decodeSongToRaw(songFile, nonNegativeSongOffset, lastFrame_t, musicVolume);
            }
        }
        if (songDecodeFuture.valid()) songDecodeFuture.get();

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

        if (includeClickSounds && !m_capturedSounds.empty()) {
            if (rawAudio.empty()) {
                size_t silentSamples = static_cast<size_t>(lastFrame_t * rawAudioSampleRate) * 2;
                if (silentSamples > 0)
                    rawAudio.resize(silentSamples, 0.0f);
            }
            if (!rawAudio.empty()) {
                std::unordered_map<std::string, std::vector<float>> effectCache;
                for (auto& ev : m_capturedSounds) {
                    auto it = effectCache.find(ev.path);
                    if (it == effectCache.end())
                        it = effectCache.emplace(ev.path, decodeEffectToRaw(ev.path)).first;
                    const auto& pcm = it->second;
                    if (pcm.empty()) continue;
                    size_t offsetSamples = static_cast<size_t>(ev.time * rawAudioSampleRate) * 2;
                    if (offsetSamples >= rawAudio.size()) continue;
                    float vol = ev.volume * sfxVolume;
                    size_t mixLen = std::min(pcm.size(), rawAudio.size() - offsetSamples);
                    for (size_t i = 0; i < mixLen; i++)
                        rawAudio[offsetSamples + i] = std::clamp(rawAudio[offsetSamples + i] + pcm[i] * vol, -1.0f, 1.0f);
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

        if (audioMode == AUDIO_SONG && !hasRawAudio && !pathExists(songFile) && !includeClickSounds) {
            log::warn("Song file '{}' was not found, so the render will have no audio", toasty::pathToUtf8(songFile));
        }

        if (!hasAudio) {
            Loader::get()->queueInMainThread([this] {
                Notification::create(trString("Render done without audio"), NotificationIcon::Success)->show();
                if (ReplayEngine::get()->renderer.hideEndscreen) {
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

        double totalTime = std::max(0.0, lastFrame_t - leadInSeconds);

        if (totalTime <= 0.0 || !pathExists(path)) {
            Loader::get()->queueInMainThread([] {
                auto title = trString("Error");
                auto msg   = trString("No frames were captured. The render produced no output.");
                auto ok    = trString("Ok");
                FLAlertLayer::create(title.c_str(), msg.c_str(), ok.c_str())->show();
            });
            return;
        }

        bool audioMuxed = false;
        bool apiMuxed   = false;

        if (!preferExeAudioMux && useApiForEncoding) {
            if (hasRawAudio) {
                if (rawAudioSampleRate != kApiMixSampleRate) {

                    rawAudio = resampleInterleavedAudio(rawAudio, 2, rawAudioSampleRate, kApiMixSampleRate);
                    rawAudioSampleRate = kApiMixSampleRate;
                    fitAudioToDuration(rawAudio, totalTime, rawAudioSampleRate);
                }

                auto rawRes = ffmpeg::events::AudioMixer::mixVideoRaw(path, rawAudio, tempPath);
                if (rawRes.isOk()) {
                    audioMuxed = apiMuxed = true;

                } else {
                    log::warn("Raw audio mux failed, trying the WAV fallback: {}", rawRes.unwrapErr());

                    if (ensureRawAudioFile()) {
                        auto fileRes = ffmpeg::events::AudioMixer::mixVideoAudio(path, rawAudioPath, tempPath);
                        if (fileRes.isOk()) {
                            audioMuxed = apiMuxed = true;

                        } else {
                            log::error("Could not mux the temporary WAV audio: {}", fileRes.unwrapErr());
                        }
                    }
                }
            } else {
                auto fileRes = ffmpeg::events::AudioMixer::mixVideoAudio(path, songFile, tempPath);
                if (fileRes.isOk()) {
                    audioMuxed = apiMuxed = true;

                } else {
                    log::error("Could not mux the song audio: {}", fileRes.unwrapErr());
                }
            }
        }

        if (apiMuxed && !audioCodec.empty() && audioCodec != "aac") {
            Loader::get()->queueInMainThread([ac = audioCodec] {
                auto msg = fmt::format("Audio muxed as AAC - ffmpeg.exe needed for {}", ac);
                Notification::create(msg, NotificationIcon::Warning)->show();
            });
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
                audioOffset = nonNegativeSongOffset + static_cast<float>(leadInSeconds);
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
                extraAudioArgs,
                audioCodec
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
        if (ec) log::error("Could not replace the old render file: {}", ec.message());
        else {
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) log::error("Could not move the completed render into place: {}", ec.message());

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

        m_resolvedParams.reset();
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
    renderTex.flushPbo(frameCapture);
    recording = false;
    frameCapture.notifyStop();
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
        if (pct != lastProgressPercent) {
            lastProgressPercent = pct;
            auto progressText = trFormat("Rendering... {percent}%", toasty::lang::arg("percent", pct));
            progressLabel->setString(progressText.c_str());
        }
    }

    bool expectedPersistenceDeath = engine
        && engine->isPersistencePlaybackFailureAttempt()
        && engine->persistencePlaybackDeathPending;
    if (!pl->m_player1 || (pl->m_player1->m_isDead && !expectedPersistenceDeath)) {

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
        if (!clockPrimed) {

            clockPrimed = true;
            if (leadInFixEligible) {
                leadInSeconds = renderTime;
            }
            lastFrame_t = renderTime - s_renderClock.frameDelta;
            extra_t = 0.0;
        }
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

            bool sceneRendered = false;
            while (time >= s_renderClock.frameDelta) {
                captureFrame(sceneRendered);
                sceneRendered = true;
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
            engine->renderer.hideLevelComplete;

        hideLevelCompleteText(this, hideLevelComplete);
    }
};

class $modify(RenderEndLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!PlayLayer::get()) return;
        auto* engine = ReplayEngine::get();
        if (engine->renderer.recording && PlayLayer::get()->m_levelEndAnimationStarted &&
            engine->renderer.hideEndscreen) {
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
        auto& r = engine->renderer;
        if (r.recording) {
            if (r.includeClickSounds) {
                auto resolved = cocos2d::CCFileUtils::sharedFileUtils()->fullPathForFilename(
                    std::string(path).c_str(), false
                );
                r.m_capturedSounds.push_back({
                    resolved.empty() ? std::string(path) : std::string(resolved.c_str()),
                    static_cast<float>(r.lastFrame_t),
                    volume,
                    speed
                });
            }
            if (std::string_view(path) == "explode_11.ogg") return 0;
        }
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
            engine->renderer.hideLevelComplete)
            ret->setVisible(false);

        return ret;
    }
};
