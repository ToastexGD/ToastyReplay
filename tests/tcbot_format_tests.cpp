#include "conversion/tcbot_format.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

constexpr uint8_t kHeader[16] = {
    0x9f, 0x88, 0x89, 0x84, 0x9f, 0x3b, 0x1d, 0xd8,
    0xcc, 0xa1, 0x86, 0x8a, 0x88, 0x99, 0x84, 0x00
};

template <class T>
void appendLittleEndian(std::vector<uint8_t>& bytes, T value) {
    for (size_t index = 0; index < sizeof(T); ++index) {
        bytes.push_back(static_cast<uint8_t>((value >> (index * 8)) & 0xff));
    }
}

void appendFloat(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendLittleEndian(bytes, bits);
}

void appendLeb128(std::vector<uint8_t>& bytes, uint32_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        bytes.push_back(byte);
    } while (value != 0);
}

std::vector<uint8_t> makeHeader(uint8_t version, uint8_t flags, float rate, uint64_t seed = 0) {
    std::vector<uint8_t> bytes(kHeader, kHeader + sizeof(kHeader));
    bytes.push_back(version);
    bytes.push_back(0);
    bytes.push_back(flags);
    bytes.push_back(0);
    appendFloat(bytes, rate);
    appendLittleEndian(bytes, seed);
    bytes.resize(16 + 0x40, 0);
    return bytes;
}

void testV1Inputs() {
    auto bytes = makeHeader(1, 0, 240.0f);
    appendLeb128(bytes, 2);
    appendLeb128(bytes, 10);
    bytes.push_back(0x80);
    appendLeb128(bytes, 20);
    bytes.push_back(0x00);

    auto result = toasty::tcbot::parse(bytes);
    assert(result.isOk());
    auto replay = std::move(result).unwrap();
    assert(replay.version == 1);
    assert(replay.initialTps == 240.0);
    assert(replay.inputs.size() == 2);
    assert(replay.inputs[0].sourceFrame == 10);
    assert(replay.inputs[0].pressed);
    assert(replay.inputs[1].sourceFrame == 20);
    assert(!replay.inputs[1].pressed);
}

void testV2SwiftAndSeed() {
    auto bytes = makeHeader(2, 0x03, 240.0f, 0x123456789abcdef0ull);
    appendLeb128(bytes, 5);
    bytes.push_back(0x45);
    bytes.push_back(10);
    bytes.push_back(0x15);

    auto result = toasty::tcbot::parse(bytes);
    assert(result.isOk());
    auto replay = std::move(result).unwrap();
    assert(replay.version == 2);
    assert(replay.hasSeed);
    assert(replay.seed == 0x123456789abcdef0ull);
    assert(replay.inputs.size() == 3);
    assert(replay.inputs[1].sourceFrame == 15);
    assert(replay.inputs[1].swift);
    assert(replay.inputs[1].pressed);
    assert(replay.inputs[2].swift);
    assert(!replay.inputs[2].pressed);
}

void testV2TpsChange() {
    auto bytes = makeHeader(2, 0x02, 240.0f);
    appendLeb128(bytes, 0);
    bytes.push_back(0x45);
    bytes.push_back(100);
    bytes.push_back(0x4c);
    appendFloat(bytes, 120.0f);
    bytes.push_back(100);
    bytes.push_back(0x01);

    auto result = toasty::tcbot::parse(bytes);
    assert(result.isOk());
    auto replay = std::move(result).unwrap();
    assert(replay.dynamicTiming);
    assert(replay.tpsEvents.size() == 2);
    assert(std::abs(replay.tpsEvents[1].timeSeconds - 100.0 / 240.0) < 0.000001);
    assert(replay.tpsEvents[1].tps == 120.0);
    assert(replay.inputs.size() == 2);
    assert(replay.inputs[1].sourceFrame == 200);
    assert(std::abs(replay.inputs[1].timeSeconds - 1.25) < 0.000001);
}

void testV2FrameDurationRate() {
    auto bytes = makeHeader(2, 0x00, 1.0f / 360.0f);
    appendLeb128(bytes, 0);
    bytes.push_back(0x05);

    auto result = toasty::tcbot::parse(bytes);
    assert(result.isOk());
    auto replay = std::move(result).unwrap();
    assert(std::abs(replay.initialTps - 360.0) < 0.01);
}

void testMalformedFiles() {
    auto unknown = makeHeader(3, 0, 240.0f);
    assert(toasty::tcbot::parse(unknown).isErr());

    auto truncated = makeHeader(2, 0x02, 240.0f);
    appendLeb128(truncated, 0);
    truncated.push_back(0x0c);
    assert(toasty::tcbot::parse(truncated).isErr());

    auto invalidButton = makeHeader(1, 0, 240.0f);
    appendLeb128(invalidButton, 1);
    appendLeb128(invalidButton, 0);
    invalidButton.push_back(0x83);
    assert(toasty::tcbot::parse(invalidButton).isErr());

    auto swiftRelease = makeHeader(2, 0x02, 240.0f);
    appendLeb128(swiftRelease, 0);
    swiftRelease.push_back(0x11);
    assert(toasty::tcbot::parse(swiftRelease).isErr());
}

int testExternalFixture(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Could not open TCM fixture\n";
        return 1;
    }
    std::vector<uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    auto result = toasty::tcbot::parse(bytes);
    if (!result) {
        std::cerr << result.unwrapErr() << '\n';
        return 1;
    }
    auto replay = std::move(result).unwrap();
    if (replay.inputs.empty()) {
        std::cerr << "TCM fixture has no inputs\n";
        return 1;
    }
    std::cout << "TCM v" << static_cast<int>(replay.version)
              << ": " << replay.inputs.size()
              << " inputs at " << replay.initialTps << " TPS\n";
    return 0;
}

}

int main(int argc, char** argv) {
    testV1Inputs();
    testV2SwiftAndSeed();
    testV2TpsChange();
    testV2FrameDurationRate();
    testMalformedFiles();
    if (argc > 1) {
        return testExternalFixture(argv[1]);
    }
    return 0;
}
