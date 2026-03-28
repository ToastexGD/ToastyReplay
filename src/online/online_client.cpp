#include "online/online_client.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"

#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/string.hpp>

#include <filesystem>
#include <random>
#include <system_error>

using namespace geode::prelude;

namespace {
    constexpr char BASE64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string base64Encode(std::vector<uint8_t> const& data) {
        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);

        for (size_t i = 0; i < data.size(); i += 3) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

            out.push_back(BASE64_TABLE[(n >> 18) & 0x3F]);
            out.push_back(BASE64_TABLE[(n >> 12) & 0x3F]);
            out.push_back(i + 1 < data.size() ? BASE64_TABLE[(n >> 6) & 0x3F] : '=');
            out.push_back(i + 2 < data.size() ? BASE64_TABLE[n & 0x3F] : '=');
        }

        return out;
    }

    std::string summarizeErrorResponse(web::WebResponse const& res, std::string_view fallback) {
        if (res.code() == 429) {
            return "Rate limited. Try again later.";
        }

        auto json = res.json();
        if (json.isOk() && json.unwrap().contains("error")) {
            return json.unwrap()["error"].asString().unwrapOr(std::string(fallback));
        }

        std::string message = std::string(fallback) + " (HTTP " + std::to_string(res.code()) + ")";
        if (res.code() >= 500) {
            message += ". Backend server error.";
        }
        return message;
    }

    std::filesystem::path avatarCachePath() {
        return Mod::get()->getSaveDir() / "cache" / "discord-avatar.png";
    }

    std::string avatarCacheKey(std::filesystem::path const& path) {
        return geode::utils::string::pathToString(path);
    }
}

struct OnlineClientImpl {
    geode::async::TaskHolder<web::WebResponse> avatarTask;
    geode::async::TaskHolder<web::WebResponse> issueTask;
    geode::async::TaskHolder<web::WebResponse> uploadTask;
    geode::async::TaskHolder<web::WebResponse> authTask;
};

std::atomic<bool> OnlineClient::alive{true};

OnlineClient::OnlineClient() : m_impl(std::make_unique<OnlineClientImpl>()) {}
OnlineClient::~OnlineClient() = default;

void OnlineClient::releaseAvatarTexture() {
    if (!avatarTexture) return;

    avatarTexture->release();
    avatarTexture = nullptr;
}

void OnlineClient::shutdown() {
    auto* client = get();
    if (!alive.exchange(false)) return;

    client->m_impl->avatarTask.cancel();
    client->m_impl->issueTask.cancel();
    client->m_impl->uploadTask.cancel();
    client->m_impl->authTask.cancel();

    client->authPolling = false;
    client->authPollTimer = 0.0f;

    client->issueState = IDLE;
    client->issueResultMsg.clear();
    client->issueResultTimer = 0.0f;

    client->uploadState = IDLE;
    client->uploadResultMsg.clear();
    client->uploadResultTimer = 0.0f;

    client->avatarLoading = false;
    client->avatarLoaded = false;
    client->releaseAvatarTexture();
}

$on_mod(Loaded) {
    GameEvent(GameEventType::Exiting).listen([] {
        OnlineClient::shutdown();
        ClickSoundManager::get()->shutdown();
    }, 50).leak();
}

void OnlineClient::generateSessionCode() {
    static constexpr char HEX[] = "0123456789abcdef";

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);

    sessionCode.clear();
    sessionCode.reserve(32);
    for (int i = 0; i < 32; i++) {
        sessionCode.push_back(HEX[dist(rng)]);
    }
}

void OnlineClient::save() {
    auto* mod = Mod::get();
    mod->setSavedValue<std::string>("online_session_code", sessionCode);
    mod->setSavedValue<std::string>("online_discord_username", discordUsername);
    mod->setSavedValue<std::string>("online_discord_id", discordId);
    mod->setSavedValue<std::string>("online_discord_avatar", discordAvatar);
}

void OnlineClient::load() {
    alive = true;

    auto* mod = Mod::get();
    sessionCode = mod->getSavedValue<std::string>("online_session_code", "");
    discordUsername = mod->getSavedValue<std::string>("online_discord_username", "");
    discordId = mod->getSavedValue<std::string>("online_discord_id", "");
    discordAvatar = mod->getSavedValue<std::string>("online_discord_avatar", "");
    mod->setSavedValue<std::string>("online_api_base", "");

    if (isLinked() && !avatarLoaded && !avatarLoading) {
        fetchAvatar();
    }
}

