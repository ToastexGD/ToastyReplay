
#include "online/online_client.hpp"
#include "lang/localization.hpp"
#include "ToastyReplay.hpp"
#include "audio/clicksounds.hpp"
#include "utils.hpp"

#include <Geode/loader/GameEvent.hpp>
#include <Geode/utils/base64.hpp>
#include <Geode/utils/string.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <random>
#include <span>
#include <string_view>
#include <system_error>

#ifdef GEODE_IS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

using namespace geode::prelude;

namespace {
    constexpr char BASE64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    constexpr char const* KEY_DISCORD_USERNAME = "online_discord_username";
    constexpr char const* KEY_DISCORD_ID = "online_discord_id";
    constexpr char const* KEY_DISCORD_AVATAR = "online_discord_avatar";
    constexpr char const* KEY_REFRESH_TOKEN = "online_refresh_token_dpapi";
    constexpr char const* KEY_LEGACY_SESSION_CODE = "online_session_code";
    constexpr char const* KEY_LEGACY_API_BASE = "online_api_base";

    struct ApiErrorPayload {
        std::string error;
        std::string code;
    };

    std::string trString(std::string_view key) {
        return std::string(toasty::lang::tr(key));
    }

    template <class... Args>
    std::string trFormat(std::string_view key, Args&&... args) {
        return toasty::lang::trf(key, std::forward<Args>(args)...);
    }

