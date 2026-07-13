#pragma once

#include <algorithm>
#include <cmath>

namespace toasty::clickaudio {
inline float volumeGain(float level) {
    if (!std::isfinite(level)) return 1.0f;
    level = std::clamp(level, 0.0f, 2.0f);
    if (level <= 1.0f) return level;
    return 1.0f + (level - 1.0f) * 3.0f;
}

inline float humanizedIntensity(float intensity, float amount) {
    if (!std::isfinite(intensity)) intensity = 1.0f;
    if (!std::isfinite(amount)) amount = 0.65f;
    return 1.0f + (intensity - 1.0f) * std::clamp(amount, 0.0f, 1.0f);
}

inline float burstGain(double secondsSincePrevious, float amount) {
    if (!std::isfinite(secondsSincePrevious) || secondsSincePrevious < 0.0) return 1.0f;
    if (!std::isfinite(amount)) amount = 0.35f;
    float proximity = 1.0f - std::clamp(static_cast<float>(secondsSincePrevious / 0.12), 0.0f, 1.0f);
    return 1.0f - proximity * std::clamp(amount, 0.0f, 1.0f) * 0.6f;
}

inline float pitchFactor(float jitter) {
    if (!std::isfinite(jitter)) return 1.0f;
    return 1.0f + std::clamp(jitter, -0.5f, 0.5f);
}

inline bool shouldUseSecondaryPack(bool separate, bool requested, bool hasSamples) {
    return separate && requested && hasSamples;
}
}
