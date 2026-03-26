#pragma once

#include "replay.hpp"

#include <Geode/Bindings.hpp>

namespace AccuracyRuntime {
    geode::Mod* getSyzziCBFMod();
    bool isSyzziCBFAvailable();
    void applyRuntimeAccuracyMode(AccuracyMode mode);
    const char* getAccuracyModeLabel(AccuracyMode mode);
}
