#include "clicksounds.hpp"
#include "replay.hpp"
#include <Geode/Geode.hpp>
#include <Geode/Bindings.hpp>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <unordered_map>

using namespace geode::prelude;

namespace {
    FMOD::System* getAudioSystem() {
        auto* audio = FMODAudioEngine::sharedEngine();
        return audio ? audio->m_system : nullptr;
    }
}

ClickSoundManager* ClickSoundManager::get() {
    static ClickSoundManager* instance = new ClickSoundManager();
    return instance;
}

bool ClickSoundManager::shouldUseP2Pack(bool requestedPlayer2, bool trueTwoPlayerMode) const {
    return separateP2Clicks && requestedPlayer2 && trueTwoPlayerMode;
}

std::filesystem::path ClickSoundManager::getClicksDir() const {
    return Mod::get()->getSaveDir() / "clicks";
}

std::filesystem::path ClickSoundManager::getClicksP2Dir() const {
    return Mod::get()->getSaveDir() / "clicks_p2";
}

void ClickSoundManager::ensureChannelGroup() {
    if (channelGroup) return;

    auto* system = getAudioSystem();
    if (!system) return;

    system->createChannelGroup("ClickSounds", &channelGroup);
}

void ClickSoundManager::scanClickPacks() {
    availablePacks.clear();
    auto dir = getClicksDir();
    if (!std::filesystem::exists(dir)) return;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_directory()) {
            availablePacks.push_back(entry.path().filename().string());
        }
    }
    std::sort(availablePacks.begin(), availablePacks.end());
}

void ClickSoundManager::scanClickPacksP2() {
    availablePacksP2.clear();
    auto dir = getClicksP2Dir();
    if (!std::filesystem::exists(dir)) return;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_directory()) {
            availablePacksP2.push_back(entry.path().filename().string());
        }
    }
    std::sort(availablePacksP2.begin(), availablePacksP2.end());
}

static bool isSupportedAudio(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".wav" || ext == ".ogg";
}

struct ResolvedClickSound {
    std::string file;
    float volume = 0.0f;
};

