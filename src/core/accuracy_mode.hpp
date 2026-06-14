#ifndef TOASTY_ACCURACY_MODE_HPP
#define TOASTY_ACCURACY_MODE_HPP

#include <cstdint>

enum class AccuracyMode : uint8_t {
    Vanilla = 0,
    CBS = 1,
    CBF = 2,
    Substep = 3,
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

inline bool usesStepBasedAccuracy(AccuracyMode mode) {
    return mode == AccuracyMode::CBS || mode == AccuracyMode::CBF;
}

inline bool usesFractionalSubstepAccuracy(AccuracyMode mode) {
    static_cast<void>(mode);
    return false;
}

inline bool externalCbfEnabledForAccuracyMode(AccuracyMode mode) {
    return mode == AccuracyMode::CBF;
}

inline bool usesReplayOwnedCbfPlayback(AccuracyMode mode) {
    return mode == AccuracyMode::CBF;
}

inline bool usesExternalCbfQueuedAccuracy(AccuracyMode mode) {
    return mode == AccuracyMode::CBF;
}

inline bool usesNativeQueuedPlaybackForAccuracyMode(AccuracyMode mode) {
    return mode == AccuracyMode::CBS;
}

inline bool externalCbfEnabledForAccuracyMode(AccuracyMode mode, bool replayOwnedCbfPlayback) {
    if (replayOwnedCbfPlayback) {
        return false;
    }
    return externalCbfEnabledForAccuracyMode(mode);
}

inline bool cbfSoftToggleForAccuracyMode(AccuracyMode mode) {
    return !externalCbfEnabledForAccuracyMode(mode);
}

inline bool nativeClickBetweenStepsForAccuracyMode(AccuracyMode mode, bool externalCbfAvailable) {
    if (mode == AccuracyMode::CBS) {
        return true;
    }
    if (mode == AccuracyMode::CBF) {
        return !externalCbfAvailable;
    }
    return false;
}

inline bool nativeClickBetweenStepsForAccuracyMode(AccuracyMode mode, bool externalCbfAvailable, bool replayOwnedCbfPlayback) {
    if (replayOwnedCbfPlayback && mode == AccuracyMode::CBF) {
        return false;
    }
    return nativeClickBetweenStepsForAccuracyMode(mode, externalCbfAvailable);
}

#endif
