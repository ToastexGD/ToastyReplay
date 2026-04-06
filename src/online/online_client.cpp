#include "online/online_client.hpp"
#include "i18n/localization.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "utils.hpp"

#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/string.hpp>

#include <filesystem>
#include <fmt/format.h>
#include <random>
#include <string_view>
#include <system_error>

using namespace geode::prelude;

namespace {
    constexpr char BASE64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    struct ApiErrorPayload {
        std::string error;
        std::string code;
    };

    std::string trString(std::string_view key) {
        return std::string(toasty::i18n::tr(key));
    }

    template <class... Args>
    std::string trFormat(std::string_view key, Args&&... args) {
        return toasty::i18n::trf(key, std::forward<Args>(args)...);
    }

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

    OnlineClient::BlacklistState parseBlacklistState(std::string_view value) {
        if (value == "macros") {
            return OnlineClient::BlacklistState::Macros;
        }
        if (value == "issues") {
            return OnlineClient::BlacklistState::Issues;
        }
        if (value == "full") {
            return OnlineClient::BlacklistState::Full;
        }
        return OnlineClient::BlacklistState::None;
    }

    OnlineClient::BlacklistState parseBlacklistErrorCode(std::string_view code) {
        if (code == "BLACKLISTED_MACROS") {
            return OnlineClient::BlacklistState::Macros;
        }
        if (code == "BLACKLISTED_ISSUES") {
            return OnlineClient::BlacklistState::Issues;
        }
        if (code == "BLACKLISTED_FULL") {
            return OnlineClient::BlacklistState::Full;
        }
        return OnlineClient::BlacklistState::None;
    }

    bool isAuthErrorCode(std::string_view code) {
        return code == "AUTH_REQUIRED" || code == "AUTH_INVALID";
    }

    ApiErrorPayload parseErrorPayload(web::WebResponse const& res) {
        ApiErrorPayload payload;
        auto json = res.json();
        if (!json.isOk()) {
            return payload;
        }

        auto const& data = json.unwrap();
        if (data.contains("error") && !data["error"].isNull()) {
            payload.error = data["error"].asString().unwrapOr("");
        }
        if (data.contains("code") && !data["code"].isNull()) {
            payload.code = data["code"].asString().unwrapOr("");
        }
        return payload;
    }

    std::string mapErrorCodeToMessage(std::string_view code) {
        if (code == "AUTH_REQUIRED") {
            return trString("Sign in with Discord first (Online tab)");
        }
        if (code == "AUTH_INVALID") {
            return trString("Session expired. Please re-link Discord.");
        }
        if (code == "BLACKLISTED_MACROS") {
            return trString("Your account is restricted from uploading macros.");
        }
        if (code == "BLACKLISTED_ISSUES") {
            return trString("Your account is restricted from submitting issues.");
        }
        if (code == "BLACKLISTED_FULL") {
            return trString("Your account has been restricted.");
        }
        if (code == "RATE_LIMITED") {
            return trString("Too many submissions. Try again later.");
        }
        if (code == "RATE_LIMITED_IP") {
            return trString("Too many requests. Try again later.");
        }
        if (code == "INVALID_INPUT") {
            return trString("Invalid submission data.");
        }
        if (code == "SPAM_DETECTED") {
            return trString("Submission rejected by content filter.");
        }
        if (code == "FILE_TOO_LARGE") {
            return trString("File too large (max 8MB).");
        }
        if (code == "SERVER_ERROR") {
            return trString("Server error. Try again later.");
        }
        return "";
    }

