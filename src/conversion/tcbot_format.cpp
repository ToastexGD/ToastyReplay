#include "conversion/tcbot_format.hpp"

#include <Geode/utils/general.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>

namespace toasty::tcbot {
namespace {

constexpr std::array<uint8_t, 16> kHeader = {
    0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
    0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00
};

constexpr size_t kHeaderBodySize = 0x40;
constexpr size_t kMaximumInputs = 1000000;

bool validTps(double tps) {
    return std::isfinite(tps) && tps >= 1.0 && tps <= 1000000.0;
}

class Reader {
public:
    explicit Reader(std::span<uint8_t const> data) : m_data(data) {}

    size_t remaining() const {
        return m_position <= m_data.size() ? m_data.size() - m_position : 0;
    }

    geode::Result<size_t> skip(size_t count, std::string_view field) {
        if (count > remaining()) {
            return geode::Err("TCM file ended while reading {}.", field);
        }
        m_position += count;
        return geode::Ok(m_position);
    }

    geode::Result<uint8_t> u8(std::string_view field) {
        if (remaining() < 1) {
            return geode::Err("TCM file ended while reading {}.", field);
        }
        return geode::Ok(m_data[m_position++]);
    }

    geode::Result<uint16_t> u16le(std::string_view field) {
        uint16_t value = 0;
        for (size_t i = 0; i < sizeof(value); ++i) {
            uint8_t byte = 0;
            GEODE_UNWRAP_INTO(byte, u8(field));
            value |= static_cast<uint16_t>(byte) << (i * 8);
        }
        return geode::Ok(value);
    }

    geode::Result<uint32_t> u32le(std::string_view field) {
        uint32_t value = 0;
        for (size_t i = 0; i < sizeof(value); ++i) {
            uint8_t byte = 0;
            GEODE_UNWRAP_INTO(byte, u8(field));
            value |= static_cast<uint32_t>(byte) << (i * 8);
        }
        return geode::Ok(value);
    }

    geode::Result<uint64_t> u64le(std::string_view field) {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(value); ++i) {
            uint8_t byte = 0;
            GEODE_UNWRAP_INTO(byte, u8(field));
            value |= static_cast<uint64_t>(byte) << (i * 8);
        }
        return geode::Ok(value);
    }

    geode::Result<float> f32le(std::string_view field) {
        uint32_t bits = 0;
        GEODE_UNWRAP_INTO(bits, u32le(field));
        return geode::Ok(std::bit_cast<float>(bits));
    }

    geode::Result<uint32_t> leb128(std::string_view field) {
        uint32_t value = 0;
        for (uint32_t index = 0; index < 5; ++index) {
            uint8_t byte = 0;
            GEODE_UNWRAP_INTO(byte, u8(field));
            if (index == 4 && (byte & 0xf0) != 0) {
                return geode::Err("TCM {} is too large.", field);
            }
            value |= static_cast<uint32_t>(byte & 0x7f) << (index * 7);
            if ((byte & 0x80) == 0) {
                return geode::Ok(value);
            }
        }
        return geode::Err("TCM {} is too large.", field);
    }

private:
    std::span<uint8_t const> m_data;
    size_t m_position = 0;
};

class Timeline {
public:
    explicit Timeline(double tps) : m_currentTps(tps) {}

    double timeForFrame(uint32_t frame) const {
        return m_segmentTime + static_cast<double>(frame - m_segmentFrame) / m_currentTps;
    }

    void changeTps(uint32_t frame, double tps) {
        m_segmentTime = timeForFrame(frame);
        m_segmentFrame = frame;
        m_currentTps = tps;
    }

    void reset(double tps) {
        m_segmentTime = 0.0;
        m_segmentFrame = 0;
        m_currentTps = tps;
    }

