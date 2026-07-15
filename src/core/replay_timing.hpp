#pragma once

#include "core/accuracy_mode.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace toasty::replay_timing {

enum class ExactInputDispatch {
    Immediate,
    QueueNative,
    Wait,
};

enum class QueuedInputPlayerMatch {
    Exact,
    PreferInverted,
};

struct CbfFrameInputTiming {
    int stepIndex = 0;
    double phase = 0.0;
    double offsetSeconds = 0.0;
};

inline bool sameTimestampSlice(double lhs, double rhs, double epsilon = 0.000000001) {
    if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        return lhs == rhs;
    }
    return std::abs(lhs - rhs) <= epsilon;
}

inline bool shouldSkipRepeatedProcessSlice(
    bool timedPlayback,
    int lastTick,
    int tick,
    bool hasMacro,
    int lastStepDelta,
    int stepDelta,
    double lastTimestampSlice,
    double timestampSlice
) {
    if (lastTick != tick || tick == 0 || !hasMacro) {
        return false;
    }
    if (!timedPlayback) {
        return true;
    }
    return lastStepDelta == stepDelta && sameTimestampSlice(lastTimestampSlice, timestampSlice);
}

inline bool shouldReconcileAnchorsBeforePhysics(bool stepBasedPlayback) {
    static_cast<void>(stepBasedPlayback);
    return false;
}

inline bool shouldReconcileAnchorsAfterPhysics(bool stepBasedPlayback) {
    static_cast<void>(stepBasedPlayback);
    return true;
}

inline bool shouldTrustHandleTimestampFallback(AccuracyMode mode) {
    return mode == AccuracyMode::CBS;
}

inline bool shouldScopeQueuedCallbackAsMacro(AccuracyMode mode, bool queuedMacroInput) {
    return queuedMacroInput && usesExternalCbfQueuedAccuracy(mode);
}

inline bool shouldUseSynthesizedCbfCaptureOffset(
    AccuracyMode mode,
    bool hasQueuedTimestamp,
    bool synthesisActive
) {
    return mode == AccuracyMode::CBF && !hasQueuedTimestamp && synthesisActive;
}

inline bool shouldStartCbfCaptureSynthesisFromModifiedDelta(
    AccuracyMode mode,
    bool capturing,
    double stepSeconds
) {
    return mode == AccuracyMode::CBF && capturing && std::isfinite(stepSeconds) && stepSeconds > 0.0;
}

inline bool shouldKeepCbfCaptureSynthesisAcrossProcessCommands(AccuracyMode mode) {
    return mode == AccuracyMode::CBF;
}

inline bool shouldSuppressSubstepTrail(bool substepMidStep, bool substepSuppressTrail) {
    return substepMidStep && substepSuppressTrail;
}

inline double cbfCaptureSynthesisStepSeconds(double runtimeTickRate) {
    if (!std::isfinite(runtimeTickRate) || runtimeTickRate <= 0.0) {
        return -1.0;
    }
    return 1.0 / runtimeTickRate;
}

inline double sanitizeSynthesizedCbfCaptureOffset(double elapsedSeconds, double stepSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 || !std::isfinite(stepSeconds) || stepSeconds <= 0.0) {
        return -1.0;
    }

    double maxOffset = std::max(0.0, stepSeconds * 0.999999);
    double offset = elapsedSeconds;
    if (offset >= stepSeconds) {
        offset = std::fmod(offset, stepSeconds);
    }
    return std::clamp(offset, 0.0, maxOffset);
}

inline int calculateCbfStepCount(
    double delta,
    float timewarp,
    bool physicsBypass,
    bool legacyBypass,
    double animationInterval,
    double& averageDelta,
    bool forceVanilla = false
) {
    if (!std::isfinite(delta) || delta <= 0.0) {
        return 1;
    }

    double timewarpDivisor = std::min(1.0, static_cast<double>(timewarp));
    if (!std::isfinite(timewarpDivisor) || timewarpDivisor <= 0.0) {
        timewarpDivisor = 1.0;
    }

    auto roundedStepCount = [](double value) {
        if (!std::isfinite(value) || value <= 0.0) {
            return 1;
        }
        return std::max(1, static_cast<int>(std::round(value)));
    };

    if (!physicsBypass || forceVanilla) {
        return roundedStepCount(std::max(1.0, ((delta * 60.0) / timewarpDivisor) * 4.0));
    }

    if (legacyBypass) {
        return roundedStepCount(std::max(4.0, delta * 240.0) / timewarpDivisor);
    }

    if (!std::isfinite(animationInterval) || animationInterval <= 0.0) {
        animationInterval = 1.0 / 60.0;
    }

    averageDelta = (0.05 * delta) + (0.95 * averageDelta);
    if (averageDelta > animationInterval * 10.0) {
        averageDelta = animationInterval * 10.0;
    }

    bool laggingOneFrame = animationInterval < delta - (1.0 / 240.0);
    bool laggingManyFrames = averageDelta - animationInterval > 0.0005;

    if (!laggingOneFrame && !laggingManyFrames) {
        return roundedStepCount(std::ceil((animationInterval * 240.0) - 0.0001) / timewarpDivisor);
    }
    if (!laggingOneFrame) {
        return roundedStepCount(std::ceil(averageDelta * 240.0) / timewarpDivisor);
    }
    return roundedStepCount(std::ceil(delta * 240.0) / timewarpDivisor);
}

