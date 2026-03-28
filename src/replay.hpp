#ifndef _replay_hpp
#define _replay_hpp

#include <Geode/Geode.hpp>
#include <gdr/gdr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;

#define MACRO_FORMAT_VER 1.2f

enum class AccuracyMode : uint8_t {
    Vanilla = 0,
    CBS = 1,
    CBF = 2,
};

inline AccuracyMode sanitizeAccuracyMode(int rawMode) {
    switch (rawMode) {
        case static_cast<int>(AccuracyMode::CBS):
            return AccuracyMode::CBS;
        case static_cast<int>(AccuracyMode::CBF):
            return AccuracyMode::CBF;
        default:
            return AccuracyMode::Vanilla;
    }
}

inline bool usesTimedAccuracy(AccuracyMode mode) {
    return mode != AccuracyMode::Vanilla;
}

namespace ReplayStorage {
    inline constexpr uintmax_t kMaxReplayFileSize = 64 * 1024 * 1024;

    inline std::filesystem::path getReplayDirectoryPath() {
        return Mod::get()->getSaveDir() / "replays";
    }

    inline std::string sanitizeReplayName(std::string name) {
        auto isTrimChar = [](unsigned char ch) {
            return std::isspace(ch) || ch == '.';
        };

        while (!name.empty() && isTrimChar(static_cast<unsigned char>(name.front()))) {
            name.erase(name.begin());
        }
        while (!name.empty() && isTrimChar(static_cast<unsigned char>(name.back()))) {
            name.pop_back();
        }

        for (char& c : name) {
            if (c == ':' || c == '*' || c == '?' || c == '/' || c == '\\' || c == '|' || c == '<' || c == '>' || c == '"') {
                c = '_';
            }
        }

        return name;
    }

    inline bool replayStemExists(
        std::filesystem::path const& directory,
        std::string_view stem,
        std::string_view excludedStem = {}
    ) {
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec) || ec) {
            return false;
        }

        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }

            std::string currentStem = it->path().stem().string();
            if (currentStem == excludedStem) {
                continue;
            }
            if (currentStem == stem) {
                return true;
            }
        }

        return false;
    }

    inline std::string makeUniqueReplayName(std::string requestedName, std::string_view excludedStem = {}) {
        auto directory = getReplayDirectoryPath();
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec)) {
            std::filesystem::create_directories(directory, ec);
        }

        std::string baseName = sanitizeReplayName(std::move(requestedName));
        if (baseName.empty()) {
            baseName = "macro";
        }

        std::string excludedName = sanitizeReplayName(std::string(excludedStem));
        if (!replayStemExists(directory, baseName, excludedName)) {
            return baseName;
        }

        for (int suffix = 1;; ++suffix) {
            std::string candidate = baseName + "_" + std::to_string(suffix);
            if (!replayStemExists(directory, candidate, excludedName)) {
                return candidate;
            }
        }
    }

    inline std::optional<std::vector<uint8_t>> readReplayBytes(std::filesystem::path const& path) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            return std::nullopt;
        }

        auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            log::warn("Failed to inspect replay file {}: {}", path.string(), ec.message());
            return std::nullopt;
        }
        if (size > kMaxReplayFileSize) {
            log::warn("Replay file is too large: {}", path.string());
            return std::nullopt;
        }

        auto maxSize = static_cast<uintmax_t>(std::numeric_limits<size_t>::max());
        auto maxStreamSize = static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max());
        if (size > maxSize || size > maxStreamSize) {
            log::warn("Replay file is too large: {}", path.string());
            return std::nullopt;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            log::warn("Failed to open replay file {}", path.string());
            return std::nullopt;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!bytes.empty()) {
            auto streamSize = static_cast<std::streamsize>(size);
            input.read(reinterpret_cast<char*>(bytes.data()), streamSize);
            if (!input || input.gcount() != streamSize) {
                log::warn("Failed to read replay file {}", path.string());
                return std::nullopt;
            }
        }

        return bytes;
    }
}

struct PlayerKinematicState {
    cocos2d::CCPoint position = { 0.f, 0.f };
    float rotation = 0.f;
    double verticalVelocity = 0.0;
    double preSlopeVerticalVelocity = 0.0;
    double horizontalVelocity = 0.0;
};

struct PlayerFlagState {
    bool upsideDown = false;
    bool holdingLeft = false;
    bool holdingRight = false;
    bool platformer = false;
    bool dead = false;
    std::array<bool, 4> buttonHolds = { false, false, false, false };
};

struct PlayerEnvironmentState {
    double gravity = 0.0;
    bool dualContext = false;
    bool twoPlayerContext = false;
};