static std::vector<float> resampleInterleavedAudio(std::vector<float> const& input, int channels, int sourceRate, int targetRate) {
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

template <class Rng>
static std::string pickRandomFileWithRng(const std::vector<std::string>& files, Rng& rng) {
    if (files.empty()) return "";
    std::uniform_int_distribution<size_t> dist(0, files.size() - 1);
    return files[dist(rng)];
}

template <class Rng>
static ResolvedClickSound resolveClickSound(const ClickPack& pack, bool pressed, float softness, Rng& rng) {
    std::uniform_real_distribution<float> softDist(0.0f, 1.0f);

    if (pressed) {
        bool useSoft = !pack.softClicks.empty() && (pack.hardClicks.empty() || softDist(rng) < softness);
        if (useSoft) {
            return { pickRandomFileWithRng(pack.softClicks, rng), pack.softVolume };
        }
        if (!pack.hardClicks.empty()) {
            return { pickRandomFileWithRng(pack.hardClicks, rng), pack.hardVolume };
        }
        return {};
    }

    bool useSoftRelease = !pack.softReleases.empty() && (pack.hardReleases.empty() || softDist(rng) < softness);
    if (useSoftRelease) {
        return { pickRandomFileWithRng(pack.softReleases, rng), pack.releaseVolume };
    }
    if (!pack.hardReleases.empty()) {
        return { pickRandomFileWithRng(pack.hardReleases, rng), pack.releaseVolume };
    }
    if (!pack.releases.empty()) {
        return { pickRandomFileWithRng(pack.releases, rng), pack.releaseVolume };
    }

    return {};
}

static void scanSubfolder(const std::filesystem::path& dir, const std::vector<std::string>& possibleNames, std::vector<std::string>& out) {
    out.clear();
    for (auto& name : possibleNames) {
        auto sub = dir / name;
        if (!std::filesystem::exists(sub) || !std::filesystem::is_directory(sub)) continue;
        for (auto& entry : std::filesystem::directory_iterator(sub)) {
            if (entry.is_regular_file() && isSupportedAudio(entry.path())) {
                out.push_back(entry.path().string());
            }
        }
        if (!out.empty()) break;
    }
    std::sort(out.begin(), out.end());
}

void ClickSoundManager::loadClickPack(const std::string& packName, ClickPack& target, bool isP2) {
    target.name = packName;
    target.hardClicks.clear();
    target.softClicks.clear();
    target.hardReleases.clear();
    target.softReleases.clear();
    target.releases.clear();
    target.noiseFiles.clear();
    clearSoundCache();

    auto packDir = (isP2 ? getClicksP2Dir() : getClicksDir()) / packName;
    if (!std::filesystem::exists(packDir)) return;

    scanSubfolder(packDir, {"clicks", "hard", "hardclicks", "hardclick", "click"}, target.hardClicks);
    scanSubfolder(packDir, {"softclicks", "soft", "softclick"}, target.softClicks);
    scanSubfolder(packDir, {"hardreleases", "hardrelease"}, target.hardReleases);
    scanSubfolder(packDir, {"softreleases", "softrelease"}, target.softReleases);
    scanSubfolder(packDir, {"releases", "release"}, target.releases);
    scanSubfolder(packDir, {"noise", "background", "bg", "mic"}, target.noiseFiles);

    if (target.noiseFiles.empty()) {
        static const std::vector<std::string> noiseNames = {"noise", "background", "bg", "mic"};
        for (auto& entry : std::filesystem::directory_iterator(packDir)) {
            if (!entry.is_regular_file() || !isSupportedAudio(entry.path())) continue;
            auto stem = entry.path().stem().string();
            std::string lowerStem = stem;
            std::transform(lowerStem.begin(), lowerStem.end(), lowerStem.begin(), ::tolower);
            for (auto& name : noiseNames) {
                if (lowerStem == name) {
                    target.noiseFiles.push_back(entry.path().string());
                    break;
                }
            }
        }
        std::sort(target.noiseFiles.begin(), target.noiseFiles.end());
    }

    if (target.hardClicks.empty()) {
        for (auto& entry : std::filesystem::directory_iterator(packDir)) {
            if (entry.is_regular_file() && isSupportedAudio(entry.path())) {
                target.hardClicks.push_back(entry.path().string());
            }
        }
        std::sort(target.hardClicks.begin(), target.hardClicks.end());
    }
}

std::string ClickSoundManager::pickRandomFile(const std::vector<std::string>& files) {
    return pickRandomFileWithRng(files, rng);
}

FMOD::Sound* ClickSoundManager::getCachedSound(const std::string& path) {
    auto it = soundCache.find(path);
    if (it != soundCache.end()) return it->second;

    auto* system = getAudioSystem();
    if (!system) return nullptr;

    FMOD::Sound* sound = nullptr;
    if (system->createSound(path.c_str(), FMOD_CREATESAMPLE, nullptr, &sound) != FMOD_OK || !sound)
        return nullptr;

    sound->setMode(FMOD_LOOP_OFF);
    soundCache[path] = sound;
    return sound;
}

void ClickSoundManager::clearSoundCache() {
    clearPendingClicks();
    stopBackgroundNoise();
    for (auto& [_, s] : soundCache) {
        if (s) s->release();
    }
    soundCache.clear();
}

void ClickSoundManager::playFile(const std::string& path, float volume) {
    if (path.empty() || volume <= 0.0f) return;

    ensureChannelGroup();
    if (!channelGroup) return;

    auto* system = getAudioSystem();
    if (!system) return;

    auto* sound = getCachedSound(path);
    if (!sound) return;

    FMOD::Channel* channel = nullptr;
    system->playSound(sound, channelGroup, true, &channel);
    if (channel) {
        channel->setVolume(volume);
        channel->setPaused(false);
    }
}

void ClickSoundManager::playResolvedClick(bool pressed, bool isPlayer2) {
    if (!enabled) return;

    auto* playLayer = PlayLayer::get();
    bool trueTwoPlayerMode = false;
    if (playLayer && playLayer->m_levelSettings) {
        trueTwoPlayerMode = playLayer->m_levelSettings->m_twoPlayerMode;
    }

    ClickPack& pack = shouldUseP2Pack(isPlayer2, trueTwoPlayerMode) ? p2Pack : p1Pack;
    if (pack.empty()) return;

    auto resolved = resolveClickSound(pack, pressed, softness, rng);
    if (!resolved.file.empty() && resolved.volume > 0.0f) {
        playFile(resolved.file, resolved.volume);
    }
}

void ClickSoundManager::playClick(bool pressed, bool isPlayer2) {
    if (!enabled) return;

    float delayMs = 0.0f;
    if (clickDelayMax > 0.0f) {
        float lo = std::min(clickDelayMin, clickDelayMax);
        float hi = std::max(clickDelayMin, clickDelayMax);
        std::uniform_real_distribution<float> delayDist(lo, hi);
        delayMs = delayDist(rng);
    }

    if (delayMs > 0.1f) {
        PendingClick pending;
        pending.pressed = pressed;
        pending.isPlayer2 = isPlayer2;
        pending.playAt = std::chrono::steady_clock::now()
            + std::chrono::microseconds(static_cast<int64_t>(delayMs * 1000.0f));

        std::lock_guard<std::mutex> lock(pendingClickMutex);
        pendingClicks.push_back(pending);
    } else {
        playResolvedClick(pressed, isPlayer2);
    }
}

void ClickSoundManager::updatePendingClicks() {
    std::vector<PendingClick> readyClicks;
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(pendingClickMutex);
        auto ready = std::remove_if(pendingClicks.begin(), pendingClicks.end(),
            [&](const PendingClick& pending) {
                if (pending.playAt > now) return false;
                readyClicks.push_back(pending);
                return true;
            });
        pendingClicks.erase(ready, pendingClicks.end());
    }

    for (const auto& pending : readyClicks) {
        playResolvedClick(pending.pressed, pending.isPlayer2);
    }
}

