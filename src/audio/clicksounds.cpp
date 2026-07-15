#include "clicksounds.hpp"
#include "audio/click_audio_math.hpp"
#include "format/replay.hpp"
#include "utils.hpp"
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

    bool isLeftRightClick(int button) {
        return button == static_cast<int>(PlayerButton::Left) ||
            button == static_cast<int>(PlayerButton::Right);
    }

    int buttonStateIndex(int button) {
        if (button == static_cast<int>(PlayerButton::Left)) return 1;
        if (button == static_cast<int>(PlayerButton::Right)) return 2;
        return 0;
    }
}

ClickSoundManager* ClickSoundManager::get() {
    static ClickSoundManager* instance = new ClickSoundManager();
    return instance;
}

bool ClickSoundManager::shouldUseP2Pack(bool requestedPlayer2, bool trueTwoPlayerMode) const {
    static_cast<void>(trueTwoPlayerMode);
    return toasty::clickaudio::shouldUseSecondaryPack(separateP2Clicks, requestedPlayer2, !p2Pack.empty());
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
            availablePacks.push_back(toasty::pathToUtf8(entry.path().filename()));
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
            availablePacksP2.push_back(toasty::pathToUtf8(entry.path().filename()));
        }
    }
    std::sort(availablePacksP2.begin(), availablePacksP2.end());
}