struct PlayerStateBundle {
    PlayerKinematicState motion;
    PlayerFlagState flags;
    PlayerEnvironmentState environment;
};

struct AnchorRngState {
    uintptr_t fastRandState = 0;
    bool locked = false;
    uint32_t seed = 0;
};

struct PlaybackAnchor {
    int tick = 0;
    bool hasPlayer2 = false;
    PlayerStateBundle player1;
    PlayerStateBundle player2;
    AnchorRngState rng;
    uint8_t player1LatchMask = 0;
    uint8_t player2LatchMask = 0;
};

struct CheckpointStateBundle {
    int tick = 0;
    int priorTick = 0;
    PlayerStateBundle player1;
    PlayerStateBundle player2;
    AnchorRngState rng;
    uint8_t player1LatchMask = 0;
    uint8_t player2LatchMask = 0;
};

struct MacroAction : gdr::Input {
    float stepOffset = 0.0f;

    MacroAction() = default;

    MacroAction(int tick, int actionType, bool secondPlayer, bool pressed, float offset = 0.0f)
        : Input(tick, actionType, secondPlayer, pressed),
          stepOffset(offset) {}

    void parseExtension(gdr::json::object_t obj) override {
        if (obj.count("accuracy_offset")) {
            stepOffset = obj.at("accuracy_offset").get<float>();
        } else if (obj.count("cbf_offset")) {
            stepOffset = obj.at("cbf_offset").get<float>();
        }
    }

    gdr::json::object_t saveExtension() const override {
        gdr::json::object_t ext;
        if (stepOffset != 0.0f) {
            ext["accuracy_offset"] = stepOffset;
        }
        return ext;
    }
};

class MacroSequence : public gdr::Replay<MacroSequence, MacroAction> {
public:
    std::string name;
    std::string persistedName;
    std::vector<PlaybackAnchor> anchors;
    AccuracyMode accuracyMode = AccuracyMode::Vanilla;
    int savedAnchorInterval = 240;
    float startPosX = 0.f;
    float startPosY = 0.f;
    bool recordedFromStartPos = false;

    MacroSequence()
        : Replay("ToastyReplay", MOD_VERSION) {}

    void parseExtension(gdr::json::object_t obj) override {
        if (obj.count("accuracy_mode")) {
            accuracyMode = sanitizeAccuracyMode(obj.at("accuracy_mode").get<int>());
        } else if (obj.count("cbf_enabled") && obj.at("cbf_enabled").get<bool>()) {
            accuracyMode = AccuracyMode::CBS;
        }
        if (obj.count("anchor_interval")) {
            savedAnchorInterval = std::max(1, obj.at("anchor_interval").get<int>());
        } else if (obj.count("correction_interval")) {
            savedAnchorInterval = std::max(1, obj.at("correction_interval").get<int>());
        }
        if (obj.count("from_start_pos")) {
            recordedFromStartPos = obj.at("from_start_pos").get<bool>();
        }
        if (obj.count("start_pos_x")) {
            startPosX = obj.at("start_pos_x").get<float>();
        }
        if (obj.count("start_pos_y")) {
            startPosY = obj.at("start_pos_y").get<float>();
        }

        anchors.clear();
        if (obj.count("anchors")) {
            for (auto const& entry : obj.at("anchors")) {
                anchors.push_back(importAnchor(entry));
            }
        } else if (obj.count("corrections")) {
            for (auto const& entry : obj.at("corrections")) {
                anchors.push_back(importLegacyCorrection(entry));
            }
        }
    }

    gdr::json::object_t saveExtension() const override {
        gdr::json::object_t ext;
        if (accuracyMode != AccuracyMode::Vanilla) {
            ext["accuracy_mode"] = static_cast<int>(accuracyMode);
        }
        ext["anchor_interval"] = getPersistedAnchorStride();

        if (recordedFromStartPos) {
            ext["from_start_pos"] = true;
            ext["start_pos_x"] = startPosX;
            ext["start_pos_y"] = startPosY;
        }

        if (!anchors.empty()) {
            auto arr = gdr::json::array();
            for (auto const& anchor : buildPersistedAnchorSlice()) {
                arr.push_back(exportAnchor(anchor));
            }
            ext["anchors"] = arr;
        }
        return ext;
    }

    void persist(AccuracyMode mode = AccuracyMode::Vanilla, int anchorInterval = 240) {
        author = GJAccountManager::get()->m_username;
        duration = inputs.empty() ? 0.0 : static_cast<double>(inputs.back().frame) / framerate;
        accuracyMode = mode;
        savedAnchorInterval = std::max(1, anchorInterval);

        auto dir = ReplayStorage::getReplayDirectoryPath();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directory(dir);
        }

