#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace toasty::noclip {
    inline double accuracyPercent(int totalFrames, int unsafeFrames) {
        if (totalFrames <= 0) {
            return 100.0;
        }

        int unsafe = std::clamp(unsafeFrames, 0, totalFrames);
        double percent = 100.0 * (1.0 - static_cast<double>(unsafe) / static_cast<double>(totalFrames));
        return std::clamp(percent, 0.0, 100.0);
    }

    inline std::string formatAccuracy(int totalFrames, int unsafeFrames, int decimalPlaces) {
        if (totalFrames <= 0) {
            return "--";
        }

        int decimals = std::clamp(decimalPlaces, 0, 6);
        int unsafe = std::clamp(unsafeFrames, 0, totalFrames);
        double percent = accuracyPercent(totalFrames, unsafe);
        if (unsafe > 0 && percent > 0.0) {
            double factor = std::pow(10.0, decimals);
            percent = std::floor(percent * factor) / factor;
        }

        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.*f%%", decimals, percent);
        return buffer;
    }
}
