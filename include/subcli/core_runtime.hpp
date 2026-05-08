#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace subcli {

struct RuntimeStatus {
    bool hasState = false;
    bool running = false;
    int pid = 0;
    std::string target;
    std::string binaryPath;
    std::string configPath;
    std::string logPath;
    std::string startedAt;
};

bool startCoreRuntime(
    const std::filesystem::path& stateDir,
    const std::string& target,
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::string& configPath,
    const std::string& logPath,
    std::string& error
);

RuntimeStatus inspectCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, std::string& error);

bool stopCoreRuntime(const std::filesystem::path& stateDir, const std::string& target, int timeoutSec, std::string& error);

} // namespace subcli