        name = ReplayStorage::makeUniqueReplayName(name, persistedName);
        persistedName = name;

        std::ofstream output(dir / (name + ".gdr"), std::ios::binary);
        auto bytes = exportData(false);
        output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
        output.close();
        log::info("Saved replay to {}", (dir / (name + ".gdr")).string());
    }

    static MacroSequence* loadFromDisk(std::string const& filename) {
        auto dir = geode::prelude::Mod::get()->getSaveDir() / "replays";
        if (!std::filesystem::exists(dir) && !std::filesystem::create_directory(dir)) {
            return nullptr;
        }

        auto path = dir / (filename + ".gdr");
        if (!std::filesystem::exists(path)) {
            path = dir / filename;
            if (!std::filesystem::exists(path)) {
                return nullptr;
            }
        }

        auto bytes = ReplayStorage::readReplayBytes(path);
        if (!bytes) {
            return nullptr;
        }

        try {
            auto* result = new MacroSequence();
            *result = MacroSequence::importData(*bytes);
            auto stem = path.stem().string();
            result->name = stem;
            result->persistedName = stem;
            return result;
        } catch (std::exception const& e) {
            log::warn("Failed to load GDR macro {}: {}", path.string(), e.what());
        } catch (...) {
            log::warn("Failed to load GDR macro {}", path.string());
        }

        return nullptr;
    }

    void truncateAfter(int tick) {
        auto inputIt = std::lower_bound(inputs.begin(), inputs.end(), tick,
            [](MacroAction const& action, int t) { return static_cast<int>(action.frame) < t; });
        inputs.erase(inputIt, inputs.end());

        auto anchorIt = std::lower_bound(anchors.begin(), anchors.end(), tick,
            [](PlaybackAnchor const& anchor, int t) { return anchor.tick < t; });
        anchors.erase(anchorIt, anchors.end());
    }

    void recordAction(int tick, int actionType, bool secondPlayer, bool pressed, float offset = 0.0f) {
        inputs.emplace_back(tick, actionType, secondPlayer, pressed, offset);
    }

    void recordAnchor(PlaybackAnchor anchor) {
        if (!anchors.empty() && anchors.back().tick == anchor.tick) {
            anchors.back() = std::move(anchor);
            return;
        }
        anchors.push_back(std::move(anchor));
    }