    std::string summarizeErrorResponse(web::WebResponse const& res, std::string_view fallback) {
        auto payload = parseErrorPayload(res);
        if (auto mapped = mapErrorCodeToMessage(payload.code); !mapped.empty()) {
            return mapped;
        }
        if (!payload.error.empty()) {
            return payload.error;
        }
        if (res.code() == 429) {
            return trString("Too many submissions. Try again later.");
        }
        std::string message = trFormat(
            "{fallback} (HTTP {code})",
            fmt::arg("fallback", toasty::i18n::tr(fallback)),
            fmt::arg("code", res.code())
        );
        if (res.code() >= 500) {
            message += " " + trString("Backend server error.");
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
    geode::async::TaskHolder<web::WebResponse> statusTask;
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
    client->m_impl->statusTask.cancel();

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
    client->blacklistState = BlacklistState::None;
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

bool OnlineClient::canUploadMacros() const {
    return hasValidSession() &&
        blacklistState != BlacklistState::Macros &&
        blacklistState != BlacklistState::Full;
}

bool OnlineClient::canSubmitIssues() const {
    return hasValidSession() &&
        blacklistState != BlacklistState::Issues &&
        blacklistState != BlacklistState::Full;
}

std::string OnlineClient::getRestrictionMessage(bool forUpload) const {
    if (!hasValidSession()) {
        return "";
    }

    switch (blacklistState) {
        case BlacklistState::Macros:
            return forUpload ? trString("Your account is restricted from uploading macros.") : "";
        case BlacklistState::Issues:
            return forUpload ? "" : trString("Your account is restricted from submitting issues.");
        case BlacklistState::Full:
            return trString("Your account has been restricted.");
        case BlacklistState::None:
        default:
            return "";
    }
}

std::string OnlineClient::getBlacklistStatusText() const {
    switch (blacklistState) {
        case BlacklistState::Macros:
            return trString("Restriction: macro uploads are disabled for this account.");
        case BlacklistState::Issues:
            return trString("Restriction: issue submissions are disabled for this account.");
        case BlacklistState::Full:
            return trString("Restriction: this account is fully restricted.");
        case BlacklistState::None:
        default:
            return "";
    }
}

void OnlineClient::clearAuthState(bool cancelTasks) {
    authPolling = false;
    authPollTimer = 0.0f;

    if (cancelTasks) {
        m_impl->authTask.cancel();
        m_impl->statusTask.cancel();
    }
    m_impl->avatarTask.cancel();

    discordUsername.clear();
    discordId.clear();
    discordAvatar.clear();
    sessionCode.clear();
    blacklistState = BlacklistState::None;

    avatarLoading = false;
    avatarLoaded = false;
    releaseAvatarTexture();
    save();
}

void OnlineClient::setLinkedState(
    std::string const& username,
    std::string const& id,
    std::string const& avatar,
    BlacklistState blacklist
) {
    bool avatarChanged = discordId != id || discordAvatar != avatar;

    discordUsername = username;
    discordId = id;
    discordAvatar = avatar;
    blacklistState = blacklist;
    save();

    if (avatarChanged) {
        m_impl->avatarTask.cancel();
        avatarLoading = false;
        avatarLoaded = false;
        releaseAvatarTexture();
    }

    if ((avatarChanged || !avatarLoaded) && !avatarLoading) {
        fetchAvatar();
    }
}

bool OnlineClient::handleAuthStatusResponse(web::WebResponse const& res, bool clearOnUnlinked) {
    if (!res.ok()) {
        auto payload = parseErrorPayload(res);
        if (isAuthErrorCode(payload.code)) {
            clearAuthState(false);
        }
        return false;
    }

    auto json = res.json();
    if (!json.isOk()) {
        return false;
    }

    auto const& data = json.unwrap();
    auto status = data["status"].asString().unwrapOr("");
    if (status != "linked") {
        if (clearOnUnlinked) {
            clearAuthState(false);
        }
        return false;
    }

    auto blacklist = BlacklistState::None;
    if (data.contains("blacklist") && !data["blacklist"].isNull()) {
        blacklist = parseBlacklistState(data["blacklist"].asString().unwrapOr(""));
    }

    setLinkedState(
        data["discord_username"].asString().unwrapOr(""),
        data["discord_id"].asString().unwrapOr(""),
        data["discord_avatar"].asString().unwrapOr(""),
        blacklist
    );
    return true;
}

void OnlineClient::load() {
    alive = true;

    auto* mod = Mod::get();
    sessionCode = mod->getSavedValue<std::string>("online_session_code", "");
    discordUsername = mod->getSavedValue<std::string>("online_discord_username", "");
    discordId = mod->getSavedValue<std::string>("online_discord_id", "");
    discordAvatar = mod->getSavedValue<std::string>("online_discord_avatar", "");
    blacklistState = BlacklistState::None;
    mod->setSavedValue<std::string>("online_api_base", "");

    if (isLinked() && !avatarLoaded && !avatarLoading) {
        fetchAvatar();
    }

    refreshAuthStatus();
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

    if (auto id = toasty::parseInteger<uint64_t>(discordId)) {
        int index = static_cast<int>(((*id) >> 22) % 6);
        return "https://cdn.discordapp.com/embed/avatars/" + std::to_string(index) + ".png";
    }

    log::warn("Invalid Discord ID stored for avatar lookup: {}", discordId);
    return "";
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
    if (!hasValidSession()) return;
    if (auto restriction = getRestrictionMessage(false); !restriction.empty()) {
        issueState = RSERROR;
        issueResultMsg = restriction;
        issueResultTimer = 5.0f;
        return;
    }

    issueState = PENDING;
    issueResultMsg = trString("Submitting...");

    web::WebRequest req;
    req.header("Content-Type", "application/json");

    matjson::Value body;
    body["title"] = title;
    body["description"] = description;
    body["session_code"] = sessionCode;
    req.bodyJSON(body);

    m_impl->issueTask.spawn("ToastyReplay Submit Issue", req.post(getApiBase() + "/submit-issue"), [this](web::WebResponse res) {
        if (!alive) return;

        if (res.ok()) {
            issueState = SUCCESS;
            issueResultMsg = trString("Issue submitted successfully!");
        } else {
            auto payload = parseErrorPayload(res);
            if (isAuthErrorCode(payload.code)) {
                clearAuthState(false);
            }
            if (auto blacklist = parseBlacklistErrorCode(payload.code); blacklist != BlacklistState::None) {
                blacklistState = blacklist;
            }
            issueState = RSERROR;
            issueResultMsg = summarizeErrorResponse(res, "Issue submission failed");
        }

        issueResultTimer = 5.0f;
    });
}

void OnlineClient::uploadMacro(std::string const& macroName, std::string const& comment) {
    if (uploadState == PENDING) return;
    if (!hasValidSession()) return;
    if (auto restriction = getRestrictionMessage(true); !restriction.empty()) {
        uploadState = RSERROR;
        uploadResultMsg = restriction;
        uploadResultTimer = 5.0f;
        return;
    }

    uploadState = PENDING;
    uploadResultMsg = trString("Uploading...");

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
            uploadResultMsg = trString("Failed to load macro file.");
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
            uploadResultMsg = trString("Failed to load macro file.");
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
    body["session_code"] = sessionCode;

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
            uploadResultMsg = trString("Macro uploaded successfully!");
        } else {
            auto payload = parseErrorPayload(res);
            if (isAuthErrorCode(payload.code)) {
                clearAuthState(false);
            }
            if (auto blacklist = parseBlacklistErrorCode(payload.code); blacklist != BlacklistState::None) {
                blacklistState = blacklist;
            }
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
    m_impl->authTask.spawn("ToastyReplay Auth Poll", web::WebRequest().get(getApiBase() + "/auth/status?code=" + code), [this, code](web::WebResponse res) {
        if (!alive || !authPolling || sessionCode != code) return;
        if (handleAuthStatusResponse(res, false)) {
            authPolling = false;
            authPollTimer = 0.0f;
        }
    });
}

void OnlineClient::refreshAuthStatus() {
    if (sessionCode.empty() || authPolling || m_impl->statusTask.isPending()) {
        return;
    }

    auto code = sessionCode;
    m_impl->statusTask.spawn("ToastyReplay Auth Refresh", web::WebRequest().get(getApiBase() + "/auth/status?code=" + code), [this, code](web::WebResponse res) {
        if (!alive || sessionCode != code) return;
        handleAuthStatusResponse(res, true);
    });
}

void OnlineClient::stopAuthPolling() {
    authPolling = false;
    authPollTimer = 0.0f;
    m_impl->authTask.cancel();
}

void OnlineClient::unlinkAccount() {
    clearAuthState(true);
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
