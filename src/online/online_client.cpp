#include "online/online_client.hpp"
#include "ToastyReplay.hpp"
#include <Geode/modify/AppDelegate.hpp>
#include <random>
#include <thread>

using namespace geode::prelude;

std::atomic<bool> OnlineClient::alive{true};

void OnlineClient::shutdown() {
    alive = false;
    auto* client = get();
    if (client->avatarTexture) {
        client->avatarTexture->release();
        client->avatarTexture = nullptr;
    }
    client->avatarLoaded = false;
}

class $modify(OnlineAppDelegate, AppDelegate) {
    void trySaveGame(bool p0) {
        OnlineClient::shutdown();
        AppDelegate::trySaveGame(p0);
    }
};

static const char BASE64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);

        out.push_back(BASE64_TABLE[(n >> 18) & 0x3F]);
        out.push_back(BASE64_TABLE[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < data.size()) ? BASE64_TABLE[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < data.size()) ? BASE64_TABLE[n & 0x3F] : '=');
    }
    return out;
}

static std::string summarizeErrorResponse(web::WebResponse const& res, std::string_view fallback) {
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

void OnlineClient::generateSessionCode() {
    static const char HEX[] = "0123456789abcdef";
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
        std::string ext = (discordAvatar.rfind("a_", 0) == 0) ? ".gif" : ".png";
        return "https://cdn.discordapp.com/avatars/" + discordId + "/" + discordAvatar + ext + "?size=128";
    }
    if (!discordId.empty()) {
        uint64_t id = std::stoull(discordId);
        int index = static_cast<int>((id >> 22) % 6);
        return "https://cdn.discordapp.com/embed/avatars/" + std::to_string(index) + ".png";
    }
    return "";
}

void OnlineClient::fetchAvatar() {
    std::string url = getAvatarUrl();
    if (url.empty()) return;

    avatarLoading = true;

    std::thread([this, url]() {
        auto req = web::WebRequest();
        auto res = req.getSync(url);

        if (res.ok()) {
            auto& bytes = res.data();

            Loader::get()->queueInMainThread([this, bytes = std::move(bytes)]() {
                if (!alive) return;
                auto* image = new cocos2d::CCImage();
                if (image->initWithImageData(const_cast<uint8_t*>(bytes.data()), bytes.size())) {
                    auto* texture = new cocos2d::CCTexture2D();
                    if (texture->initWithImage(image)) {
                        if (avatarTexture) avatarTexture->release();
                        avatarTexture = texture;
                        avatarLoaded = true;
                    } else {
                        texture->release();
                    }
                }
                image->release();
                avatarLoading = false;
            });
        } else {
            Loader::get()->queueInMainThread([this]() {
                if (!alive) return;
                avatarLoading = false;
            });
        }
    }).detach();
}

void OnlineClient::submitIssue(const std::string& title, const std::string& description) {
    if (issueState == PENDING) return;
    issueState = PENDING;
    issueResultMsg = "Submitting...";

    std::string titleCopy = title;
    std::string descCopy = description;
    std::string code = sessionCode;

    std::thread([this, titleCopy, descCopy, code]() {
        auto req = web::WebRequest();
        req.header("Content-Type", "application/json");

        matjson::Value body;
        body["title"] = titleCopy;
        body["description"] = descCopy;
        if (!code.empty()) {
            body["session_code"] = code;
        }
        req.bodyJSON(body);

        auto res = req.postSync(getApiBase() + "/submit-issue");

        Loader::get()->queueInMainThread([this, res = std::move(res)]() {
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
    }).detach();
}

void OnlineClient::uploadMacro(const std::string& macroName, const std::string& comment) {
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
        TTRMacro* macro = TTRMacro::loadFromDisk(macroName);
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
        MacroSequence* macro = MacroSequence::loadFromDisk(macroName);
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
    if (tps <= 0) tps = 240.0;

    std::string code = sessionCode;
    std::string b64Data = fileData.empty() ? "" : base64Encode(fileData);

    std::string uploadComment = comment;

    std::thread([this, levelName, levelId, tps, actionCount, frameCount, durationSeconds, filename, code, b64Data, uploadComment]() {
        auto req = web::WebRequest();
        req.header("Content-Type", "application/json");

        matjson::Value body;
        body["level_name"] = levelName;
        body["level_id"] = levelId;
        body["tps"] = tps;
        body["action_count"] = actionCount;
        body["frame_count"] = frameCount;
        body["duration_seconds"] = durationSeconds;
        body["filename"] = filename;

        if (!code.empty()) {
            body["session_code"] = code;
        }
        if (!b64Data.empty()) {
            body["macro_data"] = b64Data;
        }
        if (!uploadComment.empty()) {
            body["comment"] = uploadComment;
        }

        req.bodyJSON(body);

        auto res = req.postSync(getApiBase() + "/upload-macro");

        Loader::get()->queueInMainThread([this, res = std::move(res)]() {
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
    }).detach();
}

void OnlineClient::startAuthFlow() {
    if (sessionCode.empty()) {
        generateSessionCode();
        save();
    }

    std::string url = getApiBase() + "/auth/login?code=" + sessionCode;
    geode::utils::web::openLinkInBrowser(url);

    authPolling = true;
    authPollTimer = 0.0f;
}

void OnlineClient::pollAuthStatus() {
    std::string code = sessionCode;

    std::thread([this, code]() {
        auto req = web::WebRequest();
        auto res = req.getSync(getApiBase() + "/auth/status?code=" + code);

        Loader::get()->queueInMainThread([this, res = std::move(res)]() {
            if (!alive) return;
            if (res.ok()) {
                auto json = res.json();
                if (json.isOk()) {
                    auto& data = json.unwrap();
                    auto status = data["status"].asString().unwrapOr("");
                    if (status == "linked") {
                        discordUsername = data["discord_username"].asString().unwrapOr("");
                        discordId = data["discord_id"].asString().unwrapOr("");
                        discordAvatar = data["discord_avatar"].asString().unwrapOr("");
                        authPolling = false;
                        save();
                        fetchAvatar();
                    }
                }
            }
        });
    }).detach();
}

void OnlineClient::stopAuthPolling() {
    authPolling = false;
    authPollTimer = 0.0f;
}

void OnlineClient::unlinkAccount() {
    discordUsername.clear();
    discordId.clear();
    discordAvatar.clear();
    sessionCode.clear();
    authPolling = false;
    if (avatarTexture) {
        avatarTexture->release();
        avatarTexture = nullptr;
    }
    avatarLoaded = false;
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
