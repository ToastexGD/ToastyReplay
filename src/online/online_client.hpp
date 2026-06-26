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

    std::string sessionCode;

    std::string discordUsername;
    std::string discordId;
    std::string discordAvatar;
    BlacklistState blacklistState = BlacklistState::None;

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

    std::string m_accessToken;
    std::string m_refreshToken;
    std::int64_t m_accessTokenExpiresAt = 0;

    std::string m_pollToken;
    std::int64_t m_activationExpiresAt = 0;
    bool m_refreshInFlight = false;

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

    void saveRefreshToken(std::string const& token);
    void clearRefreshToken();
    std::string loadRefreshToken() const;

    void onActivationApproved(matjson::Value const& data);
    void onRefreshSuccess(matjson::Value const& data);
    void performAuthRefresh();
    bool accessTokenExpired() const;

    void dispatchPendingIntent();
    void clearPendingIntent();

    void doSubmitIssue(std::string const& title, std::string const& description);
    void doUploadMacro(std::string const& macroName, std::string const& comment);
};

#endif
