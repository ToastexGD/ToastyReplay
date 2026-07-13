#pragma once

#include <Geode/Result.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace toasty::tcbot {

struct Input {
    uint32_t sourceFrame = 0;
    double timeSeconds = 0.0;
    uint8_t button = 1;
    bool player2 = false;
    bool pressed = false;
    bool swift = false;
};

struct TpsEvent {
    double timeSeconds = 0.0;
    double tps = 240.0;
};

struct Replay {
    uint8_t version = 0;
    uint8_t headerFlags = 0;
    double initialTps = 240.0;
    double duration = 0.0;
    bool hasSeed = false;
    uint64_t seed = 0;
    bool dynamicTiming = false;
    std::vector<Input> inputs;
    std::vector<TpsEvent> tpsEvents;
};

geode::Result<Replay> parse(std::span<uint8_t const> data);

}