    std::int64_t epochSecondsNow() {
        return static_cast<std::int64_t>(std::time(nullptr));
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

    std::string accuracyModeToString(AccuracyMode mode) {
        switch (mode) {
            case AccuracyMode::CBS: return "CBS";
            case AccuracyMode::CBF: return "CBF";
            case AccuracyMode::Vanilla:
            default:                return "Vanilla";
        }
    }

    OnlineClient::BlacklistState parseBlacklistState(std::string_view value) {
        if (value == "macros") return OnlineClient::BlacklistState::Macros;
        if (value == "issues") return OnlineClient::BlacklistState::Issues;
        if (value == "full")   return OnlineClient::BlacklistState::Full;
        return OnlineClient::BlacklistState::None;
    }

    OnlineClient::BlacklistState parseBlacklistErrorCode(std::string_view code) {
        if (code == "BLACKLISTED_MACROS") return OnlineClient::BlacklistState::Macros;
        if (code == "BLACKLISTED_ISSUES") return OnlineClient::BlacklistState::Issues;
        if (code == "BLACKLISTED_FULL")   return OnlineClient::BlacklistState::Full;
        return OnlineClient::BlacklistState::None;
    }

    bool isAuthErrorCode(std::string_view code) {
        return code == "AUTH_REQUIRED" || code == "AUTH_INVALID";
    }

    ApiErrorPayload parseErrorPayload(web::WebResponse const& res) {
        ApiErrorPayload payload;
        auto json = res.json();
        if (!json.isOk()) return payload;

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
        if (code == "AUTH_REQUIRED")      return trString("Sign in with Discord first (Online tab)");
        if (code == "AUTH_INVALID")       return trString("Session expired. Please re-link Discord.");
        if (code == "BLACKLISTED_MACROS") return trString("Your account is restricted from uploading macros.");
        if (code == "BLACKLISTED_ISSUES") return trString("Your account is restricted from submitting issues.");
        if (code == "BLACKLISTED_FULL")   return trString("Your account has been restricted.");
        if (code == "RATE_LIMITED")       return trString("Too many submissions. Try again later.");
        if (code == "RATE_LIMITED_IP")    return trString("Too many requests. Try again later.");
        if (code == "INVALID_INPUT")      return trString("Invalid submission data.");
        if (code == "SPAM_DETECTED")      return trString("Submission rejected by content filter.");
        if (code == "FILE_TOO_LARGE")     return trString("File too large (max 8MB).");
        if (code == "SERVER_ERROR")       return trString("Server error. Try again later.");
        return "";
    }

    std::string summarizeErrorResponse(web::WebResponse const& res, std::string_view fallback) {
        auto payload = parseErrorPayload(res);
        if (auto mapped = mapErrorCodeToMessage(payload.code); !mapped.empty()) {
            return mapped;
        }
        if (!payload.error.empty()) return payload.error;
        if (res.code() == 429) return trString("Too many submissions. Try again later.");

        std::string message = trFormat(
            "{fallback} (HTTP {code})",
            toasty::lang::arg("fallback", toasty::lang::tr(fallback)),
            toasty::lang::arg("code", res.code())
        );
        if (res.code() >= 500) message += " " + trString("Backend server error.");
        return message;
    }

    std::filesystem::path avatarCachePath() {
        return Mod::get()->getSaveDir() / "cache" / "discord-avatar.png";
    }

    std::string avatarCacheKey(std::filesystem::path const& path) {
        return geode::utils::string::pathToString(path);
    }

#ifdef GEODE_IS_WINDOWS
    bool dpapiProtect(std::string const& plain, std::string& encrypted) {
        DATA_BLOB input{};
        input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
        input.cbData = static_cast<DWORD>(plain.size());

        DATA_BLOB output{};
        if (!CryptProtectData(&input, L"ToastyReplay online refresh token",
                              nullptr, nullptr, nullptr, 0, &output)) {
            return false;
        }

        std::span<std::uint8_t const> bytes(
            reinterpret_cast<std::uint8_t const*>(output.pbData),
            static_cast<size_t>(output.cbData)
        );
        encrypted = geode::utils::base64::encode(bytes, geode::utils::base64::Base64Variant::Normal);
        LocalFree(output.pbData);
        return true;
    }

    bool dpapiUnprotect(std::string const& encrypted, std::string& plain) {
        auto decoded = geode::utils::base64::decode(encrypted, geode::utils::base64::Base64Variant::Normal);
        if (!decoded.isOk()) return false;

        auto bytes = decoded.unwrap();
        DATA_BLOB input{};
        input.pbData = reinterpret_cast<BYTE*>(bytes.data());
        input.cbData = static_cast<DWORD>(bytes.size());

        DATA_BLOB output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
            return false;
        }

        plain.assign(reinterpret_cast<char const*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return true;
    }
#else
    bool dpapiProtect(std::string const& plain, std::string& encrypted) {
        encrypted = geode::utils::base64::encode(plain, geode::utils::base64::Base64Variant::Normal);
        return true;
    }
    bool dpapiUnprotect(std::string const& encrypted, std::string& plain) {
        auto decoded = geode::utils::base64::decodeString(encrypted, geode::utils::base64::Base64Variant::Normal);
        if (!decoded.isOk()) return false;
        plain = decoded.unwrap();
        return true;
    }
#endif
}

struct OnlineClientImpl {
    geode::async::TaskHolder<web::WebResponse> avatarTask;
    geode::async::TaskHolder<web::WebResponse> issueTask;
    geode::async::TaskHolder<web::WebResponse> uploadTask;
    geode::async::TaskHolder<web::WebResponse> authStartTask;
    geode::async::TaskHolder<web::WebResponse> authPollTask;
    geode::async::TaskHolder<web::WebResponse> authRefreshTask;
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
    client->m_impl->authStartTask.cancel();
    client->m_impl->authPollTask.cancel();
    client->m_impl->authRefreshTask.cancel();
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
    client->clearPendingIntent();
}

$on_mod(Loaded) {
    GameEvent(GameEventType::Exiting).listen([] {
        OnlineClient::shutdown();
        ClickSoundManager::get()->shutdown();
    }, 50).leak();
}

void OnlineClient::save() {
    auto* mod = Mod::get();
    mod->setSavedValue<std::string>(KEY_DISCORD_USERNAME, discordUsername);
    mod->setSavedValue<std::string>(KEY_DISCORD_ID, discordId);
    mod->setSavedValue<std::string>(KEY_DISCORD_AVATAR, discordAvatar);
}

void OnlineClient::saveRefreshToken(std::string const& token) {
    if (token.empty()) {
        clearRefreshToken();
        return;
    }
    std::string encrypted;
    if (!dpapiProtect(token, encrypted)) {
        log::warn("Could not protect the online refresh token, so it will not be saved");
        clearRefreshToken();
        return;
    }
    Mod::get()->setSavedValue<std::string>(KEY_REFRESH_TOKEN, encrypted);
}

void OnlineClient::clearRefreshToken() {
    Mod::get()->setSavedValue<std::string>(KEY_REFRESH_TOKEN, "");
}

std::string OnlineClient::loadRefreshToken() const {
    auto encrypted = Mod::get()->getSavedValue<std::string>(KEY_REFRESH_TOKEN, "");
    if (encrypted.empty()) return "";
    std::string plain;
    if (!dpapiUnprotect(encrypted, plain)) {
        log::warn("Could not read the saved online refresh token, so it will be cleared");
        Mod::get()->setSavedValue<std::string>(KEY_REFRESH_TOKEN, "");
        return "";
    }
    return plain;
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
    if (!hasValidSession()) return "";
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
        m_impl->authStartTask.cancel();
        m_impl->authPollTask.cancel();
        m_impl->authRefreshTask.cancel();
        m_impl->statusTask.cancel();
    }
    m_impl->avatarTask.cancel();

