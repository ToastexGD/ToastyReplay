#ifndef _online_client_hpp
#define _online_client_hpp

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

using namespace geode::prelude;

struct OnlineClientImpl;

class OnlineClient {
public:
    static constexpr const char* DEFAULT_API_BASE = "https://toastyreplay.xyz";

    enum class BlacklistState {
        None,
        Macros,
        Issues,
        Full
    };

    OnlineClient();
    ~OnlineClient();

    // Legacy compatibility field. The old auth flow exposed a 32-char hex
    // session code; the new device-code flow uses a 6-char pairing code plus
    // opaque refresh/access tokens. We keep this field so the GUI's existing
    // emptiness checks still work — its value is now the current 6-char code
    // while an activation is in flight, or empty once we're linked.
    std::string sessionCode;

    std::string discordUsername;
    std::string discordId;
    std::string discordAvatar;
    BlacklistState blacklistState = BlacklistState::None;

    // True while we're polling the server waiting for the user to approve the
    // device in their browser. Matches the old field name/behavior.
    bool authPolling = false;
    float authPollTimer = 0.0f;
    static constexpr float AUTH_POLL_INTERVAL = 3.0f;

    enum RequestState { IDLE, PENDING, SUCCESS, RSERROR };

    RequestState issueState = IDLE;
    std::string issueResultMsg;
    float issueResultTimer = 0.0f;

    RequestState uploadState = IDLE;
    std::string uploadResultMsg;
    float uploadResultTimer = 0.0f;

    cocos2d::CCTexture2D* avatarTexture = nullptr;
    bool avatarLoading = false;
    bool avatarLoaded = false;

    void submitIssue(const std::string& title, const std::string& description);
    void uploadMacro(const std::string& macroName, const std::string& comment = "");
    void startAuthFlow();
    void pollAuthStatus();
    void refreshAuthStatus();
    void stopAuthPolling();
    void unlinkAccount();
    void update(float dt);
    void fetchAvatar();

    void save();
    void load();
    std::string getApiBase() const;

    // Linked means we hold a refresh token and a Discord identity. A valid
    // session additionally means that refresh token hasn't been revoked by
    // the server yet (checked opportunistically on every API call).
    bool isLinked() const { return !discordUsername.empty() && !m_refreshToken.empty(); }
    bool hasValidSession() const { return isLinked(); }
    bool canUploadMacros() const;
    bool canSubmitIssues() const;
    std::string getRestrictionMessage(bool forUpload) const;
    std::string getBlacklistStatusText() const;

    std::string getAvatarUrl() const;

    static OnlineClient* get() {
        static OnlineClient* instance = new OnlineClient();
        return instance;
    }

    static std::atomic<bool> alive;
    static void shutdown();

private:
    std::unique_ptr<OnlineClientImpl> m_impl;

    // Auth tokens. Access token is a 1-hour bearer; refresh token rotates on
    // every server refresh call. Refresh token is persisted DPAPI-encrypted.
    std::string m_accessToken;
    std::string m_refreshToken;
    std::int64_t m_accessTokenExpiresAt = 0;

    // Transient state used during device-code activation.
    std::string m_pollToken;
    std::int64_t m_activationExpiresAt = 0;
    bool m_refreshInFlight = false;

    // A queued upload/issue that's waiting for an auto-refresh to finish.
    // Only one pending intent at a time; the GUI already prevents double-fire
    // by checking ::PENDING state.
    enum class PendingIntent { None, SubmitIssue, UploadMacro };
    PendingIntent m_pendingIntent = PendingIntent::None;
    std::string m_pendingIssueTitle;
    std::string m_pendingIssueDescription;
    std::string m_pendingMacroName;
    std::string m_pendingMacroComment;

    void releaseAvatarTexture();
    void clearAuthState(bool cancelTasks);
    void setLinkedState(
        std::string const& username,
        std::string const& id,
        std::string const& avatar,
        BlacklistState blacklist
    );

    // Save/load refresh token (DPAPI-encrypted on Windows, obfuscated on macOS).
    void saveRefreshToken(std::string const& token);
    void clearRefreshToken();
    std::string loadRefreshToken() const;

    // Token lifecycle helpers.
    void onActivationApproved(matjson::Value const& data);
    void onRefreshSuccess(matjson::Value const& data);
    void performAuthRefresh();
    bool accessTokenExpired() const;

    // Re-runs whichever intent was queued while a refresh was in flight.
    void dispatchPendingIntent();
    void clearPendingIntent();

    void doSubmitIssue(std::string const& title, std::string const& description);
    void doUploadMacro(std::string const& macroName, std::string const& comment);
};

#endif