void ClickSoundManager::clearPendingClicks() {
    std::lock_guard<std::mutex> lock(pendingClickMutex);
    pendingClicks.clear();
}

void ClickSoundManager::shutdown() {
    clearSoundCache();
    decodedClickCache.clear();

    if (channelGroup) {
        channelGroup->release();
        channelGroup = nullptr;
    }
}

void ClickSoundManager::startBackgroundNoise() {
    stopBackgroundNoise();
    if (!backgroundNoiseEnabled || backgroundNoiseVolume <= 0.0f) return;

    auto* system = getAudioSystem();
    if (!system) return;

    auto& pack = p1Pack;
    if (pack.noiseFiles.empty()) return;

    ensureChannelGroup();
    if (!channelGroup) return;

    std::string path = pack.noiseFiles[0];
    if (bgNoiseSound) { bgNoiseSound->release(); bgNoiseSound = nullptr; }

    if (system->createSound(path.c_str(), FMOD_CREATESAMPLE | FMOD_LOOP_NORMAL, nullptr, &bgNoiseSound) != FMOD_OK || !bgNoiseSound)
        return;

    bgNoiseSound->setMode(FMOD_LOOP_NORMAL);
    system->playSound(bgNoiseSound, channelGroup, true, &bgNoiseChannel);
    if (bgNoiseChannel) {
        bgNoiseChannel->setVolume(backgroundNoiseVolume);
        bgNoiseChannel->setPaused(false);
    }
}

void ClickSoundManager::stopBackgroundNoise() {
    if (bgNoiseChannel) {
        bgNoiseChannel->stop();
        bgNoiseChannel = nullptr;
    }
    if (bgNoiseSound) {
        bgNoiseSound->release();
        bgNoiseSound = nullptr;
    }
}

void ClickSoundManager::openClickFolder() {
    auto dir = getClicksDir();
    if (!std::filesystem::exists(dir))
        std::filesystem::create_directories(dir);
    geode::utils::file::openFolder(dir);
}

void ClickSoundManager::openClickFolderP2() {
    auto dir = getClicksP2Dir();
    if (!std::filesystem::exists(dir))
        std::filesystem::create_directories(dir);
    geode::utils::file::openFolder(dir);
}

std::vector<float> ClickSoundManager::decodeClickToRaw(const std::string& filePath, int targetSampleRate) {
    std::vector<float> result;
    auto* system = getAudioSystem();
    if (!system) return result;

    FMOD::Sound* sound = nullptr;

    if (system->createSound(filePath.c_str(), FMOD_CREATESAMPLE, nullptr, &sound) != FMOD_OK || !sound)
        return result;

    FMOD_SOUND_FORMAT format;
    int channels, bits;
    sound->getFormat(nullptr, &format, &channels, &bits);

    float freq;
    sound->getDefaults(&freq, nullptr);
    int sampleRate = static_cast<int>(freq);

    if (sampleRate <= 0) {
        unsigned int lengthMs, lengthPcm;
        sound->getLength(&lengthMs, FMOD_TIMEUNIT_MS);
        sound->getLength(&lengthPcm, FMOD_TIMEUNIT_PCM);
        if (lengthMs > 0 && lengthPcm > 0)
            sampleRate = static_cast<int>((static_cast<double>(lengthPcm) * 1000.0) / lengthMs);
    }
    if (sampleRate <= 0) {
        system->getSoftwareFormat(&sampleRate, nullptr, nullptr);
    }

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

    size_t totalSamples = static_cast<size_t>(pcmSamples) * channels;
    std::vector<float> raw(totalSamples);

    if (format == FMOD_SOUND_FORMAT_PCM16) {
        auto* s = static_cast<const int16_t*>(ptr1);
        for (size_t i = 0; i < totalSamples; i++) raw[i] = s[i] / 32768.f;
    } else if (format == FMOD_SOUND_FORMAT_PCMFLOAT) {
        auto* s = static_cast<const float*>(ptr1);
        std::copy(s, s + totalSamples, raw.begin());
    } else if (format == FMOD_SOUND_FORMAT_PCM32) {
        auto* s = static_cast<const int32_t*>(ptr1);
        for (size_t i = 0; i < totalSamples; i++) raw[i] = s[i] / 2147483648.f;
    } else {
        sound->unlock(ptr1, ptr2, len1, len2);
        sound->release();
        return result;
    }

    sound->unlock(ptr1, ptr2, len1, len2);

    if (channels == 1) {
        std::vector<float> stereo(totalSamples * 2);
        for (size_t i = 0; i < totalSamples; i++) {
            stereo[i * 2] = raw[i];
            stereo[i * 2 + 1] = raw[i];
        }
        raw = std::move(stereo);
        channels = 2;
    }

    if (sampleRate != targetSampleRate && targetSampleRate > 0) {
        raw = resampleInterleavedAudio(raw, channels, sampleRate, targetSampleRate);
    }

    sound->release();
    return raw;
}