static bool isSupportedAudio(const std::filesystem::path& p) {
    auto ext = toasty::pathToUtf8(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".wav" || ext == ".ogg";
}

struct ResolvedClickSound {
    std::string file;
    float volume = 0.0f;
    float pitchJitter = 0.0f;
    float panOffset = 0.0f;
    bool wasHardChoice = false;
    int pickedIndex = -1;
    int pickedCategory = 0;
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
static int pickRandomIndexAvoiding(int size, int avoid, Rng& rng) {
    if (size <= 0) return -1;
    if (size == 1) return 0;
    std::uniform_int_distribution<int> dist(0, size - 1);
    for (int attempt = 0; attempt < 4; ++attempt) {
        int idx = dist(rng);
        if (idx != avoid) return idx;
    }
    return dist(rng);
}

template <class Rng>
static std::string pickRandomFileWithRng(const std::vector<std::string>& files, Rng& rng) {
    if (files.empty()) return "";
    std::uniform_int_distribution<size_t> dist(0, files.size() - 1);
    return files[dist(rng)];
}

template <class Rng>
static float skewedLowFloat(float lo, float hi, Rng& rng) {
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    float u = uni(rng);
    float curved = u * u * (1.0f + 0.4f * uni(rng));
    return lo + (hi - lo) * std::clamp(curved, 0.0f, 1.0f);
}

template <class Rng>
static float symmetricJitter(float magnitude, Rng& rng) {
    if (magnitude <= 0.0f) return 0.0f;
    std::uniform_real_distribution<float> uni(-magnitude, magnitude);
    return uni(rng);
}

template <class Rng>
static ResolvedClickSound resolveClickSound(
    const ClickPack& pack,
    bool pressed,
    float softness,
    Rng& rng,
    ClickPlayerState& state
) {
    ResolvedClickSound result;

    constexpr float kVolumeJitter = 0.12f;
    constexpr float kPitchJitter = 0.025f;
    constexpr float kPanJitter = 0.10f;

    auto applyCommonJitter = [&](float baseVolume, float intensityScale) {
        float volJitter = 1.0f + symmetricJitter(kVolumeJitter, rng);
        result.volume = std::clamp(toasty::clickaudio::volumeGain(baseVolume) * intensityScale * volJitter, 0.0f, 4.0f);
        result.pitchJitter = symmetricJitter(kPitchJitter, rng);
        result.panOffset = symmetricJitter(kPanJitter, rng);
    };

    if (pressed) {
        std::uniform_real_distribution<float> softDist(0.0f, 1.0f);
        bool useSoft = !pack.softClicks.empty() && (pack.hardClicks.empty() || softDist(rng) < softness);

        if (useSoft) {
            int idx = pickRandomIndexAvoiding(static_cast<int>(pack.softClicks.size()), state.lastSoftIdx, rng);
            if (idx < 0) return result;
            result.file = pack.softClicks[idx];
            result.wasHardChoice = false;
            result.pickedIndex = idx;
            result.pickedCategory = 1;
            state.lastSoftIdx = idx;
            state.lastPressWasHard = false;
            float intensity = 0.92f + symmetricJitter(0.08f, rng);
            state.lastPressIntensity = intensity;
            applyCommonJitter(pack.softVolume, intensity);
            return result;
        }

        if (!pack.hardClicks.empty()) {
            int idx = pickRandomIndexAvoiding(static_cast<int>(pack.hardClicks.size()), state.lastHardIdx, rng);
            if (idx < 0) return result;
            result.file = pack.hardClicks[idx];
            result.wasHardChoice = true;
            result.pickedIndex = idx;
            result.pickedCategory = 0;
            state.lastHardIdx = idx;
            state.lastPressWasHard = true;
            float intensity = 1.0f + symmetricJitter(0.10f, rng);
            state.lastPressIntensity = intensity;
            applyCommonJitter(pack.hardVolume, intensity);
            return result;
        }

        return result;
    }

    bool pairedHard = state.lastPressWasHard;
    float releaseIntensity = std::clamp(state.lastPressIntensity * 0.95f, 0.6f, 1.2f);

    if (pairedHard && !pack.hardReleases.empty()) {
        int idx = pickRandomIndexAvoiding(static_cast<int>(pack.hardReleases.size()), state.lastHardReleaseIdx, rng);
        if (idx < 0) return result;
        result.file = pack.hardReleases[idx];
        result.pickedIndex = idx;
        result.pickedCategory = 2;
        state.lastHardReleaseIdx = idx;
        applyCommonJitter(pack.releaseVolume, releaseIntensity);
        return result;
    }

    if (!pairedHard && !pack.softReleases.empty()) {
        int idx = pickRandomIndexAvoiding(static_cast<int>(pack.softReleases.size()), state.lastSoftReleaseIdx, rng);
        if (idx < 0) return result;
        result.file = pack.softReleases[idx];
        result.pickedIndex = idx;
        result.pickedCategory = 3;
        state.lastSoftReleaseIdx = idx;
        applyCommonJitter(pack.releaseVolume, releaseIntensity * 0.92f);
        return result;
    }

    if (!pack.releases.empty()) {
        int idx = pickRandomIndexAvoiding(static_cast<int>(pack.releases.size()), state.lastReleaseIdx, rng);
        if (idx < 0) return result;
        result.file = pack.releases[idx];
        result.pickedIndex = idx;
        result.pickedCategory = 4;
        state.lastReleaseIdx = idx;
        applyCommonJitter(pack.releaseVolume, releaseIntensity);
        return result;
    }

    if (!pack.hardReleases.empty()) {
        int idx = pickRandomIndexAvoiding(static_cast<int>(pack.hardReleases.size()), state.lastHardReleaseIdx, rng);
        if (idx < 0) return result;
        result.file = pack.hardReleases[idx];
        result.pickedIndex = idx;
        result.pickedCategory = 2;
        state.lastHardReleaseIdx = idx;
        applyCommonJitter(pack.releaseVolume, releaseIntensity);
        return result;
    }

    return result;
}

static void scanSubfolder(const std::filesystem::path& dir, const std::vector<std::string>& possibleNames, std::vector<std::string>& out) {
    out.clear();
    for (auto& name : possibleNames) {
        auto sub = dir / name;
        if (!std::filesystem::exists(sub) || !std::filesystem::is_directory(sub)) continue;
        for (auto& entry : std::filesystem::directory_iterator(sub)) {
            if (entry.is_regular_file() && isSupportedAudio(entry.path())) {
                out.push_back(toasty::pathToUtf8(entry.path()));
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
            auto stem = toasty::pathToUtf8(entry.path().stem());
            std::string lowerStem = stem;
            std::transform(lowerStem.begin(), lowerStem.end(), lowerStem.begin(), ::tolower);
            for (auto& name : noiseNames) {
                if (lowerStem == name) {
                    target.noiseFiles.push_back(toasty::pathToUtf8(entry.path()));
                    break;
                }
            }
        }
        std::sort(target.noiseFiles.begin(), target.noiseFiles.end());
    }

    if (target.hardClicks.empty()) {
        for (auto& entry : std::filesystem::directory_iterator(packDir)) {
            if (entry.is_regular_file() && isSupportedAudio(entry.path())) {
                target.hardClicks.push_back(toasty::pathToUtf8(entry.path()));
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
    stopActiveVoices();
    for (auto& [_, s] : soundCache) {
        if (s) s->release();
    }
    soundCache.clear();
}

void ClickSoundManager::stopActiveVoices() {
    std::lock_guard<std::mutex> lock(voiceMutex);
    for (auto const& ticket : activeVoices) {
        if (ticket.channel) ticket.channel->stop();
    }
    activeVoices.clear();
}

void ClickSoundManager::cullExpiredVoices() {
    std::lock_guard<std::mutex> lock(voiceMutex);
    activeVoices.erase(
        std::remove_if(activeVoices.begin(), activeVoices.end(),
            [](VoiceTicket const& ticket) {
                if (!ticket.channel) return true;
                bool isPlaying = false;
                if (ticket.channel->isPlaying(&isPlaying) != FMOD_OK) return true;
                return !isPlaying;
            }),
        activeVoices.end()
    );
}

void ClickSoundManager::enforcePolyphonyLimit() {
    constexpr size_t kMaxConcurrentVoices = 12;
    cullExpiredVoices();
    std::lock_guard<std::mutex> lock(voiceMutex);
    while (activeVoices.size() >= kMaxConcurrentVoices) {
        auto oldest = std::min_element(activeVoices.begin(), activeVoices.end(),
            [](VoiceTicket const& a, VoiceTicket const& b) {
                return a.startedAt < b.startedAt;
            });
        if (oldest == activeVoices.end()) break;
        if (oldest->channel) {
            oldest->channel->stop();
        }
        activeVoices.erase(oldest);
    }
}

void ClickSoundManager::playFile(const std::string& path, float volume, float pitchJitter, float panOffset) {
    if (path.empty() || volume <= 0.0f) return;

    ensureChannelGroup();
    if (!channelGroup) return;

    auto* system = getAudioSystem();
    if (!system) return;

    auto* sound = getCachedSound(path);
    if (!sound) return;

    enforcePolyphonyLimit();

    FMOD::Channel* channel = nullptr;
    if (system->playSound(sound, channelGroup, true, &channel) != FMOD_OK || !channel) return;

    channel->setVolume(volume);

    if (std::abs(pitchJitter) > 0.0001f) {
        float baseFreq = 0.0f;
        sound->getDefaults(&baseFreq, nullptr);
        if (baseFreq > 0.0f) {
            channel->setFrequency(baseFreq * (1.0f + pitchJitter));
        }
    }

    if (std::abs(panOffset) > 0.0001f) {
        channel->setPan(std::clamp(panOffset, -0.5f, 0.5f));
    }

    channel->setPaused(false);

    {
        std::lock_guard<std::mutex> lock(voiceMutex);
        activeVoices.push_back({channel, std::chrono::steady_clock::now()});
    }
}

void ClickSoundManager::playResolvedClick(bool pressed, bool isPlayer2, int button) {
    if (!enabled) return;
    if (muteLeftRightClicks && isLeftRightClick(button)) return;

    auto* playLayer = PlayLayer::get();
    bool trueTwoPlayerMode = false;
    if (playLayer && playLayer->m_levelSettings) {
        trueTwoPlayerMode = playLayer->m_levelSettings->m_twoPlayerMode;
    }

    bool useP2Pack = shouldUseP2Pack(isPlayer2, trueTwoPlayerMode);
    ClickPack& pack = useP2Pack ? p2Pack : p1Pack;
    if (pack.empty()) return;

    int playerIndex = isPlayer2 ? 1 : 0;
    int stateIndex = buttonStateIndex(button);
    auto resolved = resolveClickSound(pack, pressed, softness, rng, playerState[playerIndex][stateIndex]);
    if (!resolved.file.empty() && resolved.volume > 0.0f) {
        playFile(resolved.file, resolved.volume, resolved.pitchJitter, resolved.panOffset);
    }
}

void ClickSoundManager::playClick(bool pressed, bool isPlayer2, int button) {
    if (!enabled) return;

    float delayMs = 0.0f;
    if (clickDelayMax > 0.0f) {
        float lo = std::min(clickDelayMin, clickDelayMax);
        float hi = std::max(clickDelayMin, clickDelayMax);
        delayMs = skewedLowFloat(lo, hi, rng);
    }

    if (delayMs > 0.1f) {
        PendingClick pending;
        pending.pressed = pressed;
        pending.isPlayer2 = isPlayer2;
        pending.button = button;
        pending.playAt = std::chrono::steady_clock::now()
            + std::chrono::microseconds(static_cast<int64_t>(delayMs * 1000.0f));

        std::lock_guard<std::mutex> lock(pendingClickMutex);
        pendingClicks.push_back(pending);
    } else {
        playResolvedClick(pressed, isPlayer2, button);
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

    std::sort(readyClicks.begin(), readyClicks.end(), [](PendingClick const& a, PendingClick const& b) {
        return a.playAt < b.playAt;
    });

    for (const auto& pending : readyClicks) {
        playResolvedClick(pending.pressed, pending.isPlayer2, pending.button);
    }
}

void ClickSoundManager::clearPendingClicks() {
    std::lock_guard<std::mutex> lock(pendingClickMutex);
    pendingClicks.clear();
    for (auto& perPlayer : playerState) {
        for (auto& state : perPlayer) state = {};
    }
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

    std::uniform_int_distribution<size_t> noisePick(0, pack.noiseFiles.size() - 1);
    std::string path = pack.noiseFiles[noisePick(rng)];
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

    geode::log::debug("Preparing click audio: {} actions at {} TPS, {:.3f}s, {} Hz, start tick {}",
        actions.size(), tickRate, duration, sampleRate, startTick);

    if (tickRate <= 0.0f || sampleRate <= 0) return output;

    std::mt19937 renderRng{42};
    ClickPlayerState renderState[2][3];
    int clicksPlaced = 0;
    double firstClickTime = -1.0, lastClickTime = -1.0;

    for (auto& action : actions) {
        if (muteLeftRightClicks && isLeftRightClick(action.button)) {
            continue;
        }

        bool useP2Pack = shouldUseP2Pack(action.player2, trueTwoPlayerMode);
        ClickPack& pack = useP2Pack ? p2Pack : p1Pack;
        int playerIndex = action.player2 ? 1 : 0;
        int stateIndex = buttonStateIndex(action.button);

        auto resolved = resolveClickSound(pack, action.down, softness, renderRng, renderState[playerIndex][stateIndex]);
        std::string file = resolved.file;
        float volume = resolved.volume;

        if (file.empty() || volume <= 0.0f) continue;
        if (action.frame < startTick) continue;

        double timeSec = static_cast<double>(action.frame - startTick) / tickRate;
        if (applyDelay && clickDelayMax > 0.0f) {
            float lo = std::min(clickDelayMin, clickDelayMax);
            float hi = std::max(clickDelayMin, clickDelayMax);
            timeSec += static_cast<double>(skewedLowFloat(lo, hi, renderRng)) / 1000.0;
        }
        size_t sampleOffset = static_cast<size_t>(timeSec * sampleRate) * 2;

        auto cacheIt = decodedClickCache.find(file);
        auto clickSamples = (cacheIt != decodedClickCache.end())
            ? cacheIt->second : decodeClickToRaw(file, sampleRate);
        if (clickSamples.empty()) continue;

        float pan = std::clamp(resolved.panOffset, -0.5f, 0.5f);
        float leftGain = volume * (1.0f - std::max(0.0f, pan));
        float rightGain = volume * (1.0f - std::max(0.0f, -pan));

        double pitch = static_cast<double>(toasty::clickaudio::pitchFactor(resolved.pitchJitter));
        size_t sourceFrames = clickSamples.size() / 2;
        for (size_t outputFrame = 0; sampleOffset + outputFrame * 2 + 1 < totalSamples; ++outputFrame) {
            double sourcePosition = static_cast<double>(outputFrame) * pitch;
            size_t sourceFrame = static_cast<size_t>(sourcePosition);
            if (sourceFrame >= sourceFrames) break;
            size_t nextFrame = std::min(sourceFrame + 1, sourceFrames - 1);
            float fraction = static_cast<float>(sourcePosition - static_cast<double>(sourceFrame));
            float left = clickSamples[sourceFrame * 2] +
                (clickSamples[nextFrame * 2] - clickSamples[sourceFrame * 2]) * fraction;
            float right = clickSamples[sourceFrame * 2 + 1] +
                (clickSamples[nextFrame * 2 + 1] - clickSamples[sourceFrame * 2 + 1]) * fraction;
            output[sampleOffset + outputFrame * 2] += left * leftGain;
            output[sampleOffset + outputFrame * 2 + 1] += right * rightGain;
        }

        clicksPlaced++;
        if (firstClickTime < 0.0) firstClickTime = timeSec;
        lastClickTime = timeSec;
    }

    geode::log::debug("Mixed {} clicks from {:.3f}s to {:.3f}s into {:.3f}s of audio",
        clicksPlaced, firstClickTime, lastClickTime, duration);

    if (backgroundNoiseEnabled && backgroundNoiseVolume > 0.0f && !p1Pack.noiseFiles.empty()) {
        std::uniform_int_distribution<size_t> noisePick(0, p1Pack.noiseFiles.size() - 1);
        std::string const& noiseFile = p1Pack.noiseFiles[noisePick(renderRng)];
        auto cacheIt = decodedClickCache.find(noiseFile);
        auto noiseSamples = (cacheIt != decodedClickCache.end())
            ? cacheIt->second : decodeClickToRaw(noiseFile, sampleRate);
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
