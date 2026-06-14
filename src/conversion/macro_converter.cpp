#include "conversion/macro_converter.hpp"

#include "format/replay.hpp"
#include "format/ttr_format.hpp"
#include "utils.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <zlib.h>

using namespace geode::prelude;

namespace toasty::conversion {
namespace {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

thread_local bool g_importForTTR3 = false;

static constexpr uint8_t kTCMHeader[16] = {
    0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
    0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00
};

static constexpr std::array<ReplayFormat, 28> kSupportedReplayFormats = {
    ReplayFormat::MegaHackJson,
    ReplayFormat::MegaHackBinary,
    ReplayFormat::TasBotJson,
    ReplayFormat::ZBotFrame,
    ReplayFormat::YBotFrame,
    ReplayFormat::YBot2,
    ReplayFormat::Amethyst,
    ReplayFormat::Echo,
    ReplayFormat::GDMO,
    ReplayFormat::ReplayBot,
    ReplayFormat::Rush,
    ReplayFormat::KDBot,
    ReplayFormat::Plaintext,
    ReplayFormat::DDHOR,
    ReplayFormat::XBotFrame,
    ReplayFormat::XdBot,
    ReplayFormat::RBot,
    ReplayFormat::Zephyrus,
    ReplayFormat::ReplayEngine1,
    ReplayFormat::ReplayEngine2,
    ReplayFormat::ReplayEngine3,
    ReplayFormat::Silicate1,
    ReplayFormat::Silicate2,
    ReplayFormat::Silicate3,
    ReplayFormat::TCBot,
    ReplayFormat::GDR2,
    ReplayFormat::GdrJson,
    ReplayFormat::UvBot,
};

static std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
        value.substr(value.size() - suffix.size()) == suffix;
}

class ByteReader {
public:
    explicit ByteReader(std::vector<uint8_t> const& data) : m_data(data) {}
    explicit ByteReader(std::vector<uint8_t> const& data, size_t offset) : m_data(data), m_pos(offset) {}

    size_t position() const { return m_pos; }
    size_t size() const { return m_data.size(); }
    size_t remaining() const { return m_pos <= m_data.size() ? m_data.size() - m_pos : 0; }
    bool eof() const { return m_pos >= m_data.size(); }

    void seek(size_t pos) {
        if (pos > m_data.size()) throw FormatError("unexpected end of file");
        m_pos = pos;
    }

    void skip(size_t count) {
        if (count > remaining()) throw FormatError("unexpected end of file");
        m_pos += count;
    }

    uint8_t u8() {
        if (remaining() < 1) throw FormatError("unexpected end of file");
        return m_data[m_pos++];
    }

    bool boolean() {
        return u8() != 0;
    }

    uint16_t u16le() { return readLE<uint16_t>(); }
    int16_t i16le() { return readLE<int16_t>(); }
    uint32_t u32le() { return readLE<uint32_t>(); }
    int32_t i32le() { return readLE<int32_t>(); }
    uint64_t u64le() { return readLE<uint64_t>(); }
    int64_t i64le() { return readLE<int64_t>(); }

    uint16_t u16be() { return readBE<uint16_t>(); }
    uint32_t u32be() { return readBE<uint32_t>(); }