std::vector<float> ClickSoundManager::generateClickAudio(
    const std::vector<MacroAction>& actions,
    float tickRate, float duration, int sampleRate,
    int startTick, bool applyDelay, bool trueTwoPlayerMode)
{
    size_t totalSamples = static_cast<size_t>(duration * sampleRate) * 2;
    std::vector<float> output(totalSamples, 0.0f);

    geode::log::info("generateClickAudio: tickRate={}, duration={:.3f}s, sampleRate={}, startTick={}, actions={}, totalSamples={}",
        tickRate, duration, sampleRate, startTick, actions.size(), totalSamples);

    if (tickRate <= 0.0f || sampleRate <= 0) return output;

    std::mt19937 renderRng{42};
    int clicksPlaced = 0;
    double firstClickTime = -1.0, lastClickTime = -1.0;

    for (auto& action : actions) {
        ClickPack& pack = shouldUseP2Pack(action.player2, trueTwoPlayerMode) ? p2Pack : p1Pack;

        auto resolved = resolveClickSound(pack, action.down, softness, renderRng);
        std::string file = resolved.file;
        float volume = resolved.volume;

        if (file.empty() || volume <= 0.0f) continue;
        if (action.frame < startTick) continue;

        double timeSec = static_cast<double>(action.frame - startTick) / tickRate;
        if (applyDelay && clickDelayMax > 0.0f) {
            float lo = std::min(clickDelayMin, clickDelayMax);
            float hi = std::max(clickDelayMin, clickDelayMax);
            std::uniform_real_distribution<float> delayDist(lo, hi);
            timeSec += static_cast<double>(delayDist(renderRng)) / 1000.0;
        }
        size_t sampleOffset = static_cast<size_t>(timeSec * sampleRate) * 2;

        auto cacheIt = decodedClickCache.find(file);
        auto clickSamples = (cacheIt != decodedClickCache.end())
            ? cacheIt->second : decodeClickToRaw(file, sampleRate);
        if (clickSamples.empty()) continue;

        for (size_t i = 0; i < clickSamples.size() && (sampleOffset + i) < totalSamples; i++) {
            output[sampleOffset + i] += clickSamples[i] * volume;
        }

        clicksPlaced++;
        if (firstClickTime < 0.0) firstClickTime = timeSec;
        lastClickTime = timeSec;
    }

    geode::log::info("generateClickAudio: placed {} clicks, first={:.3f}s last={:.3f}s (duration={:.3f}s)",
        clicksPlaced, firstClickTime, lastClickTime, duration);

    if (backgroundNoiseEnabled && backgroundNoiseVolume > 0.0f && !p1Pack.noiseFiles.empty()) {
        auto cacheIt = decodedClickCache.find(p1Pack.noiseFiles[0]);
        auto noiseSamples = (cacheIt != decodedClickCache.end())
            ? cacheIt->second : decodeClickToRaw(p1Pack.noiseFiles[0], sampleRate);
        if (!noiseSamples.empty()) {
            for (size_t i = 0; i < totalSamples; i++) {
                output[i] += noiseSamples[i % noiseSamples.size()] * backgroundNoiseVolume;
            }
        }
    }

    for (auto& s : output) {
        s = std::clamp(s, -1.0f, 1.0f);
    }

    return output;
}

void ClickSoundManager::preDecodeForRender(int sampleRate) {
    decodedClickCache.clear();
    auto decodeAll = [&](const std::vector<std::string>& files) {
        for (const auto& f : files) {
            if (decodedClickCache.find(f) == decodedClickCache.end())
                decodedClickCache[f] = decodeClickToRaw(f, sampleRate);
        }
    };
    decodeAll(p1Pack.hardClicks);
    decodeAll(p1Pack.softClicks);
    decodeAll(p1Pack.hardReleases);
    decodeAll(p1Pack.softReleases);
    decodeAll(p1Pack.releases);
    decodeAll(p1Pack.noiseFiles);
    if (separateP2Clicks) {
        decodeAll(p2Pack.hardClicks);
        decodeAll(p2Pack.softClicks);
        decodeAll(p2Pack.hardReleases);
        decodeAll(p2Pack.softReleases);
        decodeAll(p2Pack.releases);
        decodeAll(p2Pack.noiseFiles);
    }
}