    discordUsername.clear();
    discordId.clear();
    discordAvatar.clear();
    sessionCode.clear();
    blacklistState = BlacklistState::None;

    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessTokenExpiresAt = 0;
    m_pollToken.clear();
    m_activationExpiresAt = 0;
    m_refreshInFlight = false;
    clearPendingIntent();

    avatarLoading = false;
    avatarLoaded = false;
    releaseAvatarTexture();
    clearRefreshToken();
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

void OnlineClient::load() {
    alive = true;

    auto* mod = Mod::get();
    discordUsername = mod->getSavedValue<std::string>(KEY_DISCORD_USERNAME, "");
    discordId = mod->getSavedValue<std::string>(KEY_DISCORD_ID, "");
    discordAvatar = mod->getSavedValue<std::string>(KEY_DISCORD_AVATAR, "");
    blacklistState = BlacklistState::None;

    auto legacySession = mod->getSavedValue<std::string>(KEY_LEGACY_SESSION_CODE, "");
    if (!legacySession.empty()) {
        mod->setSavedValue<std::string>(KEY_LEGACY_SESSION_CODE, "");
    }
    mod->setSavedValue<std::string>(KEY_LEGACY_API_BASE, "");

    m_refreshToken = loadRefreshToken();
    sessionCode.clear();
    m_accessToken.clear();
    m_accessTokenExpiresAt = 0;

    if (!m_refreshToken.empty()) {
        refreshAuthStatus();
    }

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
    if (discordId.empty()) return "";

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

static std::string generateDeviceFingerprint() {
    static constexpr char const* KEY = "online_device_fingerprint";
    auto* mod = Mod::get();
    auto existing = mod->getSavedValue<std::string>(KEY, "");
    if (!existing.empty()) return existing;

    static constexpr char HEX[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    std::string out = "dev_";
    out.reserve(4 + 32);
    for (int i = 0; i < 16; ++i) {
        auto v = static_cast<unsigned>(dist(rng));
        out.push_back(HEX[(v >> 4) & 0xF]);
        out.push_back(HEX[v & 0xF]);
    }
    mod->setSavedValue<std::string>(KEY, out);
    return out;
}

void OnlineClient::startAuthFlow() {
    if (authPolling || m_impl->authStartTask.isPending()) return;

    sessionCode.clear();
    m_pollToken.clear();
    m_activationExpiresAt = 0;

    authPolling = true;
    authPollTimer = 0.0f;

    web::WebRequest req;
    req.header("Content-Type", "application/json");
    matjson::Value body;
    body["device_name"] = "ToastyReplay Geode Mod";
    body["device_fingerprint"] = generateDeviceFingerprint();
    req.bodyJSON(body);

    m_impl->authStartTask.spawn(
        "ToastyReplay Auth Start",
        req.post(getApiBase() + "/api/macros/auth/start"),
        [this](web::WebResponse res) {
            if (!alive) return;
            if (!res.ok()) {
                authPolling = false;
                authPollTimer = 0.0f;
                log::error("Could not start online authentication: HTTP {}", res.code());
                return;
            }
            auto json = res.json();
            if (!json.isOk()) {
                authPolling = false;
                return;
            }
            auto const& data = json.unwrap();
            auto code = data.contains("code") ? data["code"].asString().unwrapOr("") : "";
            auto pollToken = data.contains("poll_token") ? data["poll_token"].asString().unwrapOr("") : "";
            auto approveUrl = data.contains("approve_url") ? data["approve_url"].asString().unwrapOr("") : "";
            auto expiresIn = data.contains("expires_in") ? data["expires_in"].asInt().unwrapOr(0) : 0;

            if (code.empty() || pollToken.empty()) {
                authPolling = false;
                return;
            }

            sessionCode = code;
            m_pollToken = pollToken;
            m_activationExpiresAt = epochSecondsNow() + expiresIn;

            auto base = approveUrl.empty() ? getApiBase() + "/macros/approve" : approveUrl;
            auto sep = base.find('?') == std::string::npos ? "?" : "&";
            geode::utils::web::openLinkInBrowser(base + sep + "code=" + code);
        }
    );
}

void OnlineClient::pollAuthStatus() {
    if (m_pollToken.empty() || m_impl->authPollTask.isPending()) return;

    if (m_activationExpiresAt > 0 && epochSecondsNow() > m_activationExpiresAt) {
        authPolling = false;
        authPollTimer = 0.0f;
        m_pollToken.clear();
        sessionCode.clear();
        m_activationExpiresAt = 0;
        return;
    }

    web::WebRequest req;
    req.header("Content-Type", "application/json");
    matjson::Value body;
    body["poll_token"] = m_pollToken;
    req.bodyJSON(body);

    auto expected = m_pollToken;
    m_impl->authPollTask.spawn(
        "ToastyReplay Auth Poll",
        req.post(getApiBase() + "/api/macros/auth/poll"),
        [this, expected](web::WebResponse res) {
            if (!alive || expected != m_pollToken) return;

            if (!res.ok()) {
                auto payload = parseErrorPayload(res);
                if (payload.code == "EXPIRED" || payload.code == "INVALID_POLL" ||
                    payload.code == "ALREADY_CONSUMED") {
                    authPolling = false;
                    authPollTimer = 0.0f;
                    m_pollToken.clear();
                    sessionCode.clear();
                    m_activationExpiresAt = 0;
                }
                return;
            }

            auto json = res.json();
            if (!json.isOk()) return;
            auto const& data = json.unwrap();
            auto status = data.contains("status") ? data["status"].asString().unwrapOr("") : "";

            if (status == "pending") {
                return;
            }
            if (status == "approved") {
                onActivationApproved(data);
            }
        }
    );
}

void OnlineClient::onActivationApproved(matjson::Value const& data) {
    auto access = data.contains("access_token") ? data["access_token"].asString().unwrapOr("") : "";
    auto refresh = data.contains("refresh_token") ? data["refresh_token"].asString().unwrapOr("") : "";
    auto accessExp = data.contains("access_expires_in") ? data["access_expires_in"].asInt().unwrapOr(3600) : 3600;
    auto username = data.contains("discord_username") ? data["discord_username"].asString().unwrapOr("") : "";
    auto id = data.contains("discord_id") ? data["discord_id"].asString().unwrapOr("") : "";
    auto avatar = data.contains("discord_avatar") ? data["discord_avatar"].asString().unwrapOr("") : "";
    auto blacklist = BlacklistState::None;
    if (data.contains("blacklist") && !data["blacklist"].isNull()) {
        blacklist = parseBlacklistState(data["blacklist"].asString().unwrapOr(""));
    }

    if (access.empty() || refresh.empty() || username.empty() || id.empty()) {
        log::error("Could not activate the online session because the response was incomplete");
        return;
    }

    m_accessToken = access;
    m_refreshToken = refresh;
    m_accessTokenExpiresAt = epochSecondsNow() + accessExp;
    saveRefreshToken(refresh);

    authPolling = false;
    authPollTimer = 0.0f;
    m_pollToken.clear();
    sessionCode.clear();
    m_activationExpiresAt = 0;

    setLinkedState(username, id, avatar, blacklist);
}

bool OnlineClient::accessTokenExpired() const {
    if (m_accessToken.empty()) return true;
    return epochSecondsNow() + 60 >= m_accessTokenExpiresAt;
}

void OnlineClient::performAuthRefresh() {
    if (m_refreshInFlight || m_impl->authRefreshTask.isPending()) return;
    if (m_refreshToken.empty()) {
        clearAuthState(false);
        return;
    }
    m_refreshInFlight = true;

    web::WebRequest req;
    req.header("Content-Type", "application/json");
    matjson::Value body;
    body["refresh_token"] = m_refreshToken;
    req.bodyJSON(body);

    m_impl->authRefreshTask.spawn(
        "ToastyReplay Auth Refresh",
        req.post(getApiBase() + "/api/macros/auth/refresh"),
        [this](web::WebResponse res) {
            m_refreshInFlight = false;
            if (!alive) return;

            if (!res.ok()) {
                auto payload = parseErrorPayload(res);
                if (isAuthErrorCode(payload.code) || res.code() == 401 || res.code() == 403) {
                    clearAuthState(false);
                }
                clearPendingIntent();
                return;
            }

            auto json = res.json();
            if (!json.isOk()) {
                clearPendingIntent();
                return;
            }
            onRefreshSuccess(json.unwrap());
        }
    );
}

void OnlineClient::onRefreshSuccess(matjson::Value const& data) {
    auto access = data.contains("access_token") ? data["access_token"].asString().unwrapOr("") : "";
    auto refresh = data.contains("refresh_token") ? data["refresh_token"].asString().unwrapOr("") : "";
    auto accessExp = data.contains("access_expires_in") ? data["access_expires_in"].asInt().unwrapOr(3600) : 3600;

    if (access.empty() || refresh.empty()) {
        clearPendingIntent();
        return;
    }

    m_accessToken = access;
    m_refreshToken = refresh;
    m_accessTokenExpiresAt = epochSecondsNow() + accessExp;
    saveRefreshToken(refresh);

    dispatchPendingIntent();
}

void OnlineClient::refreshAuthStatus() {
    if (m_refreshToken.empty()) {
        return;
    }

    if (accessTokenExpired()) {
        performAuthRefresh();
    }

    if (m_impl->statusTask.isPending() || m_accessToken.empty()) return;

    web::WebRequest req;
    req.header("Authorization", "Bearer " + m_accessToken);
    m_impl->statusTask.spawn(
        "ToastyReplay Auth Status",
        req.get(getApiBase() + "/api/macros/auth/status"),
        [this](web::WebResponse res) {
            if (!alive) return;
            if (!res.ok()) {
                auto payload = parseErrorPayload(res);
                if (isAuthErrorCode(payload.code) || res.code() == 401) {
                    performAuthRefresh();
                }
                return;
            }
            auto json = res.json();
            if (!json.isOk()) return;
            auto const& data = json.unwrap();

            auto status = data.contains("status") ? data["status"].asString().unwrapOr("") : "";
            if (status != "linked") return;

            auto blacklist = BlacklistState::None;
            if (data.contains("blacklist") && !data["blacklist"].isNull()) {
                blacklist = parseBlacklistState(data["blacklist"].asString().unwrapOr(""));
            }
            setLinkedState(
                data.contains("discord_username") ? data["discord_username"].asString().unwrapOr("") : discordUsername,
                data.contains("discord_id") ? data["discord_id"].asString().unwrapOr("") : discordId,
                data.contains("discord_avatar") ? data["discord_avatar"].asString().unwrapOr("") : discordAvatar,
                blacklist
            );
        }
    );
}

void OnlineClient::stopAuthPolling() {
    authPolling = false;
    authPollTimer = 0.0f;
    m_pollToken.clear();
    sessionCode.clear();
    m_activationExpiresAt = 0;
    m_impl->authStartTask.cancel();
    m_impl->authPollTask.cancel();
}

void OnlineClient::unlinkAccount() {
    if (!m_accessToken.empty()) {
        web::WebRequest req;
        req.header("Authorization", "Bearer " + m_accessToken);
        m_impl->authRefreshTask.spawn(
            "ToastyReplay Auth Logout",
            req.post(getApiBase() + "/api/macros/auth/logout"),
            [](web::WebResponse) {  }
        );
    }
    clearAuthState(true);
}

void OnlineClient::clearPendingIntent() {
    m_pendingIntent = PendingIntent::None;
    m_pendingIssueTitle.clear();
    m_pendingIssueDescription.clear();
    m_pendingMacroName.clear();
    m_pendingMacroComment.clear();
}

void OnlineClient::dispatchPendingIntent() {
    auto intent = m_pendingIntent;
    auto title = m_pendingIssueTitle;
    auto desc = m_pendingIssueDescription;
    auto name = m_pendingMacroName;
    auto comment = m_pendingMacroComment;
    clearPendingIntent();

    switch (intent) {
        case PendingIntent::None: break;
        case PendingIntent::SubmitIssue:  doSubmitIssue(title, desc); break;
        case PendingIntent::UploadMacro:  doUploadMacro(name, comment); break;
    }
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

    if (accessTokenExpired()) {
        m_pendingIntent = PendingIntent::SubmitIssue;
        m_pendingIssueTitle = title;
        m_pendingIssueDescription = description;
        performAuthRefresh();
        return;
    }

    doSubmitIssue(title, description);
}

void OnlineClient::doSubmitIssue(std::string const& title, std::string const& description) {
    web::WebRequest req;
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_accessToken);

    matjson::Value body;
    body["title"] = title;
    body["description"] = description;
    req.bodyJSON(body);

    m_impl->issueTask.spawn(
        "ToastyReplay Submit Issue",
        req.post(getApiBase() + "/submit-issue"),
        [this, title, description](web::WebResponse res) {
            if (!alive) return;

            if (res.ok()) {
                issueState = SUCCESS;
                issueResultMsg = trString("Issue submitted successfully!");
                issueResultTimer = 5.0f;
                return;
            }

            auto payload = parseErrorPayload(res);
            if (isAuthErrorCode(payload.code) || res.code() == 401) {
                if (!m_refreshToken.empty() && m_pendingIntent == PendingIntent::None) {
                    m_pendingIntent = PendingIntent::SubmitIssue;
                    m_pendingIssueTitle = title;
                    m_pendingIssueDescription = description;
                    performAuthRefresh();
                    return;
                }
                clearAuthState(false);
            }

            if (auto blacklist = parseBlacklistErrorCode(payload.code); blacklist != BlacklistState::None) {
                blacklistState = blacklist;
            }
            issueState = RSERROR;
            issueResultMsg = summarizeErrorResponse(res, "Issue submission failed");
            issueResultTimer = 5.0f;
        }
    );
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

    if (accessTokenExpired()) {
        m_pendingIntent = PendingIntent::UploadMacro;
        m_pendingMacroName = macroName;
        m_pendingMacroComment = comment;
        performAuthRefresh();
        return;
    }

    doUploadMacro(macroName, comment);
}

void OnlineClient::doUploadMacro(std::string const& macroName, std::string const& comment) {
    auto* engine = ReplayEngine::get();
    bool isLegacyCBS = engine->legacyCbsMacros.count(macroName) > 0 &&
        engine->ttr3Macros.count(macroName) == 0 &&
        engine->ttr2Macros.count(macroName) == 0;
    if (isLegacyCBS) {
        uploadState = RSERROR;
        uploadResultMsg = trString("Legacy CBS macros are playback only. Re-record in TTR3 CBS mode for exact timing.");
        uploadResultTimer = 5.0f;
        return;
    }

    std::string levelName;
    int levelId = 0;
    double tps = 240.0;
    int actionCount = 0;
    int frameCount = 0;
    double durationSeconds = 0.0;
    std::string accuracyMode = "Vanilla";
    std::vector<uint8_t> fileData;
    std::string filename;
    std::string macroFormat;
    std::string macroOrigin = "recorded";
    std::string sourceFormat;

    if (auto* macro = TTRMacro::loadFromDisk(macroName)) {
        levelName = macro->levelName;
        levelId = macro->levelId;
        tps = macro->framerate;
        accuracyMode = accuracyModeToString(macro->accuracyMode);
        actionCount = static_cast<int>(macro->inputs.size());
        frameCount = macro->inputs.empty() ? 0 : macro->inputs.back().tick;
        durationSeconds = macro->duration;
        fileData = macro->serialize();
        filename = macroName + ".ttr3";
        macroFormat = "ttr3";
        if (macro->macroConverted) {
            macroOrigin = "converted";
            sourceFormat = "GDR";
        } else {
            macroOrigin = "recorded_ttr3";
        }
        delete macro;
    } else {
        auto* gdrMacro = MacroSequence::loadFromDisk(macroName);
        if (!gdrMacro) {
            uploadState = RSERROR;
            uploadResultMsg = trString("Failed to load macro file.");
            uploadResultTimer = 5.0f;
            return;
        }
        levelName = gdrMacro->levelInfo.name;
        levelId = gdrMacro->levelInfo.id;
        tps = gdrMacro->framerate;
        accuracyMode = accuracyModeToString(gdrMacro->accuracyMode);
        actionCount = static_cast<int>(gdrMacro->inputs.size());
        frameCount = gdrMacro->inputs.empty() ? 0 : gdrMacro->inputs.back().frame;
        durationSeconds = gdrMacro->duration;
        fileData = gdrMacro->exportData(false);
        filename = macroName + ".gdr";
        macroFormat = "gdr";
        if (sourceFormat.empty()) macroOrigin = "legacy_gdr";
        delete gdrMacro;
    }

    if (levelName.empty()) levelName = macroName;
    if (tps <= 0.0) tps = 240.0;

    web::WebRequest req;
    req.header("Content-Type", "application/json");
    req.header("Authorization", "Bearer " + m_accessToken);

    matjson::Value body;
    body["level_name"] = levelName;
    body["level_id"] = levelId;
    body["tps"] = tps;
    body["action_count"] = actionCount;
    body["frame_count"] = frameCount;
    body["duration_seconds"] = durationSeconds;
    body["filename"] = filename;
    body["macro_title"] = macroName;
    body["accuracy_mode"] = accuracyMode;
    body["macro_format"] = macroFormat;
    body["macro_origin"] = macroOrigin;
    if (!sourceFormat.empty()) body["source_format"] = sourceFormat;

    auto b64Data = fileData.empty() ? std::string() : base64Encode(fileData);
    if (!b64Data.empty()) body["macro_data"] = b64Data;
    if (!comment.empty()) body["comment"] = comment;

    req.bodyJSON(body);

    m_impl->uploadTask.spawn(
        "ToastyReplay Upload Macro",
        req.post(getApiBase() + "/upload-macro"),
        [this, macroName, comment](web::WebResponse res) {
            if (!alive) return;

            if (res.ok()) {
                uploadState = SUCCESS;
                uploadResultMsg = trString("Macro uploaded successfully!");
                uploadResultTimer = 5.0f;
                return;
            }

            auto payload = parseErrorPayload(res);
            if (isAuthErrorCode(payload.code) || res.code() == 401) {
                if (!m_refreshToken.empty() && m_pendingIntent == PendingIntent::None) {
                    m_pendingIntent = PendingIntent::UploadMacro;
                    m_pendingMacroName = macroName;
                    m_pendingMacroComment = comment;
                    performAuthRefresh();
                    return;
                }
                clearAuthState(false);
            }

            if (auto blacklist = parseBlacklistErrorCode(payload.code); blacklist != BlacklistState::None) {
                blacklistState = blacklist;
            }
            uploadState = RSERROR;
            uploadResultMsg = summarizeErrorResponse(res, "Macro upload failed");
            uploadResultTimer = 5.0f;
        }
    );
}

void OnlineClient::update(float dt) {
    if (authPolling && !m_pollToken.empty()) {
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