    double currentTps() const {
        return m_currentTps;
    }

private:
    double m_segmentTime = 0.0;
    uint32_t m_segmentFrame = 0;
    double m_currentTps = 240.0;
};

geode::Result<double> decodeTps(uint8_t version, uint8_t flags, float rateField) {
    double tps = static_cast<double>(rateField);
    if (version == 2 && (flags & 0x02) == 0) {
        if (!std::isfinite(rateField) || rateField <= 0.0f) {
            return geode::Err("TCM v2 has an invalid frame duration.");
        }
        tps = 1.0 / tps;
    }
    if (!validTps(tps)) {
        return geode::Err("TCM replay TPS is outside the supported range.");
    }
    return geode::Ok(tps);
}

geode::Result<size_t> appendInput(
    Replay& replay,
    Timeline const& timeline,
    uint32_t frame,
    uint8_t button,
    bool player2,
    bool pressed,
    bool swift
) {
    if (swift && !pressed) {
        return geode::Err("TCM v2 contains an unsupported swift release record.");
    }
    size_t added = swift ? 2 : 1;
    if (replay.inputs.size() > kMaximumInputs - added) {
        return geode::Err("TCM replay contains too many inputs.");
    }
    double time = timeline.timeForFrame(frame);
    replay.inputs.push_back({frame, time, button, player2, pressed, swift});
    if (swift) {
        replay.inputs.push_back({frame, time, button, player2, !pressed, true});
    }
    replay.duration = std::max(replay.duration, time);
    return geode::Ok(added);
}

geode::Result<Replay> parseV1(Reader& reader, Replay replay) {
    uint32_t count = 0;
    GEODE_UNWRAP_INTO(count, reader.leb128("v1 input count"));
    if (count > kMaximumInputs) {
        return geode::Err("TCM v1 input count is too large.");
    }

    Timeline timeline(replay.initialTps);
    uint32_t previousFrame = 0;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t frame = 0;
        uint8_t state = 0;
        GEODE_UNWRAP_INTO(frame, reader.leb128("v1 input frame"));
        GEODE_UNWRAP_INTO(state, reader.u8("v1 input state"));
        if (index != 0 && frame < previousFrame) {
            return geode::Err("TCM v1 input frames are not ordered.");
        }
        previousFrame = frame;
        uint8_t inputType = state & 0x07;
        if (inputType >= 3) {
            return geode::Err("TCM v1 input uses an unsupported button type.");
        }
        GEODE_UNWRAP(appendInput(
            replay,
            timeline,
            frame,
            static_cast<uint8_t>(inputType + 1),
            (state & 0x40) != 0,
            (state & 0x80) != 0,
            false
        ));
    }
    if (reader.remaining() != 0) {
        return geode::Err("TCM v1 contains trailing data.");
    }
    return geode::Ok(std::move(replay));
}

