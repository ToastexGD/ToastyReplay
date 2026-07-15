#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace toasty::conversion {

struct ReplayImportResult {
    std::string outputName;
    std::filesystem::path outputPath;
    size_t inputCount = 0;
    double tps = 240.0;
    std::string message;
};

}
