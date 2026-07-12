#pragma once

#include <algorithm>
#include <cmath>

namespace toasty::start_position {
inline bool isAtLevelStart(float positionX, float levelLength) {
    if (!std::isfinite(positionX)) return false;
    float tolerance = 50.0f;
    if (std::isfinite(levelLength) && levelLength > 0.0f) {
        tolerance = std::max(tolerance, levelLength * 0.01f);
    }
    return positionX <= tolerance;
}
}