std::string OnlineClient::getApiBase() const {
    return DEFAULT_API_BASE;
}

std::string OnlineClient::getAvatarUrl() const {
    if (!discordAvatar.empty() && !discordId.empty()) {
        return "https://cdn.discordapp.com/avatars/" + discordId + "/" + discordAvatar + ".png?size=128";
    }

    if (discordId.empty()) {
        return "";
    }

    try {
        uint64_t id = std::stoull(discordId);
        int index = static_cast<int>((id >> 22) % 6);
        return "https://cdn.discordapp.com/embed/avatars/" + std::to_string(index) + ".png";
    } catch (...) {
        log::warn("Invalid Discord ID stored for avatar lookup: {}", discordId);
        return "";
    }
}

void OnlineClient::fetchAvatar() {
    if (avatarLoading) return;

    auto url = getAvatarUrl();
    if (url.empty()) {
        avatarLoading = false;
        avatarLoaded = false;
        releaseAvatarTexture();
        return;
    }

    avatarLoading = true;

    m_impl->avatarTask.spawn("ToastyReplay Avatar", web::WebRequest().get(url), [this](web::WebResponse res) {
        avatarLoading = false;
        if (!alive) return;

        if (!res.ok()) {
            avatarLoaded = false;
            releaseAvatarTexture();
            return;
        }

        auto cachePath = avatarCachePath();
        std::error_code ec;
        std::filesystem::create_directories(cachePath.parent_path(), ec);
        if (ec) {
            avatarLoaded = false;
            releaseAvatarTexture();
            return;
        }

        auto writeResult = res.into(cachePath);
        if (!writeResult) {
            avatarLoaded = false;
            releaseAvatarTexture();
            return;
        }

        auto cacheKey = avatarCacheKey(cachePath);
        auto* cache = cocos2d::CCTextureCache::get();
        cache->removeTextureForKey(cacheKey.c_str());

        auto* texture = cache->addImage(cacheKey.c_str(), false);
        if (!texture) {
            avatarLoaded = false;
            releaseAvatarTexture();
            return;
        }

        texture->retain();
        releaseAvatarTexture();
        avatarTexture = texture;
        avatarLoaded = true;
    });
}

void OnlineClient::submitIssue(std::string const& title, std::string const& description) {
    if (issueState == PENDING) return;

    issueState = PENDING;
    issueResultMsg = "Submitting...";

    web::WebRequest req;
    req.header("Content-Type", "application/json");

    matjson::Value body;
    body["title"] = title;
    body["description"] = description;
    if (!sessionCode.empty()) {
        body["session_code"] = sessionCode;
    }
    req.bodyJSON(body);

    m_impl->issueTask.spawn("ToastyReplay Submit Issue", req.post(getApiBase() + "/submit-issue"), [this](web::WebResponse res) {
        if (!alive) return;

        if (res.ok()) {
            issueState = SUCCESS;
            issueResultMsg = "Issue submitted successfully!";
        } else {
            issueState = RSERROR;
            issueResultMsg = summarizeErrorResponse(res, "Issue submission failed");
        }

        issueResultTimer = 5.0f;
    });
}

