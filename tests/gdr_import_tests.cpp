#include "format/replay.hpp"

#include <cassert>

int main() {
    MacroSequence source;
    source.gameVersion = 2.2081f;
    source.description = "dependency-clean";
    source.version = 1.0f;
    source.duration = 0.1f;
    source.framerate = 240.0f;
    source.levelInfo.id = 123;
    source.levelInfo.name = "test-level";
    source.author = "Toast";
    source.seed = 456;
    source.coins = 3;
    source.ldm = true;
    source.accuracyMode = AccuracyMode::CBS;
    source.savedAnchorInterval = 120;
    source.platformerMode = true;
    source.hasPlatformerModeMetadata = true;
    source.inputs.emplace_back(24, 1, false, true, 0.5f);

    auto bytes = source.exportData(false);
    auto imported = MacroSequence::tryImportData(bytes);

    assert(imported);
    assert(imported->gameVersion == source.gameVersion);
    assert(imported->levelInfo.id == source.levelInfo.id);
    assert(imported->accuracyMode == AccuracyMode::CBS);
    assert(imported->savedAnchorInterval == 12);
    assert(imported->platformerMode);
    assert(imported->inputs.size() == 1);
    assert(imported->inputs.front().frame == 24);
    assert(imported->inputs.front().stepOffset == 0.5f);
    return 0;
}
