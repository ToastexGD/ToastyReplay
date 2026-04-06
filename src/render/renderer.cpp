#include "render/renderer.hpp"
#include "i18n/localization.hpp"
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
#include <filesystem>
#include <chrono>
#include <fstream>
#include <cmath>
#include <string_view>
#include <fmt/format.h>

using namespace geode::prelude;

static constexpr int kApiMixSampleRate = 44100;

static std::string trString(std::string_view key) {
    return std::string(toasty::i18n::tr(key));
}

template <class... Args>
static std::string trFormat(std::string_view key, Args&&... args) {
    return toasty::i18n::trf(key, std::forward<Args>(args)...);
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

static bool pathExists(std::filesystem::path const& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(path, ec) && !ec;
}

static bool pathContainsMarker(std::filesystem::path const& path, std::string_view marker) {
    return toasty::pathToUtf8(path).find(marker) != std::string::npos;
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

    std::string command = fmt::format(
        "\"{}\" -y -ss {:.6f} -i \"{}\" -i \"{}\" -t {:.6f} -map 1:v -map 0:a -c:v copy -c:a aac {}-af \"adelay=0|0{}{},volume={:.2f}\" \"{}\"",
        toasty::pathToUtf8(ffmpegPath),
        audioOffset,
        toasty::pathToUtf8(audioInput),
        toasty::pathToUtf8(videoInput),
        totalTime,
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
    return engine->lastTickIndex;
}

float Renderer::getTPS() const {
    return static_cast<float>(ReplayEngine::get()->tickRate);
}

void RenderTexture::begin() {
#ifdef GEODE_IS_WINDOWS
    end();

    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &old_fbo);

    texture = new CCTexture2D();
    {
        auto data = malloc(width * height * 3);
        memset(data, 0, width * height * 3);
        texture->initWithData(data, kCCTexture2DPixelFormat_RGB888, width, height,
            CCSize(static_cast<float>(width), static_cast<float>(height)));
        free(data);
    }

    glGetIntegerv(GL_RENDERBUFFER_BINDING_EXT, &old_rbo);

    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
        GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, texture->getName(), 0);

    texture->setAliasTexParameters();

    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, old_rbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, old_fbo);
#endif
}

void RenderTexture::end() {
#ifdef GEODE_IS_WINDOWS
    if (fbo) {
        glDeleteFramebuffersEXT(1, &fbo);
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

#ifdef GEODE_IS_WINDOWS
    auto frame = frameCapture.createFrameBuffer();
    if (frame.empty()) return;

    glViewport(-1, 1, width, height);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &old_fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

    pl->visit();

    if (overlay) {
        overlay->visit();
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, frame.data());
    if (!frameCapture.submit(std::move(frame))) {
        log::warn("Render frame capture aborted before frame submission completed");
    }

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, old_fbo);
    director->setViewport();
#endif
}

void Renderer::captureFrame() {
    if (!recording) {
        return;
    }
    renderTex.capture(frameCapture, watermarkLabel);
}

void Renderer::changeRes(bool og) {
    cocos2d::CCEGLView* view = cocos2d::CCEGLView::get();
    cocos2d::CCSize res = {0, 0};
    float scaleX = 1.f;
    float scaleY = 1.f;

    res = og ? ogRes : CCSize(320.f * (width / static_cast<float>(height)), 320.f);
    scaleX = og ? ogScaleX : (width / res.width);
    scaleY = og ? ogScaleY : (height / res.height);

    if (res == CCSize(0, 0) && !og) return changeRes(true);

    CCDirector::sharedDirector()->m_obWinSizeInPoints = res;
    view->setDesignResolutionSize(res.width, res.height, ResolutionPolicy::kResolutionExactFit);
    view->m_fScaleX = scaleX;
    view->m_fScaleY = scaleY;
}