void OnlineClient::uploadMacro(std::string const& macroName, std::string const& comment) {
    if (uploadState == PENDING) return;

    uploadState = PENDING;
    uploadResultMsg = "Uploading...";

    auto* engine = ReplayEngine::get();
    bool isTTR = engine->ttrMacros.count(macroName) > 0;

    std::string levelName;
    int levelId = 0;
    double tps = 240.0;
    int actionCount = 0;
    int frameCount = 0;
    double durationSeconds = 0.0;
    std::vector<uint8_t> fileData;
    std::string filename;

    if (isTTR) {
        auto* macro = TTRMacro::loadFromDisk(macroName);
        if (!macro) {
            uploadState = RSERROR;
            uploadResultMsg = "Failed to load macro file.";
            uploadResultTimer = 5.0f;
            return;
        }

        levelName = macro->levelName;
        levelId = macro->levelId;
        tps = macro->framerate;
        actionCount = static_cast<int>(macro->inputs.size());
        frameCount = macro->inputs.empty() ? 0 : macro->inputs.back().tick;
        durationSeconds = macro->duration;
        fileData = macro->serialize();
        filename = macroName + ".ttr";
        delete macro;
    } else {
        auto* macro = MacroSequence::loadFromDisk(macroName);
        if (!macro) {
            uploadState = RSERROR;
            uploadResultMsg = "Failed to load macro file.";
            uploadResultTimer = 5.0f;
            return;
        }

        levelName = macro->levelInfo.name;
        levelId = macro->levelInfo.id;
        tps = macro->framerate;
        actionCount = static_cast<int>(macro->inputs.size());
        frameCount = macro->inputs.empty() ? 0 : macro->inputs.back().frame;
        durationSeconds = macro->duration;
        fileData = macro->exportData(false);
        filename = macroName + ".gdr";
        delete macro;
    }

    if (levelName.empty()) levelName = macroName;
    if (tps <= 0.0) tps = 240.0;

    web::WebRequest req;
    req.header("Content-Type", "application/json");

    matjson::Value body;
    body["level_name"] = levelName;
    body["level_id"] = levelId;
    body["tps"] = tps;
    body["action_count"] = actionCount;
    body["frame_count"] = frameCount;
    body["duration_seconds"] = durationSeconds;
    body["filename"] = filename;

    if (!sessionCode.empty()) {
        body["session_code"] = sessionCode;
    }

    auto b64Data = fileData.empty() ? std::string() : base64Encode(fileData);
    if (!b64Data.empty()) {
        body["macro_data"] = b64Data;
    }
    if (!comment.empty()) {
        body["comment"] = comment;
    }

    req.bodyJSON(body);

    m_impl->uploadTask.spawn("ToastyReplay Upload Macro", req.post(getApiBase() + "/upload-macro"), [this](web::WebResponse res) {
        if (!alive) return;

        if (res.ok()) {
            uploadState = SUCCESS;
            uploadResultMsg = "Macro uploaded successfully!";
        } else {
            uploadState = RSERROR;
            uploadResultMsg = summarizeErrorResponse(res, "Macro upload failed");
        }

        uploadResultTimer = 5.0f;
    });
}

void OnlineClient::startAuthFlow() {
    if (sessionCode.empty()) {
        generateSessionCode();
        save();
    }

    auto url = getApiBase() + "/auth/login?code=" + sessionCode;
    geode::utils::web::openLinkInBrowser(url);

    authPolling = true;
    authPollTimer = 0.0f;
}

void OnlineClient::pollAuthStatus() {
    if (sessionCode.empty() || m_impl->authTask.isPending()) {
        return;
    }

    auto code = sessionCode;
    m_impl->authTask.spawn("ToastyReplay Auth Poll", web::WebRequest().get(getApiBase() + "/auth/status?code=" + code), [this](web::WebResponse res) {
        if (!alive || !authPolling) return;
        if (!res.ok()) return;

        auto json = res.json();
        if (!json.isOk()) return;

        auto& data = json.unwrap();
        auto status = data["status"].asString().unwrapOr("");
        if (status != "linked") {
            return;
        }

        discordUsername = data["discord_username"].asString().unwrapOr("");
        discordId = data["discord_id"].asString().unwrapOr("");
        discordAvatar = data["discord_avatar"].asString().unwrapOr("");
        authPolling = false;
        authPollTimer = 0.0f;
        save();
        fetchAvatar();
    });
}

void OnlineClient::stopAuthPolling() {
    authPolling = false;
    authPollTimer = 0.0f;
    m_impl->authTask.cancel();
}

void OnlineClient::unlinkAccount() {
    stopAuthPolling();
    m_impl->avatarTask.cancel();

    discordUsername.clear();
    discordId.clear();
    discordAvatar.clear();
    sessionCode.clear();

    avatarLoading = false;
    avatarLoaded = false;
    releaseAvatarTexture();
    save();
}

void OnlineClient::update(float dt) {
    if (authPolling && !sessionCode.empty()) {
        authPollTimer += dt;
        if (authPollTimer >= AUTH_POLL_INTERVAL) {
            authPollTimer = 0.0f;
            pollAuthStatus();
        }
    }

    if (issueResultTimer > 0.0f) {
        issueResultTimer -= dt;
        if (issueResultTimer <= 0.0f) {
            issueResultTimer = 0.0f;
            issueState = IDLE;
            issueResultMsg.clear();
        }
    }

    if (uploadResultTimer > 0.0f) {
        uploadResultTimer -= dt;
        if (uploadResultTimer <= 0.0f) {
            uploadResultTimer = 0.0f;
            uploadState = IDLE;
            uploadResultMsg.clear();
        }
    }
}