    float f32le() {
        uint32_t raw = u32le();
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    double f64le() {
        uint64_t raw = u64le();
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    float f32be() {
        uint32_t raw = readBE<uint32_t>();
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    double f64be() {
        uint64_t raw = readBE<uint64_t>();
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    uint64_t varU64() {
        uint64_t result = 0;
        int shift = 0;
        while (shift < 64) {
            uint8_t byte = u8();
            result |= static_cast<uint64_t>(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) return result;
            shift += 7;
        }
        throw FormatError("varint is too large");
    }

    int32_t varI32() {
        uint64_t value = varU64();
        if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
            throw FormatError("varint exceeds int32");
        }
        return static_cast<int32_t>(value);
    }

    std::string gdr2String() {
        int32_t length = varI32();
        if (length < 0 || length > 0xffff) throw FormatError("invalid string length");
        if (static_cast<size_t>(length) > remaining()) throw FormatError("unexpected end of file");
        std::string result(reinterpret_cast<char const*>(m_data.data() + m_pos), static_cast<size_t>(length));
        m_pos += static_cast<size_t>(length);
        return result;
    }

private:
    template <typename T>
    T readLE() {
        if (remaining() < sizeof(T)) throw FormatError("unexpected end of file");
        T value = 0;
        std::memcpy(&value, m_data.data() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return value;
    }

    template <typename T>
    T readBE() {
        if (remaining() < sizeof(T)) throw FormatError("unexpected end of file");
        T value = 0;
        for (size_t i = 0; i < sizeof(T); ++i) {
            value = static_cast<T>((value << 8) | m_data[m_pos + i]);
        }
        m_pos += sizeof(T);
        return value;
    }

    std::vector<uint8_t> const& m_data;
    size_t m_pos = 0;
};

static bool isFiniteFps(double fps) {
    return std::isfinite(fps) && fps >= 1.0 && fps <= 1000000.0;
}

static int sanitizeButton(int button) {
    if (button < 1 || button > 3) return 1;
    return button;
}

static double safeFps(double fps) {
    return isFiniteFps(fps) ? fps : 240.0;
}

static void addWarningOnce(ImportedReplay& replay, std::string warning) {
    if (std::find(replay.warnings.begin(), replay.warnings.end(), warning) == replay.warnings.end()) {
        replay.warnings.push_back(std::move(warning));
    }
}

class ImportTimeline {
public:
    explicit ImportTimeline(double fps)
        : m_initialFps(safeFps(fps)),
          m_currentFps(safeFps(fps)) {}

    double timeForFrame(double frame) const {
        if (!std::isfinite(frame) || frame < 0.0) {
            frame = 0.0;
        }
        return m_segmentTime + (frame - m_segmentFrame) / m_currentFps;
    }

    void changeFps(ImportedReplay& replay, double frame, double fps) {
        fps = safeFps(fps);
        frame = std::max(0.0, frame);
        m_segmentTime = timeForFrame(frame);
        m_segmentFrame = frame;
        m_currentFps = fps;
        replay.dynamicTiming = true;
        replay.needsCbsTiming = true;
        replay.tpsEvents.push_back({m_segmentTime, fps});
        if (!replay.convertedForTTR3) {
            addWarningOnce(replay, "Dynamic TPS/FPS was converted using absolute timing.");
        }
    }

    double initialFps() const {
        return m_initialFps;
    }

private:
    double m_initialFps = 240.0;
    double m_currentFps = 240.0;
    double m_segmentFrame = 0.0;
    double m_segmentTime = 0.0;
};

static uint8_t holdMask(std::array<bool, 3> const& holds) {
    uint8_t mask = 0;
    for (size_t i = 0; i < holds.size(); ++i) {
        if (holds[i]) {
            mask |= static_cast<uint8_t>(1u << i);
        }
    }
    return mask;
}

static PlayerStateBundle makeImportedPlayerState(
    float x,
    float y,
    float rotation,
    double yVelocity,
    bool platformer
) {
    auto sanitizeF = [](float value) -> float {
        return std::isfinite(value) ? value : 0.0f;
    };
    auto sanitizeD = [](double value) -> double {
        return std::isfinite(value) ? value : 0.0;
    };

    PlayerStateBundle state;
    state.motion.position = cocos2d::CCPoint { sanitizeF(x), sanitizeF(y) };
    state.motion.rotation = sanitizeF(rotation);
    state.motion.verticalVelocity = sanitizeD(yVelocity);
    state.motion.preSlopeVerticalVelocity = sanitizeD(yVelocity);
    state.flags.platformer = platformer;
    state.environment.extendedState = false;
    return state;
}

static void addAnchor(
    ImportedReplay& replay,
    double time,
    double sourceFrame,
    std::optional<PlayerStateBundle> player1,
    std::optional<PlayerStateBundle> player2 = std::nullopt
) {
    if (!std::isfinite(time) || time < 0.0) {
        time = std::isfinite(sourceFrame) && sourceFrame >= 0.0 ? sourceFrame / safeFps(replay.fps) : 0.0;
    }

    PlaybackAnchor anchor;
    anchor.tick = static_cast<int>(std::clamp<int64_t>(
        static_cast<int64_t>(std::floor(time * safeFps(replay.fps))) + 1,
        1,
        std::numeric_limits<int32_t>::max()
    ));
    anchor.hasPlayer2 = player2.has_value();
    if (player1) {
        anchor.player1 = *player1;
    }
    if (player2) {
        anchor.player2 = *player2;
        replay.twoPlayerMode = true;
    }
    replay.anchors.push_back(std::move(anchor));
    replay.duration = std::max(replay.duration, time);
}

static void addPlayerAnchor(
    ImportedReplay& replay,
    double time,
    double sourceFrame,
    bool player2,
    PlayerStateBundle state
) {
    if (!std::isfinite(time) || time < 0.0) {
        time = std::isfinite(sourceFrame) && sourceFrame >= 0.0 ? sourceFrame / safeFps(replay.fps) : 0.0;
    }
    int tick = static_cast<int>(std::clamp<int64_t>(
        static_cast<int64_t>(std::floor(time * safeFps(replay.fps))) + 1,
        1,
        std::numeric_limits<int32_t>::max()
    ));

    auto it = std::find_if(replay.anchors.begin(), replay.anchors.end(), [&](PlaybackAnchor const& anchor) {
        return anchor.tick == tick;
    });
    if (it == replay.anchors.end()) {
        PlaybackAnchor anchor;
        anchor.tick = tick;
        it = replay.anchors.insert(replay.anchors.end(), std::move(anchor));
    }

    if (player2) {
        it->player2 = std::move(state);
        it->hasPlayer2 = true;
        replay.twoPlayerMode = true;
    } else {
        it->player1 = std::move(state);
    }
    replay.duration = std::max(replay.duration, time);
}

static void readReplayEngineFrameAnchor(ByteReader& reader, ImportedReplay& replay) {
    size_t start = reader.position();
    uint32_t frame = reader.u32le();
    float x = reader.f32le();
    float y = reader.f32le();
    float rotation = reader.f32le();
    double yVelocity = reader.f64le();
    bool player2 = reader.u8() != 0;
    reader.seek(start + 32);

    addPlayerAnchor(
        replay,
        static_cast<double>(frame) / safeFps(replay.fps),
        frame,
        player2,
        makeImportedPlayerState(x, y, rotation, yVelocity, replay.platformerMode)
    );
}

static PlayerStateBundle readZephyrusPlayerState(ByteReader& reader, ImportedReplay const& replay) {
    float x = reader.f32le();
    float y = reader.f32le();
    double yVelocity = reader.f64le();
    float rotation = reader.f32le();
    return makeImportedPlayerState(x, y, rotation, yVelocity, replay.platformerMode);
}

static void readUvBotPhysicsAnchor(ByteReader& reader, ImportedReplay& replay, bool player2) {
    uint64_t frame = reader.u64le();
    float x = reader.f32le();
    float y = reader.f32le();
    float rotation = reader.f32le();
    double yVelocity = reader.f64le();
    addPlayerAnchor(
        replay,
        static_cast<double>(frame) / safeFps(replay.fps),
        static_cast<double>(frame),
        player2,
        makeImportedPlayerState(x, y, rotation, yVelocity, replay.platformerMode)
    );
}

static void addInput(
    ImportedReplay& replay,
    double time,
    double sourceFrame,
    int button,
    bool player2,
    bool pressed,
    bool swift = false
) {
    if (!isFiniteFps(replay.fps)) replay.fps = 240.0;
    if (!std::isfinite(time) || time < 0.0) {
        time = std::isfinite(sourceFrame) && sourceFrame >= 0.0 ? sourceFrame / replay.fps : 0.0;
    }
    if (!std::isfinite(sourceFrame) || sourceFrame < 0.0) {
        sourceFrame = time * replay.fps;
    }

    button = sanitizeButton(button);
    double rawTick = time * replay.fps;
    bool fractional = std::abs(rawTick - std::round(rawTick)) > 0.000001;
    if (fractional && !g_importForTTR3) {
        replay.needsCbsTiming = true;
    }

    replay.platformerMode = replay.platformerMode || button == 2 || button == 3;
    replay.twoPlayerMode = replay.twoPlayerMode || player2;
    replay.duration = std::max(replay.duration, time);
    replay.inputs.push_back({
        time,
        sourceFrame,
        static_cast<int64_t>(std::llround(std::max(0.0, rawTick))),
        static_cast<uint64_t>(replay.inputs.size()),
        button,
        player2,
        pressed,
        swift,
        0.0f,
        -1.0f
    });
}

static bool inputLess(ImportedInput const& a, ImportedInput const& b) {
    if (a.time != b.time) return a.time < b.time;
    if (a.sourceFrame != b.sourceFrame) return a.sourceFrame < b.sourceFrame;
    return a.sequence < b.sequence;
}

static void resolveSameTickButtonCollisions(ImportedReplay& replay) {
    if (replay.inputs.size() < 2) {
        return;
    }

    double fps = safeFps(replay.fps);
    double tickDuration = 1.0 / fps;
    bool anyAdjusted = false;

    size_t i = 0;
    while (i < replay.inputs.size()) {
        int64_t tick = replay.inputs[i].tick;
        size_t tickEnd = i + 1;
        while (tickEnd < replay.inputs.size() && replay.inputs[tickEnd].tick == tick) {
            ++tickEnd;
        }

        if (tickEnd - i > 1) {
            std::unordered_map<int, std::vector<size_t>> groups;
            for (size_t j = i; j < tickEnd; ++j) {
                if (replay.inputs[j].swift) {
                    continue;
                }
                int key = replay.inputs[j].button | (replay.inputs[j].player2 ? 0x100 : 0x000);
                groups[key].push_back(j);
            }

            for (auto const& entry : groups) {
                auto const& indices = entry.second;
                if (indices.size() < 2) continue;

                if (!replay.inputs[indices.front()].pressed) {
                    continue;
                }

                double baseTime = static_cast<double>(tick) / fps;
                double fractionStep = 1.0 / static_cast<double>(indices.size());
                for (size_t k = 0; k < indices.size(); ++k) {
                    double fraction = static_cast<double>(k) * fractionStep;
                    replay.inputs[indices[k]].time = baseTime + fraction * tickDuration;
                }
                anyAdjusted = true;
            }
        }

        i = tickEnd;
    }

    if (anyAdjusted) {
        replay.needsCbsTiming = true;
        addWarningOnce(replay, "Spread same-tick taps across sub-ticks for accurate playback.");
    }
}

static void populateAnchorHolds(ImportedReplay& replay) {
    if (replay.anchors.empty()) {
        return;
    }

    std::sort(replay.anchors.begin(), replay.anchors.end(), [](PlaybackAnchor const& a, PlaybackAnchor const& b) {
        return a.tick < b.tick;
    });

    std::array<bool, 3> p1Holds = { false, false, false };
    std::array<bool, 3> p2Holds = { false, false, false };
    size_t inputIndex = 0;

    for (auto& anchor : replay.anchors) {
        while (inputIndex < replay.inputs.size() && replay.inputs[inputIndex].tick <= anchor.tick) {
            auto const& input = replay.inputs[inputIndex++];
            auto& holds = input.player2 ? p2Holds : p1Holds;
            holds[static_cast<size_t>(std::clamp(input.button, 1, 3) - 1)] = input.pressed;
        }

        for (size_t i = 0; i < p1Holds.size(); ++i) {
            anchor.player1.flags.buttonHolds[i] = p1Holds[i];
        }
        anchor.player1LatchMask = holdMask(p1Holds);

        if (anchor.hasPlayer2) {
            for (size_t i = 0; i < p2Holds.size(); ++i) {
                anchor.player2.flags.buttonHolds[i] = p2Holds[i];
            }
            anchor.player2LatchMask = holdMask(p2Holds);
        }
    }
}

static void finishImport(ImportedReplay& replay) {
    if (replay.convertedForTTR3 || g_importForTTR3) {
        finishImportForTTR3(replay);
        return;
    }

    if (!isFiniteFps(replay.fps)) {
        replay.warnings.push_back("Invalid source FPS; using 240 TPS.");
        replay.fps = 240.0;
    }

    std::stable_sort(replay.inputs.begin(), replay.inputs.end(), inputLess);

    std::array<bool, 6> held = { false, false, false, false, false, false };
    std::vector<ImportedInput> normalized;
    normalized.reserve(replay.inputs.size());
    size_t removedDuplicates = 0;

    for (auto input : replay.inputs) {
        input.button = sanitizeButton(input.button);
        input.tick = static_cast<int64_t>(std::llround(std::max(0.0, input.time * replay.fps)));
        size_t idx = (input.player2 ? 3u : 0u) + static_cast<size_t>(input.button - 1);
        if (!input.swift && held[idx] == input.pressed) {
            ++removedDuplicates;
            continue;
        }
        held[idx] = input.pressed;
        input.sequence = static_cast<uint64_t>(normalized.size());
        normalized.push_back(input);
    }

    if (removedDuplicates != 0) {
        addWarningOnce(replay, "Removed duplicate or stale input transitions.");
    }
    replay.inputs = std::move(normalized);

    if (!replay.inputs.empty()) {
        auto const& last = replay.inputs.back();
        replay.duration = std::max(replay.duration, last.time);
    }

    populateAnchorHolds(replay);
    resolveSameTickButtonCollisions(replay);
}

static bool hasMagic(std::vector<uint8_t> const& data, std::string_view magic) {
    return data.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), data.begin());
}

static bool looksLikeText(std::vector<uint8_t> const& data) {
    if (data.empty()) return false;
    size_t printable = 0;
    size_t checked = std::min<size_t>(data.size(), 4096);
    for (size_t i = 0; i < checked; ++i) {
        uint8_t ch = data[i];
        if (ch == 0) return false;
        if (ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 0x20 && ch < 0x7f)) {
            ++printable;
        }
    }
    return printable * 100 / checked > 90;
}

static std::string bytesToText(std::vector<uint8_t> const& data) {
    return std::string(reinterpret_cast<char const*>(data.data()), data.size());
}

static std::vector<std::string> splitLines(std::string const& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

static std::optional<gdr::json> parseJson(std::vector<uint8_t> const& data) {
    auto text = bytesToText(data);
    auto parsed = gdr::json::parse(text, nullptr, false);
    if (parsed.is_discarded()) return std::nullopt;
    return parsed;
}

static ReplayFormat detectJsonFormat(gdr::json const& json, std::string const& filename) {
    auto const* object = ReplayJson::asObject(json);
    if (!object) return ReplayFormat::Unknown;
    if (ReplayJson::getObject(*object, "meta") && ReplayJson::getArray(*object, "events")) {
        return ReplayFormat::MegaHackJson;
    }
    if (ReplayJson::getFloat<double>(*object, "fps") && ReplayJson::getArray(*object, "macro")) {
        return ReplayFormat::TasBotJson;
    }
    if (ReplayJson::getArray(*object, "Echo Replay")) {
        return ReplayFormat::Echo;
    }
    if (ReplayJson::getFloat<double>(*object, "framerate") && ReplayJson::getArray(*object, "inputs")) {
        return ReplayFormat::GdrJson;
    }
    if (ReplayJson::getFloat<double>(*object, "fps") && ReplayJson::getArray(*object, "inputs")) {
        return ReplayFormat::Echo;
    }
    if (endsWith(filename, ".mhr.json")) return ReplayFormat::MegaHackJson;
    if (endsWith(filename, ".echo.json")) return ReplayFormat::Echo;
    if (endsWith(filename, ".gdr.json")) return ReplayFormat::GdrJson;
    return ReplayFormat::TasBotJson;
}

static ReplayFormat guessByExtension(std::filesystem::path const& path) {
    auto filename = lowerCopy(toasty::pathToUtf8(path.filename()));
    auto ext = lowerCopy(toasty::pathToUtf8(path.extension()));
    if (endsWith(filename, ".mhr.json")) return ReplayFormat::MegaHackJson;
    if (endsWith(filename, ".echo.json")) return ReplayFormat::Echo;
    if (endsWith(filename, ".gdr.json")) return ReplayFormat::GdrJson;
    if (ext == ".json") return ReplayFormat::TasBotJson;
    if (ext == ".mhr") return ReplayFormat::MegaHackBinary;
    if (ext == ".zbf") return ReplayFormat::ZBotFrame;
    if (ext == ".replay") return ReplayFormat::OmegaBot;
    if (ext == ".ybf") return ReplayFormat::YBotFrame;
    if (ext == ".ybot") return ReplayFormat::YBot2;
    if (ext == ".echo") return ReplayFormat::Echo;
    if (ext == ".thyst") return ReplayFormat::Amethyst;
    if (ext == ".osr") return ReplayFormat::OsuReplay;
    if (ext == ".macro") return ReplayFormat::GDMO;
    if (ext == ".replaybot") return ReplayFormat::ReplayBot;
    if (ext == ".rsh") return ReplayFormat::Rush;
    if (ext == ".kd") return ReplayFormat::KDBot;
    if (ext == ".txt") return ReplayFormat::Plaintext;
    if (ext == ".re") return ReplayFormat::ReplayEngine1;
    if (ext == ".ddhor") return ReplayFormat::DDHOR;
    if (ext == ".xbot") return ReplayFormat::XBotFrame;
    if (ext == ".xd") return ReplayFormat::XdBot;
    if (ext == ".qb") return ReplayFormat::QBot;
    if (ext == ".rbot") return ReplayFormat::RBot;
    if (ext == ".zr") return ReplayFormat::Zephyrus;
    if (ext == ".re2") return ReplayFormat::ReplayEngine2;
    if (ext == ".re3") return ReplayFormat::ReplayEngine3;
    if (ext == ".slc") return ReplayFormat::Silicate1;
    if (ext == ".slc2") return ReplayFormat::Silicate2;
    if (ext == ".slc3") return ReplayFormat::Silicate3;
    if (ext == ".gdr2") return ReplayFormat::GDR2;
    if (ext == ".uv") return ReplayFormat::UvBot;
    if (ext == ".tcm") return ReplayFormat::TCBot;
    return ReplayFormat::Unknown;
}

static ReplayFormat detectContent(std::filesystem::path const& path, std::vector<uint8_t> const& data) {
    auto filename = lowerCopy(toasty::pathToUtf8(path.filename()));
    if (hasMagic(data, std::string_view("TTR\0", 4))) return ReplayFormat::Unknown;
    if (hasMagic(data, "GDR")) return ReplayFormat::GDR2;
    if (hasMagic(data, "SILL")) return ReplayFormat::Silicate2;
    if (hasMagic(data, "SLC3")) return ReplayFormat::Silicate3;
    if (hasMagic(data, "META")) return ReplayFormat::Echo;
    if (hasMagic(data, "UVBOT")) return ReplayFormat::UvBot;
    if (data.size() >= 16 && std::memcmp(data.data(), kTCMHeader, 16) == 0) return ReplayFormat::TCBot;
    if (hasMagic(data, "RPLY")) return ReplayFormat::ReplayBot;
    if (hasMagic(data, "DDHR")) return ReplayFormat::DDHOR;
    if (hasMagic(data, "RE2")) return ReplayFormat::ReplayEngine2;
    if (hasMagic(data, "ybot")) return ReplayFormat::YBot2;
    if (data.size() >= 4 && data[0] == 'H' && data[1] == 'A' && data[2] == 'C' && data[3] == 'K') {
        return ReplayFormat::MegaHackBinary;
    }
    if (data.size() >= 3 && data[0] == 'Z' && data[1] == 'R' && data[2] == 2) {
        return ReplayFormat::Zephyrus;
    }
    if (auto json = parseJson(data)) {
        return detectJsonFormat(*json, filename);
    }
    return ReplayFormat::Unknown;
}

static ImportedReplay parseMegaHackJson(std::vector<uint8_t> const& data) {
    auto json = parseJson(data);
    if (!json) throw FormatError("invalid Mega Hack JSON");
    auto const* object = ReplayJson::asObject(*json);
    if (!object) throw FormatError("Mega Hack JSON root is not an object");
    auto const* meta = ReplayJson::getObject(*object, "meta");
    auto const* events = ReplayJson::getArray(*object, "events");
    if (!meta || !events) throw FormatError("Mega Hack JSON missing meta/events");

    ImportedReplay replay;
    replay.format = ReplayFormat::MegaHackJson;
    replay.fps = ReplayJson::getFloat<double>(*meta, "fps").value_or(240.0);
    replay.levelName = ReplayJson::getString(*meta, "level_name").value_or("");
    replay.levelId = ReplayJson::getInteger<int32_t>(*meta, "level_id").value_or(0);

    for (auto const& entry : *events) {
        auto const* event = ReplayJson::asObject(entry);
        if (!event) continue;
        auto frame = ReplayJson::getInteger<int64_t>(*event, "frame");
        auto down = ReplayJson::getBool(*event, "down");
        if (!frame || !down) continue;
        bool player2 = ReplayJson::getBool(*event, "p2").value_or(false);
        int button = ReplayJson::getInteger<int>(*event, "button").value_or(1);
        addInput(replay, static_cast<double>(*frame) / replay.fps, *frame, button, player2, *down);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseTasBotJson(std::vector<uint8_t> const& data) {
    auto json = parseJson(data);
    if (!json) throw FormatError("invalid TASBot JSON");
    auto const* object = ReplayJson::asObject(*json);
    if (!object) throw FormatError("TASBot JSON root is not an object");
    if (ReplayJson::getObject(*object, "meta") && ReplayJson::getArray(*object, "events")) {
        return parseMegaHackJson(data);
    }
    auto const* macro = ReplayJson::getArray(*object, "macro");
    if (!macro) throw FormatError("TASBot JSON missing macro array");

    ImportedReplay replay;
    replay.format = ReplayFormat::TasBotJson;
    replay.fps = ReplayJson::getFloat<double>(*object, "fps").value_or(240.0);
    int previousP1 = 0;
    int previousP2 = 0;

    for (auto const& entry : *macro) {
        auto const* event = ReplayJson::asObject(entry);
        if (!event) continue;
        auto frame = ReplayJson::getInteger<int64_t>(*event, "frame");
        if (!frame) continue;
        auto const* p1 = ReplayJson::getObject(*event, "player_1");
        auto const* p2 = ReplayJson::getObject(*event, "player_2");
        int p1Click = p1 ? ReplayJson::getInteger<int>(*p1, "click").value_or(0) : 0;
        int p2Click = p2 ? ReplayJson::getInteger<int>(*p2, "click").value_or(0) : 0;

        if (p1Click != 0) {
            if (p1Click == 1 && previousP1 == 1) {
                addInput(replay, static_cast<double>(*frame) / replay.fps, *frame, 1, false, false);
            }
            addInput(replay, static_cast<double>(*frame) / replay.fps, *frame, 1, false, p1Click == 1);
        }
        if (p2Click != 0) {
            if (p2Click == 1 && previousP2 == 1) {
                addInput(replay, static_cast<double>(*frame) / replay.fps, *frame, 1, true, false);
            }
            addInput(replay, static_cast<double>(*frame) / replay.fps, *frame, 1, true, p2Click == 1);
        }
        previousP1 = p1Click;
        previousP2 = p2Click;
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseMegaHackBinary(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    if (reader.u32be() != 0x4841434b) throw FormatError("invalid Mega Hack binary magic");
    reader.seek(12);

    ImportedReplay replay;
    replay.format = ReplayFormat::MegaHackBinary;
    replay.fps = static_cast<double>(reader.u32le());
    reader.seek(28);
    uint32_t count = reader.u32le();
    if (count > data.size() / 32 + 1) throw FormatError("invalid Mega Hack action count");

    for (uint32_t i = 0; i < count; ++i) {
        reader.skip(2);
        bool down = reader.u8() == 1;
        bool player1 = reader.u8() == 0;
        uint32_t frame = reader.u32le();
        reader.skip(24);
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, !player1, down);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseZBotFrame(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::ZBotFrame;
    float delta = reader.f32le();
    float speedhack = reader.f32le();
    if (!std::isfinite(speedhack) || speedhack == 0.0f) speedhack = 1.0f;
    replay.fps = 1.0 / static_cast<double>(delta) / static_cast<double>(speedhack);

    while (reader.remaining() >= 6) {
        int32_t frame = reader.i32le();
        bool down = reader.u8() == '1';
        bool player1 = reader.u8() == '1';
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, !player1, down);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseYBotFrame(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::YBotFrame;
    replay.fps = reader.f32le();
    int32_t count = reader.i32le();
    if (count < 0 || static_cast<size_t>(count) > reader.remaining() / 8 + 1) {
        throw FormatError("invalid yBot frame action count");
    }
    for (int32_t i = 0; i < count; ++i) {
        uint32_t frame = reader.u32le();
        uint32_t state = reader.u32le();
        bool down = (state & 0b10) != 0;
        bool player2 = (state & 0b01) != 0;
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, player2, down);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseYBot2(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    if (!hasMagic(data, "ybot")) throw FormatError("invalid yBot2 magic");
    reader.skip(4);
    reader.u32le();
    uint32_t metaLength = reader.u32le();
    uint32_t blobs = reader.u32le();
    if (16ull + metaLength > data.size()) throw FormatError("invalid yBot2 metadata length");

    ImportedReplay replay;
    replay.format = ReplayFormat::YBot2;
    if (metaLength >= 28) {
        ByteReader metaReader(data, 16 + 24);
        replay.fps = metaReader.f32le();
    } else {
        replay.fps = 240.0;
    }
    ImportTimeline timeline(replay.fps);
    replay.fps = timeline.initialFps();

    reader.seek(16 + metaLength);
    for (uint32_t i = 0; i < blobs; ++i) {
        uint32_t blobLength = reader.u32le();
        reader.skip(blobLength);
    }

    uint64_t frame = 0;
    while (!reader.eof()) {
        uint64_t value = reader.varU64();
        uint8_t flags = static_cast<uint8_t>(value & 0x0f);
        frame += value >> 4;
        if (flags == 0x0f) {
            timeline.changeFps(replay, static_cast<double>(frame), reader.f32le());
            continue;
        }
        bool player1 = (flags & 1) != 0;
        bool pressed = (flags & 2) != 0;
        int button = static_cast<int>(flags >> 2);
        if (button < 1 || button > 3) continue;
        addInput(replay, timeline.timeForFrame(static_cast<double>(frame)), static_cast<double>(frame), button, !player1, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseXdBot(std::vector<uint8_t> const& data) {
    auto lines = splitLines(bytesToText(data));
    ImportedReplay replay;
    replay.format = ReplayFormat::XdBot;
    replay.fps = 240.0;

    for (auto const& line : lines) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::stringstream stream(line);
        std::string part;
        while (std::getline(stream, part, '|')) parts.push_back(part);
        if (parts.size() == 1) {
            replay.fps = std::stod(parts[0]);
            continue;
        }
        if (parts.size() < 4) continue;
        int64_t frame = std::stoll(parts[0]);
        bool pressed = std::stoi(parts[1]) == 1;
        int button = std::stoi(parts[2]);
        bool player1 = std::stoi(parts[3]) == 1;
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, button, !player1, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseXBotFrame(std::vector<uint8_t> const& data) {
    auto lines = splitLines(bytesToText(data));
    if (lines.size() < 2 || lowerCopy(lines[1]) != "frames") {
        throw FormatError("xBot file is not frame format");
    }

    ImportedReplay replay;
    replay.format = ReplayFormat::XBotFrame;
    replay.fps = std::stod(lines[0]);

    for (size_t i = 2; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        std::stringstream stream(lines[i]);
        int state = 0;
        int64_t frame = 0;
        stream >> state >> frame;
        if (!stream) continue;
        bool player2 = state > 1;
        bool pressed = state % 2 == 1;
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, player2, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parsePlaintext(std::vector<uint8_t> const& data) {
    auto lines = splitLines(bytesToText(data));
    if (lines.empty()) throw FormatError("empty plaintext macro");

    ImportedReplay replay;
    replay.format = ReplayFormat::Plaintext;
    replay.fps = std::stod(lines[0]);

    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        std::stringstream stream(lines[i]);
        double frame = 0.0;
        int down = 0;
        int button = 1;
        int p2Flag = 1;
        stream >> frame >> down >> button >> p2Flag;
        if (!stream) continue;
        bool player2 = p2Flag == 0;
        addInput(replay, frame / replay.fps, static_cast<int64_t>(std::llround(frame)), button, player2, down == 1);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseAmethyst(std::vector<uint8_t> const& data) {
    auto lines = splitLines(bytesToText(data));
    size_t cursor = 0;
    ImportedReplay replay;
    replay.format = ReplayFormat::Amethyst;
    replay.fps = 240.0;

    auto readGroup = [&](bool player2, bool pressed) {
        if (cursor >= lines.size()) throw FormatError("unexpected end of Amethyst replay");
        int count = std::stoi(lines[cursor++]);
        for (int i = 0; i < count; ++i) {
            if (cursor >= lines.size()) throw FormatError("unexpected end of Amethyst replay");
            double time = std::stod(lines[cursor++]);
            addInput(replay, time, static_cast<int64_t>(std::llround(time * replay.fps)), 1, player2, pressed);
        }
    };

    readGroup(false, true);
    readGroup(false, false);
    readGroup(true, true);
    readGroup(true, false);
    finishImport(replay);
    return replay;
}

static ImportedReplay parseEchoBinary(std::vector<uint8_t> const& data) {
    if (data.size() < 48) throw FormatError("Echo binary file is too small");
    ByteReader reader(data);
    if (reader.u32be() != 0x4d455441) throw FormatError("invalid Echo binary magic");
    uint32_t replayType = reader.u32be();
    size_t actionSize = replayType == 0x44424700u ? 24u : 6u;

    ImportedReplay replay;
    replay.format = ReplayFormat::Echo;
    reader.seek(24);
    replay.fps = static_cast<double>(reader.f32le());
    reader.seek(48);

    while (reader.remaining() >= actionSize) {
        size_t start = reader.position();
        uint32_t frame = reader.u32le();
        bool down = reader.u8() == 1;
        bool player1 = reader.u8() == 0;
        reader.seek(start + actionSize);
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, !player1, down);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseEchoJson(gdr::json const& json) {
    auto const* object = ReplayJson::asObject(json);
    if (!object) throw FormatError("Echo JSON root is not an object");

    ImportedReplay replay;
    replay.format = ReplayFormat::Echo;

    if (auto const* echoReplay = ReplayJson::getArray(*object, "Echo Replay")) {
        replay.fps = ReplayJson::getFloat<double>(*object, "FPS").value_or(240.0);
        int64_t startingFrame = ReplayJson::getInteger<int64_t>(*object, "Starting Frame").value_or(0);
        for (auto const& entry : *echoReplay) {
            auto const* action = ReplayJson::asObject(entry);
            if (!action) continue;
            auto frame = ReplayJson::getInteger<int64_t>(*action, "Frame");
            auto hold = ReplayJson::getBool(*action, "Hold");
            if (!frame || !hold) continue;
            bool player2 = ReplayJson::getBool(*action, "Player 2").value_or(false);
            int64_t resolved = *frame + startingFrame;
            addInput(replay, static_cast<double>(resolved) / replay.fps, static_cast<double>(resolved), 1, player2, *hold);
        }
        finishImport(replay);
        return replay;
    }

    auto const* inputs = ReplayJson::getArray(*object, "inputs");
    if (!inputs) throw FormatError("Echo JSON is missing the 'inputs'/'Echo Replay' array");
    replay.fps = ReplayJson::getFloat<double>(*object, "fps").value_or(240.0);
    for (auto const& entry : *inputs) {
        auto const* action = ReplayJson::asObject(entry);
        if (!action) continue;
        auto frame = ReplayJson::getInteger<int64_t>(*action, "frame");
        auto hold = ReplayJson::getBool(*action, "holding");
        if (!frame || !hold) continue;
        bool player2 = ReplayJson::getBool(*action, "player_2").value_or(false);
        addInput(replay, static_cast<double>(*frame) / replay.fps, static_cast<double>(*frame), 1, player2, *hold);
    }
    finishImport(replay);
    return replay;
}

static ImportedReplay parseEcho(std::vector<uint8_t> const& data) {
    if (auto json = parseJson(data)) {
        return parseEchoJson(*json);
    }
    return parseEchoBinary(data);
}

static uint32_t readLEB128(ByteReader& reader) {
    uint32_t value = 0;
    uint32_t shift = 0;
    while (reader.remaining() > 0) {
        uint8_t byte = reader.u8();
        value |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return value;
        shift += 7;
        if (shift >= 35) throw FormatError("LEB128 overflow");
    }
    throw FormatError("unexpected end of LEB128");
}

static ImportedReplay parseTCBot(std::vector<uint8_t> const& data) {
    if (data.size() < 16 + 0x40) throw FormatError("TCM file too small");
    if (std::memcmp(data.data(), kTCMHeader, 16) != 0) throw FormatError("invalid TCM header");

    ByteReader reader(data);
    reader.skip(16);

    uint8_t version = reader.u8();
    reader.skip(3);
    float tps = reader.f32le();
    reader.skip(0x40 - 8);

    ImportedReplay replay;
    replay.format = ReplayFormat::TCBot;
    replay.fps = static_cast<double>(tps);

    if (version == 1) {
        uint32_t count = readLEB128(reader);
        if (count > 1000000) throw FormatError("TCM v1 action count too large");
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t frame = readLEB128(reader);
            uint8_t inputByte = reader.u8();
            uint8_t inputType = inputByte & 0x07;
            if (inputType >= 3) continue;
            bool pressed = (inputByte & 0x80) != 0;
            bool player2 = (inputByte & 0x40) != 0;
            int button = static_cast<int>(inputType) + 1;
            addInput(replay, static_cast<double>(frame) / replay.fps, frame, button, player2, pressed);
        }
    } else if (version == 2) {
        uint32_t currentFrame = readLEB128(reader);
        uint64_t lastDelta = 0;

        while (reader.remaining() > 0) {
            uint8_t byte = reader.u8();
            uint8_t deltaData = (byte >> 5) & 0x07;
            uint8_t inputData = byte & 0x03;
            bool deltaMagic = (deltaData & 1) != 0;
            uint8_t deltaBlob = (deltaData >> 1) & 0x03;

            if (inputData > 0) {
                bool pressed = (byte & 0x04) != 0;
                bool player2 = (byte & 0x08) != 0;
                bool swift = (byte & 0x10) != 0;
                int button = static_cast<int>(inputData);
                addInput(replay, static_cast<double>(currentFrame) / replay.fps, currentFrame, button, player2, pressed);
                if (swift) {
                    addInput(replay, static_cast<double>(currentFrame) / replay.fps, currentFrame, button, player2, !pressed);
                }
            } else {
                uint8_t customType = (byte >> 2) & 0x03;
                bool extra = (byte & 0x10) != 0;
                if (customType == 3) {
                    if (!extra) {
                        if (reader.remaining() < 4) break;
                        float newTps = reader.f32le();
                        replay.fps = static_cast<double>(newTps);
                    }
                } else {
                    if (extra) {
                        if (reader.remaining() < 8) break;
                        reader.skip(8);
                    }
                    currentFrame = 0;
                }
            }

            uint64_t delta = 0;
            if (deltaBlob == 0) {
                delta = deltaMagic ? lastDelta : 0;
            } else {
                uint64_t rawValue = 0;
                if (deltaBlob == 1) {
                    if (reader.remaining() < 1) break;
                    rawValue = reader.u8();
                } else if (deltaBlob == 2) {
                    if (reader.remaining() < 2) break;
                    rawValue = reader.u16le();
                } else {
                    if (reader.remaining() < 4) break;
                    rawValue = reader.u32le();
                }
                delta = (deltaMagic ? lastDelta : 0) + rawValue;
            }
            if (delta != 0) lastDelta = delta;
            currentFrame += static_cast<uint32_t>(delta);
        }
    } else {
        throw FormatError("unsupported TCM version " + std::to_string(version));
    }

    finishImport(replay);
    return replay;
}

static uint64_t readLEBytes(ByteReader& reader, size_t byteSize) {
    if (byteSize == 0 || byteSize > 8) throw FormatError("invalid byte field size");
    uint64_t value = 0;
    for (size_t i = 0; i < byteSize; ++i) {
        value |= static_cast<uint64_t>(reader.u8()) << (i * 8);
    }
    return value;
}

static void addSilicatePlayerInput(
    ImportedReplay& replay,
    ImportTimeline& timeline,
    uint64_t frame,
    int button,
    bool player2,
    bool pressed,
    bool swift = false
) {
    addInput(
        replay,
        timeline.timeForFrame(static_cast<double>(frame)),
        static_cast<double>(frame),
        button,
        player2,
        pressed,
        swift
    );
}

static void readSilicatePackedInput(ImportedReplay& replay, ImportTimeline& timeline, uint32_t packed) {
    uint32_t frame = packed >> 4;
    bool player2 = (packed & 0b1000) != 0;
    bool pressed = (packed & 0b0001) != 0;
    int button = static_cast<int>((packed & 0b0110) >> 1);
    addSilicatePlayerInput(replay, timeline, frame, button, player2, pressed);
}

static void readSilicateV2Input(
    ByteReader& reader,
    ImportedReplay& replay,
    ImportTimeline& timeline,
    uint64_t& currentFrame,
    size_t byteSize
) {
    uint64_t state = readLEBytes(reader, byteSize);
    uint64_t delta = state >> 5;
    currentFrame += delta;
    int button = static_cast<int>((state & 0b11100) >> 2);
    if (button >= 1 && button <= 3) {
        bool pressed = (state & 1) != 0;
        bool player2 = (state & 2) != 0;
        addSilicatePlayerInput(replay, timeline, currentFrame, button, player2, pressed);
    } else if (button == 7) {
        double tps = reader.f64le();
        timeline.changeFps(replay, static_cast<double>(currentFrame), tps);
    }
}

struct Silicate3PlayerInput {
    uint64_t delta = 0;
    int button = 1;
    bool pressed = false;
    bool player2 = false;
    bool swift = false;
};

static Silicate3PlayerInput readSilicate3PlayerInput(ByteReader& reader, size_t byteSize) {
    uint64_t state = readLEBytes(reader, byteSize);
    int rawButton = static_cast<int>((state >> 2) & 0b11);
    Silicate3PlayerInput input;
    input.delta = state >> 4;
    input.swift = rawButton == 0;
    input.button = rawButton == 0 ? 1 : rawButton;
    input.pressed = (state & 1) != 0;
    input.player2 = (state & 0b10) != 0;
    return input;
}

static void addSilicate3PlayerInput(
    ImportedReplay& replay,
    ImportTimeline& timeline,
    uint64_t& currentFrame,
    Silicate3PlayerInput const& input,
    uint64_t& actionCount
) {
    currentFrame += input.delta;
    if (input.swift) {
        addSilicatePlayerInput(replay, timeline, currentFrame, 1, input.player2, true, true);
        addSilicatePlayerInput(replay, timeline, currentFrame, 1, input.player2, false, true);
        actionCount += 2;
        return;
    }
    addSilicatePlayerInput(replay, timeline, currentFrame, input.button, input.player2, input.pressed);
    ++actionCount;
}

static void readSilicate3Section(
    ByteReader& reader,
    ImportedReplay& replay,
    ImportTimeline& timeline,
    uint64_t& currentFrame,
    uint64_t& actionsRead,
    uint64_t actionCount
) {
    uint16_t header = reader.u16le();
    uint16_t sectionType = header >> 14;
    if (sectionType == 0) {
        size_t byteSize = static_cast<size_t>(1u << ((header >> 12) & 0b11));
        uint64_t length = 1ull << ((header >> 8) & 0b1111);
        for (uint64_t i = 0; i < length && actionsRead < actionCount; ++i) {
            auto input = readSilicate3PlayerInput(reader, byteSize);
            addSilicate3PlayerInput(replay, timeline, currentFrame, input, actionsRead);
        }
    } else if (sectionType == 1) {
        size_t byteSize = static_cast<size_t>(1u << ((header >> 12) & 0b11));
        uint64_t length = 1ull << ((header >> 8) & 0b1111);
        uint64_t repeats = 1ull << ((header >> 3) & 0b11111);
        std::vector<Silicate3PlayerInput> inputs;
        inputs.reserve(static_cast<size_t>(std::min<uint64_t>(length, 65536)));
        for (uint64_t i = 0; i < length; ++i) {
            auto input = readSilicate3PlayerInput(reader, byteSize);
            inputs.push_back(input);
        }
        for (uint64_t r = 0; r < repeats && actionsRead < actionCount; ++r) {
            for (auto const& input : inputs) {
                if (actionsRead >= actionCount) break;
                addSilicate3PlayerInput(replay, timeline, currentFrame, input, actionsRead);
            }
        }
    } else if (sectionType == 2) {
        size_t byteSize = static_cast<size_t>(1u << ((header >> 8) & 0b11));
        uint16_t specialType = (header >> 10) & 0b1111;
        uint64_t delta = readLEBytes(reader, byteSize);
        currentFrame += delta;
        if (specialType == 3) {
            double tps = reader.f64le();
            timeline.changeFps(replay, static_cast<double>(currentFrame), tps);
        } else if (specialType <= 2) {
            reader.u64le();
        } else if (specialType == 4) {
        } else {
            throw FormatError("invalid Silicate 3 special section");
        }
        ++actionsRead;
    } else {
        throw FormatError("invalid Silicate 3 section");
    }
}

static ImportedReplay parseSilicate2(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "SILL")) throw FormatError("invalid Silicate 2 magic");
    ByteReader reader(data);
    reader.skip(4);

    ImportedReplay replay;
    replay.format = ReplayFormat::Silicate2;
    replay.fps = reader.f64le();
    ImportTimeline timeline(replay.fps);

    uint64_t metaSize = reader.u64le();
    if (metaSize > reader.remaining()) throw FormatError("invalid Silicate 2 meta size");
    reader.skip(static_cast<size_t>(metaSize));

    uint64_t inputCount = reader.u64le();
    uint64_t blobCount = reader.u64le();
    if (blobCount > 1000000) throw FormatError("invalid Silicate 2 blob count");

    struct BlobInfo {
        uint64_t byteSize = 0;
        uint64_t start = 0;
        uint64_t length = 0;
    };
    std::vector<BlobInfo> blobs;
    blobs.reserve(static_cast<size_t>(blobCount));
    for (uint64_t i = 0; i < blobCount; ++i) {
        BlobInfo blob;
        blob.byteSize = reader.u64le();
        blob.start = reader.u64le();
        blob.length = reader.u64le();
        if (blob.byteSize == 0 || blob.byteSize > 8 || blob.start > inputCount || blob.length > inputCount) {
            throw FormatError("invalid Silicate 2 blob");
        }
        blobs.push_back(blob);
    }

    uint64_t currentFrame = 0;
    uint64_t readCount = 0;
    for (auto const& blob : blobs) {
        for (uint64_t i = 0; i < blob.length; ++i) {
            if (readCount >= inputCount) throw FormatError("invalid Silicate 2 input table");
            readSilicateV2Input(reader, replay, timeline, currentFrame, static_cast<size_t>(blob.byteSize));
            ++readCount;
        }
    }

    if (readCount != inputCount) throw FormatError("invalid Silicate 2 input count");
    if (reader.remaining() < 3 || reader.u8() != 'E' || reader.u8() != 'O' || reader.u8() != 'M') {
        throw FormatError("invalid Silicate 2 footer");
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseSilicate3(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "SLC3")) throw FormatError("invalid Silicate 3 magic");
    ByteReader reader(data);
    if (data.size() >= 8 && std::memcmp(data.data(), "SLC3RPLY", 8) == 0) {
        reader.skip(8);

        uint16_t metaSize = reader.u16le();
        if (metaSize < 64 || metaSize > reader.remaining()) throw FormatError("invalid Silicate 3 metadata size");

        ImportedReplay replay;
        replay.format = ReplayFormat::Silicate3;
        replay.fps = reader.f64le();
        reader.skip(static_cast<size_t>(metaSize) - 8);
        ImportTimeline timeline(replay.fps);

        if (reader.remaining() < 1) throw FormatError("invalid Silicate 3 footer");
        size_t atomEnd = data.size() - 1;
        uint64_t currentFrame = 0;

        while (reader.position() < atomEnd) {
            if (atomEnd - reader.position() < 12) throw FormatError("invalid Silicate 3 atom");
            uint32_t atomId = reader.u32le();
            uint64_t atomSize = reader.u64le();
            atomSize &= ~(0xFFull << 56);
            if (atomId == 1) {
                uint64_t actionCount = reader.u64le();
                uint64_t actionsRead = 0;
                while (actionsRead < actionCount && reader.position() < atomEnd) {
                    readSilicate3Section(reader, replay, timeline, currentFrame, actionsRead, actionCount);
                }
                if (actionsRead != actionCount) throw FormatError("invalid Silicate 3 action count");
            } else {
                if (atomSize > static_cast<uint64_t>(atomEnd - reader.position())) throw FormatError("invalid Silicate 3 atom size");
                reader.skip(static_cast<size_t>(atomSize));
            }
        }

        if (reader.u8() != 0xCC) throw FormatError("invalid Silicate 3 footer");
        finishImport(replay);
        return replay;
    }

    reader.skip(4);

    ImportedReplay replay;
    replay.format = ReplayFormat::Silicate3;
    replay.fps = reader.f64le();
    ImportTimeline timeline(replay.fps);
    uint32_t atomCount = reader.u32le();

    for (uint32_t a = 0; a < atomCount && reader.remaining() > 0; ++a) {
        uint8_t atomType = reader.u8();
        uint32_t atomSize = reader.u32le();
        size_t payloadStart = reader.position();
        if (atomSize > reader.remaining()) throw FormatError("invalid Silicate 3 atom size");

        if (atomType == 1) {
            uint32_t count = reader.u32le();
            for (uint32_t i = 0; i < count && reader.position() + 4 <= payloadStart + atomSize; ++i) {
                uint32_t packed = reader.u32le();
                readSilicatePackedInput(replay, timeline, packed);
            }
        }
        reader.seek(payloadStart + atomSize);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseSilicate1(std::vector<uint8_t> const& data) {
    if (hasMagic(data, "SILL")) return parseSilicate2(data);
    if (hasMagic(data, "SLC3")) return parseSilicate3(data);

    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::Silicate1;
    replay.fps = reader.f64le();
    uint32_t count = reader.u32le();
    if (count > reader.remaining() / 4 + 1) throw FormatError("invalid Silicate action count");
    ImportTimeline timeline(replay.fps);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t packed = reader.u32le();
        readSilicatePackedInput(replay, timeline, packed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseReplayBot(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "RPLY")) throw FormatError("invalid ReplayBot magic");
    ByteReader reader(data);
    reader.skip(4);
    uint8_t version = reader.u8();
    if (version != 2) throw FormatError("only ReplayBot v2 frame replays are supported");
    if (reader.u8() != 1) throw FormatError("ReplayBot replay is not frame-based");

    ImportedReplay replay;
    replay.format = ReplayFormat::ReplayBot;
    replay.fps = reader.f32le();
    while (reader.remaining() >= 5) {
        uint32_t frame = reader.u32le();
        uint8_t state = reader.u8();
        bool pressed = (state & 1) != 0;
        bool player2 = (state >> 1) != 0;
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, player2, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseRush(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::Rush;
    replay.fps = static_cast<double>(reader.i16le());
    while (reader.remaining() >= 5) {
        int32_t frame = reader.i32le();
        uint8_t state = reader.u8();
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, (state >> 1) != 0, (state & 1) != 0);
    }
    finishImport(replay);
    return replay;
}

static ImportedReplay parseKDBot(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::KDBot;
    replay.fps = reader.f32le();
    while (reader.remaining() >= 6) {
        int32_t frame = reader.i32le();
        bool pressed = reader.u8() == 1;
        bool player2 = reader.u8() == 1;
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, player2, pressed);
    }
    finishImport(replay);
    return replay;
}

static std::optional<std::vector<uint8_t>> inflateGzip(std::vector<uint8_t> const& data) {
    z_stream stream {};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<Bytef const*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) return std::nullopt;

    std::vector<uint8_t> output;
    std::array<uint8_t, 8192> buffer {};
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        size_t produced = buffer.size() - stream.avail_out;
        output.insert(output.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) return std::nullopt;
    return output;
}

static ImportedReplay parseRBot(std::vector<uint8_t> const& data) {
    std::vector<uint8_t> uncompressed;
    std::vector<uint8_t> const* source = &data;
    if (data.size() >= 2 && data[0] == 0x1f && data[1] == 0x8b) {
        auto inflated = inflateGzip(data);
        if (!inflated) throw FormatError("failed to decompress RBot gzip replay");
        uncompressed = std::move(*inflated);
        source = &uncompressed;
    }

    ByteReader reader(*source);
    ImportedReplay replay;
    replay.format = ReplayFormat::RBot;
    replay.fps = static_cast<double>(reader.u32le());
    uint32_t count = reader.u32le();
    if (count > reader.remaining() / 6 + 1) throw FormatError("invalid RBot action count");
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t frame = reader.u32le();
        bool pressed = reader.u8() != 0;
        bool player1 = reader.u8() != 0;
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, !player1, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseDDHOR(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "DDHR")) throw FormatError("invalid DDHOR magic");
    ByteReader reader(data);
    reader.skip(4);

    ImportedReplay replay;
    replay.format = ReplayFormat::DDHOR;
    replay.fps = static_cast<double>(reader.i16le());
    int32_t p1Count = reader.i32le();
    int32_t p2Count = reader.i32le();
    int32_t total = std::max(0, p1Count) + std::max(0, p2Count);
    if (total > static_cast<int32_t>(reader.remaining() / 5 + 1)) throw FormatError("invalid DDHOR action count");
    for (int32_t i = 0; i < total; ++i) {
        float frame = reader.f32le();
        bool pressed = reader.u8() == 0;
        bool player2 = i >= p1Count;
        addInput(replay, static_cast<double>(frame) / replay.fps, static_cast<int64_t>(std::llround(frame)), 1, player2, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseZephyrus(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    if (reader.u16le() != 0x525a) throw FormatError("invalid Zephyrus magic");
    uint8_t version = reader.u8();
    if (version != 2) throw FormatError("unsupported Zephyrus version");

    ImportedReplay replay;
    replay.format = ReplayFormat::Zephyrus;
    replay.fps = static_cast<double>(reader.u32le());
    uint32_t actionCount = reader.u32le();
    uint32_t frameFixCount = reader.u32le();
    if (actionCount > reader.remaining() / 5 + 1) throw FormatError("invalid Zephyrus action count");
    for (uint32_t i = 0; i < actionCount; ++i) {
        uint32_t frame = reader.u32le();
        uint8_t flags = reader.u8();
        bool player2 = (flags & 0b10000000) != 0;
        bool pressed = (flags & 0b01000000) != 0;
        int button = static_cast<int>((flags & 0b00110000) >> 4);
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, button, player2, pressed);
    }
    for (uint32_t i = 0; i < frameFixCount; ++i) {
        if (reader.remaining() < 25) {
            throw FormatError("invalid Zephyrus frame-fix data");
        }
        uint32_t frame = reader.u32le();
        auto player1 = readZephyrusPlayerState(reader, replay);
        bool hasPlayer2 = reader.u8() != 0;
        std::optional<PlayerStateBundle> player2;
        if (hasPlayer2) {
            if (reader.remaining() < 20) {
                throw FormatError("invalid Zephyrus player 2 frame-fix data");
            }
            player2 = readZephyrusPlayerState(reader, replay);
        }
        addAnchor(
            replay,
            static_cast<double>(frame) / safeFps(replay.fps),
            frame,
            std::optional<PlayerStateBundle> { std::move(player1) },
            std::move(player2)
        );
    }
    if (frameFixCount != 0) {
        addWarningOnce(replay, "Converted Zephyrus frame-fix physics into Toasty anchors.");
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseReplayEngine2(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "RE2")) throw FormatError("invalid ReplayEngine 2 magic");
    ByteReader reader(data);
    reader.skip(3);

    ImportedReplay replay;
    replay.format = ReplayFormat::ReplayEngine2;
    replay.fps = 240.0;
    uint32_t count = reader.u32le();
    if (count > reader.remaining() / 16 + 1) throw FormatError("invalid ReplayEngine 2 action count");
    for (uint32_t i = 0; i < count; ++i) {
        size_t start = reader.position();
        uint32_t frame = reader.u32le();
        bool pressed = reader.u8() != 0;
        reader.seek(start + 8);
        int32_t button = reader.i32le();
        bool player2 = reader.u8() != 0;
        reader.seek(start + 16);
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, button, player2, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseReplayEngine3(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::ReplayEngine3;
    replay.fps = reader.f32le();
    uint32_t p1FrameCount = reader.u32le();
    uint32_t p2FrameCount = reader.u32le();
    uint32_t p1InputCount = reader.u32le();
    uint32_t p2InputCount = reader.u32le();
    size_t frameDataBytes = (static_cast<size_t>(p1FrameCount) + p2FrameCount) * 32;
    if (frameDataBytes > reader.remaining()) throw FormatError("invalid ReplayEngine 3 frame data");
    for (uint32_t i = 0; i < p1FrameCount + p2FrameCount; ++i) {
        readReplayEngineFrameAnchor(reader, replay);
    }

    auto readActions = [&](uint32_t count, bool player2) {
        for (uint32_t i = 0; i < count; ++i) {
            size_t start = reader.position();
            uint32_t frame = reader.u32le();
            bool pressed = reader.u8() != 0;
            reader.seek(start + 8);
            int32_t button = reader.i32le();
            reader.seek(start + 16);
            addInput(replay, static_cast<double>(frame) / replay.fps, frame, button, player2, pressed);
        }
    };
    readActions(p1InputCount, false);
    readActions(p2InputCount, true);

    if (p1FrameCount + p2FrameCount != 0) {
        addWarningOnce(replay, "Converted ReplayEngine physics frames into Toasty anchors.");
    }
    finishImport(replay);
    return replay;
}

static ImportedReplay parseReplayEngine1(std::vector<uint8_t> const& data) {
    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::ReplayEngine1;
    replay.fps = reader.f32le();
    uint32_t frameCount = reader.u32le();
    uint32_t actionCount = reader.u32le();
    size_t frameBytes = static_cast<size_t>(frameCount) * 32;
    if (frameBytes > reader.remaining()) throw FormatError("invalid ReplayEngine frame data");
    for (uint32_t i = 0; i < frameCount; ++i) {
        readReplayEngineFrameAnchor(reader, replay);
    }
    if (actionCount == 0) {
        finishImport(replay);
        return replay;
    }
    size_t actionSize = reader.remaining() / actionCount;
    if (actionSize != 8 && actionSize != 16) throw FormatError("unknown ReplayEngine action layout");
    for (uint32_t i = 0; i < actionCount; ++i) {
        size_t start = reader.position();
        uint32_t frame = reader.u32le();
        bool pressed = reader.u8() != 0;
        int32_t button = 1;
        bool player2 = false;
        if (actionSize == 16) {
            reader.seek(start + 8);
            button = reader.i32le();
            player2 = reader.u8() != 0;
        } else {
            player2 = reader.u8() != 0;
        }
        reader.seek(start + actionSize);
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, button, player2, pressed);
    }

    if (frameCount != 0) {
        addWarningOnce(replay, "Converted ReplayEngine physics frames into Toasty anchors.");
    }
    finishImport(replay);
    return replay;
}

static ImportedReplay parseGDR2(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "GDR")) throw FormatError("invalid GDR2 magic");
    ByteReader reader(data);
    reader.skip(3);
    if (reader.varI32() != 2) throw FormatError("unsupported GDR2 version");
    std::string inputTag = reader.gdr2String();
    bool hasExtension = !inputTag.empty();

    ImportedReplay replay;
    replay.format = ReplayFormat::GDR2;
    std::string author = reader.gdr2String();
    std::string description = reader.gdr2String();
    replay.duration = reader.f32be();
    reader.varI32();
    replay.fps = reader.f64be();
    reader.varI32();
    reader.varI32();
    reader.boolean();
    replay.platformerMode = reader.boolean();
    reader.gdr2String();
    reader.varI32();
    replay.levelId = reader.varI32();
    replay.levelName = reader.gdr2String();
    int32_t extSize = reader.varI32();
    if (extSize < 0) throw FormatError("invalid GDR2 extension size");
    reader.skip(static_cast<size_t>(extSize));
    if (extSize > 0 || hasExtension) {
        addWarningOnce(replay, "GDR2 extensions were imported as inputs only; custom physics data was skipped.");
    }

    int32_t deathCount = reader.varI32();
    if (deathCount < 0) throw FormatError("invalid GDR2 death count");
    for (int32_t i = 0; i < deathCount; ++i) {
        reader.varI32();
    }

    int32_t totalInputs = reader.varI32();
    int32_t p1Inputs = reader.varI32();
    if (totalInputs < 0 || p1Inputs < 0 || p1Inputs > totalInputs) {
        throw FormatError("invalid GDR2 input count");
    }

    auto readInputs = [&](int32_t count, bool player2) {
        uint64_t frame = 0;
        for (int32_t i = 0; i < count; ++i) {
            uint64_t packed = static_cast<uint64_t>(reader.varI32());
            int button = 1;
            bool pressed = false;
            if (replay.platformerMode) {
                frame += packed >> 3;
                button = static_cast<int>((packed >> 1) & 3);
                pressed = (packed & 1) != 0;
            } else {
                frame += packed >> 1;
                pressed = (packed & 1) != 0;
            }
            addInput(replay, static_cast<double>(frame) / replay.fps, static_cast<int64_t>(frame), button, player2, pressed);
            if (hasExtension) {
                int32_t inputExtSize = reader.varI32();
                if (inputExtSize < 0) throw FormatError("invalid GDR2 input extension size");
                reader.skip(static_cast<size_t>(inputExtSize));
            }
        }
    };
    readInputs(p1Inputs, false);
    readInputs(totalInputs - p1Inputs, true);

    finishImport(replay);
    return replay;
}

static ImportedReplay parseGdrJson(std::vector<uint8_t> const& data) {
    auto json = parseJson(data);
    if (!json) throw FormatError("invalid GDR JSON");
    auto const* object = ReplayJson::asObject(*json);
    if (!object) throw FormatError("GDR JSON root is not an object");
    auto const* inputs = ReplayJson::getArray(*object, "inputs");
    if (!inputs) throw FormatError("GDR JSON missing inputs array");

    ImportedReplay replay;
    replay.format = ReplayFormat::GdrJson;
    replay.fps = ReplayJson::getFloat<double>(*object, "framerate").value_or(240.0);
    if (auto const* level = ReplayJson::getObject(*object, "level")) {
        replay.levelId = ReplayJson::getInteger<int32_t>(*level, "id").value_or(0);
        replay.levelName = ReplayJson::getString(*level, "name").value_or(std::string{});
    }

    for (auto const& entry : *inputs) {
        auto const* action = ReplayJson::asObject(entry);
        if (!action) continue;
        auto frame = ReplayJson::getInteger<int64_t>(*action, "frame");
        if (!frame) continue;
        int button = sanitizeButton(ReplayJson::getInteger<int>(*action, "btn").value_or(1));
        bool pressed = ReplayJson::getBool(*action, "down").value_or(false);
        bool player2 = ReplayJson::getBool(*action, "2p").value_or(false);
        addInput(replay, static_cast<double>(*frame) / replay.fps, static_cast<double>(*frame), button, player2, pressed);
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseGDMO(std::vector<uint8_t> const& data) {
    {
        try {
            ByteReader reader(data);
            uint32_t count = reader.u32le();
            if (count > 0 && 4ull + static_cast<size_t>(count) * 16 + 4 <= data.size()) {
                ImportedReplay replay;
                replay.format = ReplayFormat::GDMO;
                replay.fps = 240.0;
                for (uint32_t i = 0; i < count; ++i) {
                    double time = reader.f64le();
                    int32_t button = reader.i32le();
                    bool pressed = reader.u8() != 0;
                    bool player1 = reader.u8() != 0;
                    reader.skip(2);
                    addInput(replay, time, static_cast<int64_t>(std::llround(time * replay.fps)), button, !player1, pressed);
                }
                if (reader.remaining() >= 4) {
                    uint32_t correctionCount = reader.u32le();
                    size_t correctionSize = correctionCount == 0 ? 0 : reader.remaining() / correctionCount;
                    if (correctionCount != 0 && (correctionSize == 56 || correctionSize == 0x23a8)) {
                        for (uint32_t i = 0; i < correctionCount; ++i) {
                            size_t start = reader.position();
                            double time = reader.f64le();
                            bool player1 = reader.u8() != 0;
                            reader.seek(start + 16);
                            double yVelocity = reader.f64le();
                            reader.f64le();
                            float x = reader.f32le();
                            float y = reader.f32le();
                            reader.skip(8);
                            float rotation = reader.f32le();
                            reader.seek(start + correctionSize);
                            addPlayerAnchor(
                                replay,
                                time,
                                time * replay.fps,
                                !player1,
                                makeImportedPlayerState(x, y, rotation, yVelocity, replay.platformerMode)
                            );
                        }
                        addWarningOnce(replay, "Converted GDMO correction data into Toasty anchors.");
                    } else if (correctionCount != 0) {
                        addWarningOnce(replay, "GDMO correction data had an unknown layout and was skipped.");
                    }
                }
                finishImport(replay);
                return replay;
            }
        } catch (...) {
        }
    }

    ByteReader reader(data);
    ImportedReplay replay;
    replay.format = ReplayFormat::GDMO;
    replay.fps = reader.f32le();
    uint32_t count = reader.u32le();
    reader.u32le();
    if (count > reader.remaining() / 24 + 1) throw FormatError("invalid GDMO action count");
    for (uint32_t i = 0; i < count; ++i) {
        size_t start = reader.position();
        bool pressed = reader.u8() != 0;
        bool player2 = reader.u8() != 0;
        reader.seek(start + 4);
        uint32_t frame = reader.u32le();
        double yVelocity = reader.f64le();
        float x = reader.f32le();
        float y = reader.f32le();
        reader.seek(start + 24);
        addInput(replay, static_cast<double>(frame) / replay.fps, frame, 1, player2, pressed);
        addPlayerAnchor(
            replay,
            static_cast<double>(frame) / safeFps(replay.fps),
            frame,
            player2,
            makeImportedPlayerState(x, y, 0.0f, yVelocity, replay.platformerMode)
        );
    }
    if (count != 0) {
        addWarningOnce(replay, "Converted GDMO frame position data into Toasty anchors.");
    }

    finishImport(replay);
    return replay;
}

static ImportedReplay parseUvBot(std::vector<uint8_t> const& data) {
    if (!hasMagic(data, "UVBOT")) throw FormatError("invalid uvBot magic");
    ByteReader reader(data);
    reader.skip(5);
    uint8_t version = reader.u8();
    if (version != 1 && version != 2) throw FormatError("unsupported uvBot version");

    ImportedReplay replay;
    replay.format = ReplayFormat::UvBot;
    replay.fps = version == 2 ? static_cast<double>(reader.f32le()) : 240.0;
    int32_t inputCount = reader.i32le();
    int32_t p1PhysicsCount = reader.i32le();
    int32_t p2PhysicsCount = reader.i32le();
    if (inputCount < 0 || inputCount > static_cast<int32_t>(reader.remaining() / 9 + 1)) {
        throw FormatError("invalid uvBot input count");
    }
    for (int32_t i = 0; i < inputCount; ++i) {
        uint64_t frame = reader.u64le();
        uint8_t flags = reader.u8();
        bool pressed = (flags & 1) != 0;
        uint8_t packedButton = flags >> 1;
        int button = static_cast<int>((packedButton % 3) + 1);
        bool player2 = packedButton > 2;
        addInput(replay, static_cast<double>(frame) / replay.fps, static_cast<int64_t>(frame), button, player2, pressed);
    }
    int32_t totalPhysics = std::max(0, p1PhysicsCount) + std::max(0, p2PhysicsCount);
    size_t physicsBytes = static_cast<size_t>(totalPhysics) * 28;
    if (physicsBytes > reader.remaining()) {
        throw FormatError("invalid uvBot physics data");
    }
    for (int32_t i = 0; i < std::max(0, p1PhysicsCount); ++i) {
        readUvBotPhysicsAnchor(reader, replay, false);
    }
    for (int32_t i = 0; i < std::max(0, p2PhysicsCount); ++i) {
        readUvBotPhysicsAnchor(reader, replay, true);
    }
    if (totalPhysics != 0) {
        addWarningOnce(replay, "Converted uvBot physics data into Toasty anchors.");
    }
    finishImport(replay);
    return replay;
}

static ImportedReplay parseByFormat(ReplayFormat format, std::vector<uint8_t> const& data) {
    switch (format) {
        case ReplayFormat::MegaHackJson: return parseMegaHackJson(data);
        case ReplayFormat::MegaHackBinary: return parseMegaHackBinary(data);
        case ReplayFormat::TasBotJson: return parseTasBotJson(data);
        case ReplayFormat::ZBotFrame: return parseZBotFrame(data);
        case ReplayFormat::YBotFrame: return parseYBotFrame(data);
        case ReplayFormat::YBot2: return parseYBot2(data);
        case ReplayFormat::Amethyst: return parseAmethyst(data);
        case ReplayFormat::Echo: return parseEcho(data);
        case ReplayFormat::GDMO: return parseGDMO(data);
        case ReplayFormat::ReplayBot: return parseReplayBot(data);
        case ReplayFormat::Rush: return parseRush(data);
        case ReplayFormat::KDBot: return parseKDBot(data);
        case ReplayFormat::Plaintext: return parsePlaintext(data);
        case ReplayFormat::DDHOR: return parseDDHOR(data);
        case ReplayFormat::XBotFrame: return parseXBotFrame(data);
        case ReplayFormat::XdBot: return parseXdBot(data);
        case ReplayFormat::RBot: return parseRBot(data);
        case ReplayFormat::Zephyrus: return parseZephyrus(data);
        case ReplayFormat::ReplayEngine1: return parseReplayEngine1(data);
        case ReplayFormat::ReplayEngine2: return parseReplayEngine2(data);
        case ReplayFormat::ReplayEngine3: return parseReplayEngine3(data);
        case ReplayFormat::Silicate1: return parseSilicate1(data);
        case ReplayFormat::Silicate2: return parseSilicate2(data);
        case ReplayFormat::Silicate3: return parseSilicate3(data);
        case ReplayFormat::TCBot: return parseTCBot(data);
        case ReplayFormat::GDR2: return parseGDR2(data);
        case ReplayFormat::GdrJson: return parseGdrJson(data);
        case ReplayFormat::UvBot: return parseUvBot(data);
        default:
            throw FormatError(std::string(formatDisplayName(format)) + " is recognized but not supported by the native importer yet.");
    }
}

static bool replayStemExists(std::filesystem::path const& directory, std::string_view stem) {
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || ec) return false;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        if (toasty::pathToUtf8(it->path().stem()) == stem) return true;
    }
    return false;
}

static std::string makeUniqueName(std::filesystem::path const& directory, std::string requestedName) {
    std::string base = ReplayStorage::sanitizeReplayName(std::move(requestedName));
    if (base.empty()) base = "converted_macro";
    if (!replayStemExists(directory, base)) return base;
    for (int suffix = 1;; ++suffix) {
        std::string candidate = base + "_" + std::to_string(suffix);
        if (!replayStemExists(directory, candidate)) return candidate;
    }
}

static std::vector<ImportedInput> prepareOutputInputs(ImportedReplay const& replay, double fps, bool& useCbsTiming) {
    fps = safeFps(fps);
    useCbsTiming = replay.needsCbsTiming || replay.dynamicTiming;

    constexpr double kFractionEpsilon = 0.000001;
    for (auto const& input : replay.inputs) {
        double rawTick = std::max(0.0, input.time * fps);
        if (std::abs(rawTick - std::round(rawTick)) > kFractionEpsilon) {
            useCbsTiming = true;
            break;
        }
    }

    std::vector<ImportedInput> result;
    result.reserve(replay.inputs.size());
    double maxTick = static_cast<double>(std::numeric_limits<int32_t>::max());
    for (auto input : replay.inputs) {
        double rawTick = std::max(0.0, input.time * fps);
        if (useCbsTiming) {
            double tickFloor = std::floor(rawTick + kFractionEpsilon);
            double fraction = rawTick - tickFloor;
            if (fraction < kFractionEpsilon) {
                fraction = 0.0;
            } else if (fraction > 1.0 - kFractionEpsilon) {
                tickFloor += 1.0;
                fraction = 0.0;
            }
            input.tick = static_cast<int64_t>(std::clamp<double>(tickFloor + 1.0, 1.0, maxTick));
            input.stepOffset = static_cast<float>(std::clamp(fraction, 0.0, 0.999999));
            input.cbsTimeOffset = fraction / fps;
        } else {
            input.tick = static_cast<int64_t>(std::clamp<int64_t>(input.tick + 1, 1, std::numeric_limits<int32_t>::max()));
            input.stepOffset = 0.0f;
            input.cbsTimeOffset = -1.0;
        }
        result.push_back(input);
    }
    std::stable_sort(result.begin(), result.end(), [](ImportedInput const& a, ImportedInput const& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        bool aHasCbs = a.cbsTimeOffset >= 0.0;
        bool bHasCbs = b.cbsTimeOffset >= 0.0;
        if (aHasCbs && bHasCbs && a.cbsTimeOffset != b.cbsTimeOffset) {
            return a.cbsTimeOffset < b.cbsTimeOffset;
        }
        return a.sequence < b.sequence;
    });
    return result;
}

static double preparedDuration(
    ImportedReplay const& replay,
    std::vector<ImportedInput> const& inputs,
    double fps
) {
    double duration = replay.duration;
    fps = safeFps(fps);
    for (auto const& input : inputs) {
        double inputTime = static_cast<double>(std::max<int64_t>(0, input.tick)) / fps;
        if (std::isfinite(input.cbsTimeOffset) && input.cbsTimeOffset >= 0.0f) {
            inputTime += static_cast<double>(input.cbsTimeOffset);
        }
        duration = std::max(duration, inputTime);
    }
    return duration;
}

static std::string conversionTargetExtension(ConversionTarget target) {
    switch (target) {
        case ConversionTarget::TTR3: return ".ttr3";
        case ConversionTarget::GDR: return ".gdr";
    }
    return ".gdr";
}

static double nativeGDRDuration(MacroSequence const& macro, double fps) {
    double duration = std::isfinite(macro.duration) && macro.duration >= 0.0 ? macro.duration : 0.0;
    fps = safeFps(fps);
    for (auto const& input : macro.inputs) {
        duration = std::max(duration, static_cast<double>(std::max<int64_t>(0, static_cast<int64_t>(input.frame))) / fps);
    }
    return duration;
}

}

const char* formatDisplayName(ReplayFormat format) {
    switch (format) {
        case ReplayFormat::MegaHackJson: return "Mega Hack JSON";
        case ReplayFormat::MegaHackBinary: return "Mega Hack Binary";
        case ReplayFormat::TasBotJson: return "TASBot JSON";
        case ReplayFormat::ZBotFrame: return "zBot Frame";
        case ReplayFormat::OmegaBot: return "OmegaBot";
        case ReplayFormat::YBotFrame: return "yBot Frame";
        case ReplayFormat::YBot2: return "yBot 2";
        case ReplayFormat::Echo: return "Echo";
        case ReplayFormat::Amethyst: return "Amethyst";
        case ReplayFormat::OsuReplay: return "osu! Replay";
        case ReplayFormat::GDMO: return "GDMO";
        case ReplayFormat::ReplayBot: return "ReplayBot";
        case ReplayFormat::Rush: return "Rush";
        case ReplayFormat::KDBot: return "KD-BOT";
        case ReplayFormat::Plaintext: return "Plaintext";
        case ReplayFormat::DDHOR: return "DDHOR";
        case ReplayFormat::XBotFrame: return "xBot Frame";
        case ReplayFormat::XdBot: return "xdBot";
        case ReplayFormat::QBot: return "qBot";
        case ReplayFormat::RBot: return "RBot";
        case ReplayFormat::Zephyrus: return "Zephyrus";
        case ReplayFormat::ReplayEngine1: return "ReplayEngine 1";
        case ReplayFormat::ReplayEngine2: return "ReplayEngine 2";
        case ReplayFormat::ReplayEngine3: return "ReplayEngine 3";
        case ReplayFormat::Silicate1: return "Silicate";
        case ReplayFormat::Silicate2: return "Silicate 2";
        case ReplayFormat::Silicate3: return "Silicate 3";
        case ReplayFormat::GDR2: return "GDReplayFormat 2";
        case ReplayFormat::GdrJson: return "GDReplayFormat JSON";
        case ReplayFormat::UvBot: return "uvBot";
        case ReplayFormat::TCBot: return "TCBot";
        default: return "Unknown";
    }
}

bool isSupportedFormat(ReplayFormat format) {
    return std::find(kSupportedReplayFormats.begin(), kSupportedReplayFormats.end(), format) != kSupportedReplayFormats.end();
}

std::vector<ReplayFormat> supportedFormats() {
    return { kSupportedReplayFormats.begin(), kSupportedReplayFormats.end() };
}

bool isForeignReplayExtension(std::filesystem::path const& path) {
    return guessByExtension(path) != ReplayFormat::Unknown || toasty::pathToUtf8(path.extension()).empty();
}

std::string normalizedPathKey(std::filesystem::path const& path) {
    std::error_code ec;
    auto absolute = std::filesystem::weakly_canonical(path, ec);
    if (ec) absolute = std::filesystem::absolute(path, ec);
    if (ec) absolute = path;
    return lowerCopy(toasty::pathToUtf8(absolute));
}

DetectedReplay detectReplay(
    std::filesystem::path const& path,
    std::unordered_set<std::string> const& convertedSources,
    std::unordered_set<std::string> const& usableStems
) {
    DetectedReplay result;
    result.path = path;
    result.stem = toasty::pathToUtf8(path.stem());
    result.filename = toasty::pathToUtf8(path.filename());
    result.converted = convertedSources.count(normalizedPathKey(path)) > 0 || usableStems.count(result.stem) > 0;

    auto bytes = ReplayStorage::readReplayBytes(path);
    if (!bytes) {
        result.recognized = true;
        result.detail = "Could not read file.";
        return result;
    }

    result.format = detectContent(path, *bytes);
    if (result.format == ReplayFormat::Unknown) {
        result.format = guessByExtension(path);
    }
    result.recognized = result.format != ReplayFormat::Unknown;
    result.supported = isSupportedFormat(result.format);
    if (!result.recognized) {
        result.detail = "Unknown macro format.";
    } else if (!result.supported) {
        result.detail = "Detected, but native conversion is not implemented yet.";
    }
    return result;
}

std::optional<ImportedReplay> importReplay(std::filesystem::path const& path, std::string* error) {
    try {
        auto bytes = ReplayStorage::readReplayBytes(path);
        if (!bytes) throw FormatError("Could not read source file.");

        ReplayFormat format = detectContent(path, *bytes);
        if (format == ReplayFormat::Unknown) {
            format = guessByExtension(path);
        }
        if (format == ReplayFormat::Unknown) {
            throw FormatError("Unknown macro format.");
        }

        auto imported = parseByFormat(format, *bytes);
        imported.sourceName = toasty::pathToUtf8(path.stem());
        imported.format = format;
        finishImport(imported);
        if (imported.inputs.empty()) {
            throw FormatError("No convertible inputs were found.");
        }
        return imported;
    } catch (std::exception const& ex) {
        if (error) *error = ex.what();
        return std::nullopt;
    }
}

ConversionResult convertReplay(
    std::filesystem::path const& sourcePath,
    ConversionTarget target,
    std::string requestedName,
    std::string author,
    std::filesystem::path const& outputDirectory
) {
    ConversionResult result;
    geode::log::info("[TR-CONV][I002] conversion started: src={} target={}",
        toasty::pathToUtf8(sourcePath), static_cast<int>(target));
    try {
        std::error_code ec;
        if (!std::filesystem::exists(outputDirectory, ec)) {
            std::filesystem::create_directories(outputDirectory, ec);
        }
        if (ec) {
            geode::log::error("[TR-CONV][E004] replay output directory unavailable: {} ({})",
                toasty::pathToUtf8(outputDirectory), ec.message());
            throw FormatError("Replay folder is unavailable.");
        }

        std::string importError;
        auto imported = importReplay(sourcePath, &importError);
        if (!imported) {
            geode::log::error("[TR-CONV][E002] no matching importer for source bytes: {} ({})",
                toasty::pathToUtf8(sourcePath),
                importError.empty() ? "no importer matched" : importError);
            throw FormatError(importError.empty() ? "Failed to import source macro." : importError);
        }

        geode::log::info("[TR-CONV][I001] detected foreign replay format: id={} inputCount={} fps={:.1f}",
            static_cast<int>(imported->format), imported->inputs.size(), imported->fps);
        result.detectedFormat = imported->format;
        result.fps = isFiniteFps(imported->fps) ? imported->fps : 240.0;
        result.inputCount = imported->inputs.size();
        result.warnings = imported->warnings;

        if (requestedName.empty()) requestedName = imported->sourceName;
        std::string outputName = makeUniqueName(outputDirectory, requestedName);
        auto outputPath = outputDirectory / (outputName + conversionTargetExtension(target));

        if (target == ConversionTarget::TTR3) {
            TTR3InspectionFacts facts;
            facts.hasFractionalPlaintextFrames = imported->hasFractionalPlaintextFrames;
            facts.hasDecodedGdr2Extensions = imported->hasDecodedGdr2Extensions;
            auto eligibility = inspectTTR3Eligibility(imported->format, facts);
            if (!eligibility.lossless || eligibility.route != TTR3Route::LosslessTTR3) {
                addWarningOnce(*imported, eligibility.message);
                result.warnings = imported->warnings;
                throw FormatError(eligibility.message);
            }

            imported->convertedForTTR3 = true;
            finishImportForTarget(*imported, ConversionTarget::TTR3);
            auto ttr3Macro = buildTTR3FromImported(*imported, outputName, std::move(author));
            auto bytes = ttr3Macro.serialize();
            if (bytes.empty()) throw FormatError("Failed to serialize TTR3 output.");
            std::ofstream output(outputPath, std::ios::binary);
            output.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!output) throw FormatError("Failed to write TTR3 output.");

            result.ok = true;
            result.outputName = outputName;
            result.outputPath = outputPath;
            result.inputCount = ttr3Macro.inputs.size();
            std::ostringstream message;
            message << "Converted " << result.inputCount << " inputs at " << result.fps
                    << " TPS to " << outputName << conversionTargetExtension(target);
            result.message = message.str();
            geode::log::info("[TR-CONV][I003] TTR3 conversion finished: out={} inputs={} fps={:.1f}",
                toasty::pathToUtf8(result.outputPath), result.inputCount, result.fps);
            return result;
        }

        bool useCbsTiming = false;
        auto inputs = prepareOutputInputs(*imported, result.fps, useCbsTiming);
        auto outputDuration = preparedDuration(*imported, inputs, result.fps);
        auto outputAccuracy = useCbsTiming ? AccuracyMode::CBS : AccuracyMode::Vanilla;
        if (useCbsTiming && target == ConversionTarget::GDR) {
            addWarningOnce(*imported, "GDR output stores best-effort timing offsets only.");
            result.warnings = imported->warnings;
        }

        MacroSequence macro;
        macro.author = std::move(author);
        macro.name = outputName;
        macro.persistedName = outputName;
        macro.levelInfo.name = imported->levelName.empty() ? outputName : imported->levelName;
        macro.levelInfo.id = imported->levelId;
        macro.framerate = result.fps;
        macro.duration = outputDuration;
        macro.accuracyMode = outputAccuracy;
        macro.platformerMode = imported->platformerMode;
        macro.hasPlatformerModeMetadata = true;
        macro.anchors = imported->anchors;
        macro.inputs.reserve(inputs.size());
        for (auto const& input : inputs) {
            macro.inputs.emplace_back(
                static_cast<int>(std::clamp<int64_t>(input.tick, 0, std::numeric_limits<int32_t>::max())),
                input.button,
                input.player2,
                input.pressed,
                useCbsTiming ? input.stepOffset : 0.0f
            );
        }
        auto bytes = macro.exportData(false);
        if (bytes.empty()) throw FormatError("Failed to serialize GDR output.");
        std::ofstream output(outputPath, std::ios::binary);
        output.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw FormatError("Failed to write GDR output.");

        result.ok = true;
        result.outputName = outputName;
        result.outputPath = outputPath;
        std::ostringstream message;
        message << "Converted " << result.inputCount << " inputs at " << result.fps
                << " TPS to " << outputName << conversionTargetExtension(target);
        result.message = message.str();
        geode::log::info("[TR-CONV][I003] GDR/TTR2 conversion finished: out={} inputs={} fps={:.1f} cbs={}",
            toasty::pathToUtf8(result.outputPath), result.inputCount, result.fps, useCbsTiming);
    } catch (std::exception const& ex) {
        result.ok = false;
        result.message = ex.what();
        geode::log::error("[TR-CONV][E005] conversion aborted: {}", ex.what());
    }
    return result;
}

ConversionResult convertNativeGDRToTTRDuplicate(
    std::filesystem::path const& sourcePath,
    std::string author,
    std::filesystem::path const& outputDirectory
) {
    ConversionResult result;
    try {
        std::error_code ec;
        if (!std::filesystem::exists(outputDirectory, ec)) {
            std::filesystem::create_directories(outputDirectory, ec);
        }
        if (ec) {
            throw FormatError("Replay folder is unavailable.");
        }

        auto bytes = ReplayStorage::readReplayBytes(sourcePath);
        if (!bytes) {
            throw FormatError("Could not read source GDR.");
        }

        auto imported = MacroSequence::tryImportData(*bytes);
        if (!imported) {
            throw FormatError("Failed to import ToastyReplay GDR macro.");
        }

        imported->inferMissingPlatformerMode();
        if (imported->inputs.empty()) {
            throw FormatError("No convertible inputs were found.");
        }

        std::string sourceStem = toasty::pathToUtf8(sourcePath.stem());
        if (sourceStem.empty()) {
            sourceStem = imported->name.empty() ? "macro" : imported->name;
        }

        std::string outputName = makeUniqueName(outputDirectory, sourceStem + "_ttr3");
        auto outputPath = outputDirectory / (outputName + ".ttr3");
        double fps = safeFps(imported->framerate);
        AccuracyMode importedMode = writableAccuracyMode(imported->accuracyMode);
        bool isTimedSource = usesTimedAccuracy(importedMode);
        bool hasPlayer2Inputs = std::any_of(imported->inputs.begin(), imported->inputs.end(), [](MacroAction const& input) {
            return input.player2;
        });

        TTRMacro macro;
        macro.fileFormat = TTRFileFormat::TTR3;
        macro.sourceFormatId = static_cast<uint64_t>(ReplayFormat::Unknown);
        macro.losslessVerified = true;
        macro.macroConverted = true;
        macro.author = author.empty() ? imported->author : std::move(author);
        macro.name = outputName;
        macro.persistedName = outputName;
        macro.levelName = imported->levelInfo.name.empty() ? outputName : imported->levelInfo.name;
        macro.levelId = imported->levelInfo.id;
        macro.framerate = fps;
        macro.duration = nativeGDRDuration(*imported, fps);
        macro.accuracyMode = AccuracyMode::CBS;
        macro.platformerMode = imported->platformerMode;
        macro.twoPlayerMode = hasPlayer2Inputs;
        macro.recordedFromStartPos = imported->recordedFromStartPos;
        macro.startPosX = imported->startPosX;
        macro.startPosY = imported->startPosY;
        macro.exactCbsTiming = true;
        macro.anchors = imported->anchors;
        macro.tpsEvents = {{0.0, fps}};
        macro.recordTimestamp = static_cast<int64_t>(std::time(nullptr));
        macro.inputs.reserve(imported->inputs.size());

        double substepDuration = isTimedSource ? (1.0 / std::max(1.0, fps * 4.0)) : 0.0;
        for (auto const& input : imported->inputs) {
            float stepOffset = isTimedSource ? input.stepOffset : 0.0f;
            double cbsTimeOffset = isTimedSource
                ? std::clamp(static_cast<double>(stepOffset), 0.0, 3.999) * substepDuration
                : -1.0;
            macro.recordAction(
                static_cast<int>(std::clamp<int64_t>(
                    static_cast<int64_t>(input.frame),
                    0,
                    static_cast<int64_t>(std::numeric_limits<int32_t>::max())
                )),
                input.button,
                input.player2,
                input.down,
                stepOffset,
                cbsTimeOffset
            );
        }

        auto outputBytes = macro.serialize();
        if (outputBytes.empty()) {
            throw FormatError("Failed to serialize TTR3 output.");
        }

        std::ofstream output(outputPath, std::ios::binary);
        output.write(reinterpret_cast<char const*>(outputBytes.data()), static_cast<std::streamsize>(outputBytes.size()));
        if (!output) {
            throw FormatError("Failed to write TTR3 output.");
        }

        result.ok = true;
        result.outputName = outputName;
        result.outputPath = outputPath;
        result.inputCount = imported->inputs.size();
        result.fps = fps;
        std::ostringstream message;
        message << "Converted " << result.inputCount << " inputs at " << result.fps
                << " TPS to " << outputName << ".ttr3";
        result.message = message.str();
        geode::log::info("[TR-CONV][I003] conversion finished: out={} inputs={} fps={:.1f}",
            toasty::pathToUtf8(result.outputPath), result.inputCount, result.fps);
    } catch (std::exception const& ex) {
        result.ok = false;
        result.message = ex.what();
        geode::log::error("[TR-CONV][E005] conversion aborted: {}", ex.what());
    }
    return result;
}

}
