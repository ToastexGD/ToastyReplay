#ifndef _replay_hpp
#define _replay_hpp

#include "utils.hpp"

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

            std::string currentStem = toasty::pathToUtf8(it->path().stem());
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
            log::warn("Failed to inspect replay file {}: {}", toasty::pathToUtf8(path), ec.message());
            return std::nullopt;
        }
        if (size > kMaxReplayFileSize) {
            log::warn("Replay file is too large: {}", toasty::pathToUtf8(path));
            return std::nullopt;
        }

        auto maxSize = static_cast<uintmax_t>(std::numeric_limits<size_t>::max());
        auto maxStreamSize = static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max());
        if (size > maxSize || size > maxStreamSize) {
            log::warn("Replay file is too large: {}", toasty::pathToUtf8(path));
            return std::nullopt;
        }

        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            log::warn("Failed to open replay file {}", toasty::pathToUtf8(path));
            return std::nullopt;
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!bytes.empty()) {
            auto streamSize = static_cast<std::streamsize>(size);
            input.read(reinterpret_cast<char*>(bytes.data()), streamSize);
            if (!input || input.gcount() != streamSize) {
                log::warn("Failed to read replay file {}", toasty::pathToUtf8(path));
                return std::nullopt;
            }
        }

        return bytes;
    }
}