bool Renderer::shouldUseAPI() {
#ifdef GEODE_IS_WINDOWS
    bool foundApi = Loader::get()->isModLoaded("eclipse.ffmpeg-api");
    return foundApi;
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

    bool foundApi = Loader::get()->isModLoaded("eclipse.ffmpeg-api");
    engine->renderer.usingApi = shouldUseAPI();

    std::filesystem::path exePath = Mod::get()->getSettingValue<std::filesystem::path>("ffmpeg_path");
#ifdef GEODE_IS_WINDOWS
    std::filesystem::path resolvedFfmpegPath;
    bool foundExe = false;
    if (!foundApi) {
        resolvedFfmpegPath = resolveUsableFfmpegExecutable(exePath);
        foundExe = !resolvedFfmpegPath.empty();
    }
#endif

#ifdef GEODE_IS_WINDOWS
    if (!foundExe && !foundApi) {
        auto popupTitle = trString("Error");
        auto popupMessage = trString("<cl>FFmpeg</c> not found. Set the path to ffmpeg.exe in mod settings or install FFmpeg API.\nOpen download link?");
        auto cancelText = trString("Cancel");
        auto yesText = trString("Yes");
        geode::createQuickPopup(
            popupTitle.c_str(),
            popupMessage.c_str(),
            cancelText.c_str(), yesText.c_str(),
            [](auto, bool btn2) {
                if (btn2) {
                    auto infoTitle = trString("Info");
                    auto infoMessage = trString("Unzip the downloaded file and look for <cl>ffmpeg.exe</c> in the 'bin' folder.");
                    auto okText = trString("Ok");
                    FLAlertLayer::create(infoTitle.c_str(), infoMessage.c_str(), okText.c_str())->show();
                    utils::web::openLinkInBrowser("https://www.gyan.dev/ffmpeg/builds/ffmpeg-git-essentials.7z");
                }
            }
        );
        return false;
    }
    engine->renderer.ffmpegPath = resolvedFfmpegPath;
#else
    if (!foundApi) {
        auto errorTitle = trString("Error");
        auto errorMessage = trString("<cl>FFmpeg API</c> not found. Download it to render a level.");
        auto okText = trString("Ok");
        FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
        return false;
    }
#endif

    if (!PlayLayer::get()) {
        auto warningTitle = trString("Warning");
        auto warningMessage = trString("<cl>Open a level</c> to start rendering.");
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
            auto errorMessage = trString("Could not create renders folder.");
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
    Mod* mod = Mod::get();
    fmod = FMODAudioEngine::sharedEngine();

    fps = static_cast<unsigned>(mod->getSavedValue<int64_t>("render_fps", 60));
    codec = mod->getSavedValue<std::string>("render_codec", "");
    if (codec.empty()) codec = "libx264";
    bitrate = mod->getSavedValue<std::string>("render_bitrate", "30") + "M";
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
    levelFinished = false;
    timeAfter = 0.f;
    finishFrame = 0;
    levelStartFrame = 0;
    lastFrame_t = extra_t = 0;

    encodeSession.reset();
    audioCapture.reset();
    frameCapture.configure(static_cast<size_t>(width) * static_cast<size_t>(height) * 3, 8);
    renderedFrames.clear();
    renderTex.begin();
    changeRes(false);
    resetSimulationTimingState();

    if (watermarkLabel) {
        watermarkLabel->release();
        watermarkLabel = nullptr;
    }
    {
        auto winSize = cocos2d::CCDirector::sharedDirector()->getWinSize();
        std::string wmText = fmt::format("ToastyReplay v{}", MOD_VERSION);
        watermarkLabel = cocos2d::CCLabelBMFont::create(wmText.c_str(), "chatFont.fnt");
        watermarkLabel->setPosition(ccp(winSize.width / 2.0f, 8.0f));
        watermarkLabel->setScale(0.58f);
        watermarkLabel->setOpacity(150);
        watermarkLabel->retain();
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
        mod->getSavedValue<std::string>("render_bitrate", "30")).unwrapOr(30) * 1000000;

    if (includeClickSounds) {
        int systemRate = 44100;
        fmod->m_system->getSoftwareFormat(&systemRate, nullptr, nullptr);
        ClickSoundManager::get()->preDecodeForRender(systemRate);
    }

    std::thread([this, songFile, songOffset, fadeIn, fadeOut, extension, bitrateApi]() {
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
            std::vector<std::string> fallbacks = {"libx264", "libx265", "h264", "h264_mf", "mpeg4", "libvpx-vp9"};
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
                        Notification::create(trString("FFmpeg API unavailable, using ffmpeg.exe"), NotificationIcon::Warning)->show();
                    });
                } else {
#endif
                    Loader::get()->queueInMainThread([this] {
                        auto errorTitle = trString("Error");
                        auto errorMessage = trString("FFmpeg API failed to initialize.");
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
                if (useApiForEncoding) {
                    auto res = recorder.writeFrame(frame);
                    if (res.isErr()) {
                        Loader::get()->queueInMainThread([this] {
                            auto errorTitle = trString("Error");
                            auto errorMessage = trString("FFmpeg API failed to write frame.");
                            auto okText = trString("Ok");
                            FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                            stop();
                        });
                        audioMode = AUDIO_OFF;
                        recording = false;
                        break;
                    }
                }
#ifdef GEODE_IS_WINDOWS
                else {
                    process.writeStdin(frame.data(), frame.size());
                }
#endif
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        if (useApiForEncoding) {
            recorder.stop();
        } else {
#ifdef GEODE_IS_WINDOWS
            if (process.close()) {
                Loader::get()->queueInMainThread([] {
                    auto errorTitle = trString("Error");
                    auto errorMessage = trString("There was an error saving the render.");
                    auto okText = trString("Ok");
                    FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                });
                return;
            }
#endif
        }
        } 

        bool preferExeAudioMux = false;
#ifdef GEODE_IS_WINDOWS
        preferExeAudioMux = pathExists(ffmpegPath);
#endif

        bool needMixedAudio = includeClickSounds || (!preferExeAudioMux && musicVolume != 1.0f);

        audioCapture.rawMixBuffer.clear();
        auto& rawAudio = audioCapture.rawMixBuffer;
        if (needMixedAudio && audioMode == AUDIO_SONG && pathExists(songFile)) {
            rawAudio = decodeSongToRaw(songFile, songOffset, lastFrame_t, musicVolume);
        }

        audioCapture.sampleRate = 44100;
        int& rawAudioSampleRate = audioCapture.sampleRate;
        if (FMODAudioEngine::sharedEngine() && FMODAudioEngine::sharedEngine()->m_system) {
            FMODAudioEngine::sharedEngine()->m_system->getSoftwareFormat(&rawAudioSampleRate, nullptr, nullptr);
        }

        if (includeClickSounds) {
            auto* csm = ClickSoundManager::get();
            auto* engine = ReplayEngine::get();

            std::vector<MacroAction> ttrConvertedActions;
            bool hasInputs = false;
            std::vector<MacroAction>* inputsPtr = nullptr;
            double macroFramerate = 240.0;

            if (engine->ttrMode && engine->activeTTR && !engine->activeTTR->inputs.empty()) {
                ttrConvertedActions = engine->activeTTR->toMacroActions();
                inputsPtr = &ttrConvertedActions;
                macroFramerate = engine->activeTTR->framerate;
                hasInputs = true;
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
        }

        Loader::get()->queueInMainThread([] {
            Notification::create(trString("Saving Render..."), NotificationIcon::Loading)->show();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        bool hasRawAudio = !rawAudio.empty();
        bool hasAudio = hasRawAudio || (audioMode == AUDIO_SONG && (musicVolume > 0.f && pathExists(songFile)));
        log::info("Audio mux: hasRawAudio={}, rawAudio.size={}, hasAudio={}, musicVolume={:.3f}, useApi={}",
            hasRawAudio, rawAudio.size(), hasAudio, musicVolume, useApiForEncoding);
        if (audioMode == AUDIO_SONG && !hasRawAudio && !pathExists(songFile) && !includeClickSounds) {
            log::error("Song file not found and no raw audio captured: {}", toasty::pathToUtf8(songFile));
        }

        if (!hasAudio) {
            Loader::get()->queueInMainThread([this] {
                Notification::create(trString("Render Saved Without Audio"), NotificationIcon::Success)->show();
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
                        auto errorMessage = trString("Failed to write temporary render audio.");
                        auto okText = trString("Ok");
                        FLAlertLayer::create(errorTitle.c_str(), errorMessage.c_str(), okText.c_str())->show();
                    });
                    return;
                }
                audioInput = rawAudioPath;
            } else {
                audioInput = songFile;
                audioOffset = songOffset;
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
                auto errorMessage = trString("FFmpeg failed to add audio.");
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
                Notification::create(trString("Render Saved With Audio"), NotificationIcon::Success)->show();
            });
        } else {
            Loader::get()->queueInMainThread([] {
                Notification::create(trString("Error saving render with audio"), NotificationIcon::Error)->show();
            });
        }

    }).detach();
}

