#include "format/replay.hpp"

#include <cassert>
#include <vector>

static gdr::json makeReplayJson(bool timed) {
    gdr::json input = {
        {"frame", 24u},
        {"btn", 1},
        {"2p", false},
        {"down", true}
    };
    if (timed) {
        input["accuracy_offset"] = 0.5f;
    }

    gdr::json replay = {
        {"gameVersion", 2.2081f},
        {"description", "fixture"},
        {"version", 1.2f},
        {"duration", 0.25f},
        {"bot", {{"name", "ToastyReplay"}, {"version", "2.2.1"}}},
        {"level", {{"id", 123u}, {"name", "Test Level"}}},
        {"author", "Toast"},
        {"seed", 7},
        {"coins", 0},
        {"ldm", false},
        {"framerate", 240.0f},
        {"platformer_mode", true},
        {"inputs", gdr::json::array({input})}
    };
    if (timed) {
        replay["accuracy_mode"] = static_cast<int>(AccuracyMode::CBS);
    }
    return replay;
}

static void testGDRImportAcceptsMsgpack() {
    auto bytes = gdr::json::to_msgpack(makeReplayJson(true));
    auto imported = MacroSequence::tryImportData(bytes);
    assert(imported.has_value());
    assert(imported->author == "Toast");
    assert(imported->levelInfo.id == 123u);
    assert(imported->accuracyMode == AccuracyMode::CBS);
    assert(imported->platformerMode);
    assert(imported->inputs.size() == 1);
    assert(imported->inputs.front().frame == 24);
    assert(imported->inputs.front().stepOffset == 0.5f);
}

static void testGDRImportRejectsMalformedData() {
    std::vector<uint8_t> malformed = {0xff, 0x00, 0x01};
    assert(!MacroSequence::tryImportData(malformed).has_value());
}

int main() {
    testGDRImportAcceptsMsgpack();
    testGDRImportRejectsMalformedData();
    return 0;
}
