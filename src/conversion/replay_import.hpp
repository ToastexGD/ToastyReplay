#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace toasty::conversion {

enum class ReplayImportFidelity {
    Verified,
    BestEffort,
};

struct ReplayImportResult {
    std::string outputName;
    std::filesystem::path outputPath;
    size_t inputCount = 0;
    double tps = 240.0;
    ReplayImportFidelity fidelity = ReplayImportFidelity::Verified;
    std::string message;
};

struct ReplayImportPayload {
    std::vector<uint8_t> bytes;
    size_t inputCount = 0;
    double tps = 240.0;
    ReplayImportFidelity fidelity = ReplayImportFidelity::Verified;
};

}