void Renderer::stop(int frame) {
    renderedFrames.clear();
    finishFrame = getCurrentFrame();
    recording = false;
    timeAfter = 0.f;

    if (audioMode != AUDIO_OFF) {
        fmod->m_globalChannel->setVolume(ogSFXVol);
        fmod->m_backgroundMusicChannel->setVolume(ogMusicVol);
    }

    ClickSoundManager::get()->decodedClickCache.clear();

    if (PlayLayer* pl = PlayLayer::get()) {
        if (pl->m_levelEndAnimationStarted) {
            finishFrame = 0;
            levelFinished = true;
        }
    } else {
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
    isPlatformer = pl->m_levelSettings->m_platformerMode;
    if (dontRender) return;

    if (progressLabel) {
        int pct = pl->getCurrentPercentInt();
        auto progressText = trFormat("Rendering... {percent}%", fmt::arg("percent", pct));
        progressLabel->setString(progressText.c_str());
    }

    if (!pl->m_player1 || pl->m_player1->m_isDead) {
        log::info("Stopping render because the player died at frame {}", frame);
        stop(frame);
        return;
    }

    if (renderedFrames.contains(frame) && frame > 10)
        return;

    renderedFrames.insert(frame);

    if (!pl->m_hasCompletedLevel || timeAfter < stopAfter) {
        float dt = 1.f / static_cast<double>(fps);
        float tickDt = 1.f / static_cast<float>(getTPS());

        if (pl->m_hasCompletedLevel) {
            timeAfter += tickDt;
            levelFinished = true;
        }

        double time = static_cast<double>(pl->m_gameState.m_levelTime) + extra_t - lastFrame_t;
        if (time >= dt) {
            int correctMusicTime = static_cast<int>((frame / getTPS()
                + pl->m_levelSettings->m_songOffset) * 1000);
            correctMusicTime += fmod->m_musicOffset;

            if (fmod->getMusicTimeMS(0) - correctMusicTime >= 110)
                fmod->setMusicTimeMS(correctMusicTime, true, 0);

            while (time >= dt) {
                captureFrame();
                time -= dt;
            }
            extra_t = time;
            lastFrame_t = pl->m_gameState.m_levelTime;
        }
    } else {
        stop(frame);
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
        if (!engine->renderer.recording) return;

        if (m_levelEndAnimationStarted &&
            Mod::get()->getSavedValue<bool>("render_hide_levelcomplete", false)) {
            for (CCNode* node : CCArrayExt<CCNode*>(getChildren())) {
                CCSprite* spr = typeinfo_cast<CCSprite*>(node);
                if (!spr) continue;
                if (!isSpriteFrameName(spr, "GJ_levelComplete_001.png")) continue;
                spr->setVisible(false);
            }
        }
    }
};

class $modify(RenderEndLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!PlayLayer::get()) return;
        auto* engine = ReplayEngine::get();
        if (engine->renderer.recording && PlayLayer::get()->m_levelEndAnimationStarted &&
            Mod::get()->getSavedValue<bool>("render_hide_endscreen", false)) {
            Loader::get()->queueInMainThread([this] {
                setVisible(false);
            });
        }
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
