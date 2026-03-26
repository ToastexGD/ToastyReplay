#include "core/cbf_integration.hpp"

namespace {
    constexpr char kSyzziCBFModId[] = "syzzi.click_between_frames";
}

geode::Mod* AccuracyRuntime::getSyzziCBFMod() {
    return geode::Loader::get()->getLoadedMod(kSyzziCBFModId);
}

bool AccuracyRuntime::isSyzziCBFAvailable() {
    return getSyzziCBFMod() != nullptr;
}

void AccuracyRuntime::applyRuntimeAccuracyMode(AccuracyMode mode) {
    if (auto* gameManager = GameManager::get()) {
        gameManager->setGameVariable("0177", mode == AccuracyMode::CBS);
    }

    if (auto* cbfMod = getSyzziCBFMod()) {
        cbfMod->setSettingValue<bool>("click-on-steps", false);
        cbfMod->setSettingValue<bool>("soft-toggle", mode != AccuracyMode::CBF);
    }
}

const char* AccuracyRuntime::getAccuracyModeLabel(AccuracyMode mode) {
    switch (mode) {
        case AccuracyMode::CBS:
            return "CBS";
        case AccuracyMode::CBF:
            return "CBF";
        default:
            return "Vanilla";
    }
}
