#ifndef _online_client_hpp
#define _online_client_hpp

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <atomic>
#include <memory>
#include <string>

using namespace geode::prelude;

struct OnlineClientImpl;

class OnlineClient {
public:
    static constexpr const char* DEFAULT_API_BASE = "http://50.21.191.142:3000";

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

    bool isLinked() const { return !discordUsername.empty(); }
    bool hasValidSession() const { return isLinked() && !sessionCode.empty(); }
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
    void generateSessionCode();
    void releaseAvatarTexture();
    void clearAuthState(bool cancelTasks);
    void setLinkedState(
        std::string const& username,
        std::string const& id,
        std::string const& avatar,
        BlacklistState blacklist
    );
    bool handleAuthStatusResponse(web::WebResponse const& res, bool clearOnUnlinked);
};

#endif