inline std::optional<CbfFrameInputTiming> cbfFrameInputTiming(
    double inputTimestamp,
    double frameStartTimestamp,
    double frameEndTimestamp,
    int stepCount,
    double tickSeconds
) {
    if (!std::isfinite(inputTimestamp)
        || !std::isfinite(frameStartTimestamp)
        || !std::isfinite(frameEndTimestamp)
        || !std::isfinite(tickSeconds)
        || stepCount <= 0
        || tickSeconds <= 0.0
        || frameEndTimestamp <= frameStartTimestamp) {
        return std::nullopt;
    }

    double frameDelta = frameEndTimestamp - frameStartTimestamp;
    double realStepSeconds = frameDelta / static_cast<double>(stepCount);
    if (!std::isfinite(realStepSeconds) || realStepSeconds <= 0.0) {
        return std::nullopt;
    }

    double elapsed = inputTimestamp - frameStartTimestamp;
    if (elapsed >= frameDelta) {
        return std::nullopt;
    }

    CbfFrameInputTiming timing;
    if (elapsed <= 0.0) {
        return timing;
    }

    timing.stepIndex = static_cast<int>(std::floor(elapsed / realStepSeconds));
    timing.stepIndex = std::clamp(timing.stepIndex, 0, stepCount - 1);

    double stepStart = static_cast<double>(timing.stepIndex) * realStepSeconds;
    timing.phase = (elapsed - stepStart) / realStepSeconds;
    if (!std::isfinite(timing.phase)) {
        timing.phase = 0.0;
    }
    timing.phase = std::clamp(timing.phase, 0.0, 0.999999);
    timing.offsetSeconds = std::clamp(timing.phase * tickSeconds, 0.0, tickSeconds * 0.999999);
    return timing;
}

template <typename Iterator>
inline Iterator findQueuedInputMatch(
    Iterator begin,
    Iterator end,
    int button,
    bool down,
    bool player2,
    QueuedInputPlayerMatch playerMatch,
    bool allowUniqueButtonFallback
) {
    auto matchesButton = [&](auto const& queued) {
        return queued.button == button && queued.down == down;
    };
    auto findForPlayer = [&](bool wantedPlayer2) {
        return std::find_if(begin, end, [&](auto const& queued) {
            return matchesButton(queued) && queued.player2 == wantedPlayer2;
        });
    };

    if (playerMatch == QueuedInputPlayerMatch::PreferInverted) {
        auto inverted = findForPlayer(!player2);
        if (inverted != end) {
            return inverted;
        }
    }

    auto exact = findForPlayer(player2);
    if (exact != end) {
        return exact;
    }

    if (allowUniqueButtonFallback) {
        Iterator candidate = end;
        int matchCount = 0;
        for (auto it = begin; it != end; ++it) {
            if (!matchesButton(*it)) {
                continue;
            }
            candidate = it;
            ++matchCount;
            if (matchCount > 1) {
                return end;
            }
        }
        if (matchCount == 1) {
            return candidate;
        }
    }

    return end;
}

inline float exactTimeOffsetToSubstepPhase(double offset, double tickRate) {
    if (!std::isfinite(offset) || offset <= 0.0 || !std::isfinite(tickRate) || tickRate <= 0.0) {
        return 0.0f;
    }

    double phase = offset * tickRate;
    if (!std::isfinite(phase) || phase <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(phase, 0.0, 0.999999));
}

inline ExactInputDispatch classifyExactInputDispatch(
    double targetTimestamp,
    double sliceStartTimestamp,
    double sliceEndTimestamp,
    double epsilon = 0.000001
) {
    if (!std::isfinite(targetTimestamp) || !std::isfinite(sliceStartTimestamp) || !std::isfinite(sliceEndTimestamp)) {
        return ExactInputDispatch::Wait;
    }
    if (targetTimestamp <= sliceStartTimestamp + epsilon) {
        return ExactInputDispatch::Immediate;
    }
    if (targetTimestamp >= sliceEndTimestamp - epsilon) {
        return ExactInputDispatch::Wait;
    }
    return ExactInputDispatch::QueueNative;
}

inline bool shouldPreReconcileAnchorForExactInput(
    bool stepBasedPlayback,
    bool hasExactTimestamp,
    int inputTick,
    int effectiveTick,
    ExactInputDispatch dispatch
) {
    if (!stepBasedPlayback || !hasExactTimestamp || inputTick != effectiveTick) {
        return false;
    }
    return dispatch == ExactInputDispatch::QueueNative;
}

inline double playbackRuntimeTps(double macroTps, double requiredTps) {
    double result = std::isfinite(macroTps) && macroTps > 0.0 ? macroTps : 240.0;
    if (std::isfinite(requiredTps) && requiredTps > result) {
        result = requiredTps;
    }
    return result;
}

inline int materializeTickFromTime(double timeSeconds, double runtimeTps) {
    if (!std::isfinite(timeSeconds) || timeSeconds <= 0.0 || !std::isfinite(runtimeTps) || runtimeTps <= 0.0) {
        return 0;
    }
    double rawTick = timeSeconds * runtimeTps;
    double rounded = std::nearbyint(rawTick);
    if (std::abs(rawTick - rounded) < 1e-6) {
        return static_cast<int>(rounded);
    }
    return static_cast<int>(std::floor(rawTick));
}

inline double targetTimestampForPlaybackInput(
    double macroStartTimestamp,
    double tickStartTimestamp,
    double inputTimeSeconds,
    double cbsTimeOffset
) {
    if (std::isfinite(inputTimeSeconds) && inputTimeSeconds >= 0.0
        && std::isfinite(macroStartTimestamp) && macroStartTimestamp >= 0.0) {
        return macroStartTimestamp + inputTimeSeconds;
    }
    return tickStartTimestamp + std::max(0.0, cbsTimeOffset);
}

}
