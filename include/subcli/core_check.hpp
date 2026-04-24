#pragma once

#include <string>

namespace subcli {

struct CoreCheckResult {
    bool ok = false;
    int exitCode = -1;
    std::string message;
};

CoreCheckResult runMihomoConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec = 30);
CoreCheckResult runSingBoxConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec = 30);
CoreCheckResult runXrayConfigCheck(const std::string& binaryPath, const std::string& configPath, int timeoutSec = 30);

} // namespace subcli