namespace ReplayJson {
    inline gdr::json const* find(gdr::json::object_t const& object, char const* key) {
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    template <typename Int>
    inline std::optional<Int> asInteger(gdr::json const& value) {
        static_assert(std::is_integral_v<Int>, "asInteger requires an integral type");

        if (auto const* signedValue = value.get_ptr<gdr::json::number_integer_t const*>()) {
            if (*signedValue < static_cast<gdr::json::number_integer_t>(std::numeric_limits<Int>::min()) ||
                *signedValue > static_cast<gdr::json::number_integer_t>(std::numeric_limits<Int>::max())) {
                return std::nullopt;
            }
            return static_cast<Int>(*signedValue);
        }

        if (auto const* unsignedValue = value.get_ptr<gdr::json::number_unsigned_t const*>()) {
            if (*unsignedValue > static_cast<gdr::json::number_unsigned_t>(std::numeric_limits<Int>::max())) {
                return std::nullopt;
            }
            return static_cast<Int>(*unsignedValue);
        }

        return std::nullopt;
    }

    template <typename Float>
    inline std::optional<Float> asFloat(gdr::json const& value) {
        static_assert(std::is_floating_point_v<Float>, "asFloat requires a floating-point type");

        if (auto const* floatValue = value.get_ptr<gdr::json::number_float_t const*>()) {
            return static_cast<Float>(*floatValue);
        }
        if (auto const* signedValue = value.get_ptr<gdr::json::number_integer_t const*>()) {
            return static_cast<Float>(*signedValue);
        }
        if (auto const* unsignedValue = value.get_ptr<gdr::json::number_unsigned_t const*>()) {
            return static_cast<Float>(*unsignedValue);
        }

        return std::nullopt;
    }

    inline std::optional<bool> asBool(gdr::json const& value) {
        if (auto const* boolValue = value.get_ptr<bool const*>()) {
            return *boolValue;
        }
        return std::nullopt;
    }

    inline std::optional<std::string> asString(gdr::json const& value) {
        if (auto const* stringValue = value.get_ptr<std::string const*>()) {
            return *stringValue;
        }
        return std::nullopt;
    }

    inline gdr::json::object_t const* asObject(gdr::json const& value) {
        return value.get_ptr<gdr::json::object_t const*>();
    }

    inline gdr::json::array_t const* asArray(gdr::json const& value) {
        return value.get_ptr<gdr::json::array_t const*>();
    }

    template <typename Int>
    inline std::optional<Int> getInteger(gdr::json::object_t const& object, char const* key) {
        auto const* value = find(object, key);
        return value ? asInteger<Int>(*value) : std::nullopt;
    }

    template <typename Float>
    inline std::optional<Float> getFloat(gdr::json::object_t const& object, char const* key) {
        auto const* value = find(object, key);
        return value ? asFloat<Float>(*value) : std::nullopt;
    }

    inline std::optional<bool> getBool(gdr::json::object_t const& object, char const* key) {
        auto const* value = find(object, key);
        return value ? asBool(*value) : std::nullopt;
    }

    inline std::optional<std::string> getString(gdr::json::object_t const& object, char const* key) {
        auto const* value = find(object, key);
        return value ? asString(*value) : std::nullopt;
    }

    inline gdr::json::object_t const* getObject(gdr::json::object_t const& object, char const* key) {
        auto const* value = find(object, key);
        return value ? asObject(*value) : nullptr;
    }

    inline gdr::json::array_t const* getArray(gdr::json::object_t const& object, char const* key) {
        auto const* value = find(object, key);
        return value ? asArray(*value) : nullptr;
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
        if (auto value = ReplayJson::getFloat<float>(obj, "accuracy_offset")) {
            stepOffset = *value;
        } else if (auto legacyValue = ReplayJson::getFloat<float>(obj, "cbf_offset")) {
            stepOffset = *legacyValue;
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
        if (auto mode = ReplayJson::getInteger<int>(obj, "accuracy_mode")) {
            accuracyMode = sanitizeAccuracyMode(*mode);
        } else if (auto legacyMode = ReplayJson::getBool(obj, "cbf_enabled"); legacyMode && *legacyMode) {
            accuracyMode = AccuracyMode::CBS;
        }
        if (auto anchorInterval = ReplayJson::getInteger<int>(obj, "anchor_interval")) {
            savedAnchorInterval = std::max(1, *anchorInterval);
        } else if (auto correctionInterval = ReplayJson::getInteger<int>(obj, "correction_interval")) {
            savedAnchorInterval = std::max(1, *correctionInterval);
        }
        if (auto fromStartPos = ReplayJson::getBool(obj, "from_start_pos")) {
            recordedFromStartPos = *fromStartPos;
        }
        if (auto x = ReplayJson::getFloat<float>(obj, "start_pos_x")) {
            startPosX = *x;
        }
        if (auto y = ReplayJson::getFloat<float>(obj, "start_pos_y")) {
            startPosY = *y;
        }

        anchors.clear();
        if (auto const* anchorsJson = ReplayJson::getArray(obj, "anchors")) {
            for (auto const& entry : *anchorsJson) {
                if (auto anchor = importAnchor(entry)) {
                    anchors.push_back(*anchor);
                }
            }
        } else if (auto const* correctionsJson = ReplayJson::getArray(obj, "corrections")) {
            for (auto const& entry : *correctionsJson) {
                if (auto anchor = importLegacyCorrection(entry)) {
                    anchors.push_back(*anchor);
                }
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
        log::info("Saved replay to {}", toasty::pathToUtf8(dir / (name + ".gdr")));
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

        if (auto imported = MacroSequence::tryImportData(*bytes)) {
            auto* result = new MacroSequence();
            *result = std::move(*imported);
            auto stem = toasty::pathToUtf8(path.stem());
            result->name = stem;
            result->persistedName = stem;
            return result;
        }

        log::warn("Failed to load GDR macro {}", toasty::pathToUtf8(path));

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
        auto const* object = ReplayJson::asObject(json);
        if (!object) {
            return state;
        }

        if (auto value = ReplayJson::getFloat<float>(*object, "x")) state.motion.position.x = *value;
        if (auto value = ReplayJson::getFloat<float>(*object, "y")) state.motion.position.y = *value;
        if (auto value = ReplayJson::getFloat<float>(*object, "r")) state.motion.rotation = *value;
        if (auto value = ReplayJson::getFloat<double>(*object, "vy")) state.motion.verticalVelocity = *value;
        if (auto value = ReplayJson::getFloat<double>(*object, "svy")) state.motion.preSlopeVerticalVelocity = *value;
        if (auto value = ReplayJson::getFloat<double>(*object, "hv")) state.motion.horizontalVelocity = *value;
        if (auto value = ReplayJson::getFloat<double>(*object, "g")) state.environment.gravity = *value;
        if (auto value = ReplayJson::getBool(*object, "u")) state.flags.upsideDown = *value;
        if (auto value = ReplayJson::getBool(*object, "hl")) state.flags.holdingLeft = *value;
        if (auto value = ReplayJson::getBool(*object, "hr")) state.flags.holdingRight = *value;
        if (auto value = ReplayJson::getBool(*object, "pf")) state.flags.platformer = *value;
        if (auto value = ReplayJson::getBool(*object, "dead")) state.flags.dead = *value;
        if (auto value = ReplayJson::getBool(*object, "dual")) state.environment.dualContext = *value;
        if (auto value = ReplayJson::getBool(*object, "tp")) state.environment.twoPlayerContext = *value;
        if (auto value = ReplayJson::getInteger<uint8_t>(*object, "hold")) state.flags.buttonHolds = decodeHoldMask(*value);
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

    static std::optional<PlaybackAnchor> importAnchor(gdr::json const& entry) {
        auto const* object = ReplayJson::asObject(entry);
        if (!object) {
            return std::nullopt;
        }

        PlaybackAnchor anchor;
        if (auto tick = ReplayJson::getInteger<int>(*object, "t")) {
            anchor.tick = *tick;
        }

        auto const* player1 = ReplayJson::getObject(*object, "p1");
        if (!player1) {
            return std::nullopt;
        }
        anchor.player1 = importPlayerState(gdr::json(*player1));

        if (auto hasPlayer2 = ReplayJson::getBool(*object, "has_p2")) {
            anchor.hasPlayer2 = *hasPlayer2;
        } else {
            anchor.hasPlayer2 = ReplayJson::getObject(*object, "p2") != nullptr;
        }

        if (anchor.hasPlayer2) {
            if (auto const* player2 = ReplayJson::getObject(*object, "p2")) {
                anchor.player2 = importPlayerState(gdr::json(*player2));
            } else {
                anchor.hasPlayer2 = false;
            }
        }

        if (auto rngState = ReplayJson::getInteger<uint64_t>(*object, "rng")) {
            anchor.rng.fastRandState = static_cast<uintptr_t>(*rngState);
        }
        if (auto rngLocked = ReplayJson::getBool(*object, "rng_locked")) {
            anchor.rng.locked = *rngLocked;
        }
        if (auto rngSeed = ReplayJson::getInteger<uint32_t>(*object, "rng_seed")) {
            anchor.rng.seed = *rngSeed;
        }
        if (auto latch1 = ReplayJson::getInteger<uint8_t>(*object, "l1")) {
            anchor.player1LatchMask = *latch1;
        }
        if (auto latch2 = ReplayJson::getInteger<uint8_t>(*object, "l2")) {
            anchor.player2LatchMask = *latch2;
        }
        return anchor;
    }

    static std::optional<PlaybackAnchor> importLegacyCorrection(gdr::json const& entry) {
        auto const* object = ReplayJson::asObject(entry);
        if (!object) {
            return std::nullopt;
        }

        PlaybackAnchor anchor;
        auto tick = ReplayJson::getInteger<int>(*object, "t");
        auto x1 = ReplayJson::getFloat<float>(*object, "x1");
        auto y1 = ReplayJson::getFloat<float>(*object, "y1");
        auto a1 = ReplayJson::getFloat<float>(*object, "a1");
        auto x2 = ReplayJson::getFloat<float>(*object, "x2");
        auto y2 = ReplayJson::getFloat<float>(*object, "y2");
        auto a2 = ReplayJson::getFloat<float>(*object, "a2");
        if (!tick || !x1 || !y1 || !a1 || !x2 || !y2 || !a2) {
            return std::nullopt;
        }

        anchor.tick = *tick;
        anchor.hasPlayer2 = true;
        anchor.player1.motion.position.x = *x1;
        anchor.player1.motion.position.y = *y1;
        anchor.player1.motion.rotation = *a1;
        anchor.player2.motion.position.x = *x2;
        anchor.player2.motion.position.y = *y2;
        anchor.player2.motion.rotation = *a2;
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