private:
    static uint8_t encodeHoldMask(std::array<bool, 4> const& holds) {
        uint8_t mask = 0;
        for (size_t i = 0; i < holds.size(); ++i) {
            if (holds[i]) {
                mask |= static_cast<uint8_t>(1u << i);
            }
        }
        return mask;
    }

    static std::array<bool, 4> decodeHoldMask(uint8_t mask) {
        std::array<bool, 4> holds = { false, false, false, false };
        for (size_t i = 0; i < holds.size(); ++i) {
            holds[i] = (mask & static_cast<uint8_t>(1u << i)) != 0;
        }
        return holds;
    }

    static gdr::json::object_t exportPlayerState(PlayerStateBundle const& state) {
        gdr::json::object_t player;
        player["x"] = state.motion.position.x;
        player["y"] = state.motion.position.y;
        player["r"] = state.motion.rotation;
        player["vy"] = state.motion.verticalVelocity;
        player["svy"] = state.motion.preSlopeVerticalVelocity;
        player["hv"] = state.motion.horizontalVelocity;
        player["g"] = state.environment.gravity;
        player["u"] = state.flags.upsideDown;
        player["hl"] = state.flags.holdingLeft;
        player["hr"] = state.flags.holdingRight;
        player["pf"] = state.flags.platformer;
        player["dead"] = state.flags.dead;
        player["dual"] = state.environment.dualContext;
        player["tp"] = state.environment.twoPlayerContext;
        player["hold"] = encodeHoldMask(state.flags.buttonHolds);
        return player;
    }

    static PlayerStateBundle importPlayerState(gdr::json const& json) {
        PlayerStateBundle state;
        if (json.contains("x")) state.motion.position.x = json.at("x").get<float>();
        if (json.contains("y")) state.motion.position.y = json.at("y").get<float>();
        if (json.contains("r")) state.motion.rotation = json.at("r").get<float>();
        if (json.contains("vy")) state.motion.verticalVelocity = json.at("vy").get<double>();
        if (json.contains("svy")) state.motion.preSlopeVerticalVelocity = json.at("svy").get<double>();
        if (json.contains("hv")) state.motion.horizontalVelocity = json.at("hv").get<double>();
        if (json.contains("g")) state.environment.gravity = json.at("g").get<double>();
        if (json.contains("u")) state.flags.upsideDown = json.at("u").get<bool>();
        if (json.contains("hl")) state.flags.holdingLeft = json.at("hl").get<bool>();
        if (json.contains("hr")) state.flags.holdingRight = json.at("hr").get<bool>();
        if (json.contains("pf")) state.flags.platformer = json.at("pf").get<bool>();
        if (json.contains("dead")) state.flags.dead = json.at("dead").get<bool>();
        if (json.contains("dual")) state.environment.dualContext = json.at("dual").get<bool>();
        if (json.contains("tp")) state.environment.twoPlayerContext = json.at("tp").get<bool>();
        if (json.contains("hold")) state.flags.buttonHolds = decodeHoldMask(json.at("hold").get<uint8_t>());
        return state;
    }

    static gdr::json::object_t exportAnchor(PlaybackAnchor const& anchor) {
        gdr::json::object_t obj;
        obj["t"] = anchor.tick;
        obj["has_p2"] = anchor.hasPlayer2;
        obj["p1"] = exportPlayerState(anchor.player1);
        if (anchor.hasPlayer2) {
            obj["p2"] = exportPlayerState(anchor.player2);
        }
        if (anchor.rng.fastRandState != 0) {
            obj["rng"] = static_cast<uint64_t>(anchor.rng.fastRandState);
        }
        if (anchor.rng.locked) {
            obj["rng_locked"] = true;
            obj["rng_seed"] = anchor.rng.seed;
        }
        if (anchor.player1LatchMask != 0) {
            obj["l1"] = anchor.player1LatchMask;
        }
        if (anchor.player2LatchMask != 0) {
            obj["l2"] = anchor.player2LatchMask;
        }
        return obj;
    }

    static PlaybackAnchor importAnchor(gdr::json const& entry) {
        PlaybackAnchor anchor;
        if (entry.contains("t")) anchor.tick = entry.at("t").get<int>();
        anchor.player1 = importPlayerState(entry.at("p1"));
        anchor.hasPlayer2 = entry.contains("has_p2")
            ? entry.at("has_p2").get<bool>()
            : entry.contains("p2");
        if (anchor.hasPlayer2 && entry.contains("p2")) {
            anchor.player2 = importPlayerState(entry.at("p2"));
        }
        if (entry.contains("rng")) {
            anchor.rng.fastRandState = static_cast<uintptr_t>(entry.at("rng").get<uint64_t>());
        }
        if (entry.contains("rng_locked")) {
            anchor.rng.locked = entry.at("rng_locked").get<bool>();
        }
        if (entry.contains("rng_seed")) {
            anchor.rng.seed = entry.at("rng_seed").get<uint32_t>();
        }
        if (entry.contains("l1")) {
            anchor.player1LatchMask = entry.at("l1").get<uint8_t>();
        }
        if (entry.contains("l2")) {
            anchor.player2LatchMask = entry.at("l2").get<uint8_t>();
        }
        return anchor;
    }

    static PlaybackAnchor importLegacyCorrection(gdr::json const& entry) {
        PlaybackAnchor anchor;
        anchor.tick = entry.at("t").get<int>();
        anchor.hasPlayer2 = true;
        anchor.player1.motion.position.x = entry.at("x1").get<float>();
        anchor.player1.motion.position.y = entry.at("y1").get<float>();
        anchor.player1.motion.rotation = entry.at("a1").get<float>();
        anchor.player2.motion.position.x = entry.at("x2").get<float>();
        anchor.player2.motion.position.y = entry.at("y2").get<float>();
        anchor.player2.motion.rotation = entry.at("a2").get<float>();
        return anchor;
    }

    int getPersistedAnchorStride() const {
        int stride = savedAnchorInterval > 0
            ? std::max(1, savedAnchorInterval / 10)
            : std::max(1, static_cast<int>(std::round(framerate / 10.0)));
        return stride;
    }

    std::vector<PlaybackAnchor> buildPersistedAnchorSlice() const {
        if (anchors.empty()) {
            return {};
        }

        std::vector<PlaybackAnchor> persisted;
        persisted.reserve(anchors.size() / 8 + 2);

        int stride = getPersistedAnchorStride();
        int lastTick = std::numeric_limits<int>::min();
        for (auto const& anchor : anchors) {
            bool boundary = persisted.empty() || anchor.tick == anchors.back().tick;
            if (!boundary && anchor.tick - lastTick < stride) {
                continue;
            }

            persisted.push_back(anchor);
            lastTick = anchor.tick;
        }

        if (persisted.back().tick != anchors.back().tick) {
            persisted.push_back(anchors.back());
        }

        return persisted;
    }
};

#endif