geode::Result<Replay> parseV2(Reader& reader, Replay replay) {
    uint32_t currentFrame = 0;
    GEODE_UNWRAP_INTO(currentFrame, reader.leb128("v2 starting frame"));
    uint64_t lastDelta = 0;
    Timeline timeline(replay.initialTps);

    while (reader.remaining() != 0) {
        uint8_t state = 0;
        GEODE_UNWRAP_INTO(state, reader.u8("v2 record"));
        uint8_t deltaData = (state >> 5) & 0x07;
        uint8_t inputData = state & 0x03;
        bool deltaUsesPrevious = (deltaData & 1) != 0;
        uint8_t deltaSize = (deltaData >> 1) & 0x03;

        if (inputData != 0) {
            GEODE_UNWRAP(appendInput(
                replay,
                timeline,
                currentFrame,
                inputData,
                (state & 0x08) != 0,
                (state & 0x04) != 0,
                (state & 0x10) != 0
            ));
        } else {
            uint8_t customType = (state >> 2) & 0x03;
            bool extra = (state & 0x10) != 0;
            if (customType == 3) {
                if (extra) {
                    return geode::Err("TCM v2 contains an unsupported custom record.");
                }
                float newTps = 0.0f;
                GEODE_UNWRAP_INTO(newTps, reader.f32le("v2 TPS change"));
                if (!validTps(static_cast<double>(newTps))) {
                    return geode::Err("TCM v2 contains an invalid TPS change.");
                }
                double eventTime = timeline.timeForFrame(currentFrame);
                timeline.changeTps(currentFrame, static_cast<double>(newTps));
                replay.tpsEvents.push_back({eventTime, static_cast<double>(newTps)});
                replay.dynamicTiming = true;
            } else {
                if (extra) {
                    GEODE_UNWRAP_INTO(replay.seed, reader.u64le("v2 reset seed"));
                    replay.hasSeed = true;
                }
                double resetTps = timeline.currentTps();
                currentFrame = 0;
                replay.initialTps = resetTps;
                replay.inputs.clear();
                replay.tpsEvents = {{0.0, resetTps}};
                replay.duration = 0.0;
                replay.dynamicTiming = false;
                timeline.reset(resetTps);
            }
        }

        uint64_t rawDelta = 0;
        if (deltaSize == 1) {
            uint8_t value = 0;
            GEODE_UNWRAP_INTO(value, reader.u8("v2 8-bit frame delta"));
            rawDelta = value;
        } else if (deltaSize == 2) {
            uint16_t value = 0;
            GEODE_UNWRAP_INTO(value, reader.u16le("v2 16-bit frame delta"));
            rawDelta = value;
        } else if (deltaSize == 3) {
            uint32_t value = 0;
            GEODE_UNWRAP_INTO(value, reader.u32le("v2 32-bit frame delta"));
            rawDelta = value;
        }

        uint64_t delta = rawDelta + (deltaUsesPrevious ? lastDelta : 0);
        if (delta > std::numeric_limits<uint32_t>::max() - currentFrame) {
            return geode::Err("TCM v2 frame delta overflows the frame counter.");
        }
        if (delta != 0) {
            lastDelta = delta;
        }
        currentFrame += static_cast<uint32_t>(delta);
    }

    return geode::Ok(std::move(replay));
}

}

geode::Result<Replay> parse(std::span<uint8_t const> data) {
    if (data.size() < kHeader.size() + kHeaderBodySize) {
        return geode::Err("TCM file is smaller than its fixed header.");
    }
    if (!std::equal(kHeader.begin(), kHeader.end(), data.begin())) {
        return geode::Err("TCM file has an invalid header.");
    }

    Reader reader(data);
    GEODE_UNWRAP(reader.skip(kHeader.size(), "file signature"));

    Replay replay;
    GEODE_UNWRAP_INTO(replay.version, reader.u8("format version"));
    GEODE_UNWRAP(reader.skip(1, "version padding"));
    GEODE_UNWRAP_INTO(replay.headerFlags, reader.u8("header flags"));
    GEODE_UNWRAP(reader.skip(1, "flag padding"));

    float rateField = 0.0f;
    GEODE_UNWRAP_INTO(rateField, reader.f32le("replay rate"));
    GEODE_UNWRAP_INTO(replay.seed, reader.u64le("header seed"));
    GEODE_UNWRAP(reader.skip(kHeaderBodySize - 16, "header padding"));

    if (replay.version != 1 && replay.version != 2) {
        return geode::Err("TCM format version {} is not supported.", replay.version);
    }
    if (replay.version == 2 && (replay.headerFlags & 0xfc) != 0) {
        return geode::Err("TCM v2 uses unsupported header flags.");
    }
    GEODE_UNWRAP_INTO(replay.initialTps, decodeTps(replay.version, replay.headerFlags, rateField));
    replay.hasSeed = replay.version == 2 && (replay.headerFlags & 0x01) != 0;
    replay.tpsEvents.push_back({0.0, replay.initialTps});

    if (replay.version == 1) {
        return parseV1(reader, std::move(replay));
    }
    return parseV2(reader, std::move(replay));
}

}
